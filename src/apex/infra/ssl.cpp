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

#include <apex/infra/ssl.hpp>
#include <apex/core/Logger.hpp>

namespace apex
{

sslstatus get_sslstatus(SSL* ssl, int n)
{
  switch (SSL_get_error(ssl, n)) {
    case SSL_ERROR_NONE:
      return sslstatus::ok;
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_READ:
      return sslstatus::want_io;
    case SSL_ERROR_ZERO_RETURN:
    case SSL_ERROR_SYSCALL:
    default:
      return sslstatus::fail;
  }
}

SslContext::SslContext(const SslConfig& conf)
  : _ctx(nullptr), _config(conf), _is_custom_ctx(false)
{
  /* SSL library initialisation */
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();

#if OPENSSL_VERSION_MAJOR < 3
  ERR_load_BIO_strings(); // deprecated since OpenSSL 3.0
#endif

  ERR_load_crypto_strings();

  if (conf.custom_ctx_creator == nullptr) {
    // create default SSL context
    _ctx = SSL_CTX_new(TLS_method());

    if (!_ctx)
      throw_ssl_error("SSL_CTX_new failed");

    SSL_CTX_set_security_level(_ctx, 4);

    _is_custom_ctx = false;

    if (!_config.certificate_file.empty() &&
        !_config.private_key_file.empty()) {
      /* Load certificate and private key files, and check consistency  */
      if (SSL_CTX_use_certificate_file(_ctx, _config.certificate_file.c_str(),
                                       SSL_FILETYPE_PEM) != 1)
        throw_ssl_error("SSL_CTX_use_certificate_file");

      /* Indicate the key file to be used */
      if (SSL_CTX_use_PrivateKey_file(_ctx, _config.private_key_file.c_str(),
                                      SSL_FILETYPE_PEM) != 1)
        throw_ssl_error("SSL_CTX_use_PrivateKey_file");

      /* Make sure the key and certificate file match */
      if (SSL_CTX_check_private_key(_ctx) != 1)
        throw_ssl_error("SSL_CTX_check_private_key");
    }

    /* Recommended to avoid SSLv2 & SSLv3 */
    // SSL_CTX_set_options(m_ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2 |
    // SSL_OP_NO_SSLv3);
    SSL_CTX_set_options(_ctx, 0);


    SSL_CTX_set_ciphersuites(_ctx, TLS_DEFAULT_CIPHERSUITES);

#ifdef SSL_CTX_set_ecdh_auto

    /* Enable automatic ECDH selection */
    if (SSL_CTX_set_ecdh_auto(_ctx, 1) != 1)
      throw_ssl_error("SSL_CTX_set_ecdh_auto");
#endif
  } else {
    // use customised context
    _ctx = (SSL_CTX*)conf.custom_ctx_creator(conf);
    if (_ctx == nullptr)
      throw_ssl_error("Failed to create custom ssl context");
    else
      _is_custom_ctx = true;
  }
}

SslContext::~SslContext()
{
  if (_ctx && !_is_custom_ctx)
    SSL_CTX_free(_ctx);
}

void SslContext::log_ssl_error_queue()
{
  unsigned long l;
  char buf[256];

  while ((l = ERR_get_error()) != 0) {
    ERR_error_string_n(l, buf, sizeof buf);
    LOG_ERROR("ssl " << buf);
  }
}


std::string SslContext::get_ssl_errors()
{
  std::ostringstream oss;
  unsigned long ec;
  char buf[256];

  do {
    ec = ERR_get_error();
    if (ec) {
      memset(buf, 0, sizeof(buf));
      ERR_error_string_n(ec, buf, sizeof(buf)-1);
      oss << buf << ";";
    }
  } while (ec);

  return oss.str();
}



SslSession::SslSession(SslContext* ctx, connect_mode cm)
  : ssl(nullptr), rbio(nullptr), wbio(nullptr)
{
  if (ctx == nullptr)
    throw std::runtime_error("SSL context is null");

  rbio = BIO_new(BIO_s_mem());
  wbio = BIO_new(BIO_s_mem());
  ssl = SSL_new(ctx->context());
  SSL_set_bio(ssl, rbio, wbio);

  if (cm == connect_mode::connect)
    SSL_set_connect_state(ssl);
  if (cm == connect_mode::accept)
    SSL_set_accept_state(ssl);
}


SslSession::~SslSession()
{
  if (ssl)
    SSL_free(ssl); // will also free associated BIO
}


std::string to_string(sslstatus s)
{
  switch (s) {
    case sslstatus::ok:
      return "ok";
    case sslstatus::want_io:
      return "want_io";
    case sslstatus::fail:
      return "fail";
  }

  return "unknown_enum";
}


} // namespace apex
