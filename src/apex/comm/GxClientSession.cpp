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

#include <apex/comm/GxClientSession.hpp>
#include <apex/model/Account.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/model/Order.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/core/Logger.hpp>

#include <netinet/in.h>

namespace apex
{


static apex::pb::Exchange to_exchange(apex::ExchangeId id) {
  switch (id) {
    case ExchangeId::none:
      return apex::pb::exchange_none;
    case ExchangeId::binance:
      return apex::pb::exchange_binance;
    case ExchangeId::binance_usdfut:
      return apex::pb::exchange_binance_usdfut;
    case ExchangeId::binance_coinfut:
      return apex::pb::exchange_binance_coinfut;
    default:
      throw std::runtime_error("invalid ExchangeId during conversion to pb::Exchange");
  }
}


static apex::pb::Side to_side(apex::Side side)
{
  switch (side) {
    case apex::Side::none:
      return apex::pb::Side::side_none;
    case apex::Side::buy:
      return apex::pb::Side::side_buy;
    case apex::Side::sell:
      return apex::pb::side_sell;
    default:
      return apex::pb::Side::side_none;
  }
}


GxClientSession::GxClientSession(IoLoop& ioloop, RealtimeEventLoop& evloop,
                                 std::string addr, std::string port,
                                 OrderService* order_service)
  : GxSessionBase<GxClientSession>(ioloop, evloop, {}),
    _remote_addr(addr),
    _remote_port(port),
    _order_service(order_service),
    m_connected_subject(
        [this](std::function<void(const bool&)> fn, const bool& value) {
          this->_event_loop.dispatch([fn, value]() { fn(value); });
        })
{
}

void GxClientSession::start_connecting()
{
  auto wp = weak_from_this();
  _event_loop.dispatch(std::chrono::milliseconds(1000), [wp]() {
    if (auto sp = wp.lock()) {
      try {
        sp->check_connection();
      } catch (std::exception& err) {
        LOG_WARN("check-connection error: " << err.what());
      }
    }
    return std::chrono::milliseconds(1000);
  });
}


bool GxClientSession::is_connected()
{
  return (_sock && _sock->is_connected());
}


void GxClientSession::check_connection()
{
  assert(_event_loop.this_thread_is_ev());

  if (_sock) {

    if (_sock->is_connected())
      return;

    if (_sock->is_closed()) {
      _sock.reset();
      m_connected_subject.next(false);
      return;
    }

    _sock->close();
  }


  if (!_pending_sock) {
    LOG_INFO("connecting to " << _remote_addr << ":" << _remote_port);
    _pending_sock = std::make_unique<apex::TcpSocket>(_io_loop);
    _pending_sock_fut = _pending_sock->connect(_remote_addr, _remote_port);
    _pending_sock_start = Time::realtime_now();
  } else {
    if (_pending_sock->is_connected()) {
      _sock = std::move(_pending_sock);
      auto wp = weak_from_this();
      _callbacks.on_err = [wp](GxClientSession&) {
        if (auto sp = wp.lock())
          sp->_sock->close();
      };

      LOG_INFO("connected to gx-server");
      this->start_read([](GxClientSession& session, apex::UvErr err) {
        if (err) {
          if (err.is_eof()) {
            LOG_INFO("connection lost to gx-server");
          } else {
            LOG_ERROR("start_read error: " << err);
          }
        }
        // this call to _sock->close is needed
        // because the _sock still reports a state of
        // is_connected()==true, even though a socket error
        // has occurred.
        session._sock->close();
      });

      for (auto& item : _active_subs) {
        _pending_subs.push_back(std::move(item.second));
      }
      _active_subs.clear();

      this->perform_subscriptions();

      m_connected_subject.next(true);
    } else {
      auto fut_status = _pending_sock_fut.wait_for(std::chrono::seconds(0));
      if (fut_status == std::future_status::ready) {
        auto err = _pending_sock_fut.get();
        if (err) {
          LOG_ERROR("connect error: " << err);
        _pending_sock->close().wait();
        _pending_sock.reset();
        }
      }
      auto now = Time::realtime_now();
      if (now - _pending_sock_start >= std::chrono::seconds(3)) {
        LOG_WARN("connection time out");
        _pending_sock->close().wait();
        _pending_sock.reset();
      }
    }
  }
}


void GxClientSession::subscribe_account(std::string exchange, Account& target)
{
  VariantSubscription variant{AccountSubscription{exchange, &target}};
  auto wp = weak_from_this();
  this->_event_loop.dispatch([wp, variant]() mutable {
    if (auto sp = wp.lock()) {
      sp->_pending_subs_2.push_back(variant);
      sp->perform_subscriptions();
    }
  });
}


void GxClientSession::subscribe(std::string symbol, ExchangeId exchange, apex::MarketData* mv)
{
  // TODO: protect with mutex, or perform on event thread
  //_subs.insert({symbol, });

  MarketViewSubscription sub {symbol, exchange, mv};

  auto wp = weak_from_this();
  this->_event_loop.dispatch([wp, sub]() {
    if (auto sp = wp.lock()) {
      sp->_pending_subs.push_back(sub);
      sp->perform_subscriptions();
    }
  });
}


void GxClientSession::perform_subscriptions()
{
  assert(_event_loop.this_thread_is_ev());
  if (!_sock || !_sock->is_connected())
    return;

  for (auto& item : this->_pending_subs) {
    // TODO/EASY: check if exists, return if so
    this->_ticks_subscription.insert({item.symbol, item.mv});
    this->_active_subs.insert({item.symbol, item});

    // construct wire-protocol message
    apex::pb::SubscribeTicks msg;
    msg.set_symbol(item.symbol);
    msg.set_exchange(to_exchange(item.exchange));

    // build wire format
    auto payload = msg.SerializeAsString();
    apex::gx::Header header;
    uint16_t flags = static_cast<uint16_t>(apex::gx::Flags::proto3);
    apex::gx::Header::init(&header, payload.size(), gx::Type::subscribe, flags);
    header.hton();

    // socket write/queue
    _sock->write((char*)&header, sizeof(header));
    _sock->write(payload.data(), payload.size());
  }
  _pending_subs.clear();

  /* Variants */

  // for (auto& item : _pending_subs_2) {
  //   if (std::holds_alternative<AccountSubscription>(item)) {
  //     apex::pb::SubscribeWallet msg;
  //     AccountSubscription& sub = std::get<AccountSubscription>(item);

  //     msg.set_exchange(std::get<AccountSubscription>(item).exchange);
  //     LOG_INFO("REMEMBER TO ADD THESE TO A LIST FOR REUSE ON RECONNECT");

  //     // build wire format
  //     auto payload = msg.SerializeAsString();
  //     apex::gx::Header header;
  //     uint16_t flags = static_cast<uint16_t>(apex::gx::Flags::proto3);
  //     apex::gx::Header::init(&header, payload.size(),
  //                           gx::Type::subscribe_account, flags);
  //     header.hton();

  //     // socket write/queue
  //     _sock->write((char*)&header, sizeof(header));
  //     _sock->write(payload.data(), payload.size());
  //     LOG_INFO("written " << sizeof(header) + payload.size()
  //                         << " bytes to socket");

  //     _account_subs.insert({sub.exchange, std::move(sub)});
  //   }
  // }
  _pending_subs_2.clear();
}


void GxClientSession::io_on_full_message(gx::Header* header, char* payload,
                                         size_t payload_len)
{
  /* IO-thread */
  gx::Type type = (gx::Type)header->type;
  const auto flags = header->flags;

  if (type == gx::Type::trade) {
    apex::pb::TickTrade msg;
    msg.ParseFromArray(payload, payload_len);
    _event_loop.dispatch([wp = weak_from_this(), msg]() mutable {
      if (auto sp = wp.lock()) {
        auto iter = sp->_ticks_subscription.find(msg.symbol());
        if (iter != std::end(sp->_ticks_subscription)) {
          TickTrade tick;
          tick.price = msg.price();
          tick.qty = msg.size();
          switch (msg.side()) {
            case apex::pb::Side::side_buy:
              tick.aggr_side = apex::Side::buy;
              break;
            case apex::pb::Side::side_sell:
              tick.aggr_side = apex::Side::sell;
              break;
            case apex::pb::Side::side_none:
              tick.aggr_side = apex::Side::none;
              break;
            default: {
              LOG_ERROR("dropping GxTickTrade message, invalid side");
              return;
            }
          }
          iter->second->apply(tick);
        } else {
          LOG_ERROR("received unexpected TickTrade event");
        }
      }
    });
  } else if (type == gx::Type::account_update) {
    // apex::pb::WalletUpdate msg;
    // msg.ParseFromArray(payload, payload_len);
    // _event_loop.dispatch([wp = weak_from_this(), msg]() mutable {
    //   if (auto sp = wp.lock()) {

    //     // convert from wire to model-update
    //     apex::AccountUpdate update;
    //     update.asset.symbol = msg.symbol();
    //     //update.asset.exchange = msg.exchange();
    //     update.avail = msg.position();

    //     // apply the model-update
    //     auto iter = sp->_account_subs.find(msg.exchange());
    //     if (iter != std::end(sp->_account_subs)) {
    //       iter->second.model->apply(update);
    //     } else {
    //       LOG_WARN("unexpected AccountUpdate, exchange '" << msg.exchange()
    //                                                       << "'");
    //     }
    //   }
    // });
  } else if (type == gx::Type::tick_top) {
    apex::pb::TickTop msg;
    msg.ParseFromArray(payload, payload_len);
    _event_loop.dispatch([wp = weak_from_this(), msg]() mutable {
      if (auto sp = wp.lock()) {

        auto iter = sp->_ticks_subscription.find(msg.symbol());
        if (iter != std::end(sp->_ticks_subscription)) {

          // convert from wire to model-update
          apex::TickTop tick;
          tick.bid_price = msg.bid_price();
          tick.ask_price = msg.ask_price();

          // apply the model-update
          iter->second->apply(tick);
        } else {
          LOG_WARN("received unexpected TickTop event");
        }
      }
    });
  } else if (type == gx::Type::error) {

    apex::pb::Error msg;
    gx::Type req_type = (gx::Type)msg.orig_request_type(); // TODO: improve this
    msg.ParseFromArray(payload, payload_len);

    _event_loop.dispatch(
        [wp = weak_from_this(), req_type, msg_id = header->id, msg]() mutable {
          auto orig_msg_type = (gx::Type)msg.orig_request_type();
          if (auto sp = wp.lock()) {
            switch (orig_msg_type) {
              case gx::Type::new_order:
                sp->on_submit_order_error(msg_id, msg.code(), msg.text());
                return;
              case gx::Type::cancel_order:
                sp->on_cancel_order_error(msg_id, msg.code(), msg.text());
                return;
              default:
                LOG_WARN("received GX error message for unknown request type, "
                         << " request-type: " << (char)req_type);
            }
          }
        });
  } else if (type == gx::Type::order_exec) {
    // parse response
    apex::pb::OrderExecution msg;
    msg.ParseFromArray(payload, payload_len);
    OrderUpdate update;
    update.state = static_cast<OrderState>(msg.order_state());
    update.close_reason = static_cast<OrderCloseReason>(msg.close_reason());
    update.ext_order_id = msg.ext_order_id();
    auto update_reason = msg.reason(); // reason for this order_exec

    _event_loop.dispatch([wp = weak_from_this(), msg_id = header->id,
                          order_id = msg.order_id(), update = update,
                          update_reason]() mutable {
      if (auto sp = wp.lock()) {
        switch (update_reason) {
          case pb::OrderUpdateReason::NEW_ORDER_ACK: {
            auto iter = sp->_pending_submit_order.find(msg_id);
            if (iter != std::end(sp->_pending_submit_order)) {
              if (auto order = iter->second.lock()) {
                order->apply(update);
              }
              sp->_pending_submit_order.erase(iter);
            } else {
              LOG_WARN("can't find orginal order order_exec(new-order)");
            }
            break;
          }

          case pb::OrderUpdateReason::CANCEL_ORDER_ACK: {
            auto iter = sp->_pending_cancel_order.find(msg_id);
            if (iter != std::end(sp->_pending_cancel_order)) {

              if (auto order = iter->second.lock()) {
                order->apply(update);
              }
              sp->_pending_cancel_order.erase(iter);
            } else {
              LOG_WARN("can't find original order order_exec(cancel-order)");
            }
            break;
          }

          case pb::OrderUpdateReason::UNSOLICITED: {
            sp->_order_service->route_update_to_order(order_id, update);
            break;
          }

          default: {
            LOG_WARN("unhandled GX OrderExecution message");
            break;
          }
        }
      }
    });


    // _event_loop.dispatch([wp = weak_from_this(), msg_id = header->id,
    //                       msg]() mutable {
    //   if (auto sp = wp.lock()) {

    //     OrderUpdate update;
    //     update.state = static_cast<OrderState>(msg.order_state());
    //     update.close_reason =
    //     static_cast<OrderCloseReason>(msg.close_reason());
    //     update.ext_order_id = msg.ext_order_id();

    //     auto iter = sp->_pending_submit_order.find(msg_id);
    //     if (iter != std::end(sp->_pending_submit_order)) {
    //       if (auto order = iter->second.lock()) {
    //         order->apply(update);
    //       }
    //       sp->_pending_submit_order.erase(iter);
    //     } else {
    //       LOG_INFO("cant find orginal order for send-order-reply");
    //     }
    //   }
    // });
  } else if (type == gx::Type::order_fill) {
    apex::pb::OrderFill msg;
    msg.ParseFromArray(payload, payload_len);

    _event_loop.dispatch(
        [wp = weak_from_this(), msg_id = header->id, msg]() mutable {
          if (auto sp = wp.lock()) {
            OrderFill fill;
            fill.size = msg.size();
            fill.price = msg.price();
            fill.is_fully_filled = msg.fully_filled();
            sp->_order_service->route_fill_to_order(msg.order_id(), fill);
          }
        });
  } else if (type == gx::Type::om_logon) {
    apex::pb::OmLogonReply msg;
    msg.ParseFromArray(payload, payload_len);

    _event_loop.dispatch(
        [wp = weak_from_this(), msg_id = header->id, msg]() mutable {
          if (auto sp = wp.lock()) {
            sp->m_om_logon_subject.next(msg.error());
          }
        }

    );
  }

  else {
    LOG_WARN("unable to handle GX message with len: " << payload_len
                                                      << ", flags:" << flags);
  }
}


void GxClientSession::on_submit_order_error(gx::t_msgid req_id,
                                            std::string code, std::string text)
{
  assert(_event_loop.this_thread_is_ev());

  auto iter = _pending_submit_order.find(req_id);
  if (iter == std::end(_pending_submit_order)) {
    LOG_WARN("received unexpected send-order error, reqId: " << req_id);
  } else {
    if (auto order = iter->second.lock()) {
      order->set_is_rejected(code, text);
    }
    _pending_submit_order.erase(iter);
  }
}


void GxClientSession::on_cancel_order_error(gx::t_msgid req_id,
                                            std::string code, std::string text)
{
  assert(_event_loop.this_thread_is_ev());

  auto iter = _pending_cancel_order.find(req_id);
  if (iter == std::end(_pending_cancel_order)) {
    LOG_WARN("received unexpected cancel-order error, reqId: " << req_id);
  } else {
    if (auto order = iter->second.lock()) {
      order->apply_cancel_reject(code, text);
    }
    _pending_cancel_order.erase(iter);
  }
}



void GxClientSession::cancel_order(Order& order)
{
  assert(_event_loop.this_thread_is_ev());

  // construct wire-protocol message
  apex::pb::CancelOrder msg;
  msg.set_order_id(order.order_id());
  msg.set_ext_order_id(order.exch_order_id());
  msg.set_exchange(to_exchange(order.instrument().exchange_id()));
  msg.set_symbol(order.instrument().native_symbol());

  // build wire format
  auto payload = msg.SerializeAsString();
  apex::gx::Header header;
  uint16_t flags = static_cast<uint16_t>(apex::gx::Flags::proto3);
  apex::gx::Header::init(&header, payload.size(), gx::Type::cancel_order, flags);
  const auto reqid = _next_reqid++;
  header.id = reqid;
  header.hton();

  _pending_cancel_order[reqid] = order.weak_from_this();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxClientSession::strategy_logon(std::string strategy_id, RunMode run_mode)
{
  assert(_event_loop.this_thread_is_ev());

  // construct wire-protocol message
  apex::pb::OmLogonRequest msg;
  msg.set_strategy_id(strategy_id);
  switch (run_mode) {
    case RunMode::live:
      msg.set_run_mode(pb::runmode_live);
      break;
    case RunMode::paper:
      msg.set_run_mode(pb::runmode_sim);
      break;
    case RunMode::backtest:
      throw std::runtime_error("GxClientSession cannot be used in backtest mode");
  }

  // build wire format
  auto payload = msg.SerializeAsString();
  apex::gx::Header header;
  uint16_t flags = static_cast<uint16_t>(apex::gx::Flags::proto3);
  apex::gx::Header::init(&header, payload.size(), gx::Type::om_logon, flags);
  const auto reqid = _next_reqid++;
  header.id = reqid;
  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxClientSession::new_order(Order& order)
{
  assert(_event_loop.this_thread_is_ev());

  // construct wire-protocol message
  apex::pb::NewOrder msg;
  msg.set_exchange(to_exchange(order.instrument().exchange_id()));
  msg.set_symbol(order.instrument().native_symbol());
  msg.set_side(to_side(order.side()));
  msg.set_price(order.price());
  msg.set_size(order.size());
  msg.set_tif(static_cast<uint32_t>(order.time_in_force()));
  msg.set_order_id(order.order_id());

  // build wire format
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(apex::gx::Flags::proto3);
  gx::Header::init(&header, payload.size(), gx::Type::new_order, flags);
  const auto reqid = _next_reqid++;
  header.id = reqid;
  header.hton();

  _pending_submit_order[reqid] = order.weak_from_this();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}

rx::observable<bool>& GxClientSession::connected_observable()
{
  return m_connected_subject;
}

rx::observable<std::string>& GxClientSession::om_logon_observable()
{
  return m_om_logon_subject;
}


} // namespace apex
