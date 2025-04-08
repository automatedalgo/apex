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

#include <apex/gx/GxServer.hpp>

#include <apex/comm/GxServerSession.hpp>
#include <apex/core/Errors.hpp>
#include <apex/infra/SocketAddress.hpp>
#include <apex/model/StrategyId.hpp>
#include <apex/util/Error.hpp>

#define DEFAULT_GX_PORT 5780

namespace apex {

ExchangeSubscription::ExchangeSubscription(
  std::shared_ptr<apex::BaseExchangeSession> exchange_session,
  ExchangeSubscriptionKey sym)
  : _exchange_session(std::move(exchange_session)), _symbol(sym)
{

  apex::Symbol symbol;
  symbol.native = sym.symbol;
}


void ExchangeSubscription::activate()
{
  apex::Symbol symbol;
  symbol.native = _symbol.symbol;

  // Note: storing shared_ptr in the callback here, deliberate, dont think we
  // need to have the wp yet.

  auto sp = shared_from_this();
  std::function<void(TickTrade)> callback = [sp](TickTrade tick) {
    // update local market-view, for later snapshot requests
    sp->_market.apply(tick);

    // broadcast the update to all connect server-sessions
    std::set<std::shared_ptr<GxServerSession>> drop_list;
    for (auto& item : sp->_subscribers) {
      try {
        item->send(sp->_symbol.symbol, sp->_symbol.exchange_id, tick);
      } catch (std::exception& err) {
        // if write has failed, indicates session has an
        // error, so no longer want to send updates;
        // move to drop list
        drop_list.insert(std::move(item));
      }
    }

    if (!drop_list.empty()) {
      // if we have sessions to drop, we rebuild the
      // subscriber list here
      std::vector<std::shared_ptr<GxServerSession>> newsubs;
      for (auto& item : sp->_subscribers) {
        if (item)
          newsubs.push_back(std::move(item));
      }

      sp->_subscribers = std::move(newsubs);

      // finally, clear the drop-list; we do this
      // deliberately post-loop so that GxSessionClient
      // destructors are triggered from here.
      drop_list.clear();
    }
  };
  apex::subscription_options options(apex::StreamType::Trades);

  // TODO: the exchange requests made should be controled via a StreamReq
  // conveyed in the subscription options.

  _exchange_session->subscribe_trades(symbol, options, callback);
  _exchange_session->subscribe_top(symbol, options, [sp](TickTop tick) {
    sp->_market.apply(tick);

    // broadcast the update to all connect server-sessions
    for (auto& item : sp->_subscribers) {
      try {
        item->send(sp->_symbol.symbol, sp->_symbol.exchange_id, tick);
      } catch (std::exception& e) {
        LOG_ERROR("exception during GX send: " << e.what());
        // TODO: how best to drop this client?
      }
    }


    // TODO: prune any bad connections
  });
}

void ExchangeSubscription::subscribe(GxServerSession& session)
{
  _subscribers.push_back(session.shared_from_this());
}


void AccountTopic::start_exchange_subscription(
  std::shared_ptr<apex::BaseExchangeSession> session)
{
  _exchange_session = std::move(session);
  // _exchange_session->subscribe_account(
  //   [this](std::vector<AccountUpdate> updates) {
  //     // apply the exchange update to the model
  //     this->_model.apply(updates);

  //     // notify subscribers
  //     for (auto& item : _subscribers) {
  //       item->send(this->_exchange_session->exchangeId(),
  //                  updates); // TODO: handle exceptions
  //     }
  //   });
}


GxServer::GxServer(apex::RunMode run_mode,
                   Config config)
  : _run_mode(run_mode),
    _config(std::move(config)),
    _external_event_loop(nullptr),
    _port(0)
{
  if (run_mode == apex::RunMode::backtest)
    throw std::runtime_error("GxServer does not support RunMode::backtest");

  SslConfig sslconf(true);
  _ssl = std::make_unique<SslContext>(sslconf);

  _own_event_loop = std::make_unique<apex::RealtimeEventLoop>(
      [](){
        return false;
      },
      [] {
        apex::Logger::instance().register_thread_id("gxev");
      });

  _port = _config.get_uint("port", DEFAULT_GX_PORT);
}


GxServer::GxServer(RealtimeEventLoop* external_event_loop,
                   apex::RunMode run_mode,
                   Config config)
  : _run_mode(run_mode),
    _config(std::move(config)),
    _external_event_loop(external_event_loop),
    _try_other_ports(true),
    _port(0)
{
  if (run_mode == apex::RunMode::backtest)
    throw std::runtime_error("GxServer does not support RunMode::backtest");

  SslConfig sslconf(true);
  _ssl = std::make_unique<SslContext>(sslconf);
  _port = _config.get_uint("port", 5780);
}


RealtimeEventLoop* GxServer::event_loop() {
  if (_external_event_loop)
    return _external_event_loop;
  if (_own_event_loop)
    return _own_event_loop.get();
  return nullptr;
}


GxServer::~GxServer()
{
  if (_own_event_loop)
    _own_event_loop->sync_stop();
}


bool GxServer::on_logon_request(GxServerSession& s, std::string id,
                                RunMode run_mode)
{
  if (run_mode != _run_mode) {
    LOG_WARN("om-logon rejected, client's run-mode does not match GX run-mode");
    return false;
  }

  if (_gx_session_map.find(id) == std::end(_gx_session_map)) {
    _gx_session_map.insert({id, s.shared_from_this()});
    LOG_INFO("om-logon accepted, strategyId: " << QUOTE(id));
    return true;
  } else {
    // TODO: need an error code for this.
    LOG_WARN("om-logon rejected, strategy-id " << QUOTE(id)
             << " already connected");
    return false;
  }
}

void GxServer::add_venue(BinanceSession::Params params)
{
  BaseExchangeSession::EventCallbacks callbacks;
  callbacks.on_order_fill = [this](BaseExchangeSession& exchange,
                                   std::string order_id, OrderFill msg) {
    this->on_fill(exchange, std::move(order_id), msg);
  };
  callbacks.on_order_cancel = [this](BaseExchangeSession& exchange,
                                     std::string order_id, OrderUpdate msg) {
    this->on_unsol_cancel(exchange, order_id, msg);
  };

  // TODO: check, if binance already added, throw.
  auto sp = std::make_shared<apex::BinanceSession>(
      callbacks, params, _run_mode, &_reactor, *event_loop(), _ssl.get());
  _exchange_sessions.insert({ExchangeId::binance, sp});
  sp->start();
}

void GxServer::start()
{


  // auto account_callback = [this](std::map<std::string, AccountUpdate>
  // updates) {
  //   for (auto& item : updates) {
  //     ExchangeSubscriptionKey key;
  //     key.exchange = "binance";
  //     key.symbol = item.first;
  //   }

  // };

  // const bool paper_mode = _run_mode == RunMode::paper;

  BaseExchangeSession::EventCallbacks callbacks;
  callbacks.on_order_fill = [this](BaseExchangeSession& exchange,
                                   std::string order_id, OrderFill msg) {
    this->on_fill(exchange, std::move(order_id), msg);
  };
  callbacks.on_order_cancel = [this](BaseExchangeSession& exchange,
                                     std::string order_id, OrderUpdate msg) {
    this->on_unsol_cancel(exchange, order_id, msg);
  };

  // create connections to venues

  auto exchanges_config =
      _config.get_sub_config("exchanges", Config::empty_config());

  if (exchanges_config.is_array()) {
    for (size_t i = 0; i < exchanges_config.array_size(); i++) {
      auto config = exchanges_config.array_item(i);
      auto session_type = config.get_string("type");
      if (session_type == "binance") {
        auto sp = std::make_shared<apex::BinanceSession>(
            callbacks, config, _run_mode, &_reactor, *event_loop(), _ssl.get());
        _exchange_sessions.insert({ExchangeId::binance, sp});
        sp->start();
      } // else if (session_type == "binance_usdfut") {
        // auto sp = std::make_shared<apex::BinanceUsdFutSession>(
        //     callbacks, config, sim_mode, &_reactor, *_event_loop.get(),
        //     _ssl.get());
        // _exchange_sessions.insert({"binance-usdfut", sp});
        // sp->start();
      // }
      else {
        std::ostringstream oss;
        oss << "invalid exchange-type, " << QUOTE(session_type);
        throw ConfigError(oss.str());
      }
    }
  }

  int remaining_port_attempts = _try_other_ports? 100 : 1;

  while (true) {
    auto err = create_listen_socket();
    if (!err)
      break;
    if (--remaining_port_attempts > 0)
      _port += 1;
    else
      throw std::runtime_error("unable to obtain listen socket");
  }
}

int GxServer::create_listen_socket() {
  // create the GX server socket
  auto sock = std::make_unique<TcpSocket>(&_reactor);
  // auto on_accept = [this](std::unique_ptr<TcpSocket>& sk, UvErr e) {
  //   if (e) {
  //     THROW("accept() failed: " << e);
  //   }
  //   GxServerSession::EventHandlers handlers{
  //       [this](GxServerSession& s) { on_error(s); },
  //       [this](GxServerSession& s, GxSubscribeRequest& req) {
  //         on_subscribe(s, req);
  //       },
  //       [this](GxServerSession& /*s*/, ExchangeId /*exchange*/) {
  //         //on_subscribe_wallet(s, exchange);
  //       },
  //       [this](GxServerSession& s, GxServerSession::Request r, OrderParams p) {
  //         on_submit_order(s, r, p);
  //       },
  //       [this](GxServerSession& s, GxServerSession::Request r, ExchangeId exch,
  //              std::string sym, std::string oid, std::string eid) {
  //         this->on_cancel_order_request(s, r, exch, sym, oid, eid);
  //       },
  //       [this](GxServerSession& s, GxLogonRequest request) -> bool {
  //         return this->on_logon_request(s, request.strategy_id,
  //                                       request.run_mode);
  //       }
  //   };
  //   auto client = std::make_shared<GxServerSession>(&_reactor, *event_loop(),
  //                                                   std::move(sk), handlers);
  //   event_loop()->dispatch([this, client]() { this->new_client(client); });
  // };
  auto node = "0.0.0.0";

  auto on_accept_cb = [this](std::unique_ptr<TcpSocket>& sk){

    // if (e) {
    //   THROW("accept() failed: " << e);
    // }
    GxServerSession::EventHandlers handlers{
        [this](GxServerSession& s) { on_error(s); },
        [this](GxServerSession& s, GxSubscribeRequest& req) {
          on_subscribe(s, req);
        },
        [this](GxServerSession& /*s*/, ExchangeId /*exchange*/) {
          //on_subscribe_wallet(s, exchange);
        },
        [this](GxServerSession& s, GxServerSession::Request r, OrderParams p) {
          on_submit_order(s, r, p);
        },
        [this](GxServerSession& s, GxServerSession::Request r, ExchangeId exch,
               std::string sym, std::string oid, std::string eid) {
          this->on_cancel_order_request(s, r, exch, sym, oid, eid);
        },
        [this](GxServerSession& s, GxLogonRequest request) -> bool {
          return this->on_logon_request(s, request.strategy_id,
                                        request.run_mode);
        }
    };
    auto client = std::make_shared<GxServerSession>(&_reactor, *event_loop(),
                                                    std::move(sk), handlers);
    event_loop()->dispatch([this, client]() { this->new_client(client); });
  };

  // TODO: detect failure to listen on the provided port
  sock->listen(/*node, */ _port, on_accept_cb);

//  auto fut = sock->listen(node, std::to_string(_port), on_accept);

  // auto uv_err = fut.get();

  // if (uv_err) {
  //   LOG_WARN("failed to listen on port " << _port << ", error: " << uv_err);
  //   sock->close().wait();
  //   return uv_err;
  // }
  // else {
  //   _server_sock = std::move(sock);
  //   LOG_INFO("listening for GX connections on " << node << ":" << _port);
  //   return UvErr{};
  // }
}

void GxServer::new_client(std::shared_ptr<GxServerSession> session)
{
  assert(event_loop()->this_thread_is_ev());

  auto sock = session->get_socket2();
  // LOG_INFO("received new GX connection from "
  //          << sock->get_peer_address().to_string() << ":"
  //          << sock->get_peer_port());
  _gx_sessions.push_back(session);

  session->start_read([&](GxServerSession& session, int err) {
    LOG_INFO("session closed, error: " << err);
    // on socket err, just remove the session
    for (auto iter = _gx_sessions.begin(); iter != _gx_sessions.end(); ++iter) {
      if (iter->get() == &session) {
        _gx_sessions.erase(iter);
        break;
      }
    }

    // note:  the session might appear several times in the strategy_id map
    for (auto iter = _gx_session_map.begin(); iter != _gx_session_map.end();) {
      if (iter->second.get() == &session) {
        LOG_INFO("removing om-logon for strategyId: " << QUOTE(iter->first));
        _gx_session_map.erase(iter++);
      } else
        ++iter;
    }
  });
}


void GxServer::on_unsol_cancel(BaseExchangeSession& exchange,
                               std::string order_id, OrderUpdate msg)
{
  // TODO: handle order_id too short
  std::string strategy_id = order_id.substr(0, strategy_id_size);

  auto iter = _gx_session_map.find(strategy_id);
  if (iter != std::end(_gx_session_map)) {
    iter->second->send_order_unsol_cancel(exchange.exchange_id(), order_id, msg);
  } else {
    LOG_WARN("no GX-session found for order-unsol-cancel, exchange:"
             << exchange.exchange_id() << ", orderId:" << order_id
             << ", strategyId:" << strategy_id);
  }
}


void GxServer::on_fill(BaseExchangeSession& exchange, std::string order_id,
                       OrderFill fill)
{
  // TODO: handle order_id too short
  std::string strategy_id = order_id.substr(0, strategy_id_size);

  auto iter = _gx_session_map.find(strategy_id);
  if (iter != std::end(_gx_session_map)) {
    iter->second->send_order_fill(exchange.exchange_id(), order_id, fill);
  } else {
    LOG_WARN("no GX-session found for order-fill, exchange:"
             << exchange.exchange_id() << ", orderId:" << order_id
             << ", strategyId:" << strategy_id);
  }
}


void GxServer::on_error(GxServerSession&)
{
  LOG_WARN("GxServer::on_error");
}


void GxServer::on_cancel_order_request(GxServerSession& session,
                                       GxServerSession::Request request,
                                       ExchangeId exchange,
                                       std::string symbol,
                                       std::string order_id,
                                       std::string ext_order_id)
{
  // find the exchange
  auto iter = _exchange_sessions.find(exchange);
  if (iter == std::end(_exchange_sessions)) {
    LOG_WARN("no exchange session for requested venue " << QUOTE(exchange));
    session.send_error(request, error::e0001, "exchange not found");
    return;
  }


  // TODO: session should be a weak-pointer here?

  // order callbacks
  BaseExchangeSession::SubmitOrderCallbacks callbacks;
  callbacks.on_rejected = [&session, request](std::string code,
                                              std::string error) {
    session.send_error(request, code, error);
  };
  callbacks.on_reply = [&session, request](OrderUpdate update) {
    session.send(request, update);
  };

  iter->second->cancel_order(symbol, order_id, ext_order_id, callbacks);
}


void GxServer::on_subscribe(GxServerSession& session,
                            GxSubscribeRequest& req)
{
  assert(event_loop()->this_thread_is_ev());

  ExchangeSubscriptionKey key;
  key.exchange_id = req.exchange;
  key.symbol = req.symbol;

  auto iter = _exchange_subscriptions.find(key);
  if (iter == std::end(_exchange_subscriptions)) {
    LOG_WARN("no exchange subscription for " << key.symbol);

    // create a subscription
    // LOG_INFO("creating exchange subscription object");
    auto exchange_session = _exchange_sessions[ExchangeId::binance];
    std::shared_ptr<ExchangeSubscription> sub =
      std::make_shared<ExchangeSubscription>(exchange_session, key);
    auto ins = _exchange_subscriptions.insert({key, sub});
    sub->activate();
    iter = ins.first;
  }

  iter->second->subscribe(session);
}


// // TODO: rename this method to refer to account
// void GxServer::on_subscribe_wallet(GxServerSession& session,
//                                    std::string exchange)
// {
//   assert(event_loop()->this_thread_is_ev());

//   auto exchange_iter = _exchange_sessions.find(exchange);
//   if (exchange_iter == std::end(_exchange_sessions)) {
//     LOG_WARN("ignoring subscription request for unsupported exchange '"
//              << exchange << "'");
//     // TODO: here we need to reply to the client to indicate the subscription
//     // has failed.
//     return;
//   }

//   ExchangeId2 eid{exchange};

//   auto iter = _wallets.find(eid);
//   if (iter == std::end(_wallets)) {

//     auto up = std::make_unique<AccountTopic>();
//     iter = _wallets.insert({eid, std::move(up)}).first;
//     // TODO: resolve the exchange name to BinanceSession, or other session etc.
//     // TODO: check the session actually exists
//     iter->second->start_exchange_subscription(exchange_iter->second);
//   }

//   iter->second->add_subscriber(session.shared_from_this());

//   // TODO: need to send the initial snapshot

//   // TODO/NEXT:  continue here.  This should trigger the creation of the Account
//   // model, it's setup, and then add the GxServerSession as a subscriber.

//   // TODO: create the account model

//   // TODO: request all sessions start the stream, which menas snapshot & updates

//   //
// }


void GxServer::on_submit_order(GxServerSession& session,
                               GxServerSession::Request req,
                               OrderParams& params)
{
  auto iter = _exchange_sessions.find(params.exchange);
  if (iter == std::end(_exchange_sessions)) {
    LOG_WARN("no exchange session for requested venue "
             << QUOTE(params.exchange));
    session.send_error(req, error::e0001, "exchange not found");
    return;
  }

  // TODO: session should be a weak-pointer here?

  // order callbacks
  BaseExchangeSession::SubmitOrderCallbacks callbacks;
  callbacks.on_rejected = [&session, req](std::string code, std::string error) {
    LOG_INFO("ERROR : " << code << "," << error);
    session.send_error(req, code, error);
  };
  callbacks.on_reply = [&session, req](OrderUpdate update) {
    session.send(req, update);
  };

  iter->second->submit_order(params, callbacks);
}

} // namespace apex
