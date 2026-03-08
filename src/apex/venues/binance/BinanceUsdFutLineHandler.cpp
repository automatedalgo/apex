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

#include <apex/core/Core.hpp>
#include <apex/core/Logger.hpp>
#include <apex/net/WebsocketClient.hpp>
#include <apex/util/JsonWriter.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/venues/binance/BinanceUsdFutLineHandler.hpp>
#include <apex/venues/binance/binance_common.hpp>

#include <apache/base64.h> // from 3rdparty
#include <sodium.h>


namespace apex {



static std::string sign_message(const std::string& payload,
                                const std::string& seed_hex) {

  // convert seed hex string to bytes
  unsigned char seed[crypto_sign_SEEDBYTES];
  sodium_hex2bin(seed, sizeof(seed), seed_hex.c_str(), seed_hex.size(),
                 NULL, NULL, NULL);

  // generate Ed25519 keypair from seed
  unsigned char public_key[crypto_sign_PUBLICKEYBYTES];
  unsigned char secret_key[crypto_sign_SECRETKEYBYTES];
  crypto_sign_seed_keypair(public_key, secret_key, seed);

  // create signature (in raw bytes)
  unsigned char signature[crypto_sign_BYTES];
  unsigned long long sig_len;

  crypto_sign_detached(
    signature,
    &sig_len,
    reinterpret_cast<const unsigned char*>(payload.c_str()),
    payload.size(),
    secret_key
    );

  auto temp_len = ::ceil((sig_len * 8) / 24) * 4;

  // convert signature to base64
  char tmp[150] = {0};   // assert is >= 88

  assert(sizeof(tmp) > temp_len);
  ap_base64encode(tmp, reinterpret_cast<char*>(signature), sig_len);

  return tmp;
}



BinanceUsdFutLineHandler::BinanceUsdFutLineHandler(
  Core* core,
  Reactor* reactor,
  RealtimeEventLoop* event_loop,
  LineHandlerCallbacks callbacks,
  OrderRouterConfig config)
  : _core(core),
    _event_loop(event_loop),
    _reactor(reactor),
    _ssl(_core->ssl()),
    _callbacks(callbacks),
    _config(config)
{
  _callbacks.assert_all_defined();

  assert(!_core->is_backtest());

  // TODO: allow these to come from config
  _line_url = "wss://ws-fapi.binance.com:443/ws-fapi/v1?returnRateLimits=false";
  _feed_url = "wss://fstream.binance.com:443/ws";

  // load secrets
  std::filesystem::path secrets_file = config.api_key_file;
  auto obj = json::parse(slurp(secrets_file.native().c_str()));

  _apikey = obj["key"].get<std::string>();
  _seedhex = obj["seed"].get<std::string>(); // TODO: allow other form, eg PEM

  int uat_mode = 0;
  if (uat_mode) {
    _line_url = "ws://127.0.0.1:9000/ws-fapi/v1?returnRateLimits=false";
    _feed_url = "ws://127.0.0.1:9001/ws";
    _apikey = "sErnzoWaTESThtDlKTHISISATESTfWppp7t34vOOszl8wNTEST3feMyGv5SjSsLv";
  }

  if (auto mcap = _core->message_capture_service()) {

    std::tie(_ws_line_msgcap_id_in, _ws_line_msgcap_id_out)
      = mcap->register_stream_id_pair("binance-ufut-line");

    std::tie(_ws_feed_msgcap_id_in, _ws_feed_msgcap_id_out)
      = mcap->register_stream_id_pair("binance-ufut-feed");
  }

  _connector_thread = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("binufutlinecx");
    });
}


bool BinanceUsdFutLineHandler::is_open() const
{
  return apex::websock_is_open(_ws_feed) && apex::websock_is_open(_ws_line);
}


