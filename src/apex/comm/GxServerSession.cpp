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

#include <apex/comm/GxServerSession.hpp>
#include <apex/comm/GxClientSession.hpp>
#include <apex/model/Account.hpp>
#include <apex/model/ExchangeId.hpp>
#include <apex/core/Errors.hpp>
#include <apex/infra/UvErr.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/tick_msgs.hpp>

#include <type_traits>

#include <netinet/in.h>
namespace apex
{

class RealtimeEventLoop;
class TcpSocket;


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

static apex::ExchangeId from_exchange(apex::pb::Exchange id)
{
  switch (id) {
    case apex::pb::exchange_none :
      return ExchangeId::none ;
    case apex::pb::exchange_binance:
      return ExchangeId::binance;
    case apex::pb::exchange_binance_usdfut:
      return ExchangeId::binance_usdfut ;
    case  apex::pb::exchange_binance_coinfut:
      return ExchangeId::binance_coinfut;
    default:
      throw std::runtime_error("invalid ExchangeId during conversion to pb::Exchange");
  }
}


static apex::Side from_side(apex::pb::Side side)
{
  switch (side) {
    case apex::pb::Side::side_none:
      return apex::Side::none;
    case apex::pb::Side::side_buy:
      return apex::Side::buy;
    case apex::pb::side_sell:
      return apex::Side::sell;
    default:
      return apex::Side::none;
  }
}


GxServerSession::GxServerSession(IoLoop& ioloop, RealtimeEventLoop& evloop,
                                 std::unique_ptr<TcpSocket> sk,
                                 EventHandlers callbacks)
  : GxSessionBase<GxServerSession>(ioloop, evloop, std::move(sk)),
    _server_callbacks(callbacks)
{
  // if (!_callbacks.on_err)
  //   throw std::runtime_error("callback on_err not provided");
}

GxServerSession::~GxServerSession() {}

void GxServerSession::on_subscribe(const apex::pb::SubscribeTicks& msg)
{
  assert(_event_loop.this_thread_is_ev());
  LOG_INFO("receive subscribe request for '" << msg.symbol() << "'");
}


void GxServerSession::send(ExchangeId /*exchange*/,
                           const std::vector<AccountUpdate>& /*updates*/)
{
  // for (auto& update : updates) {
  //   apex::pb::WalletUpdate msg;
  //   msg.set_symbol(update.asset.symbol);
  //   msg.set_exchange(from_exchange(exchange));
  //   msg.set_position(update.avail);

  //   // build wire
  //   auto payload = msg.SerializeAsString();
  //   gx::Header header;
  //   uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);
  //   gx::Header::init(&header, payload.size(), gx::Type::account_update, flags);

  //   header.hton();

  //   // socket write/queue
  //   _sock->write((char*)&header, sizeof(header));
  //   _sock->write(payload.data(), payload.size());
  // }
}


apex::pb::Side from_size(apex::Side s)
{
  switch (s) {
    case apex::Side::buy:
      return apex::pb::side_buy;
    case apex::Side::sell:
      return apex::pb::side_sell;
    case apex::Side::none:
      return apex::pb::side_none;
    default:
      throw std::runtime_error(
          "cannot convert from apex::Side to apex::pb::Side");
  }
}


void GxServerSession::io_on_full_message(gx::Header* header, char* payload,
                                         size_t payload_len)
{
  /* io-thread */
  const auto flags = header->flags;
  const gx::Type type = header->type;
  const auto id = header->id;

  if (header->type == gx::Type::subscribe) {
    apex::pb::SubscribeTicks msg;
    msg.ParseFromArray(payload, payload_len);
    auto wp = weak_from_this();
    _event_loop.dispatch([wp, msg]() {
      if (auto sp = wp.lock()) {
        GxSubscribeRequest req(msg.symbol(), from_exchange(msg.exchange()));
        sp->_server_callbacks.on_subscribe(*sp, req);
      }
    });
  } else if (type == gx::Type::subscribe_account) {
    apex::pb::SubscribeWallet msg;
    msg.ParseFromArray(payload, payload_len);
    auto wp = weak_from_this();
    _event_loop.dispatch([wp, msg]() {
      if (auto sp = wp.lock()) {
        LOG_INFO(
            "received subscribe-account request, exchange:" << msg.exchange());
        sp->_server_callbacks.on_subscribe_account(*sp, from_exchange(msg.exchange()));
      }
    });
  } else if (type == gx::Type::cancel_order) {
    apex::pb::CancelOrder msg;
    msg.ParseFromArray(payload, payload_len);

    Request request;
    request.req_type = gx::Type::cancel_order;
    request.req_id = id;

    auto wp = weak_from_this();
    _event_loop.dispatch([wp, request, msg]() mutable {
      if (auto sp = wp.lock()) {
        sp->_server_callbacks.on_cancel_order_request(
          *sp, request, from_exchange(msg.exchange()), msg.symbol(), msg.order_id(),
            msg.ext_order_id());
      }
    });
  } else if (type == gx::Type::new_order) {
    apex::pb::NewOrder msg;
    msg.ParseFromArray(payload, payload_len);

    Request request;
    request.req_type = gx::Type::new_order;
    request.req_id = id;

    OrderParams params;
    params.exchange = from_exchange(msg.exchange());
    params.symbol = msg.symbol();
    params.price = msg.price();
    params.size = msg.size();
    params.side = from_side(msg.side());
    params.time_in_force = static_cast<TimeInForce>(msg.tif());
    params.order_id = msg.order_id();

    if (!_logon_accepted) {
      LOG_WARN("rejecting order-submit, apex-gx session not logged-on");
      send_error(request, error::e0200, "not logged-on to apex-gx");
      return;
    }

    auto wp = weak_from_this();
    _event_loop.dispatch([wp, request, params]() mutable {
      if (auto sp = wp.lock())
        sp->_server_callbacks.on_submit_order(*sp, request, params);
    });
  } else if (type == gx::Type::logon) {
    apex::pb::LogonRequest msg;
    msg.ParseFromArray(payload, payload_len);

    // Request request;
    // request.req_type = RequestType::new_order;
    // request.req_id = id;

    // auto wp = weak_from_this();
    // _event_loop.dispatch([wp, request, msg]() mutable {
    //   if (auto sp = wp.lock()) {
    //     GxLogonRequest logon_request;
    //     logon_request.strategy_id = msg.strategy_id();
    //     auto accepted = sp->_server_callbacks.on_logon(*sp, logon_request);
    //     if (accepted) {
    //       sp->_logon_accepted = true;
    //       sp->set_app_id(logon_request.strategy_id);
    //       sp->send_logon_reply();
    //     } else {

    //     }

    //     // TODO: send a logon-reply here
    //   }
    // });
  } else if (type == gx::Type::om_logon) {
    apex::pb::OmLogonRequest msg;
    msg.ParseFromArray(payload, payload_len);

    Request request;
    request.req_type = gx::Type::new_order;
    request.req_id = id;

    auto wp = weak_from_this();
    _event_loop.dispatch([wp, request, msg]() mutable {
      if (auto sp = wp.lock()) {
        GxLogonRequest logon_request;
        logon_request.strategy_id = msg.strategy_id();
        switch (msg.run_mode()) {
          case pb::runmode_live:
            logon_request.run_mode = RunMode::live;
            break;
          case pb::runmode_sim:
            logon_request.run_mode = RunMode::paper;
            break;
          default:
             sp->send_om_logon_reply(error::e0201);
             return;
        }

        auto accepted = sp->_server_callbacks.on_logon(*sp, logon_request);
        if (accepted) {
          sp->_logon_accepted = true;
          sp->set_app_id(logon_request.strategy_id);
          sp->send_om_logon_reply();
        } else {
          sp->send_om_logon_reply(error::e0201);
        }
      }
    });
  }

  else {
    LOG_WARN("unable to handle GX-message with len: " << payload_len
                                                      << ", flags:" << flags);
  }
}

void GxServerSession::send_logon_reply(std::string /*error*/)
{
  // TODO: send a LogonReply back to the peer
}

void GxServerSession::send_om_logon_reply(std::string error)
{
  // build network message
  apex::pb::OmLogonReply msg;
  msg.set_error(error);

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);

