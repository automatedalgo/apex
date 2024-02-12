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

#include <apex/util/utils.hpp>
#include <apex/util/platform.hpp>

#include <iostream>

using namespace std;

TEST_CASE("utils")
{
  REQUIRE(apex::split("", ',').empty());
  REQUIRE(apex::split(",", ',') == vector<string>({"", ""}));
  REQUIRE(apex::split("a,b", ',') == vector<string>({"a", "b"}));
  REQUIRE(apex::split("a,,b", ',') == vector<string>({"a", "", "b"}));

  REQUIRE(apex::trim(" a	 ") == "a");
  REQUIRE(apex::trim(" a") == "a");
  REQUIRE(apex::trim("  ").empty());
  REQUIRE(apex::trim("	").empty());
  REQUIRE(apex::trim("").empty());

  REQUIRE(apex::trim("012340", "0") == "1234");
  REQUIRE(apex::trim("1234", "0") == "1234");
  REQUIRE(apex::trim("1234", "") == "1234");
  REQUIRE(apex::trim("", "").empty());
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
