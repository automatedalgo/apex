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

#include <apex/util/utils.hpp>
#include <apex/core/TimeLogService.hpp>
#include <apex/core/Logger.hpp>

#include <iostream>
#include <filesystem>
#include <map>
#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace apex;

struct Stats
{
  double min = 0.0;
  double max = 0.0;
  double mean = 0.0;
  double pct25= 0.0;
  double pct50 = 0.0;
  double pct90 = 0.0;
  double pct95 = 0.0;
  double pct99 = 0.0;
};


struct Series
{
  explicit Series(std::string_view name = ""):
    name(name)
  {
  }

  bool is_all_zero() const {
    return std::all_of(values.begin(), values.end(), [](double x) { return x == 0.0; });
  }

  Series& operator-=(const Series& rhs) {
    for (size_t i = 0; i < values.size(); i++)
      this->values[i] = this->values[i] - rhs.values[i];
    return *this;
  }

  double percentile(const std::vector<double>& sorted, double p) {
    size_t n = sorted.size();
    if (n == 0) return NAN;
    size_t idx = static_cast<size_t>(std::round(p * (n - 1)));
    return sorted[idx];
  }

  Stats summary() {
    Stats stats;
    stats.min = values[0];
    stats.max = values[0];
    stats.mean = 0.0;
    for (size_t i = 0; i < values.size(); i++) {
      stats.mean += values[i];
      stats.min = std::min(stats.min, values[i]);
      stats.max = std::max(stats.max, values[i]);
    }
    stats.mean /= values.size();

    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());

    stats.pct25 = percentile(sorted, 0.25);
    stats.pct50 = percentile(sorted, 0.50);
    stats.pct90 = percentile(sorted, 0.9);
    stats.pct95 = percentile(sorted, 0.95);
    stats.pct99 = percentile(sorted, 0.99);
    return stats;
  }

  void push_back(double d) { values.push_back(d); }

  size_t size() const { return values.size(); }

  void clear() { values.clear(); }

  std::vector<double> values;
  std::string name;
};


class Frame
{
public:
  template <typename... Args>
  void append(Args... args) {
    // Use a fold expression to apply an action to each arg
    ((handle(std::move(args))), ...);
  }

  const Series& operator[](size_t i) const { return series[i]; }

  size_t columns() const { return series.size(); }
  size_t rows() const { return series[0].size(); }

  double iloc(size_t r, size_t c) const { return series[c].values[r];}

  std::vector<Series> series;

private:
  template <typename T>
  void handle(T&& obj) {
    if (!series.empty() && series[0].size() != obj.size())
      throw std::runtime_error("Frame: cannot add Series of different size");
    series.push_back(std::move(obj));
  }
};


unsigned int calc_column_width(const Series& s, int dp) {
  auto min = s.values[0];
  auto max = s.values[0];
  for (auto v :  s.values) {
    min = std::min(min, v);
    max = std::max(max, v);
  }
  auto min_str = format_double(min, false, dp);
  auto max_str = format_double(max, false, dp);
  return std::max(min_str.size(), max_str.size());
}


void dump_frame(const Frame& f, int dp)
{
  // calculate the width required by column, depending on their content
  int padding = 1;
  std::vector<int> widths(f.columns()+1, 0);

  widths[0] = 2 + padding;
  for (size_t i = 0; i < f.columns(); i++ )
    widths[i+1] = calc_column_width(f.series[i], dp)+padding;

  // build the horiz border
  std::ostringstream hb;
  hb << "   " <<  std::setfill('-');
  for (size_t c=0; c < f.columns(); c++)
    hb << (c==0? "+": "") << std::setw(widths[c]) << "" << "+";

  std::cout << hb.str() << std::endl; // horiz border

  // header
  for (size_t c=0; c < f.columns()+1; c++) {
    if (c==0)
      std::cout << "|";
    std::cout << std::setw(widths[c]) << f[c].name << "|";
  }
  std::cout << std::endl;

  std::cout << hb.str() << std::endl; // horiz border

  // rows
  std::cout << std::setfill(' ');
  for (size_t r=0; r < f.rows(); r++) {
    std::cout << " t"<< r ;
    for (size_t c=0; c < f.columns(); c++) {
      if (c==0)
        std::cout << "|";
      std::cout << std::setw(widths[c]) << format_double(f.iloc(r,c), false, dp)
                << "|";
    }
    std::cout << std::endl;
  }

  std::cout << hb.str() << std::endl; // horiz border
}


