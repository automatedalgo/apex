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

#include "quicktest.hpp"

#include <apex/util/platform.hpp>
#include <apex/util/utils.hpp>
#include <apex/backtest/TardisFileReader.hpp>
#include <apex/backtest/TardisCsvParsers.hpp>

#include <iostream>

#include <string.h>

using namespace std;


auto raw30 = R"(exchange,symbol,timestamp,local_timestamp,id,side,price,amount
binance,LASTLAST,1704067199991000,1704067200004373,1266393080,buy,2281.88,0.0394
)";


auto raw35 = R"(exchange,symbol,timestamp,local_timestamp,id,side,price,amount
binance,ETHUSDT,1704067199989000,1704067200004171,1266393072,buy,2281.88,0.043
binance,ETHUSDT,1704067199990000,1704067200004280,1266393073,sell,2281.87,0.0209
binance,ETHUSDT,1704067199990000,1704067200004287,1266393074,buy,2281.88,0.0132
binance,ETHUSDT,1704067199990000,1704067200004301,1266393075,sell,2281.87,0.0434
binance,ETHUSDT,1704067199990000,1704067200004307,1266393076,sell,2281.87,0.0215
binance,ETHUSDT,1704067199991000,1704067200004333,1266393077,buy,2281.88,0.0142
binance,ETHUSDT,1704067199991000,1704067200004334,1266393078,sell,2281.87,0.038
binance,ETHUSDT,1704067199991000,1704067200004357,1266393079,sell,2281.87,0.016
binance,LASTLAST,1704067199991000,1704067200004373,1266393080,buy,2281.88,0.0394
)";


auto raw48 = R"(exchange,symbol,timestamp,local_tisd;kf;lsakf;sakfd;sfajflkdsjflkdsjflksjdfljsdf;ljsad;lkfjsd;lfjsdf
)";

auto raw_ = R"(exchange,symbol,timestamp,local_timestamp,id,side,price,amount
binance,ETHUSDT,1704067199989000,1704067200004171,1266393072,buy,2281.88,0.043,extra
binance,ETHUSDT,1704067199990000,1704067200004280,1266393073,sell,2281.87,0.0209
binance,ETHUSDT,1704067199990000,1704067200004287,1266393074,buy,2281.88,0.0132
binance,ETHUSDT,1704067199990000,1704067200004301,1266393075,sell,2281.87,0.0434
binance,ETHUSDT,1704067199990000,1704067200004307,1266393076,sell,2281.87,0.0215
binance,ETHUSDT,1704067199991000,1704067200004333,1266393077,buy,2281.88,0.0142
binance,ETHUSDT,1704067199991000,1704067200004334,1266393078,sell,2281.87,0.038
binance,ETHUSDT,1704067199991000,1704067200004357,1266393079,sell,2281.87,0.016
binance,LASTLAST,1704067199991000,1704067200004373,1266393080,buy,2281.88,0.0394
)";


auto raw = R"(exchange,symbol,timestamp,local_timestamp,id,side,price,amount
binance,ETHUSDT,1704067199989000,1704067200004171,1266393072,buy,2281.88,0.043
binance,ETHUSDT,1704067199990000,1704067200004280,1266393073,sell,2281.87,0.0209
YYbinance,ETHUSDT,1704067199990000,1704067200XXXXXXX)";


void tardis_decode(std::string raw)
{
  std::vector<char> bytes(raw.size());
  memcpy(bytes.data(), raw.c_str(), raw.size());

  // parse_raw(bytes.data(), bytes.size());

  apex::TardisCsvParserTrades parser(bytes.data(), bytes.size());

  while (parser.next()) {
    cout << "----- record -----\n";
    cout << parser.to_string() << "\n";
    cout << "parsed: " << parser.bytes_parsed() << "\n";
    cout << "bad: " << parser.is_bad() << "\n";
    cout << "avail: " << parser.avail() << "\n";
  }
  cout << "----- complete -----\n";


  cout << "bad: " << parser.is_bad() << "\n";
  cout << "avail: " << parser.avail() << "\n";

  if (parser.avail()) {
    cout << "next bytes: " << std::string_view(parser.ptr(), 3) << "\n";

  }
}

TEST_CASE("tardis_parse_trades") { tardis_decode(raw); }


TEST_CASE("utils")
{
  REQUIRE(apex::split("", ',').empty());
  REQUIRE(apex::split(",", ',') == vector<string>({"", ""}));
  REQUIRE(apex::split("a,b", ',') == vector<string>({"a", "b"}));
  REQUIRE(apex::split("a,,b", ',') == vector<string>({"a", "", "b"}));

  REQUIRE(apex::trim(" a	 ") == "a");
  REQUIRE(apex::trim(" a") == "a");
  REQUIRE(apex::trim("  ").empty());
  REQUIRE(apex::trim("	").empty());
  REQUIRE(apex::trim("").empty());

  REQUIRE(apex::trim("012340", "0") == "1234");
  REQUIRE(apex::trim("1234", "0") == "1234");
  REQUIRE(apex::trim("1234", "") == "1234");
  REQUIRE(apex::trim("", "").empty());
}

TEST_CASE("json")
{
  std::string rawstr =
      R"({  "bin":"20240505",    "c":"aggtrades",    "cm":{"loc":"london"},    "e":"binance",    "i":"BTC/USDT.BNC",    "s":"BTCUSDT"})";

  auto msg = json::parse(rawstr);


}

int main(int argc, char** argv)
{
  try {
    int result = quicktest::run(argc, argv);
    return (result < 0xFF ? result : 0xFF);
  } catch (exception& e) {
    cout << e.what() << endl;
    return 1;
  }
}
