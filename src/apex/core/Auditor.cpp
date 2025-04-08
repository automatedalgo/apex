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

#include <apex/model/Position.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/core/Auditor.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/EventLoop.hpp>

#include <iostream>

#define FMT(X) (std::isfinite(X)? apex::format_double(X, true): "")

namespace apex
{

int to_int(Side s) {
  switch (s) {
    case Side::buy:
      return 1;
    case Side::sell:
      return -1;
    case Side::none:
      return 0;
  }
  return 0;
}



 Auditor::Auditor(Services * services,
                 std::string transactions_dir)
  : _services(services)
{
  if (transactions_dir.empty())
    transactions_dir = apex_home() / "log";

  if (!transactions_dir.empty())
    create_dir(transactions_dir);

  std::ostringstream oss;

  if (!transactions_dir.empty())
    oss << transactions_dir << "/";

  oss << "audit-transactions-";
  oss << Time::realtime_now().strftime("%Y%m%d_%H%M%S");
  oss << ".csv";

  // DIR/apex_transactions-DATE.log

  auto now = Time::realtime_now();

  auto fn = oss.str();
  LOG_INFO("auditor transactions file '" << fn << "'");

  _file.open(fn, std::ofstream::out | std::ofstream::trunc);
  auto columns = {
    "time",
    "symbol",
    "venue",
    "event",
    "order_state",
    "order_id",
    "side",
    "qty",
    "price",
    "value_usd",
    "done_qty",
    "remain_qty",
    "fill_qty",
    "fill_price",
    "exch_order_id",
    "buy_qty",
    "sell_qty",
    "net_qty",
    "buy_cost",
    "sell_cost",
    "turnover",
    "total_pnl",
    "bid",
    "ask",
    "last",
    "last_qty",
    "last_time",
    "fx_to_usd",
    "iside",
    "strat_id",
  };
  for (auto & item : columns)
    _file << item << ",";
  _file << "\n";

  auto delay = std::chrono::seconds(5);
  _services->evloop()->dispatch(delay, [this, delay]()-> std::chrono::milliseconds {
      try {
        this->_file.flush();
        return delay;
      } catch (...) {
        LOG_ERROR("transactions file flush failed");
        return {};
      }
    });
}

Auditor::~Auditor() = default;

void Auditor::add_transaction(Time time,
                              const std::string& strat_id,
                              const OrderEvent& order_event,
                              const std::string /* event_type */,
                              const Position& position,
                              const MarketData* market_data,
                              double fx_to_usd,
                              bool is_fill,
                              double fill_qty,
                              double fill_price)
{
  _file
    << time.as_iso8601(Time::Resolution::micro, true)
    << "," << order_event.order->instrument().native_symbol()
    << "," << order_event.order->instrument().exchange_name()
    << "," << (is_fill? "fill": "order")
    // order
    << "," << order_event.order->state()
    << "," << order_event.order->order_id()
    << "," << order_event.order->side()
    << "," << FMT(order_event.order->size())
    << "," << FMT(order_event.order->price())
    << "," << FMT(order_event.order->size() * order_event.order->price() * fx_to_usd)
    << "," << FMT(order_event.order->filled_size())
    << "," << FMT(order_event.order->remain_size())
    // current fill
    << "," << (is_fill? FMT(fill_qty) : "")
    << "," << (is_fill? FMT(fill_price) : "")
    // other order fields
    << "," << order_event.order->exch_order_id()
    // position
    << "," << FMT(position.buy_qty())
    << "," << FMT(position.sell_qty())
    << "," << FMT(position.net_qty())
    << "," << FMT(position.buy_cost())
    << "," << FMT(position.sell_cost())
    << "," << FMT(position.total_turnover(market_data->last().price))
    << "," << FMT(position.total_pnl(market_data->last().price))
    // market data
    << "," << FMT(market_data->bid())
    << "," << FMT(market_data->ask())
    << "," << FMT(market_data->last().price)
    << "," << FMT(market_data->last().qty)
    << "," << market_data->last().et.as_iso8601(Time::Resolution::micro, true)
    << "," << FMT(fx_to_usd)
    << "," << to_int(order_event.order->side())
    // misc
    << "," << strat_id
    << "\n";
}

}
