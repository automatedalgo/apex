list(APPEND LIB_APEX_SRC_FILES
        "util/platform.hpp"
        "util/platform.cpp"
        "core/Logger.hpp"
        "core/Logger.cpp"
        "infra/DecodeBuffer.hpp"
        "infra/DecodeBuffer.cpp"
        "util/Config.hpp"
        "util/Config.cpp"
        "core/Alert.hpp"
        "core/Alert.cpp"
        "model/tick_msgs.hpp"
        "util/BufferedFileReader.hpp"
        "util/rx.hpp"
        "util/utils.hpp"
        "util/utils.cpp"
        "util/Error.hpp"
        "util/GzFile.hpp"
        "util/GzFile.cpp"
        "util/Time.hpp"
        "util/Time.cpp"
        "util/json.hpp"
        "util/json.cpp"
        "infra/SocketAddress.hpp"
        "infra/SocketAddress.cpp"
        "infra/UvErr.hpp"
        "infra/UvErr.cpp"
        "util/StopFlag.hpp"
        "util/StopFlag.cpp"
        "util/EventLoop.cpp"
        "util/EventLoop.hpp"
        "util/RealtimeEventLoop.hpp"
        "util/RealtimeEventLoop.cpp"
        "util/BacktestEventLoop.hpp"
        "util/BacktestEventLoop.cpp"
        "core/Services.hpp"
        "core/Services.cpp"
        "infra/IoLoop.hpp"
        "infra/IoLoop.cpp"
        "infra/TcpSocket.hpp"
        "infra/TcpSocket.cpp"
        "infra/ssl.hpp"
        "infra/ssl.cpp"
        "infra/SslSocket.hpp"
        "infra/SslSocket.cpp"
        "infra/HttpParser.hpp"
        "infra/HttpParser.cpp"
        "infra/WebsocketProtocol.hpp"
        "infra/WebsocketProtocol.cpp"
        "infra/WebsocketppImpl.hpp"
        "infra/WebsocketppImpl.cpp"
        "infra/WebsocketClient.hpp"
        "infra/WebsocketClient.cpp"
        "comm/GxSessionBase.hpp"
        "comm/GxSessionBase.cpp"
        "comm/GxClientSession.hpp"
        "comm/GxClientSession.cpp"
        "comm/GxServerSession.hpp"
        "comm/GxServerSession.cpp"
        "comm/GxWireFormat.pb.h"
        "comm/GxWireFormat.pb.cc"
        "model/MarketData.hpp"
        "model/MarketData.cpp"
        "model/Account.hpp"
        "model/Account.cpp"
        "model/Position.hpp"
        "model/Position.cpp"
        "model/ExchangeId.hpp"
        "model/ExchangeId.cpp"
        "model/Order.hpp"
        "model/Order.cpp"
        "model/Instrument.hpp"
        "model/Instrument.cpp"
        "model/StrategyId.hpp"
        "model/StrategyId.cpp"
        "core/BacktestService.hpp"
        "core/BacktestService.cpp"
        "core/OrderRouterService.hpp"
        "core/OrderRouterService.cpp"
        "core/OrderService.hpp"
        "core/OrderService.cpp"
        "core/PersistenceService.hpp"
        "core/PersistenceService.cpp"
        "core/RefDataService.hpp"
        "core/RefDataService.cpp"
        "core/Bot.hpp"
        "core/Bot.cpp"
        "core/GatewayService.hpp"
        "core/GatewayService.cpp"
        "core/MockMatchingEngine.hpp"
        "core/MockMatchingEngine.cpp"
        "core/OrderRouter.hpp"
        "core/OrderRouter.cpp"
        "core/Strategy.hpp"
        "core/Strategy.cpp"
        "core/StrategyMain.hpp"
        "core/StrategyMain.cpp"
        "backtest/TickReplayer.hpp"
        "backtest/TickReplayer.cpp"
        "backtest/TickFileReader.hpp"
        "backtest/TickFileReader.cpp"
        "backtest/TickFileWriter.hpp"
        "backtest/TickFileWriter.cpp"
        "backtest/TardisCsvParsers.hpp"
        "backtest/TardisCsvParsers.cpp"
        "backtest/TardisFileReader.hpp"
        "backtest/TardisFileReader.cpp"
        "backtest/TickbinMsgs.hpp"
        "backtest/TickbinMsgs.cpp"
        "core/Errors.hpp"
        "core/MarketDataService.hpp"
        "core/MarketDataService.cpp"
        "demo/DemoMakerBot.hpp"
        "demo/DemoMakerBot.cpp"
        "gx/GxServer.cpp"
        "gx/GxServer.hpp"
        "gx/ExchangeSession.hpp"
        "gx/BinanceSession.cpp"
        "gx/BinanceSession.hpp"
        )


file(GLOB LIB_3RDPTY_HTTPPARSER_SRC_FILES ${PROJECT_SOURCE_DIR}/3rdparty/http_parser/*.c)
file(GLOB LIB_3RDPTY_APACH_SRC_FILES ${PROJECT_SOURCE_DIR}/3rdparty/apache/*.c)

##
## Static library
##
if (BUILD_STATIC_LIBS)
    add_library(apexcore_static STATIC
            ${LIB_APEX_SRC_FILES}
            ${LIB_3RDPTY_HTTPPARSER_SRC_FILES}
            ${LIB_3RDPTY_APACH_SRC_FILES})

    set_property(TARGET apexcore_static PROPERTY CXX_STANDARD 17)
    set_property(TARGET apexcore_static PROPERTY CXX_STANDARD_REQUIRED ON)

    set_target_properties(apexcore_static PROPERTIES OUTPUT_NAME apexcore)

    target_include_directories(apexcore_static
            PUBLIC
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            PRIVATE
            ${LIBCURL_INCLUDE_DIRS}
            ${LIBUV_INCLUDE_DIRS}
            ${LIBPROTOBUF3_INCLUDE_DIRS}
            )

    target_link_libraries(apexcore_static
            PUBLIC
            uv
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
            ${LIB_3RDPTY_APACH_SRC_FILES})

    set_property(TARGET apexcore_shared PROPERTY CXX_STANDARD 17)
    set_property(TARGET apexcore_shared PROPERTY CXX_STANDARD_REQUIRED ON)

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
            ${LIBUV_INCLUDE_DIRS}
            ${LIBPROTOBUF3_INCLUDE_DIRS}
            )

    target_link_libraries(apexcore_shared
            PUBLIC
            uv
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
