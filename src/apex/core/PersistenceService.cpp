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

#include <apex/core/PersistenceService.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/Time.hpp>
#include <apex/util/json.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/utils.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace apex
{


void write_file(std::filesystem::path path, std::string content)
{
  auto tmp_path = path;
  tmp_path += ".tmp";

  // LOG_INFO("writing file " << tmp_path);
  auto file = std::ofstream(tmp_path);
  file << content;
  file.close();

  // LOG_INFO("renaming file " << tmp_path << " -> " << path);
  fs::rename(tmp_path, path);
}

PersistenceService::PersistenceService(Services* services) : _services(services)
{
  auto default_path = services->paths_config().fdb;

  Config config = services->config().get_sub_config("persist", Config::empty_config());
  _persist_path = config.get_string("path", "");
  if (_persist_path == "") {
    _persist_path = default_path;
    LOG_NOTICE("using default persistence path " << QUOTE(_persist_path));
  }
}


std::vector<RestoredPosition> PersistenceService::restore_instrument_positions(
    std::string strategy_id)
{
  auto app_name = "apex";
  auto table_name = "instrument_positions";

  fs::path path = _persist_path;

  path /= app_name;
  path /= table_name;

  create_dir(path);
  std::vector<RestoredPosition> records;

  for (const auto& entry : fs::directory_iterator(path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {

      auto tokens = split(entry.path().filename().c_str(), '.');
      if (std::size(tokens) == 4) {
        if (tokens[0] == strategy_id) {
          auto raw = read_json_file(entry.path().string());
          RestoredPosition record;
          record.strategy_id = raw["strategyid"].get_ref<const std::string&>();
          record.exchange = raw["exchange"].get_ref<const std::string&>();
          record.native_symbol = raw["symbol"].get_ref<const std::string&>();
          record.qty = raw["qty"].get<double>();
          records.push_back(std::move(record));
        }
      } else {
        LOG_WARN("skipping position file with unexpected name-format: "
                 << entry.path());
      }
    }
  }

  return records;
}


void PersistenceService::persist_instrument_positions(
    std::string algo_id, const Instrument& instrument, double qty)
{
  // construct the record
  json record;
  record["exchange"] = instrument.exchange_id();
  record["symbol"] = instrument.native_symbol();
  record["strategyid"] = algo_id;
  record["ts"] = _services->now().as_iso8601();
  record["qty"] = format_double(qty, true);

  auto content = to_string(record);
  std::ostringstream os;
  os << algo_id << "." << instrument.exchange_id() << "."
     << instrument.native_symbol();

  // construct the record key
  auto record_key = os.str();

  const auto json_ext = "json";

  auto app_name = "apex";
  auto table_name = "instrument_positions";

  // fdb table path
  fs::path fdb_home{_persist_path};
  auto dir = fdb_home / app_name / table_name;
  auto record_path = dir / (record_key + "." + json_ext);

  create_dir(dir);

  write_file(record_path, content);
}

} // namespace apex
