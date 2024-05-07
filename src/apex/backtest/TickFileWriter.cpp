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
#include <apex/core/Logger.hpp>

#include <fstream>

namespace fs = std::filesystem;

namespace apex
{

TickbinFileWriter::TickbinFileWriter(
  TickFileBucketId bucketid,
  std::filesystem::path dirname,
  std::filesystem::path filename,
  StreamInfo stream_info,
  json collect_meta)
  : _bucketid(bucketid),
    _dirname(std::move(dirname)),
    _filename(std::move(filename)
      ) {
  std::filesystem::path dir = _dirname;
  if (!std::filesystem::exists(full_path())) {
    std::error_code err;
    auto created = fs::create_directories(dir, err);
    if (err) {
      LOG_WARN("failed to create tickdata directory " << dir << ", error " << err);
      throw std::system_error(err, "cannot create tickdata directory");
    }
    if (created) {
      LOG_INFO("created tickdata directory "<< dir << "");
    }

    // generate meta-data information
    json meta;
    meta["e"] = exchange_id_to_string(stream_info.exchange_id());
    meta["c"] = stream_info.channel;
    meta["s"] = stream_info.symbol();
    meta["i"] = stream_info.instrument.id();
    meta["bin"] = bucketid.as_string();
    meta["cm"] = std::move(collect_meta);
    auto meta_str = to_string(meta);
    auto head_plus_meta_len = 8 + meta_str.size() + 1;  // +1 for null term

    // Decide the preamble block size. This has to be large enough to accomodate
    // the JSON meta-data, but also we want it be sympathetic to later memory
    // mapping; so we ensure we allocate header space in 1024 byte blocks.

    size_t preamble_size = (1 + (head_plus_meta_len  >> 10)) << 10;
    assert (head_plus_meta_len < preamble_size);

    // --- Build the binary image for the preamble ---
    std::string tick_version = "TICK1   ";
    assert(tick_version.size() == 8);
    std::vector<char> preamble(preamble_size, '\0');

    // write the tick header version
    strcpy(&preamble[0], tick_version.c_str());

    // write the preamble region size
    snprintf(&preamble[8], 8, "%07lu", preamble_size);

    // write the json meta data
    strncpy(&preamble[16], meta_str.c_str(), 1024 - 16);

    // we should not have overritten the final preamble byte
    assert(preamble[preamble_size-1] == '\0');

    // --- Write to file
    LOG_INFO("creating tick-bin file: " << full_path());
    auto file = std::ofstream(full_path(), std::ios::binary);
    file.write(preamble.data(), preamble_size);
    file.close();
  }

  _ostream = std::make_unique<std::ofstream>(full_path(), std::ios::binary | std::ios::app);
}

TickbinFileWriter::~TickbinFileWriter()
{
  _ostream->close();
}


void TickbinFileWriter::write_bytes(char* buf, size_t size) {
  if (_ostream->good())
    _ostream->write(buf, size);
}




} // namespace apex
