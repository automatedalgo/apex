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

#include <apex/backtest/TickbinFileReader.hpp>
#include <apex/backtest/TickbinMsgs.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/util/Error.hpp>

#include <fstream>

#include <unistd.h>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <iostream>

namespace apex
{

TickbinHeader decode_tickbin_file_header(const char* ptr)
{
  char ver[8] = {0};
  char len[8] = {0};

  memcpy(&ver[0], ptr, 8);
  ptr += 8;
  memcpy(&len[0], ptr, 8);

  TickbinHeader header;
  header.version = trim(std::string_view{ver, 8});
  header.length =  std::stoul(trim(std::string_view{len, 8}));
  return header;
}


class TickbinDecoder
{
public:
  TickbinDecoder(char* file_start, char* file_end)
    : _head(file_start), _start(file_start), _end(file_end)
  {
  }

  ~TickbinDecoder() = default;

  virtual apex::Time get_next_event_time() const {
    tickbin::Header* head = reinterpret_cast<tickbin::Header*>(_head);
    std::chrono::microseconds event_time(head->capture_time);
    return apex::Time(event_time);
  }

  virtual bool has_next_event()  {
    size_t bytes_remaining = _end - _head;
    if (bytes_remaining > sizeof(tickbin::Header)) {
      tickbin::Header* head = reinterpret_cast<tickbin::Header*>(_head);
      assert(head->size >= sizeof(tickbin::Header));
      return  bytes_remaining >= head->size;
    }
    else
      return false;
  }

  virtual void consume_next_event(MarketData*) {}

  const char* read_head() const { return _head; }

protected:
  char* _head;
  char* _start;
  char* _end;
};



class TickbinDecoderTickLevel1 : public TickbinDecoder
{
public:
  TickbinDecoderTickLevel1(char* ptr, char* end)
    : TickbinDecoder(ptr, end)
  {
  }

  void consume_next_event(MarketData* mktdata) override
  {
    // TODO: should use type ID here to cast to appropriate type
    tickbin::Header* head = reinterpret_cast<tickbin::Header*>(_head);

    if (mktdata) {
      TickTop tick;
      tickbin::Serialiser::deserialise(_head, tick);
      mktdata->apply(tick);
      //LOG_INFO("tickL1 update: " << tick.ask_price);
    }

    _head += head->size;
  }
};


class TickbinDecoderAggTrade : public TickbinDecoder
{
public:
  TickbinDecoderAggTrade(char* ptr, char* end)
    : TickbinDecoder(ptr, end)
  {
  }

  void consume_next_event(MarketData* mktdata) override
  {
    // TODO: should use type ID here to cast to appropriate type

    tickbin::Header* head = reinterpret_cast<tickbin::Header*>(_head);
    if (mktdata) {
      TickTrade tick;
      tickbin::Serialiser::deserialise(_head, tick);
      mktdata->apply(tick);
      // LOG_INFO("trade update: " << tick.price  << ", time: " << tick.et);
    }

    _head += head->size;
  }
};


TickbinFileReader::TickbinFileReader(std::filesystem::path fn,
                                     MarketData* mktdata,
                                     MdStream stream_type)
  :_fn(fn),
   _mktdata(mktdata)
{
  namespace fs = std::filesystem;

  if (!fs::exists(fn) || !fs::is_regular_file(fn)) {
    THROW("tickbin file not found " << fn);
  }
  LOG_INFO("reading tickbin file " << fn);
  auto file = std::ifstream(fn, std::ios::binary);

  std::string fn_native = fn.native();

  // check file is accessible
  if (access(fn_native.c_str(), F_OK) < 0) {
    THROW("access failed, file " << fn << ", errno " << errno);
  }

  // try open
  auto fd = open(fn_native.c_str(), O_RDONLY);
  if (fd < 0) {
    THROW("open failed, file " << fn << ", errno " << errno);
  }

  // obtain file size
  struct stat stat_buf{};
  if (fstat(fd, &stat_buf) < 0) {
    THROW("fstat failed, file " << fn << ", errno " << errno);
  }

  // map the file into memory
  char* addr = (char*) ::mmap(NULL, stat_buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    THROW("mmap failed, file " << fn << ", errno " << errno);
  }
  char* const end = addr + stat_buf.st_size;

  // read the file header
  auto result = parse_mmap_header(addr);
  size_t header_len = std::get<0>(result);
  json header_json = std::get<1>(result);
  addr += header_len;

  //size_t bytes_remaining = end - addr;

  // int msg_size = header_json["sz"].get<int>();
  // std::string msg_type = header_json["mt"].get<std::string>();

  // TODO: use a factory use?
  // Build a msg decoder

  if (stream_type == MdStream::AggTrades) {
    _decoder = std::make_unique<TickbinDecoderAggTrade>(addr, end);
  }
  else if (stream_type == MdStream::L1) {
    _decoder = std::make_unique<TickbinDecoderTickLevel1>(addr, end);
  }
  else {
    THROW("invalid stream_type: '" << stream_type << "'");
  }

  // give it the next bytes
}


TickbinFileReader::~TickbinFileReader() = default;

void TickbinFileReader::wind_forward(apex::Time t)
{
  size_t consumed = 0;
  apex::Time earliest_consumed;
  apex::Time latest_consumed;

  while (_decoder->has_next_event() && _decoder->get_next_event_time() < t) {
    auto next_event_time = _decoder->get_next_event_time();
    if (consumed == 0)
      earliest_consumed = next_event_time;
    else
      latest_consumed = next_event_time;
    _decoder->consume_next_event(0);
    consumed++;
  }

  if (consumed == 0) {
    LOG_DEBUG("wind-forward events consumed: "
              << consumed << "; next event time: "
              << _decoder->get_next_event_time() << ", seeking time: " << t);
  } else {
    LOG_INFO("wind-forward events consumed: "
             << consumed << "; from " << earliest_consumed << " upto "
             << latest_consumed << "; next event time: "
             << _decoder->get_next_event_time() << ", seeking time: " << t);
  }
}

[[nodiscard]] bool TickbinFileReader::has_next_event() const {
    return _decoder && _decoder->has_next_event();
}

[[nodiscard]] apex::Time TickbinFileReader::next_event_time() const {
  if (_decoder)
    return _decoder->get_next_event_time();
  else
    return Time{};
}

void TickbinFileReader::consume_next_event() {
    if (_decoder)
      _decoder->consume_next_event(_mktdata);
}


std::tuple<size_t, json> TickbinFileReader::parse_mmap_header(char* ptr)
{
  auto tickbin_header = decode_tickbin_file_header(ptr);

  auto meta = json::parse(ptr + TickbinHeader::header_lead_length,
                          ptr + tickbin_header.length);

  return {tickbin_header.length, meta};
}

} // namespace apex
