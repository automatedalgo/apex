# Apex

Algorithmic trading and execution platform

## Overview

Apex is an open-source, high-performance software framework for building
algorithmic trading strategies.  It is aimed at experienced quant traders who
need a platform for trading highly responsive tick-by-tick systematic
strategies, across various instruments and exchanges.

## Features

- **Production grade** -- Apex has evolved from earlier versions developed for
  banking and hedge fund clients.  This drives a design and implementation that
  is fast, robust and scalable.

- **Fast** -- being written in modern C/C++ means lowest possible tick-to-trades
  are achievable, supporting some classes of low latency strategies.

- **Unified backtest/paper/live** -- strategies built in Apex can be deployed,
  without modification, into backtest, paper trading and live trading
  environments.

- **Flexible** -- single Apex strategies can concurrently trade multiple assets
  at multiple-venues.

- **Modular** -- Apex has a modular design, meaning core engine, control-gui and
  market-access components are deployed as separate inter-communicating
  processes. This allows for multiple algo engines to reuse market access
  components.

- **Linux** -- Apex is compiles for the Linux operating systems, although the
code can be ported, in principle, to support Windows.

## How to get started


To get started with Apex, you can begin by following the installation guide,
which covers how to setup a Linux box to support C++ development, and then
follow the instructions to build the Apex code.

Once built there are a couple of basic demos that show how market data can be
consumed and orders generated.

## Caution!

Running algo strategies is inherently dangerous.  You can quickly lose all of
your money invested. Bugs in either the core platform code, or your strategy
code, or network issues, or elsewhere, can lead to financial loss.  You should
only use Apex for trading purposes if you are an experience software developer
and have significant trading experience.
