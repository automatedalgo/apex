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

#pragma once


#include <apex/model/Order.hpp>
#include <apex/model/tick_msgs.hpp>

#include <cstdint>
#include <vector>

namespace apex
{

namespace tickbin {

enum class MsgType : int {
  None = 0,
  TickLevel1 = 1,
  TickAggTrade = 2,
};

#pragma pack(push, 1)

struct Header {
  uint64_t capture_time; // usec since epoch
  uint8_t msg_type;
  uint8_t size;
};
static_assert(sizeof(Header) == 10);

template <typename T>
struct FullMsg {
  Header head;
  T body;
};

struct TickLevel1 {
  static const int Type = static_cast<int>(MsgType::TickLevel1);
  double ask_price;
  double ask_qty;
  double bid_price;
  double bid_qty;
};


struct TickAggTrade {
  double price;
  double qty;
  uint64_t et;
  char side;
  char pad[3];
};

#pragma pack(pop)


class Serialiser {
public:
  using bytes = std::vector<char>;

  static char encode_side(apex::Side side);
  static Side decode_side(char c);

  static bytes serialise(Time capture_time, TickTop& src);
  static bytes serialise(Time capture_time, TickTrade& src);

  static void deserialise(char * buf, TickTop&);
  static void deserialise(char * buf, TickTrade&);

};


} // namespace tickbin




} // namespace apex
