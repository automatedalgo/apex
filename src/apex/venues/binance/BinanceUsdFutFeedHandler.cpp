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
#include <apex/util/TimeLog.hpp>
#include <simdjson/simdjson.h>

#define BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC 300

namespace apex {

struct ParserImpl {
  simdjson::ondemand::parser parser;
};

BinanceUsdFutFeedHandler::BinanceUsdFutFeedHandler(Services* services,
                                                   RunMode run_mode,
                                                   Reactor* reactor,
                                                   RealtimeEventLoop* event_loop,
                                                   FeedHandlerCallbacks callbacks)
  : FeedHandlerImpl(ExchangeId::binance_usdfut,
                    services,
                    run_mode, reactor,
                    event_loop),
    _callbacks(std::move(callbacks)),
    _impl{std::make_unique<ParserImpl>()}
{
  _callbacks.assert_all_defined();

  if (auto mcap = _services->message_capture_service()) {
    std::tie(_ws_msgcap_id_in, _ws_msgcap_id_out)
      = mcap->register_stream_id_pair("binanceusdfut-feed");
  }

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
          mcap->push_event(_ws_msgcap_id_out, request);
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

  if (websock_is_open(_ws_feed)) {
    // in an attempt to keep Binance from disconnect/stale, send pings
    if ((Time::realtime_now() - _time_last_ping) > std::chrono::minutes(2)) {
      _time_last_ping = Time::realtime_now();
      _ws_feed->send_ping();
    }
    return;
  }

  _ws_feed = connect_websocket(
    _feed_url,
    "binance-ufut-mktdata",
    _reactor,
    _services->ssl(),
    _event_loop,  // TODO: why is this needed?
    [this](const char* buf, size_t n){ this->process_raw_message(buf, n); });

  _time_last_ping = Time::realtime_now();

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


void BinanceUsdFutFeedHandler::process_raw_message(const char* buf, size_t n)
{
  /* io-thread */

  _ws_feed->timelog().at_message.mark();

  simdjson::error_code error;

  // note: buf must have already been padded by SIMDJSON_PADDING bytes
  simdjson::ondemand::document doc = _impl->parser.iterate(buf, n);

  try {
    simdjson::ondemand::object root;
    error = doc.get_object().get(root);
    if (error)
      throw error;

    std::string_view stream;
    error = root["stream"].get(stream);
    if (error == simdjson::error_code::NO_SUCH_FIELD)
      return;
    if (error)
      throw error;

    simdjson::ondemand::object data;
    error = root["data"].get(data);
    if (error)
      throw error;

    auto pos = stream.find('@');
    if (pos == std::string_view::npos)
      return;

    std::string feed_sym = std::string{stream.substr(0, pos)};
    std::string_view msg_type = stream.substr(pos + 1);

    if (msg_type == "aggTrade") {
      auto [tick, err] = parse_binanceusdfut_aggtrade(data);
      if (!err) {
        _ws_feed->timelog().at_parsed.mark();
        _callbacks.on_trade(feed_sym, tick, _ws_feed->timelog());
      }
      else {
        LOG_WARN("failed to parse aggTrade");
      }
    }
    else if (msg_type == "bookTicker") {
      auto [tick, err] = parse_binanceusdfut_bookticker(data);
      if (!err) {
        _ws_feed->timelog().at_parsed.mark();
        _callbacks.on_top(feed_sym, tick, _ws_feed->timelog());
      }
      else {
        LOG_WARN("failed to parse bookTicker");
      }
    }
  }
  catch (simdjson::error_code ec) {
    LOG_WARN("json parse error: " << simdjson::error_message(ec));
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
