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

#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/core/OrderRouter.hpp>
#include <apex/core/PersistenceService.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/core/Bot.hpp>
#include <apex/core/GatewayService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/Error.hpp>

#include <future>

namespace apex
{

Strategy::~Strategy() {
  this->stop();
}

std::set<std::string> Strategy::parse_flat_instruments_config()
{
  // configured names
  std::vector<std::string> symbols;
  auto universe_config = _config.get_sub_config("universe");
  for (size_t i = 0; i < universe_config.array_size(); i++) {
    symbols.push_back(universe_config.get_string(i));
  }

  if (symbols.empty())
    throw apex::ConfigError("universe is empty");

  // check for duplicate instruments
  std::set<std::string> unique_symbols(symbols.begin(), symbols.end());
  if (unique_symbols.size() != symbols.size())
    throw apex::ConfigError("universe contains duplicate entries");

  return unique_symbols;
}

void Strategy::stop_bots()
{
  assert(_services->evloop()->this_thread_is_ev());

  // stop bots, this will cause them to issue cancel order requests

  // dumb wait; better would be to check each bot if is still waiting
  // for orders to be canceled; however, we cannot wait forever
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // delete the bots
  LOG_INFO("deleting bots");
  for (auto& item : _bots) {
    item.second.release();
  }
}

void Strategy::stop()
{
  if (_services->is_backtest()) {
    LOG_INFO("stopping bots");
    for (auto& item : _bots)
      item.second->stop();

    LOG_INFO("deleting bots");
    for (auto& item : _bots)
      item.second.release();

    return;
  }

  // Note, this routine should not be called on the event thread. Reason is that
  // it performs a wait; this wait is to allow the event thread to be used by
  // other program components to make progress towards their shutdown state.
  // So, if we do ever need to run this operation on the event thread, we'd need
  // to rewrite this method, where each step below get queued as a sequence of
  // actions on the event thread; and definitely no dumb sleep!
  assert(not _services->evloop()->this_thread_is_ev());

  // call stop for each bot, will triggers order cancels. caution: this
  // consumes time on the EV thread.

  std::promise<void> promise_stop_bots;
  _services->evloop()->dispatch([&]() {
    LOG_INFO("stopping bots");
    for (auto& item : _bots)
      item.second->stop();
    promise_stop_bots.set_value();
  });
  promise_stop_bots.get_future().wait();


  // now wait for bots to complete their stop
  LOG_INFO("waiting for bots to stop");
  for (auto& item : _bots) {
    item.second->wait_for_stop();
  }

  // delete bots
  std::promise<void> delete_bots;
  _services->evloop()->dispatch([&]() {
    LOG_INFO("deleting bots");
    for (auto& item : _bots) {
      item.second.release();
    }
    delete_bots.set_value();
  });
  delete_bots.get_future().wait();
}

void Strategy::init_bots()
{
  // load all instrument positions for this strategy
  std::map<apex::Instrument, double> instrument_positions;
  for (auto& instrument_position :
       _services->persistence_service()->restore_instrument_positions(
           _strategy_id)) {
    LOG_INFO("GOT: " << instrument_position.native_symbol << ", "
                     << instrument_position.qty);
    apex::Instrument instrument = _services->ref_data_service()->get_instrument(
        instrument_position.native_symbol, instrument_position.exchange);
    instrument_positions.insert({instrument, instrument_position.qty});
  }

  // initialise all bots
  LOG_INFO("initialising bots");
  for (auto& item : _bots) {
    double init_instrument_position = 0.0;
    auto iter = instrument_positions.find(item.first);
    if (iter != std::end(instrument_positions)) {
      init_instrument_position = iter->second;
    } else {
      LOG_WARN("no instrument position restored for " << item.first);
    }
    item.second->init(init_instrument_position);
  }
}

void Strategy::add_bot(std::unique_ptr<Bot> bot) {
  const Instrument instrument = bot->instrument();
  auto iter = _bots.find(instrument);
  if (iter != std::end(_bots)) {
   THROW("cannot add duplicate bot for instrument " << instrument);
  }

  _bots.insert({instrument, std::move(bot)});
}

} // namespace apex
