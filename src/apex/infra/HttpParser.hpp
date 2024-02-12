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

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

// types brought in from nodejs http-parser project (these are in the global
// namespace)
struct http_parser;
struct http_parser_settings;

namespace apex
{

class HttpParser
{
public:
  static constexpr unsigned int status_code_switching_protocols = 101;

  enum parser_type { e_http_request, e_http_response };

  static bool is_http_get(const char* s, size_t n)
  {
    return n > 3 && s[0] == 'G' && s[1] == 'E' && s[2] == 'T' && isspace(s[3]);
  }

  explicit HttpParser(parser_type);
  ~HttpParser();

  static constexpr const unsigned char HEADER_SIZE = 4; /* "GET " */

  size_t handle_input(char* const, size_t const);

  /** have we completed parsing headers? */
  [[nodiscard]] bool is_complete() const { return m_state == eComplete; }

  /** does header indicate connection upgrade? */
  [[nodiscard]] bool is_upgrade() const;

  /** access the http-parser error code (see nodejs|http_parser.h for codes) */
  [[nodiscard]] unsigned int error() const;

  /** return string associated with any error */
  [[nodiscard]] std::string error_text() const;

  /** does http-parser error indicate success? */
  [[nodiscard]] bool is_good() const;

  /** is field present in headers? field should be lowercase */
  bool has(const char* s) const { return m_headers.find(s) != m_headers.end(); }

  /** return header field, otherwise throw; field should be lowercase */
  [[nodiscard]] const std::string& get(const std::string& field) const
  {
    auto it = m_headers.find(field);
    if (it != m_headers.end())
      return it->second;
    else
      throw std::runtime_error("http header field not found");
  }

  /* HTTP response status-line textual phrase */
  [[nodiscard]] const std::string& http_status_phrase() const { return _http_status; }

  /* HTTP response status-line code */
  [[nodiscard]] unsigned int http_status_code() const { return _http_status_code; }

  [[nodiscard]] const std::string& url() const { return m_url; }

private:
  void store_current_header_field();
  int on_headers_complete();
  int on_url(const char* s, size_t n);
  int on_header_field(const char* s, size_t n);
  int on_header_value(const char* s, size_t n);
  int on_status(const char* s, size_t n);

  std::map<std::string, std::string> m_headers;
  std::string m_url;

  std::unique_ptr<::http_parser_settings> m_settings;
  std::unique_ptr<::http_parser> m_parser;

  enum state { eParsingField = 0, eParsingValue, eComplete };
  state m_state = eParsingField;

  std::string _current_field;
  std::string _current_value;

  unsigned int _http_status_code;
  std::string _http_status;
};
} // namespace apex
