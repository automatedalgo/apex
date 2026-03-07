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
#include <apex/core/Logger.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/Services.hpp>
#include <apex/net/Reactor.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/Error.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/util/json.hpp>
#include <apex/venues/binance/BinanceUsdFutFeedHandler.hpp>

#include <chrono>
#include <fstream>
#include <functional>
#include <list>
#include <set>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>
#include <format>
#include <filesystem>

namespace apex {

struct LoggerParams {
  std::string level = "info";
  bool detail = false;
  bool async = true;

  static auto schema() {
    FIELD_DEF_INIT( LoggerParams );
    FIELD_DEF_OPTIONAL( level, "info" );
    FIELD_DEF_OPTIONAL( detail, false );
    FIELD_DEF_OPTIONAL( async, true );
    FIELD_DEF_RETURN();
  }

  Logger::level to_level() const {
    return Logger::string_to_level(level);
  }
};

struct ExchangeParams {
  std::string id;
  static auto schema() {
    FIELD_DEF_INIT( ExchangeParams );
    FIELD_DEF_REQUIRED( id );
    FIELD_DEF_RETURN();
  }

  ExchangeId exchange_id() const {
    return to_exchange_id(id);
  }
};

struct TickCollectorParams {
  LoggerParams logger;
  std::vector <std::string> symbols;
  std::string location;
  ExchangeParams exchange;
  static auto schema() {
    FIELD_DEF_INIT( TickCollectorParams );
    FIELD_DEF_OPTIONAL( logger, LoggerParams{} );
    FIELD_DEF_OPTIONAL( location, "" );
    FIELD_DEF_REQUIRED( symbols );
    FIELD_DEF_REQUIRED( exchange );
    FIELD_DEF_RETURN();
  }
};

/* This is a MarketData-like object that can receive Tick messaages, but instead
 * of maintaining a book, will pass the tick event to a tick collector for
 * saving to file.
 */
struct MarketDataCapture {

  MarketDataCapture(apex::Instrument inst)
    : _inst(inst)
  {
  }
  void set_collector_l1(std::function<void(Time, apex::TickTop&)> sink) {
    _level1_sink = sink;
  }
  void set_collector_trade(std::function<void(Time, apex::TickTrade&)> sink) {
    _trade_sink = sink;
  }

  void apply(Time t, apex::TickTop& tick) {
    _level1_sink(t, tick);
  };
  void apply(Time t, apex::TickTrade& tick) {
    _trade_sink(t, tick);
  };

  apex::Instrument _inst;

  // tick sinks
  std::function<void(Time, apex::TickTop&)> _level1_sink;
  std::function<void(Time, apex::TickTrade&)> _trade_sink;
};

// Purpose of EmbeddedFeedHandler is proxy a direct FeedHandler instance so that
// an Apex instance can connect direct direct to an exchange.
// EmbeddedFeedHandler will perform the mapping from feed symbols (pxsym) to
// MarketData instances.

class EmbeddedFeedHandler {
public:

  using FHBuilder = std::function<std::shared_ptr<FeedHandler>(FeedHandlerCallbacks)>;

  EmbeddedFeedHandler(ExchangeId eid,
                      FHBuilder feed_builder)
    : _eid(eid)
  {
    FeedHandlerCallbacks callbacks;
    callbacks.on_trade = [this](std::string pxsym, TickTrade& tick, TimeLog&) {
      this->on_tick<TickTrade>(pxsym, tick);
    };
    callbacks.on_top = [this](std::string pxsym, TickTop& tick, TimeLog&){
      this->on_tick<TickTop>(pxsym, tick);
    };
    _fh = feed_builder(std::move(callbacks));
  }


  void subscribe_trades(std::string pxsym, MarketDataCapture* md) {
    // TODO: should check if already exists, is the same

    pxsym = str_tolower(pxsym);
    {
      std::lock_guard<std::mutex> guard(_mds_mtx);
      _mds.insert({pxsym, md});
    }
    _fh->subscribe_trades(pxsym);
  }

  void subscribe_top(std::string pxsym, MarketDataCapture* md) {
    // TODO: should check if already exists, is the same
    pxsym = str_tolower(pxsym);
    {
      std::lock_guard<std::mutex> guard(_mds_mtx);
      _mds.insert({pxsym, md});
    }
    _fh->subscribe_top(pxsym);
  }