  gx::Header::init(&header, payload.size(), gx::Type::om_logon, flags);
  header.id = 0;
  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxServerSession::set_app_id(std::string id) { this->_app_id = id; }


void GxServerSession::send_order_fill(ExchangeId exchange_id,
                                      const std::string& order_id,
                                      const OrderFill& fill)
{
  // build network message
  apex::pb::OrderFill msg;

  msg.set_exchange(to_exchange(exchange_id));
  msg.set_order_id(order_id);
  msg.set_size(fill.size);
  msg.set_price(fill.price);
  msg.set_fully_filled(fill.is_fully_filled);

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);
  gx::Header::init(&header, payload.size(), gx::Type::order_fill, flags);
  header.id = 0;
  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxServerSession::send_order_unsol_cancel(ExchangeId /*exchange_id*/,
                                              const std::string& order_id,
                                              const OrderUpdate& update)
{
  // build network message
  apex::pb::OrderExecution msg;
  msg.set_order_id(order_id);
  msg.set_close_reason(static_cast<uint32_t>(update.close_reason));
  msg.set_order_state(static_cast<uint32_t>(update.state));
  msg.set_reason(pb::OrderUpdateReason::UNSOLICITED);

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);

  gx::Header::init(&header, payload.size(), gx::Type::order_exec, flags);
  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


/* Used to convey new-order-ack and cancel-order-ack. */
void GxServerSession::send(Request orig_req, OrderUpdate& update)
{
  // build network message
  apex::pb::OrderExecution msg;
  msg.set_close_reason(static_cast<uint32_t>(update.close_reason));
  msg.set_order_state(static_cast<uint32_t>(update.state));
  msg.set_ext_order_id(update.ext_order_id);

  switch (orig_req.req_type) {
    case gx::Type::new_order:
      msg.set_reason(pb::OrderUpdateReason::NEW_ORDER_ACK);
      break;
    case gx::Type::cancel_order:
      msg.set_reason(pb::OrderUpdateReason::CANCEL_ORDER_ACK);
      break;
    default:
      msg.set_reason(pb::OrderUpdateReason::UNSOLICITED);
  }

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);

