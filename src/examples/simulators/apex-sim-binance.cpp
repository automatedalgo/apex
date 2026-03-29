/* Copyright 2025 Automated Algo (www.automatedalgo.com)

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

#include <apex/net/Reactor.hpp>
#include <apex/net/TcpSocket.hpp>
#include <apex/net/WebsocketProtocol.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/util/json.hpp>
#include <apex/core/Logger.hpp>
#include <apex/net/HttpParser.hpp>

#include <string>
#include <list>

using namespace apex;
using namespace std;


/* Provides a Binance (Spot) Simulation Exchange.

   Purpose is help assist local development, so we can connect to this to
   replicate problems encountered in production.
 */

#define BINANCE_UAT_API_KEY "sErnzoWaTESThtDlKTHISISATESTfWppp7t34vOOszl8wNTEST3feMyGv5SjSsLv"


// Options for controlling various order interaction scenarios.
struct Options {
  bool reject_all_orders = false;
  bool reject_all_cancel = false;
} options;


static size_t timestamp() {
  return Time::realtime_now().as_epoch_ms().count();
}

const auto order_reject = R"(
{
  "id": "0000000004",
  "status": 400,
  "error": {
    "code": -2011,
    "msg": "Unknown order sent."
  },
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 9
    }
  ]
}
)";


const auto cancel_ack = R"(
{
  "id": "0000000003",
  "status": 200,
  "result": {
    "symbol": "DOGEUSDT",
    "origClientOrderId": "0000000002",
    "orderId": 11199699515,
    "orderListId": -1,
    "clientOrderId": "5LTisc50oVKDsNFoDt481l",
    "transactTime": 1753641776749,
    "price": "0.23000000",
    "origQty": "50.00000000",
    "executedQty": "0.00000000",
    "origQuoteOrderQty": "0.00000000",
    "cummulativeQuoteQty": "0.00000000",
    "status": "CANCELED",
    "timeInForce": "GTC",
    "type": "LIMIT",
    "side": "BUY",
    "selfTradePreventionMode": "EXPIRE_MAKER"
  },
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 8
    }
  ]
}
)";


const auto unsol_cancel_execution_report = R"(
{
  "event": {
    "e": "executionReport",
    "E": 1753637100702,
    "s": "DOGEUSDT",
    "c": "web_ce2059ebfd784cefa9bf420b2f4443b1",
    "S": "BUY",
    "o": "LIMIT",
    "f": "GTC",
    "q": "50.00000000",
    "p": "0.23000000",
    "P": "0.00000000",
    "F": "0.00000000",
    "g": -1,
    "C": "0000000002",
    "x": "CANCELED",
    "X": "CANCELED",
    "r": "NONE",
    "i": 11198860768,
    "l": "0.00000000",
    "z": "0.00000000",
    "L": "0.00000000",
    "n": "0",
    "N": null,
    "T": 1753637100702,
    "t": -1,
    "I": 23836822866,
    "w": false,
    "m": false,
    "M": false,
    "O": 1753637094972,
    "Z": "0.00000000",
    "Y": "0.00000000",
    "Q": "0.00000000",
    "W": 1753637094972,
    "V": "EXPIRE_MAKER"
  }
}
)";


const auto on_fill_execution_report = R"(
{
  "event": {
    "e": "executionReport",
    "E": 1753614784677,
    "s": "DOGEUSDT",
    "c": "0000000002",
    "S": "BUY",
    "o": "LIMIT",
    "f": "GTC",
    "q": "50.00000000",
    "p": "0.23815000",
    "P": "0.00000000",
    "F": "0.00000000",
    "g": -1,
    "C": "",
    "x": "TRADE",
    "X": "FILLED",
    "r": "NONE",
    "i": 11194752766,
    "l": "50.00000000",
    "z": "50.00000000",
    "L": "0.23815000",
    "n": "0.00001117",
    "N": "BNB",
    "T": 1753614784676,
    "t": 1203488670,
    "I": 23827963513,
    "w": false,
    "m": true,
    "M": true,
    "O": 1753614782309,
    "Z": "11.90750000",
    "Y": "11.90750000",
    "Q": "0.00000000",
    "W": 1753614782309,
    "V": "EXPIRE_MAKER"
  }
}
)";

