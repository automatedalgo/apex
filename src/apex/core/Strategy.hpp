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

#pragma once

#include <apex/core/Core.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/model/StrategyId.hpp>
#include <apex/util/Config.hpp>

#include <map>
#include <set>
#include <utility>

namespace apex
{

class Bot;
class Instrument;
class Auditor;

class Strategy
{
public:
  Strategy(apex::Core*, Config);

  Strategy(apex::Core*, std::string strategy_id);

  Strategy(std::unique_ptr<apex::Core>&, std::string strategy_id);

  virtual Bot* construct_bot(const Instrument&)
  {
    throw std::runtime_error("not implemented");
  };

  virtual void create_bots(){};
  virtual void init_bots();

  void stop();

  void add_bot(std::unique_ptr<Bot> bot);

  template<typename T>
  void create_bot(const Instrument& instrument) {
    auto bot = std::make_unique<T>(this, instrument);
    this->add_bot(std::move(bot));
  }

  template<typename T>
  void create_bot(const InstrumentQuery& query) {
    auto & instrument = _core->ref_data_service()->get_instrument(query);
    auto bot = std::make_unique<T>(this, instrument);
    this->add_bot(std::move(bot));
  }

  ~Strategy();

  const std::string& strategy_id() { return _strategy_id; }

  Core* core() { return _core; }

  Auditor* auditor() { return _auditor.get(); }

protected:
  std::set<std::string> parse_flat_instruments_config();
  void stop_bots();

  Core* _core;
  Config _config;
  std::string _strategy_id;
  std::map<apex::Instrument, std::unique_ptr<Bot>> _bots;

  std::unique_ptr<Auditor> _auditor;
};

} // namespace apex
