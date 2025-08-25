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

#include <apex/core/Logger.hpp>
#include <apex/core/Services.hpp>
#include <apex/infra/WebsocketClient.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/venues/binance/BinanceFeedHandler.hpp>
#include <apex/venues/binance/binance_common.hpp>

#define BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC 300

namespace apex {


BinanceFeedHandler::BinanceFeedHandler(Services* services,
                                                   RunMode run_mode,
                                                   Reactor* reactor,
                                                   RealtimeEventLoop* event_loop,
                                                   FeedHandlerCallbacks callbacks)
  : FeedHandlerImpl(ExchangeId::binance_usdfut,
                    services,
                    run_mode, reactor,
                    event_loop),
    _callbacks(std::move(callbacks))
{
  _callbacks.assert_all_defined();

  if (auto mcap = _services->message_capture_service()) {
    std::tie(_ws_msgcap_id_in, _ws_msgcap_id_out)
      = mcap->register_stream_id_pair("binance-feed");
  }


  _connector_thread = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("binufutfeedcx");
    });

  _feed_url = "wss://stream.binance.com:9443/stream";
}

BinanceFeedHandler::~BinanceFeedHandler() = default;

// TODO: need to allow subscriptions to come after has started
void BinanceFeedHandler::subscribe_trades(std::string feed_symbol)
{
  /* called by user thread */

  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    std::string stream = feed_symbol + "@aggTrade";
    if (_subs.find(stream) != _subs.end())
      return;
    int sub_id = std::size(_subs)+1;

    auto request = concat(R"({"method":"SUBSCRIBE","params":[")",
                          stream,
                          R"("],"id":)",
                          sub_id,
                          "}");

    _subs[stream] = Subscription{sub_id, std::move(request), false};
  }

  auto wp = this->weak_from_this();
  _connector_thread -> dispatch_after(
    std::chrono::milliseconds{250},
    [wp]() {
      if (auto sp = wp.lock())
        sp->do_subscriptions();
    });
}


void BinanceFeedHandler::subscribe_top(std::string feed_symbol)
{
  /* called by user thread */

  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    std::string stream = concat(feed_symbol, "@bookTicker");
    if (_subs.find(stream) != _subs.end())
      return;
    int sub_id = std::size(_subs)+1;

    auto request = concat(R"({"method":"SUBSCRIBE","params":[")",
                          stream,
                          R"("],"id":)",
                          sub_id,
                          "}");

    _subs[stream] = Subscription{sub_id, std::move(request), false};
  }

  auto wp = this->weak_from_this();
  _connector_thread -> dispatch_after(std::chrono::milliseconds{250}, [wp]() {
    if (auto sp = wp.lock())
      sp->do_subscriptions();
  });

}


void BinanceFeedHandler::do_subscriptions()
{
  /* management thread */

  if (websock_is_open(_ws_feed)) {
    std::lock_guard<std::mutex> lock(_subs_mtx);

    // TODO: need to put a delay, to prevent exceeding connection threshold,
    // however, don't want to sleep holding the lock, instead request another
    // manage connection callback in say 1 second?

    // send subscriptions
    for (auto & sub : _subs)
      if (!sub.second.active) {
        std::string request = sub.second.request;
        LOG_INFO("sending subscription: " << request);
        if (auto mcap = _services->message_capture_service()) {
          mcap->push_event(_ws_msgcap_id_out, request);
        }
        usleep(BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC * 1000);
        _ws_feed->send(request);
        sub.second.active = true;
      }
  }
}


void BinanceFeedHandler::manage_connection()
{
  /* feed management thread */

  if (websock_is_open(_ws_feed))
      return;

  _ws_feed = connect_websocket(
    _feed_url,
    "binance-mktdata",
    _reactor,
    _services->ssl(),
    _event_loop,  // TODO: why is this needed?
    [this](const char* buf, size_t n){ this->process_raw_message(buf, n); });


  // clear the subscription states
  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    for (auto & sub : _subs) {
      sub.second.active = false;
    }
  }

  // schedule a redo of subscriptions now that we have reconnected
  auto wp = this->weak_from_this();
  _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
    if (auto sp = wp.lock())
      sp->do_subscriptions();
    return std::chrono::seconds{0};
  });
}

void BinanceFeedHandler::process_raw_message(const char* buf, size_t n)
{
  /* io-thread */

  _ws_feed->timelog().at_message.mark();

  // if (auto mcap = _services->message_capture_service()) {
  //   mcap->push_event(_ws_msgcap_id.in, std::string_view(buf, n));
  // }

  auto msg = json::parse(buf, buf + n);

  try {
    if (msg.is_object()) {
      if (auto data = msg.find("data"); data != msg.end()) {
        auto & stream = get_field<std::string>(msg, "stream");
        size_t pos = stream.find('@');
        if (pos != std::string::npos) {
          std::string_view feed_sym = std::string_view(stream).substr(0, pos);
          std::string_view msg_type = std::string_view(stream).substr(pos+1);
          // auto & msg_type = get_field<std::string>(*data, "e");

          if (msg_type == "aggTrade") {
            process_aggtrade(feed_sym, *data);
            return;
          }
          if (msg_type == "bookTicker") {
            process_bookticker(feed_sym, *data);
            return;
          }
        }
      }

    if (msg.contains("id"))
      return;  // ignore
    }
    else
      throw std::runtime_error("expected JSON object");

    throw std::runtime_error(concat("message not handled: ",
                                    std::string_view(buf, n)));
  }
  catch (std::exception& e) {
    LOG_WARN("process message failed, " << e.what()
             << ", message: " << std::string_view(buf, n));
  }
}

void BinanceFeedHandler::process_bookticker(std::string_view feed_sym, json& msg)
{
  /* io-thread */

  TickTop tick;

  // tick.xt -- Binance doesn't have 'T' field
  // tick.et  -- Binance doesn't have 'E' field
  tick.ask_price = std::stod(get_string_field(msg, "a"));
  tick.ask_qty = std::stod(get_string_field(msg, "A"));
  tick.bid_price = std::stod(get_string_field(msg, "b"));
  tick.bid_qty = std::stod(get_string_field(msg, "B"));

  std::string tmp(feed_sym); // TODO: try replace all callbacks with string_view
  _ws_feed->timelog().at_parsed.mark();
  _callbacks.on_top(tmp, tick, _ws_feed->timelog());
}


void BinanceFeedHandler::process_aggtrade(std::string_view feed_sym, json& msg)
{
  /* io-thread */

  TickTrade tick;

  tick.xt = from_binance_timestamp(get_uint(msg, "T"));
  tick.et = from_binance_timestamp(get_uint(msg, "E"));
  tick.price = std::stod(get_string_field(msg, "p"));
  tick.qty = std::stod(get_string_field(msg, "q"));
  tick.side = buyer_market_maker_to_aggrSide(get_bool(msg, "m"));

  _ws_feed->timelog().at_parsed.mark();
  std::string tmp(feed_sym); // TODO: try replace all callbacks with string_view
  _callbacks.on_trade(tmp, tick, _ws_feed->timelog());
}


void BinanceFeedHandler::start()
{
  using namespace std::chrono_literals;
  auto wp = weak_from_this();
  _connector_thread -> dispatch(1s, [wp]() {
    if (auto sp = wp.lock())
      sp->manage_connection();
    return 5s;
  });
}


}
