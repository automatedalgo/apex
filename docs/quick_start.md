Quick Start
===========

In this tutorial we will look at how to run a simple demo strategy.

This demo strategy creates a single order, which is then cancelled a few seconds
later. The order is placed far away from the last trade price, with the
intention of not getting filled.

Although this demo is simple, it still serves to introduce some important
aspects of trading with Apex, including:

* understanding Apex log files

* the necessary boiler plate code to host a stand-alone strategy

* the most basic mechanism for triggering bot behaviour, which is to evaluate
  trading logic on regular timer callback

* how to determine order price and quantity, for example, choosing an order
  quantity that evaluates to a fixed USD amount

* how to place an order and how to cancel

* how to modify a strategy so that it goes from paper trading mode to live
  trading


## Generating instrument reference data


Apex strategies rely on reference data that describes the crypto instruments it
can trade, providing key details such as symbol, venue, type, rounding, and
minimum quantity. This data is read from a CSV file named "instruments.csv"
during Apex initialisation.

This instrument reference data must be generated before running an Apex
strategy.  If this file is not found, Apex will abort during start-up (with an
error like "failed to initialise instrument ref-data: Can not open file")

A Python script is provided to generate the reference data file; you can
invoke it by running commands like:

```
cd python
./generate-refdata.sh
```

On success the reference data will be written to:
`${APEX_HOME}/data/refdata/instruments/instruments.csv`.  If `APEX_HOME` is not
defined, it defaults to `~/apex`.

## Starting the demo strategy


Apex source code contains several demo programs.  The demo used in this tutorial
is `apex-demo-single-order.cpp`.  If Apex source has been successfully complied,
this demo can be run using these commands:

cd BUILD-DEBUG
./src/examples/standalone/apex-demo-single-order

The program will start, initialise and then simulate sending a single order to
Binance, followed shortly by simulated cancel.

## Understanding the log file

When operating Apex strategies it is important to become familiar with Apex log
files.

The log file is a primary diagnostic tool for understanding what a strategy is
doing at the current moment, what it did in the past, and detecting and
investigating warnings and errors that inevitably arise.  We will now go through
the log file associated with the demo program.

Immediately upon startup the program displays the Apex banner.  A critical point
to note is the run-mode.  This tells us whether the strategy is operating in
live trading mode (allowing it to trade with real money), or instead, is
operating in a safer mode of either paper trading or backtest.

The demo is programmed to operate in paper trading mode, which means it will
receive real time market data from Binance, but order send & cancel is
simulated.

```
   __ _ _ __   _____  __
  / _` | '_ \ / _ \ \/ /   mode: paper trading
 | (_| | |_) |  __/>  <
  \__,_| .__/ \___/_/\_\
       |_|

