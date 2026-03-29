if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    set(EXTRA_GCC_LIBS stdc++fs)
endif ()

if (BUILD_SHARED_LIBS)
    set(EXTRA_LIBS apexcore_shared)
else ()
    set(EXTRA_LIBS apexcore_static)
endif ()

list(APPEND SRC_FILES)

add_executable(apex-demo-single-order-cancel
  "apex_demo_single_order_cancel.cpp"
  ${SRC_FILES}
)
target_link_libraries(apex-demo-single-order-cancel PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS} sodium)
