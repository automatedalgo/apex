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

#include <apex/backtest/TickFileWriter.hpp>
#include <apex/backtest/TickbinMsgs.hpp>
#include <apex/gx/BinanceSession.hpp>
#include <apex/gx/GxServer.hpp>
#include <apex/infra/Reactor.hpp>
#include <apex/infra/SocketAddress.hpp>
#include <apex/infra/ssl.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/json.hpp>
#include <apex/util/Error.hpp>

#include <unistd.h>
#include <utility>
#include <vector>
#include <variant>
#include <list>
#include <fstream>
#include <chrono>
#include <functional>

namespace apex {

/* Capture ticks related to a single instrument and single stream/channel. As a
 base class this largely defines an interface that derived classes have to
 implement. */
class BaseCollector {
public:

  static constexpr std::chrono::seconds stale_interval{60};

  // used to indicate when a stream becomes stale, which means that new ticks
  // have not arrived for a specified interval, `stale_interval`
  bool is_stale;

  BaseCollector(std::string descr,
                StreamInfo info) :
    is_stale(false),
    _descr(std::move(descr)),
    _info(std::move(info))
  {}


  virtual ~BaseCollector() = default;

  [[nodiscard]] virtual size_t tick_count() const = 0;

  [[nodiscard]] virtual size_t total_tick_count() const { return _count; }

  [[nodiscard]] virtual apex::TickFileBucketId earliest_tick_bucket_id() const = 0;

  virtual size_t write_to_file(apex::TickbinFileWriter&) = 0;

  [[nodiscard]] const std::string & descr() const { return _descr; }
  [[nodiscard]] const apex::StreamInfo& info() const { return _info; }

  // Time elapse since the most recent tick arrived
  [[nodiscard]] std::chrono::milliseconds duration_since_update() const {
    return apex::Time::realtime_now().as_epoch_ms() - _last_data.as_epoch_ms() ;
  }

protected:
  apex::Time _last_data; // time last data arrived
  size_t _count = 0; // count of ticks collected

private:
  std::string _descr; // description, for logging purpose
  apex::StreamInfo _info; // instrument & stream
};


/* Base class for tick-collectors that use a single data type for capturing
   ticks. */
template<typename T>
class BaseCollectorImpl : public BaseCollector {
public:

  BaseCollectorImpl(std::string descr, apex::StreamInfo info)
    : BaseCollector(descr, info) {}

  [[nodiscard]] size_t tick_count() const override { return this->_ticks.size(); }

  [[nodiscard]] apex::TickFileBucketId earliest_tick_bucket_id() const override {
    if (tick_count() == 0)
      return apex::TickFileBucketId{};

    auto bucketid = apex::TickFileBucketId::from_time(_ticks.front().recv_time);
    return bucketid;
  }


  size_t write_ticks_impl(TickbinFileWriter& file,
                          std::function<size_t(T&)> write_fn) {
    size_t byte_count = 0;
    auto iter = this->_ticks.begin();
    while (iter != _ticks.end() &&
           (TickFileBucketId::from_time(iter->recv_time) == file.bucketid())) {
      byte_count += write_fn(*iter);
      iter++;
    }
    this->_ticks.erase(this->_ticks.begin(), iter);
    return byte_count;
  }

protected:
  std::list<T> _ticks;
};


struct CapturedVariantTick {
  apex::Time recv_time;
  std::variant<apex::TickTop, apex::TickTrade> tick;
};

// VariantCollector is able to collect streams of heterogeneous tick types
class VariantCollector : public BaseCollectorImpl<CapturedVariantTick> {
public:
  VariantCollector(std::string descr, apex::StreamInfo info)
    : BaseCollectorImpl<CapturedVariantTick>(std::move(descr), std::move(info)) {
  }


  template<typename T>
  void add_tick(apex::Time captured, const T& tick) {
    _count++;
    this->_last_data = apex::Time::realtime_now();
    _ticks.push_back({captured, tick});
  }


