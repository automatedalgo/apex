/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free softwarg: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/

#include <apex/backtest/TardisCsvParsers.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/Error.hpp>

#include <string_view>
#include <cstring>


static void check_field(std::string_view expected, std::string_view actual)
{
  if (expected != actual) {
    THROW("Tardis CSV header problem; expected '"
          << expected << "', actual '"
          << actual << "'");
  }
}


namespace apex
{


[[nodiscard]] apex::Time TardisCsvParser::event_time() const
{
  if (this->parse_ok()) {
    auto epoc_usec2 = strtoll("123455", nullptr, 10);
    auto epoc_usec = strtoll(this->p_timestamp, nullptr, 10);
    auto epoc_sec = epoc_usec / 1000000;
    auto usec = epoc_usec - (epoc_sec * 1000000);
    apex::Time t(epoc_sec, std::chrono::microseconds(usec));
    return t;
  }
  else { //  "1706745599997000"
    return Time{};
  }
}


/* Search for a field delimiter.  If sound, overwrite it with null character
 * and return the field start (which is just the original value of `ptr`), and
 * update `ptr` to point to first byte after the delimiter.  If not found
 * return nullptr, which indicates an error, since the field is not
 * terminated. `end` points to first byte that should be read. */
inline char* TardisCsvParser::parse_field(char*& ptr,
                                          const char* end,
                                          char ch,
                                          bool& err)
{
  char* const start = ptr;
  char* delim = (char*)memchr(ptr, ch, end - ptr);
  if (delim) {
    *delim = '\0';
    ptr = delim + 1;
    return start;
  } else {
    err = true;
    return nullptr;
  }
}


bool TardisCsvParserBookSnapshot5::next()
{
  _parse_success = false;

  if (_ptr < _end && !_err) {

    // `line_fin` points to line's final char (the delimiter)
    char* const line_fin = (char*) memchr(_ptr, '\n', _end - _ptr);

    if (line_fin) {

      // replace the line-final char delimiter with a field delimiter; this
      // simplifies detection of unexpected extra delimiters
      *line_fin = ',';

      p_exchange = parse_field(_ptr, line_fin + 1, ',', _err);
      p_symbol = parse_field(_ptr, line_fin + 1, ',', _err);
      p_timestamp = parse_field(_ptr, line_fin + 1, ',', _err);
      p_local_timestamp = parse_field(_ptr, line_fin + 1, ',', _err);

      for (int i=0; i<5; i++) {
        p_ask_price[i] = parse_field(_ptr, line_fin + 1, ',', _err);
        p_ask_amount[i] = parse_field(_ptr, line_fin + 1, ',', _err);
        p_bid_price[i] = parse_field(_ptr, line_fin + 1, ',', _err);
        p_bid_amount[i] = parse_field(_ptr, line_fin + 1, ',', _err);
      }

      // after parsing all fields, _ptr should point to first byte that
      // follows the line delimiter
      _err = _err || (_ptr != (line_fin + 1));

      if (!_err)
        _parse_success = true;
    }
  }

  return _parse_success;
}

template<int N>
void book_check_header(const TardisCsvParserBookSnapshot<N>& parser)
{
  static_assert(N<1000);

  std::vector<std::string> expected = {
    "exchange",
    "symbol",
    "timestamp",
    "local_timestamp",
    "asks[0].price",
    "asks[0].amount",
    "bids[0].price",
    "bids[0].amount",
    "asks[1].price",
    "asks[1].amount",
    "bids[1].price",
    "bids[1].amount",
    "asks[2].price",
    "asks[2].amount",
    "bids[2].price",
    "bids[2].amount",
    "asks[3].price",
    "asks[3].amount",
    "bids[3].price",
    "bids[3].amount",
    "asks[4].price",
    "asks[4].amount",
    "bids[4].price",
    "bids[4].amount",
  };
  int i = 0;
  check_field(expected[i++], parser.p_exchange);
  check_field(expected[i++], parser.p_symbol);
  check_field(expected[i++], parser.p_timestamp);
  check_field(expected[i++], parser.p_local_timestamp);

  for (int l=0; l<N; l++) {
    check_field(expected.at(i++), parser.p_ask_price[l] );
    check_field(expected.at(i++), parser.p_ask_amount[l] );
    check_field(expected.at(i++), parser.p_bid_price[l] );
    check_field(expected.at(i++), parser.p_bid_amount[l] );
  }
}


template<int N>
std::string book_to_string(const TardisCsvParserBookSnapshot<N>& parser)
{
  std::ostringstream oss;
  oss << "exchange=" << parser.p_exchange << ", "
      << "symbol=" << parser.p_symbol << ", "
      << "timestamp=" << parser.p_timestamp << ", "
      << "local_timestamp=" << parser.p_local_timestamp << ", ";
  for (int i=0; i< N; i++) {
    oss << "ask_price[" << i << "]=" << parser.p_ask_price[i] << ", "
        << "ask_amount[" << i << "]=" << parser.p_ask_amount[i] << ", "
        << "bid_price[" << i << "]=" << parser.p_bid_price[i] << ", "
        << "bid_amount[" << i << "]=" << parser.p_bid_amount[i] << ", ";
  }
  return oss.str();
}


[[nodiscard]] std::string TardisCsvParserBookSnapshot5::to_string() const
{
  return book_to_string(*this);
}


void TardisCsvParserBookSnapshot5::check_header() const
{
  book_check_header(*this);
}


void TardisCsvParserBookSnapshot5::apply_event(MarketData* mktdata)
{
  // timestamp
  auto epoc_usec = atoll(this->p_timestamp);
  auto epoc_sec = epoc_usec / 1000000;
  auto usec = epoc_usec - (epoc_sec * 1000000);
  apex::Time t(epoc_sec, std::chrono::microseconds(usec));

  TickBookSnapshot5 tick;
  tick.xt = t;
  tick.et = t;
  for (int i=0; i<5; i++) {
    tick.levels[i].ask_price = atof(p_ask_price[i]);
    tick.levels[i].ask_qty = atof(p_ask_amount[i]);
    tick.levels[i].bid_price = atof(p_bid_price[i]);
    tick.levels[i].bid_qty = atof(p_bid_amount[i]);
  }

  mktdata->apply(tick);
}


// Parse the next record and return whether the next record was successfully
// parsed.  If it has been parsed, then the field values are now accessible.
// If a record was not parsed, this is either because we don't have a complete
// record in the buffer, or a parsing error has occurred.
bool TardisCsvParserTrades::next()
{
  _parse_success = false;

  if (_ptr < _end && !_err) {

    // `line_fin` points to line's final char (the delimiter)
    char* const line_fin = (char*) memchr(_ptr, '\n', _end - _ptr);

    if (line_fin) {

      // replace the line-final char delimiter with a field delimiter; this
      // simplifies detection of unexpected extra delimiters
      *line_fin = ',';

      p_exchange = parse_field(_ptr, line_fin + 1, ',', _err);
      p_symbol = parse_field(_ptr, line_fin + 1, ',', _err);
      p_timestamp = parse_field(_ptr, line_fin + 1, ',', _err);
      p_local_timestamp = parse_field(_ptr, line_fin + 1, ',', _err);
      p_id = parse_field(_ptr, line_fin + 1, ',', _err);
      p_side = parse_field(_ptr, line_fin + 1, ',', _err);
      p_price = parse_field(_ptr, line_fin + 1, ',', _err);
      p_amount = parse_field(_ptr, line_fin + 1, ',', _err);

      // after parsing all fields, _ptr should point to first byte that
      // follows the line delimiter
      _err = _err || (_ptr != (line_fin + 1));

      if (!_err)
        _parse_success = true;
    }
  }
  return _parse_success;
}


[[nodiscard]] std::string TardisCsvParserTrades::to_string() const
{
  return "TODO: TardisCsvParserTrades::to_string()";
}


void TardisCsvParserTrades::check_header() const
{
  check_field("exchange", p_exchange);
  check_field("symbol", p_symbol);
  check_field("timestamp", p_timestamp);
  check_field("local_timestamp", p_local_timestamp);
  check_field("id", p_id);
  check_field("side", p_side);
  check_field("price", p_price);
  check_field("amount", p_amount);
}


void TardisCsvParserTrades::apply_event(MarketData* md)
{
  // symbol - skip

  // timestamp
  auto epoc_usec = atoll(this->p_timestamp);
  auto epoc_sec = epoc_usec / 1000000;
  auto usec = epoc_usec - (epoc_sec * 1000000);
  apex::Time t(epoc_sec, std::chrono::microseconds(usec));

  // side
  Side aggr_side = Side::none;
  if (strcmp(this->p_side, "buy") == 0)
    aggr_side = Side::buy;
  else if (strcmp(this->p_side, "sell") == 0)
    aggr_side = Side::sell;

  // price
  double price = ::strtod(this->p_price, nullptr);

  // amount
  double qty = ::strtod(this->p_amount, nullptr);

  TickTrade tick;
  tick.aggr_side = aggr_side;
  tick.price = price;
  tick.qty = qty;
  tick.et = t;
  tick.xt = t;

  md->apply(tick);
}

} // namespace apex
