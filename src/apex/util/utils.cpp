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

#include <apex/util/utils.hpp>
#include <apex/util/json.hpp>
#include <apex/util/Time.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/platform.hpp>

#include <fstream>
#include <memory>

#include <openssl/hmac.h> // cryptography functions

#include <cxxabi.h>
#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <future>

namespace apex
{

std::vector<std::string> split(const std::string_view& s, char d)
{
  std::vector<std::string> items;
  if (!s.empty()) {
    size_t p = 0;
    while (true) {
      size_t e = s.find(d, p);
      items.push_back(std::string(s.substr(p, e-p)));
      if (e == std::string::npos)
        break;
      else
        p = e + 1;
    }
  }
  return items;
}


std::filesystem::path user_home_dir() {
  const char * path = std::getenv("HOME");

  if (path == nullptr)
    return path;

  // if HOME not set, try other approach
  struct passwd* pwd = getpwuid(getuid());
  if (pwd && pwd->pw_dir)
    return pwd->pw_dir;

  return {};
}


std::string utc_timestamp_iso8601()
{
  auto now = Time::realtime_now();
  return now.as_iso8601(Time::Resolution::milli, true);
}


std::string utc_timestamp_condensed(bool add_fraction)
{
  static constexpr char full_format[] = "20170521-075117.000";
  static constexpr char shrt_format[] = "20170521-075117";
  static constexpr int short_len = 15;

  static_assert(short_len == (sizeof shrt_format - 1),
                "short_len check failed");

  auto tv = apex::time_now();

  char buf[32] = {0};
  assert(sizeof buf > (sizeof full_format));
  assert(sizeof full_format > sizeof shrt_format);

  struct tm parts;
  time_t rawtime = tv.sec;

#ifndef _WIN32
  gmtime_r(&rawtime, &parts);
#else
  gmtime_s(&parts, &rawtime);
#endif

  if (0 == strftime(buf, sizeof buf - 1, "%Y%m%d-%H%M%S", &parts))
    return ""; // strftime not successful

  // append milliseconds
  if (add_fraction) {
    int ec;
#ifndef _WIN32
    ec = snprintf(&buf[short_len], sizeof(buf) - short_len, ".%03dZ",
                  (int)tv.usec / 1000);
#else
    ec = sprintf_s(&buf[short_len], sizeof(buf) - short_len, ".%03dZ",
                   (int)tv.usec / 1000);
#endif
    if (ec < 0)
      return "";
  }

  buf[sizeof full_format - 1] = '\0';
  return buf;
}


std::string demangle(const char* name)
{
  int status = -1; // arbitrary value, eliminate compiler warning

  std::unique_ptr<char, void (*)(void*)> res{
      abi::__cxa_demangle(name, NULL, NULL, &status), std::free};

  return (status == 0) ? res.get() : name;
}

std::string to_hex(const unsigned char* p, size_t size)
{
  static const char digits[] = "0123456789abcdef";
  std::string s(size * 2, ' ');

  for (size_t i = 0; i < size; ++i) {
    s[i * 2 + 0] = digits[(p[i] & 0xF0) >> 4];
    s[i * 2 + 1] = digits[(p[i] & 0x0F)];
  }

  return s;
}

std::string to_hex(const char* p, size_t size)
{
  return to_hex((const unsigned char*)(p), size);
}


void log_exception(const char* site)
{
  try {
    throw;
  } catch (std::exception& e) {
    LOG_WARN("exception caught for " << site << " : " << e.what());
  } catch (...) {
    LOG_WARN("exception caught for " << site << " : unknown");
  }
}

std::string slurp(const char* filename)
{
  std::ifstream f(filename, std::ios::in | std::ios::binary);

  LOG_INFO("reading file '" << filename << "'");
  if (f) {
    // set file read header to the end of the file
    f.seekg(0, std::ios::end);
    // reserve correct length of a string
    std::string buf(f.tellg(), 0);
    // set file read header to the start of the file
    f.seekg(0, std::ios::beg);
    // read the file direct into the string DecodeBuffer
    f.read(&buf[0], buf.size());
    return buf;
  }
  throw std::system_error(errno, std::system_category(), "file read failed");
}


/*
Compute the HMAC-SHA256 using a secret over a message.

On success, zero is returned.  On error, -1 is returned.
*/
std::string HMACSHA256_base4(const char* key, int keylen, const char* msg,
                             int msglen)
{
  unsigned char md[EVP_MAX_MD_SIZE + 1]; // EVP_MAX_MD_SIZE=64
  memset(md, 0, sizeof(md));
  unsigned int mdlen;

  HMAC(EVP_sha256(), key, keylen, (const unsigned char*)msg, msglen, md,
       &mdlen);

  return to_hex(md, mdlen);
}

void log_message_exception(const char* source, std::string data)
{
  try {
    throw;
  } catch (const std::exception& e) {
    LOG_WARN("message processing error, from: "
             << source << ", error: " << e.what() << ", message:" << data);
  } catch (...) {
    LOG_WARN("message processing error, from: " << source
                                                << ", message:" << data);
  }
}

std::string format_double(double d, bool trim_zeros, int precision)
{
  char buf[256] = {};
  int written = snprintf(buf, sizeof(buf) - 1, "%.*f", precision, d);


  // TODO: unit test this!
  if (written > 0 && trim_zeros) // 50.02000###
    for (int i = written + 1;
         i > 0 && ((buf[i] == '0' && buf[i - 1] != '.') || buf[i] == 0); i--)
      buf[i] = 0;

  return buf;
}


std::string str_toupper(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return s;
}


std::string str_tolower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}


