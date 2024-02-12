if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  set(EXTRA_GCC_LIBS stdc++fs)
endif()

if (BUILD_SHARED_LIBS)
  set(EXTRA_LIBS apexcore_shared stdc++fs)
else ()
  set(EXTRA_LIBS apexcore_static stdc++fs)
endif ()

macro(Compile_Program filename)

  add_executable(${filename}
    "${PROJECT_SOURCE_DIR}/src/tick-collector/${filename}.cpp"
    ${SRC_FILES}
    )
  set_property(TARGET ${filename} PROPERTY CXX_STANDARD 17)
  set_property(TARGET ${filename} PROPERTY CXX_STANDARD_REQUIRED ON)
  target_link_libraries(${filename} PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS})

  if (WIN32)
    set_target_properties(${filename} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib")
  endif ()

  install(TARGETS ${filename})

endmacro()

Compile_Program(apex-tick-tool)
Compile_Program(apex-tick-collector)