  template<typename T>
  size_t write(apex::TickbinFileWriter& file, CapturedVariantTick& item) {
    if (std::holds_alternative<T>(item.tick)) {
      auto bytes = _serialiser.serialise(item.recv_time, std::get<T>(item.tick));
      file.write_bytes(&bytes[0], bytes.size());
      return bytes.size();
    }
    else
      return 0;
  }

  size_t write_to_file(apex::TickbinFileWriter& file) override {
    auto write_fn= [&](CapturedVariantTick& item) ->size_t{
      size_t bytes;
      if ((bytes = this->write<apex::TickTop>(file, item)))
        return bytes;
      if ((bytes = this->write<apex::TickTrade>(file, item)))
        return bytes;
      throw std::runtime_error("cannot serialise collected tick, unsupported tick variant");
    };
    return this->write_ticks_impl(file, write_fn);
  }

private:
  apex::tickbin::Serialiser _serialiser;
};


template <typename T>
struct CapturedSingleTick {
  apex::Time recv_time;
  T tick;
};

// SingleTypeCollector is able to collect streams of a single tick type (which
// is provided via the template argument T).
template <typename T>
class SingleTypeCollector : public BaseCollectorImpl<CapturedSingleTick<T> > {
public:

  SingleTypeCollector(std::string descr, apex::StreamInfo info)
    : BaseCollectorImpl<CapturedSingleTick<T> > (std::move(descr), info) {}

  void add_tick(apex::Time captured, T& tick) {
    this->_count++;
    this->_last_data = apex::Time::realtime_now();
    this->_ticks.push_back({captured, tick});
  }

  size_t write_to_file(apex::TickbinFileWriter& file) override {

    auto write_fn = [&]( CapturedSingleTick<T> & item) -> size_t {
      auto bytes = apex::tickbin::Serialiser::serialise(item.recv_time,
                                                        item.tick);
      file.write_bytes(&bytes[0], bytes.size());
      return bytes.size();
    };

    return this->write_ticks_impl(file, write_fn);
  }
};

/* Apex tick collection service */

class TickCollectorService {
public:

  // The location parameter is user text that can describe the geographic
  // location of where the tick collector is running.  This is important because
  // tick can arrive at different times based on how far away the tick
  // collector is from the exchange.
  TickCollectorService(apex::Services* services,
                       std::string location)
    : _services{services},
      _location(std::move(location)),
      _event_loop{_services->realtime_evloop()},
      _reactor{_services->reactor()}
  {
    apex::SslConfig sslconf(true);
    _ssl = std::make_unique<apex::SslContext>(sslconf);
  }

  void start();
  void check_collector_queues();

  // add a new collector for a specified instrument
  void add_collector(std::string symbol, apex::ExchangeId exchange_id, std::string stream) {
    apex::Instrument inst = _services->ref_data_service()->get_instrument({std::move(symbol), exchange_id});
    apex::StreamInfo info{inst, std::move(stream)};
    this->add_stream_collector(info);
  }

  // add a stream-collector; this configures new object to capture a market-data
  // stream (e.g. L1, Trades etc.) for a specific Instrument
  void add_stream_collector(const apex::StreamInfo& info) {
    if (_streams_to_add.find(info) != std::end(_streams_to_add))
      throw std::runtime_error("cannot add duplicate collector");
    _streams_to_add.insert(info);
  }

  [[nodiscard]] const std::string& location() const { return _location; }

private:
  std::pair<std::filesystem::path, std::filesystem::path>
  build_tickbin_filename(apex::TickFileBucketId bucketid,
                         BaseCollector& collector);
  void create_exchange_sessions();
  void setup_collectors();
  void setup_collector_l1(apex::BaseExchangeSession*, const apex::StreamInfo&);
  void setup_collector_aggtrades(apex::BaseExchangeSession*, const apex::StreamInfo&);

  apex::Services* _services;
  std::string _location;
  apex::RealtimeEventLoop* _event_loop;
  apex::Reactor* _reactor;
  std::unique_ptr<apex::SslContext> _ssl;

