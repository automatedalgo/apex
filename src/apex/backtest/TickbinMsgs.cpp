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

#include <apex/backtest/TickbinMsgs.hpp>

#include <string.h>

namespace apex {
namespace tickbin {

char Serialiser::encode_side(apex::Side side)
{
  switch (side) {
    case apex::Side::buy:
      return 'b';
    case apex::Side::sell:
      return 's';
    default:
      return ' ';
  }
}


Side Serialiser::decode_side(char c)
{
  switch (c) {
    case 'b':
      return Side::buy;
    case 's':
      return Side::sell;
    default:
      return Side::none;
  }
}


Serialiser::bytes Serialiser::serialise(Time capture_time, TickTop& src) {
  apex::tickbin::FullMsg<apex::tickbin::TickLevel1> msg;
  memset(&msg, 0, sizeof(msg));
  msg.head.capture_time = capture_time.as_epoch_us().count();
  msg.head.size = sizeof(msg);
  msg.body.ask_price = src.ask_price;
  msg.body.ask_qty = src.ask_qty;
  msg.body.bid_price = src.bid_price;
  msg.body.bid_qty = src.bid_qty;
  return {(char*) &msg,  ((char*)&msg) + sizeof(msg) };
}


Serialiser::bytes Serialiser::serialise(Time capture_time, TickTrade& src) {
  apex::tickbin::FullMsg<apex::tickbin::TickAggTrade> msg;
  memset(&msg, 0, sizeof(msg));
  msg.head.capture_time = capture_time.as_epoch_us().count();
  msg.head.size = sizeof(msg);
  msg.body.price = src.price;
  msg.body.qty = src.qty;
  msg.body.et = src.et.as_epoch_us().count();
  msg.body.side = encode_side(src.aggr_side);
  return {(char*) &msg,  ((char*)&msg) + sizeof(msg) };
}


void Serialiser::deserialise(char * buf, TickTop& tick) {
  auto * bin = reinterpret_cast<FullMsg<tickbin::TickLevel1>*>(buf);
  tick.ask_price = bin->body.ask_price;
  tick.ask_qty = bin->body.ask_qty;
  tick.bid_price = bin->body.bid_price;
  tick.bid_qty = bin->body.bid_qty;
}


void Serialiser::deserialise(char * buf, TickTrade& tick) {
  auto * bin = reinterpret_cast<FullMsg<tickbin::TickAggTrade>*>(buf);
  tick.et = Time{std::chrono::microseconds{bin->body.et}};
  tick.price = bin->body.price;
  tick.qty = bin->body.qty;
  tick.aggr_side = decode_side(bin->body.side);
}

}}
