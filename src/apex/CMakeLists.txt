list(APPEND LIB_APEX_SRC_FILES
  "backtest/SimExchange.cpp"
  "backtest/SimExchange.hpp"
  "backtest/TardisCsvParsers.cpp"
  "backtest/TardisCsvParsers.hpp"
  "backtest/TardisFileReader.cpp"
  "backtest/TardisFileReader.hpp"
  "backtest/TickFileWriter.cpp"
  "backtest/TickFileWriter.hpp"
  "backtest/TickReplayer.cpp"
  "backtest/TickReplayer.hpp"
  "backtest/TickbinFileReader.cpp"
  "backtest/TickbinFileReader.hpp"
  "backtest/TickbinMsgs.cpp"
  "backtest/TickbinMsgs.hpp"
  "gx/GxClientSession.cpp"
  "gx/GxClientSession.hpp"
  "gx/GxServerSession.cpp"
  "gx/GxServerSession.hpp"
  "gx/GxSessionBase.cpp"
  "gx/GxSessionBase.hpp"
  "gx/GxWireFormat.pb.cc"
  "gx/GxWireFormat.pb.h"
  "core/Alert.cpp"
  "core/Alert.hpp"
  "core/Auditor.cpp"
  "core/Auditor.hpp"
  "core/BacktestService.cpp"
  "core/BacktestService.hpp"
  "core/Bot.cpp"
  "core/Bot.hpp"
  "core/Errors.hpp"
  "core/GatewayService.cpp"
  "core/GatewayService.hpp"
  "core/Logger.cpp"
  "core/Logger.hpp"
  "core/MarketDataService.cpp"
  "core/MarketDataService.hpp"
  "core/OrderRouter.cpp"
  "core/OrderRouter.hpp"
  "core/OrderRouterService.cpp"
  "core/OrderRouterService.hpp"
  "core/OrderService.cpp"
  "core/OrderService.hpp"
  "core/PersistenceService.cpp"
  "core/PersistenceService.hpp"
  "core/RefDataService.cpp"
  "core/RefDataService.hpp"
  "core/Services.cpp"
  "core/Services.hpp"
  "core/Strategy.cpp"
  "core/Strategy.hpp"
  "core/StrategyMain.cpp"
  "core/StrategyMain.hpp"
  "core/TimeLogMemmap.cpp"
  "core/TimeLogMemmap.hpp"
  "core/TimeLogService.cpp"
  "core/TimeLogService.hpp"
  "core/common.cpp"
  "core/common.hpp"
  "gx/ExchangeSession.hpp"
  "gx/GxServer.cpp"
  "gx/GxServer.hpp"
  "net/common.hpp"
  "net/DecodeBuffer.cpp"
  "net/DecodeBuffer.hpp"
  "net/HttpParser.cpp"
  "net/HttpParser.hpp"
  "net/Reactor.cpp"
  "net/Reactor.hpp"
  "net/ReactorConfig.hpp"
  "net/SslSocket.cpp"
  "net/SslSocket.hpp"
  "net/TcpConnector.cpp"
  "net/TcpConnector.hpp"
  "net/TcpSocket.cpp"
  "net/TcpSocket.hpp"
  "net/WebsocketClient.cpp"
  "net/WebsocketClient.hpp"
  "net/WebsocketProtocol.cpp"
  "net/WebsocketProtocol.hpp"
  "net/ssl.cpp"
  "net/ssl.hpp"
  "model/Account.cpp"
  "model/Account.hpp"
  "model/ExchangeId.cpp"
  "model/ExchangeId.hpp"
  "model/Instrument.cpp"
  "model/Instrument.hpp"
  "model/MarketData.cpp"
  "model/MarketData.hpp"
  "model/Order.cpp"
  "model/Order.hpp"
  "model/Position.cpp"
  "model/Position.hpp"
  "model/StrategyId.cpp"
  "model/StrategyId.hpp"
  "model/tick_msgs.hpp"
  "util/AllocTracker.cpp"
  "util/AllocTracker.hpp"
  "util/BacktestEventLoop.cpp"
  "util/BacktestEventLoop.hpp"
  "util/BufferedFileReader.hpp"
  "util/Config.cpp"
  "util/Config.hpp"
  "util/Error.hpp"
  "util/EventLoop.cpp"
  "util/EventLoop.hpp"
  "util/GzFile.cpp"
  "util/GzFile.hpp"
  "util/JsonWriter.hpp"
  "util/RealtimeEventLoop.cpp"
  "util/RealtimeEventLoop.hpp"
  "util/StopFlag.cpp"
  "util/StopFlag.hpp"
  "util/Time.cpp"
  "util/Time.hpp"
  "util/TimeLog.cpp"
  "util/TimeLog.hpp"
  "util/json.cpp"
  "util/json.hpp"
  "util/platform.cpp"
  "util/platform.hpp"
  "util/rx.hpp"
  "util/utils.cpp"
  "util/utils.hpp"
  "venues/binance/BinanceFeedHandler.cpp"
  "venues/binance/BinanceFeedHandler.hpp"
  "venues/binance/BinanceLineHandler.cpp"
  "venues/binance/BinanceLineHandler.hpp"
  "venues/binance/BinanceUsdFutFeedHandler.cpp"
  "venues/binance/BinanceUsdFutFeedHandler.hpp"
  "venues/binance/BinanceUsdFutLineHandler.cpp"
  "venues/binance/BinanceUsdFutLineHandler.hpp"
  "venues/binance/binance_common.cpp"
  "venues/binance/binance_common.hpp"
  "venues/bybit/BybitFeedHandler.cpp"
  "venues/bybit/BybitFeedHandler.hpp"
  "venues/kucoin/KucoinFutFeedHandler.cpp"
  "venues/kucoin/KucoinFutFeedHandler.hpp"
  "venues/venues_common.cpp"
  "venues/venues_common.hpp"
)


