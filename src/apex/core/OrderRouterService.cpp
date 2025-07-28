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

#include <apex/backtest/SimExchange.hpp>
#include <apex/core/BacktestService.hpp>
#include <apex/core/GatewayService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/Error.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/venues/binance/BinanceUsdFutLineHandler.hpp>
#include <apex/venues/binance/BinanceLineHandler.hpp>

#include <memory>
#include <cassert>

namespace apex
{

OrderRouterService::OrderRouterService(Services* services) :
  _services(services)
{
}

OrderRouterService::~OrderRouterService() {
}

/* Get an OrderRouter object for sending orders to the provided exchange, and
 * this is configured with the provided strategy_id. */
OrderRouter* OrderRouterService::get_order_router(Instrument& instrument,
                                                  const std::string& strategy_id)
{

  if (_services->run_mode() == RunMode::paper || _services->run_mode() == RunMode::backtest) {
    // create/obtain an exchange simulator
    auto iter = _sim_exchanges.find(instrument.exchange_id());
    if (iter == std::end(_sim_exchanges)) {
      bool inserted = false;
      std::tie(iter, inserted) = _sim_exchanges.insert({instrument.exchange_id(),
                                                        std::make_unique<SimExchange>(_services)});
      if (inserted) {
        LOG_INFO("created exchange-simulator for " << QUOTE(instrument.exchange_name()));
      }
    }

    // add our instrument to the exchange simulator
    iter->second->add_instrument(instrument);
    return iter->second.get();
  }
  else {
    auto exchange = instrument.exchange_id();

    if ( auto iter = _routers.find(exchange);iter != std::end(_routers))
      return iter->second.get();


    // TODO: don't want to allow creating links on the fly - prefer all infra
    // comes from config.  However, we might want to perform some login.

    LOG_NOTICE("creating OrderRouter for exchange " << QUOTE(exchange));

    auto gx_session = _services->gateway_service()->find_session(exchange);
    if (!gx_session) {
      THROW("cannot find GxSession for exchange " << QUOTE(exchange));
    }
    auto router = std::make_shared<RealtimeOrderRouter>(_services,
                                                        std::move(gx_session),
                                                        strategy_id);

    auto inserted = _routers.insert({exchange, std::move(router)});

    assert(inserted.first->second.get() != nullptr);
    return inserted.first->second.get();
  }
}


void OrderRouterService::add_route() {
}


void OrderRouterService::add_binance_usdfut(OrderRouterConfig config)
{
  if (_routers.find(ExchangeId::binance_usdfut) != _routers.end())
    throw std::runtime_error("OrderRouter for BinanceUsdFut already exists");


  LineHandlerCallbacks callbacks;

  callbacks.on_submit_order_ack = [this](const MxSubmitOrderAck& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_submit_order_ack(
        ExchangeId::binance_usdfut, msg);
    });
  };

  callbacks.on_submit_order_rej = [this](const MxSubmitOrderRej& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_submit_order_rej(msg);
    });
  };

  callbacks.on_cancel_order_ack = [this](const MxCancelOrderAck& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_cancel_order_ack(msg);
    });
  };

  callbacks.on_cancel_order_rej = [this](const MxCancelOrderRej& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_cancel_order_rej(msg);
   });
  };

  callbacks.on_order_expired = [this](const MxOrderExpired& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_order_expired(
        ExchangeId::binance_usdfut, msg);
   });
  };

  callbacks.on_order_execution = [this](const MxOrderExecution& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_order_execution(
        ExchangeId::binance_usdfut, msg);
   });
  };

  auto lh = std::make_shared<BinanceUsdFutLineHandler>(
    _services,
    _services->reactor(),
    _services->realtime_evloop(),
    callbacks,
    config
    );
  lh->start();

  auto router = lh->get_order_router_adapter();

  _routers[ExchangeId::binance_usdfut] = std::move(router);
}


void OrderRouterService::add_binance_spot(OrderRouterConfig config)
{
  if (_routers.find(ExchangeId::binance) != _routers.end())
    throw std::runtime_error("OrderRouter for Binance(Spot) already exists");

  LineHandlerCallbacks callbacks;

  callbacks.on_submit_order_ack = [this](const MxSubmitOrderAck& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_submit_order_ack(
        ExchangeId::binance, msg);
    });
  };

  callbacks.on_submit_order_rej = [this](const MxSubmitOrderRej& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_submit_order_rej(msg);
    });
  };

  callbacks.on_cancel_order_ack = [this](const MxCancelOrderAck& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_cancel_order_ack(msg);
    });
  };

  callbacks.on_cancel_order_rej = [this](const MxCancelOrderRej& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_cancel_order_rej(msg);
   });
  };

  callbacks.on_order_expired = [this](const MxOrderExpired& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_order_expired(
        ExchangeId::binance, msg);
   });
  };

  callbacks.on_order_execution = [this](const MxOrderExecution& msg) {
    this->_services->evloop()->dispatch([this, msg]() {
      this->_services->order_service()->process_order_execution(
        ExchangeId::binance, msg);
   });
  };

  auto lh = std::make_shared<BinanceLineHandler>(
    _services,
    _services->reactor(),
    _services->realtime_evloop(),
    callbacks,
    config
    );
  lh->start();

  auto router = lh->get_order_router_adapter();

  _routers[ExchangeId::binance] = std::move(router);
}


}
