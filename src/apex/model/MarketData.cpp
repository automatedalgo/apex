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

#include <apex/model/MarketData.hpp>

#include <ostream>

namespace apex
{

MarketData::MarketData() = default;

std::ostream& operator<<(std::ostream& os, MdStream& st)
{
  switch (st) {
    case MdStream::Null : os << "null"; break;
    case MdStream::L1 : os << "l1"; break;
    case MdStream::L2 : os << "l2"; break;
    case MdStream::L3 : os << "l3"; break;
    case MdStream::Trades : os << "trades"; break;
    case MdStream::AggTrades : os << "aggtrades"; break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const TickTrade& trade)
{
  os << trade.aggr_side << " " << trade.qty << " @ " << trade.price;
  return os;
}

void MarketData::subscribe_events(std::function<void(EventType)> fn)
{
  _events_listeners.push_back(std::move(fn));
}


void MarketData::apply(TickTrade& t)
{
  this->_last = t;
  EventType mask = {};
  mask.value = EventType::trade;
  for (auto& item : _events_listeners) {
    item(mask);
  }
}


void MarketData::apply(TickTop& tick)
{
  _l1_bid.price = tick.bid_price;
  _l1_bid.qty = tick.bid_qty;
  _l1_ask.price = tick.ask_price;
  _l1_ask.qty = tick.ask_qty;

  EventType mask = {};
  mask.value = EventType::top;
  for (auto& item : _events_listeners)
    item(mask);
}


void Book::apply(TickBookSnapshot5& tick)
{
  unsigned N = TickBookSnapshot5::N;

  if (_bids.size() != N) {
    _bids = std::vector<Level>(N, {0,0});
    _asks = std::vector<Level>(N, {0,0});
  }

  for (std::size_t i = 0; i < 5; i++) {
    _bids[i].price = tick.levels[i].bid_price;
    _bids[i].qty = tick.levels[i].bid_qty;
    _asks[i].price = tick.levels[i].ask_price;
    _asks[i].qty = tick.levels[i].ask_qty;
  }
}


void MarketData::apply(TickBookSnapshot5& tick)
{
  _book.apply(tick);

  _l1_bid.price = tick.levels[0].bid_price;
  _l1_bid.qty = tick.levels[0].bid_qty;
  _l1_ask.price = tick.levels[0].ask_price;
  _l1_ask.qty = tick.levels[0].ask_qty;

  EventType mask(EventType::top | EventType::full_book);
  for (auto& item : _events_listeners)
    item(mask);
}

[[nodiscard]] double MarketData::mid() const
{
  if (bid() == 0.0 || ask() == 0.0)
    return 0.0;
  else
    return (bid()+ask())/2.0;
}

} // namespace apex