  std::map<apex::ExchangeId, std::shared_ptr<apex::BaseExchangeSession>> _exchange_sessions;

  // container of tick collections pending creation
  std::set<apex::StreamInfo> _streams_to_add;
  std::vector<std::shared_ptr<BaseCollector>> _collectors;
};


std::pair<std::filesystem::path, std::filesystem::path>
TickCollectorService::build_tickbin_filename(
  apex::TickFileBucketId bucketid,
  BaseCollector& collector) {

  auto directory = _services->paths_config().tickdata / "bin1";

  char year[8] = {0};
  char month[8] = {0};
  char day[8] = {0};

  snprintf(year, sizeof(year), "%04d", bucketid.year);
  snprintf(month, sizeof(month), "%02d", bucketid.month);
  snprintf(day, sizeof(day), "%02d", bucketid.day);

  auto dir = directory / exchange_id_to_string(collector.info().exchange_id())  /
    collector.info().channel / year / month / day;

  std::filesystem::path fn = collector.info().symbol();
  fn += ".bin";
  return {dir, fn};
}


void TickCollectorService::check_collector_queues()
{
  /* For all collector objects, check the contents of their tick queue, and
     decide if a write to disk is required. */
  for (auto& collector : _collectors) {
    auto duration_since_update = collector->duration_since_update();
    if (duration_since_update > BaseCollector::stale_interval && !collector->is_stale) {
      LOG_WARN("no update on stream " << collector->descr());
      collector->is_stale = true;
    }
    else {
      collector->is_stale = false;
    }

    if (!collector->tick_count())
      continue;

    auto bucketid = collector->earliest_tick_bucket_id();

    auto tickbin = build_tickbin_filename(bucketid, *collector);

    json meta;
    meta["loc"] = _location;

    apex::TickbinFileWriter file(bucketid,
                                 tickbin.first,
                                 tickbin.second,
                                 collector->info(),
                                 meta);

    auto byte_count = collector->write_to_file(file);
    LOG_INFO("stream: " << collector->descr() << ", file: " << file.full_path()
             << ", wrote bytes: " << byte_count
             << ", total ticks: " << collector->total_tick_count());
  }
}


void TickCollectorService::setup_collector_aggtrades(apex::BaseExchangeSession* sp,
                                                     const apex::StreamInfo& info) {
  std::ostringstream oss;
  oss << info.symbol() << "." << "aggtrades";

  auto collector = std::make_shared<SingleTypeCollector<apex::TickTrade>>(oss.str(), info);

  auto callback = [collector](apex::TickTrade tick) {
    collector->add_tick(apex::Time::realtime_now(), tick);
  };

  // make subscriptions
  apex::Symbol symbol;
  symbol.native = info.symbol();
  apex::subscription_options options(apex::StreamType::Trades);
  sp->subscribe_trades(symbol, options, callback);

  _collectors.push_back(std::move(collector));
}


void TickCollectorService::setup_collector_l1(apex::BaseExchangeSession* sp,
                                              const apex::StreamInfo& info) {
  std::ostringstream oss;
  oss << info.symbol() << "." << "l1";
  auto collector = std::make_shared<SingleTypeCollector<apex::TickTop>>(
    oss.str(), info);

  auto callback = [collector](apex::TickTop tick) {
    collector->add_tick(apex::Time::realtime_now(), tick);
  };

  // make the subscriptions required to receive L1 market data model
  apex::Symbol symbol;
  symbol.native = info.symbol();
  apex::subscription_options options;
  sp->subscribe_top(symbol, options, callback);

  _collectors.push_back(std::move(collector));
}


/* For the various instruments this service is configured to collect, create the
 * exchange sessions that will provide the underlying market data access.
 */
void TickCollectorService::create_exchange_sessions() {
  for (auto & item : _streams_to_add)  {
    auto iter = _exchange_sessions.find(item.exchange_id());
    if (iter == std::end(_exchange_sessions)) {
      if (item.exchange_id() == apex::ExchangeId::binance) {
        apex::BaseExchangeSession::EventCallbacks callbacks;
        apex::BinanceSession::Params params;
        auto sp = std::make_shared<apex::BinanceSession>(
          callbacks, params, apex::RunMode::paper, _reactor, *_event_loop, _ssl.get());
        _exchange_sessions.insert({apex::ExchangeId::binance, sp});
        sp->start();
      }
      else
        THROW("cannot setup tick collector for unknown exchange '"
              << item.channel << "'");
    }
  }
}


void TickCollectorService::setup_collectors()
{
  for (auto & item : _streams_to_add)  {
    auto iter = _exchange_sessions.find(item.exchange_id());
    if (iter == std::end(_exchange_sessions)) {
      THROW("no exchange session for '"<<item.exchange_id()<< "'");
    }
    auto sp = iter->second;

    if (item.channel == "l1")
      setup_collector_l1(sp.get(), item);
    else if (item.channel == "aggtrades")
      setup_collector_aggtrades(sp.get(), item);
    else
      THROW("cannot setup tick collector for unknown stream type '"
            << item.channel << "'");

    usleep(1000 * 1000);

    LOG_INFO("created tick-collector for " << item.exchange_id() << "/"
                                           << item.channel << "/"<< item.symbol());
  }
}


void TickCollectorService::start()
{
  // create the exchange-session components required by the tick collectors
  create_exchange_sessions();

  // create the tick-collectors, which will immediately start collecting
  setup_collectors();

  // create a period callback that will check the state of tick collectors,
  // possibly leading to disk-writes
  auto save_internal = std::chrono::seconds(60);
  _event_loop->dispatch(
    save_internal,
    [this, save_internal]() -> std::chrono::milliseconds {
      try {
        this->check_collector_queues();
      }
      catch (std::exception& e) {
        LOG_ERROR("check_collector_queues() caught exception: " << e.what());
      }
      catch (...) {
        LOG_ERROR("check_collector_queues() caught unknown exception");
      }
      return save_internal;
    });
}
} // namespace


