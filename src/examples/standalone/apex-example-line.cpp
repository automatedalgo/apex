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

#include <apex/core/Bot.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/core/Logger.hpp>
#include <apex/venues/venues_common.hpp>
#include <apex/venues/binance/BinanceUsdFutLineHandler.hpp>
#include <apex/venues/binance/BinanceLineHandler.hpp>

#include <unistd.h>
#include <iostream>

using namespace apex;

/*
  Basic example of using the order gateway line handler classes directly. This
  is useful during development of line handlers.
*/

int main()
{
  try {

    // create core engine, configured for realtime mode
    Logger::instance().set_level(Logger::info);
    auto services = Services::create(RunMode::paper);

    // ----------------------------------------------------------------------
    // SETUP CALLBACKS
    // ----------------------------------------------------------------------

    LineHandlerCallbacks lh_callbacks;

    // used to capture received messages
    MxSubmitOrderAck new_order_ack;
    MxSubmitOrderRej new_order_rej;
    MxCancelOrderAck cancel_order_ack;

    lh_callbacks.on_submit_order_ack = [&](const MxSubmitOrderAck& msg) {
      LOG_INFO("on_submit_order_ack"
               <<", exch_order_id: " << msg.exch_order_id
               <<", order_id: " << msg.order_id
        );
      new_order_ack = msg;
    };

    lh_callbacks.on_submit_order_rej = [&](const MxSubmitOrderRej& msg) {
      LOG_INFO("order rejected, order_id: " << msg.order_id
               << ", code: " << msg.exch_error_code
               << ", text: " << msg.exch_error_text);
      new_order_rej = msg;
    };

    lh_callbacks.on_cancel_order_ack = [&](const MxCancelOrderAck& msg) {
      LOG_INFO("order cancel success");
      cancel_order_ack = msg;
    };

    lh_callbacks.on_cancel_order_rej = [&](const MxCancelOrderRej& msg) {
      LOG_INFO("order cancel reject, "
               << msg.exch_error_code << ": " << msg.exch_error_text);
    };

    lh_callbacks.on_order_expired = [&](const MxOrderExpired& msg) {
      LOG_INFO("order expired, exch_order_id: " << msg.exch_order_id);
    };

    lh_callbacks.on_order_execution = [&](const MxOrderExecution& msg) {
      LOG_INFO("order execution"
               << ", exch_order_id: " << msg.exch_order_id
               << ", symbol: " << msg.symbol
               << ", side:" << msg.side
               << ", qty: " << msg.qty
               << ", price:" << msg.price
               << ", aggr: " << msg.match_type);
    };


    // ----------------------------------------------------------------------
    // FEED HANDLER INSTANCE
    // ----------------------------------------------------------------------

    apex::OrderRouterConfig binance_config;
    binance_config.api_key_file = apex::expand("~/.secrets/binance_key.json");
    auto line_session = std::make_shared<BinanceLineHandler>(
      services.get(),
      services->reactor(),
      services->realtime_evloop(),
      lh_callbacks,
      binance_config
      );

    line_session->start();

    while (!line_session->is_open()) {
      sleep(1);
    }

    // ----------------------------------------------------------------------
    // SEND & CANCEL ORDERS
    // ----------------------------------------------------------------------

    sleep(2);
    OrderParams order;
    order.price = 0.250;
    order.size = 600;
    order.side = Side::buy;
    order.time_in_force = TimeInForce::gtc;
    order.symbol = "DOGEUSDT";
    order.order_id = "APX" + std::to_string(Time::realtime_now().as_epoch_ms().count());
    line_session->submit_order(order);


    sleep(2);

    MxCancelOrder cancel_req;

    // request order cancel - should succeed
    if (!new_order_ack.exch_order_id.empty()) {
      cancel_req.symbol = order.symbol;
      cancel_req.exchange = ExchangeId::binance_usdfut;
      cancel_req.order_id = order.order_id;
      cancel_req.exch_order_id = new_order_ack.exch_order_id;
      line_session->cancel_order(cancel_req);
    }

    if (1) {
      sleep(5);
      // request order cancel - should succeed
      if (!new_order_ack.exch_order_id.empty()) {
        cancel_req.symbol = order.symbol;
        cancel_req.exchange = ExchangeId::binance_usdfut;
        cancel_req.order_id = order.order_id;
        cancel_req.exch_order_id = new_order_ack.exch_order_id;
        line_session->cancel_order(cancel_req);
      }
    }

    // run until user presses control-c
    services->run();
  }
  catch (std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    return 1;
  }
}