void write_json_message(const std::string& dir, std::string msgtype,
                        std::string raw)
{
  try {
    auto msg = json::parse(raw);
    std::ostringstream oss;
    oss << dir << "/" << msgtype << "-" << utc_timestamp_condensed() << ".json";
    std::ofstream f(oss.str());
    f << msg;
    LOG_INFO("writing json " << oss.str());
  } catch (const std::exception& e) {
    LOG_ERROR("json parse error: " << e.what());
  }
}


static ScaledInt parse_scaled_int(const std::string& raw)
{
  const auto count_dp = std::count(raw.begin(), raw.end(), '.');
  if (count_dp > 1)
    throw std::runtime_error("too many decimal-points in strings");

  if (count_dp == 0)
    return ScaledInt(stoll(raw), 0);

  const auto trimmed = trim(raw, "0");

  if (trimmed.empty() || trimmed == ".")
    return ScaledInt(); // string was like '0' or '0.0', '.00' etc

  if (trimmed.back() == '.') {
    /* raw was like 123.000 */
    auto sig = stoll(trimmed.substr(0, std::size(trimmed) - 1));
    return {sig, 0};
  } else if (trimmed.front() == '.') {
    /* raw was like 0.00125 */
    auto digits = trimmed.substr(1);
    const int scale = digits.size();
    assert(digits.size() + 1 == trimmed.size());
    return ScaledInt(stoll(trim(digits, "0")), -scale);
  } else {
    /* raw like 100.001 */
    auto pos = trimmed.find('.');
    auto swhole = trimmed.substr(0, pos);
    auto sfract = trimmed.substr(pos);

    auto si_fract = parse_scaled_int(sfract);
    auto whole = std::stoll(swhole) * int(std::pow(10, -si_fract.scale())) +
                 si_fract.mantissa();
    return ScaledInt(whole, si_fract.scale());
  }
}

ScaledInt::ScaledInt(const std::string& s) : ScaledInt(parse_scaled_int(s)) {}

std::ostream& operator<<(std::ostream& os, const ScaledInt& i)
{
  os << "ScaledInt(_mantissa=" << i.mantissa() << ", _scale=" << i.scale()
     << ")";
  return os;
}


ScaledInt::ScaledInt(int64_t mantissa, int scale)
  : _mantissa(mantissa), _scale(scale), _exponent_pow10(pow(10, scale))
{
}


