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
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/comm/GxClientSession.hpp>
#include <apex/core/OrderRouter.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/core/StrategyMain.hpp>
#include <apex/demo/DemoMakerBot.hpp>


#include <memory>
#include <set>
#include <utility>


class TestStrategy : public apex::Strategy
{
public:
  TestStrategy(apex::Services* services, apex::Config config)
    : apex::Strategy(services, std::move(config))
  {
  }


  apex::Bot* construct_bot(const apex::Instrument& instrument) override
  {
    return new apex::DemoMakerBot{this, instrument};
  }

  void create_bots() override
  {
    auto symbols = parse_flat_instruments_config();
    for (const std::string& symbol : symbols) {
      apex::Instrument instrument =
          _services->ref_data_service()->get_instrument(
              symbol, apex::InstrumentType::coinpair);

      auto bot = std::unique_ptr<apex::Bot>(construct_bot(instrument));
      _bots.insert({instrument, std::move(bot)});
    }
  }
};


int main(int argc, char** argv)
{
  // Construct and run the strategy using a utility method `strategy_runner`.
  return strategy_runner(argc, argv, apex::StrategyFactory<TestStrategy>{});
}
