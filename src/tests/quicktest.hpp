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

/* quicktest is a simple test framework. Aim is to be fast to compile, has
 * limited features, and usage is similar to catch.hpp, which can be drop in
 * replacement.
 */


#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace quicktest
{

const char* colour_none(int bold = 0) { return bold ? "\033[1m" : "\033[0m"; }

const char* colour_red(int bold = 0)
{
  return bold ? "\033[1;31m" : "\033[0;31m";
}

const char* colour_green(int bold = 0)
{
  return bold ? "\033[1;32m" : "\033[0;32m";
}

const char* colour_yellow(int bold = 0)
{
  return bold ? "\033[1;33m" : "\033[0;33m";
}

const char* colour_blue(int bold = 0)
{
  return bold ? "\033[1;34m" : "\033[0;34m";
}

const char* colour_cyan(int bold = 0)
{
  return bold ? "\033[1;36m" : "\033[0;36m";
}

class test_exception : public std::exception
{
public:
  ~test_exception() noexcept {}
};

class test_case;

test_case* test_case_current = 0;

void raise_error(const char* msg, const char* file, int line);

#define INFO(X) std::cout << X << std::endl

#define CAPTURE(X)                                                             \
  std::cout << quicktest::colour_cyan() << #X << ": " << X               \
            << quicktest::colour_none() << std::endl

#define REQUIRE(X)                                                             \
  do {                                                                         \
    bool b{X};                                                          \
    if (!b) {                                                                  \
      quicktest::raise_error("REQUIRE( " #X " )", __FILE__, __LINE__);          \
      throw quicktest::test_exception();                                        \
    }                                                                          \
  } while (false)

std::vector<test_case*> global_test_reg;
std::map<std::string, test_case*> global_test_map;

enum class outcome { fail, pass };

std::string to_string(outcome o)
{
  std::string str;
  switch (o) {
    case outcome::fail:
      str += colour_red(1);
      str += "fail";
      break;
    case outcome::pass:
      str += colour_green(1);
      str += "pass";
      break;
  }
  str += colour_none();
  return str;
}

struct test_result
{
  std::string testname;
  outcome result;

  test_result() : result(outcome::fail) {}
  test_result(const std::string& name, bool passed)
    : testname(name), result(passed ? outcome::pass : outcome::fail)
  {
  }

  void result_dump() const
  {
    std::cout << "[" << to_string(result) << "] " << testname << std::endl;
  }
};

class test_case
{
public:
  test_case(std::string label, std::string file, int line)
    : m_has_failed(false), m_label(label), m_file(file), m_line(line)
  {
    global_test_reg.push_back(this);

    if (global_test_map.find(label) != end(global_test_map)) {
      std::cout << "error, duplicate test_case '" << label << "'" << std::endl;
      exit(1);
    } else
      global_test_map.insert({label, this});
  }
  virtual ~test_case() {}
  virtual void impl() = 0;
  const std::string& testname() const { return m_label; }

  void exception_caught(const char* err)
  {
    m_has_failed = true;
    if (err)
      std::cout << colour_yellow() << "exception: " << err << colour_none()
                << std::endl;
  }
  void incr_failure(const char* err, const char* file, int line)
  {
    m_has_failed = true;
    if (err) {
      std::cout << colour_yellow() << err << " (" << file << ":" << line << ")"
                << colour_none() << std::endl;
    }
  }

  test_result run()
  {
    m_has_failed = false;

    try {
      std::cout << std::endl << "test_case: " << colour_none(1) << testname()
                << colour_none() << std::endl;
      std::cout << "location : " << m_file << ":" << m_line << std::endl;
      this->impl();
    } catch (const test_exception&) {
      m_has_failed = true;
    } catch (const std::exception& e) {
      exception_caught(e.what());
    } catch (...) {
      exception_caught("unknown exception");
    }

    if (m_has_failed) {
      std::cout << "[" << to_string(outcome::fail) << "] " << testname()
                << std::endl;
    } else {
      std::cout << "[" << to_string(outcome::pass) << "] " << testname()
                << std::endl;
    }
    return test_result(testname(), !m_has_failed);
  }

private:
  bool m_has_failed;
  std::string m_label;
  std::string m_file;
  int m_line;
};

void banner()
{
  std::cout << "============================================================";
}

void thin_banner()
{
  std::cout << "------------------------------------------------------------";
}

int run(int /*argc*/, char** argv)
{
  banner();
  std::cout << std::endl << "QUICK_TEST: " << argv[0] << std::endl;
  banner();
  std::cout << std::endl;
  std::vector<test_result> results;
  for (std::vector<test_case*>::iterator i = global_test_reg.begin();
       i != global_test_reg.end(); ++i) {
    test_case_current = *i;
    thin_banner();
    results.push_back(test_case_current->run());
    std::cout << std::endl;
  }

  banner();
  std::cout << std::endl << "Results" << std::endl;
  banner();
  std::cout << std::endl;

  int count_tests = 0;
  int count_pass = 0;
  int count_fail = 0;
  for (std::vector<test_result>::iterator it = results.begin();
       it != results.end(); ++it) {
    it->result_dump();
    count_tests++;
    count_pass += it->result == outcome::pass;
    count_fail += it->result == outcome::fail;
  }

  std::cout << std::endl;
  std::cout << "total " << count_tests;
  std::cout << ", passes " << count_pass;
  std::cout << ", failures " << count_fail << std::endl;

  return (count_fail != 0);
}

void raise_error(const char* err, const char* file, int line)
{
  test_case_current->incr_failure(err, file, line);
}

} // namespace quicktest

#define QUICKTEST_CONCAT2(A, B) A##B
#define QUICKTEST_CONCAT(A, B) QUICKTEST_CONCAT2(A, B)

#define QUICKTEST_NAME_LINE(name, line) QUICKTEST_CONCAT2(name, line)

#define TEST_CASE_IMPL(label, file, line, token)                               \
  void QUICKTEST_CONCAT(impl_, token)() /*forward*/;                            \
  class token : public quicktest::test_case                                     \
  {                                                                            \
  public:                                                                      \
    token(std::string l, std::string f, int n) : test_case(l, f, n){};         \
    void impl() { QUICKTEST_CONCAT(impl_, token)(); }                           \
  };                                                                           \
  static token QUICKTEST_CONCAT(my__, token)(label, file, line);                \
  void QUICKTEST_CONCAT(impl_, token)()

#define TEST_CASE(X)                                                           \
  TEST_CASE_IMPL(X, __FILE__, __LINE__,                                        \
                 QUICKTEST_NAME_LINE(quicktest__, __LINE__))
