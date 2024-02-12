
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
macro(Compile_Program example)

    add_executable(${example}
            "${example}.cpp"
            ${SRC_FILES}
            )
    set_property(TARGET ${example} PROPERTY CXX_STANDARD 17)
    set_property(TARGET ${example} PROPERTY CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${example} PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS})
    install(TARGETS ${example})

    if (WIN32)
        set_target_properties(${example} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib")
    endif ()
endmacro()


Compile_Program(apex-algo-demo-market-maker)

install(FILES "demo-market-maker.json" DESTINATION ./config/examples)
