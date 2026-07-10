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
#include <apex/core/Errors.hpp>
#include <apex/net/WebsocketClient.hpp>
#include <apex/util/JsonWriter.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/venues/binance/BinanceLineHandler.hpp>
#include <apex/venues/binance/binance_common.hpp>

#include <sodium.h>

namespace apex {


MxOrderExecution::MatchType from_binance_is_maker(bool is_maker) {
  return (is_maker)? MxOrderExecution::maker : MxOrderExecution::taker;
}


Side to_side(std::string_view side) {
  if (side == "BUY")
    return Side::buy;
  if (side == "SELL")
    return Side::sell;
  return Side::none;
}


BinanceLineHandler::BinanceLineHandler(Core* core,
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
  _line_url = "wss://ws-api.binance.com:9443/ws-api/v3?returnRateLimits=false";

  // load exchange secrets
  std::filesystem::path secrets_file = config.api_key_file;

  if (core->run_mode() == RunMode::live) {
    if (secrets_file.empty()) {
      throw ConfigError("binance API-key filename not provided", __FILE__, __LINE__);
    }

    auto obj = json::parse(slurp(secrets_file.native().c_str()));

    _apikey = obj["key"].get<std::string>();
    _ed25519_signer.set_private_key_hex(obj["secret"].get<std::string>());
  }

  int uat_mode = 0;
  if (uat_mode) {
    _line_url = "ws://127.0.0.1:9010/ws-fapi/v1?returnRateLimits=false";
    _apikey = "sErnzoWaTESThtDlKTHISISATESTfWppp7t34vOOszl8wNTEST3feMyGv5SjSsLv";
  }

  if (auto mcap = _core->message_capture_service()) {

    std::tie(_ws_line_msgcap_id_in, _ws_line_msgcap_id_out)
      = mcap->register_stream_id_pair("binance-line");
  }

  _connector_thread = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("binlinecx");
    });
}

BinanceLineHandler::~BinanceLineHandler() = default;

bool BinanceLineHandler::is_open() const
{
  return apex::websock_is_open(_ws_line);
}


void BinanceLineHandler::process_submit_order_reply(PendReq& req, json& msg)
{
  if (auto result = msg.find("result"); result != msg.end() && result->is_object()) {
    const std::string & status = get_string_field(*result, "status");
    if (status == "NEW" || status == "FILLED" || status == "PARTIALLY_FILLED") {
      MxSubmitOrderAck ack;
      ack.flags = MxSubmitOrderAck::possible_duplicated;
      ack.exchange = ExchangeId::binance;
      ack.order_id = req.order_id;
      ack.exch_order_id = std::to_string((*result)["orderId"].get<size_t>());
      _callbacks.on_submit_order_ack(ack);
      return;
    }
  }

  if (auto iter = msg.find("error"); iter != msg.end() && iter->is_object()) {
    MxSubmitOrderRej out;
    out.order_id = req.order_id;
    out.exch_error_code = std::to_string((*iter)["code"].get<long>());
    out.exch_error_text = (*iter)["msg"].get<std::string>();
    _callbacks.on_submit_order_rej(out);
    return;
  }

  throw std::runtime_error("failed to process submit order reply");
}


void BinanceLineHandler::process_cancel_order_reply(PendReq& req, json& msg)
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


void BinanceLineHandler::process_session_logon_reply(PendReq&,
                                                     json& msg)
{
  /* io-thread */

  int status = msg["status"].get<int>();

  if (status == 200) {
    LOG_INFO("binance-line: logon success");

    auto wp = this->weak_from_this();
    _connector_thread -> dispatch_after(std::chrono::milliseconds(250), [wp]() {
      if (auto sp = wp.lock())
        sp->initiate_user_stream();
    });
  }
  else {
    LOG_INFO("binance-line: logon reject: "
             << msg["error"]["code"]
             << ": "
             << msg["error"]["msg"]);
  }
}


void BinanceLineHandler::process_execution_report(json& event)
{
  /* io-thread */

  const std::string & exec_type = get_string_field(event, "x");
  const std::string & ord_state = get_string_field(event, "X");

  if (exec_type == "TRADE" ) {
    MxOrderExecution exec;
    exec.match_type = from_binance_is_maker(get_field<bool>(event, "m"));
    exec.fullfill = MxOrderExecution::Filled::partial;
    exec.price = std::stod(get_field<std::string>(event, "L"));
    exec.qty = std::stod(get_field<std::string>(event, "l"));
    exec.exch_order_id = std::to_string(get_field<unsigned long>(event, "i"));
    exec.symbol = get_field<std::string>(event, "s");
    exec.side = to_side(get_field<std::string>(event, "S"));
    exec.time = from_binance_timestamp(get_field<unsigned long>(event, "T"));

    _callbacks.on_order_execution(exec);
    return;
  }

  if (ord_state == "CANCELED") {
    std::string exch_order_id = std::to_string(get_field<unsigned long>(event, "i"));
    MxOrderExpired expired;
    expired.exch_order_id = std::move(exch_order_id);
    _callbacks.on_order_expired(expired);
    return;
  }

  if (exec_type == "NEW" && ord_state == "NEW") {
    // Binance can send execution reports for fills, ahead of the initial order
    // ack response, so, here we need to also generate an order ack for the
    // initial execution response
    MxSubmitOrderAck ack;
    ack.flags = MxSubmitOrderAck::possible_duplicated;
    ack.exchange = ExchangeId::binance;
    ack.order_id = get_string_field(event, "c");
    ack.exch_order_id = std::to_string(get_field<unsigned long>(event, "i"));
    _callbacks.on_submit_order_ack(ack);
    return;
  }

  throw std::runtime_error("event-type unhandled for execution-report");
}


