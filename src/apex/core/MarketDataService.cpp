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
#include <apex/core/Core.hpp>
#include <apex/core/GatewayService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/TimeLogService.hpp>
#include <apex/gx/GxClientSession.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/util/TimeLog.hpp>
#include <apex/venues/binance/BinanceFeedHandler.hpp>
#include <apex/venues/binance/BinanceUsdFutFeedHandler.hpp>


namespace apex
{


// Purpose of EmbeddedFeedHandler is proxy a direct FeedHandler instance so that
// an Apex instance can connect direct direct to an exchange.
// EmbeddedFeedHandler will perform the mapping from feed symbols (pxsym) to
// MarketData instances.

class EmbeddedFeedHandler {
public:

  using FHBuilder = std::function<std::shared_ptr<FeedHandler>(FeedHandlerCallbacks)>;

  EmbeddedFeedHandler(ExchangeId eid,
                      FHBuilder feed_builder,
                      TimeLogService* tlog_svc)
    : _eid(eid),
      _tlog_svc(tlog_svc)
  {
    FeedHandlerCallbacks callbacks;
    callbacks.on_trade = [this](const std::string &  pxsym, TickTrade& tick, TimeLog& tl) {
      this->on_tick<TickTrade>(pxsym, tick, tl);
    };
    callbacks.on_top = [this](const std::string & pxsym, TickTop& tick, TimeLog& tl){
      this->on_tick<TickTop>(pxsym, tick, tl);
    };

    _fh = feed_builder(std::move(callbacks));
  }


  void subscribe_trades(std::string pxsym, MarketData* md) {
    // TODO: should check if already exists, is the same

    pxsym = str_tolower(pxsym);
    {
      std::lock_guard<std::mutex> guard(_mds_mtx);
      _mds.insert({pxsym, md});
    }
    _fh->subscribe_trades(pxsym);
  }

  void subscribe_top(std::string pxsym, MarketData* md) {
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

  template<typename T>
  void on_tick(const std::string& feed_sym, T& tick, TimeLog& tl) {
    MarketData* md = nullptr;
    {
      std::lock_guard<std::mutex> guard(_mds_mtx);

      if (auto iter = _mds.find(feed_sym); iter != _mds.end())
        md = iter->second;
    }
    if (md)
      md->apply(Time::realtime_now(), tick, tl);
    else {
      LOG_WARN("subscription not found for '" << feed_sym << "'");
    }
    if (_tlog_svc)
      _tlog_svc->store(tl);
  }

private:
  ExchangeId _eid;
  std::shared_ptr<FeedHandler> _fh;
  std::mutex _mds_mtx;
  std::map<std::string, MarketData*> _mds;
  TimeLogService* const _tlog_svc;
};



MarketDataService::MarketDataService(Core* core) :
  _core(core)
{
}


MarketDataService::~MarketDataService() {}


MarketData* MarketDataService::find_market_data(const Instrument& instrument)
{
  auto iter = _markets.find(instrument);
  if (iter != std::end(_markets))
    return iter->second.get();

  // TODO: when creating the market-data object, need to decide on the stream
  // configuration.
  MdStreamParams stream_params;
  stream_params.mask |= static_cast<int>(MdStream::AggTrades);
  stream_params.mask |= static_cast<int>(MdStream::L1);

  auto mkt = std::make_unique<MarketData>();
  MarketData* md = mkt.get();

  if (_core->is_backtest()) {
    auto backtest_svc = _core->backtest_service();
    backtest_svc->subscribe_canned_data(instrument, mkt.get(), stream_params);
  }
  else {
    auto session = _core->gateway_service()->find_session(instrument.exchange_id());
    if (session) {

      LOG_INFO("subscribing to market data for " << instrument
               << " (object: "<< md<< ")");
      session->subscribe(instrument.native_symbol(), instrument.exchange_id(), md);
    }
    else {
      auto feed = _feeds.find(instrument.exchange_id());
      if (feed == _feeds.end()) {
        LOG_WARN("failed to find a feed handler for " << instrument);
        return nullptr;
      }

      // TODO: how do we control what we subscribe to? And options etc.
      feed->second->subscribe_trades(instrument.native_symbol(), md);
      feed->second->subscribe_top(instrument.native_symbol(), md);
    }
  }

  _markets.insert({instrument, std::move(mkt)});
  return md;
}


void MarketDataService::add_feed(FeedConfig config,
                                 std::list<std::string> venues)
{
  auto tlog_svc = _core->time_log_service();
  if (config.type == "BinanceUsdFut") {
    EmbeddedFeedHandler::FHBuilder fh_builder;
    fh_builder = [core=_core, config]
      (FeedHandlerCallbacks callbacks) -> std::shared_ptr<FeedHandler> {
      LOG_INFO("creating feed handler " << config.type);
      auto feed = std::make_shared<BinanceUsdFutFeedHandler>(
        core,
        core->run_mode(),
        core->reactor(),
        core->realtime_evloop(),
        callbacks
      );
      return feed;
    };

    auto embed_fh = std::make_shared<EmbeddedFeedHandler>(
      ExchangeId::binance_usdfut,
      fh_builder,
      tlog_svc);

    embed_fh->feed()->start();

    for (auto & venue: venues) {
      auto venue_exch_id = to_exchange_id(venue);
      if (_feeds.find(venue_exch_id) != std::end(_feeds))
        throw ConfigError(concat("venue alread added: ", venue));
      _feeds[venue_exch_id] = embed_fh;
      LOG_INFO("feed handler " << config.type << " provides venue '" << venue << "'");
    }
  }
  else if (config.type == "Binance") {
    EmbeddedFeedHandler:: FHBuilder fh_builder;
    fh_builder = [core=_core, config]
      (FeedHandlerCallbacks callbacks) -> std::shared_ptr<FeedHandler> {
      LOG_INFO("creating feed handler " << config.type);
      auto feed = std::make_shared<BinanceFeedHandler>(
        core,
        core->run_mode(),
        core->reactor(),
        core->realtime_evloop(),
        callbacks
        );
      return feed;
    };

    auto embed_fh = std::make_shared<EmbeddedFeedHandler>(
      ExchangeId::binance,
      fh_builder,
      tlog_svc);

    embed_fh->feed()->start();

    for (auto & venue: venues) {
      auto venue_exch_id = to_exchange_id(venue);
      if (_feeds.find(venue_exch_id) != std::end(_feeds))
        throw ConfigError(concat("venue alread added: ", venue));
      _feeds[venue_exch_id] = embed_fh;
      LOG_INFO("feed handler " << config.type << " provides venue '" << venue << "'");
    }

  }
  else {
    throw std::runtime_error(concat("unknown feed type '", config.type, "'"));
  }
}



} // namespace apex
