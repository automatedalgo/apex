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

#include <apex/gx/BinanceSession.hpp>
#include <apex/gx/GxServer.hpp>
#include <apex/util/Config.hpp>

#include <unistd.h>
#include <vector>

static void help()
{
  std::cout << "options" << std::endl;
  std::cout << " -c CONFIG    config file" << std::endl;
  exit(0);
}

static void die(const char* msg)
{
  std::cerr << "error: " << msg << "\n";
  std::cerr << "try --help\n";
  exit(1);
}

struct cmd_line_args {
  std::string config_file;
};


static cmd_line_args parse_args(int argc, char** argv)
{
  cmd_line_args args;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
      if (++i >= argc)
        die("missing arg, config file");
      args.config_file = argv[i];
    } else if (strcmp(argv[i], "-h") == 0) {
      help();
    }
  }

  if (args.config_file.empty())
    throw std::runtime_error("config file not specified");
  return args;
}


int main(int argc, char** argv)
{
  try {
    // process command line and load config
    auto args = parse_args(argc, argv);
    auto config = apex::Config{apex::read_json_config_file(args.config_file)};

    // setup logging
    apex::Logger::instance().register_thread_id("main");
    apex::Logger::configure_from_config(
      config.get_sub_config("logging", apex::Config::empty_config()));

    // hard-code the run-mode based on the built-binary, since this is too
    // important a setting to try to control from configuration
#ifdef APEX_GX_RUN_MODE_PAPER
    auto run_mode = apex::RunMode::paper;
#else
    auto run_mode = apex::RunMode::live;
#endif

    LOG_INFO("application run_mode: " << run_mode);

    apex::GxServer app{run_mode, config};
    app.start();

    while (true) {
      sleep(60);
    }

    return 0;

  } catch (apex::ConfigError& e) {
    LOG_ERROR("config-error: " << e.what());
  } catch (std::exception& e) {
    LOG_ERROR("error: " << e.what());
  }

  return 1;
}
