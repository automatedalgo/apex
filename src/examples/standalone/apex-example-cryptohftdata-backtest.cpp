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
#include <apex/core/Core.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/model/ExchangeId.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{

std::string getenv_or(const char* name, const char* fallback)
{
  const char* value = std::getenv(name);
  return (value && value[0] != '\0') ? value : fallback;
}

class CryptoHftDataMarketMakerBot : public apex::Bot
{
public:
  CryptoHftDataMarketMakerBot(apex::Strategy* strategy,
                              const apex::Instrument& instrument)
    : apex::Bot("CHDMM", strategy, instrument)
  {
  }

  void on_timer() override
  {
    manage_side(apex::Side::buy, _bid_order, _bid_bounds);
    manage_side(apex::Side::sell, _ask_order, _ask_bounds);
  }

private:
  struct PriceBounds {
    double lower = 0.0;
    double upper = 0.0;
  };

  void manage_side(apex::Side side, std::shared_ptr<apex::Order>& order,
                   PriceBounds& bounds)
  {
    if (order) {
      manage_existing_order(order, bounds);
      return;
    }

    create_and_send_order(side, order, bounds);
  }

  void create_and_send_order(apex::Side side,
                             std::shared_ptr<apex::Order>& order,
                             PriceBounds& bounds)
  {
    if (!market_data_ok() || !market().has_bid_ask() || !has_fx_rate())
      return;

    if (is_stopping())
      return;

    const double mid = market().mid();
    const double offset = _quote_offset_bps / 10000.0;
    const double raw_price = (side == apex::Side::buy) ? mid * (1.0 - offset)
                                                       : mid * (1.0 + offset);
    const double price = round_price_passive(raw_price, side);
    if (price <= 0.0)
      return;

    const double qty = round_size(_order_usd / (price * fx_rate()));
    if (qty <= 0.0) {
      LOG_WARN(ticker() << ": calculated zero quantity, not sending order");
      return;
    }

    const double replace_threshold = _replace_threshold_bps / 10000.0;
    bounds.lower = mid * (1.0 - replace_threshold);
    bounds.upper = mid * (1.0 + replace_threshold);

    order = create_order(side, qty, price, apex::TimeInForce::gtc);
    order->send();
  }

  void manage_existing_order(std::shared_ptr<apex::Order>& order,
                             const PriceBounds& bounds)
  {
    if (order->is_closed()) {
      order.reset();
      return;
    }

    if (order->is_closed_or_canceling())
      return;

    if (!market().has_bid_ask())
      return;

    const double mid = market().mid();
    if (mid < bounds.lower || mid > bounds.upper) {
      order->cancel();
      return;
    }

    if (order->duration_live() > _max_order_lifetime)
      order->cancel();
  }

  const double _order_usd = 250.0;
  const double _quote_offset_bps = 10.0;
  const double _replace_threshold_bps = 5.0;
  const std::chrono::seconds _max_order_lifetime{30};

  std::shared_ptr<apex::Order> _bid_order;
  std::shared_ptr<apex::Order> _ask_order;
  PriceBounds _bid_bounds;
  PriceBounds _ask_bounds;
};

} // namespace

int main()
{
  try {
    const auto from_str = getenv_or("APEX_CHD_FROM", "2025-08-01T00:00:00");
    const auto upto_str = getenv_or("APEX_CHD_UPTO", "2025-08-01T01:00:00");
    const auto venue_str = getenv_or("APEX_CHD_VENUE", "binance_usdfut");
    const auto symbol = getenv_or("APEX_CHD_SYMBOL", "BTCUSDT");

    apex::Time from{from_str};
    apex::Time upto{upto_str};
    auto exchange = apex::to_exchange_id(venue_str);

    auto core = apex::Core::create(apex::RunMode::backtest, {from, upto});

    apex::Strategy strategy(core, "CHDMM");
    strategy.create_bot<CryptoHftDataMarketMakerBot>(
      apex::InstrumentQuery(symbol, exchange));
    strategy.init_bots();

    core->run();
  }
  catch (const std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    std::cout << "unknown exception" << std::endl;
    return 1;
  }

  return 0;
}
