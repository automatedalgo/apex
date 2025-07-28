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
#include <apex/venues/binance/BinanceUsdFutFeedHandler.hpp>
#include <apex/venues/binance/binance_common.hpp>

#define BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC 300

namespace apex {

BinanceUsdFutFeedHandler::BinanceUsdFutFeedHandler(Services* services,
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

  // if (auto msgcap = _services->message_capture_service()) {
  //   _ws_feed_msgcap_id = msgcap->register_stream_id_pair("binance-ufut-mktdata");
  // }

  _connector_thread = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("binufutfeedcx");
    });

  _feed_url = "wss://fstream.binance.com:443/stream";
}

BinanceUsdFutFeedHandler::~BinanceUsdFutFeedHandler() = default;

// TODO: need to allow subscriptions to come after has started
void BinanceUsdFutFeedHandler::subscribe_trades(std::string feed_symbol)
{

  // TODO: register the subscription
  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    int sub_id = std::size(_subs)+1;
    std::string stream = feed_symbol + "@aggTrade";

    if (_subs.find(stream) != _subs.end())
      return;

    std::ostringstream oss;
    oss << R"({"method": "SUBSCRIBE","params":[")"
        << stream
        << R"("],"id": )"
        << sub_id
        << "}";

    _subs[stream] = Subscription{sub_id, oss.str(), false};
  }

  auto wp = this->weak_from_this();
  _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
    if (auto sp = wp.lock())
      sp->do_subscriptions();
    return std::chrono::seconds{0};
  });

}


void BinanceUsdFutFeedHandler::subscribe_top(std::string symbol)
{
  auto feed_symbol = str_tolower(symbol);

  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    int sub_id = std::size(_subs)+1;
    std::string stream = concat(feed_symbol, "@bookTicker");

    std::ostringstream oss;
    oss << R"({"method": "SUBSCRIBE","params":[")"
        << stream
        << R"("],"id": )"
        << sub_id
        << "}";

    _subs[stream] = Subscription{sub_id, oss.str(), false};
  }

  auto wp = this->weak_from_this();
  _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
    if (auto sp = wp.lock())
      sp->do_subscriptions();
    return std::chrono::seconds{0};
  });

}


void BinanceUsdFutFeedHandler::do_subscriptions()
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
          mcap->push_event(_ws_feed_msgcap_id_out, request);
        }
        usleep(BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC * 1000);
        _ws_feed->send(request);
        sub.second.active = true;
      }
  }
}


void BinanceUsdFutFeedHandler::manage_connection()
{
  /* feed management thread */

  if (websock_is_open(_ws_feed))
      return;

  _ws_feed = connect_websocket(
    _feed_url,
    "binance-ufut-mktdata",
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


void BinanceUsdFutFeedHandler::process_bookticker(std::string pxsym, json& msg)
{
  /* io-thread */

  TickTop tick;

  tick.xt = from_binance_timestamp(get_uint(msg, "T"));
  tick.et = from_binance_timestamp(get_uint(msg, "E"));
  tick.ask_price = std::stod(get_string_field(msg, "a"));
  tick.ask_qty = std::stod(get_string_field(msg, "A"));
  tick.bid_price = std::stod(get_string_field(msg, "b"));
  tick.bid_qty = std::stod(get_string_field(msg, "B"));
  _callbacks.on_top(pxsym, tick);
}


void BinanceUsdFutFeedHandler::process_aggtrade(std::string pxsym, json& msg)
{
  /* io-thread */

  TickTrade tick;
  tick.xt = from_binance_timestamp(get_uint(msg, "T"));
  tick.et = from_binance_timestamp(get_uint(msg, "E"));
  tick.price = std::stod(get_string_field(msg, "p"));
  tick.qty = std::stod(get_string_field(msg, "q"));
  tick.side = buyer_market_maker_to_aggrSide(get_bool(msg, "m"));

  _callbacks.on_trade(pxsym, tick);
}


void BinanceUsdFutFeedHandler::process_raw_message(const char* buf, size_t n)
{
  /* io-thread */

  // if (auto mcap = _services->message_capture_service()) {
  //   mcap->push_event(_ws_feed_msgcap_id.in, std::string_view(buf, n));
  // }

  auto msg = json::parse(buf, buf + n);

  try {
    if (msg.is_object()) {

      if (auto data = msg.find("data"); data != msg.end()) {
        auto & stream = get_field<std::string>(msg, "stream");

        size_t pos = stream.find('@');
        if (pos != std::string::npos) {
          std::string feed_sym = stream.substr(0, pos);
          auto & msg_type = get_field<std::string>(*data, "e");

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
    LOG_ERROR("process message failed, " << e.what()
              << ", data: " << std::string_view(buf, n));
  }
}


void BinanceUsdFutFeedHandler::start()
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