  std::shared_ptr<FeedHandler>& feed() { return _fh; }

private:

  template<typename M>
  void on_tick(std::string pxsym, M& tick) {
    MarketDataCapture* md = nullptr;
    {
      std::lock_guard<std::mutex> guard(_mds_mtx);
      if (auto iter = _mds.find(pxsym); iter != _mds.end())
        md = iter->second;
    }

    if (md) {
      md->apply(Time::realtime_now(), tick);
    }
    else {
      LOG_WARN("subscription not found for '" << pxsym << "'");
    }
  }

private:
  ExchangeId _eid;
  std::shared_ptr<FeedHandler> _fh;
  std::mutex _mds_mtx;
  std::map<std::string, MarketDataCapture*> _mds;
};


/* Capture ticks related to a single instrument and single stream/channel. As a
   base class this largely defines an interface that derived classes have to
   implement. */
class BaseCollector
{
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
    std::lock_guard<std::mutex> guard(_mtx);
    return apex::Time::realtime_now().as_epoch_ms() - _last_data.as_epoch_ms() ;
  }

protected:
  apex::Time _last_data; // time last data arrived
  std::atomic<size_t> _count = 0; // count of ticks collected
  mutable std::mutex _mtx;

private:
  std::string _descr; // description, for logging purpose
  apex::StreamInfo _info; // instrument & stream
};


/* Base class for tick-collectors that use a single data type for capturing
   ticks. */
template<typename T>
class BaseCollectorImpl : public BaseCollector
{
public:

  BaseCollectorImpl(std::string descr, apex::StreamInfo info)
    : BaseCollector(descr, info) {}

  [[nodiscard]] size_t tick_count() const override {
    std::lock_guard<std::mutex> guard(_mtx);
    return this->_ticks.size();
  }

  [[nodiscard]] apex::TickFileBucketId earliest_tick_bucket_id() const override {
    std::lock_guard<std::mutex> guard(_mtx);

    if (this->_ticks.size() == 0)
      return apex::TickFileBucketId{};

    auto bucketid = apex::TickFileBucketId::from_time(_ticks.front().recv_time);
    if (_ticks.front().recv_time == Time()) {
      LOG_WARN("front tick has empty time");
    }
    return bucketid;
  }


