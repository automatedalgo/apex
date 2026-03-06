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

#include <cstdint>
#include <mutex>
#include <cassert>
#include <apex/core/Logger.hpp>
#include <string.h>

#include <immintrin.h> // for _mm_pause on x86


class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            _mm_pause(); // short CPU-friendly spin
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};


/* A light-weight memory allocator. */
class LiteMem {
private:

  struct alignas(std::max_align_t) BlockHeader {
    size_t total_size; // full size, including header
    BlockHeader* prev; // previous block in memory
    bool is_free;
  };

  static_assert(
    sizeof(BlockHeader) % alignof(std::max_align_t) == 0,
    "BlockHeader must be alignment-sized"
    );

public:

  explicit LiteMem(size_t initial_size)
    : _mem(initial_size),
      _total_bytes(initial_size * sizeof(std::max_align_t))
  {
  }

  LiteMem(const LiteMem&) = delete;
  LiteMem& operator=(const LiteMem&) = delete;

  void* alloc(size_t size,
              const char* file,
              int line);

  void* alloc(size_t size);
  void* realloc(void* ptr, size_t size);


  void free(void* ptr, const char *file, int line);
  void free(void* ptr);

  size_t raw_memory_size() const {return _mem.size() * sizeof(std::max_align_t); }

  size_t raw_offset() const { return offset; }

private:

  // Use max_align_t to ensure the starting point memory is aligned. We are
  // using vector purely for its underlying memory allocation and alignment
  // guarantees, not for indexing.
  std::vector<std::max_align_t> _mem;

  size_t _total_bytes;

  // next free memory; base+offset is always aligned for std::max_align_t.
  size_t offset = 0;

  BlockHeader* last_block = nullptr;

  SpinLock _mutex;
};


/* Fundamtentals:

Modern CPUs require that certain types (e.g., int, double, pointers) are aligned
to 4, 8, or 16 bytes.  Note: this is a CPU level requirement, not even language
dependent.

So, when it is says "aligned to", what does that mean?

Can lead to undefine behaviour if this is not followed, If later the user casts
this memory to a double* or int64_t*:

so, can we just assume, all data is algiedn to worst case? (for our allocator)

note: we are mostly trying to be safe, avoid UB

Perverse cases

Users requests 1 byte


*/
void* LiteMem::alloc(size_t size, const char*, int) {
  return alloc(size);
}

void* LiteMem::alloc(size_t size) {

  constexpr size_t alignment = alignof(std::max_align_t);

  // round up request size so that later allocations will be aligned
  size_t padded_size = (size + alignment - 1) & ~(alignment - 1);

  // calculate total bytes needed (header + user memory)
  size_t total_size = sizeof(BlockHeader) + padded_size;

  {
    std::lock_guard lock(_mutex);

    if (offset + total_size > _total_bytes) {
      return nullptr; // out of memory
    }

    // determine where the header goes
    uintptr_t base = reinterpret_cast<uintptr_t>(_mem.data()) + offset;

    // insert the header
    auto header = reinterpret_cast<BlockHeader*>(base);
    header->total_size = total_size;
    header->prev = last_block;
    header->is_free = false;

    // determine the location of the user data (assumes BlockHeader is aligned)
    void* user_ptr = header + 1;

    // update state
    last_block = header;
    offset += total_size;

    assert(user_ptr != 0);
    return user_ptr;
  }
}

void LiteMem::free(void* ptr, const char *, int) {
  free(ptr);
}

void LiteMem::free(void* ptr) {
    if (!ptr)
      return;

    std::lock_guard lock(_mutex);

    // mark the current block as free
    auto* header = reinterpret_cast<BlockHeader*>(ptr) - 1;
    header->is_free = true;

    // if we are freeing the last block, we can try to collapse backwards
    if (header == last_block) {

      while (last_block && last_block->is_free)
      {
        offset -= last_block->total_size;
        last_block = last_block->prev;
      }
    }

}

void* LiteMem::realloc(void* ptr, size_t new_size)
{
  if (!ptr)
    return this->alloc(new_size);

  if (new_size == 0) {
    this->free(ptr);
    return nullptr;
  }

  // 1️⃣ Allocate a new block
  void* new_ptr = this->alloc(new_size);
  if (!new_ptr)
    return nullptr; // out of memory

  // 2️⃣ Copy old data
  auto* header = reinterpret_cast<BlockHeader*>(ptr) - 1;
  size_t old_size = header->total_size - sizeof(BlockHeader);
  memcpy(new_ptr, ptr, std::min(old_size, new_size));

  // 3️⃣ Free old block
  this->free(ptr);

  return new_ptr;
}
