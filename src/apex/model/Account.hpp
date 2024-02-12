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

#include <apex/core/RefDataService.hpp>

#include <map>
#include <mutex>
#include <ostream>
#include <vector>


namespace apex
{

struct AccountUpdate {
  Asset asset;
  double avail;
};

/* Represent trading account/wallet balance, that is a container for our holding
 * of various Assets*/
class Account
{

public:
  void apply(AccountUpdate);

  void apply(const std::vector<AccountUpdate>&);

  std::map<Asset, double> data() const;

private:
  mutable std::mutex _mutex;
  std::map<Asset, double> _holdings;
};

} // namespace apex
