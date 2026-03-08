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

#include <apex/util/AllocTracker.hpp>
#include <config.h>

#include <array>
#include <atomic>
#include <cstdlib>

#include <algorithm>

#include <stdio.h>
#include <string.h>

/* Only include the allocation tracker implementation if APEX_ALLOC_TRACKER is
 * defined in the build system.  This is because the below code overides memory
 * allocations, leading to program slow down, so we only want this enabled if
 * deliberately configured - we want to avoid it being accidentally included in
 * a build. */

#if defined(APEX_ALLOC_TRACKER) && (APEX_ALLOC_TRACKER+0 == 1)

char msg[APEX_ALLOC_TRACKER];
#warning "Warning; AllocTracker active - memory allocations will be slow!"

// global counter for memory allocated
std::atomic<size_t> g_count_bytes = 0;

// global counter for calls made to 'new'
std::atomic<size_t> g_count_alloc = 0;


void* operator new(std::size_t sz) {
  g_count_bytes += sz;
  g_count_alloc++;
  if (void* p = std::malloc(sz))
    return p;
  throw std::bad_alloc{};
}


void operator delete(void* p) noexcept {
  std::free(p);
}


void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}

// Aligned versions (C++17+)
void* operator new(std::size_t sz, std::align_val_t al) {
  g_count_bytes += sz;
  g_count_alloc++;
  return std::aligned_alloc(static_cast<size_t>(al), sz);
}


void operator delete(void* p, std::align_val_t) noexcept {
  std::free(p);
}


void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}


namespace apex
{

struct AllocRecord {
  size_t id;  // location in the g_records array
  size_t parent_id; // location of parent scope
  int depth;
  int children; // count of child scopes
  char label[64];
  bool always_print;
  size_t bytes_start;
  size_t bytes_end;
  size_t alloc_start;
  size_t alloc_end;
};


// All active allocation records.  The capacity required covers the count of
// current parent and all its recursive childen. Once fully unwound, the records
// are reused.
std::array<AllocRecord, 16384> g_records = {};

// The number of indices associated with the sequence of parents related to the
// current allocation scope.
std::array<int, 4096> g_parents = {};

// Next index to use in g_records
std::atomic<size_t> g_next_idx = 0;

// Next scope depth
std::atomic<int> g_next_depth = 0;

static size_t next_idx() {
  size_t next_id = g_next_idx;
  g_next_idx = std::min(g_records.size(), g_next_idx+1);
  return next_id;
}

/* For a allocation at scope `idx` find its ancestor at `depth` Depth of zero is
 * its immediate parent record/scope. */
static size_t find_parent_at_depth(size_t idx, int depth) {
  auto parent_id = g_records[idx].parent_id;
  if (depth == 0)
    return parent_id;
  else
    return find_parent_at_depth(parent_id, depth-1);
}


/* Print all allocation records, in nested fashion. */
static void print_alloc_records(){
  // capture upper record used.
  const int upper_idx = g_next_idx - 1;

  // Loop over each activity recording, displaying it as a nested tree.
  for (int i = 0; i <= upper_idx; i++) {
    auto bytes = g_records[i].bytes_end - g_records[i].bytes_start;
    auto allocs = g_records[i].alloc_end - g_records[i].alloc_start;
    if (g_records[i].always_print || (allocs > 0))
    {
      for (int j = g_records[i].depth-1; j >=0 ; j--) {
        int ancestor_idx =  find_parent_at_depth(i, j);

        int & ancestor_chidren = g_records[ancestor_idx].children;

        if (j == 0)  {
          if (ancestor_chidren > 1)
            printf("├─");
          else if (ancestor_chidren == 1)
            printf("└─");
          else
            printf("?");
          ancestor_chidren--;
        }
        else {
          if (ancestor_chidren > 0)
            printf("│ ");
          else
            printf("  ");
        }
      }

      printf("(%lu, %lu)  %s\n", bytes, allocs, g_records[i].label);
    }
  }
}

AllocTracker::AllocTracker(const char* label, bool always_print)
  : _id{next_idx()}
{
  memset(&g_records[_id], 0, sizeof(AllocRecord));
  auto depth = g_next_depth++;
  g_parents[depth] = _id; // track this parent
  memset(g_records[_id].label, 0, 64);
  strncpy(g_records[_id].label, label, 64-1);
  g_records[_id].always_print = always_print;
  g_records[_id].depth = depth;

  // capture the allocation counters
  g_records[_id].bytes_start = g_count_bytes;
  g_records[_id].alloc_start = g_count_alloc;

  if (depth > 0) {
    auto parent_id = g_parents[depth-1];
    g_records[_id].parent_id = parent_id;
    g_records[parent_id].children++;
  }
}


AllocTracker::~AllocTracker() {
  // capture the allocation counters
  g_records[_id].bytes_end = g_count_bytes;
  g_records[_id].alloc_end = g_count_alloc;

  // decrement tracker depth, if we are back to 0, the tracking is complete and
  // we dump the statistics and start using array items from the beginning
  g_next_depth--;
  if (g_next_depth == 0) {
    print_alloc_records();
    g_next_idx = 0;
  }
}

} // namespace apex

#else

namespace apex
{
AllocTracker::AllocTracker(const char*, bool): _id(0){}
AllocTracker::~AllocTracker() = default;
}
#endif
