# The engine ships as shared libraries (docs/adr/0028-shared-library-modules.md).
option(MU_MONOLITHIC "Build the engine's libraries static, for one binary" OFF)

include(GenerateExportHeader)

# One rounding per operation, as the original's x87 code: GCC and Clang contract a*b + c into an FMA by default.
function(mu_set_fidelity_flags target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endif()
endfunction()

# MSVC warns that a member or a base of an exported class has no DLL interface (C4251, C4275), which is every
# std::vector member. The libraries and the executable are one build with one runtime (ADR 0028), so the layout
# the client sees is the library's.
function(mu_set_api_warnings target scope)
    if(MSVC)
        target_compile_options(${target} ${scope} /wd4251 /wd4275)
    endif()
endfunction()

# The standard-library precompiled header the engine's libraries reuse, built with their options.
function(mu_add_std_pch)
    add_library(StdPch STATIC "${PROJECT_SOURCE_DIR}/cmake/StdPch.cpp")
    set_target_properties(StdPch PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(NOT MU_MONOLITHIC)
        set_target_properties(StdPch PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    mu_set_warnings(StdPch)
    mu_set_api_warnings(StdPch PRIVATE)
    mu_set_fidelity_flags(StdPch)
    mu_precompile_headers(StdPch)
endfunction()

# mu_add_library(<target> SOURCES <files>... HEADERS <files>... [LINK <targets>...])
#
# A library of the engine, named as its file (Engine.dll, libEngine.so). Its headers are included by their path
# under Client/src/engine. <TARGET>_API, from the generated <target>Api.h, marks what other binaries use: nothing
# else leaves the library, on Linux as on Windows.
function(mu_add_library target)
    cmake_parse_arguments(MAL "" "" "SOURCES;HEADERS;LINK" ${ARGN})
    if(MU_MONOLITHIC)
        add_library(${target} STATIC ${MAL_SOURCES})
    else()
        add_library(${target} SHARED ${MAL_SOURCES})
        if(NOT MSVC)
            target_link_options(${target} PRIVATE -Wl,--no-undefined)
        endif()
    endif()
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        VERIFY_INTERFACE_HEADER_SETS ON)
    target_sources(${target} PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine"
        FILES ${MAL_HEADERS})

    string(TOUPPER ${target} upper_name)
    set(api_dir "${CMAKE_CURRENT_BINARY_DIR}/api")
    generate_export_header(${target} BASE_NAME ${upper_name} EXPORT_MACRO_NAME ${upper_name}_API
        EXPORT_FILE_NAME "${api_dir}/${target}Api.h")
    target_sources(${target} PUBLIC FILE_SET api TYPE HEADERS BASE_DIRS "${api_dir}"
        FILES "${api_dir}/${target}Api.h")

    target_link_libraries(${target} PUBLIC ${MAL_LINK})
    mu_set_warnings(${target})
    mu_set_api_warnings(${target} PUBLIC)
    mu_set_fidelity_flags(${target})
    mu_reuse_precompiled_headers(${target} StdPch)
    # <target>_EXPORTS and a backend's definitions change nothing in StdPch, which holds only the standard library.
    if(MSVC AND MU_PRECOMPILED_HEADERS)
        target_compile_options(${target} PRIVATE /wd4605)
    endif()
endfunction()
