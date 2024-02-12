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

#include <functional>
#include <mutex>
#include <vector>

namespace apex::rx
{

/*
  Hugely simplified version of RX Streams
 */

/* Consumer of a value. A concrete provider must be aware that any thread call
 * invoke the on_next; there are no threading guarantees. */
template <typename T> struct observer {

  std::function<void(const T&)> on_next;

  observer(std::function<void(const T&)> fn) : on_next(std::move(fn)) {}
};


template <typename T> class observable
{
public:
  using observable_type = observer<T>;
  using observer_cb = std::function<void(const T&)>;

  virtual ~observable() = default;

  virtual void subscribe(observer_cb fn)
  {
    auto lock = std::scoped_lock(m_mutex);
    _subscribe_nolock(std::move(fn));
  }

  size_t size() const
  {
    auto lock = std::scoped_lock(m_mutex);
    return std::size(m_observers);
  }

protected:
  void _subscribe_nolock(observer_cb fn) { m_observers.push_back(fn); }

  void notify(const T& v)
  {
    auto lock = std::scoped_lock(m_mutex);
    this->broadcast_nolock(v);
  }

  void broadcast_nolock(const T& v)
  {
    for (auto& item : m_observers)
      try {
        item.on_next(v);
      } catch (...) {
        std::terminate();
      }
  }

  mutable std::mutex m_mutex;
  std::vector<observable_type> m_observers;
};


template <typename T> class subject : public observable<T>
{
public:
  subject() = default;

  void next(const T& v) { this->notify(v); }

private:
  subject(const subject&) = delete;
  subject& operator=(const subject&) = delete;
};


template <typename T> class behaviour_subject : public observable<T>
{

public:
  using value_type = T;
  using Dispatcher =
      std::function<void(std::function<void(const T&)>, const T&)>;

  behaviour_subject(Dispatcher dispatcher, T value = T{})
    : _dispatcher(std::move(dispatcher)), _value(std::move(value))
  {
  }

  virtual void subscribe(typename observable<T>::observer_cb fn) override
  {
    auto lock = std::scoped_lock(this->m_mutex);
    _dispatcher(fn, _value);
    this->_subscribe_nolock(std::move(fn));
  }

  void next(T v)
  {
    auto lock = std::scoped_lock(this->m_mutex);
    _value = std::move(v);
    this->broadcast_nolock(_value);
  }


private:
  Dispatcher _dispatcher;
  T _value;
};

} // namespace apex::rx
