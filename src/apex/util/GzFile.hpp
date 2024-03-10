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

#include <filesystem>
#include <memory>

namespace apex {

/* Read a gzipped file. */

struct GzFileImpl;

class GzFile {
public:
  GzFile();
  ~GzFile();

  GzFile(const GzFile&) = delete;
  GzFile& operator=(const GzFile&) = delete;

  void open(std::filesystem::path filename);
  void close();

  size_t read(char * buf, size_t len);

  bool is_eof() const;
  bool is_open() const;
  bool is_bad() const { return _is_bad; }

  int last_errno() const { return _errno; }

private:
  std::unique_ptr<GzFileImpl> _handle; // pimpl pattern
  int _errno;
  bool _is_bad;
};


}
