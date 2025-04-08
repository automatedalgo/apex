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

#include <apex/backtest/TickFileWriter.hpp>
#include <apex/backtest/TickbinMsgs.hpp>
#include <apex/util/Error.hpp>
#include <apex/util/json.hpp>
#include <apex/util/utils.hpp>

#include <unistd.h>
#include <vector>
#include <variant>
#include <fstream>
#include <iostream>

using namespace apex;


json read_meta(const std::vector<char> & bytes, size_t meta_len) {
  std::string raw{&bytes[16], meta_len-16};
  auto meta = json::parse(raw);
  return meta;
}

// TODO: this code should try to reuse some of the tick-bin loading routines
// from TickFileReader.


int main(int argc, char** argv)
{
  try {
    if (argc != 2) {
      THROW("provide name of tickbin file");
    }
    auto fn = argv[1];

    auto file_len = std::filesystem::file_size(fn);

    if (file_len < TickbinHeader::header_lead_length) {
      throw std::runtime_error("tickbin file has incomplete file header");
    }

    char header_lead_buf[TickbinHeader::header_lead_length] = {0};

    std::ifstream f(fn, std::ios::binary);
    f.read(reinterpret_cast<char*>(header_lead_buf), sizeof(header_lead_buf));

    auto tickbin_header = decode_tickbin_file_header(header_lead_buf);

    std::cout << "version[" << tickbin_header.version << "]\n";
    std::cout << "length[" << tickbin_header.length << "]\n";

    // now read the entire preamble into memory
    std::vector<char> preamble(tickbin_header.length, '\0');
    memcpy(&preamble[0], header_lead_buf, sizeof(header_lead_buf));
    auto remaining_to_read = tickbin_header.length - TickbinHeader::header_lead_length;
    std::cout << "reading next " << remaining_to_read  << " bytes\n";
    f.read(reinterpret_cast<char*>(&preamble[TickbinHeader::header_lead_length]),
           remaining_to_read);

    auto meta = read_meta(preamble, tickbin_header.length);
    auto stream_type = get_string_field(meta, "c");
    auto exchange = get_string_field(meta, "e");
    auto symbol = get_string_field(meta, "s");
    auto inst_id = get_string_field(meta, "i");
    std::cout << "format: " << tickbin_header.version << "\n"
              << "exchange: " << exchange << "\n"
              << "symbol: " << symbol << "\n"
              << "inst_id: " << inst_id << "\n"
              << "stream: " << stream_type << "\n";

    std::cout << "rawjson[" << meta << "]\n";

    MarketData md;
    MdStream md_stream = apex::MdStream::AggTrades;
    TickbinFileReader reader{fn, &md, md_stream};

    md.subscribe_events([&](MarketData::EventType et){
      if (et.is_trade()) {
        auto last = md.last();
        std::cout << "i " << inst_id << "; " << "mt trade; et " << last.et
                  << "; tp " << format_double(last.price, true)
                  << "; ts " << format_double(last.qty, true) << "\n";
      }
      if (et.is_top()) {
        std::cout << "got TOP event\n";
      }
    });

    while (reader.has_next_event()) {
      reader.consume_next_event();
    }
  }
  catch (std::exception& e) {
    std::cout << "error: " << e.what() << std::endl;
  }

  return 1;
}
