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

#include <apex/gx/BinanceSession.hpp>
#include <apex/core/Errors.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/infra/SslSocket.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/infra/WebsocketClient.hpp>
#include <apex/util/json.hpp>
#include <apex/util/platform.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/util/Config.hpp>

#include <filesystem>
#include <sstream>
#include <unistd.h>

#include <curl/curl.h>

namespace apex
{
namespace binance
{

// As for the Binance Websocket Limits, the limit is 5 incoming messages per
// second (200ms), so, we translate that a required interval delay with some
// safety overhead.
#define BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC 300


static const std::string CLIENT_ORDER_ID = "c";
static const std::string EVENT_TIME = "E";
static const std::string EVENT_TYPE = "e";
static const std::string EXECUTION_TYPE = "x";
static const std::string ORDER_ID = "i";
static const std::string ORDER_STATUS = "X";
static const std::string ORIG_CLIENT_ORDER_ID = "C";
static const std::string LAST_EXECUTED_PRICE = "L";
static const std::string LAST_EXECUTED_QUANTITY = "l";


enum class EventType { none, account_update, order_update };

namespace OrderState
{
static const std::string NEW = "NEW";
static const std::string PARTIALLY_FILLED = "PARTIALLY_FILLED";
static const std::string FILLED = "FILLED";
static const std::string CANCELED = "CANCELED";
static const std::string PENDING_CANCEL = "PENDING_CANCEL";
static const std::string REJECTED = "REJECTED";
static const std::string EXPIRED = "EXPIRED";
}; // namespace OrderState


enum class ExecType {
  accepted, // this is the 'new' execution type
  canceled,
  replaced,
  rejected,
  trade,
  expired,
};

static EventType to_event_type(const std::string& s)
{
  if (s == "outboundAccountPosition")
    return EventType::account_update;
  else if (s == "executionReport")
    return EventType::order_update;
  else
    return EventType::none;
}

static ExecType to_exec_type(const std::string& s)
{
  if (s == "NEW")
    return ExecType::accepted;
  else if (s == "CANCELED")
    return ExecType::canceled;
  else if (s == "REPLACED")
    return ExecType::replaced;
  else if (s == "REJECTED")
    return ExecType::rejected;
  else if (s == "TRADE")
    return ExecType::trade;
  else if (s == "EXPIRED")
    return ExecType::expired;
  else
    THROW_PARSE_ERROR("unknown execution-type '" << s << "'");
}


struct ErrorReply {

  bool is_error = false;
  std::string code;
  std::string text;

