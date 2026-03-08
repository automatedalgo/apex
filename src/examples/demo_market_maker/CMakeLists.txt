if (BUILD_SHARED_LIBS)
    set(EXTRA_LIBS apexcore_shared)
else ()
    set(EXTRA_LIBS apexcore_static)
endif ()

add_executable(apex-algo-demo-market-maker
  "apex-algo-demo-market-maker.cpp"
  "DemoMakerBot.cpp"
  "DemoMakerBot.hpp"
)
target_link_libraries(apex-algo-demo-market-maker PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS} sodium)

install(TARGETS apex-algo-demo-market-maker)
install(FILES "demo-market-maker.json" DESTINATION ./config/examples)
