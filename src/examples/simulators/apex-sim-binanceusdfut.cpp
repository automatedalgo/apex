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

static size_t timestamp() {
  return Time::realtime_now().as_epoch_ms().count();
}

const std::string order_expired_template = R"(
{"e":"ORDER_TRADE_UPDATE","T":1753184304488,"E":1753184304488,"o":{"s":"BTCUSDT","c":"0000000003","S":"BUY","o":"LIMIT","f":"GTC","q":"0.001","p":"117912","ap":"0","sp":"0","x":"CANCELED","X":"CANCELED","i":736189971752,"l":"0","z":"0","L":"0","n":"0","N":"USDT","T":1753184304488,"t":0,"b":"0","a":"0","m":false,"R":false,"wt":"CONTRACT_PRICE","ot":"LIMIT","ps":"BOTH","cp":false,"rp":"0","pP":false,"si":0,"ss":0,"V":"EXPIRE_MAKER","pm":"NONE","gtd":0}})";

const std::string order_trade_update_template = R"(
{
  "e": "ORDER_TRADE_UPDATE",
  "T": 1752925392000,
  "E": 1752925392000,
  "o": {
    "s": "BTCUSDT",
    "c": "0000000003",
    "S": "BUY",
    "o": "LIMIT",
    "f": "GTC",
    "q": "0.001",
    "p": "111912",
    "ap": "0",
    "sp": "0",
    "x": "NEW",
    "X": "NEW",
    "i": 733832495338,
    "l": "0",
    "z": "0",
    "L": "0",
    "n": "0",
    "N": "USDT",
    "T": 1752925392000,
    "t": 0,
    "b": "111.912",
    "a": "0",
    "m": false,
    "R": false,
    "wt": "CONTRACT_PRICE",
    "ot": "LIMIT",
    "ps": "BOTH",
    "cp": false,
    "rp": "0",
    "pP": false,
    "si": 0,
    "ss": 0,
    "V": "EXPIRE_MAKER",
    "pm": "NONE",
    "gtd": 0
  }
})";


const std::string trade_lite_template = R"(
{
  "e": "TRADE_LITE",
  "E": 1752884381175,
  "T": 1752884381175,
  "s": "BTCUSDT",
  "q": "0.022",
  "p": "0.00",
  "m": false,
  "c": "clientorderid",
  "S": "SELL",
  "L": "117890.10",
  "l": "0.001",
  "t": 6485886676,
  "i": 733553672133
}
)";


const std::string trade_partial_template = R"(
{
  "e": "ORDER_TRADE_UPDATE",
  "T": 1752884381175,
  "E": 1752884381176,
  "o": {
    "s": "BTCUSDT",
    "c": "web_ZdriR273pHjIXX2Usbeg",
    "S": "SELL",
    "o": "MARKET",
    "f": "GTC",
    "q": "0.022",
    "p": "0",
    "ap": "117890.1",
    "sp": "0",
    "x": "TRADE",
    "X": "PARTIALLY_FILLED",
    "i": 733553672133,
    "l": "0.002",
    "z": "0.007",
    "L": "117890.1",
    "n": "0.1178901",
    "N": "USDT",
    "T": 1752884381175,
    "t": 6485886680,
    "b": "0",
    "a": "0",
    "m": false,
    "R": true,
    "wt": "CONTRACT_PRICE",
    "ot": "MARKET",
    "ps": "BOTH",
    "cp": false,
    "rp": "0.0594",
    "pP": false,
    "si": 0,
    "ss": 0,
    "V": "EXPIRE_MAKER",
    "pm": "NONE",
    "gtd": 0
  }
})";


const std::string trade_filled_template = R"(
{
  "e": "ORDER_TRADE_UPDATE",
  "T": 1752884381175,
  "E": 1752884381176,
  "o": {
    "s": "BTCUSDT",
    "c": "web_ZdriR273pHjIXX2Usbeg",
    "S": "SELL",
    "o": "MARKET",
    "f": "GTC",
    "q": "0.022",
    "p": "0",
    "ap": "117890.1",
    "sp": "0",
    "x": "TRADE",
    "X": "FILLED",
    "i": 733553672133,
    "l": "0.015",
    "z": "0.022",
    "L": "117890.1",
    "n": "0.88417575",
    "N": "USDT",
    "T": 1752884381175,
    "t": 6485886681,
    "b": "0",
    "a": "0",
    "m": false,
    "R": true,
    "wt": "CONTRACT_PRICE",
    "ot": "MARKET",
    "ps": "BOTH",
    "cp": false,
    "rp": "0.4455",
    "pP": false,
    "si": 0,
    "ss": 0,
    "V": "EXPIRE_MAKER",
    "pm": "NONE",
    "gtd": 0
  }
}
)";


