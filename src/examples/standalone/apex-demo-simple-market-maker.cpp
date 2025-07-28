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
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/Strategy.hpp>

#include <iostream>

class SimpleMarketMakerBot : public apex::Bot
{

  struct Config {
    double order_usd = 50;  // value of each order, in USD

    // When placing an order we choose a price that is based on a distance from
    // the best limit price.
    double entry_price_distance_bps = 50;

    double price_threshold_bps = 5;
  };
  const Config params;

public:

  SimpleMarketMakerBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("SMM", strategy, instrument) {}

  void on_timer() override
  {
    // Every 1 second run the Bot logic, which is just an order if we already
    // have not, but if we have sent one, then cancel it.

    if (_make_buy)
    {
      if (_order_buy)
        _manage_existing_order(_order_buy, _price_bounds_buy);
      else
        _create_and_send_order(apex::Side::buy, _order_buy, _price_bounds_buy);
    }

    if (_make_sell)
    {
      if (_order_sell)
        _manage_existing_order(_order_sell, _price_bounds_sell);
      else
        _create_and_send_order(apex::Side::sell, _order_sell, _price_bounds_sell);
    }
  }


private:

  struct PriceBounds {
    double lower;
    double upper;
  };

  void _create_and_send_order(apex::Side side,
                              std::shared_ptr<apex::Order>& order,
                              PriceBounds & px_bounds)
  {
    if (!market_data_ok() || !has_fx_rate()) {
      LOG_WARN(ticker() << ": waiting for market data");
      return;
    }

    if (is_stopping())
      return;

    int iside = (side == apex::Side::buy)? 1: -1;

    double price = round_price_passive(
      last_price() * (1.0 - iside*params.entry_price_distance_bps/10000),
      side);

    // don't send order if calculated order is zero
    if (apex::dbl_is_zero(price)) {
      LOG_WARN("cannot send, calculated order price is " << price);
      return;
    }

    // determine the market price lower & upper bounds that will prompt a
    // replace of the order
    px_bounds.lower = last_price() * (1.0 - params.price_threshold_bps/10000);
    px_bounds.upper = last_price() * (1.0 + params.price_threshold_bps/10000);

    // size the order quantity, based on target price and value
    double qty = round_size(params.order_usd / (price * fx_rate()));

    // don't send order if calculated order qty is zero
    if (apex::dbl_is_zero(qty) == 0) {
      LOG_INFO("order qty is zero, not sending for side " << side);
      return;
    }

    // construct an order object (this does not cause it to be sent)
    order = create_order(side, qty, price, apex::TimeInForce::gtc);

    // send order to the exchange (this is an asynchronous operation)
    order->send();
  }

  void _manage_existing_order(std::shared_ptr<apex::Order>& order,
                              PriceBounds& px_bounds)
  {
    if (!order->is_closed_or_canceling()) {

      if ((last_price() > px_bounds.upper) || last_price() < px_bounds.lower) {
        LOG_INFO("last price out of range, cancelling order");
        order->cancel();
      }

      // The order is still 'live', so here we will manage it.  Our only
      // management logic is to cancel the order if it's been alive for too
      // long.

      if (order->duration_live() > std::chrono::seconds(20)) {
        order->cancel();
      }
    }
    else {
      if (order->is_closed()) {
        order.reset();
      }
    }
  }

  bool _make_buy = true;
  bool _make_sell = true;

  std::shared_ptr<apex::Order> _order_buy;
  std::shared_ptr<apex::Order> _order_sell;

  PriceBounds _price_bounds_buy;
  PriceBounds _price_bounds_sell;
};



int main()
{
  try {
    // Configure thie demo can run in either backtest, paper, or live trading.
    // Hoever to run in back-test mode will require captured market data.
    auto run_mode = apex::RunMode::paper;

    std::unique_ptr<apex::Services> services;

    // ------------------------------------------------------------
    // FRAMEWORK SETUP
    // ------------------------------------------------------------

    if (run_mode == apex::RunMode::backtest) {
      // backtest time range
      apex::Time from{"2024-02-01T00:00:00"};
      apex::Time upto{"2024-02-31"};

      // create core engine, configured for backtest
      services = apex::Services::create(run_mode, {from, upto});
    } else {
      // create core engine, configured for realtime (prod / paper)
      services = apex::Services::create(run_mode);

      // add feed handlers
      apex::FeedConfig feed_config;
      feed_config.type = "BinanceUsdFut";
      services->market_data_service()->add_feed(feed_config,
                                                {"binance_usdfut"});

      // add line handlers
      apex::OrderRouterConfig binance_config;
      binance_config.api_key_file = apex::expand("~/.secrets/binance_key.json");
      services->order_router_service()->add_binance_usdfut(binance_config);
    }

    // ------------------------------------------------------------
    //  STRATEGY
    // ------------------------------------------------------------

    // create a Strategy object, which is a container for individual bots
    apex::Strategy strategy(services.get(), "SMM02");

    // add a bot, which is responsible for trading a single name
    strategy.create_bot<SimpleMarketMakerBot>(apex::InstrumentQuery(
                                                "DOGEUSDT",
                                                apex::ExchangeId::binance_usdfut));

    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // ----- System start -----

    // run until user presses control-c, or backtest time range completed
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
