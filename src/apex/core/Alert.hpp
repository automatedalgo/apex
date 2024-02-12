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

#include <set>
#include <string>

namespace apex
{

class Alert
{

public:
  Alert() = default;

  explicit Alert(std::string id) : _id(std::move(id)) {}

  bool operator<(const Alert& rhs) const { return _id < rhs._id; }

  [[nodiscard]] const std::string& id() const { return _id; }

private:
  std::string _id;
};

std::ostream& operator<<(std::ostream& os, const Alert& x);

// Collection of Alerts
class AlertBoard
{
public:
  void add(Alert a) { _alerts.insert(std::move(a)); }
  void remove(Alert a)
  {
    auto iter = _alerts.find(a);
    if (iter != _alerts.end())
      _alerts.erase(iter);
  }

  bool empty() const { return _alerts.empty(); }

  void log();

private:
  std::set<Alert> _alerts;
};


}
