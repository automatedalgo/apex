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

#include <apex/backtest/TickFileReader.hpp>

namespace apex
{

class TickbinFileWriter
{
public:
  TickbinFileWriter(TickFileBucketId bucketid,
                    std::filesystem::path dirname,
                    std::filesystem::path filename,
                    StreamInfo stream_info,
                    json collect_meta = {});

  ~TickbinFileWriter();

  [[nodiscard]] const TickFileBucketId& bucketid() const { return _bucketid; };

  void write_bytes(char* buf, size_t size);

  [[nodiscard]] std::filesystem::path full_path() const { return _dirname/_filename; }

private:
  TickFileBucketId _bucketid;
  std::filesystem::path _dirname;
  std::filesystem::path _filename;
  std::unique_ptr<std::ofstream> _ostream;
};


} // namespace apex
