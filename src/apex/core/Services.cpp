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

#include <apex/core/BacktestService.hpp>
#include <apex/core/GatewayService.hpp>
#include <apex/core/TimeLogService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/core/PersistenceService.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/version.hpp>
#include <apex/infra/Reactor.hpp>
#include <apex/infra/ssl.hpp>
#include <apex/util/BacktestEventLoop.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/RealtimeEventLoop.hpp>


namespace apex
{

int MessageCaptureService::register_stream(std::string s) {
  std::mutex _mutex;
  _stream_ids.push_back({std::move(s), 0});
  return std::size(_stream_ids)-1;
}

std::pair<int,int> MessageCaptureService::register_stream_id_pair(std::string s) {
  std::mutex _mutex;
  _stream_ids.push_back({s, MessageCaptureService::Direction::inbound});
  _stream_ids.push_back({s, MessageCaptureService::Direction::outbound});
  StreamIds ids {
    (int)std::size(_stream_ids)-2,
    (int)std::size(_stream_ids)-1
  };
  return std::pair<int,int>(ids.in, ids.out);
}


MessageCaptureService::MessageCaptureService()
{
  // TODO: take from config, or, just a difference sensive default
  std::string filename = "/var/tmp/apex-wirelog.txt";
  _file.open(filename, std::ios::app);
  LOG_INFO("opening file '" << filename << "'");
  if (!_file)
    throw std::runtime_error("failed to open file for appending: " + filename);

  _stream_ids.push_back({"unknown", 0}); // meaning of stream_id 0

  _file << "===== "
        << "["<< Time::realtime_now().as_iso8601(Time::Resolution::micro, true) << "] "
        << "[FILEOPEN]" << std::endl << std::endl;
  _loop = std::make_unique<RealtimeEventLoop>(
    [](){
      return false;
    },
    [] {
      apex::Logger::instance().register_thread_id("wirelog");
    });

  // start
  auto interval = std::chrono::seconds(1);
  _loop->dispatch(interval,
                  [this, interval](){
                    this->write_to_file(true);
                    return interval;
                  });
}


void MessageCaptureService::push_event(int stream_id, std::string_view sv)
{
  assert(stream_id >= 0);  // assume -1 is initial value

  Msg msg {
    stream_id,
    Time::realtime_now(),
    std::string(sv),
    sv.size(),
  };

  {
    std::lock_guard<std::mutex> guard(_mutex);
    _queue.push(msg);
  }
  _loop->dispatch([this](){ this->write_to_file(false); });
}


void MessageCaptureService::write_to_file(bool do_flush)
{
  // event thread
  while (true)
  {
    std::lock_guard<std::mutex> guard(_mutex);
    if (_queue.empty())
      return;

    auto & front  = _queue.front();

    auto direction = "";
    if (_stream_ids[front.stream_id].second == Direction::inbound)
      direction = "[IN] ";
    if (_stream_ids[front.stream_id].second == Direction::outbound)
      direction = "[OUT] ";

    _file << "===== "
          << "["<< Time::realtime_now().as_iso8601(Time::Resolution::micro, true) << "] "
          << "[" << _stream_ids[front.stream_id].first << "] "
          << direction
          << "[" << front.rawlen << "] "
          << std::endl;
    _file << front.data;
    _file << std::endl<< std::endl;
    if (do_flush)
      _file.flush();
    _queue.pop();
  }
}


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
  PathsConfig config;
  config.root = apex_home();
  config.refdata = apex_home() / "data"/ "refdata";
  config.tickdata = apex_home() / "data" / "tickdata";
  config.fdb = apex_home() / "persist";
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
    _reactor(std::make_unique<Reactor>()),
    _evloop(construct_event_loop(run_mode, backtest_period.from)),
    _bt_evloop(dynamic_cast<BacktestEventLoop*>(_evloop.get())),
    _backtest_period(backtest_period)
{

  apex::SslConfig sslconf(true);
  _ssl = std::make_unique<apex::SslContext>(sslconf);

}


Services::~Services()
{
  /* assumed called on main thread */
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
  LOG_NOTICE("apex version: " << APEX_VERSION);

  // service construction order is done in terms of those with the
  // least dependencies to those with most dependencies.

  if (_run_mode != RunMode::backtest) {
    _time_log_service = std::make_unique<TimeLogService>(this);
    _message_capture_service = std::make_unique<MessageCaptureService>();
  }

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


SslContext* Services::ssl() {
  return _ssl.get();
}

} // namespace apex
