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

#include <apex/util/GzFile.hpp>

#include <zlib.h>

namespace apex {


struct GzFileImpl
{
  gzFile file = nullptr;
};

GzFile::GzFile()
: _handle(std::make_unique<GzFileImpl>()),
  _errno(0),
  _is_bad(false)
{
}


GzFile::~GzFile()
{
  this->close();
}


void GzFile::open(std::filesystem::path filename)
{
  if (_handle->file)
    this->close();

  _errno = 0;
  _is_bad = false;

  _handle->file = ::gzopen(filename.c_str() , "rb");

  if (!_handle->file) {
    _is_bad = true;
    _errno = errno;
  }
}


bool GzFile::is_open() const {
  return !! _handle->file;
}


size_t GzFile::read(char * buf, size_t len)
{
  if (!is_eof() && !is_bad()) {
    auto nread = ::gzread(_handle->file, buf, len);

    if (nread == 0) {
      _errno = errno;
      if (!is_eof())
        _is_bad = true;
    }

    return nread;
  }
  else {
    return 0;
  }
}


bool GzFile::is_eof() const
{
  return (_handle->file)? !!::gzeof(_handle->file) : true;
}


void GzFile::close()
{
  if (_handle->file) {
    ::gzclose(_handle->file);
    _handle->file = nullptr;
  }
}


}
