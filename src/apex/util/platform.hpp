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

#include <string>

#ifndef _WIN32
#include <sys/time.h>
#endif

#ifdef _WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

namespace apex
{

struct TimeVal {
#ifndef _WIN32
  typedef time_t type_type;
#else
  typedef __time64_t type_type;
#endif
  type_type sec;  /* seconds */
  type_type usec; /* micros */
};

long thread_id();

/* Get real-world current epoch/utc time */
TimeVal time_now();

/** Return local hostname, or throw upon failure. */
std::string hostname();

/** Return username */
[[maybe_unused]] std::string username();

/** Process pid */
int getpid();

} // namespace apex