  size_t write_ticks_impl(TickbinFileWriter& file,
                          std::function<size_t(T&)> write_fn) {
    std::lock_guard<std::mutex> guard(_mtx);

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


// struct CapturedVariantTick   WORK IN PROGRESS -- NOT YET THREAD SAFE
// {
//   apex::Time recv_time;
//   std::variant<apex::TickTop, apex::TickTrade> tick;
// };

// // VariantCollector is able to collect streams of heterogeneous tick types
// class VariantCollector : public BaseCollectorImpl<CapturedVariantTick> {
// public:
//   VariantCollector(std::string descr, apex::StreamInfo info)
//     : BaseCollectorImpl<CapturedVariantTick>(std::move(descr), std::move(info)) {
//   }


//   template<typename T>
//   void add_tick(apex::Time captured, const T& tick) {
//     LOG_INFO("adding tick");
//     _count++;
//     this->_last_data = captured;
//     _ticks.push_back({captured, tick});

//   }


//   template<typename T>
//   size_t write(apex::TickbinFileWriter& file, CapturedVariantTick& item) {
//     if (std::holds_alternative<T>(item.tick)) {
//       auto bytes = _serialiser.serialise(item.recv_time, std::get<T>(item.tick));
//       file.write_bytes(&bytes[0], bytes.size());
//       return bytes.size();
//     }
//     else
//       return 0;
//   }

//   size_t write_to_file(apex::TickbinFileWriter& file) override {
//     auto write_fn= [&](CapturedVariantTick& item) ->size_t{
//       size_t bytes;
//       if ((bytes = this->write<apex::TickTop>(file, item)))
//         return bytes;
//       if ((bytes = this->write<apex::TickTrade>(file, item)))
//         return bytes;
//       throw std::runtime_error("cannot serialise collected tick, unsupported tick variant");
//     };
//     return this->write_ticks_impl(file, write_fn);
//   }

// private:
//   apex::tickbin::Serialiser _serialiser;
// };


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

  void add_tick(apex::Time captured_time, T& tick) {
    CapturedSingleTick<T> captured_tick{captured_time, tick};

    if (captured_time == Time()) {
      LOG_ERROR("tick has empty time!");
    }

    {
      std::lock_guard<std::mutex> guard(this->_mtx);
      this->_count++;
      this->_last_data = captured_time;
      this->_ticks.push_back(captured_tick);
    }
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
                       TickCollectorParams params)
    : _services{services},
      _location(params.location),
      _event_loop{_services->realtime_evloop()},
      _reactor{_services->reactor()},
      _params(params)
  {
  }

  void start();
  void check_collector_queues();

  void add_collector(std::string symbol, apex::ExchangeId, std::string stream);

  [[nodiscard]] const std::string& location() const { return _location; }

private:
  std::pair<std::filesystem::path, std::filesystem::path>
  build_tickbin_filename(apex::TickFileBucketId bucketid,
                         BaseCollector& collector);
  void create_feed_handlers();
  void setup_collectors();

  apex::Services* _services;
  std::string _location;
  apex::RealtimeEventLoop* _event_loop;
  apex::Reactor* _reactor;
  TickCollectorParams _params;

  std::map<apex::ExchangeId, std::shared_ptr<apex::EmbeddedFeedHandler>> _feeds;

  // container of tick collections pending creation
  std::set<apex::StreamInfo> _streams_to_add;
  std::vector<std::shared_ptr<BaseCollector>> _collectors;
  std::map<Instrument, std::shared_ptr<MarketDataCapture>> _mds;
};


void TickCollectorService::add_collector(std::string symbol,
                                         apex::ExchangeId exchange_id,
                                         std::string stream)
{

  apex::Instrument inst = _services->ref_data_service()->get_instrument({std::move(symbol), exchange_id});
  apex::StreamInfo info{inst, std::move(stream)};

  if (_streams_to_add.find(info) == std::end(_streams_to_add))
    _streams_to_add.insert(info);
  else
    throw std::runtime_error("cannot add duplicate collector");
}


std::pair<std::filesystem::path, std::filesystem::path>
TickCollectorService::build_tickbin_filename(
  apex::TickFileBucketId bucketid,
  BaseCollector& collector)
{
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
  /* event thread */

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

    if (collector->tick_count() == 0)
      continue;

    auto this_write_count = collector->tick_count();

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
    LOG_INFO("stream: " << collector->descr()
             << ", bucketid: " << bucketid.as_string()
             << ", file: " << file.full_path()
             << ", wrote bytes: " << byte_count
             << ", wrote ticks: " << this_write_count
             << ", total ticks: " << collector->total_tick_count());
  }
}


/* For the various instruments this service is configured to collect, create the
 * exchange sessions that will provide the underlying market data access.
 */
void TickCollectorService::create_feed_handlers()
{
  for (auto & item : _streams_to_add)  {
    auto exch_id = item.exchange_id();

    auto iter = _feeds.find(exch_id);
    if (iter == std::end(_feeds)) {

      switch (exch_id) {
        case apex::ExchangeId::binance_usdfut: {
          EmbeddedFeedHandler:: FHBuilder fh_builder;

          fh_builder = [services=_services]
            (FeedHandlerCallbacks callbacks) -> std::shared_ptr<FeedHandler> {
            auto feed = std::make_shared<BinanceUsdFutFeedHandler>(
              services,
              services->run_mode(),
              services->reactor(),
              services->realtime_evloop(),
              callbacks
              );
            return feed;
          };
          LOG_INFO("creating feed handler for " << exch_id);
          auto fh = std::make_shared<EmbeddedFeedHandler>(exch_id, fh_builder);
          _feeds[exch_id] = fh;
          fh->feed()->start();
          break;
        }
        default:
          THROW("cannot setup tick collector for unsupported exchange '"
                << item.exchange_id() << "'");
      }
    }
  }
}


