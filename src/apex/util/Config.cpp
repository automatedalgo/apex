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

#include <apex/core/Logger.hpp>
#include <apex/util/Config.hpp>

#include <list>
#include <regex>

namespace apex
{

std::string get_env(const std::string& name)
{
  auto result = ::getenv(name.c_str());
  if (result)
    return result;
  else
    return "";
}


std::string interpolate_string(const std::string& s)
{
  static std::regex re("\\$\\{[a-zA-Z0-9_]*\\}");
  std::smatch sm{};
  std::list<std::string> parts;
  auto curr = s.begin();

  while (std::regex_search(curr, s.cend(), sm, re)) {
    if (sm.position())
      parts.push_back(sm.prefix());

    // resolve the environment variable
    auto env_name = sm.str().substr(2, sm.length() - 3);
    parts.push_back(get_env(env_name));

    // continue to parse the remaining string
    curr = sm.suffix().first;
  }

  // handle any trailing string portion
  if (curr != s.end())
    parts.push_back(curr.base());

  // concatenate all the parts
  std::ostringstream oss;
  std::copy(parts.begin(), parts.end(),
            std::ostream_iterator<std::string>(oss));
  return oss.str();
}

Config Config::get_sub_config(json::const_iterator iter,
                              const std::string& field)
{
  if (iter->is_array()) {
    return Config(iter->get<json::array_t>(), _path + "." + field);
  }
  if (iter->is_object()) {
    return Config(iter->get<json::object_t>(), _path + "." + field);
  }

  std::ostringstream oss;
  oss << "field not of type json-object or json-array, " << QUOTE(field);
  throw ConfigError(oss.str());
}


Config Config::get_sub_config(const std::string& field)
{
  auto iter = find_field(field);
  return get_sub_config(iter, field);
}

Config Config::get_sub_config(const std::string& field, Config default_value)
{
  auto iter = _raw.find(field);
  if (iter != _raw.end()) {
    return get_sub_config(iter, field);
  }
  else  {
    return default_value;
  }
}

bool Config::is_empty() const { return _raw.empty(); }

bool Config::is_array() const { return _raw.is_array(); }

Config Config::array_item(size_t i)
{
  check_this_is_array();
  std::ostringstream os;
  os << _path << "." << i;
  return Config{_raw[i], os.str()};
}


size_t Config::array_size() const
{
  check_this_is_array();
  return _raw.size();
}


bool Config::get_bool(const std::string& field)
{
  check_this_is_object(field);

  try {
    return find_field(field)->get<json::boolean_t>();
  } catch (const json::exception&) {
    std::ostringstream oss;
    oss << "field not of type boolean, " << QUOTE(field);
    throw ConfigError(oss.str());
  };
}


bool Config::get_bool(const std::string& field, bool default_value)
{
  check_this_is_object(field);
  auto iter = _raw.find(field);
  if ( iter != _raw.end()) {
    try {
      bool val = iter->get<json::boolean_t>();
      return val;
    }
    catch (const json::exception&) {
      std::ostringstream oss;
      oss << "field not of type string, " << QUOTE(field);
      throw ConfigError(oss.str());
    }
  }
  else {
    return default_value;
  }
}


std::string Config::get_string(size_t i)
{
  check_this_is_array();
  try {
    auto& item = _raw[i];
    return interpolate_string(item.get<json::string_t>());
  } catch (const json::exception&) {
    std::ostringstream oss;
    oss << "array item not of type string, i=" << i;
    throw ConfigError(oss.str());
  };
}


std::string Config::get_string(const std::string& field)
{
  check_this_is_object(field);

  try {
    const auto& field_ptr = find_field(field);
    return interpolate_string(field_ptr->get<json::string_t>());
  } catch (const json::exception&) {
    std::ostringstream oss;
    oss << "field not of type string, " << QUOTE(field);
    throw ConfigError(oss.str());
  };
}


std::string Config::get_string(const std::string& field,
                               const std::string& default_value)
{
  check_this_is_object(field);
  auto iter = _raw.find(field);
  if (iter != _raw.end()) {
    try {
      std::string val = iter->get<json::string_t>();
      return interpolate_string(val);
    }
    catch (const json::exception&) {
      std::ostringstream oss;
      oss << "field not of type string, " << QUOTE(field);
      throw ConfigError(oss.str());
    }
  }
  else {
    return default_value;
  }
}

uint64_t Config::get_uint(const std::string& field)
{
  check_this_is_object(field);

  try {
    return find_field(field)->get<json::number_unsigned_t>();
  } catch (const json::exception&) {
    std::ostringstream oss;
    oss << "field not of type unsigned-number, " << QUOTE(field);
    throw ConfigError(oss.str());
  };
}


uint64_t Config::get_uint(const std::string& field,
                          uint64_t default_value)
{
  check_this_is_object(field);
  auto iter = _raw.find(field);
  if (iter != _raw.end()) {
    try {
      return iter->get<json::number_unsigned_t>();
    }
    catch (const json::exception&) {
      std::ostringstream oss;
      oss << "field not of type unsigned-number, " << QUOTE(field);
      throw ConfigError(oss.str());
    }
  }
  else {
    return default_value;
  }
}


json::const_iterator Config::find_field(const std::string& field) const
{
  auto iter = _raw.find(field);
  if (iter == _raw.end()) {
    std::ostringstream oss;
    oss << "field not found, " << QUOTE(field);
    throw MissingFieldConfigError(oss.str());
  } else
    return iter;
}


void Config::check_this_is_array() const
{
  if (!_raw.is_array()) {
    std::ostringstream oss;
    oss << "required json array";
    throw ConfigError(oss.str());
  }
}


void Config::check_this_is_object(const std::string& field) const
{
  if (!_raw.is_object()) {
    std::ostringstream oss;
    oss << "required json object when getting field " << QUOTE(field);
    throw ConfigError(oss.str());
  }
}

void Config::dump() { LOG_INFO("config: " << to_string(_raw)); }


Config Config::empty_config() { return Config(json::object()); }
} // namespace apex
