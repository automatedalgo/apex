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

#include <apex/util/json.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/utils.hpp>
#include <apex/util/Error.hpp>

#include <fstream>

namespace apex
{

json read_json_config_file(const std::string& path)
{
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    throw std::runtime_error("failed to open config file '" + path + "'");
  }
  json j = json::parse(ifs,
                       /* callback */ nullptr,
                       /* allow exceptions */ true,
                       /* ignore_comments */ true);
  return j;
}

json read_json_file(const std::string& path)
{
  /* read the file */
  std::string file_bytes;
  file_bytes = slurp(path.c_str());

  /* convert to json */
  try {
    return json::parse(file_bytes);
  } catch (std::exception& e) {
    LOG_ERROR("exception while parsing tickdata file " << path);
    throw;
  }
}

const json::array_t& get_array(const json& msg, const std::string& key)
{
  if (not msg.is_object())
    throw std::runtime_error("json value is not an object");
  auto iter = msg.find(key);
  if (iter == std::end(msg))
    THROW("missing field " << QUOTE(key));
  if (not iter->is_array())
    throw std::runtime_error("expected value to be an array");
  return iter->get_ref<const json::array_t&>();
}


const json::object_t& get_object(const json& msg, const std::string& key)
{
  if (not msg.is_object())
    throw std::runtime_error("json value is not an object");
  auto iter = msg.find(key);
  if (iter == std::end(msg))
    THROW("missing field " << QUOTE(key));
  if (not iter->is_object())
    throw std::runtime_error("expected value to be an object");
  return iter->get_ref<const json::object_t&>();
}


json::number_unsigned_t get_uint(const json& msg, const std::string& key)
{
  if (not msg.is_object())
    throw std::runtime_error("json value is not an object");
  auto iter = msg.find(key);
  if (iter == std::end(msg))
    THROW("missing field " << QUOTE(key));
  if (not iter->is_number_unsigned())
    throw std::runtime_error("field value is not an uint");
  return iter->get<json::number_unsigned_t>();
}


bool get_bool(const json& msg, const std::string& key)
{
  if (not msg.is_object())
    throw std::runtime_error("json value is not an object");
  auto iter = msg.find(key);
  if (iter == std::end(msg))
    THROW("missing field " << QUOTE(key));
  if (not iter->is_boolean())
    throw std::runtime_error("field value is not a bool");
  return iter->get<bool>();
}


double get_double_field(const json& msg, const std::string& key)
{
  if (not msg.is_object())
    throw std::runtime_error("json value is not an object");

  auto iter = msg.find(key);
  if (iter == std::end(msg))
    throw std::runtime_error("field not found");

  if (!iter->is_number_float())
    throw std::runtime_error("field not double type");

  return iter->get<double>();
}


const std::string& get_string_field(const json& msg, const std::string& key)
{
  auto iter = msg.find(key);
  if (iter == std::end(msg)) {
    THROW_PARSE_ERROR("field not found '" << key << "'");
  }

  if (!iter->is_string())
    THROW_PARSE_ERROR("wrong field type; field '"
                      << key << "'; expected 'string', actual '"
                      << iter->type_name() << "'");

  return iter->get_ref<const std::string&>();
}


std::string json_describe_type(const json& j)
{
  std::ostringstream oss;

  if (j.is_primitive())
    oss << "(primitive)";
  if (j.is_structured())
    oss << "(structured)";
  if (j.is_null())
    oss << "(none)";
  if (j.is_boolean())
    oss << "(boolean)";
  if (j.is_number())
    oss << "(number)";
  if (j.is_number_integer())
    oss << "(integer)";
  if (j.is_number_unsigned())
    oss << "(unsigned)";
  if (j.is_number_float())
    oss << "(float)";
  if (j.is_object())
    oss << "(object)";
  if (j.is_array())
    oss << "(array)";
  if (j.is_string())
    oss << "(string)";
  if (j.is_binary())
    oss << "(binary)";

  return oss.str();
}

} // namespace apex