const auto on_new_execution_report = R"(
{
  "event": {
    "e": "executionReport",
    "E": 1753614782310,
    "s": "DOGEUSDT",
    "c": "0000000002",
    "S": "BUY",
    "o": "LIMIT",
    "f": "GTC",
    "q": "50.00000000",
    "p": "0.23815000",
    "P": "0.00000000",
    "F": "0.00000000",
    "g": -1,
    "C": "",
    "x": "NEW",
    "X": "NEW",
    "r": "NONE",
    "i": 11194752766,
    "l": "0.00000000",
    "z": "0.00000000",
    "L": "0.00000000",
    "n": "0",
    "N": null,
    "T": 1753614782309,
    "t": -1,
    "I": 23827962342,
    "w": true,
    "m": false,
    "M": false,
    "O": 1753614782309,
    "Z": "0.00000000",
    "Y": "0.00000000",
    "Q": "0.00000000",
    "W": 1753614782309,
    "V": "EXPIRE_MAKER"
  }
}
)";


const auto user_data_stream_subscribe_ack = R"(
{
  "id": "<REPLACE>",
  "status": 200,
  "result": {},
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 6
    }
  ]
}
)";


const auto user_data_stream_start_ack = R"(
{
  "id": "0000000002",
  "status": 200,
  "result": {
    "listenKey": "74686973206973206E6F742061207265616C206B6579"
  },
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 6
    }
  ]
}
)";


const std::string session_logon_ack = R"(
{
  "id": "0000000001",
  "status": 200,
  "result": {
    "apiKey": "74686973206973206E6F742061207265616C206B6579",
    "authorizedSince": 1753609619794,
    "connectedSince": 1753609619673,
    "returnRateLimits": true,
    "serverTime": 1753609619914,
    "userDataStream": false
  },
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 4
    }
  ]
}
)";

const std::string logon_reject_template = R"({
  "id": "0000000001",
  "status": 401,
  "error": {
    "code": -2015,
    "msg": "Invalid API-key, IP, or permissions for action"
  },
  "rateLimits": [
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 2400,
      "count": 7
    }
  ]
})";


const auto order_ack_template = R"({
  "id": "0000000002",
  "status": 200,
  "result": {
    "symbol": "DOGEUSDT",
    "orderId": 11194752766,
    "orderListId": -1,
    "clientOrderId": "0000000002",
    "transactTime": 1753614782309,
    "price": "0.23815000",
    "origQty": "50.00000000",
    "executedQty": "0.00000000",
    "origQuoteOrderQty": "0.00000000",
    "cummulativeQuoteQty": "0.00000000",
    "status": "NEW",
    "timeInForce": "GTC",
    "type": "LIMIT",
    "side": "BUY",
    "workingTime": 1753614782309,
    "fills": [],
    "selfTradePreventionMode": "EXPIRE_MAKER"
  },
  "rateLimits": [
    {
      "rateLimitType": "ORDERS",
      "interval": "SECOND",
      "intervalNum": 10,
      "limit": 100,
      "count": 1
    },
    {
      "rateLimitType": "ORDERS",
      "interval": "DAY",
      "intervalNum": 1,
      "limit": 200000,
      "count": 1
    },
    {
      "rateLimitType": "REQUEST_WEIGHT",
      "interval": "MINUTE",
      "intervalNum": 1,
      "limit": 6000,
      "count": 1
    }
  ]
})";

constexpr std::string_view cancel_order_reject_template = R"({"id":"0000000011","status":400,"error":{"code":-2011,"msg":"Unknown order sent."},"rateLimits":[{"rateLimitType":"REQUEST_WEIGHT","interval":"MINUTE","intervalNum":1,"limit":6000,"count":16}]})";

const std::string submit_order_rej_template = R"(
{"id":"3f7df6e3-2df4-44b9-9919-d2f38f90a99a","status":400,"error":{"code":-2019,"msg":"Margin is insufficient."}}
)";

// This is a very basic implementation of a websocket server session, with the
// goal to support integration testing.
class WebsocketServerSession : public std::enable_shared_from_this<WebsocketServerSession>
{
public:

  using SessionMsgCb = std::function<void(WebsocketServerSession*, const char* buf, size_t n)> ;

