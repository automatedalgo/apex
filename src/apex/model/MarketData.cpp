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

MarketData::MarketData() {
}

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


void MarketData::apply(TickTop& t)
{
  _book.best_ask_price = t.ask_price;
  _book.best_bid_price = t.bid_price;
  EventType mask = {};
  mask.value = EventType::top;
  for (auto& item : _events_listeners)
    item(mask);
}

} // namespace apex
