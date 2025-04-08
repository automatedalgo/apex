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

#include <apex/util/utils.hpp>

#include <websocketpp/extensions/permessage_deflate/disabled.hpp>
#include <websocketpp/http/request.hpp>
#include <websocketpp/http/response.hpp>
#include <websocketpp/message_buffer/alloc.hpp>
#include <websocketpp/message_buffer/message.hpp>
#include <websocketpp/processors/hybi13.hpp>
#include <websocketpp/random/none.hpp>

namespace apex
{

/* Config class used with websocketpp, so that we can define the types which
 * websocketpp will use. */
struct WebsocketConfig {
  typedef websocketpp::http::parser::request request_type;
  typedef websocketpp::http::parser::response response_type;
  typedef websocketpp::message_buffer::message<
      websocketpp::message_buffer::alloc::con_msg_manager>
      message_type;
  typedef websocketpp::message_buffer::alloc::con_msg_manager<message_type>
      con_msg_manager_type;

  typedef websocketpp::random::none::int_generator<uint32_t> rng_type;

  /// permessage_compress extension
  struct permessage_deflate_config {
    typedef websocketpp::http::parser::request request_type;

    /// If the remote endpoint requests that we reset the compression
    /// context after each message should we honor the request?
    static const bool allow_disabling_context_takeover = true;

    /// If the remote endpoint requests that we reduce the size of the
    /// LZ77 sliding window size this is the lowest value that will be
    /// allowed. Values range from 8 to 15. A value of 8 means we will
    /// allow any possible window size. A value of 15 means do not allow
    /// negotiation of the window size (ie require the default).
    static const uint8_t minimum_outgoing_window_bits = 8;
  };

  typedef websocketpp::extensions::permessage_deflate::disabled<
      permessage_deflate_config>
      permessage_deflate_type;
  /// Default maximum message size
  /**
   * Default value for the processor's maximum message size. Maximum message
   * size determines the point at which the library will fail a connection with
   * the· message_too_big protocol error.
   *
   * The default is 32MB
   *
   * @since 0.3.0
   */
  static const size_t max_message_size = 32000000;

  /// Global flag for enabling/disabling extensions
  static const bool enable_extensions = true;
};

class WebsocketppImpl
{
public:
  WebsocketppImpl(connect_mode mode)
    : _msg_manager(new WebsocketConfig::con_msg_manager_type)
  {
    m_proc.reset(new websocketpp::processor::hybi13<WebsocketConfig>(
        false, mode == connect_mode::accept, _msg_manager, _rng_mgr));
  }

  websocketpp::processor::processor<WebsocketConfig>* processor()
  {
    return m_proc.get();
  }

  websocketpp::processor::hybi13<WebsocketConfig>::msg_manager_ptr&
  msg_manager()
  {
    return _msg_manager;
  }


  /* Get the frame details of the a message as a string, for logging. */
  static std::string frame_to_string(const WebsocketConfig::message_type::ptr&);

private:
  WebsocketConfig::rng_type _rng_mgr;
  websocketpp::processor::hybi13<WebsocketConfig>::msg_manager_ptr _msg_manager;
  std::unique_ptr<websocketpp::processor::processor<WebsocketConfig>> m_proc;
};

} // namespace apex
