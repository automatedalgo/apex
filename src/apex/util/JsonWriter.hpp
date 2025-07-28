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

#include <charconv>
#include <string.h>


namespace apex
{

template<int N = 2048>
class JsonWriter
{
public:

  JsonWriter() : _p(_buf) {
    _buf[0] = '\0';
  }


  void write_object_start() {
    write_char('{');
  }


  void write_object_end() {
    if ( (_p > _buf) && (*(_p-1) == ','))
      *(_p-1) = '}';
    else {
      write_char('}');
    }
  }


  void write_field(std::string_view fn) {
    write_char('"');
    memcpy(_p, fn.data(), fn.size());
    _p += fn.size();
    write_char('"');
    write_char(':');
  }


  void write_field(std::string_view fn, std::string_view v) {
    write_char('"');
    memcpy(_p, fn.data(), fn.size());
    _p += fn.size();
    write_char('"');
    write_char(':');
    write_char('"');
    memcpy(_p, v.data(), v.size());
    _p += v.size();
    write_char('"');
    write_char(',');
  }


  void write_value(std::string_view v) {
    write_char('"');
    memcpy(_p, v.data(), v.size());
    _p += v.size();
    write_char('"');
    write_char(',');
  }


  void write_value_long(long int v)
  {
    char tmp[32]={};
    auto [ptr, ec] = std::to_chars(tmp, tmp + sizeof(tmp)-1, v);
    *ptr = '\0';

    auto tmp_len = ::strlen(tmp);
    memcpy(_p, tmp, tmp_len);
    _p += tmp_len;
    write_char(',');
  }


  void write_value_double(double d, int precision=8)
  {
    char tmp[32]={};
    int written = snprintf(tmp, sizeof(tmp) - 1, "%.*f", precision, d);
    if (written > 0) // 50.02000###
      for (int i = written + 1;
           i > 0 && ((tmp[i] == '0' && tmp[i - 1] != '.') || tmp[i] == 0);
           i--)
        tmp[i] = 0;
    auto tmp_len = ::strlen(tmp);
    memcpy(_p, tmp, tmp_len);
    _p += tmp_len;
    write_char(',');
  }


  const char* c_str() const {
    *_p = '\0';
    return _buf;
  }


  char* c_str()  {
    *_p = '\0';
    return _buf;
  }


  size_t size() const {
    return _p - _buf;
  }


  operator std::string_view() const {
    return std::string_view(c_str(), size());
  }


private:
  inline void write_char(char c) {
    *_p++ = c;
  }

  char _buf[N];
  char* _p;
};


template<typename T>
class JsonWriterObject {
public:
  JsonWriterObject(T & jw) : _jw(jw) {
    _jw.write_object_start();
  }
  ~JsonWriterObject(){
    _jw.write_object_end();
  }
private:
  T & _jw;
};

} // namespace
