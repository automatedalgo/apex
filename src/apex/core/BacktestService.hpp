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

#include <apex/util/Time.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/core/OrderRouter.hpp>

#include <list>
#include <memory>
#include <map>

namespace apex
{

class Services;
class Instrument;
class MarketData;
class TickReplayer;
class BacktestService;
class Instrument;


class SimOrder {
public:
  using OnFillFn = std::function<void(double size, bool fully_filled)>;

  SimOrder(Order&,
           std::string ext_order_id,
           double size,
           double price,
           Side side);

  Side side() const { return _side; }
  double size() const { return _size; }
  double price() const { return _price; }
  double size_remain() const { return _size_remain; }
  void apply_fill(double d) {
    _size_remain -= d;
  }

  std::weak_ptr<Order>& orig_order() { return _order; };

  bool is_fully_filled() const;

  const std::string& ext_order_id() const { return _ext_order_id; }

private:
  std::string _symbol;
  std::string _ext_order_id;
  double _size;
  double _price;
  Side _side;
  double _size_remain;
  std::weak_ptr<Order> _order;
  OnFillFn _on_fill_fn;
};


class SimLadder {
public:
  SimLadder(Services *, const Instrument&);

  std::shared_ptr<SimOrder> add_order(Order&, std::string ext_order_id);
  void remove_order(std::shared_ptr<SimOrder>&);

  void apply_trade(double price, double size);

private:
  bool erase_order(std::shared_ptr<SimOrder>&);
  void raise_fill_event(double fill_size,
                        bool fully_filled, std::shared_ptr<SimOrder> & order);

private:
  Services* _services;
  MarketData* _mkt = nullptr;
  Instrument _instrument;

  using HalfOrderBook = std::multimap<double, std::shared_ptr<SimOrder>>;
  HalfOrderBook _bids;
  HalfOrderBook _asks;
};


class SimExchange : public OrderRouter
{
public:
  SimExchange(Services*);

  void send_order(Order&) override;
  void cancel_order(Order&) override;
  bool is_up() const override;

  void add_ladder(const Instrument&);

private:
  using ExtOrderId = std::string;
  Services* _services;
  std::map<ExtOrderId, std::shared_ptr<SimOrder>> _all_orders;
  std::map<Instrument, std::unique_ptr<SimLadder>> _ladders;
};


class BacktestService
{
public:
  BacktestService(Services*, apex::Time replay_from, apex::Time replay_upto);
  ~BacktestService();
  void subscribe_canned_data(const Instrument&, MarketData*, MdStreamParams stream_params);

  OrderRouter* get_order_router(const Instrument&);

private:

  void create_tick_replayer(const Instrument& instrument,
                            MarketData* mktdata,
                            MdStream stream_type);

  Services* _services;
  apex::Time _from;
  apex::Time _upto;
  std::list<Time> _dates;

  std::map<std::pair<Instrument, MdStream>,
           std::unique_ptr<TickReplayer>> _replayers;

  std::map<ExchangeId, std::unique_ptr<SimExchange>> _exchanges;
};


} // namespace apex