double ScaledInt::ceil(double raw) const
{
  double raw_mantissa = raw / _exponent_pow10;
  double raw_mantissa_factor = raw_mantissa / _mantissa;
  double raw_mantissa_factor_round = std::ceil(raw_mantissa_factor);
  return raw_mantissa_factor_round * _mantissa * _exponent_pow10;
}

double ScaledInt::trunc(double raw) const
{
  double raw_mantissa = raw / _exponent_pow10;
  double raw_mantissa_factor = raw_mantissa / _mantissa;
  double raw_mantissa_factor_round = std::trunc(raw_mantissa_factor);
  return raw_mantissa_factor_round * _mantissa * _exponent_pow10;
}

/*
double ScaledInt::round(double d) const
{

  // Example
  // d:  38000.123449999
  // tick: 0.01 ==>  _mantissa: 1  _scale: -2, exponent_pow10: 0.01
  // scaled: 3800012
  //  unscaled: 38000.1200000....  (tiny error term)
  //

  int64_t scaled = std::llround(d / _exponent_pow10);

  if (_mantissa == 1 || scaled % _mantissa == 0)
    return _exponent_pow10 * scaled;
  else
    return _exponent_pow10 * (_mantissa * std::llround(scaled / _mantissa));
}
*/


void create_dir(std::filesystem::path dir)
{
  std::error_code err;
  auto created = std::filesystem::create_directories(dir, err);
  if (err) {
    std::ostringstream oss;
    oss << "failed to create directory " << dir << ", error " << err;
    throw std::system_error(err, oss.str());
  }
  if (created)
    LOG_INFO("created directory " << dir << "");
}


std::string int32_to_base16(uint32_t i, bool trim_leading_zeros)
{
  static const char alphabet[] = "0123456789abcdef";
  char buf[8 + 1] = {0};

  buf[0] = alphabet[(i & 0xF0000000) >> 28];
  buf[1] = alphabet[(i & 0x0F000000) >> 24];
  buf[2] = alphabet[(i & 0x00F00000) >> 20];
  buf[3] = alphabet[(i & 0x000F0000) >> 16];
  buf[4] = alphabet[(i & 0x0000F000) >> 12];
  buf[5] = alphabet[(i & 0x00000F00) >> 8];
  buf[6] = alphabet[(i & 0x000000F0) >> 4];
  buf[7] = alphabet[(i & 0x0000000F) >> 0];

  size_t k = 0;
  while (trim_leading_zeros && k < sizeof(buf) && buf[k] == '0')
    k++;
  return &buf[k];
}

bool is_finite_non_zero(double d) { return std::isfinite(d) && d != 0.0; }

bool interrupt_invoked = false;
std::promise<int> interrupt_code;

void interrupt_handler(int)
{
  if (!interrupt_invoked) {
    interrupt_invoked = true;
    interrupt_code.set_value(1);
  }
}

void wait_for_sigint() {
  // install control-c signal handler
  struct sigaction newsigact = {};
  memset(&newsigact, 0, sizeof(newsigact));
  newsigact.sa_handler =  interrupt_handler;
  sigaction(SIGINT, &newsigact, nullptr);

  interrupt_code.get_future().wait();
}

std::ostream& operator<<(std::ostream& os, RunMode m)
{
  os << to_string(m);
  return os;
}

std::string to_string(RunMode m)
{
  switch (m) {
    case RunMode::paper:
      return {"paper"};
    case RunMode::live:
      return {"live"};
    case RunMode::backtest:
      return {"backtest"};
    default:
      return {"unkown_run_mode"};
  }
}


RunMode parse_run_mode(const std::string& s)
{
  if (s == "paper")
    return RunMode::paper;
  if (s == "live")
    return RunMode::live;
  if (s == "backtest")
    return RunMode::backtest;

  std::ostringstream oss;
  oss << "invalid RunMode value '" << s << "'";
  throw std::runtime_error(oss.str());
}


} // namespace apex