  WebsocketServerSession(
    unique_ptr<TcpSocket> sock,
    RealtimeEventLoop* idle_thread,
    SessionMsgCb on_msg)
    : _sock(std::move(sock)),
      _http_parser(HttpParser::e_http_request),
      _idle_thread(idle_thread),
      _on_msg(std::move(on_msg))
  {
    // construct the websocket protocol handler
    auto request_timer_cb = [this](std::chrono::milliseconds interval) {

      /* If protocol has requested a timer, register a reoccurring event to call
       * the protocol's on_timer function. Called during construction of
       * protocol. */
      if (interval.count() > 0) {
        auto timerfn = [wp{this->weak_from_this()},
                        interval]() -> std::chrono::milliseconds {
          if (auto sp = wp.lock()) {
            sp->_protocol->on_timer();
            return interval;
          } else {
            /* shared_ptr invalid, so cancel timer */
            return std::chrono::milliseconds();
          }
        };
        this->_idle_thread->dispatch(interval, std::move(timerfn));
      }

    };

    auto protocol_closed_fn = [this](std::chrono::milliseconds) {
    };

    protocol::protocol_callbacks callbacks {
      request_timer_cb, protocol_closed_fn
    };

    WebsocketProtocol::options protocol_options;

    _protocol = std::make_unique<WebsocketProtocol>(
      _sock.get(),
      [this](const char* buf, size_t n) { this->on_msg(buf, n); },
      callbacks,
      connect_mode::accept,
      protocol_options
      );
  }


  void start() {
    // start socket read, which feeds into the protocol handler
    _sock->start_read([sockptr=_sock.get(), this](char* buf, ssize_t n) {
      /* io-thread */
      if (n > 0) {
        this->_protocol->on_read(buf, n);
      }
      else {
        LOG_INFO("client disconnected");
      }
    });

    // register timer for websocket pings
    auto ping_interval = std::chrono::minutes(1);
    _idle_thread->dispatch(ping_interval,
                           [this, ping_interval](){
                             this->_protocol->on_timer();
                             return ping_interval;
                           });

  }


  void on_msg(const char* buf, size_t n) {
    /* io-thread */
    _on_msg(this, buf, n);
  }

  bool is_open() const {
    return _sock->is_open() && _protocol->is_open();
  }

  void send(const std::string& data) {
    _protocol->send_msg(data.c_str(), data.size());
  }

private:

  std::unique_ptr<TcpSocket>  _sock;
  HttpParser _http_parser;
  std::unique_ptr<WebsocketProtocol> _protocol;
  RealtimeEventLoop* _idle_thread;
  SessionMsgCb _on_msg;
};


class WebsocketServer
{
public:

  using OnSessionMsg = std::function<void(WebsocketServerSession*, const char* buf, size_t n)> ;

  WebsocketServer(std::string host,
                  std::string port,
                  std::string label,
                  Reactor* reactor,
                  RealtimeEventLoop* idle_thread,
                  OnSessionMsg on_session_msg)
    : _host(host),
      _port(port),
      _label(label.empty()? "websocket-server": label),
      _reactor(reactor),
      _idle_thread(idle_thread),
      _on_session_msg(on_session_msg)
  {
  }


  void start() {
    _sock = make_unique<TcpSocket>(_reactor);
    _sock->listen(_host, _port, [this](unique_ptr<TcpSocket>& sock) {
      this->on_new_client(sock);
    });
    LOG_INFO(_label << ": websocket listening on " << _host << ":" << _port);
  }


  void on_new_client(unique_ptr<TcpSocket>& sock) {
    auto s = std::make_shared<WebsocketServerSession>(std::move(sock),
                                                      _idle_thread,
                                                      _on_session_msg);

    _sessions.push_back(s);
    s->start();
  }

private:
  std::string _host;
  std::string _port;
  std::string _label;
  std::unique_ptr<TcpSocket> _sock;
  Reactor* _reactor;
  RealtimeEventLoop* _idle_thread;
  std::list<std::shared_ptr<WebsocketServerSession>> _sessions;
  OnSessionMsg _on_session_msg;
};

class SimAccount;

struct SimRestingOrder
{
  std::string symbol;
  std::string side;    // TODO: use model
  std::string client_order_id;
  size_t order_id;
  double quantity;
  double price;
  Time created;
  double tot_exec_qty = 0;
  bool is_fully_filled = false;
  double last_exec_qty = 0;
  double last_exec_price = 0;


  std::shared_ptr<SimAccount> account;
};


class SimRestingOrder;
class OnOrderEventListener
{
public:
  OnOrderEventListener(std::shared_ptr<WebsocketServerSession> session)
    : _session{std::move(session)}
  {
  }