  gx::Header::init(&header, payload.size(), gx::Type::order_exec, flags);
  header.id = orig_req.req_id;
  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxServerSession::send_error(Request req, std::string code,
                                 std::string error)
{
  // build network message
  apex::pb::Error msg;
  msg.set_orig_request_type(apex::to_underlying(req.req_type));
  msg.set_code(code);
  msg.set_text(error);

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);


  gx::Header::init(&header, payload.size(), gx::Type::error, flags);
  header.id = req.req_id;
  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxServerSession::send(const std::string & symbol,
                           ExchangeId exchange_id,
                           TickTrade& tick)
{
  // build network message
  apex::pb::TickTrade msg;
  msg.set_symbol(symbol);
  msg.set_exchange(to_exchange(exchange_id));
  msg.set_price(tick.price);
  msg.set_size(tick.qty);
  msg.set_side(from_size(tick.aggr_side));

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);
  gx::Header::init(&header, payload.size(), gx::Type::trade, flags);

  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}


void GxServerSession::send(const std::string & symbol,
                           ExchangeId exchange_id,
                           TickTop& tick)
{
  // build network message
  apex::pb::TickTop msg;
  msg.set_symbol(symbol);
  msg.set_exchange(to_exchange(exchange_id));
  msg.set_ask_price(tick.ask_price);
  msg.set_bid_price(tick.bid_price);

  // build wire
  auto payload = msg.SerializeAsString();
  gx::Header header;
  uint16_t flags = static_cast<uint16_t>(gx::Flags::proto3);
  gx::Header::init(&header, payload.size(), gx::Type::tick_top, flags);

  header.hton();

  // socket write/queue
  _sock->write((char*)&header, sizeof(header));
  _sock->write(payload.data(), payload.size());
}

}; // namespace apex
