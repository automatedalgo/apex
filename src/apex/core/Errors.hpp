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

#include <string_view>

namespace apex::error
{

// internal reject, no exchange
inline const char* const e0001 = "e0001";

// internal reject, exchange link down
inline constexpr std::string_view venue_link_down = "e0002";

// internal reject, GX connection not up
inline constexpr std::string_view gateway_down = "e0003";

// internal reject, unknown cause (e.g. caught exception)
inline constexpr std::string_view caught_exception = "e0004";

inline constexpr std::string_view order_not_found = "e0103";

inline constexpr std::string_view duplicate_id = "e0104";

inline constexpr std::string_view bad_order_state = "e0105";

// GX new order reject
inline const char* const e0200 = "e0200";

// GX logon reject
inline const char* const e0201 = "e0201";

} // namespace apex::error