  virtual void on_order_added(SimRestingOrder&) = 0;
  virtual void on_order_fill(SimRestingOrder&) = 0;
  virtual void on_order_expired(SimRestingOrder&) = 0;
  virtual void on_order_cancelled(SimRestingOrder&) = 0;

protected:
  std::shared_ptr<WebsocketServerSession> _session;
};




class SimAccount
{
public:

  SimAccount() = default;

  void push_order_expired() {
    std::lock_guard<std::mutex> guard(_mtx);
    for (auto & item : _user_data_subs) {
      if (item->is_open())
        item->send(R"({"msg":"THIS WILL BE EXPIRED PLACE HOLDER"})");
    }
  };

  void set_listenkey(std::string listenkey) {
    std::lock_guard<std::mutex> guard(_mtx);
    _listenkey = std::move(listenkey);
  }
  std::string listenkey() const {
    std::lock_guard<std::mutex> guard(_mtx);
    return _listenkey;
  }

  void subscribe_user_data(std::shared_ptr<WebsocketServerSession> ws) {
    std::lock_guard<std::mutex> guard(_mtx);
    _user_data_subs.push_back(std::move(ws));
  }

  void add_order_event_listener(std::unique_ptr<OnOrderEventListener>);

  bool cancel_order(size_t order_id,
                    std::function<void(SimRestingOrder&)> notify_private) {
    std::lock_guard<std::mutex> guard(_mtx);

    bool success = false;
    SimRestingOrder order;

    auto iter = _orders.find(order_id);
    if (iter != _orders.end()) {
      order = std::move(iter->second);
      _orders.erase(iter);
      success = true;
    }

    if (success) {
      LOG_INFO("cancel order: "
               << "symbol: " << order.symbol
               << ", order_id: " << order.order_id);
      if (notify_private)
        notify_private(order);

      for (auto & event_listener : _order_event_listeners)
        event_listener->on_order_cancelled(order);
    }
    else {
      LOG_WARN("rejecting cancel-order for unknown order, order_id: " << order_id);
    }

    return success;
  }


  void expire_order(size_t order_id) {
    SimRestingOrder order;

    if (auto iter = _orders.find(order_id); iter != _orders.end()) {
      std::lock_guard<std::mutex> guard(_mtx);
      order = std::move(iter->second);
      _orders.erase(iter);
    }
    else
      return;

    LOG_INFO("order expired:" << order_id);

    for (auto & event_listener : _order_event_listeners)
      event_listener->on_order_expired(order);
  }


  bool exec_order(size_t order_id, bool fully_fill=false) {
    std::lock_guard<std::mutex> guard(_mtx);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(0.5);
    std::vector<double> v{0.25, 0.5, 1.0};

    auto iter = _orders.find(order_id);
    if (iter != _orders.end()) {
      SimRestingOrder & order = iter->second;

      std::shuffle(v.begin(), v.end(), gen);
      if (fully_fill)
        v[0] = 1.0;

      auto want_exec_qty = iter->second.quantity * v[0];
      auto remain_qty = iter->second.quantity - iter->second.tot_exec_qty;
      auto exec_qty = std::min(want_exec_qty, remain_qty);

      if (!dbl_is_zero(exec_qty)) {
        remain_qty -= exec_qty;

        order.last_exec_price = order.price;
        order.last_exec_qty = exec_qty;
        order.tot_exec_qty += exec_qty;
        order.is_fully_filled = dbl_is_zero(remain_qty);

        LOG_INFO("order fill: " << order_id << ", qty:" << exec_qty);

        for (auto & event_listener : _order_event_listeners)
          event_listener->on_order_fill(iter->second);
      }

      if (order.is_fully_filled) {
        LOG_INFO("order fully filled: " << order_id);
        _orders.erase(iter);
        return false;
      }
      else
        return true;
    }
    return false;
  }


  void add_new_order(SimRestingOrder order,
                     std::function<void(SimRestingOrder& order)> on_inserted) {
    std::lock_guard<std::mutex> guard(_mtx);

    order.tot_exec_qty = 0;

    LOG_INFO("new order: "
             << "symbol: " << order.symbol
             << ", client_order_id: " <<  order.client_order_id
             << ", price: " << order.price
             << ", order_id: " << order.order_id);
    _orders[order.order_id] = order;

    if (on_inserted)
      on_inserted(order);

    for (auto & event_listener : _order_event_listeners)
      event_listener->on_order_added(order);
  }

  mutable std::mutex _mtx;
  std::string _listenkey;

  std::vector<std::shared_ptr<WebsocketServerSession>> _user_data_subs; // TODO: try to delte
  std::vector<std::unique_ptr<OnOrderEventListener>> _order_event_listeners;

