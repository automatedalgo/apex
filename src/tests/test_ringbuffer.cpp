/* Copyright 2025 Automated Algo (www.automatedalgo.com)

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

#include <apex/util/RingBuffer.hpp>
#include <apex/core/Logger.hpp>


#include <iostream>
#include <string_view>

#include <string.h>

using namespace std;
using namespace apex;

TEST_CASE("ringbuffer_creation")
{
  {
    RingBuffer rb(1000);
    REQUIRE(rb.capacity() > 1000);
  }
  {
    RingBuffer rb(1);
    REQUIRE(rb.capacity() > 1);
  }
  {
    RingBuffer rb(0);
    REQUIRE(rb.capacity() > 1);
  }

}


TEST_CASE("ringbuffer_readwrite_one_byte_at_a_time")
{
  RingBuffer rb(1000);
  REQUIRE(rb.capacity() > 1000);

  REQUIRE(rb.space() == rb.capacity());
  REQUIRE(rb.empty());

  const char msg{'A'};
  while (rb.space() > 0) {
    memcpy(rb.write_ptr(), &msg, 1);
    rb.advance_write_ptr(1);
  }
  REQUIRE(rb.used() == rb.capacity());
  REQUIRE(rb.space() == 0);
  REQUIRE(!rb.empty());
  REQUIRE(rb.full());


  // read all of the bytes back, one at a time
  char last_data = ' ';
  uint64_t total_read = 0;
  while (rb.used() > 0) {
    last_data = *(rb.read_ptr());
    rb.advance_read_ptr(1);
    REQUIRE(last_data == 'A');
    total_read++;
  }

  REQUIRE(rb.empty());
  REQUIRE(rb.used() == 0);
  REQUIRE(rb.space() == rb.capacity());
  REQUIRE(total_read == rb.capacity());

  const char msg_b{'B'};
  while (rb.space() > 0) {
    memcpy(rb.write_ptr(), &msg_b, 1);
    rb.advance_write_ptr(1);
  }
  REQUIRE(rb.used() == rb.capacity());
  REQUIRE(rb.space() == 0);
  REQUIRE(!rb.empty());
  REQUIRE(rb.full());

  // read all of the bytes back, one at a time
  last_data = ' ';
  total_read = 0;
  while (rb.used() > 0) {
    last_data = *(rb.read_ptr());
    rb.advance_read_ptr(1);
    REQUIRE(last_data == 'B');
    total_read++;
  }

  REQUIRE(rb.empty());
  REQUIRE(rb.used() == 0);
  REQUIRE(rb.space() == rb.capacity());
  REQUIRE(total_read == rb.capacity());
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
