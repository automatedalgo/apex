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

#include <apex/infra/SocketAddress.hpp>

#include <string.h>
#include <uv.h>

namespace apex
{

SocketAddress::SocketAddress() : _impl(new sockaddr_storage()) {}


SocketAddress::SocketAddress(const SocketAddress& other)
  : _impl(new sockaddr_storage(*(other._impl.get())))
{
}


SocketAddress::SocketAddress(SocketAddress&& other) noexcept
  : _impl() // none pointer held, but only until swapped
{
  this->swap(other);
}


SocketAddress::SocketAddress(const sockaddr_storage& other)
  : _impl(new sockaddr_storage(other))
{
}


SocketAddress::~SocketAddress() = default;


/* This assignment operator makes use of the copy constructor. */
SocketAddress& SocketAddress::operator=(SocketAddress other)
{
  this->swap(other);
  return *this;
}


void SocketAddress::swap(SocketAddress& other) { _impl.swap(other._impl); }


bool SocketAddress::operator==(const SocketAddress& other) const
{
  if (_impl.get() && other._impl.get())
    return (::memcmp(_impl.get(), other._impl.get(),
                     sizeof(sockaddr_storage)) == 0);
  else if ((_impl.get() == nullptr) && (other._impl.get() == nullptr))
    return true;
  else
    return false;
}


bool SocketAddress::operator!=(const SocketAddress& other) const
{
  return !(*this == other);
}


std::string SocketAddress::to_string() const
{
  sockaddr_storage* ss = _impl.get();
  if (ss == nullptr)
    return {};

  char text[64] = {}; // must be bigger ten IPv6 address (45 chars)

  if (ss->ss_family == AF_INET)
    uv_ip4_name((const struct sockaddr_in*)ss, text, sizeof text);
  else if (ss->ss_family == AF_INET6)
    uv_ip6_name((const struct sockaddr_in6*)ss, text, sizeof text);

  text[(sizeof text) - 1] = '\0';

  return text;
}

int SocketAddress::port() const
{
  ::sockaddr_storage* ss = _impl.get();
  if (ss == nullptr)
    return 0;

  if (ss->ss_family == AF_INET) {
    auto* addr = (::sockaddr_in*)ss;
    return ntohs(addr->sin_port);
  }

  if (ss->ss_family == AF_INET6) {
    auto* addr = (::sockaddr_in6*)ss;
    return ntohs(addr->sin6_port);
  }

  return 0;
}


} // namespace apex
