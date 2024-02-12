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

#include <apex/model/Account.hpp>
#include <apex/model/Order.hpp>
#include <apex/model/tick_msgs.hpp>

#include <memory>
#include <mutex>

namespace apex
{
class OrderRouter;
class Order;
class Services;
class ClientOrderIdGenerator;
class FullUniqueOrderIdGenerator;

/* Responsible for tracking all orders created by the strategy */
class OrderService
{
public:
  explicit OrderService(Services*);
  ~OrderService();

  std::shared_ptr<Order> create(
    OrderRouter*, Instrument, Side, double size, double price,
    TimeInForce tif, const std::string& strategy_id, void* user_data,
    std::function<void(void*)> user_data_delete_fn);

  void route_fill_to_order(const std::string& order_id, OrderFill&);
  void route_update_to_order(const std::string& order_id, OrderUpdate&);

  std::shared_ptr<Order> find_order(const std::string& order_id);

private:
  Services* _services;
  std::unique_ptr<FullUniqueOrderIdGenerator> _order_id_src;
  std::map<std::string, std::shared_ptr<Order>> _orders;

  // Note: instead of just holding onto dead orders IDs, can hold on to the
  // actual orders, and retain them for, say 1 hour?
  std::map<std::string, Time> _dead_order_ids;
};

} // namespace apex