const std::string submit_order_ack_template = R"(
{
  "id": "3f7df6e3-2df4-44b9-9919-d2f38f90a99a",
  "status": 200,
  "result": {
    "orderId": 730878025055,
    "symbol": "BTCUSDT",
    "status": "NEW",
    "clientOrderId": "dem010000123",
    "price": "116650.00",
    "avgPrice": "0.00",
    "origQty": "0.001",
    "executedQty": "0.000",
    "cumQty": "0.000",
    "cumQuote": "0.00000",
    "timeInForce": "GTC",
    "type": "LIMIT",
    "reduceOnly": false,
    "closePosition": false,
    "side": "SELL",
    "positionSide": "BOTH",
    "stopPrice": "0.00",
    "workingType": "CONTRACT_PRICE",
    "priceProtect": false,
    "origType": "LIMIT",
    "priceMatch": "NONE",
    "selfTradePreventionMode": "EXPIRE_MAKER",
    "goodTillDate": 0,
    "updateTime": 1752611645940
  }
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


const std::string submit_order_rej_template = R"(
{"id":"3f7df6e3-2df4-44b9-9919-d2f38f90a99a","status":400,"error":{"code":-2019,"msg":"Margin is insufficient."}}
)";

const std::string cancel_ack_template = R"(
{
  "id": "3f7df6e3-2df4-44b9-9919-d2f38f90a99a",
  "status": 200,
  "result": {
    "orderId": 731753480298,
    "symbol": "BTCUSDT",
    "status": "CANCELED",
    "clientOrderId": "APX1752698235524",
    "price": "119150.00",
    "avgPrice": "0.00",
    "origQty": "0.001",
    "executedQty": "0.000",
    "cumQty": "0.000",
    "cumQuote": "0.00000",
    "timeInForce": "GTC",
    "type": "LIMIT",
    "reduceOnly": false,
    "closePosition": false,
    "side": "BUY",
    "positionSide": "BOTH",
    "stopPrice": "0.00",
    "workingType": "CONTRACT_PRICE",
    "priceProtect": false,
    "origType": "LIMIT",
    "priceMatch": "NONE",
    "selfTradePreventionMode": "EXPIRE_MAKER",
    "goodTillDate": 0,
    "updateTime": 1752698245647
  }
}
)";

