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

#include <apex/model/Account.hpp>
#include <apex/core/Bot.hpp>
#include <apex/util/Config.hpp>
#include <apex/comm/GxClientSession.hpp>
#include <apex/core/OrderRouter.hpp>
#include <apex/model/Order.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/core/StrategyMain.hpp>

#include <memory>
#include <set>
#include <utility>


/*
 * Trading logic for a single tradable instrument.
 */
class SkeletonBot : public apex::Bot
{

private:
  void manage_pending_orders() {}
  void manage_live_orders() {}
  void manage_order_initiation() {}

public:
  SkeletonBot(apex::Strategy* strategy,
              apex::Instrument instrument)
    : apex::Bot("SkeletonBot", strategy, std::move(instrument))
  {
  }


public:
  void on_tick_trade(apex::MarketData::EventType) override
  {
    /* handle a public trade */
    LOG_INFO(this->ticker() << ": " << this->market().last());
  }

  virtual void on_tick_book(apex::MarketData::EventType) override
  {
    /* handle order book update */
  }

  void on_order_closed(apex::Order&) override
  {
    /* handle own order completed (fully-filled, or canceled) */
  }

  void on_order_live(apex::Order&) override
  {
    /*  handle own order is live  */
  }

  void on_order_fill(apex::Order&) override
  {
    /* handle own order execution */
  }

  void on_timer() override
  {
    /* handle regular or one-off timer */

    if (!om_session_up()) {
      /* If the order management service is not connected, typically
       * we have nothing to do.  We cannot order manage orders at the
       * exchange if we cannot connect. */
      return;
    }

    if (_order_cache.has_pending_orders()) {
      manage_pending_orders();
    }

    if (_order_cache.has_live_orders()) {
      manage_live_orders();
    }

    // if we have no live/pending then try initiate
    if (!_order_cache.has_pending_orders() && !_order_cache.has_live_orders()) {
      manage_order_initiation();
    }
  }
};


/*
 * Represent a trading strategy.  An essential job of a strategy is to construct
 * the individual Bot instances for each asset that it is configured to trade
 * (the trading universe).
 */
class SkeletonStrategy : public apex::Strategy
{
public:
  SkeletonStrategy(apex::Services* services, apex::Config config)
    : apex::Strategy(services, std::move(config))
  {
  }

  apex::Bot* construct_bot(const apex::Instrument& instrument) override
  {
    return new SkeletonBot{this, instrument};
  }


  void create_bots() override
  {
    // create individual trading bots, one for each tradable instrument
    auto symbols = parse_flat_instruments_config();
    for (const std::string& symbol : symbols) {
      apex::Instrument instrument =
          _services->ref_data_service()->get_instrument(
              symbol, apex::InstrumentType::coinpair);

      auto bot = std::unique_ptr<apex::Bot>(construct_bot(instrument));
      _bots.insert({instrument, std::move(bot)});
    }
  }

private:
};


int main(int argc, char** argv)
{
  // Construct and run the strategy using a utility method `strategy_runner`.
  return strategy_runner(argc, argv, apex::StrategyFactory<SkeletonStrategy>{});
}