file(GLOB LIB_3RDPTY_HTTPPARSER_SRC_FILES ${PROJECT_SOURCE_DIR}/3rdparty/http_parser/*.c)
file(GLOB LIB_3RDPTY_APACH_SRC_FILES ${PROJECT_SOURCE_DIR}/3rdparty/apache/*.c)

list(APPEND 3RDPTY_SIMDJSON
  "${PROJECT_SOURCE_DIR}/3rdparty/simdjson/simdjson.h"
  "${PROJECT_SOURCE_DIR}/3rdparty/simdjson/simdjson.cpp")

##
## Static library
##
if (BUILD_STATIC_LIBS)
    add_library(apexcore_static STATIC
            ${LIB_APEX_SRC_FILES}
            ${LIB_3RDPTY_HTTPPARSER_SRC_FILES}
            ${LIB_3RDPTY_APACH_SRC_FILES}
            ${3RDPTY_SIMDJSON}
          )

    set_target_properties(apexcore_static PROPERTIES OUTPUT_NAME apexcore)

    target_include_directories(apexcore_static
            PUBLIC
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            PRIVATE
            ${LIBCURL_INCLUDE_DIRS}
            ${LIBPROTOBUF3_INCLUDE_DIRS}
            )

    target_link_libraries(apexcore_static
            PUBLIC
            sodium
            curl
            protobuf3
            OpenSSL::SSL
            ZLIB::ZLIB)

    list(APPEND TO_INSTALL apexcore_static)
endif ()

##
## Shared library
##

if (BUILD_SHARED_LIBS)
    add_library(apexcore_shared SHARED
            ${LIB_APEX_SRC_FILES}
            ${LIB_3RDPTY_HTTPPARSER_SRC_FILES}
            ${LIB_3RDPTY_APACH_SRC_FILES}
            ${3RDPTY_SIMDJSON})

    # Set library output name
    set_target_properties(apexcore_shared PROPERTIES OUTPUT_NAME apexcore)

    # Set library API version
    set_property(TARGET apexcore_shared PROPERTY VERSION "6.0.0")
    set_property(TARGET apexcore_shared PROPERTY SOVERSION 6)

    target_include_directories(apexcore_shared
            PRIVATE
            ${PROJECT_SOURCE_DIR}/3rdparty
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            ${LIBCURL_INCLUDE_DIRS}
            ${LIBPROTOBUF3_INCLUDE_DIRS}
            )

    target_link_libraries(apexcore_shared
            PUBLIC
            sodium
            curl
            protobuf3
            OpenSSL::SSL
            ZLIB::ZLIB)

    list(APPEND TO_INSTALL apexcore_shared)
endif ()

##
## Install targets
##
install(TARGETS ${TO_INSTALL}
        RUNTIME DESTINATION "${INSTALL_BIN_DIR}" COMPONENT lib
        BUNDLE DESTINATION "${INSTALL_BIN_DIR}" COMPONENT lib
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}" COMPONENT lib
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}" COMPONENT lib)

install(FILES ${INSTALL_HDRS}
        DESTINATION "${INSTALL_INC_DIR}/apexcore"
        COMPONENT dev)
