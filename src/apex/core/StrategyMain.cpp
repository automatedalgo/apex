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

#include <apex/core/Bot.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/OrderRouter.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/core/StrategyMain.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/Error.hpp>

#include <iostream>

#include <csignal>

namespace apex
{

static Config _load_config(const std::string& filename)
{
  try {
    json raw_config = apex::read_json_config_file(filename);
    return apex::Config(raw_config);
  } catch (const std::exception& e) {
    THROW("error reading config file '" << filename << "': " << e.what());
  }
}


StrategyMain::StrategyMain(const StrategyFactoryBase& factory,
                           std::string config_file)
  : config_file(std::move(config_file)), factory(factory)
{
  // TODO: check the config file exists, throw if not
}


void StrategyMain::start(std::future<int>& interrupt_code)
{
  apex::Logger::instance().set_is_configured(false);
  apex::Logger::instance().register_thread_id("main");

  apex::Config root_config = _load_config(this->config_file);

  apex::Logger::configure_from_config(root_config.get_sub_config("logging", Config{}));
  LOG_NOTICE("application config file '" << this->config_file << "'");

  auto run_mode = root_config.get_string("run_mode");

  // set up apex services
  _services = std::make_unique<apex::Services>(parse_run_mode(run_mode));
  _services->init_services(root_config.get_sub_config("services"));

  auto strategy_config = root_config.get_sub_config("strategy");

  auto strategy = this->factory.create(strategy_config, _services.get());

  if (!strategy)
    throw std::runtime_error(
        "strategy factory did not create a strategy instance");

  strategy->create_bots();

  strategy->init_bots();

  interrupt_code.wait();

  if (interrupt_code.get() == 1) {
    LOG_INFO("control-c pressed, strategy will stop");
  }

  LOG_INFO("*** strategy stopping ***");

  strategy->stop();
}


struct cmd_line_args {
  std::string config_file;
};


static void die(const char* msg)
{
  std::cerr << "error: " << msg << "\n";
  std::cerr << "try --help\n";
  exit(1);
}


static void help()
{
  std::cout << "options" << std::endl;
  std::cout << " --config CONFIG    config file" << std::endl;
  exit(0);
}


static cmd_line_args parse_args(int argc, char** argv)
{
  cmd_line_args args;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--config") == 0) {
      if (++i >= argc)
        die("missing arg, config file");
      args.config_file = argv[i];
    } else if ((strcmp(argv[i], "-h") == 0) ||
               (strcmp(argv[i], "--help") == 0)) {
      help();
    }
  }

  if (args.config_file.empty())
    throw std::runtime_error("config file not specified");
  return args;
}

static std::promise<int> interrupt_code;
static bool interrupt_invoked = false;


static void interrupt_handler(int)
{
  if (!interrupt_invoked) {
    interrupt_invoked = true;
    interrupt_code.set_value(1);
  }
}


/* This function can serve as the C++ main for apex strategies.  It will parse
 * command line arguments, load a config file and create and start a strategy
 * using a provided strategy factory.
 */
int strategy_runner(int argc, char** argv, const StrategyFactoryBase& factory)
{
  // install control-c signal handler
  struct sigaction newsigact = {};
  memset(&newsigact, 0, sizeof(newsigact));
  newsigact.sa_handler = interrupt_handler;
  sigaction(SIGINT, &newsigact, nullptr);

  // TODO: allow strateyMain to parse args?
  std::future<int> prodResult = interrupt_code.get_future();

  // parse command line arguments
  cmd_line_args args;
  try {
    args = parse_args(argc, argv);
  } catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "unknown error during arg parse" << std::endl;
    return 1;
  }


  try {
    apex::StrategyMain strategy_main(factory, args.config_file);
    strategy_main.start(prodResult);
    return 0;
  }

  catch (apex::ConfigError& e) {
    if (apex::Logger::instance().is_configured())
      LOG_ERROR("config-error: " << e.what());
    std::cerr << "config-error: " << e.what() << std::endl;
  }

  catch (std::exception& e) {
    if (apex::Logger::instance().is_configured())
      LOG_ERROR(e.what());
    std::cerr << e.what() << std::endl;
  }

  return 1;
}


} // namespace apex
