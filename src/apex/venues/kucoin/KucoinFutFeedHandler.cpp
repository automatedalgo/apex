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

#include <apex/venues/kucoin/KucoinFutFeedHandler.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/net/WebsocketClient.hpp>
#include <apex/net/ssl.hpp>

#include <curl/curl.h>


namespace apex {


KucoinFutFeedHandler::KucoinFutFeedHandler(Services* services,
                                           RunMode run_mode,
                                           Reactor* reactor,
                                           RealtimeEventLoop* event_loop,
                                           FeedHandlerCallbacks callbacks)
  : FeedHandlerImpl(ExchangeId::kucoin_fut,
                    services,
                    run_mode, reactor,
                    event_loop),
    _callbacks(std::move(callbacks)),
    _ping_interval_ms(0)
{
  _callbacks.assert_all_defined();
  _connector_thread = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("kcnfutfeedcx");
    });
}

KucoinFutFeedHandler::~KucoinFutFeedHandler() = default;

// TODO: this is duplicated across files
static size_t write_callback(void* content, size_t size, size_t nmemb,
                             void* user)
{
  size_t const realsize = size * nmemb;
  std::string* result = reinterpret_cast<std::string*>(user);
  result->append((char*)content, realsize);
  return realsize;
}


void KucoinFutFeedHandler::get_public_token()
{
  /* management thread */

  _server_url = "";
  _token = "";

  std::string error;
  std::string result;
  CURL *curl;
  CURLcode res;
  curl = curl_easy_init();
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, "https://api-futures.kucoin.com/api/v1/bullet-public");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    struct curl_slist *headers = NULL;
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    res = curl_easy_perform(curl);

    /* Check for errors */
    if (res != CURLE_OK) {
      std::ostringstream oss;
      oss << "http-request failed, error '" << curl_easy_strerror(res) << "'";
      error = oss.str();
    }

  }
  curl_easy_cleanup(curl);

  auto msg = json::parse(result);

  _token = msg["data"]["token"].get<std::string>();
  _server_url = msg["data"]["instanceServers"][0]["endpoint"].get<std::string>();
  _ping_interval_ms = msg["data"]["instanceServers"][0]["pingInterval"].get<int>();
}


void KucoinFutFeedHandler::manage_connection()
{
  if (_ws_feed && _ws_feed->is_open())
    return;

  get_public_token();

  if (_server_url == "")
    return;

  auto url = concat(_server_url,
                    "?token=",
                    _token,
                    "&connectId=",
                    "apex");

  SslSocket::Options options;
  options.sni_policy = SslSocket::Options::use_addr;

  try {
    _ws_feed = connect_websocket(url, "kcnfutfeed",
                                 _services->reactor(),
                                 _services->ssl(),
                                 _event_loop,
                                 [this](const char* buf, size_t n){
                                   this->process_raw_message(buf, n);
                                 },
                                 options);

    // schedule a redo of subscriptions now that we have reconnected
    auto wp = this->weak_from_this();

    _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
      if (auto sp = wp.lock())
        sp->do_subscriptions();
      return std::chrono::seconds{0};
    });


    if (_ping_interval_ms > 0) {
      // Note: we set up a reocurring ping callback, that stores the weak
      // pointer actual websocket just created, so that we only actual if that
      // websocket is still active.
      auto ws_weak_ptr = _ws_feed->weak_from_this();
      auto ping_interval = std::chrono::milliseconds{_ping_interval_ms};
      _connector_thread -> dispatch(ping_interval, [ws_weak_ptr, ping_interval]() {
        if (auto ws_sp = ws_weak_ptr.lock()) {
          auto msg = R"({"id":"apex","type":"ping"})";
          ws_sp->send(msg);
          return ping_interval;
        }
        return std::chrono::milliseconds{0};
      });
    }

  }
  catch (const std::exception & e) {
    LOG_ERROR(e.what());
  }

}


void KucoinFutFeedHandler::process_raw_message(const char* buf, size_t n)
{
  /* io-thread */

  _ws_feed->timelog().at_message.mark();

  try {
    auto msg = json::parse(buf, buf + n);


    const std::string * type = get_ptr<std::string>(msg, "type");
    if (!type) {
      LOG_WARN("kucoin message ignored, has no 'type': " << msg);
      return;
    }

    if (*type == "message") {

      const std::string * subject = get_ptr<std::string>(msg, "subject");
      if (!subject) {
        LOG_WARN("kucoin message ignored, has no subject: " << msg);
        return;
      }

      auto data = msg.find("data");
      if (data == msg.end()) {
        LOG_WARN("kucoin message ignored, has no payload: " << msg);
        return;
      }

      if (*subject == "match") {
        process_trade(*data);
        return;
      }
      else {
        throw std::runtime_error(concat("kucoin message subject not handled: ", *subject));
      }
    }

    if (*type == "pong")
      return;
    if (*type == "welcome")
      return;
    if (*type == "ack")
      return;

    LOG_WARN("kucoin unhandled message type: '" << *type << "'");
  }
  catch (const std::exception & e) {
    LOG_ERROR("kucoin message parse failed, message: " << e.what());
  }
}


void KucoinFutFeedHandler::process_trade(json& msg) {
  const auto & symbol = get_string_field(msg, "symbol");
  const auto & side = get_string_field(msg, "side");
  unsigned long ts_ms = get_field<unsigned long>(msg, "ts") / 1000;

  Side aggr_side = Side::none;
  if (side == "sell")
    aggr_side = Side::sell;
  else if (side == "buy")
    aggr_side = Side::buy;

  TickTrade tick;
  tick.price = std::stod(get_string_field(msg, "price"));
  tick.qty = get_field<long>(msg, "size");
  tick.xt = Time(std::chrono::microseconds(ts_ms));
  tick.et = Time(std::chrono::microseconds(ts_ms));
  tick.side = aggr_side;

  _ws_feed->timelog().at_parsed.mark();
  _callbacks.on_trade(symbol, tick, _ws_feed->timelog());
}


void KucoinFutFeedHandler::subscribe_trades(std::string feed_symbol)
{
  feed_symbol = str_toupper(feed_symbol);

  {
    std::lock_guard<std::mutex> lock(_subs_mtx);
    int sub_id = std::size(_subs)+1;
    auto stream = concat(feed_symbol, "@trades");
    auto msg = concat(
      R"({"id":)",
      sub_id,
      R"(,"type":"subscribe","topic":"/contractMarket/execution:)",
      feed_symbol,
      R"(","response":true})");
    _subs[stream] = Subscription{sub_id, msg, false};
  }

  auto wp = this->weak_from_this();
  _connector_thread -> dispatch(std::chrono::seconds{1}, [wp]() {
    if (auto sp = wp.lock())
      sp->do_subscriptions();
    return std::chrono::seconds{0};
  });
}


void KucoinFutFeedHandler::subscribe_top(std::string)
{
}




void KucoinFutFeedHandler::do_subscriptions()
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


void KucoinFutFeedHandler::start()
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