```

The program then continues to initialise, first loading the reference data file
that you should have earlier generated, and deciding which assets are good
proxies for USD.

```
INFO  | reading ref-data csv file '/home/svcquant/apex/data/refdata/instruments/instruments.csv'
INFO  | refdata loaded, 613 assets, 2977 instruments
INFO  | FX-rate asset:USDT
INFO  | FX-rate asset:BUSD
INFO  | FX-rate asset:USD
```

A useful sort of data found in log files is the location at which important
files are written.  Here the program logs where it will write persistence data
files:

```
NOTE  | using default persistence path '/home/svcquant/apex/persist'
```

The demo program is designed to be "stand-alone", which means it connects
directly to Binance itself, rather than connecting to an intermediate exchange
gateway process, so next it warns that it is not connecting to external gateways
(this is safe to ignore).

```
WARN  | no gateways configured
INFO  | listening for GX connections on 0.0.0.0:5780
```

The log file next shows that the program is configured to create a trading bot
for the instrument BTC/USDT.BNC, which is the spot coin pair BTC/USDT on
Binance.

```
INFO  | BTC/USDT.BNC (OneOrderDemoBot): bot created
INFO  | initialising bots
WARN  | no instrument position restored for BTC/USDT.BNC
INFO  | BTC/USDT.BNC (OneOrderDemoBot): initialising bot, startup-position:0
INFO  | subscribing to market data for BTC/USDT.BNC
```

The demo next creates the internal component to receive Binance spot market
data, including initialisation of websocket connections.

```
NOTE  | creating OrderRouter for exchange 'binance'
INFO  | *** binance-spot up (paper-trading-mode) ***
INFO  | attempting binance-spot market-data stream connection
INFO  | binance market-data channel: attempting websocket connection to 'stream.binance.com:9443/stream'
INFO  | binance market-data channel: websocket established
INFO  | binance-spot market-data channel connected
INFO  | connecting to 127.0.0.1:5780
```

The next log entry is the first from the actual bot layer of the program.  The
bot instance dedicated to trading the BTC/USD coin-pair is in a state of waiting
for market data (this is a basic health check; a bot cannot safely operate if
the market data is not in a good state):

```
WARN  | BTC/USDT.BNC (OneOrderDemoBot): waiting for market data
```

The engine continues to initialise, next setting up access to the order manager
component which handles sending of orders.

```
INFO  | received new GX connection from 127.0.0.1:34916
INFO  | connected to gx-server
WARN  | BTC/USDT.BNC (OneOrderDemoBot): waiting for market data
WARN  | no exchange subscription for BTCUSDT
INFO  | creating exchange subscription object
INFO  | om-logon accepted, strategyId: 'DEM01'
```

Next are the subscription activities for particular market data streams.
Shortly after this live market data will begin to arrive into the program.

```
INFO  | sending binance subscribe request for 'btcusdt@aggTrade', request: {"method": "SUBSCRIBE", "params": ["btcusdt@aggTrade"], "id": 1 }
INFO  | sending binance subscribe request for 'btcusdt@bookTicker', request: {"method": "SUBSCRIBE", "params": ["btcusdt@bookTicker"], "id":2}
INFO  | order-router logon successful for strategyId 'DEM01'
INFO  | binance websocket subscription started. Message:{"id":1,"result":null}
INFO  | binance websocket subscription started. Message:{"id":2,"result":null}
```

Soon after market data arrives into the program, the bot trading logic will
begin.

The bot is programmed to send only one order.  The next log line shows an order
has been constructed and sent.  The log entry shows the order state (SENT) and
other key details such as order ID, side, price and quantity.  The next log line
shows that this order then becomes LIVE, and that in addition to its order ID
(DEM01662adef300000000) it now has an external exchange order ID (4820506962).

```
INFO  | BTC/USDT.BNC (OneOrderDemoBot): order DEM01662adef300000000 SENT side:buy, price:63938.15, qty:0.00015, qtyUsd:9.59, qdone:0.0, pos:0, posUsd:0.0
INFO  | BTC/USDT.BNC (OneOrderDemoBot): order DEM01662adef300000000 LIVE side:buy, price:63938.15, qty:0.00015, qtyUsd:9.59, qdone:0.0, pos:0, posUsd:0.0, exchId:4820506962
```

The demo is programmed to quickly cancel the order.  Once the order is closed,
this event is written to the log file, showing the order state is now XCAN,
meaning closed-by-cancel.

```
INFO  | BTC/USDT.BNC (OneOrderDemoBot): order DEM01662adef300000000 XCAN side:buy, price:63938.15, qty:0.00015, qtyUsd:9.59, qdone:0.0, pos:0, posUsd:0.0, exchId:4820506962
```

At this point the demo has nothing more to do, so it can be shutdown (via
control-c or sig-kill), leading to log entries generated by the application
shutdown sequence.

```
INFO  | stopping bots
INFO  | waiting for bots to stop
INFO  | deleting bots
INFO  | connection lost to gx-server
```

## Annotated Source Code

Next follows the source code for this demo, `apex-demo-single-order.cpp`.

Trading bots are derived from the class `apex::Bot`.  This base class provides
various services, such as access to market data and order management.

### Bot constructor

The constructor call illustrates how an individual bot instance is responsible
for trading a single asset; the constructor takes an instrument object as a
parameter. The constructor is where the bot is also given a name (which later
appears in the the log file).

```c++
class OneOrderDemoBot : public apex::Bot
{
public:
  OneOrderDemoBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("OneOrderDemoBot", strategy, instrument) {}
  .
  .
  .
};
```


### Bot timer callback

An important `Bot` method is `on_timer`.  It is normally always overridden by
derived bot classes.  This method is called repeatedly by the Apex core engine,
allowing the bot to perform periodic activities.  Typically, a bot will use this
callback to examine the state of market data, the state of live orders, and then
decide on its next action, such as sending a new order or canceling an existing
one.

The demo implements a very simple decision in this callback.  If the bot doesn't have an order, it will attempt to create one; if it does have an order, it will branch to its cancellation logic.

```c++
void on_timer() override
{
  if (!_order)
    _create_and_send_order();
  else
    _cancel_existing_order();
}
```

### Bot order creation

Next is the order creation and sending logic.  This involves determination of
order attributes (price, quantity) and then invoking an asynchronous send
request.

The first few lines are safety checks, to ensure market data is in a good state
(live prices are available), and also that the bot is not shutting down.

Then comes order price and quantity calculations. In this example the order
quantity is chosen to be approximately $10.  The price is chosen to be 1 % away
from the last trade price, which is far enough to prevent order execution.

Finally the order object is created and its send method invoked.

```c++
void _create_and_send_order()
{
  if (!market_data_ok() || !has_fx_rate()) {
    LOG_WARN(ticker() << ": waiting for market data");
    return;
  }

  if (is_stopping())
    return;

  auto order_usd = 10.0;  // desired value of the order USD

  // choose price 1% away from last trade, so that it doesn't execute
  double price = round_price_passive(last_price() * 0.99, apex::Side::buy);

  // size the order quantity, based on target price and value
  double qty = round_size(order_usd / (price * fx_rate()));

  if (qty == 0) // don't send 0 qty orders
    return;

  // construct an order object (this does not cause it to be sent)
  _order = create_order(apex::Side::buy, qty, price, apex::TimeInForce::gtc);

  // send order to the exchange (this is an asynchronous operation)
  _order->send();
}
```

### Bot cancel creation

The private method `_cancel_existing_order` contains the order cancellation
logic.  This attempts to cancel the order if it has exceeded an age threshold,
and, if it is not already being cancelled.  The later check is important to
avoid repeated attempts to cancel the order, doing so might violate Binance
connection limits.

```c++
void _cancel_existing_order()
{
  if (!_order->is_closed_or_canceling()) {
    // The order is still 'live', so here we will manage it.  Our only
    // management logic is to cancel the order if it's been alive for too
    // long.
    if (_order->duration_live() > std::chrono::seconds(20)) {
      _order->cancel();
    }
  }
}
```


### Stand-alone main function

The final part of the code, within `main()`, is essentially boiler plate that
may be copy-pasted to other simple stand-alone strategies.

The first action is to create the engine core services, configuring them for
paper trading.  A parameter provided here specifies whether to configure for
paper trading or live trading mode.

```c++
auto services = apex::Services::create(apex::RunMode::paper);
```

The next few lines create an internal exchange-gateway component, which provides
market access services to the bot.  This component is then instructed to create
a Binance connection.  For live trading mode the user API key should be provided
here.

```c++
// create embedded exchange gateway
apex::GxServer gateway{services->realtime_evloop(), services->run_mode()};

// add binance
apex::BinanceSession::Params params;
gateway.add_venue(params);
```

The exchange gateway is now started, and the Apex core services are told to use
it for order routing.

```
gateway.start();
services->gateway_service()->set_default_gateway(gateway.get_listen_port());
```


The next few lines create a `Strategy` object that acts as a container for one
or many `Bot` instances.  In the current example there is just a single bot
trading BTC/UST, but there could be many, each bot trading a different
instrument.

Finally the strategy is told to initialise all its bots.

```c++
// create a Strategy object, which is a container for individual bots
apex::Strategy strategy(services.get(), "DEM01");

// add a bot, which is responsible for trading a single name
strategy.create_bot<OneOrderDemoBot>(apex::InstrumentQuery(
                                         "BTCUSDT",
                                         apex::ExchangeId::binance));

// initialise all bots, so they can begin trading
strategy.init_bots();
```

The final step is the run the Apex services main thread.  The strategy will now
run until user interruption.

```c++
// run until user presses control-c
services->run();
```
