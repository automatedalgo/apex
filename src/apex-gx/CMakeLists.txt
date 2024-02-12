if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  set(EXTRA_GCC_LIBS stdc++fs)
endif()

if (BUILD_SHARED_LIBS)
  set(EXTRA_LIBS apexcore_shared stdc++fs)
else ()
  set(EXTRA_LIBS apexcore_static stdc++fs)
endif ()



# Helper macro for example compilation
macro(Compile_Program example)

  add_executable(${example}
    "${PROJECT_SOURCE_DIR}/src/apex-gx/${example}.cpp"
    ${SRC_FILES}
    )
  set_property(TARGET ${example} PROPERTY CXX_STANDARD 17)
  set_property(TARGET ${example} PROPERTY CXX_STANDARD_REQUIRED ON)
  target_link_libraries(${example} PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS})

  if (WIN32)
    set_target_properties(${example} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib")
  endif ()

  install(TARGETS ${example})

endmacro()


Compile_Program(apex-gx)
Compile_Program(apex-gx-paper)


install(FILES "apex-gx-sim.json" DESTINATION ./config/examples)
