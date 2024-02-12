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

#include <functional>
#include <future>
#include <memory>
#include <string>

namespace apex
{
class Services;
class Strategy;
class Config;


class StrategyFactoryBase
{
public:
  virtual std::unique_ptr<apex::Strategy> create(
      apex::Config& config, apex::Services* services) const = 0;
};

template <typename T> class StrategyFactory : public StrategyFactoryBase
{

public:
  std::unique_ptr<apex::Strategy> create(apex::Config& config,
                                        apex::Services* services) const override
  {
    return std::make_unique<T>(services, config);
  }
};


int strategy_runner(int argc, char** argv, const StrategyFactoryBase& factory);


/*
Create a strategy instance, using the provided factory object, then initialise
and start that strategy until interrupted.
 */
class StrategyMain
{
public:
  std::string config_file;
  const StrategyFactoryBase& factory;

  StrategyMain(const StrategyFactoryBase& factory, std::string config);

  void start(std::future<int>& interrupt_code);

public:
  std::unique_ptr<apex::Services> _services;
};

} // namespace apex
