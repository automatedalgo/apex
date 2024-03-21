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
#include <apex/core/GatewayService.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/gx/GxServer.hpp>
#include <apex/util/BacktestEventLoop.hpp>
#include <apex/backtest/TickbinMsgs.hpp>
#include <apex/util/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

/* This uses the Bot class to just print the tick-data, so that we can quickly
   view the ticks contained without our tick-data files.
*/
class TickBot : public apex::Bot
{
public:

  std::chrono::seconds _order_lifetime = std::chrono::seconds{60};
  TickBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("", strategy, instrument) {}


  void dump_market_data() {
    std::ostringstream oss;
    oss << ticker()
        << " bid: " << market().bid()
        << ", ask: " << market().ask()
        << ", last: " << market().last();
    LOG_INFO(oss.str());
  }

  void on_timer() override
  {
    dump_market_data();
  }

  void on_tick_trade(apex::MarketData::EventType) override {
    LOG_INFO(ticker() << ": trade " << last_price());
  }
};


int main()
{
  try {
    // backtest time range
    apex::Time from{"2024-02-01T00:00:00"};
    apex::Time upto{"2024-02-03"};

    // create core engine, configured for backtest
    auto services = apex::Services::create(apex::RunMode::backtest, {from, upto});

    // ----- Strategy -----

    // create a Strategy object, which is a container for individual Bots
    apex::Strategy strategy(services, "ticks");

    // add a bot to the strategy, responsible for trading a single name
    strategy.create_bot<TickBot>(
      apex::InstrumentQuery("BTCUSDT", apex::ExchangeId::binance));
    strategy.create_bot<TickBot>(
      apex::InstrumentQuery("ETHUSDT", apex::ExchangeId::binance));


    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // run until backtest range completed
    services->run();
  } catch (std::exception& e) {
    std::cout << "error: " << e.what() << std::endl;
    return 1;
  }
}
