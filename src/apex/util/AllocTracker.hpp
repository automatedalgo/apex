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

#pragma once

namespace apex
{
/* Utility class to track memory allocations used within a scope.  Works by
 * overriding new operator, so generally don't use this for normal production
 * builds, because it slows down memory access for entire program.
 */
class AllocTracker {
public:
  explicit AllocTracker(const char*, bool always_print=false);
  ~AllocTracker();
private:
  unsigned long _id;
};

}
