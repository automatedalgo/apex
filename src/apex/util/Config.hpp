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
#include <format>
#include <set>
#include <string>
#include <vector>
#include <map>

namespace apex
{

class ConfigError : public Error
{
public:
  explicit ConfigError(const std::string& what) : Error("", 0, what) {}
};

class ConfigParseError : public ConfigError {
public:
  ConfigParseError(std::string s):
    ConfigError(s) {};
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


/* Base class for classes that define a field that is part of config class T */
template<typename T>
struct FieldDef {
  std::string name;
  bool is_mandatory;

  FieldDef(std::string_view sv,
           bool is_mandatory)
    : name(sv),
      is_mandatory(is_mandatory)
  {  }
  virtual ~FieldDef() {}
  virtual void parse(T*, json&) = 0;
  virtual void set_default(T*) = 0;
  virtual json to_json(const T*) = 0;
};


/* Collection of FieldDef objects related to a config object T */
template<typename T>
struct ConfigSchema {
  using type = T;
  using field_schema = FieldDef<T>;
  std::vector< std::unique_ptr<field_schema> > fields;
};

template<typename T>
struct BaseConfigParser {
  using result_t = T;
  T result = {};
};

template<typename T>
struct ConfigParser : public BaseConfigParser<T>
{
  void parse(json j) {
    auto schema = T::schema();

    std::set<std::string> keys;
    for (auto& el : j.items())
      keys.insert(el.key());

    for (auto & field : schema.fields) {
      auto iter = j.find(field->name);
      if (iter != j.end()) {
        keys.erase(field->name);
        field->parse(&this->result, j[field->name]);
      }
      else {
        if (field->is_mandatory)
          throw ConfigParseError(
            std::format("config missing '{}'", field->name));
        else
          field->set_default(&this->result);
      }
    }

    if (!keys.empty())
      throw ConfigParseError(std::format("found unexpected config key '{}'",
                                         *keys.begin()));
  }
};

template<>
struct ConfigParser<int> : public BaseConfigParser<int> {
  void parse(json j) {
    this->result = j.get<int>();
  }
};

template<>
struct ConfigParser<unsigned int>: public BaseConfigParser<unsigned int> {
  void parse(json j) {
    this->result = j.get<unsigned int>();
  }
};

template<>
struct ConfigParser<long>: public BaseConfigParser<long> {
  void parse(json j) {
    this->result = j.get<long>();
  }
};

template<>
struct ConfigParser<unsigned long>: public BaseConfigParser<unsigned long> {
  void parse(json j) {
    this->result = j.get<unsigned long>();
  }
};

template<>
struct ConfigParser<double> : public BaseConfigParser<double> {
  void parse(json j) {
    this->result = j.get<double>();
  }
};

template<>
struct ConfigParser<float> : public BaseConfigParser<float> {
  void parse(json j) {
    this->result = j.get<float>();
  }
};

template<>
struct ConfigParser<bool> : public BaseConfigParser<bool> {
  void parse(json j) {
    this->result = j.get<bool>();
  }
};

template<>
struct ConfigParser<char> : public BaseConfigParser<char> {
  void parse(json j) {
    this->result = j.get<char>();
  }
};

template<>
struct ConfigParser<std::string> : public BaseConfigParser<std::string> {
  void parse(json j) {
    this->result = j.get<std::string>();
  }
};

template<typename U>
struct ConfigParser< std::vector<U> > : public BaseConfigParser<std::vector<U>> {
  void parse(json j) {
    if (!j.is_array())
      throw ConfigParseError(
        std::format("expected an array, but got {}", j.type_name()));

    for (size_t i = 0, N = j.size(); i < N; i++)
    {
      ConfigParser<U> parser;
      try {
        parser.parse(j[i]);
      }
      catch (std::exception & e) {
        throw ConfigParseError(
          std::format("failed for array index {}, {}", i, e.what()));
      }
      this->result.push_back(parser.result);
    }
  }
};

template<typename U>
struct ConfigParser< std::map<std::string, U> > : public BaseConfigParser<std::map<std::string, U>>{
  void parse(json j) {
    if (!j.is_object())
      throw ConfigParseError(
        std::format("expected an object, but got {}", j.type_name()));

    for (auto& [key, value] : j.items()) {
      ConfigParser<U> parser;
      try {
        parser.parse(value);
      }
      catch (std::exception& e) {
        throw ConfigParseError(
          std::format("failed for map key '{}', {}", key, e.what()));
      }
      this->result.insert({key, parser.result});
    }
  }
};

template<typename T>
struct ConfigWriter {
  json to_json(const T& obj) {
    json j;
    auto schema = T::schema();
    for (auto & field : schema.fields) {
      auto jval = field->to_json(&obj);
      j[ field->name ] = jval;
    }
    return j;
  }
};

template<>
struct ConfigWriter<std::string> {
  json to_json(const std::string& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<double> {
  json to_json(const double& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<float> {
  json to_json(const float& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<int> {
  json to_json(const int& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<unsigned int> {
  json to_json(const unsigned int& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<long> {
  json to_json(const long& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<unsigned long> {
  json to_json(const unsigned long& obj) {
    return json(obj);
  }
};


template<>
struct ConfigWriter<bool> {
  json to_json(const bool& obj) {
    return json(obj);
  }
};

template<>
struct ConfigWriter<char> {
  json to_json(const char& obj) {
    return json(obj);
  }
};

template<typename U>
struct ConfigWriter<std::vector<U>> {
  json to_json(const std::vector<U>& obj) {
    json j;
    ConfigWriter<U> streamer;
    for (auto & item : obj) {
      auto jval = streamer.to_json(item);
      j.push_back(jval);
    }
    return j;
  }
};

template<typename U>
struct ConfigWriter<std::map<std::string, U>> {
  json to_json(const std::map<std::string, U>& obj) {
    json j;
    ConfigWriter<U> streamer;
    for (auto & item : obj) {
      auto jval = streamer.to_json(item.second);
      j[ item.first ] = jval;
    }
    return j;
  }
};

template<typename T, typename U, typename P>
struct FieldDefImpl : public FieldDef<T>
{
  P member_ptr;
  U defval;

  FieldDefImpl(std::string_view sv,
               P member_ptr,
               U defval)
    : FieldDef<T>(sv, false),
      member_ptr(member_ptr),
      defval(std::move(defval))
  {
  }

  FieldDefImpl(std::string_view sv,
               P member_ptr)
    : FieldDef<T>(sv, true),
      member_ptr(member_ptr),
      defval{}
  {
  }

  json to_json(const T* target) override {
    ConfigWriter<U> streamer;
    return streamer.to_json(target ->* member_ptr);
  }

  void set_default(T* target) override {
    assert(this->is_mandatory == false);
    target ->* member_ptr = defval;
  }

  void parse(T* target, json& j) override {
    // assume the json object is the correct one, so now we have to
    // first parse, and then assign to a member variable.

    // so we have json.  We need to parse it.  To do that,
    // we need ConfigSchema for that target field.

    try {
      ConfigParser<U> parser;
      parser.parse(j);
      target ->* member_ptr = parser.result;
    }
    catch (std::exception& e) {
      throw ConfigParseError(
        std::format("config parse failed on '{}', {}", this->name, e.what()));
    }
  }
};

#define FIELD_DEF_INIT( Y )                     \
  using t_self = Y;                             \
  apex::ConfigSchema<t_self> schema;

#define FIELD_DEF_RETURN(  )                    \
  return schema;

#define FIELD_DEF_REQUIRED( X )                                         \
  {                                                                     \
    using MemberType = decltype( X );                                   \
    using PtrMemberType = decltype(& t_self:: X);                       \
    auto fdef                                                           \
      = std::make_unique<apex::FieldDefImpl<t_self, MemberType, PtrMemberType>>( \
        #X, & t_self::X);                                               \
    schema.fields.push_back( std::move(fdef) );                         \
  }

#define FIELD_DEF_OPTIONAL( X , Y)                                      \
  {                                                                     \
    using MemberType = decltype( X );                                   \
    using PtrMemberType = decltype(& t_self:: X);                       \
    auto fdef                                                           \
      = std::make_unique<apex::FieldDefImpl<t_self, MemberType, PtrMemberType>>( \
        #X, & t_self::X, Y);                                            \
    schema.fields.push_back( std::move(fdef) );                         \
  }

} // namespace apex
