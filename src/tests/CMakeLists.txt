if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    set(EXTRA_GCC_LIBS stdc++fs)
endif ()

if (BUILD_SHARED_LIBS)
    set(EXTRA_LIBS apexcore_shared)
else ()
    set(EXTRA_LIBS apexcore_static)
endif ()


list(APPEND SRC_FILES)

# Helper macro for example compilation
macro(Compile_Program prog)

    add_executable(${prog}
            "${prog}.cpp"
            ${SRC_FILES}
            )
    target_compile_options(${prog} PUBLIC -DSOURCE_DIR="${CMAKE_SOURCE_DIR}" )
    set_property(TARGET ${prog} PROPERTY CXX_STANDARD 17)
    set_property(TARGET ${prog} PROPERTY CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${prog} PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS})
    install(TARGETS ${prog})

    if (WIN32)
        set_target_properties(${prog} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib")
    endif ()
endmacro()


Compile_Program(test_runner)
Compile_Program(test_sockets)
add_test(TEST test_runner)
add_test(TEST test_sockets)