  explicit ErrorReply(json& msg)
  {
    {
      auto iter = msg.find("code");
      if (iter != std::end(msg)) {
        code = std::to_string(iter->get<signed int>());
        is_error = true;
      }
    }
    {
      auto iter = msg.find("msg");
      if (iter != std::end(msg)) {
        text = iter->get<std::string>();
        is_error = true;
      }
    }
  }
};


static long build_timestamp()
{
  auto tv = apex::time_now();
  return tv.sec * 1000 + tv.usec / 1000;
}


// TODO: this is duplicated across files
static size_t write_callback(void* content, size_t size, size_t nmemb,
                             void* user)
{
  size_t const realsize = size * nmemb;
  std::string* result = reinterpret_cast<std::string*>(user);
  result->append((char*)content, realsize);
  return realsize;
}


const char* to_binance(OrderType ot)
{
  switch (ot) {
    case OrderType::limit:
      return "LIMIT";
    case OrderType::market:
      return "MARKET";
    default:
      throw std::runtime_error("invalid OrderType");
  }
};


const char* to_binance(Side s)
{
  switch (s) {
    case Side::buy:
      return "BUY";
    case Side::sell:
      return "SELL";
    default:
      throw std::runtime_error("invalid Side");
  }
}


std::string to_binance(TimeInForce tif)
{
  switch (tif) {
    case TimeInForce::gtc:
      return "GTC";
    case TimeInForce::ioc:
      return "IOC";
    case TimeInForce::fok:
      return "FOK";
    default:
      return "?";
  }
}

std::string serialise_msg(json msg)
{
  std::ostringstream oss;
  oss << msg;
  return oss.str();
}


json construct_mock_new_order_reply(std::string client_order_id, double price,
                                    double size)
{
  std::string rawstr =
      R"({"clientOrderId":"ABC1000001","cummulativeQuoteQty":"0.00000000","executedQty":"0.00000000","fills":[],"orderId":4820506962,"orderListId":-1,"origQty":"0.00139000","price":"35942.81000000","side":"BUY","status":"NEW","symbol":"BTCBUSD","timeInForce":"GTC","transactTime":1650981370115,"type":"LIMIT"})";

  auto msg = json::parse(rawstr);
  msg["clientOrderId"] = std::move(client_order_id);
  msg["origQty"] = format_double(size, true, 8);
  msg["price"] = format_double(price, true, 8);
  msg["_mock"] = true;


  return msg;
}


json construct_mock_cancel_order_ack(std::string client_order_id)
{
  std::string rawstr =
      R"({"clientOrderId":"kcs1S6wvPnYgas3BaFHzHF","cummulativeQuoteQty":"0.00000000","executedQty":"0.00000000","orderId":5053959693,"orderListId":-1,"origClientOrderId":"ABC2000012","origQty":"0.00177000","price":"28292.72000000","side":"BUY","status":"CANCELED","symbol":"BTCBUSD","timeInForce":"GTC","type":"LIMIT"})";

  auto msg = json::parse(rawstr);
  msg["origClientOrderId"] = std::move(client_order_id);
  msg["_mock"] = true;

  return msg;
}


json construct_mock_unsol_cancel(std::string client_order_id) {
  std::string raw = R"({"C":"XYZ1631131880000","E":1662071187756,"F":"0.00000000","I":12197634369,"L":"0.00000000","M":false,"N":null,"O":1662071176951,"P":"0.00000000","Q":"0.00000000","S":"BUY","T":1662071187756,"X":"CANCELED","Y":"0.00000000","Z":"0.00000000","c":"web_608d3f703c16463cb99ef0314e68387d","e":"executionReport","f":"GTC","g":-1,"i":5896154397,"l":"0.00000000","m":false,"n":"0","o":"LIMIT","p":"19894.70000000","q":"0.00527000","r":"NONE","s":"BTCBUSD","t":-1,"w":false,"x":"CANCELED","z":"0.00000000"})";
  auto msg = json::parse(raw);
  msg[binance::ORIG_CLIENT_ORDER_ID] = std::move(client_order_id);
  msg["_mock"] = true;
  return msg;
}


json construct_mock_fill(std::string client_order_id, double executed_price,
                         double executed_size, bool is_fully_filled)
{
  /*
  {
      "C":"",
      "E":1650880969417,
      "F":"0.00000000",
      "I":9933907061,
      "L":"38638.48000000",    // Last executed price
      "M":true,
      "N":
      "BTC",
      "O":1650880969417,
      "P":"0.00000000",
      "Q":"0.00000000",
      "S":"BUY",
      "T":1650880969417,
      "X":"FILLED",     // Current order status
      "Y":"49.84363920",
      "Z":"49.84363920",
      "c":"TEST000002",  //  client order id
      "e":"executionReport",
      "f":"GTC",
      "g":-1,
      "i":4815055021,       // Order ID
      "l":"0.00129000",    // Last executed quantity
      "m":false,
      "n":"0.00000129",
      "o":"LIMIT",
      "p":"38640.74000000",
      "q":"0.00129000",
      "r":"NONE",
      "s":"BTCBUSD",  // symbol
      "t":337745930,
      "w":false,
      "x":"TRADE",      // Current execution type -- TRADE?
      "z":"0.00129000"
  }
  */

  std::string rawstr =
      R"({"C":"","E":1650880969417,"F":"0.00000000","I":9933907061,"L":"38638.48000000","M":true,"N":"BTC","O":1650880969417,"P":"0.00000000","Q":"0.00000000","S":"BUY","T":1650880969417,"X":"FILLED","Y":"49.84363920","Z":"49.84363920","c":"TEST000002","e":"executionReport","f":"GTC","g":-1,"i":4815055021,"l":"0.00129000","m":false,"n":"0.00000129","o":"LIMIT","p":"38640.74000000","q":"0.00129000","r":"NONE","s":"BTCBUSD","t":337745930,"w":false,"x":"TRADE","z":"0.00129000"})";

  auto msg = json::parse(rawstr);
  msg["c"] = std::move(client_order_id);
  msg["l"] = format_double(executed_size, true, 8);
  msg["L"] = format_double(executed_price, true, 8);
  if (!is_fully_filled)
    msg["X"] = "PARTIALLY_FILLED";
  msg["_mock"] = true;
  return msg;
}


} // namespace binance
} // namespace apex

