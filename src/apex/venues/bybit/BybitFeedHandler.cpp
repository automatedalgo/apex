/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/

#include <apex/venues/bybit/BybitFeedHandler.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/net/WebsocketClient.hpp>
#include <apex/net/ssl.hpp>

#include <curl/curl.h>


namespace apex {


//static const char* spot_url = "wss://stream.bybit.com/v5/public/spot";
static const char* usdfut_url = "wss://stream.bybit.com/v5/public/linear";
//static const char* coinfut_url = "wss://stream.bybit.com/v5/public/inverse";


Time parse_time (unsigned long ms) {
  return Time(std::chrono::milliseconds(ms));
}

Side parse_side(std::string_view sv)
{
  return (sv=="Buy")? Side::buy : ((sv == "Sell")? Side::sell : Side::none);
}


ByBitFeedHandler::ByBitFeedHandler(Services* services,
                                           RunMode run_mode,
                                           Reactor* reactor,
                                           RealtimeEventLoop* event_loop,
                                           FeedHandlerCallbacks callbacks)
  : FeedHandlerImpl(ExchangeId::bybit,
                    services,
                    run_mode,
                    reactor,
                    event_loop),
    _callbacks(std::move(callbacks))
{
  _callbacks.assert_all_defined();

  _connector_thread = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("bybitfeedcx");
    });
}

ByBitFeedHandler::~ByBitFeedHandler() = default;


void ByBitFeedHandler::manage_connection()
{
  if (_ws_feed && _ws_feed->is_open())
    return;


  SslConfig ssl_config(true);
  SslContext ssl_context(ssl_config);
  SslSocket::Options ssl_options;
  try {
    _ws_feed = connect_websocket(usdfut_url, "bybitfeed",
                                 _services->reactor(),
                                 &ssl_context,
                                 _event_loop,
                                 [this](const char* buf, size_t n){
                                   this->process_raw_message(buf, n);
                                 },
                                 ssl_options,
                                 1024*1024  // 1MB
      );

    // schedule a redo of subscriptions now that we have reconnected
    auto wp = this->weak_from_this();

    _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
      if (auto sp = wp.lock())
        sp->do_subscriptions();
      return std::chrono::seconds{0};
    });

    // Note: we set up a reocurring ping callback, that stores the weak pointer
    // actual websocket just created, so that we only actual if that websocket
    // is still active.
    auto ws_weak_ptr = _ws_feed->weak_from_this();
    auto ping_interval = std::chrono::seconds{20}; // 20s bybit recommended
    _connector_thread->dispatch(ping_interval, [ws_weak_ptr, ping_interval]() {
      if (auto ws_sp = ws_weak_ptr.lock()) {
        auto msg = R"({"req_id":"0","op":"ping"})";
        ws_sp->send(msg);
        return ping_interval;
      }
      return decltype(ping_interval){0};
    });

  }
  catch (const std::exception & e) {
    LOG_ERROR(e.what());
  }
}

void ByBitFeedHandler::process_raw_message(const char* buf, size_t n)
{
  /* io-thread */
  _ws_feed->timelog().at_message.mark();

  static std::string_view pubtrade("publicTrade.");
  try {
    auto msg = json::parse(buf, buf + n);


    const std::string * topic = get_ptr<std::string>(msg, "topic");

    if (topic && strncmp(topic->c_str(), pubtrade.data(), pubtrade.size())==0) {

      auto data = msg.find("data");
      if (data !=  msg.end()) {
        std::string_view sym = topic->substr(pubtrade.size());

        // LOG_INFO("TRADE: " << msg) ;

        process_trade(sym,
                      parse_time(get_field<unsigned long>(msg, "ts")),
                      *data);
      }
      return;

    }


    const std::string * op = get_ptr<std::string>(msg, "op");

    if (op) {
      if (*op == "ping")
        return;

      if (*op == "subscribe") {
        // TODO: warn if "success":false , and log full message
      }
    }



    LOG_INFO(msg);

    return;




    const std::string * type = get_ptr<std::string>(msg, "type");
    if (!type) {
      LOG_WARN("bybit message ignored, has no 'type': " << msg);
      return;
    }

    if (*type == "message") {

      const std::string * subject = get_ptr<std::string>(msg, "subject");
      if (!subject) {
        LOG_WARN("bybit message ignored, has no subject: " << msg);
        return;
      }

      auto data = msg.find("data");
      if (data == msg.end()) {
        LOG_WARN("bybit message ignored, has no payload: " << msg);
        return;
      }

      if (*subject == "match") {
        //process_trade(*data);
        return;
      }
      else {
        throw std::runtime_error(concat("bybit message subject not handled: ", *subject));
      }
    }

    if (*type == "pong")
      return;
    if (*type == "welcome")
      return;
    if (*type == "ack")
      return;

    LOG_WARN("bybit unhandled message type: '" << *type << "'");
  }
  catch (const std::exception & e) {
    LOG_ERROR("bybit message parse failed, message: " << e.what());
  }
}