void BinanceUsdFutLineHandler::subscribe_user_stream(std::string listenkey)
{
  if (_ws_feed && _ws_feed->is_open()) {
    LOG_INFO("subscribing to user-stream");

    std::stringstream oss;
    oss << R"({"method":"SUBSCRIBE","params":[")"
        << listenkey
        << R"("],"id":"user_stream_sub"})";

    auto msg = oss.str();
    if (auto mcap = _core->message_capture_service()) {
      mcap->push_event(_ws_feed_msgcap_id_out, msg);
    }
    _ws_feed ->send(msg);
  }
}


void BinanceUsdFutLineHandler::process_order_trade_update(json& msg)
{
  /* io-thread */

  // we respone to some ORDER_TRADE_UPDATE messages, ones that tell us an order
  // has lapsed
  auto o_iter = msg.find("o");
  if (o_iter != msg.end()) {
      // TODO:  look for order expired indication


//    auto iii = find_field_str(*o_iter, "x"); // check exists and is string
    // std::string *  execution_type = find_field_str(*o_iter, "x");


    auto x_iter = o_iter->find("x");
    // check_x_iter is string
    // now get the value
    //const auto & execution_type = x_iter->get_ref<const std::string&>();


    const std::string& execution_type =  get_string_field(*o_iter, "x");
    // const std::string& order_status = find_field_ref_str (*o_iter, "X");

    const std::string& order_status = get_string_field(*o_iter, "X");

    if (order_status == "CANCELED" && execution_type == "CANCELED") {
      std::string exch_order_id = std::to_string(get_field<unsigned long>(*o_iter, "i"));
      MxOrderExpired expired;
      expired.exch_order_id = std::move(exch_order_id);
      _callbacks.on_order_expired(expired);
    }
  }
}


void BinanceUsdFutLineHandler::process_trade_lite(json& msg)
{
  /* io-thread */

  MxOrderExecution exec;
  exec.fullfill = MxOrderExecution::partial;
  exec.qty = std::stod(get_field<std::string>(msg, "l"));
  exec.price = std::stod(get_field<std::string>(msg, "L"));
  exec.exch_order_id = std::to_string(get_field<unsigned long>(msg, "i"));
  bool is_maker = get_field<bool>(msg, "m");
  exec.match_type = is_maker? MxOrderExecution::maker : MxOrderExecution::taker;
  exec.symbol = get_field<std::string>(msg, "S");
  auto & side = get_field<std::string>(msg, "S");
  if (side == "BUY")
    exec.side = Side::buy;
  if (side == "SELL")
    exec.side = Side::sell;

  _callbacks.on_order_execution(exec);
}


void BinanceUsdFutLineHandler::on_feed_message(const char* buf, size_t len)
{
  if (auto mcap = _core->message_capture_service()) {
    mcap->push_event(_ws_feed_msgcap_id_in, std::string_view(buf, len));
  }

  try {
    json msg = json::parse(buf, buf+len);

    if (auto iter = msg.find("id"); iter != std::end(msg)) {
      // handle the initial reponse to SUBSCRIBE
      if ((*iter).get_ref<std::string&>() == "user_stream_sub")
        return;
      throw std::runtime_error("unknown subscribe response ID");
    }

    auto & event_type = get_field<std::string>(msg, "e");
    if (event_type == "TRADE_LITE") {
      process_trade_lite(msg);
    }
    else if (event_type == "ORDER_TRADE_UPDATE") {
      process_order_trade_update(msg);
    }
    else {
      LOG_WARN("unhandled message binance-ufut-feed: " << msg);
    }
  }
  catch (std::exception& e) {
    LOG_ERROR("failed to process inbound message on binance-ufut-feed: "
             << e.what()
              << "; message: "
              << std::string_view(buf, len));
  }
}