int main(int , char** )
{
  try {
    // setup logging
    // apex::Logger::instance().set_level(apex::Logger::debug);
    apex::Logger::instance().set_detail(true);
    apex::Logger::instance().register_thread_id("main");

    // Create core-services configured for paper trading, which provides
    // real-time a real time event loop and market-data but no access to
    // production trading.
    auto services = apex::Services::create(apex::RunMode::paper);

    // capture location of the collection;
    auto location = "london";
    apex::TickCollectorService tick_collector_svc(services.get(), location);

    // list of binance symbols to subscribe to
    auto symbols = {"AAVEUSDT",
        "ADAUSDT",
        "ALGOUSDT",
        "ATOMUSDT",
        "BTCUSDT",
        "DOGEUSDT",
        "DOTUSDT",
        "ETHUSDT",
        "FILUSDT",
        "LTCUSDT",
        "MATICUSDT",
        "SHIBUSDT",
        "SOLUSDT",
        "XRPUSDT"
        };

    for (const auto & symbol : symbols) {
      tick_collector_svc.add_collector(symbol, apex::ExchangeId::binance, "l1");
      tick_collector_svc.add_collector(symbol, apex::ExchangeId::binance, "aggtrades");
    }

    // start the collector service after the various streams have been
    // configured.
    tick_collector_svc.start();

    apex::wait_for_sigint();
    LOG_INFO("shutting down");

    // shutting down
    std::promise<void> queue_flush_promise;
    services->evloop()->dispatch([&](){
      try {
        tick_collector_svc.check_collector_queues();
      }
      catch (...) {
      }
      queue_flush_promise.set_value();
    });
    queue_flush_promise.get_future().wait();
    return 0;
  } catch (apex::ConfigError& e) {
    LOG_ERROR("config-error: " << e.what());
  } catch (std::exception& e) {
    LOG_ERROR("error: " << e.what());
  }

  return 1;
}
