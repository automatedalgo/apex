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

#include <apex/infra/HttpParser.hpp>

#include <apex/util/utils.hpp>

#include <http_parser/HttpParser.h> /* nodejs http parser, from 3rdparty */

#include <cctype>
#include <iostream>
#include <string.h>


namespace apex
{

unsigned int HttpParser::error() const { return m_parser->http_errno; }

bool HttpParser::is_good() const { return error() == HPE_OK; }

bool HttpParser::is_upgrade() const { return m_parser->upgrade != 0; }

std::string HttpParser::error_text() const
{
  return std::string(
      ::http_errno_description((enum ::http_errno)m_parser->http_errno));
}


HttpParser::HttpParser(parser_type pt)
  : m_settings(new ::http_parser_settings),
    m_parser(new ::http_parser),
    _http_status_code(0)
{
  ::http_parser_settings_init(m_settings.get());

  if (pt == e_http_request)
    ::http_parser_init(m_parser.get(), HTTP_REQUEST);
  else if (pt == e_http_response)
    ::http_parser_init(m_parser.get(), HTTP_RESPONSE);

  m_parser->data = this;

  // set up the callbacks, using lambdas without captures, so that these lambdas
  // can be assigned to function pointers.

  m_settings->on_headers_complete = [](::http_parser* p) {
    auto hp = (apex::HttpParser*)p->data;
    return hp->on_headers_complete();
  };

  m_settings->on_url = [](::http_parser* p, const char* s, size_t n) {
    auto hp = (apex::HttpParser*)p->data;
    return hp->on_url(s, n);
  };

  m_settings->on_header_field = [](::http_parser* p, const char* s, size_t n) {
    auto hp = (apex::HttpParser*)p->data;
    return hp->on_header_field(s, n);
  };

  m_settings->on_header_value = [](::http_parser* p, const char* s, size_t n) {
    auto hp = (apex::HttpParser*)p->data;
    return hp->on_header_value(s, n);
  };

  m_settings->on_status = [](::http_parser* p, const char* s, size_t n) {
    auto hp = (apex::HttpParser*)p->data;
    return hp->on_status(s, n);
  };
}


HttpParser::~HttpParser()
{
  /* Need destructor here, even empty, so that when the unique_ptr destructor
   * for http_parser_settings is instantiated it can see the definition of
   * http_parser_settings. */
}

void HttpParser::store_current_header_field()
{
  if (!_current_field.empty()) {
    // convert the header to lower case
    for (auto& item : _current_field)
      item = std::tolower(item);

    /* It's possible that a HTTP header field might be duplicated, in which case
     * we combine the values together in a comma separated list. */
    auto it = m_headers.find(_current_field);
    if (it != m_headers.end()) {
      it->second.append(",");
      it->second.append(std::move(_current_value));
    } else {
      m_headers.insert({_current_field, _current_value});
    }
  }

  _current_field.clear();
  _current_value.clear();
}


int HttpParser::on_url(const char* s, size_t n)
{
  m_url = std::string(s, n);
  return 0;
}


int HttpParser::on_header_field(const char* s, size_t n)
{
  if (m_state == eParsingField) {
    _current_field += std::string(s, n);
  } else {
    store_current_header_field();
    _current_field = std::string(s, n);
    m_state = eParsingField;
  }

  return 0;
}


int HttpParser::on_header_value(const char* s, size_t n)
{
  if (m_state == eParsingField) {
    _current_value = std::string(s, n);
    m_state = eParsingValue;
  } else {
    _current_value += std::string(s, n);
  }

  return 0;
}


size_t HttpParser::handle_input(char* const data, size_t const len)
{
  if (m_state != eComplete)
    return ::http_parser_execute(m_parser.get(), m_settings.get(), data, len);
  else
    throw std::runtime_error("http parse already complete");
}


int HttpParser::on_headers_complete()
{
  store_current_header_field();
  m_state = eComplete;
  _http_status_code = m_parser->status_code;
  return HPE_OK;
}


int HttpParser::on_status(const char* s, size_t n)
{
  _http_status += std::string(s, n);
  return 0;
}

} // namespace apex