  std::map<size_t, SimRestingOrder> _orders;
};


void SimAccount::add_order_event_listener(std::unique_ptr<OnOrderEventListener> listener)
{
  auto guard = std::lock_guard(_mtx);
  _order_event_listeners.push_back(std::move(listener));
}


std::string random_ascii_string(size_t length)
{
  static auto chars = "012345678ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_int_distribution<> dist(0, strlen(chars)-1);
  std::string s(length, '\0');
  for (auto& c : s)
    c = chars[dist(gen)];
  return s;
}


class BinanceOnOrderEventListener : public OnOrderEventListener
{
public:
  BinanceOnOrderEventListener(std::shared_ptr<WebsocketServerSession> session)
    : OnOrderEventListener(std::move(session))
  {
  }

  void on_order_added(SimRestingOrder&) override;
  void on_order_fill(SimRestingOrder&) override;
  void on_order_expired(SimRestingOrder&) override;
  void on_order_cancelled(SimRestingOrder&) override;
};


void BinanceOnOrderEventListener::on_order_cancelled(SimRestingOrder& order)
{
  auto msg = json::parse(unsol_cancel_execution_report);

  msg["event"]["E"] = timestamp();
  msg["event"]["s"] = order.symbol;
  msg["event"]["S"] = order.side;  // TODO: convert to Binance? BUY/SELL ?
  msg["event"]["q"] = format_double(order.quantity);
  msg["event"]["p"] = format_double(order.price);
  msg["event"]["C"] = order.client_order_id;
  msg["event"]["i"] = order.order_id; // json number
  msg["event"]["T"] = timestamp();
  msg["event"]["O"] = timestamp();
  msg["event"]["W"] = timestamp();

  _session->send(msg.dump());
}


void BinanceOnOrderEventListener::on_order_expired(SimRestingOrder& order)
{
  auto msg = json::parse(unsol_cancel_execution_report);

  msg["event"]["E"] = timestamp();
  msg["event"]["s"] = order.symbol;
  msg["event"]["C"] = order.client_order_id;
  msg["event"]["S"] = order.side;  // TODO: convert to Binance? BUY/SELL ?
  msg["event"]["q"] = format_double(order.quantity);
  msg["event"]["p"] = format_double(order.price);
  msg["event"]["i"] = order.order_id; // json number
  msg["event"]["T"] = timestamp();
  msg["event"]["t"] = -1;
  msg["event"]["O"] = timestamp();
  msg["event"]["W"] = timestamp();
  msg["event"]["w"] = false;

  _session->send(msg.dump());
}


void BinanceOnOrderEventListener::on_order_fill(SimRestingOrder& order)
{
  auto msg = json::parse(on_fill_execution_report);

  msg["event"]["E"] = timestamp();
  msg["event"]["s"] = order.symbol;
  msg["event"]["c"] = order.client_order_id;
  msg["event"]["s"] = order.side;  // TODO: convert to Binance? BUY/SELL ?
  msg["event"]["q"] = format_double(order.quantity);
  msg["event"]["p"] = format_double(order.price);
  msg["event"]["i"] = order.order_id; // json number
  msg["event"]["T"] = timestamp();
  msg["event"]["t"] = -1;
  msg["event"]["O"] = timestamp();
  msg["event"]["W"] = timestamp();

  // extra for the fill
  msg["event"]["m"] = false; // is this trade the maker?
  msg["event"]["l"] = format_double(order.last_exec_qty);
  msg["event"]["z"] = format_double(order.tot_exec_qty);
  msg["event"]["L"] = format_double(order.last_exec_price);
  msg["event"]["x"] = "TRADE";
  msg["event"]["X"] = (order.is_fully_filled)? "FILLED ": "PARTIALLY_FILLED";

  _session->send(msg.dump());
}

void BinanceOnOrderEventListener::on_order_added(SimRestingOrder& order)
{
  auto msg = json::parse(on_new_execution_report);

  msg["event"]["E"] = timestamp();
  msg["event"]["s"] = order.symbol;
  msg["event"]["c"] = order.client_order_id;
  msg["event"]["s"] = order.side;  // TODO: convert to Binance? BUY/SELL ?
  msg["event"]["q"] = format_double(order.quantity);
  msg["event"]["p"] = format_double(order.price);
  msg["event"]["x"] = "NEW";
  msg["event"]["X"] = "NEW";
  msg["event"]["i"] = order.order_id; // json number
  msg["event"]["T"] = timestamp();
  msg["event"]["t"] = -1;
  msg["event"]["O"] = timestamp();
  msg["event"]["W"] = timestamp();

  _session->send(msg.dump());
}


