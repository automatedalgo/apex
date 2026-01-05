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
#include <simdjson/simdjson.h>
#include <apex/venues/binance/binance_common.hpp>
#include <websocketpp/message_buffer/message.hpp>

#include <iostream>
#include <string_view>

using namespace std;
using namespace apex;
using namespace simdjson;

static_assert(simdjson::SIMDJSON_PADDING == WSCPP_SIMDJSON_PADDING);

TEST_CASE("parse_bookticker")
{
  constexpr std::string_view BOOK_TICKER_MSG = R"({"stream":"solusdt@bookTicker","data":{"e":"bookTicker","u":9507065556773,"s":"SOLUSDT","b":"126.1800","B":"952.91","a":"126.1900","A":"594.90","T":1766311173056,"E":1766311173057}})";

  simdjson::error_code error{};
  simdjson::ondemand::parser parser{};

  simdjson::padded_string padded_buf(BOOK_TICKER_MSG); // copies to a padded buffer
  simdjson::ondemand::document doc = parser.iterate(padded_buf);

  simdjson::ondemand::object root;
  error = doc.get_object().get(root);
  REQUIRE(error == simdjson::error_code::SUCCESS);

  std::string_view stream;
  TickTop tick;
  int err;

  for (auto field : root) {
    std::string_view key;
    error = field.unescaped_key().get(key);  // get field name
    REQUIRE(error == simdjson::error_code::SUCCESS);
    if (key == "stream") {
      error = field.value().get(stream);
      REQUIRE(error == simdjson::error_code::SUCCESS);
    }
    else if (key == "data") {
      simdjson::ondemand::object data; // invalid until the get() succeeds
      error = field.value().get(data);
      REQUIRE(error == simdjson::error_code::SUCCESS);
      std::tie(tick, err) = parse_binanceusdfut_bookticker(data);
    }
  }

  REQUIRE(stream == "solusdt@bookTicker");

  std::cout << "bid_price: " << tick.bid_price << ", "
            << "bid_qty: " << tick.bid_qty << ", "
            << "ask_price: " << tick.ask_price << ", "
            << "ask_qty: " << tick.ask_qty << ", "
            << "xt: " << tick.xt
            << "et: " << tick.et
            << "\n";

  REQUIRE(tick.bid_price == 126.18);
  REQUIRE(tick.ask_price == 126.19);
  REQUIRE(tick.bid_qty == 952.91);
  REQUIRE(tick.ask_qty == 594.9);
}


TEST_CASE("parse_via_lookup")
{
  // 'stream' is first
  constexpr std::string_view AGG_TRADE_MSG = R"({"stream":"solusdt@aggTrade","data":{"e":"aggTrade","E":1766311173057,"a":1015031140,"s":"SOLUSDT","p":"126.1900","q":"7.85","f":3028484142,"l":3028484150,"T":1766311172921,"m":false}})";

  // 'stream' is second
  constexpr std::string_view AGG_TRADE_MSG_2 = R"({"data":{"e":"aggTrade","E":1766311173057,"a":1015031140,"s":"SOLUSDT","p":"126.1900","q":"7.85","f":3028484142,"l":3028484150,"T":1766311172921,"m":false}, "msgid":"1234","stream":"solusdt@aggTrade" })";

  auto msgs = { AGG_TRADE_MSG, AGG_TRADE_MSG_2};
  for (const std::string_view & msg : msgs ) {
    simdjson::error_code error{};
    simdjson::ondemand::parser parser{};
    simdjson::padded_string padded_buf(msg); // copies to a padded buffer
    simdjson::ondemand::document doc = parser.iterate(padded_buf);

    simdjson::ondemand::object root;
    error = doc.get_object().get(root);
    REQUIRE(error == simdjson::error_code::SUCCESS);

    std::string_view stream;
    error = root["stream"].get(stream);
    REQUIRE(error == simdjson::error_code::SUCCESS);
    REQUIRE(stream == "solusdt@aggTrade");

    const size_t pos = stream.find('@');
    REQUIRE(pos != std::string_view::npos);

    std::string_view ticker = stream.substr(0, pos);
    std::string_view msgtype = stream.substr(pos + 1);
    REQUIRE(stream == "solusdt");
    REQUIRE(msgtype == "aggTrade");

    simdjson::ondemand::object data;
    error = root["data"].get(data);
    REQUIRE(error == simdjson::error_code::SUCCESS);

    TickTrade tick;
    int err;
    std::tie(tick, err) = parse_binanceusdfut_aggtrade(data);

    REQUIRE(stream == "solusdt@aggTrade");
    REQUIRE(tick.price == 126.19);
    REQUIRE(tick.qty == 7.85);
    REQUIRE(err == 0);
    std::cout << "price: " << tick.price << ", "
              << "qty: " << tick.qty << ", "
              << "side: " << tick.side << ", "
              << "xt: " << tick.xt << ", "
              << "et: " << tick.et
              << "\n";
  }
}


TEST_CASE("parse_aggtrade")
{
  constexpr std::string_view AGG_TRADE_MSG = R"({"stream":"solusdt@aggTrade","data":{"e":"aggTrade","E":1766311173057,"a":1015031140,"s":"SOLUSDT","p":"126.1900","q":"7.85","f":3028484142,"l":3028484150,"T":1766311172921,"m":false}})";

  constexpr std::string_view AGG_TRADE_MSG_2 = R"({"data":{"e":"aggTrade","E":1766311173057,"a":1015031140,"s":"SOLUSDT","p":"126.1900","q":"7.85","f":3028484142,"l":3028484150,"T":1766311172921,"m":false}, "stream":"solusdt@aggTrade" })";

  auto msgs = { AGG_TRADE_MSG, AGG_TRADE_MSG_2};
  for (const std::string_view & msg : msgs ) {

    simdjson::error_code error{};
    simdjson::ondemand::parser parser{};

    simdjson::padded_string padded_buf(msg); // copies to a padded buffer
    simdjson::ondemand::document doc = parser.iterate(padded_buf);

    simdjson::ondemand::object root;
    error = doc.get_object().get(root);
    REQUIRE(error == simdjson::error_code::SUCCESS);

    std::string_view stream;
    TickTrade tick;
    int err;

    for (auto field : root) {
      std::string_view key;
      error = field.unescaped_key().get(key);  // get field name
      REQUIRE(error == simdjson::error_code::SUCCESS);
      if (key == "stream") {
        error = field.value().get(stream);
        REQUIRE(error == simdjson::error_code::SUCCESS);
      }
      else if (key == "data") {
        simdjson::ondemand::object data; // invalid until the get() succeeds
        error = field.value().get(data);
        REQUIRE(error == simdjson::error_code::SUCCESS);
        std::tie(tick, err) = parse_binanceusdfut_aggtrade(data);
      }
    }

    REQUIRE(stream == "solusdt@aggTrade");
    REQUIRE(tick.price == 126.19);
    REQUIRE(tick.qty == 7.85);
  }
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