void TickCollectorService::setup_collectors()
{
  for (auto & info : _streams_to_add)
  {
    Instrument inst = info.instrument;
    ExchangeId exch_id = info.exchange_id();

    // get or create a MarketDataCapture
    std::shared_ptr<MarketDataCapture> md = _mds[inst];
    if (!md) {
      md = std::make_shared<MarketDataCapture>(inst);
      _mds[inst] = md;
    }

    // get the feed (should have been created earlier)
    auto iter = _feeds.find(exch_id);
    if (iter == std::end(_feeds)) {
      THROW("no feed handler for '"<< exch_id << "'");
    }

    // Create a tick-target - this is the object that will receive the ticks,
    // and then pass them to a collector
    std::shared_ptr<apex::EmbeddedFeedHandler> sp = iter->second;

    std::string pxsym;
    pxsym = apex::str_tolower(info.instrument.symbol());

    if (info.channel == "l1") {
      auto collector = std::make_shared<SingleTypeCollector<apex::TickTop>>(
        apex::concat(exch_id, ".", info.symbol(), ".l1"), info);

      _collectors.push_back(collector);

      md->set_collector_l1([collector](Time t, TickTop& tick) {
        collector->add_tick(t, tick);
      });

      sp->subscribe_top(pxsym, md.get());
    }
    else if (info.channel == "aggtrades") {
      auto collector = std::make_shared<SingleTypeCollector<apex::TickTrade>>(
        apex::concat(exch_id, ".", info.symbol(),".aggtrades"),
        info);
      _collectors.push_back(collector);

      md->set_collector_trade([collector](Time t, TickTrade& tick) {
        collector->add_tick(t, tick);
      });

      sp->subscribe_trades(pxsym, md.get());
    }
    else
      THROW("cannot setup tick collector for unknown stream type '"
            << info.channel << "'");

    usleep(1000 * 1000);
    LOG_INFO("created tick-collector for " << info.exchange_id() << "/"
                                           << info.channel << "/"<< info.symbol());
  }
}


void TickCollectorService::start()
{
  // create the exchange-session components required by the tick collectors
  create_feed_handlers();

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

} // namespace apex


apex::TickCollectorParams load_config(const std::string& filename)
{
  auto raw_data =  apex::read_file(filename);

  try {
    // parse to JSON object
    json raw_config = json::parse(raw_data,
                                  /* callback */ nullptr,
                                  /* allow exceptions */ true,
                                  /* ignore_comments */ true);

    // parse to parameters object
    apex::ConfigParser<apex::TickCollectorParams> parser;
    parser.parse(raw_config);
    return parser.result;
  }
  catch (json::parse_error& e)
  {
    throw apex::ConfigError(apex::concat("error parsing JSON config file '",
                                         filename,
                                         "' json parse error: ",
                                         e.what()));
  }
}

int main(int argc, char** argv)
{
  std::string filename;
  try {
    for (int i = 1; i < argc; i++) {
      if (std::string_view(argv[i]) == "--config") {
        i++;
        if (i < argc)
          filename = argv[i];
        else
          throw std::runtime_error("missing argument to --config");
      }
    }
    if (filename.empty())
      throw std::runtime_error("provide config file, using --config option");
  }
  catch (std::exception & e) {
    std::cout << e.what() << std::endl;
    return 1;
  }

  apex::TickCollectorParams params;
  try {
    params = load_config(filename);
  }
  catch (std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }

  try
  {
    // setup logging
    apex::Logger::instance().set_opts({
        .filename = "auto",
        .time = apex::LogOpts::Time::second,
        .mode = apex::LogOpts::Mode::trunc,
        .level = params.logger.to_level(),
        .detail = params.logger.detail,
        .async = params.logger.async
      });
    apex::Logger::instance().register_thread_id("main");

    // Create core-services configured for paper trading, which provides
    // real-time a real time event loop and market-data but no access to
    // production trading.
    auto services = apex::Services::create(apex::RunMode::paper);

    // capture location of the collection;
    apex::TickCollectorService tick_collector_svc(services.get(), params);

    auto exch_id = params.exchange.exchange_id();
    for (const auto & symbol : params.symbols) {
      tick_collector_svc.add_collector(symbol, exch_id, "l1");
      tick_collector_svc.add_collector(symbol, exch_id, "aggtrades");
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
