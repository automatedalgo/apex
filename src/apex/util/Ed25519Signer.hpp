/* Copyright 2026 Automated Algo (www.automatedalgo.com)

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

#include <cstdint>
#include <array>
#include <string>

namespace apex
{

class Ed25519Signer {
public:
  static constexpr int secret_key_bytes_len = 64;
  static constexpr int signature_bytes_len = 64;

  struct Signature {
    std::array<uint8_t, signature_bytes_len> bytes = {};
    [[nodiscard]] std::string to_hex() const;
    [[nodiscard]] std::string to_base64() const;
  };

  Ed25519Signer() = default;
  explicit Ed25519Signer(std::string_view private_key_hex);

  ~Ed25519Signer();

  void set_private_key_hex(std::string_view private_key_hex);

  [[nodiscard]] Signature sign_detached(std::string_view payload) const;

private:
  std::array<uint8_t, secret_key_bytes_len> _secret = {};
};



} // namespace apex
