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

#include <functional>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <string.h>

namespace apex
{

struct SslConfig {
  /* Must be set to true to indicate SSL should be set up. */
  bool enable;

  /* SSL security level */
  int security_level = 3;

  /* For SSL in server mode, both certificate and private key files must be
   * provided. */
  std::string certificate_file;
  std::string private_key_file;

  /* Optional custom SSL context creator (advanced usage). If this function is
   * not empty, it will be called and can return a pointer to an SSL_CTX
   * instance, which is then used internally. Ownership of
   * any returned SSL_CTX* remains with the caller. The returned void* pointer
   * will be internally cast to an SSL_CTX* pointer. */
  std::function<void*(const struct SslConfig&)> custom_ctx_creator;

  explicit SslConfig(bool use_ssl_) : enable(use_ssl_) {}
};

/* Represent the global context OpenSSL object. */
class SslContext
{
public:
  explicit SslContext(const SslConfig& conf);
  ~SslContext();

  /* log all entries in the SSL error queue */
  void log_ssl_error_queue();

  std::string get_ssl_errors();

  template <size_t N> void throw_ssl_error(const char (&what)[N])
  {
    char buf[N + 256];
    memset(buf, 0, sizeof buf);
    memcpy(buf, what, N);
    buf[N - 1] = ':'; // replace none char of `what` with colon

    // store the last error on the queue before logging all
    unsigned long lasterr = ERR_peek_last_error();

    log_ssl_error_queue();

    /* throw an exception using the last error */
    ERR_error_string_n(lasterr, buf + N, sizeof(buf) - N);
    buf[sizeof(buf) - 1] = '\0';
    throw std::runtime_error(buf);
  }

  SSL_CTX* context() { return _ctx; };

private:
  SSL_CTX* _ctx; /* can be internal, or custom */
  SslConfig _config;
  bool _is_custom_ctx;
};


/* Represent the objects & state associated with an SSL session. */
struct SslSession {
  SSL* ssl;

  BIO* rbio; /* SSL reads from, we write to. */
  BIO* wbio; /* SSL writes to, we read from. */

  SslSession(SslContext* ctx, apex::connect_mode);
  ~SslSession();
};

/* Obtain the return value of an SSL operation and convert into a simplified
 * error code, which is easier to examine for failure. */
enum class sslstatus { ok, want_io, fail };
sslstatus get_sslstatus(SSL* ssl, int n);
std::string to_string(sslstatus);

} // namespace apex