void BinanceUsdFutLineHandler::process_submit_order_reply(PendReq& req, json& msg)
{
  if (auto iter = msg.find("result"); iter != msg.end() && iter->is_object()) {
    if ((*iter)["status"] == "NEW") {
      MxSubmitOrderAck out;
      out.flags = 0;
      out.exchange = ExchangeId::binance_usdfut;
      out.order_id = req.order_id;
      out.exch_order_id = std::to_string((*iter)["orderId"].get<size_t>());
      _callbacks.on_submit_order_ack(out);
      return;
    }
  }

  if (auto iter = msg.find("error"); iter != msg.end() && iter->is_object()) {
    LOG_WARN("binance-usdfut order reject: " << msg);
    MxSubmitOrderRej out;
    out.order_id = req.order_id;
    out.exch_error_code = std::to_string((*iter)["code"].get<long>());
    out.exch_error_text = (*iter)["msg"].get<std::string>();
    _callbacks.on_submit_order_rej(out);
    return;
  }

  throw std::runtime_error("failed to process submit order reply");
}


void BinanceUsdFutLineHandler::process_cancel_order_reply(PendReq& req, json& msg)
{
  if (auto iter = msg.find("error"); iter != msg.end()) {
    MxCancelOrderRej rej;
    rej.order_id = req.order_id;
    rej.exch_error_code = std::to_string(msg["error"]["code"].get<long>());
    rej.exch_error_text = msg["error"]["msg"].get<std::string>();

      _callbacks.on_cancel_order_rej(rej);
    return;
  }

  if (auto iter = msg.find("result"); iter != msg.end()) {
    if (msg["result"]["status"] == "CANCELED") {
      MxCancelOrderAck ack;
      ack.order_id = req.order_id;
      _callbacks.on_cancel_order_ack(ack);
      return;
    }
  }

  throw std::runtime_error("unhandled cancel order reply");
}


void BinanceUsdFutLineHandler::process_session_logon_reply(PendReq&,
                                                           json& msg)
{
  /* io-thread */

  int status = msg["status"].get<int>();

  if (status == 200) {
    LOG_INFO("binance-ufut-line: logon success");

    auto wp = this->weak_from_this();
    _connector_thread -> dispatch(std::chrono::milliseconds(250), [wp]() {
      if (auto sp = wp.lock())
        sp->initiate_user_stream();
      return std::chrono::seconds(0);
    });
  }
  else {
    LOG_INFO("binance-ufut-line: logon reject: "
             << msg["error"]["code"]
             << ": "
             << msg["error"]["msg"]);
  }
}


void BinanceUsdFutLineHandler::on_line_message(const char* buf, size_t len)
{
  /* io-thread */

  // This function receives Binance order responses, such as new-order-ack,
  // cancel-order-ack, and so on.

  if (auto mcap = _core->message_capture_service())
    mcap->push_event(_ws_line_msgcap_id_in, std::string_view(buf, len));

  try {
    json msg = json::parse(buf, buf + len);
    PendReq req;

    if (auto id = msg.find("id"); id != msg.end()) {
      {
        std::lock_guard<std::mutex> guard{_pend_mtx};
        const std::string & id_value = id->get_ref<const std::string&>();
        auto iter = _pend_reqs.find(id_value);
        if (iter != _pend_reqs.end()) {
          req = std::move(iter->second);
          _pend_reqs.erase(iter);
        }
        else
          throw std::runtime_error("cannot find pending request id");
      }

      if (req.type == PendReq::new_order) {
        this->process_submit_order_reply(req, msg);
      }
      else if (req.type == PendReq::cancel_order) {
        this->process_cancel_order_reply(req, msg);
      }
      else if (req.type == PendReq::user_ping) {
        // pass
      }
      else if (req.type == PendReq::user_start) {
        if (auto result = msg.find("result"); result != msg.end()) {
          if (result->is_object())
            if (auto listenkey = result->find("listenKey"); listenkey != result->end()) {
              if (listenkey->is_string()) {
                auto value = listenkey->get<std::string>();
                _listenkey = value;
                _connector_thread->dispatch(std::chrono::milliseconds(250),
                                            [this, value]() -> std::chrono::milliseconds {
                                              this->subscribe_user_stream(value);
                                              return std::chrono::milliseconds{0};
                                            });
              }
            }
        }
      }
      else if (req.type == PendReq::session_logon) {
        process_session_logon_reply(req, msg);
      }
      else {
        throw std::runtime_error("unhandled response type");
      }
    }
    else {
      throw std::runtime_error("no 'id' field in message");
    }
  }
  catch (const std::exception & e) {
    LOG_ERROR("failed to process message: " << e.what()
              << "; message: "
              << std::string_view(buf, len));
  }
}


