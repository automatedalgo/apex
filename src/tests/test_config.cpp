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

#include "quicktest.hpp"

#include <apex/util/platform.hpp>
#include <apex/util/utils.hpp>
#include <apex/util/Config.hpp>
#include <iostream>

#include <string.h>

using namespace std;
using namespace apex;

struct Limits {
  string limit;
  double lower;
  double upper;

  static auto schema() {
    FIELD_DEF_INIT( Limits )
      FIELD_DEF_REQUIRED( limit );
    FIELD_DEF_REQUIRED( lower );
    FIELD_DEF_REQUIRED( upper );
    FIELD_DEF_RETURN();
  }
  bool operator==(const Limits &) const = default;
};


struct LoginParams {
  string user;
  string pass;

  static auto schema() {
    FIELD_DEF_INIT( LoginParams )
      FIELD_DEF_REQUIRED( user );
    FIELD_DEF_REQUIRED( pass );
    FIELD_DEF_RETURN();
  }
  bool operator==(const LoginParams&) const = default;
};


struct Exchange {
  string name;
  string mic;
  bool active;

  static auto schema() {
    FIELD_DEF_INIT( Exchange );
      FIELD_DEF_REQUIRED( name );
    FIELD_DEF_REQUIRED( mic );
    FIELD_DEF_OPTIONAL( active, true );
    FIELD_DEF_RETURN();
  }

  bool operator==(const Exchange&) const = default;
};

struct AllTypes {
  string _string;
  int _int;
  unsigned int _unsigned_int;
  double _double;
  float _float;
  char _char;
  unsigned long _unsigned_long;
  signed long _signed_long;

  static auto schema() {
    FIELD_DEF_INIT( AllTypes );
    FIELD_DEF_OPTIONAL( _string, "" );
    FIELD_DEF_OPTIONAL( _int, 0 );
    FIELD_DEF_OPTIONAL( _unsigned_int, 0 );
    FIELD_DEF_OPTIONAL( _double, 0 );
    FIELD_DEF_OPTIONAL( _float, 0 );
    FIELD_DEF_OPTIONAL( _char, ' ' );
    FIELD_DEF_OPTIONAL( _unsigned_long, 0 );
    FIELD_DEF_OPTIONAL( _signed_long, 0 );
    FIELD_DEF_RETURN();
  }
  bool operator==(const AllTypes&) const = default;
};

struct TcpTransport
{
  AllTypes all_types;
  string addr;
  string port;
  int max_notional;
  LoginParams creds;
  vector<string> names;
  vector<Limits> limits;
  map<string, string> options;
  vector<map<string, Exchange>> exchanges;

  static auto schema() {
    FIELD_DEF_INIT( TcpTransport );
    FIELD_DEF_OPTIONAL( all_types, AllTypes{} );
    FIELD_DEF_REQUIRED( addr );
    FIELD_DEF_OPTIONAL( port, "18000" );
    FIELD_DEF_REQUIRED( options );
    FIELD_DEF_OPTIONAL( max_notional, 500000 );
    FIELD_DEF_REQUIRED( creds );
    FIELD_DEF_REQUIRED( names );
    FIELD_DEF_REQUIRED( limits );
    FIELD_DEF_REQUIRED( exchanges );
    FIELD_DEF_RETURN();
  }

  bool operator==(const TcpTransport&) const = default;
};


TEST_CASE("basic_config")
{
  const auto rawstr = R"(
    {
        "addr" : "localhost",
        "creds" : {
            "user": "sam",
            "pass": "n"
        },
        "names": ["doge","btc"],
        "options": {
            "nagle": "yes",
            "quickack": "no"
        },
        "limits": [
            {
                "limit": "risk",
                "upper" : 123432.0,
                "lower" : 33.0
            },
            {
                "limit": "gmv",
                "upper" : 123432.0,
                "lower" : 33.0
            }
        ],
        "exchanges" : [
            {
                "eurex" : {
                    "name": "eurex1",
                    "mic" : "ERX",
                    "active": true

                },
                "liffe" : {
                    "name": "liffe1",
                    "mic" : "LIF",
                    "active": false
                }
            },
            {
                "xetra" : {
                    "name": "xetra",
                    "mic" : "XTR"
                },
                "lse" : {
                    "name": "lse",
                    "mic" : "LSE"
                }
            },
            {
                "binance" : {
                    "name": "bin",
                    "mic" : "BIN"
                },
                "kucoin" : {
                    "name": "kcn",
                    "mic" : "KCN"
                }
            }
        ]
    }
)";

  json raw = json::parse(rawstr);

  apex::ConfigParser<TcpTransport> parser;
  parser.parse(raw);
  TcpTransport tmp = std::move(parser.result);

  REQUIRE(tmp.addr == "localhost");
  REQUIRE(tmp.port == "18000");
  REQUIRE(tmp.limits.size() == 2);
}


