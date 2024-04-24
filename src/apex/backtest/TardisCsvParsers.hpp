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

#pragma once

#include <apex/util/Time.hpp>

#include <string>
#include <array>

namespace apex
{

class MarketData;

class TardisCsvParser
{
public:
  TardisCsvParser()
    : _buf(nullptr), _end(nullptr), _ptr(nullptr) {}


  TardisCsvParser(char* buf, std::size_t len)
    : _buf(buf), _end(buf + len), _ptr(buf)
  {
  }

  virtual ~TardisCsvParser() = default;
  virtual bool next() = 0;
  virtual void check_header() const = 0;
  virtual void apply_event(MarketData*) = 0;
  [[nodiscard]] virtual std::string to_string() const = 0;

  // Return the number of bytes still available for parsing
  [[nodiscard]] std::size_t avail() const { return _end - _ptr;   }

  [[nodiscard]] char* ptr() const { return _ptr; }

  [[nodiscard]] std::size_t bytes_parsed() const { return _ptr - _buf; }

  [[nodiscard]] bool parse_ok() const { return _parse_success; }

  // Return whether the parser has encountered an error
  [[nodiscard]] bool is_bad() const { return _err; }


  /* Reset parser state to point to new raw bytes available for decoding. */
  void reset_pointers(char* buf, std::size_t len)
  {
    _buf = buf;
    _end = buf + len;
    _ptr = buf;
  }


  [[nodiscard]] apex::Time event_time() const;


protected:
  /* Search for a field delimiter.  If sound, overwrite it with null character
   * and return the field start (which is just the original value of `ptr`), and
   * update `ptr` to point to first byte after the delimiter.  If not found
   * return nullptr, which indicates an error, since the field is not
   * terminated. `end` points to first byte that should be read. */
  static inline char*  parse_field(char*& ptr, const char* end, char ch,
                                   bool& err);

  char* _buf;
  char* _end;
  char* _ptr;

  bool _err = false;

  bool _parse_success = false;

public:
  // parsed fields common to all Tardis CSV datasets
  char* p_exchange = nullptr;
  char* p_symbol = nullptr;
  char* p_timestamp = nullptr;
  char* p_local_timestamp = nullptr;


};



template<int N>
class TardisCsvParserBookSnapshot : public TardisCsvParser
{
public:
  static constexpr std::size_t levels = N;

  TardisCsvParserBookSnapshot() = default;
  TardisCsvParserBookSnapshot(char* buf, std::size_t len)
    : TardisCsvParser(buf, len)
  {
  }

public:
  std::array<char*, N> p_ask_price = {nullptr};
  std::array<char*, N> p_ask_amount = {nullptr};
  std::array<char*, N> p_bid_price =  {nullptr};
  std::array<char*, N> p_bid_amount = {nullptr};
};


class TardisCsvParserBookSnapshot5 : public TardisCsvParserBookSnapshot<5>
{
public:
  TardisCsvParserBookSnapshot5() = default;
  TardisCsvParserBookSnapshot5(char* buf, std::size_t len)
    : TardisCsvParserBookSnapshot<5>(buf, len)
  {
  }

  // Parse the next record and return whether the next record was successfully
  // parsed.  If it has been parsed, then the field values are now accessible.
  // If a record was not parsed, this is either because we don't have a complete
  // record in the buffer, or a parsing error has occurred.
  bool next() override;

  [[nodiscard]] std::string to_string() const override;

  void check_header() const override;

  void apply_event(MarketData*) override;

};


class TardisCsvParserTrades : public TardisCsvParser
{
public:
  TardisCsvParserTrades() = default;
  TardisCsvParserTrades(char* buf, std::size_t len)
    : TardisCsvParser(buf, len)
  {
  }

  // Parse the next record and return whether the next record was successfully
  // parsed.  If it has been parsed, then the field values are now accessible.
  // If a record was not parsed, this is either because we don't have a complete
  // record in the buffer, or a parsing error has occurred.
  bool next() override;

  [[nodiscard]] std::string to_string() const override;

  void check_header() const override;

  void apply_event(MarketData*) override;

public:
  char* p_id = nullptr;
  char* p_side = nullptr;
  char* p_price = nullptr;
  char* p_amount = nullptr;
};


} // namespace apex
