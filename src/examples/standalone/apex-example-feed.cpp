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

int main()
{
  try {

    Logger::instance().set_level(Logger::info);

    // create core engine, configured for paper or live trading
    auto services = Services::create(RunMode::paper);

    // list of our feed handers
    std::list<std::shared_ptr<FeedHandler>> feeds;

    // callbacks that will receive the market data updates - here we just log
    // them.
    FeedHandlerCallbacks callbacks;
    callbacks.on_trade = [](std::string pxsym, TickTrade& t, TimeLog&){
      LOG_INFO(pxsym
               << ": qty:" << format_double(t.qty, true, 6)
               << ", price:" << format_double(t.price, true, 6)
               << ", side:" << t.side
               << ", xt: " << t.xt
               << ", et: " << t.et
        );
    };
    callbacks.on_top = [](std::string pxsym, TickTop& t, TimeLog&){
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
