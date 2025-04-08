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

#include <apex/util/Config.hpp>
#include <apex/util/Time.hpp>
#include <apex/util/utils.hpp>

#include <memory>
#include <filesystem>

namespace apex
{

class Reactor;
class EventLoop;
class RealtimeEventLoop;
class BacktestEventLoop;
class OrderService;
class RefDataService;
class PersistenceService;
class GatewayService;
class MarketDataService;
class OrderRouterService;
class BacktestService;

struct BacktestPeriod {
  Time from;
  Time upto;

  BacktestPeriod() = default;
  BacktestPeriod(Time time_from, Time time_upto)
    : from(time_from),
      upto(time_upto)
  {
    if (time_from > time_upto)
      throw std::runtime_error("bad backtest-period, time-from cannot be later than time-upto");
  }
};

struct PathsConfig
{
  std::filesystem::path root;
  std::filesystem::path tickdata;
  std::filesystem::path refdata;
  std::filesystem::path fdb;
};

/* Responsible for creating and providing access to the various core
 * components and services required by all apex application components. */
class Services
{
public:
  explicit Services(RunMode run_mode,
                    BacktestPeriod backtest_period={});
  ~Services();

  static const char* build_datetime();

  Time startup_time() const { return _startup_time; }

  void init_services(Config = Config::empty_config());

  const PathsConfig& paths_config() const { return _paths_config; }

  /* Utility method used to create and init services with minimal config */
  static std::unique_ptr<Services> create(RunMode run_mode,
                                          BacktestPeriod backtest_period={});

  OrderService* order_service() { return _order_service.get(); }

  RefDataService* ref_data_service() { return _ref_data_service.get(); }

  OrderRouterService* order_router_service() { return _order_router_service.get(); }

  BacktestService* backtest_service() { return _backtest_service.get(); }

  PersistenceService* persistence_service()
  {
    return _persistence_service.get();
  }

  MarketDataService* market_data_service()
  {
    return _market_data_service.get();
  }

  GatewayService* gateway_service() { return _gateway_service.get(); }

  Reactor* reactor() { return _reactor.get(); }
  EventLoop* evloop() { return _evloop.get(); }

  Time now();

  [[nodiscard]] RunMode run_mode() const { return _run_mode; }

  [[nodiscard]] bool is_backtest() const { return _run_mode == RunMode::backtest; }

  Config& config() { return _config; }

  RealtimeEventLoop* realtime_evloop();

  // Obtain the backtest event loop, if Serivces has one, else return null.
  BacktestEventLoop* backtest_evloop();

  // Utility method, typically called by a program main thread, to run a engine
  // until either completion (for backtest mode), or until interrupted
  // (control-c) for non-backtest modes.
  void run();

private:
  RunMode _run_mode;
  Config _config;
  PathsConfig _paths_config;
  Time _startup_time;
  std::unique_ptr<Reactor> _reactor;
  std::unique_ptr<EventLoop> _evloop;
  BacktestEventLoop* _bt_evloop;
  std::unique_ptr<OrderRouterService> _order_router_service;
  std::unique_ptr<RefDataService> _ref_data_service;
  std::unique_ptr<PersistenceService> _persistence_service;
  std::unique_ptr<OrderService> _order_service;

  std::unique_ptr<GatewayService> _gateway_service;
  std::unique_ptr<MarketDataService> _market_data_service;
  std::unique_ptr<BacktestService> _backtest_service;

  BacktestPeriod _backtest_period;
};


} // namespace apex