void BinanceLineHandler::process_raw_message(const char* buf, size_t len)
{
  /* io-thread */

  // This function receives Binance order responses, such as new-order-ack,
  // cancel-order-ack, execution reports, and so on.

  if (auto mcap = _core->message_capture_service())
    mcap->push_event(_ws_line_msgcap_id_in, std::string_view(buf, len));

  try {
    json msg = json::parse(buf, buf + len);

    // handle solicited reposes arising from prior requests
    if (auto id = msg.find("id"); id != msg.end()) {
      const std::string & id_value = id->get_ref<const std::string&>();
      if (id_value == "udssub") {
        /*  Note: is status is not 200, we should close connection, and reopen
{
  "id": "udssub",
  "result": {},
  "status": 200
}
         */
        // TODO: handle userDataStream.subscribe
        return;
      }

      PendReq req;
      {
        auto guard = std::lock_guard{_pend_mtx};
        const std::string & id_value = id->get_ref<const std::string&>();
        auto iter = _pend_reqs.find(id_value);
        if (iter != _pend_reqs.end()) {
          req = std::move(iter->second);
          _pend_reqs.erase(iter);
        }
        else
          throw std::runtime_error("cannot find pending request for this response");
      }

      switch (req.type) {
        case PendReq::new_order :
          return process_submit_order_reply(req, msg);
        case PendReq::cancel_order :
          return process_cancel_order_reply(req, msg);
        case PendReq::user_ping :
          return;
        case PendReq::session_logon :
          return process_session_logon_reply(req, msg);
      }
      throw std::runtime_error("unhandled response type");
    }

    // handle events
    if (auto it = msg.find("event"); it != msg.end() && it->is_object()) {
      const std::string & event_type = (*it)["e"].get_ref<const std::string&>();
      if (event_type == "executionReport")
        return process_execution_report(*it);
      if (event_type == "outboundAccountPosition")
        return;
    }

    throw std::runtime_error("binance-line message not handled");
  }
  catch (const std::exception & e) {
    LOG_ERROR("failed to process message: " << e.what()
              << "; message: "
              << std::string_view(buf, len));
  }
}


void BinanceLineHandler::manage_connection()
{
  /* management thread */

  if (!this->is_open()) {
    try {
      LOG_INFO("binance line is down, attempting reconnect");
      _ws_line.reset();

      connect_websocket(
        _ws_line,
        _line_url,
        "binance-line",
        _reactor,
        _ssl,
        _event_loop,
        [this](const char* buf, size_t n){ this->process_raw_message(buf, n); },
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
      auto signature = _ed25519_signer.sign_detached(payload).to_base64();
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


SendStatus BinanceLineHandler::submit_order(OrderParams order)
{
  // TODO: concern: how to make these IDs unique if we have 2 engines?
  //       ideally need an instance ID.
  //       And what if the same engine restarts?

  // create request ID - used to associate a request with its reply
  char req_id[16] = {};
  {
    auto msg_seq_num = ++_msg_seq_num;
    snprintf(req_id, sizeof(req_id), "%010lu", msg_seq_num);
    std::lock_guard<std::mutex> guard{_pend_mtx};
    _pend_reqs[req_id] = PendReq{PendReq::new_order, order.order_id};
  }

  if (apex::websock_is_open(_ws_line)) {

    // TODO: add time window for order eligibility (say now + 5 sec)

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
    return SendStatus{error::venue_link_down};
  }

  return SendStatus::success;
}


SendStatus BinanceLineHandler::cancel_order(const MxCancelOrder& msg)
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
    params["symbol"] = msg.symbol;
    params["timestamp"] = Time::realtime_now().as_epoch_ms().count();
    params["orderId"] = std::stoul(msg.exch_order_id);
    jm["params"] = std::move(params);
    auto msg = jm.dump();
    if (auto mcap = _core->message_capture_service())
      mcap->push_event(_ws_line_msgcap_id_out, msg);
    _ws_line->send(msg);
  }

  return SendStatus::success;
}


void BinanceLineHandler::initiate_user_stream()
{
  /* line management thread */


  // NEW - try to subscribe directly on the line connection

  auto msg = R"({
  "id": "udssub",
  "method": "userDataStream.subscribe"
})";

  if (auto mcap = _core->message_capture_service())
    mcap->push_event(_ws_line_msgcap_id_out, msg);
  _ws_line->send(msg);
}


void BinanceLineHandler::user_stream_keepalive()
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


void BinanceLineHandler::start()
{
  using namespace std::chrono_literals;

  auto delay = 10s;
  _connector_thread -> dispatch(std::chrono::seconds{1}, [this, delay]() {
    this->manage_connection();
    return delay;
  });

  // auto user_stream_keepalive = 15min;
  // _connector_thread -> dispatch(user_stream_keepalive, [this, user_stream_keepalive]() {
  //   this->user_stream_keepalive();
  //   return user_stream_keepalive;
  // });
}


std::unique_ptr<OrderRouter> BinanceLineHandler::get_order_router_adapter()
{
  auto router = std::make_unique<OrderRouterAdapter<BinanceLineHandler>>(
    _core, shared_from_this());

  return router;
}


}
