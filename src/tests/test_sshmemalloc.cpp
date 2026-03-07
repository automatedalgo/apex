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
#include <apex/core/LiteMem.hpp>

#include <iostream>
#include <string_view>
#include <cstdint>

using namespace std;


TEST_CASE("basic_test")
{
  LiteMem mem(1024);

  std::cout << "sizeof(std::max_align_t): " << sizeof(std::max_align_t) << std::endl;
  std::cout << "raw_memory_size: " << mem.raw_memory_size() << std::endl;
  std::cout << "alignof(std::max_align_t) = " << alignof(std::max_align_t) << "\n";
  std::cout << "sizeof(std::max_align_t) = " << sizeof(std::max_align_t) << "\n";

  {
    auto orig_offset = mem.raw_offset();
    auto ptr = mem.alloc(1);
    REQUIRE(ptr != nullptr);
    REQUIRE(mem.raw_offset() != orig_offset);
    mem.free(ptr);
    REQUIRE(mem.raw_offset() == orig_offset);
  }

  for (int i = 0; i < 256; i++) {
    auto orig_offset = mem.raw_offset();
    auto ptr = mem.alloc(i);

    REQUIRE(ptr != nullptr);
    REQUIRE(mem.raw_offset() != orig_offset);
    mem.free(ptr);
    REQUIRE(mem.raw_offset() == orig_offset);
  }
  REQUIRE(mem.raw_offset() == 0);
}


TEST_CASE("collapse_free")
{
  LiteMem mem(1024);

  std::cout << "sizeof(std::max_align_t): " << sizeof(std::max_align_t) << std::endl;
  std::cout << "raw_memory_size: " << mem.raw_memory_size() << std::endl;
  std::cout << "alignof(std::max_align_t) = " << alignof(std::max_align_t) << "\n";
  std::cout << "sizeof(std::max_align_t) = " << sizeof(std::max_align_t) << "\n";


  {
    REQUIRE(mem.raw_offset() == 0);
    auto ptr1 = mem.alloc(100);
    auto ptr2 = mem.alloc(200);

    mem.free(ptr1);
    mem.free(ptr2);

    REQUIRE(mem.raw_offset() == 0);
  }

  {
    REQUIRE(mem.raw_offset() == 0);

    auto ptr1 = mem.alloc(100);
    auto ptr2 = mem.alloc(200);

    mem.free(ptr2);
    mem.free(ptr1);

    REQUIRE(mem.raw_offset() == 0);
  }

  {
    REQUIRE(mem.raw_offset() == 0);

    auto ptr1 = mem.alloc(100);
    auto ptr2 = mem.alloc(200);
    auto ptr3 = mem.alloc(200);

    mem.free(ptr1);
    mem.free(ptr2);
    mem.free(ptr3);

    REQUIRE(mem.raw_offset() == 0);
  }

  {
    REQUIRE(mem.raw_offset() == 0);

    auto ptr1 = mem.alloc(100);
    auto ptr2 = mem.alloc(200);
    auto ptr3 = mem.alloc(200);

    mem.free(ptr3);
    mem.free(ptr2);
    mem.free(ptr1);

    REQUIRE(mem.raw_offset() == 0);
  }

}

int main(int argc, char** argv)
{
  try {
    int result = quicktest::run(argc, argv);
    return (result < 0xFF ? result : 0xFF);
  } catch (std::exception& e) {
    cout << e.what() << endl;
    return 1;
  }
}
