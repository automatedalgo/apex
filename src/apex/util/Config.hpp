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

#include <apex/util/json.hpp>
#include <apex/util/Error.hpp>

#include <stdexcept>

namespace apex
{

class ConfigError : public Error
{
public:
  explicit ConfigError(const std::string& what) : Error("", 0, what) {}
};

class MissingFieldConfigError : public ConfigError
{
public:
  explicit MissingFieldConfigError(const std::string& what) : ConfigError(what) {}
};


/* Represent applications configuration, which is essentially just a
 * utility wrapper around a JSON instance. */
class Config
{

public:
  explicit Config(json raw = {}, std::string path = "")
    : _raw(std::move(raw)), _path(std::move(path))
  {
  }

  static Config empty_config();

  bool get_bool(const std::string& field);

  Config get_sub_config(json::const_iterator iter,
                        const std::string& field);

  bool get_bool(const std::string& field, bool default_value);

  Config get_sub_config(const std::string& field);
  Config get_sub_config(const std::string& field, Config default_value);

  [[nodiscard]] size_t array_size() const;
  Config array_item(size_t i);
  std::string get_string(size_t i);

  std::string get_string(const std::string& field);
  std::string get_string(const std::string& field,
                         const std::string& default_value);

  uint64_t get_uint(const std::string& field);
  uint64_t get_uint(const std::string& field, uint64_t default_value);

  void dump();

  const std::string& path() { return _path; }

  [[nodiscard]] bool is_array() const;
  [[nodiscard]] bool is_empty() const;

private:
  [[nodiscard]] json::const_iterator find_field(const std::string& field) const;
  void check_this_is_array() const;
  void check_this_is_object(const std::string& field) const;
  json _raw;
  std::string _path;
};

} // namespace apex