/* Crude simulator for Binance spot exchange, to help as a development aid for
 * building the Binance line hander class. */
class BinanceSimulator
{
public:

  std::mutex _mtx; // fat lock
  std::map<std::shared_ptr<WebsocketServerSession>,
           std::shared_ptr<SimAccount>> _accounts;

  BinanceSimulator(std::string line_host,
                   std::string line_port,
                   Reactor* reactor,
                   RealtimeEventLoop* idle_thread);

  void start();

private:

  void attach_to_account(const std::shared_ptr<WebsocketServerSession>&);
  std::shared_ptr<SimAccount> find_account(const std::shared_ptr<WebsocketServerSession>&);

  std::string start_user_data_stream(std::shared_ptr<WebsocketServerSession>);

  void subscribe_to_sim_account(WebsocketServerSession* session,
                                const std::string& listenkey);

  void process_session_logon(WebsocketServerSession*, json& req);
  void process_submit_order_request(WebsocketServerSession*, json& req);
  void process_order_cancel_request(WebsocketServerSession*, json& req);
  void process_user_data_stream_start(WebsocketServerSession*, json& req);
  void process_user_data_stream_subscribe(WebsocketServerSession*, json& req);

  void on_line_message(WebsocketServerSession*, const char*, size_t);

  Reactor* _reactor;
  RealtimeEventLoop* _idle_thread;
  std::unique_ptr<WebsocketServer> _line;
};


BinanceSimulator::BinanceSimulator(std::string line_host,
                                   std::string line_port,
                                   Reactor* reactor,
                                   RealtimeEventLoop* idle_thread)
  : _reactor(reactor),
    _idle_thread(idle_thread)
{
  // order entry
  _line = std::make_unique<WebsocketServer>(
    line_host, line_port, "line", reactor, idle_thread,
    [this](WebsocketServerSession* session, const char* buf, size_t n) {
      this->on_line_message(session, buf, n);
    });
}


void BinanceSimulator::start()
{
  LOG_INFO("starting Binance (Spot) exchange simulator");
  // start the websocket servers for the order-entry channel, and the
  // user-data-stream channel.
  _line->start();
}


void BinanceSimulator::attach_to_account(const std::shared_ptr<WebsocketServerSession>& ws) {
  std::lock_guard<std::mutex> guard(_mtx);
  auto iter = _accounts.find(ws);
  if (iter == std::end(_accounts)) {
    auto account = std::make_shared<SimAccount>();
    iter = _accounts.insert({ws, account}).first;

    auto event_listener = std::make_unique<BinanceOnOrderEventListener>(ws);
    account->add_order_event_listener(std::move(event_listener));
  }
}


/* Find the account, if any, associated with a websocket session */
std::shared_ptr<SimAccount> BinanceSimulator::find_account(
  const std::shared_ptr<WebsocketServerSession> & ws)
{
  std::lock_guard<std::mutex> guard(_mtx);

  auto it = _accounts.find(ws);
  return (it != _accounts.end())? it->second : nullptr;
}


void BinanceSimulator::subscribe_to_sim_account(WebsocketServerSession* session,
                                                const std::string& listenkey) {
  std::lock_guard<std::mutex> guard(_mtx);
  for (auto & item : _accounts) {
    if (item.second->listenkey() == listenkey) {
      item.second->subscribe_user_data(session->shared_from_this());
    }
  }
}


std::string BinanceSimulator::start_user_data_stream(std::shared_ptr<WebsocketServerSession> ws) {
  std::lock_guard<std::mutex> guard(_mtx);
  auto iter = _accounts.find(ws);
  if (iter == std::end(_accounts))
    throw std::runtime_error("session has not completed logon");

  auto listenkey = random_ascii_string(64);
  iter->second->set_listenkey(listenkey);
  return listenkey;
}


