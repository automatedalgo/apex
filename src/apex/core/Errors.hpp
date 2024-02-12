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

namespace apex
{

namespace error
{

// internal reject, GX connection not up
inline const char* const e0003 = "e0003";

// internal reject, no exchange
inline const char* const e0001 = "e0001";


// exchange new order reject
inline const char* const e0102 = "e0102";

// exchange cancel order reject
inline const char* const e0103 = "e0103";

// GX new order reject
inline const char* const e0200 = "e0200";

// GX logon reject
inline const char* const e0201 = "e0201";


} // namespace error
} // namespace apex