int main(int argc, char** argv)
{
  apex::Logger::instance().set_level(apex::Logger::warn);

  try {
    if (argc < 2) {
      std::cout << "please provide path to time-log memmap file" << std::endl;
      return 1;
    }
    std::filesystem::path  filename = argv[1];

    bool show_info = true;
    bool show_offsets = true;
    bool show_stats = true;

    TimeLogMemMap mm{filename, false};

    // count rows which have data
    int rows_used = 0;
    for (size_t i = 0; i < mm.data()->header.row_capacity ; i++) {
      TimingRecord * rec = & mm.data()->records[i];
      if (!rec->tp[0])
        break;
      rows_used++;
    }

    if (show_info) {
      std::cout << "version: " << mm.data()->header.preamble << std::endl;
      std::cout << "capacity: " << mm.data()->header.row_capacity << std::endl;
      std::cout << "rows_used: " << rows_used << std::endl;
    }

    // {
    //   std::cout << "DATA: " ;
    //   for (auto j = 0; j < TimingRecord::capacity; j++)
    //     if (!data[j].is_all_zero())
    //       std::cout << format_double(data[j].values[0], true, 1)<< ", ";
    //   std::cout << std::endl;
    // }

    // load the raw data into a set of Series
    std::vector<Series> raw(TimingRecord::tp_capacity);
    for (size_t r = 0; r < mm.data()->header.row_capacity; r++) {
      TimingRecord * rec = & mm.data()->records[r];
      if (!rec->tp[0])
        break;  // reached end of used rows
      for (auto c = 0; c < TimingRecord::tp_capacity; c++)
        raw[c].values.push_back(rec->tp[c]);
    }

    // drop any columns that are empty
    std::vector<Series> nonzero;
    nonzero.reserve(raw.size());
    for (auto & s : raw)
      if (s.is_all_zero())
        s.clear();
      else
        nonzero.push_back(std::move(s));
    raw.clear();

    // calculate the time offets between each time mark
    for (auto c = std::size(nonzero)-1; c > 0; c--)
      nonzero[c] -= nonzero[c-1];

    // for each latency measurement, calc distribution states
    if (show_stats) {
      Series min("min");
      Series p50("p50");
      Series p90("p90");
      Series p95("p95");
      Series p99("p99");
      Series max("max");
      for (size_t c = 1; c < std::size(nonzero); c++) {
        auto stats = nonzero[c].summary();
        min.push_back(stats.min/1000.0);
        p50.push_back(stats.pct50/1000.0);
        p90.push_back(stats.pct90/1000.0);
        p95.push_back(stats.pct95/1000.0);
        p99.push_back(stats.pct99/1000.0);
        max.push_back(stats.max/1000.0);
    }

      //  display the results
      Frame frame;
      frame.append(min, p50, p90, p95, p99, max);
      dump_frame(frame, 1);
      std::cout << "rows: " << rows_used << std::endl;
    }

    // print all data
    if (false) {
      for (size_t r = 0; r < mm.data()->header.row_capacity; r++) {
        TimingRecord * rec = & mm.data()->records[r];
        if (!rec->tp[0])
          break;

        std::cout << rec->msgid;
        std::cout << ","  << rec->tp[0];
        for (auto j = 1; j < 6; j++) {
          std::cout << ",";
          if (show_offsets)
            std::cout << format_double((rec->tp[j] - rec->tp[j-1])/1000.0, true, 1);
          else
            std::cout << rec->tp[j];
        }
        std::cout << std::endl;
      }
    }
  } catch (std::exception& e) {
    std::cout << "error: " << e.what() << std::endl;
    return 1;
  }
}