void BinanceSimulator::process_submit_order_request(WebsocketServerSession* session,
                                                    json& req)
{
  std::string symbol = req["params"]["symbol"].get<std::string>();

  if (symbol.empty())  {
    // reject
    json resp = json::parse(submit_order_rej_template);
    resp["id"] = req["id"];
    resp["error"]["code"] = -1121;
    resp["error"]["msg"] = "Invalid symbol.";
    session->send(resp.dump());
    return;
  }

  if (options.reject_all_orders) {
    json resp = json::parse(submit_order_rej_template);
    resp["id"] = req["id"];
    resp["error"]["code"] = -1121;
    resp["error"]["msg"] = "orders not allowed";
    session->send(resp.dump());
    return;
  }

  // generate an order ID
  size_t auto_order_id;
  {
    std::lock_guard<std::mutex> guard(_mtx);
    auto_order_id = Time::realtime_now().as_epoch_ms().count();
  }

  // create a resting order
  {
    // find our account
    auto sp = session->shared_from_this();
    auto iter = _accounts.find(sp);
    if (iter != std::end(_accounts)) {
      std::shared_ptr<SimAccount> sim_acct = iter->second;

      // create the callback for when a new order is accepted into the orderbook
      // - these will do the work of sending an order reply
      auto on_order_inserted = [this, session, req](const SimRestingOrder& order) {
        auto resp = json::parse(order_ack_template);
        resp["id"] = req["id"];
        resp["result"]["clientOrderId"] = req["params"]["newClientOrderId"];
        resp["result"]["symbol"] = req["params"]["symbol"];
        resp["result"]["origQty"] = req["params"]["quantity"];
        resp["result"]["side"] = req["params"]["side"];
        resp["result"]["price"] = req["params"]["price"];
        resp["result"]["orderId"] = order.order_id;
        resp["result"]["transactTime"] = timestamp();
        resp["result"]["workingTime"] = timestamp();
        this->_idle_thread->dispatch([this, sp=session->shared_from_this(), resp]() {
          sp->send(resp.dump());
        });
      };

      // create a resting order object
      SimRestingOrder resting_order;
      resting_order.symbol = symbol;
      resting_order.side = req["params"]["side"].get<std::string>();
      resting_order.quantity = req["params"]["quantity"].get<double>();
      resting_order.price = req["params"]["price"].get<double>();
      resting_order.client_order_id = req["params"]["newClientOrderId"].get<std::string>();
      resting_order.order_id = auto_order_id;
      resting_order.created = Time::realtime_now();
      resting_order.account = iter->second;

      // Note: when accepting an order, there are various scenarios that can
      // happen.

      enum class Scenario {
        delayed_fill,
        execution_reports_first,
        no_fill,
        no_fill_expire,
      };

      Scenario scenario = Scenario::no_fill;


      if (scenario == Scenario::no_fill) {
        sim_acct->add_new_order(std::move(resting_order), on_order_inserted);
        return;
      }

      if (scenario == Scenario::execution_reports_first) {
       // Add the order, but don't send the initial order reply - that will
        // come later.  Adding the order will however result in an execution
        // report being emitted.
        sim_acct->add_new_order(resting_order, {});

        // generate a full fill
        _idle_thread->dispatch_after(std::chrono::milliseconds(500),
                                     [this,auto_order_id,sim_acct](){
                                       sim_acct->exec_order(auto_order_id, true);
                                     });

        // generate the order accept - will arrive after the execution reports
        _idle_thread->dispatch_after(std::chrono::milliseconds(1500),
                                     [this, on_order_inserted, resting_order](){
                                       on_order_inserted(resting_order);
                                     });

        return;
      }

      if (scenario == Scenario::delayed_fill) {
        sim_acct->add_new_order(std::move(resting_order), on_order_inserted);

        // schedule a fill
        _idle_thread->dispatch(std::chrono::milliseconds(3500),
                               [this,auto_order_id,sim_acct](){
                                 auto again = sim_acct->exec_order(auto_order_id);
                                 return std::chrono::milliseconds(again? 3500:0);
                               });
        return;
      }

      if (scenario == Scenario::no_fill_expire) {
        sim_acct->add_new_order(resting_order, on_order_inserted);

        // schedule a lapse
        _idle_thread->dispatch_after(std::chrono::seconds(5),
                                     [this,auto_order_id,sim_acct]() {
                                       sim_acct->expire_order(auto_order_id);
                                     });
        return;
      }

      throw std::runtime_error("scenario not handled");
    }
    else {
      throw std::runtime_error("cannot find a SimAccount");
    }
  }
}


