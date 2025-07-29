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
#include <apex/core/Strategy.hpp>
#include <apex/core/Logger.hpp>
#include <apex/venues/binance/BinanceUsdFutFeedHandler.hpp>
#include <apex/venues/binance/BinanceFeedHandler.hpp>
#include <apex/venues/bybit/BybitFeedHandler.hpp>
#include <apex/venues/kucoin/KucoinFutFeedHandler.hpp>

#include <iostream>

using namespace apex;

/*
  Basic example of using the feed handler classes directly. This is useful
  during development of feed handlers.
*/
extern char *program_invocation_name;
extern char *program_invocation_short_name;


/*

Two approaches to an automatic filename:

- has daily TS, appended
- had high fideltiy timestamp, truncated


 */

struct LogfileOpts {
  enum Time { none, day, second} time = LogfileOpts::second;
  enum Mode { trunc, append } mode = LogfileOpts::trunc;

  static LogfileOpts append_daily() {
    return {
      .time=day,
      .mode=append};
  };
  static LogfileOpts trunc_seconds() {
    return {
      .time=second,
      .mode=trunc};
  };
};

std::string default_logfile_name(LogfileOpts opts = LogfileOpts::trunc_seconds()) {
  Time t = Time::realtime_now();
  auto base_path = apex_home() / "log";

  base_path /= std::string(program_invocation_short_name);

  switch (opts.time) {
    case LogfileOpts::day :
      base_path += t.strftime(".%Y%m%d");
      break;
    case LogfileOpts::second :
      base_path += t.strftime(".%Y%m%d-%H%M%S");
      break;
    case LogfileOpts::none:
      break;
  }
  base_path += ".log";

  return base_path.string();
}

int main()
{
  try {


    auto fn = default_logfile_name();
    LOG_INFO("FN: " << fn);
    return 0;
    Logger::instance().enable_async_mode();
    //Logger::instance().enable_file_logging("/var/tmp/apex-log.txt", true);


    // std::cout << "auto: " << create_automatic_filename() << "\n";
    // Time t = Time::realtime_now();

    // std::cout << "Full name: " << program_invocation_name << std::endl;
    // std::cout << "Short name: " << program_invocation_short_name << std::endl;
    // std::cout <<

    return 0;

    Logger::instance().set_level(Logger::info);

    // create core engine, configured for paper or live trading
    auto services = Services::create(RunMode::paper);

    // list of our feed handers, place them in here to keep them alive
    std::list<std::shared_ptr<FeedHandler>> feeds;

    // callbacks that will receive the market data updates - here we just log
    // them.
    FeedHandlerCallbacks callbacks;
    callbacks.on_trade = [](std::string pxsym, TickTrade& t){
      LOG_INFO(pxsym
               << ": qty:" << format_double(t.qty, true, 6)
               << ", price:" << format_double(t.price, true, 6)
               << ", side:" << t.side
               << ", xt: " << t.xt
               << ", et: " << t.et
        );
    };
    callbacks.on_top = [](std::string pxsym, TickTop& t){
      LOG_INFO(pxsym
               << ": bid:" << format_double(t.bid_price, true, 6)
               << ", ask:" << format_double(t.ask_price, true, 6));
    };

    // ----------------------------------------------------------------------
    // BINANCE SPOT
    // ----------------------------------------------------------------------

    if (1)
    {
      auto feed_handler = std::make_shared<BinanceFeedHandler>(
        services.get(),
        services->run_mode(),
        services->reactor(),
        services->realtime_evloop(),
        callbacks
        );
      feed_handler->subscribe_trades("btcusdt");
      //feed_handler->subscribe_top("btcusdt");

      feeds.push_back(feed_handler);
    }

    // ----------------------------------------------------------------------
    // BINANCE USD FUTURES
    // ----------------------------------------------------------------------

    if (0)
    {
      auto feed_handler = std::make_shared<BinanceUsdFutFeedHandler>(
        services.get(),
        services->run_mode(),
        services->reactor(),
        services->realtime_evloop(),
        callbacks
        );
      feed_handler->subscribe_trades("btcusdt");
      //feed_handler->subscribe_top("btcusdt");

      feeds.push_back(feed_handler);
    }

    // ----------------------------------------------------------------------
    // KUCOIN FUTURES
    // ----------------------------------------------------------------------

    if (0)
    {
      auto feed_handler = std::make_shared<KucoinFutFeedHandler>(
        services.get(),
        services->run_mode(),
        services->reactor(),
        services->realtime_evloop(),
        callbacks
        );
      feed_handler->subscribe_trades("XBTUSDTM");

      feeds.push_back(feed_handler);
    }

    // ----------------------------------------------------------------------
    // BYBIT
    // ----------------------------------------------------------------------

    if (0)
    {
      auto feed_handler = std::make_shared<ByBitFeedHandler>(
        services.get(),
        services->run_mode(),
        services->reactor(),
        services->realtime_evloop(),
        callbacks
        );

      feed_handler->subscribe_trades("BTCUSDT");

      feeds.push_back(feed_handler);
    }

    // ----------------------------------------------------------------------
    // START ALL FEEDS
    // ----------------------------------------------------------------------

    if (feeds.empty()) {
      std::cout << "no feed handlers created" << std::endl;
      return 1;
    }

    for (auto & feed : feeds)
      feed->start();

    services->run();
  }
  catch (std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    return 1;
  }
}