auto order_update_solicited_cancel_ack = R"(
{
  "e": "ORDER_TRADE_UPDATE",
  "T": 1752698245647,
  "E": 1752698245647,
  "o": {
    "s": "BTCUSDT",
    "c": "APX1752698235524",
    "S": "BUY",
    "o": "LIMIT",
    "f": "GTC",
    "q": "0.001",
    "p": "119150",
    "ap": "0",
    "sp": "0",
    "x": "CANCELED",
    "X": "CANCELED",
    "i": 731753480298,
    "l": "0",
    "z": "0",
    "L": "0",
    "n": "0",
    "N": "USDT",
    "T": 1752698245647,
    "t": 0,
    "b": "0",
    "a": "0",
    "m": false,
    "R": false,
    "wt": "CONTRACT_PRICE",
    "ot": "LIMIT",
    "ps": "BOTH",
    "cp": false,
    "rp": "0",
    "pP": false,
    "si": 0,
    "ss": 0,
    "V": "EXPIRE_MAKER",
    "pm": "NONE",
    "gtd": 0
  }
}
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
      LOG_INFO("*** request_timer_cb("<<interval.count()<<") *** ");

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

    auto protocol_closed_fn = [this](std::chrono::milliseconds s) {
      LOG_INFO("*** protocol_closed_fn(" << s.count() << ")  *** s=" );
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
        LOG_ERROR("socket disconnected");
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



class WebsocketServer {
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
    LOG_INFO(_label << ": listening on " << _host << ":" << _port);
  }


  void on_new_client(unique_ptr<TcpSocket>& sock) {
    LOG_INFO("connection received");
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

struct SimRestingOrder {
  std::string symbol;
  std::string side;
  std::string client_order_id;
  size_t order_id;
  double quantity;
  double price;
  Time created;
  double tot_exec_qty;

  std::shared_ptr<SimAccount> account;
};

class SimAccount {
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
      LOG_INFO("order cancelled: " << order.symbol << " " << order.order_id);
      if (notify_private)
        notify_private(order);

      // TODO: the following is BinanceUsdFut specific, so should be moved out
      // to a separate class.
      json jm = json::parse(order_update_solicited_cancel_ack);
      jm["T"] = timestamp();
      jm["E"] = timestamp();
      jm["o"]["s"] = order.symbol;
      jm["o"]["c"] = order.client_order_id;
      jm["o"]["S"] = order.side;
      jm["o"]["i"] = order.order_id;
      jm["o"]["T"] = timestamp();
      auto data = jm.dump();
      for (auto & item : _user_data_subs)
        item->send(data);
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

    LOG_INFO("expiring order " << order_id);

    // publish an ORDER_TRADE_UPDATE
    json msg = json::parse(order_expired_template);
    msg["E"] = Time::realtime_now().as_epoch_ms().count();
    msg["T"] = Time::realtime_now().as_epoch_ms().count();
    msg["o"]["s"] = order.symbol;
    msg["o"]["S"] = order.side;
    msg["o"]["c"] = order.client_order_id;
    msg["o"]["q"] = std::to_string(order.quantity);
    msg["o"]["p"] = std::to_string(order.price); // orig price
    msg["o"]["i"] = order_id;
    auto data = msg.dump();
    for (auto & item : _user_data_subs)
      item->send(data);
  }

  bool exec_order(size_t order_id) {
    std::lock_guard<std::mutex> guard(_mtx);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(0.5);
    std::vector<double> v{0.25, 0.5, 1.0};

    auto iter = _orders.find(order_id);
    if (iter != _orders.end()) {

      std::shuffle(v.begin(), v.end(), gen);
      bool is_maker = dist(gen);

      auto want_exec_qty = iter->second.quantity * v[0];
      auto remain_qty = iter->second.quantity - iter->second.tot_exec_qty;
      auto exec_qty = std::min(want_exec_qty, remain_qty);

      if (!dbl_is_zero(exec_qty)) {
        iter->second.tot_exec_qty += exec_qty;
        remain_qty -= exec_qty;

        // post a TRADE_LITE
        auto msg = json::parse(trade_lite_template);
        msg["E"] = Time::realtime_now().as_epoch_ms().count();
        msg["T"] = Time::realtime_now().as_epoch_ms().count();
        msg["s"] = iter->second.symbol;
        msg["q"] = std::to_string(iter->second.quantity);
        msg["p"] = std::to_string(iter->second.price); // orig price
        msg["S"] = iter->second.side;
        msg["m"] = is_maker;
        msg["L"] = std::to_string(iter->second.price); // exec price
        msg["l"] = format_double(exec_qty, true); // exec qty
        msg["i"] = order_id;
        auto data = msg.dump();

        LOG_INFO("order exec: " << order_id);
        for (auto & item : _user_data_subs)
          item->send(data);
      }

      if (dbl_is_zero(remain_qty)) {
        LOG_INFO("order filled: " << order_id);
        _orders.erase(iter);
        return false;
      }
      else
        return true;
    }
    return false;
  }

  void add_new_order(SimRestingOrder order) {
    std::lock_guard<std::mutex> guard(_mtx);

    order.tot_exec_qty = 0;

    LOG_INFO("order added: " << order.symbol << " "
             << order.client_order_id
             << " "
             << order.order_id);
    _orders[order.order_id] = std::move(order);

    // post a ORDER_TRADE_UPDATE event
    auto resp = json::parse(R"({"e":"ORDER_TRADE_UPDATE","T":1752595146453,"E":1752595146453,"o":{"s":"BTCUSDT","c":"dem010000123","S":"BUY","o":"LIMIT","f":"GTC","q":"0.001","p":"115300","ap":"0","sp":"0","x":"NEW","X":"NEW","i":730711776269,"l":"0","z":"0","L":"0","n":"0","N":"USDT","T":1752595146453,"t":0,"b":"115.3","a":"0","m":false,"R":false,"wt":"CONTRACT_PRICE","ot":"LIMIT","ps":"BOTH","cp":false,"rp":"0","pP":false,"si":0,"ss":0,"V":"EXPIRE_MAKER","pm":"NONE","gtd":0}})");
    resp["o"]["s"] = order.symbol;
    resp["o"]["S"] = order.side;
    resp["o"]["c"] = order.client_order_id;
    resp["o"]["T"] = Time::realtime_now().as_epoch_ms().count();
    resp["o"]["i"] = order.order_id;
    resp["o"]["q"] = format_double(order.quantity);
    resp["o"]["p"] = format_double(order.price);
    resp["E"] = Time::realtime_now().as_epoch_ms().count();
    resp["T"] = Time::realtime_now().as_epoch_ms().count();
    auto data = resp.dump();
    for (auto & item : _user_data_subs) {
      item->send(data);
    }
  }

  mutable std::mutex _mtx;
  std::string _listenkey;
  std::vector<std::shared_ptr<WebsocketServerSession>> _user_data_subs;
  std::map<size_t, SimRestingOrder> _orders;
};


class BinanceUsdFutSimulator {
public:

  std::mutex _mtx; // fat lock
  std::map<std::shared_ptr<WebsocketServerSession>, std::shared_ptr<SimAccount>> _accounts;

  BinanceUsdFutSimulator(std::string line_host,
                         std::string line_port,
                         std::string feed_host,
                         std::string feed_port,
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

    // user feed for fills
    _feed = std::make_unique<WebsocketServer>(
      feed_host, feed_port, "feed", reactor, idle_thread,
      [this](WebsocketServerSession* session, const char* buf, size_t n) {
        this->on_feed_message(session, buf, n);
      });
  }


  void start() {
    _line->start();
    _feed->start();
  }

  std::string random_ascii_string(size_t length) {
    static auto chars = "012345678ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_int_distribution<> dist(0, strlen(chars)-1);
    std::string s(length, '\0');
    for (auto& c : s)
      c = chars[dist(gen)];
    return s;
  }

  void session_logon(std::shared_ptr<WebsocketServerSession> ws) {
    std::lock_guard<std::mutex> guard(_mtx);
    auto iter = _accounts.find(ws);
    if (iter == std::end(_accounts)) {
      iter = _accounts.insert({ws, std::make_shared<SimAccount>()}).first;
    }
  }

  std::string start_user_data_stream(std::shared_ptr<WebsocketServerSession> ws) {
    std::lock_guard<std::mutex> guard(_mtx);
    auto iter = _accounts.find(ws);
    if (iter == std::end(_accounts))
      throw std::runtime_error("session has not completed logon");

    auto listenkey = random_ascii_string(64);
    iter->second->set_listenkey(listenkey);
    return listenkey;
  }


  void subscribe_to_sim_account(WebsocketServerSession* session,
                                const std::string& listenkey) {
    std::lock_guard<std::mutex> guard(_mtx);
    for (auto & item : _accounts) {
      if (item.second->listenkey() == listenkey) {
        item.second->subscribe_user_data(session->shared_from_this());
      }
    }
  }


  void process_submit_order(WebsocketServerSession* session,
                            json& req) {

    std::string symbol = req["params"]["symbol"].get<std::string>();

    if (symbol.empty())  {
      // reject
      LOG_WARN("order rejected");
      json resp = json::parse(submit_order_rej_template);
      resp["id"] = req["id"];
      resp["error"]["code"] = -1121;
      resp["error"]["msg"] = "Invalid symbol.";
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
        sim_acct->add_new_order(std::move(resting_order));

        // schedule a lapse
        _idle_thread->dispatch(std::chrono::seconds(20),
                               [this,auto_order_id,sim_acct](){
                                 sim_acct->expire_order(auto_order_id);
                                 return std::chrono::milliseconds(0);
                               });
        // // schedule a fill
        // _idle_thread->dispatch(std::chrono::milliseconds(250),
        //                        [this,auto_order_id,sim_acct](){
        //                          auto again = sim_acct->exec_order(auto_order_id);
        //                          return std::chrono::milliseconds(again? 100:0);
        //                        });
      }
      else {
        throw std::runtime_error("cannot find a SimAccount");
      }
    }

    // order ack
    json resp = json::parse(submit_order_ack_template);

    resp["id"] = req["id"];
    resp["result"]["clientOrderId"] = req["params"]["newClientOrderId"];
    resp["result"]["symbol"] = req["params"]["symbol"];
    resp["result"]["origQty"] = req["params"]["quantity"];
    resp["result"]["side"] = req["params"]["side"];
    resp["result"]["price"] = req["params"]["price"];
    resp["result"]["updateTime"] = ::time(0)*1000;
    resp["result"]["orderId"] = auto_order_id;


    // schedule the order ack with a slight delay, so that the
    // ORDER_TRADE_UPDATE goes out first
    _idle_thread->dispatch(std::chrono::milliseconds(50),
                           [this,sp=session->shared_from_this(), resp](){
                             sp->send(resp.dump());
                             return std::chrono::seconds(0);
                           });

    // EXAMPLE OF AN UPDATE TO THE USER STREAM
    // 1. find the simuattion account
    // auto sp = session->shared_from_this();
    // auto iter = _accounts.find(sp);
    // if (iter != std::end(_accounts)) {
    //   iter->second->push_order_expired();
    // }

  }

  void on_line_message(WebsocketServerSession* session, const char* buf, size_t n) {
    /* io-thread */

    // Message received from Apex engine, typically an order related request

    auto sp = session->shared_from_this();
    auto req = json::parse(buf, buf + n);

    if (req["method"] == "session.logon") {

      auto apikey = req["params"]["apiKey"].get<std::string>();

      if (apikey == "sErnzoWaTESThtDlKTHISISATESTfWppp7t34vOOszl8wNTEST3feMyGv5SjSsLv")  {
        LOG_INFO("logon successful");
        auto example = R"({"id":"1","status":200,"result":{"apiKey":"sErnzoWanLRshtDlKpJUZyb4ClOQfWppp7t34vOOszl8wNPvbE3feMyGv5SjSsLv","authorizedSince":1752611642683,"connectedSince":1752611642443,"returnRateLimits":false,"serverTime":1752611642683}})";
        json resp = json::parse(example);
        resp["id"] = req["id"];
        resp["apiKey"] = req["params"]["apiKey"];
        resp["connectedSince"] = ::time(0)*1000;
        resp["serverTime"] = ::time(0)*1000;
        auto data = resp.dump();

        this->session_logon(session->shared_from_this());

        session->send(data);
      }
      else
      {
        LOG_WARN("rejecting logon, incorrect apiKey");
        json resp = json::parse(logon_reject_template);
        resp["id"] = req["id"];
        session->send(resp.dump());
      }
    }
    else if (req["method"] == "userDataStream.start") {
      json resp = json::parse(R"({"id":"reqkey","status":200,"result":{"listenKey":"REPLACEME"}})");
      resp["id"] = req["id"];
      resp["result"]["listenKey"] = this->start_user_data_stream(session->shared_from_this());
      session->send(resp.dump());
    }
    else if (req["method"] == "order.cancel") {
      // find the account
      if (auto iter = _accounts.find(sp); iter != _accounts.end()) {
        json & params = req["params"];
        std::string symbol = params["symbol"].get<std::string>();
        std::string origClientOrderId = params.value("origClientOrderId", "");
        size_t orderId = params["orderId"].get<size_t>();

        // callback to notify the private channel of the cancel before its
        // published on the user channel
        auto after_cancel = [sp, req](SimRestingOrder& order){
          auto resp = json::parse(cancel_ack_template);
          resp["id"] = req["id"];
          resp["result"]["orderId"] = order.order_id;
          resp["result"]["symbol"] = order.symbol;
          resp["result"]["clientOrderId"] = order.client_order_id;
          resp["result"]["side"] = order.side;
          resp["result"]["origQty"] = order.quantity;
          resp["result"]["updateTime"] = Time::realtime_now().as_epoch_ms().count();
          sp->send(resp.dump());
        };

        if (iter->second->cancel_order(orderId, after_cancel)) {
          // pass
        }
        else {
          // failure - order not found
          LOG_INFO("order cancel failed (clientOrderId: " << origClientOrderId
                   << ", orderId: " << orderId << ")");
          auto resp = json::parse(R"({"id":"3f7df6e3-2df4-44b9-9919-d2f38f90a99a","status":400,"error":{"code":-2011,"msg":"Unknown order sent."}})");
          resp["id"] = req["id"];
          session->send(resp.dump());
        }
      }
    }
    else if (req["method"] == "order.place") {
      process_submit_order(session, req);
    }
    else {
      LOG_WARN("simulator didn't handle request: " << req);
    }
  }


  void on_feed_message(WebsocketServerSession* session, const char* buf, size_t n) {
    /* io-thread */
    auto req = json::parse(buf, buf + n);

    if (req["method"] == "SUBSCRIBE") {
      json resp = json::parse(R"({"result":null,"id":"userdata1"})");
      resp["id"] = req["id"];
      session->send(resp.dump());

      auto listenkey = req["params"][0].get<std::string>();
      this->subscribe_to_sim_account(session, listenkey);
    }
    else {
      LOG_WARN("simulator didn't handle request: " << req);
    }
  }

private:
  Reactor* _reactor;
  RealtimeEventLoop* _idle_thread;
  std::unique_ptr<WebsocketServer> _line;
  std::unique_ptr<WebsocketServer> _feed;
};


int main() {

  // return _main();
  Logger::instance().set_level(Logger::info);
  Logger::instance().set_detail(true);
  apex::Logger::instance().register_thread_id("main");

  Reactor reactor;
  RealtimeEventLoop idle_thread{
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("idle");
    }};

  BinanceUsdFutSimulator sim("127.0.0.1",
                             "9000",
                             "127.0.0.1",
                             "9001",
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