void BinanceUsdFutLineHandler::manage_connection()
{
  /* management thread */

  if (!this->is_open()) {
    try {
      _ws_feed.reset();
      _ws_line.reset();

      _ws_line = connect_websocket(
        _line_url,
        "binance-ufut-line",
        _reactor,
        _ssl,
        _event_loop,
        [this](const char* buf, size_t n){ this->on_line_message(buf, n); },
        SslSocket::Options{},
        1024*1024  // 1MB
       );

      if (!apex::websock_is_open(_ws_line))
        return;

      // create request ID
      char req_id[16] = {};
      {
        auto msg_seq_num = ++_msg_seq_num;
        snprintf(req_id, sizeof(req_id), "%010lu", msg_seq_num);
        std::lock_guard<std::mutex> guard{_pend_mtx};
        _pend_reqs[req_id] = PendReq{PendReq::session_logon, ""};
      }

      auto timestamp = Time::realtime_now().as_epoch_ms().count();
      auto payload = concat("apiKey=", _apikey, "&timestamp=", timestamp);
      auto signature = sign_message(payload, _seedhex);
      auto msg = concat(R"({"id":")",
                        req_id,
                        R"(","method":"session.logon","params":{"apiKey":")",
                        _apikey,
                        R"(","timestamp":)",
                        timestamp,
                        R"(,"signature":")",
                        signature,
                        R"("}})");

      if (auto mcap = _core->message_capture_service())
        mcap->push_event(_ws_line_msgcap_id_out, msg);
      _ws_line->send(msg);
    }
    catch (const std::exception& err) {
      LOG_ERROR(err.what());
    }
  }

  return;
}


// TODO: what about cancel?  We will get order_id & exch_order_id
void BinanceUsdFutLineHandler::submit_order(OrderParams order)
{

  // TODO: concern: how to make these IDs unique if we have 2 engines?
  //       ideally need an instance ID.
  //       And what if the same engine restarts?

  // create request ID
  char req_id[16] = {};
  {
    auto msg_seq_num = ++_msg_seq_num;
    snprintf(req_id, sizeof(req_id), "%010lu", msg_seq_num);
    std::lock_guard<std::mutex> guard{_pend_mtx};
    _pend_reqs[req_id] = PendReq{PendReq::new_order, order.order_id};
  }

  if (apex::websock_is_open(_ws_line)) {

    // TOOD: add time window for order eligibility (say now + 5 sec)

    // Using JsonWriter to have better control of number formatting
    JsonWriter jw;
    {
      JsonWriterObject obj_scope(jw);
      jw.write_field("id");
      jw.write_value(req_id);
      jw.write_field("method", "order.place");
      jw.write_field("params");
      {
        JsonWriterObject obj_scope(jw);
        jw.write_field("newClientOrderId", order.order_id);
        jw.write_field("price");
        jw.write_value_double(order.price);
        jw.write_field("quantity");
        jw.write_value_double(order.size);
        jw.write_field("side", to_binance(order.side));
        jw.write_field("symbol", order.symbol);
        jw.write_field("timeInForce", to_binance(order.time_in_force));
        jw.write_field("timestamp");
        jw.write_value_long(Time::realtime_now().as_epoch_ms().count());
        jw.write_field("type", "LIMIT");
      }
    }

    std::string_view sv ( jw );
    if (auto mcap = _core->message_capture_service())
      mcap->push_event(_ws_line_msgcap_id_out, sv);
    _ws_line->send(sv);
  } else {
    LOG_WARN("*** NOT OPEN ***");
    // TODO: how to return with a reject here?
  }
}


