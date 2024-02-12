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

#include <memory>
#include <string>

// from sockets API
struct sockaddr_storage;

namespace apex
{

class TcpSocket;

/** Socket address, essentially a wrapper around the socket API's
 * sockaddr_storage structure, with some utility methods provided. */
class SocketAddress
{
public:
  SocketAddress();
  SocketAddress(const SocketAddress&);
  SocketAddress(SocketAddress&&) noexcept;

  explicit SocketAddress(const sockaddr_storage&);

  ~SocketAddress();

  SocketAddress& operator=(SocketAddress);

  bool operator==(const SocketAddress&) const;
  bool operator!=(const SocketAddress&) const;

  /** String representation of the address. */
  [[nodiscard]] std::string to_string() const;

  /** Return the local port. */
  [[nodiscard]] int port() const;

  void swap(SocketAddress&);

private:
  using impl_type = std::unique_ptr<sockaddr_storage>;
  impl_type _impl;
  friend class TcpSocket;
};

} // namespace apex
