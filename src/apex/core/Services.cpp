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

#include <apex/core/GatewayService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/core/PersistenceService.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/Services.hpp>
#include <apex/infra/IoLoop.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/util/BacktestEventLoop.hpp>
#include <apex/core/BacktestService.hpp>


namespace apex
{

std::unique_ptr<EventLoop> construct_event_loop(RunMode run_mode,
                                                Time backtest_time_start) {
  if (run_mode == RunMode::backtest) {
    return std::make_unique<BacktestEventLoop>(backtest_time_start);
  }
  else {
    return std::make_unique<RealtimeEventLoop>(
              [](){
                try {
                  throw;
                }
                catch (const std::exception& e) {
                  LOG_ERROR("caught exception at event loop: ("
                            << demangle(typeid(e).name()) << ") " << e.what());
                }
                catch (...) {
                  LOG_ERROR("caught unknown exception at event loop");
                }
                return false; // dont terminate the eventloop
              },
              [] { apex::Logger::instance().register_thread_id("ev"); });
  }
}

static PathsConfig default_paths_config() {
  // the APEX_HOME environment variable can be used to customise where Apex
  // finds all the files it needs (such as tickdata, refdata, positions etc)
  PathsConfig config;
  const char *  home_var = "APEX_HOME";
  const char * custom_base = std::getenv(home_var);
  std::string default_root = "apex";

  auto root = (custom_base)? custom_base : apex::user_home_dir() / default_root;

  config.root = root;
  config.refdata = root / "data"/ "refdata";
  config.tickdata = root / "data" / "tickdata";
  config.fdb = root / "persist";

  return config;
}

static Time calc_startup_time(RunMode run_mode,
                              BacktestPeriod& backtest_period) {

  if (run_mode == RunMode::live || run_mode == RunMode::paper)
    return Time::realtime_now();
  else
    return backtest_period.from;
}


Services::Services(RunMode run_mode,
                   BacktestPeriod backtest_period)
  : _run_mode(run_mode),
    _paths_config{default_paths_config()},
    _startup_time(calc_startup_time(run_mode, backtest_period)),
    _ioloop(std::make_unique<IoLoop>()),
    _evloop(construct_event_loop(run_mode, backtest_period.from)),
    _bt_evloop(dynamic_cast<BacktestEventLoop*>(_evloop.get())),
    _backtest_period(backtest_period)
{
}


Services::~Services()
{
  /* assumed called on main thread */
  _ioloop->sync_stop();
  _evloop->sync_stop();
}


const char* Services::build_datetime()  {
  return __DATE__ " - " __TIME__;
}


std::unique_ptr<Services> Services::create(apex::RunMode run_mode,
                                           BacktestPeriod backtest_period) {
  apex::Logger::instance().register_thread_id("main");
  auto services = std::make_unique<Services>(run_mode, backtest_period);
  services->init_services();
  return services;
}

void Services::init_services(Config config)
{
  Logger::instance().log_banner(_run_mode);

  // initialise logging; do this very early on, so that for backtest mode
  // the logging timestamps always refect the backtest time
  if (is_backtest()) {
    auto clock_source = [this](){
      return this->now();
    };

    Logger::instance().set_clock_source(clock_source);
  }

  _config = config;

  LOG_NOTICE("apex run-mode: " << _run_mode);

  // service construction order is done in terms of those with the
  // least dependencies to those with most dependencies.


  if (_run_mode == RunMode::backtest) {
    _backtest_service = std::make_unique<BacktestService>(
        this,
        _backtest_period.from,
        _backtest_period.upto);
  }

  _order_router_service = std::make_unique<OrderRouterService>(this);

  _ref_data_service =
      std::make_unique<RefDataService>(this, config.get_sub_config("ref_data", Config::empty_config()));

  _persistence_service = std::make_unique<PersistenceService>(this);

  _order_service = std::make_unique<OrderService>(this);

  if (_run_mode != RunMode::backtest) {
    _gateway_service =
      std::make_unique<GatewayService>(this, config.get_sub_config("gateways", Config::empty_config()));
  }

  _market_data_service = std::make_unique<MarketDataService>(this);
}


Time Services::now() {
  if (_run_mode == RunMode::live || _run_mode == RunMode::paper)
    return Time::realtime_now();
  else
    return _bt_evloop ->get_time();
}


RealtimeEventLoop* Services::realtime_evloop() {
  if (_run_mode == RunMode::backtest)
    throw std::runtime_error("RealtimeEventLoop not created in backtest mode");
  else
   return dynamic_cast<RealtimeEventLoop*>(_evloop.get());
}

BacktestEventLoop* Services::backtest_evloop() {
  if (_run_mode != RunMode::backtest)
    throw std::runtime_error("BacktestEventLoop only available in RunMode::backtest");
  else
    return _bt_evloop;
}

void Services::run() {
  if (_run_mode == RunMode::backtest) {
    backtest_evloop()->set_time(_backtest_period.from);
    backtest_evloop()->run_loop(_backtest_period.upto);
  }
  else {
    apex::wait_for_sigint();
  }
}

} // namespace apex