namespace apex
{


// subscribe: BTCUSDT / streamtype, stream-options

Time from_binance_timestamp(json::number_unsigned_t i)
{
  int ms = i % 1000;
  int sec = (i - ms) / 1000;
  return Time{sec, std::chrono::milliseconds(ms)};
}

BinanceSession::Params build_params(Config config) {
  BinanceSession::Params params;
  params.raw_capture_dir = config.get_string("raw_capture_dir", "");
  params.api_key_file = config.get_string("api_key_file", "");
  return params;
}

BinanceSession::BinanceSession(BaseExchangeSession::EventCallbacks callbacks,
                               Config& config, RunMode run_mode, IoLoop* ioloop,
                               RealtimeEventLoop& event_loop, SslContext* ssl)
  : BinanceSession(callbacks, build_params(config),  run_mode, ioloop, event_loop, ssl)
{
}

const std::string& get_string_field(const json& msg, const std::string& key,
                                    const std::string& default_value)
{
  auto iter = msg.find(key);
  if (iter == std::end(msg)) {
    return default_value;
  }

  if (!iter->is_string())
    THROW_PARSE_ERROR("wrong field type; field '"
                      << key << "'; expected 'string', actual '"
                      << iter->type_name() << "'");

  return iter->get_ref<const std::string&>();
}


BinanceSession::BinanceSession(BaseExchangeSession::EventCallbacks callbacks,
                               Params config, RunMode run_mode, IoLoop* ioloop,
                               RealtimeEventLoop& event_loop, SslContext* ssl)
  : ExchangeSession(std::move(callbacks), ExchangeId::binance,
                    run_mode, ioloop,
                    event_loop, ssl)
{
  if (ssl == nullptr)
    throw std::runtime_error("SslContext cannot be none for BinanceSession");

  _raw_capture_dir = config.raw_capture_dir;
  if (!_raw_capture_dir.empty())
    create_dir(_raw_capture_dir);

  std::filesystem::path secrets_file = config.api_key_file;

  if (run_mode == RunMode::live)
  {
    if (secrets_file.empty()) {
      throw std::runtime_error("Binance API key file not provided, but is required for live run-mode");
    }
    else {
      auto obj = json::parse(slurp(secrets_file.native().c_str()));
      std::string name = get_string_field(obj, "name", "NONAME");
      _user_api_key = obj["key"].get<std::string>();
      _user_api_secret = obj["secret"].get<std::string>();
      LOG_INFO("user api key file read");
    }
  }

  // we have a separate event-loop for curl, because the requests are
  // synchronous
  _curl_requests.dispatch(
      []() { Logger::instance().register_thread_id("curl"); });
}



void BinanceSession::start()
{
  using namespace std::chrono_literals;

  auto wp = weak_from_this();
  _event_loop.dispatch(1s, [wp]() {
    if (auto sp = wp.lock())
      sp->manage_connection();
    return 5s;
  });
}


Side buyer_market_maker_to_aggrSide(bool buyer_is_maker)
{
  if (buyer_is_maker)
    return Side::sell;
  else
    return Side::buy;
}


void BinanceSession::subscribe_top(Symbol symbol, subscription_options,
                                   std::function<void(TickTop)> callback)
{
  // Note: this is called from user-thread

  if (!callback)
    throw std::runtime_error("subscribe_top() provided with a none callback");

  const int subscribe_id = _next_id++;

  Subscription sub;
  sub.id = subscribe_id;
  std::string stream = str_tolower(symbol.native) + "@bookTicker";

  sub.build_request = [stream, subscribe_id]() -> std::string {
    std::ostringstream oss;
    oss << "{\"method\": \"SUBSCRIBE\", \"params\": [" << '"' << stream << '"'
        << "], \"id\":" << subscribe_id << "}";
    return oss.str();
  };

  auto wp = weak_from_this();


  sub.handler = [wp, callback](json msg) {
    if (auto sp = wp.lock()) {
      if (auto data = msg.find("data"); data != msg.end()) {
        if (data->is_object()) {
          TickTop tick;
          tick.ask_price = std::stod(get_string_field(*data, "a"));
          tick.ask_qty = std::stod(get_string_field(*data, "A"));
          tick.bid_price = std::stod(get_string_field(*data, "b"));
          tick.bid_qty = std::stod(get_string_field(*data, "B"));
          callback(tick);
        }
      }
    }
  };

  {
    auto lock = std::scoped_lock(m_subscriptions_mtx);
    if (m_subscriptions.find(stream) == std::end(m_subscriptions)) {
      m_subscriptions.insert({stream, std::move(sub)});
      run_on_evloop(
          [](BinanceSession* self) { self->make_pending_subscriptions(); });
    }
  }
}


void BinanceSession::subscribe_trades(Symbol sym, subscription_options,
                                      std::function<void(TickTrade)> callback)
{
  // Note: this is called from user-thread

  if (!callback)
    throw std::runtime_error(
        "subscribe_trades() provided with a none callback");



  // TODO: reject duplicate; ah, but, could be a new client? But still,
  // only want a single subscription on the exchange side

  // TODO: create localised subscription request

  Subscription sub;
  const int subscribe_id = _next_id++;
  sub.id = subscribe_id;
  std::string stream = str_tolower(sym.native) + "@aggTrade";

  sub.build_request = [stream, subscribe_id]() -> std::string {
    std::ostringstream oss;
    oss << "{\"method\": \"SUBSCRIBE\", \"params\": [" << '"' << stream << '"'
        << "], \"id\": " << subscribe_id << " }";
    return oss.str();
  };

  auto wp = weak_from_this();
  sub.handler = [wp, callback, sym](json msg) {
    if (auto sp = wp.lock()) {
      if (auto data = msg.find("data"); data != msg.end()) {
        if (data->is_object()) {

          TickTrade tick;
          tick.xt = from_binance_timestamp(get_uint(*data, "T"));
          tick.et = from_binance_timestamp(get_uint(*data, "E"));
          tick.price = std::stod(get_string_field(*data, "p"));
          tick.qty = std::stod(get_string_field(*data, "q"));
          tick.aggr_side = buyer_market_maker_to_aggrSide(get_bool(*data, "m"));
          callback(tick);
        }
      }
    }
  };

  {
    auto lock = std::scoped_lock(m_subscriptions_mtx);
    if (m_subscriptions.find(stream) == std::end(m_subscriptions)) {
      m_subscriptions.insert({stream, std::move(sub)});
      run_on_evloop(
          [](BinanceSession* self) { self->make_pending_subscriptions(); });
    }
  }
}


void BinanceSession::retry_connect_market_data_stream()
{
  assert(is_event_thread());

  if (not _mktdata_stream) {
    LOG_INFO("attempting binance-spot market-data stream connection");

    auto wp = weak_from_this();

    auto on_down = [wp]() {
      /* io-thread */
      if (auto sp = wp.lock())
        sp->run_on_evloop(
            [](BinanceSession* self) {
              self->on_mktdata_websocket_down();
            });
    };

    auto on_msg = [wp](json j) mutable {
      /* io-thread */
      if (auto sp = wp.lock()) {
        sp->run_on_evloop([j](BinanceSession* self) {
          self->on_websocket_msg(std::move(j));
        });
      }
    };

    try {
      auto ws =
          open_websocket("binance market-data channel", _params.md_host,
                         _params.md_port, _params.md_path, on_down, on_msg);
      if (ws) {
        run_on_evloop(
            [ws](BinanceSession* self) { self->on_websocket_up(ws); });
      }
    } catch (const std::runtime_error& e) {
      LOG_WARN("failed to establish binance market-data websocket, "
               << e.what());
    }
  }
}


void BinanceSession::retry_connect_user_data_stream()
{
  assert(is_event_thread());

  // note: in mock mode, we dont attempt connection to the user-data stream
  if (!_user_stream && !is_paper_trading()) {
    LOG_INFO("attempting binance-spot user-data connection");

    std::string listen_key;
    {
      {
        auto lock = std::scoped_lock(_listen_key_lock);
        listen_key = _listen_key;
      }

      if (!listen_key.empty() && _params.user_port) {
        std::string path = _params.user_path + "/" + listen_key;

        auto on_down = [this]() {
          /* io-thread */
          this->run_on_evloop([](BinanceSession*) {
            LOG_WARN("websocket lost to exchange");
          });
        };

        auto on_msg = [this](json j) mutable {
          /* io-thread */
          this->run_on_evloop([j](BinanceSession* self) {
            self->on_userdata_msg(std::move(j));
          });
        };

        try {
          auto ws =
              open_websocket("binance-user-data channel", _params.user_host,
                             _params.user_port, path, on_down, on_msg);
          if (ws) {
            run_on_evloop([ws](BinanceSession* self) {
              self->on_user_data_stream_up(ws);
            });
          }
        } catch (const std::runtime_error& e) {
          LOG_WARN("failed to establish websocket, " << e.what());
        }
      }

      // TODO: also check the age of it here, and dont assign to outer
      // listen_key if too old
    }
  }
}


void BinanceSession::manage_connection()
{
  assert(is_event_thread());

  bool is_up = eval_connection_state();

  if (is_up) {
    if (_service_state != ServiceState::connected) {
      _service_state = ServiceState::connected;
      LOG_INFO("*** binance-spot up" << (is_paper_trading() ? " (paper-trading-mode)" : "")
                                     << " ***");
    }
  } else {
    if (_service_state == ServiceState::connected) {
      _service_state = ServiceState::reseting;
    }
  }

  switch (_service_state) {
    case ServiceState::reseting: {
      _mktdata_stream.reset();
      _user_stream.reset();

      LOG_INFO("*** binance-spot reconnecting ***");
      _service_state = ServiceState::connecting;
      break;
    }
    case ServiceState::connecting: {
      refresh_user_stream_listen_key();
      retry_connect_user_data_stream();
      break;
    }
    case ServiceState::connected: {
      refresh_user_stream_listen_key();
      ping_market_data();
      retry_connect_market_data_stream();
    }
  }


  // refresh_account();
}


void BinanceSession::ping_market_data()
{
  assert(is_event_thread());
  auto path = endpoint("/api/v3/ping");

  auto on_result = [this](std::string, std::string error) mutable {
    if (not error.empty()) {
      LOG_ERROR("cannot ping binance-spot market data server");
      // TODO: should we reset the websocket?
    }
  };

  this->http_request(HttpRequestType::get, _params.api_endpoint, path, {}, {},
                     on_result);
}


bool BinanceSession::eval_connection_state()
{
  if (!is_paper_trading()) {
    if (!_user_stream)
      return false;
    if (_user_stream && !_user_stream->is_open())
      return false;
  }

  return true;
}


void BinanceSession::check_connection_state()
{
  bool is_up = eval_connection_state();

  if (is_up) {
    if (_service_state != ServiceState::connected) {
      _service_state = ServiceState::connected;
    }
  } else {
    if (_service_state == ServiceState::connected) {
      LOG_INFO("REQUESTING RESET");
      _service_state = ServiceState::reseting;
    }
  }
}


void BinanceSession::on_user_data_stream_up(std::shared_ptr<WebsocketClient> ws)
{
  assert(is_event_thread());
  _user_stream = std::move(ws);

  LOG_INFO("binance-spot user-data channel connected");

  check_connection_state();
}


std::shared_ptr<apex::WebsocketClient> BinanceSession::open_websocket(
    std::string streamname, std::string host, int port, std::string path,
    std::function<void()> on_down, std::function<void(json)> on_msg)
{
  LOG_INFO(streamname << ": attempting websocket connection to '" << host << ":"
                      << port << path << "'");

  std::unique_ptr<TcpSocket> sock(new SslSocket(*_ssl, *_ioloop));
  auto fut = sock->connect(host, port);

  if (fut.wait_for(std::chrono::milliseconds(4000)) !=
      std::future_status::ready)
    throw std::runtime_error("timeout during connect");

  if (UvErr ec = fut.get())
    throw std::runtime_error(
        "connect failed: " + std::to_string(ec.os_value()) + ", " +
        ec.message());

  auto msg_cb = [on_msg](const char* buf, size_t len) {
    /* io-thread */
    on_msg(json::parse(buf, buf + len));
  };


  auto completion_promise = std::make_shared<std::promise<void>>();

  auto on_open = [&] {
    /* io-thread */
    completion_promise->set_value();
  };

  auto on_error = on_down;

  std::shared_ptr<WebsocketClient> ws = std::make_shared<WebsocketClient>(
    _event_loop, std::move(sock), path, msg_cb, on_open, on_error);

  {
    // wait for the websocket to become open
    auto fut = completion_promise->get_future();
    if (fut.wait_for(std::chrono::milliseconds(4000)) !=
        std::future_status::ready)
      throw std::runtime_error("timeout during websocket initiation");
  }

  LOG_INFO(streamname << ": websocket established");
  return ws;
}


void BinanceSession::on_mktdata_websocket_down()
{
  LOG_WARN("binance-spot market-data websocket down");
  _mktdata_stream.reset();
}


void BinanceSession::on_websocket_msg(json msg)
{
  assert(is_event_thread());

  std::ostringstream oss;
  oss << msg;

  if (msg.is_object()) {

    if (auto istream = msg.find("stream"); istream != msg.end()) {
      auto stream = istream->get<std::string>();

      if (auto iter = m_subscriptions.find(stream);
          iter != std::end(m_subscriptions)) {
        iter->second.handler(msg);
      } else {
        LOG_ERROR("no handler set up for stream update '" << stream << "'");
      }
    } else if (auto iter = msg.find("id"); iter != msg.end()) {
      LOG_INFO("binance websocket subscription started. Message:" << msg);
    } else {
      LOG_ERROR("binance websocket not recognised. Message:" << msg);
    }
  } else {
    LOG_ERROR("binance json message expected to be json object: " << msg);
  }
}


void BinanceSession::on_websocket_up(std::shared_ptr<WebsocketClient> ws)
{
  assert(is_event_thread());
  _mktdata_stream = std::move(ws);
  LOG_INFO("binance-spot market-data channel connected");

  // We need to reissue all subscriptions, so, set them all to inactive to allow
  // that to happen.

  {
    auto lock = std::scoped_lock(m_subscriptions_mtx);
    for (auto& item : m_subscriptions)
      item.second.is_requested = false;
  }

  make_pending_subscriptions();
}


void BinanceSession::make_pending_subscriptions()
{
  assert(is_event_thread());

  if (not _mktdata_stream)
    return;

  auto lock = std::scoped_lock(m_subscriptions_mtx);

  for (auto& item : m_subscriptions) {
    Subscription& sub = item.second;

    if (!sub.is_requested) {
      sub.is_requested = true;
      std::string request = sub.build_request();
      LOG_INFO("sending binance subscribe request for '"
               << item.first << "', request: " << request);

      _mktdata_stream->send(request.c_str(), request.size());

      _event_loop.dispatch(
        std::chrono::milliseconds(BINANCE_SPOT_SUBSCRIBE_DELAY_MILLISEC),
        [weak{this->weak_from_this()}]() -> std::chrono::milliseconds {
          if (auto sp = weak.lock())
            sp->make_pending_subscriptions();
          return std::chrono::milliseconds{0};
        }
        );

      // we only make 1 request per call to make_pending_subscriptions; other
      // requests happen on delayed invocations of this function, so as to not
      // breach the Binance incoming-message-rate limit.
      return;
    }
  }
}


void BinanceSession::refresh_user_stream_listen_key()
{
  if (is_paper_trading())
    return;

  if (_params.user_port) {
    /* manage the user data listenKey */
    bool request_listenKey = false;

    {
      auto lock = std::scoped_lock(_listen_key_lock);
      if (_listen_key.empty()) {
        request_listenKey = true;
      } else {
        auto age = std::chrono::steady_clock::now() - _listen_key_created;
        if (age > std::chrono::minutes(30))
          request_listenKey = true;
      }
    }

    if (request_listenKey) {
      std::string path = "/api/v3/userDataStream";

      std::vector<std::string> headers;
      headers.push_back("X-MBX-APIKEY: " + _user_api_key);

      LOG_INFO("requesting binance-spot user_stream listenKey");
      LOG_INFO("making http POST request to '" << _params.api_endpoint << path << "'");
      this->http_request(
          HttpRequestType::post, _params.api_endpoint, path, {},
          std::move(headers), [this](std::string result, std::string error) {
            if (error.empty()) {
              try {
                this->on_user_data_stream_reply(json::parse(result));
              } catch (...) {
                log_message_exception("on_user_data_stream_reply", result);
              }
            } else {
              LOG_ERROR("http error when requesting user listenKey: " << error);
            }
          });
    }
  }
}


void BinanceSession::on_user_data_stream_reply(json msg)
{
  LOG_INFO("on_user_data_stream_reply: " << msg);
  assert(is_event_thread());
  auto* listenKey = get_ptr<std::string>(msg, "listenKey");
  if (listenKey) {
    {
      auto lock = std::scoped_lock(_listen_key_lock);
      _listen_key = *listenKey;
      _listen_key_created = std::chrono::steady_clock::now();
    }
    LOG_INFO("obtained binance user data listenKey '" << *listenKey << "'");
  } else {
    LOG_ERROR("listenKey missing in binance response; raw msg: " << msg);
  }
}


void BinanceSession::http_request(
    HttpRequestType type, std::string endpoint, std::string path,
    std::string post_data, std::vector<std::string> headers,
    std::function<void(std::string, std::string)> on_result)
{
  if (endpoint.empty())
    throw std::runtime_error(
        "empty endpoint provided for HTTP request, path '" + path + "'");

  if (!on_result)
    throw std::runtime_error("on_result callback cannot be none");

  std::string url = endpoint + path;

  auto fn = [this, url, type, post_data, headers = std::move(headers),
             on_result]() {
    LOG_DEBUG("URL:" << url);
    CURL* curl = curl_easy_init();

    std::string result;
    std::string error;

    if (curl) {
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, binance::write_callback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

      if (type == HttpRequestType::put ||
          type == HttpRequestType::post ||
          type == HttpRequestType::del) {
        if (type == HttpRequestType::del)
          curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

        if (type == HttpRequestType::put)
          curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
      }

      if (type == HttpRequestType::get) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
      }

      struct curl_slist* list = nullptr;
      if (!headers.empty()) {
        for (auto& header : headers)
          list = curl_slist_append(list, header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
      }

      CURLcode res = curl_easy_perform(curl);

      /* Check for errors */
      if (res != CURLE_OK) {
        std::ostringstream oss;
        oss << "http-request failed, error '" << curl_easy_strerror(res) << "'";
        error = oss.str();
      }

      /* always cleanup */
      curl_easy_cleanup(curl);
      curl_slist_free_all(list);
    }

    _event_loop.dispatch(
        [on_result, result, error]() { on_result(result, error); });
  };
  _curl_requests.dispatch(std::move(fn));
}


void BinanceSession::refresh_account()
{
  std::string api_path = "/api/v3/account";

  // build the url request portion that embeds the request parameters
  std::ostringstream oss;
  oss << "recvWindow=" << _params.recv_window
      << "&timestamp=" << binance::build_timestamp();
  auto params = oss.str();

  /* calculate the hmac-sha256 signature of the request parameters */
  std::string digest = HMACSHA256_base4(_user_api_secret.c_str(), _user_api_secret.size(),
                                        params.c_str(), params.size());

  /* create the signed request */
  std::string request = api_path + "?" + params + "&signature=" + digest;

  /* request headers */
  std::vector<std::string> headers;
  headers.push_back("X-MBX-APIKEY: " + _user_api_key);

  auto wp = weak_from_this();
  this->http_request(
      HttpRequestType::get, _params.api_endpoint, request, {},
      std::move(headers), [wp](std::string result, std::string error) {
        if (error.empty()) {
          try {
            if (auto sp = wp.lock())
              sp->on_account_reply(std::move(result));
          } catch (...) {
            log_message_exception("on account request", result);
          }
        } else {
          LOG_ERROR("http error when requesting user accounty: " << error);
        }
      });
}


void BinanceSession::http_get_account_information()
{
  std::string api_path = "/api/v3/account";

  // build the url request portion that embeds the request parameters
  std::ostringstream oss;
  oss << "recvWindow=" << _params.recv_window
      << "&timestamp=" << apex::binance::build_timestamp();
  auto params = oss.str();

  /* calculate the hmac-sha256 signature of the request parameters */
  std::string digest = HMACSHA256_base4(_user_api_secret.c_str(), _user_api_secret.size(),
                                        params.c_str(), params.size());

  /* create the signed request */
  std::string request = api_path + "?" + params + "&signature=" + digest;

  /* request headers */
  std::vector<std::string> headers;
  headers.push_back("X-MBX-APIKEY: " + _user_api_key);

  auto wp = weak_from_this();
  this->http_request(
      HttpRequestType::get, _params.api_endpoint, request, {},
      std::move(headers), [wp](std::string result, std::string error) {
        if (error.empty()) {
          try {
            if (auto sp = wp.lock())
              sp->on_account_reply(std::move(result));
          } catch (...) {
            log_message_exception("on account request", result);
          }
        } else {
          LOG_ERROR("http error when requesting user accounty: " << error);
        }
      });
}


void BinanceSession::subscribe_account(
    std::function<void(std::vector<AccountUpdate>)> callback)
{
  if (!callback)
    throw std::runtime_error(
        "subscribe_account() provided with empty callback");

  auto wp = weak_from_this();
  _event_loop.dispatch([wp, callback]() {
    if (auto sp = wp.lock()) {

      if (!sp->_account_stream) {
        sp->_account_stream = std::make_unique<AccountStream>(callback);
        sp->http_get_account_information();
        // now we can issue the POST request
      } else {
        LOG_WARN(
            "Ignoring repeated call to BinanceSession::subscribe_account(");
      }

      // TODO: if this is the first time, then maybe we need to start with the
      // snapshot request, followed by the updates. Or, perhaps just assume that
      // there is no further calls to this method.
      //
      //
    }
  });
}

void BinanceSession::on_account_reply(std::string /*raw_response*/)
{
  // TODO: need to handle case where the result is a json string that indicates
  // an error, eg, such as can happen if we get the HMAC signature incorrect.

  // if (!_account_stream)
  //   return;

  // auto msg = json::parse(raw_response);

  // // convert from the binance-json to a normalised type
  // std::vector<AccountUpdate> updates;
  // if (msg.is_object()) {
  //   auto& balances = apex::get_array(msg, "balances");
  //   for (const auto& item : balances) {
  //     if (item.is_object()) {
  //       auto field = apex::get_string_field(item, "asset");
  //       auto qty = std::stod(apex::get_string_field(item, "free"));
  //       Asset asset;
  //       asset.symbol = field;
  //       //asset.exchange = exchangeId();
  //       AccountUpdate au{asset, qty};
  //       updates.push_back(std::move(au));
  //     }
  //   }
  // }
  // _account_stream->on_update(updates);
}


void BinanceSession::cancel_order(std::string symbol, std::string order_id,
                                  std::string ext_order_id,
                                  SubmitOrderCallbacks callbacks)
{
  if (is_paper_trading()) {
    LOG_WARN("cancel-order not valid for paper-trading");
    return;
  }

  auto path = "/api/v3/order";
  auto timestamp = binance::build_timestamp();

  /* build the request body */

  std::ostringstream oss;
  oss << "symbol=" << str_toupper(symbol) << "&orderId=" << ext_order_id
      << "&recvWindow=" << _params.recv_window << "&timestamp=" << timestamp;
  std::string body = oss.str();

  /* obtain the hmac-sha256 signature*/

  std::string digest = HMACSHA256_base4(_user_api_secret.c_str(), _user_api_secret.size(),
                                        body.c_str(), body.size());

  /* create the signed request body */
  std::string post_data;
  post_data = body + "&signature=" + digest;

  std::vector<std::string> headers;
  headers.push_back("X-MBX-APIKEY: " + _user_api_key);

  auto on_result = [this, callbacks](std::string result, std::string error) {
    this->on_cancel_order_reply(result, error, callbacks);
  };

  this->http_request(HttpRequestType::del, _params.api_endpoint, path,
                     post_data, std::move(headers), on_result);
}


void BinanceSession::submit_order(OrderParams params,
                                  SubmitOrderCallbacks callbacks)
{
  if (is_paper_trading()) {
    LOG_WARN("submit-order not valid for paper-trading");
    return;
  }

  auto path = endpoint("/api/v3/order");

  auto tv = apex::time_now();
  auto timestamp = tv.sec * 1000 + tv.usec / 1000;

  /* build the request body */
  std::ostringstream oss;
  oss << "symbol=" << str_toupper(params.symbol)
      << "&side=" << binance::to_binance(params.side)
      << "&type=" << binance::to_binance(params.order_type)
      << "&timeInForce=" << binance::to_binance(params.time_in_force)
      << "&quantity=" << format_double(params.size, true, 8)
      << "&newClientOrderId=" << params.order_id
      << "&price=" << format_double(params.price, true)
      << "&recvWindow=" << _params.recv_window << "&timestamp=" << timestamp;
  std::string body = oss.str();

  /* obtain the hmac-sha256 signature*/

  std::string digest = HMACSHA256_base4(_user_api_secret.c_str(), _user_api_secret.size(),
                                        body.c_str(), body.size());

  /* create the signed request body */
  std::string post_data;
  post_data = body + "&signature=" + digest;

  LOG_DEBUG("POST-DATA:" << post_data);

  std::vector<std::string> headers;
  headers.push_back("X-MBX-APIKEY: " + _user_api_key);

  auto on_result = [this, callbacks](std::string result, std::string error) {
    if (error.empty()) {
      try {
        this->on_new_order_reply(std::move(result), callbacks);
      } catch (...) {
        std::string code;
        log_message_exception("on_new_order_reply", result);
        callbacks.on_rejected(error::e0102, "unknown reason");
      }
    } else {
      callbacks.on_rejected(error::e0102, error);
    }
  };

  this->http_request(HttpRequestType::post, _params.api_endpoint, path,
                     post_data, std::move(headers), on_result);
}


std::string BinanceSession::endpoint(std::string url)
{
  return _params.use_test ? url + "/test" : std::move(url);
}


void BinanceSession::on_cancel_order_reply(std::string result,
                                           std::string error,
                                           SubmitOrderCallbacks callbacks)
{
  assert(is_event_thread());
  // LOG_INFO("on_cancel_order_reply, result:" << result << ", error: " << error);

  if (error.empty()) {
    try {

      if (!_raw_capture_dir.empty())
        write_json_message(_raw_capture_dir, "cancel_order_reply", result);

      auto msg = json::parse(result);

      binance::ErrorReply error(msg);
      if (error.is_error) {
        callbacks.on_rejected(error.code, error.text);
      } else {
        OrderUpdate update;
        update.state = OrderState::closed;
        update.close_reason = OrderCloseReason::cancelled;
        callbacks.on_reply(update);
      }
    } catch (...) {
      log_message_exception("on_cancel_order_reply", result);
      callbacks.on_rejected(error::e0103, "unknown reason");
    }
  } else {
    callbacks.on_rejected(error::e0103, error);
  }
}

void BinanceSession::on_new_order_reply(std::string raw,
                                        SubmitOrderCallbacks callbacks)
{
  assert(is_event_thread());

  if (!_raw_capture_dir.empty())
    write_json_message(_raw_capture_dir, "new_order_reply", raw);

  OrderUpdate update;

  // TODO: handle case of json parse error here
  auto reply = json::parse(raw);

  {
    binance::ErrorReply error(reply);
    if (error.is_error) {
      callbacks.on_rejected(error.code, error.text);
      return;
    }
  }

  auto status = reply["status"].get<std::string>();
  auto iter = reply.find("orderId");
  if (iter != std::end(reply))
    update.ext_order_id = std::to_string(iter->get<long>());

  if (status == "NEW") {
    update.state = OrderState::live;
  } else if (status == "PARTIALLY_FILLED") {
    // TODO: handle any partial fills
    update.state = OrderState::live;
  } else if (status == "FILLED") {
    update.state = OrderState::live;
    update.close_reason = OrderCloseReason::filled;
  } else if (status == "CANCELED") {
    update.state = OrderState::closed;
    update.close_reason = OrderCloseReason::cancelled;
  } else if (status == "PENDING_CANCEL") {
    // TODO: need a state for this?
    // order->set_is_canceling(now());
  } else if (status == "REJECTED") {
    update.state = OrderState::closed;
    update.close_reason = OrderCloseReason::rejected;
  } else if (status == "EXPIRED") {
    update.state = OrderState::closed;
    update.close_reason = OrderCloseReason::lapsed;
  } else {
    LOG_WARN("unhandled on_new_order_reply, raw-msg: " + raw);
  }

  callbacks.on_reply(update);
}


void BinanceSession::on_userdata_msg(json msg)
{
  assert(is_event_thread());

  if (!_raw_capture_dir.empty()) {
    std::ostringstream oss;
    oss << msg;
    write_json_message(_raw_capture_dir, "userdata", oss.str());
  }

  LOG_DEBUG("Binance::on_userdata_msg: " << msg);

  auto event_type_str = get_string_field(msg, binance::EVENT_TYPE);
  auto event_type = binance::to_event_type(event_type_str);

  switch (event_type) {
    case binance::EventType::none: {
      LOG_WARN("unhandled binance user-data message: " << msg);
      break;
    }

    case binance::EventType::account_update: {
      // Not currently processing account updates
      break;
    }

    case binance::EventType::order_update: {
      auto exec_type_str = get_string_field(msg, binance::EXECUTION_TYPE);
      auto exec_type = binance::to_exec_type(exec_type_str);

      switch (exec_type) {

        case binance::ExecType::replaced:
        case binance::ExecType::rejected:
        case binance::ExecType::accepted: {
          // These are update types are being ignored because instead we respond
          // to the REST request, ie, immediate order accept / reject.
          break;
        }

        case binance::ExecType::expired:
        case binance::ExecType::canceled: {
          const auto& orig_order_id =
            get_string_field(msg, binance::ORIG_CLIENT_ORDER_ID);
          OrderUpdate update;
          update.state = OrderState::closed;
          update.close_reason = OrderCloseReason::lapsed;
          _callbacks.on_order_cancel(*this, orig_order_id, update);
          break;
        };

        case binance::ExecType::trade: {
          const auto& order_id = get_string_field(msg, binance::CLIENT_ORDER_ID);
          auto& order_status = get_string_field(msg, binance::ORDER_STATUS);
          OrderFill fill;
          fill.is_fully_filled = (order_status == binance::OrderState::FILLED);
          fill.size =
              std::stod(get_string_field(msg, binance::LAST_EXECUTED_QUANTITY));
          fill.price =
              std::stod(get_string_field(msg, binance::LAST_EXECUTED_PRICE));
          fill.recv_time = Time::realtime_now();
          _callbacks.on_order_fill(*this, order_id, fill);
          break;
        }
        default: {
          LOG_WARN("unhandled Binance order-update: " << msg);
          break;
        }
      }

      break;
    }
    default: {
      LOG_WARN("unhandled Binance user-data message: " << event_type_str);
      break;
    }
  }
}

} // namespace apex