void ByBitFeedHandler::process_trade(std::string_view symbol,
                                     Time et,
                                     json& msg) {
  TickTrade aggr;
  aggr.side = Side::none;
  aggr.price = 0;
  aggr.type = TradeType::aggr;


  // LOG_INFO(msg);
  for (json::iterator it = msg.begin(); it != msg.end(); ++it) {
    TickTrade tick;
    tick.price = std::stod(get_string_field(*it, "p"));
    tick.qty = std::stod(get_string_field(*it, "v"));
    tick.et = et;
    tick.xt = parse_time(get_field<unsigned long>(*it, "T"));
    tick.side = parse_side(get_string_field(*it, "S"));

    // can disable this after testing.
    if (symbol != get_string_field(*it, "s")) {
      LOG_ERROR("program error: individual trade symbols don't match subscription");
    }

    /* --- trade aggregation --- */

    if (aggr.price == 0 && aggr.side == Side::none) {
      // our aggragation is empty, so seed from current tick
      aggr = tick;
    }
    else {
      // we have a current aggregated trade, so see if we can continue to
      // aggregate with the current trade
      if (aggr.side == tick.side &&
          aggr.xt == tick.xt &&
          aggr.price == tick.price) {
        aggr.qty += tick.qty;
      }
      else {
        // stop aggregation, so publish current aggr trade, and reseed
        _ws_feed->timelog().at_parsed.mark();
        _callbacks.on_trade(std::string(symbol), aggr, _ws_feed->timelog());
         aggr = tick;
      }
    }
  }

  // publish any final aggregated trade that exist after the loop
  if (aggr.price != 0 && aggr.side != Side::none) {
    _ws_feed->timelog().at_parsed.mark();
    _callbacks.on_trade(std::string(symbol), aggr, _ws_feed->timelog());
  }
}


void ByBitFeedHandler::subscribe_trades(std::string feed_symbol)
{
  feed_symbol = str_toupper(feed_symbol);

  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    int sub_id = std::size(_subs)+1;
    auto stream = concat(feed_symbol, "@trades");
    auto msg = concat(R"({"op": "subscribe","args": ["publicTrade.)",
                      feed_symbol,
                      R"("]})");
    _subs[stream] = Subscription{sub_id, msg, false};
  }

  auto wp = this->weak_from_this();
  _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
    if (auto sp = wp.lock())
      sp->do_subscriptions();
    return std::chrono::seconds{0};
  });
}


void ByBitFeedHandler::subscribe_top(std::string)
{
}




void ByBitFeedHandler::do_subscriptions()
{
  /* management thread */

  if (_ws_feed && _ws_feed->is_open()) {
    std::lock_guard<std::mutex> lock(_subs_mtx);

    // TODO: need to put a delay, to prevent exceeding connection threshold,
    // however, don't want to sleep holding the lock, instead request another
    // manage connection callback in say 1 second?

    // send subscriptions
    for (auto & sub : _subs)
      if (!sub.second.active) {
        std::string request = sub.second.request;
        LOG_INFO("sending subscription: " << request);
        // if (auto mcap = _services->message_capture_service()) {
        //   mcap->push_event(_ws_feed_msgcap_id_out, request);
        // }
        _ws_feed->send(request);
        sub.second.active = true;
      }
  }
}


void ByBitFeedHandler::start()
{
  using namespace std::chrono_literals;
  _connector_thread -> dispatch(1s, [wp=weak_from_this()]() {
    if (auto sp = wp.lock()) {
      sp->manage_connection();
      return 0s;
    }
    else
      return 5s;
  });
}

} // namepace
