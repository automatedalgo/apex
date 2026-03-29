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

#include <apex/util/utils.hpp>
#include <apex/util/Ed25519Signer.hpp>

#include <apache/base64.h> // from 3rdparty
#include <sodium.h>

namespace apex
{

static_assert(Ed25519Signer::secret_key_bytes_len == crypto_sign_SECRETKEYBYTES);
static_assert(Ed25519Signer::signature_bytes_len == crypto_sign_BYTES);

std::string Ed25519Signer::Signature::to_hex() const
{
  return apex::to_hex(bytes.data(), bytes.size());
}


std::string Ed25519Signer::Signature::to_base64() const
{
  std::string base64(base64_encoded_size(bytes.size()), 0);
  ap_base64encode(base64.data(),
                  reinterpret_cast<const char*>(bytes.data()),
                  static_cast<int>(bytes.size()));
  return base64;
}


Ed25519Signer::Ed25519Signer(const std::string_view private_key_hex) {
  set_private_key_hex(private_key_hex);
}


void Ed25519Signer::set_private_key_hex(const std::string_view private_key_hex) {
  if (private_key_hex.size() % 2 != 0)
    throw std::runtime_error("hex private-key should have length modulo 2");

  size_t bin_len = 0;
  if (sodium_hex2bin(
        _secret.data(), _secret.size(),
        private_key_hex.data(), private_key_hex.size(),
        nullptr, &bin_len, nullptr) != 0) {
    throw std::runtime_error("failed to decode hex private key");
  }

  if (bin_len != crypto_sign_SECRETKEYBYTES) {
    throw std::runtime_error("private key length mismatch");
  }
}


Ed25519Signer::~Ed25519Signer() {
  _secret.fill(0);
}


Ed25519Signer::Signature Ed25519Signer::sign_detached(const std::string_view payload) const
{
  Signature sig;
  unsigned long long sig_len;

  if (crypto_sign_detached(
        sig.bytes.data(),
        &sig_len,
        reinterpret_cast<const unsigned char*>(payload.data()),
        payload.size(),
        _secret.data()) != 0) {
    throw std::runtime_error("crypto signing failed");
  }

  return sig;
}

} // namespace apex
