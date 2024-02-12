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

// Pull in JSON library, plus utilities

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#include <string>

#include <sstream>


/* json parsing utils */

namespace apex
{

#define THROW_PARSE_ERROR(X)                                                   \
  do {                                                                         \
    std::ostringstream oss;                                                    \
    oss << X;                                                                  \
    throw apex::parse_error(oss.str());                                         \
  } while (false)


class parse_error : public std::runtime_error
{
public:
  parse_error(std::string s) : std::runtime_error(std::move(s)) {}
};


/* Return a string representation of the json value, suitable *only* for logging
 * & display.  This uses the usual JSON serialisation format, exception for
 * strings; for strings the quotes will be removed. */
inline std::string to_string(const json& value)
{
  if (value.is_string())
    return value.get<std::string>();
  else {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  }
}

/* Lookup in `msg` which is expected to be a JSON Object for provided key. */

const json::array_t& get_array(const json& msg, const std::string& key);

const json::object_t& get_object(const json& msg, const std::string& key);

double get_double_field(const json& msg, const std::string& key);

const std::string& get_string_field(const json& msg, const std::string& key);

json::number_unsigned_t get_uint(const json& msg, const std::string& key);

bool get_bool(const json& msg, const std::string& key);



template <typename T> const T* get_ptr(const json& src, const char* fieldname)
{
  const T* rv = nullptr;
  auto iter = src.find(fieldname);
  if (iter != std::end(src)) {
    rv = iter->get_ptr<decltype(rv)>();
  }
  return rv;
}

std::string json_describe_type(const json&);


json read_json_file(const std::string& path);

json read_json_config_file(const std::string& path);

} // namespace apex