void BinanceSimulator::process_order_cancel_request(WebsocketServerSession* ws,
                                                    json& req)
{
  auto account = find_account(ws->shared_from_this());

  if (options.reject_all_cancel) {
    json resp = json::parse(cancel_order_reject_template);
    resp["id"] = req["id"];
    resp["error"]["code"] = -2011;
    resp["error"]["msg"] = "order cannot be cancelled";
    ws->send(resp.dump());
    return;
  }

  // callback to notify the private channel of the cancel before its
  // published on the user channel
  auto after_cancel_cb = [ws, req](SimRestingOrder& order){
    auto resp = json::parse(cancel_ack);
    resp["id"] = req["id"];
    resp["result"]["symbol"] = order.symbol;
    resp["result"]["origClientOrderId"] = order.client_order_id;
    resp["result"]["orderId"] = order.order_id;
    resp["result"]["transactTime"] = timestamp();
    resp["result"]["price"] = format_double(order.price);
    resp["result"]["origQty"] = format_double(order.quantity);
    resp["result"]["executedQty"] = format_double(order.tot_exec_qty);
    resp["result"]["side"] = order.side;
    ws->send(resp.dump());
  };

  size_t orderId = req["params"]["orderId"].get<size_t>();

  if (account->cancel_order(orderId, after_cancel_cb)) {
    // success - nothing to do
  }
  else {
    json resp = json::parse(order_reject);
    resp["id"] = req["id"];
    ws->send(resp.dump());
  }
}


void BinanceSimulator::process_session_logon(WebsocketServerSession* ws,
                                             json& req)
{
  /* io-thread */

  auto apikey = req["params"]["apiKey"].get<std::string>();

  if (apikey == BINANCE_UAT_API_KEY)  {
    LOG_INFO("client logon successful");

    attach_to_account(ws->shared_from_this());

    auto resp = json::parse(session_logon_ack);
    resp["id"] = req["id"];
    resp["result"]["apiKey"] = req["params"]["apiKey"];
    resp["result"]["authorizedSince"] = timestamp();
    resp["result"]["serverTime"] = timestamp();
    resp["result"]["connectedSince"] = timestamp();
    ws->send(resp.dump());
  }
  else
  {
    LOG_WARN("rejecting logon, incorrect apiKey");
    json resp = json::parse(logon_reject_template);
    resp["id"] = req["id"];
    ws->send(resp.dump());
  }
}

void BinanceSimulator::process_user_data_stream_start(WebsocketServerSession* ws,
                                                      json& req)
{
  json resp = json::parse(user_data_stream_start_ack);
  resp["id"] = req["id"];
  resp["result"]["listenKey"] = this->start_user_data_stream(ws->shared_from_this());
  ws->send(resp.dump());
}


void BinanceSimulator::process_user_data_stream_subscribe(
  WebsocketServerSession* ws,
  json& req)
{
  if (const auto account = find_account(ws->shared_from_this())) {
    // subscribe to user stream
    account->subscribe_user_data(ws->shared_from_this());

    // send accept
    json resp = json::parse(user_data_stream_subscribe_ack);
    resp["id"] = req["id"];
    ws->send(resp.dump());
  }
  else {
    LOG_ERROR("not implemented: subscribing when not logged in");
  }
}


void BinanceSimulator::on_line_message(WebsocketServerSession* session,
                                       const char* buf, size_t n)
{
  /* io-thread */

  // Message received from Apex engine, eg, SubmitOrder or Cancel Order.
  auto sp = session->shared_from_this();
  auto req = json::parse(buf, buf + n);

  if (req["method"] == "session.logon") {
    process_session_logon(session, req);
    return;
  }

  if (req["method"] == "userDataStream.start") {
    process_user_data_stream_start(session, req);
    return;
  }

  if (req["method"] == "order.cancel") {
    process_order_cancel_request(session, req);
    return;
  }

  if (req["method"] == "order.place") {
    process_submit_order_request(session, req);
    return;
  }

  if (req["method"] == "userDataStream.subscribe") {
    process_user_data_stream_subscribe(session, req);
    return;
  }

  LOG_WARN("simulator didn't handle request: " << req);
}


int main()
{
  Logger::configure(Logger::info);
  apex::Logger::instance().register_thread_id("main");

  Reactor reactor;
  RealtimeEventLoop idle_thread{
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("idle");
    }};

  BinanceSimulator sim("127.0.0.1",
                       "9010",
                       &reactor,
                       &idle_thread);

  sim.start();

  try {
    while (1)
      sleep(60);

    return 0;
  }
  catch (const std::exception& err) {
    cerr << err.what() << endl;
    return 1;
  }
}