void BinanceUsdFutLineHandler::cancel_order(const MxCancelOrder& msg)
{
  // create request ID
  char req_id[16] = {};
  {
    auto msg_seq_num = ++_msg_seq_num;

    snprintf(req_id, sizeof(req_id), "%010lu", msg_seq_num);

    std::lock_guard<std::mutex> guard{_pend_mtx};
    _pend_reqs[req_id] = PendReq{PendReq::cancel_order, msg.order_id};
  }

  if (_ws_line && _ws_line->is_open()) {
    json jm;
    jm["id"] = req_id;
    jm["method"] = "order.cancel";
    json params;
    params["symbol"] = msg.symbol;  // TOOD: needs to be the feed symbol
    params["timestamp"] = Time::realtime_now().as_epoch_ms().count();
    params["orderId"] = std::stoul(msg.exch_order_id);
    jm["params"] = std::move(params);
    auto msg = jm.dump();
    if (auto mcap = _core->message_capture_service())
      mcap->push_event(_ws_line_msgcap_id_out, msg);
    _ws_line->send(msg);
  }
}


void BinanceUsdFutLineHandler::initiate_user_stream()
{
  /* line management thread */

  _ws_feed = connect_websocket(
    _feed_url,
    "binance-ufut-user",
    _reactor,
    _ssl,
    _event_loop,
    [this](const char* buf, size_t n){ this->on_feed_message(buf, n); });

  char req_id[16] = {};
  {
    auto msg_seq_num = ++_msg_seq_num;
    snprintf(req_id, sizeof(req_id), "%010lu", msg_seq_num);
    std::lock_guard<std::mutex> guard{_pend_mtx};
    _pend_reqs[req_id] = PendReq{PendReq::user_start, ""};
  }

  auto payload = "apiKey=" + _apikey;
  auto signature = sign_message(payload, _seedhex);

  std::ostringstream oss;
  oss << R"({"id":")" << req_id
      << R"(","method":"userDataStream.start","params":{"apiKey":")"
      << _apikey
      << R"("}})";

  auto msg = oss.str();
  if (auto mcap = _core->message_capture_service())
    mcap->push_event(_ws_line_msgcap_id_out, msg);
  _ws_line->send(msg);
}


void BinanceUsdFutLineHandler::user_stream_keepalive()
{
  /* management thread */

  char req_id[16] = {};
  {
    auto msg_seq_num = ++_msg_seq_num;
    snprintf(req_id, sizeof(req_id), "%010lu", msg_seq_num);
    std::lock_guard<std::mutex> guard{_pend_mtx};
    _pend_reqs[req_id] = PendReq{PendReq::user_ping, ""};
  }

  if (_ws_line && _ws_line->is_open()) {
    auto msg = concat(R"({"id":")",
                      req_id,
                      R"(","method":"userDataStream.ping","params":{"apiKey":")",
                      _apikey,
                      R"("}})");

  if (auto mcap = _core->message_capture_service())
    mcap->push_event(_ws_line_msgcap_id_out, msg);
  _ws_line->send(msg);
  }
}


void BinanceUsdFutLineHandler::start()
{
  using namespace std::chrono_literals;

  auto delay = 10s;
  _connector_thread -> dispatch(std::chrono::seconds{1}, [this, delay]() {
    this->manage_connection();
    return delay;
  });

  auto user_stream_keepalive = 15min;
  _connector_thread -> dispatch(user_stream_keepalive, [this, user_stream_keepalive]() {
    this->user_stream_keepalive();
    return user_stream_keepalive;
  });
}


std::unique_ptr<OrderRouter> BinanceUsdFutLineHandler::get_order_router_adapter()

{
  auto router = std::make_unique<OrderRouterAdapter<BinanceUsdFutLineHandler>>(
    _core, shared_from_this());

  return router;
}


}
