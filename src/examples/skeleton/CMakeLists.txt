
if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    set(EXTRA_GCC_LIBS stdc++fs)
endif ()

if (BUILD_SHARED_LIBS)
    set(EXTRA_LIBS apexcore_shared)
else ()
    set(EXTRA_LIBS apexcore_static)
endif ()


list(APPEND SRC_FILES)

# Helper macro for progname compilation
macro(Compile_Program progname)

    add_executable(${progname}
            "${progname}.cpp"
            ${SRC_FILES}
            )
    set_property(TARGET ${progname} PROPERTY CXX_STANDARD 17)
    set_property(TARGET ${progname} PROPERTY CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${progname} PRIVATE ${EXTRA_LIBS} ${EXTRA_GCC_LIBS})
    install(TARGETS ${progname})

    if (WIN32)
        set_target_properties(${progname} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib")
    endif ()
endmacro()


Compile_Program(apex-algo-skeleton)

install(FILES "skeleton.json" DESTINATION ./config/examples)