TEST_CASE("parse_reparse") {
  const auto rawstr = R"(
    {
        "addr" : "localhost",
        "creds" : {
            "user": "sam",
            "pass": "n"
        },
        "names": ["doge","btc"],
        "options": {
            "nagle": "yes",
            "quickack": "no"
        },
        "limits": [
            {
                "limit": "risk",
                "upper" : 123432.0,
                "lower" : 33.0
            },
            {
                "limit": "gmv",
                "upper" : 123432.0,
                "lower" : 33.0
            }
        ],
        "exchanges" : [
            {
                "eurex" : {
                    "name": "eurex1",
                    "mic" : "ERX",
                    "active": true

                },
                "liffe" : {
                    "name": "liffe1",
                    "mic" : "LIF",
                    "active": false
                }
            },
            {
                "xetra" : {
                    "name": "xetra",
                    "mic" : "XTR"
                },
                "lse" : {
                    "name": "lse",
                    "mic" : "LSE"
                }
            },
            {
                "binance" : {
                    "name": "bin",
                    "mic" : "BIN"
                },
                "kucoin" : {
                    "name": "kcn",
                    "mic" : "KCN"
                }
            }
        ]
    }
)";

  json raw = json::parse(rawstr);

  apex::ConfigParser<TcpTransport> parser;
  parser.parse(raw);
  TcpTransport tmp = std::move(parser.result);


  REQUIRE(tmp.addr == "localhost");
  REQUIRE(tmp.port == "18000");
  REQUIRE(tmp.limits.size() == 2);

  apex::ConfigWriter<TcpTransport> writer;
  std::cout << "--------------------\n";
  auto j = writer.to_json(tmp);
  auto serialised = j.dump();

  {
    apex::ConfigParser<TcpTransport> parser;
    parser.parse(json::parse(serialised));
    TcpTransport parse2 = std::move(parser.result);
    bool same = tmp == parse2;
    REQUIRE(same);
  }
}


TEST_CASE("missing_field")
{
  const auto rawstr = R"(
{
"user" : "karl"
}
)";

  json raw = json::parse(rawstr);

  std::string error;
  apex::ConfigParser<LoginParams> parser;
  try {
    parser.parse(raw); // should throw
  }
  catch (std::exception& e) {
    error = e.what();
  }
  // PRINT(error);
  auto iter = error.find("pass");
  REQUIRE(iter != std::string::npos); // expect to find
  REQUIRE(error == "config missing 'pass'");
}


TEST_CASE("extra_field")
{
  const auto rawstr = R"(
{
"user" : "karl",
"pass" : "fjdnJD!FK*34782",
"realm" : "assets"
}
)";

  json raw = json::parse(rawstr);

  std::string error;
  apex::ConfigParser<LoginParams> parser;
  try {
    parser.parse(raw); // should throw
  }
  catch (std::exception& e) {
    error = e.what();
  }
  // PRINT(error);
  auto iter = error.find("realm");
  REQUIRE(iter != std::string::npos); // expect to find
  REQUIRE(error == "unexpected config key 'realm'");
}


TEST_CASE("wrong_field_type")
{
  const auto rawstr = R"(
{
"user" : "karl",
"pass" : 1235
}
)";

  json raw = json::parse(rawstr);

  std::string error;
  apex::ConfigParser<LoginParams> parser;
  try {
    parser.parse(raw); // should throw
  }
  catch (std::exception& e) {
    error = e.what();
  }
  // PRINT(error);
  REQUIRE(error.find("'pass'") != std::string::npos); // expect to find
}


int main(int argc, char** argv)
{
  try {
    int result = quicktest::run(argc, argv);
    return (result < 0xFF ? result : 0xFF);
  } catch (exception& e) {
    cout << e.what() << endl;
    return 1;
  }
}
