# Los tests del cliente, como el tests/ de sven-n/MuMain: un CMakeLists.txt por módulo y un
# ejecutable por archivo. MuMain llama a mu_add_test a mano por cada archivo; acá el módulo junta
# sus test_*.cpp con file(GLOB) y llama a mu_add_test por cada uno (una diferencia elegida, en
# la regla 7). Cada TEST de GoogleTest es un test de CTest con el nombre de la suite
# (PacketReaderTest.CutsAC1), así que `ctest -R PacketReader` filtra igual que antes.
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# MSVC: un archivo de tests grande (test_character_select_scene, con lo que suma el hito 11 de la CL9) pasa el tope de
# secciones de un .obj (C1128). /bigobj lo sube para todo lo de esta carpeta, también para los precompiled headers
# que los tests reusan, así las opciones coinciden.
if(MSVC)
    add_compile_options(/bigobj)
endif()

# mu_reaches_game(<file> <out>): whether <file> includes a header of the game or the app, itself or through the
# headers of tests/. A header of tests/ brings its .cpp along, because who includes it needs what that defines.
# It decides what a test links: Engine alone, or ClientLib too. A test that starts including the game needs a
# reconfigure, and says so by not finding the header.
set(MU_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
cmake_path(SET MU_CLIENT_SRC_DIR NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
function(mu_reaches_game file out)
    get_property(known GLOBAL PROPERTY "MU_REACHES_GAME:${file}" SET)
    if(known)
        get_property(reaches GLOBAL PROPERTY "MU_REACHES_GAME:${file}")
        set(${out} ${reaches} PARENT_SCOPE)
        return()
    endif()
    # Before its includes, so a cycle of headers ends.
    set_property(GLOBAL PROPERTY "MU_REACHES_GAME:${file}" FALSE)
    set(reaches FALSE)
    get_filename_component(folder "${file}" DIRECTORY)
    file(STRINGS "${file}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]+\"")
    foreach(include_line IN LISTS include_lines)
        string(REGEX REPLACE "^[^\"]*\"([^\"]+)\".*$" "\\1" included "${include_line}")
        set(next "")
        if(EXISTS "${folder}/${included}")
            set(next "${folder}/${included}")
        elseif(EXISTS "${MU_TESTS_DIR}/${included}")
            set(next "${MU_TESTS_DIR}/${included}")
        elseif(NOT EXISTS "${MU_CLIENT_SRC_DIR}/engine/${included}" AND
               (EXISTS "${MU_CLIENT_SRC_DIR}/${included}" OR EXISTS "${MU_CLIENT_SRC_DIR}/game/${included}"))
            set(reaches TRUE)
        endif()
        if(next)
            mu_reaches_game("${next}" reaches)
            string(REGEX REPLACE "\\.h$" ".cpp" next_source "${next}")
            if(NOT reaches AND NOT next_source STREQUAL next AND EXISTS "${next_source}")
                mu_reaches_game("${next_source}" reaches)
            endif()
        endif()
        if(reaches)
            break()
        endif()
    endforeach()
    set_property(GLOBAL PROPERTY "MU_REACHES_GAME:${file}" ${reaches})
    set(${out} ${reaches} PARENT_SCOPE)
endfunction()

# Lo que comparten los tests (support/): leer archivos, las carpetas golden, el cable. Sus headers
# se incluyen como "support/...". Usa GoogleTest (GoldenDir marca los tests salteados).
# EngineTestSupport has what needs only the engine, and ClientTestSupport what needs the game too.
file(GLOB support_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")
list(FILTER support_sources EXCLUDE REGEX "/test_[^/]*\\.cpp$")
list(FILTER support_sources EXCLUDE REGEX "/support/GlTestContext\\.cpp$")
set(engine_support_sources "")
set(client_support_sources "")
foreach(support_source IN LISTS support_sources)
    mu_reaches_game("${support_source}" reaches_game)
    if(reaches_game)
        list(APPEND client_support_sources "${support_source}")
    else()
        list(APPEND engine_support_sources "${support_source}")
    endif()
endforeach()
add_library(EngineTestSupport STATIC ${engine_support_sources})
target_include_directories(EngineTestSupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(EngineTestSupport PUBLIC Engine GTest::gtest)
# Los vectores JPEG, los de red, los de la tela y los de los objetos del mapa 55 (el README.md de
# vectors/jpeg, vectors/net, vectors/cloth y vectors/world55) están en el repo:
# los tests los leen de acá. La Data del original también (Client/assets/Data): es la carpeta
# golden cuando MU_CLIENT_GOLDEN_DIR no está.
cmake_path(SET mu_repo_data_dir NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/Data")
target_compile_definitions(EngineTestSupport PUBLIC
    MU_CLIENT_JPEG_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/jpeg"
    MU_CLIENT_NET_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/net"
    MU_CLIENT_CLOTH_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/cloth"
    MU_CLIENT_WORLD55_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/world55"
    MU_CLIENT_REPO_DATA_DIR="${mu_repo_data_dir}")
mu_set_warnings(EngineTestSupport)
mu_precompile_headers(EngineTestSupport <gtest/gtest.h>)

add_library(ClientTestSupport STATIC ${client_support_sources})
target_link_libraries(ClientTestSupport PUBLIC EngineTestSupport ClientLib)
mu_set_warnings(ClientTestSupport)
mu_reuse_precompiled_headers(ClientTestSupport EngineTestSupport)

# Lo de support/ que abre OpenGL (CL5.1): la ventana escondida con su contexto 3.3 core y la regla
# de MU_CLIENT_REQUIRE_GL. Va aparte para que el resto de los tests no enlace SDL3 ni GL.
add_library(GlTestSupport STATIC support/GlTestContext.cpp)
target_link_libraries(GlTestSupport PUBLIC EngineTestSupport OpenGL)
target_link_libraries(GlTestSupport PRIVATE SDL3::SDL3)
mu_set_warnings(GlTestSupport)
mu_precompile_headers(GlTestSupport <gtest/gtest.h>)

# La carpeta golden llega a los tests como variable de entorno. Sin ella, es la Data del repo. Otra
# se puede fijar acá, al configurar (-DMU_CLIENT_GOLDEN_DIR=...), o en el entorno al correr
# ctest. La de los volcados de memoria (MU_CLIENT_MEMORY_DIR) y la del cable
# (MU_CLIENT_WIRE_DIR) van solo en el entorno: gtest_discover_tests parte en dos una lista de
# variables en ENVIRONMENT, y la segunda se pierde.
set(MU_CLIENT_GOLDEN_DIR "" CACHE PATH "Otra copia del Data\\ del cliente original para los tests golden (vacío: la del repo, Client/assets/Data)")

# Cuánto puede tardar un ejecutable en listar sus tests al compilar. El valor por defecto (5 s) no
# alcanza con los sanitizers en una máquina cargada.
set(MU_TEST_DISCOVERY_TIMEOUT 60)

# Cuánto puede tardar cada TEST, en segundos: uno colgado falla en vez de frenar todo, como el
# TIMEOUT de mu_add_test en MuMain (120 s). Acá es más, porque los golden recorren los 2.380 JPEG
# del laboratorio y con los sanitizers tardan.
set(MU_TEST_CASE_TIMEOUT 600)

# mu_add_test(NAME <ejecutable> SOURCES a.cpp [b.cpp ...] [LINK_LIBS lib ...] [LABEL <etiqueta>]
#                [TIMEOUT <segundos>])
#
# Como mu_add_test de MuMain: arma el ejecutable, lo enlaza con GoogleTest (con su main), le pone los warnings
# del repo y registra cada TEST en CTest. A test that includes only the engine links Engine and EngineTestSupport;
# one that reaches the game links ClientLib and ClientTestSupport too (mu_reaches_game). LABEL le
# pone una etiqueta de CTest a cada uno (`ctest -L`), y TIMEOUT cambia su tope, que si no es
# MU_TEST_CASE_TIMEOUT.
function(mu_add_test)
    cmake_parse_arguments(MAT "" "NAME;LABEL;TIMEOUT" "SOURCES;LINK_LIBS" ${ARGN})
    add_executable(${MAT_NAME} ${MAT_SOURCES})
    # Anota cada fuente, con su ruta entera, para el chequeo del final de este archivo.
    set(reaches_game FALSE)
    foreach(source IN LISTS MAT_SOURCES)
        get_filename_component(source_path ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
        set_property(GLOBAL APPEND PROPERTY MU_REGISTERED_TEST_SOURCES ${source_path})
        if(NOT reaches_game)
            mu_reaches_game("${source_path}" reaches_game)
        endif()
    endforeach()
    if(reaches_game)
        target_link_libraries(${MAT_NAME} PRIVATE ClientTestSupport ClientLib)
    endif()
    target_link_libraries(${MAT_NAME} PRIVATE EngineTestSupport Engine GTest::gtest GTest::gtest_main
        ${MAT_LINK_LIBS})
    mu_set_warnings(${MAT_NAME})
    # El precompiled header de GlTestSupport si el test ve Platform, que suma una
    # definición; si no, el de EngineTestSupport (cmake/PrecompiledHeaders.cmake).
    set(pch_from EngineTestSupport)
    foreach(platform_lib IN ITEMS Platform OpenGL GlTestSupport)
        if(platform_lib IN_LIST MAT_LINK_LIBS)
            set(pch_from GlTestSupport)
        endif()
    endforeach()
    mu_reuse_precompiled_headers(${MAT_NAME} ${pch_from})
    if(NOT MAT_TIMEOUT)
        set(MAT_TIMEOUT ${MU_TEST_CASE_TIMEOUT})
    endif()
    set(test_properties TIMEOUT ${MAT_TIMEOUT})
    if(MAT_LABEL)
        list(APPEND test_properties LABELS ${MAT_LABEL})
    endif()
    if(MU_CLIENT_GOLDEN_DIR)
        list(APPEND test_properties ENVIRONMENT "MU_CLIENT_GOLDEN_DIR=${MU_CLIENT_GOLDEN_DIR}")
    endif()
    gtest_discover_tests(${MAT_NAME} DISCOVERY_TIMEOUT ${MU_TEST_DISCOVERY_TIMEOUT}
        PROPERTIES ${test_properties})
endfunction()

# mu_add_module_tests([OTHER_SYSTEM <file> ...] [XOR_REMAP <file> ...] [EXCLUDE <file> ...] [LINK_LIBS lib ...]
# [LABEL <label>] [TIMEOUT <seconds>]): one executable for each test_*.cpp of the calling folder, named after the
# file (test_packet_reader), with the rest passed on to mu_add_test. A new test joins on reconfigure. OTHER_SYSTEM
# lists the tests of another system's code, which this one does not build. XOR_REMAP lists the tests of the XOR and
# remap layer: they build and link XorRemap only with MU_XOR_REMAP. EXCLUDE lists the tests the folder registers
# itself with mu_add_test.
function(mu_add_module_tests)
    cmake_parse_arguments(MAMT "" "" "OTHER_SYSTEM;XOR_REMAP;EXCLUDE;LINK_LIBS" ${ARGN})
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
    foreach(excluded_test IN LISTS MAMT_EXCLUDE)
        list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/${excluded_test}")
    endforeach()
    foreach(other_system_test IN LISTS MAMT_OTHER_SYSTEM)
        set(other_system_path "${CMAKE_CURRENT_SOURCE_DIR}/${other_system_test}")
        list(REMOVE_ITEM tests "${other_system_path}")
        set_property(GLOBAL APPEND PROPERTY MU_UNBUILT_TEST_SOURCES "${other_system_path}")
    endforeach()
    foreach(xor_remap_test IN LISTS MAMT_XOR_REMAP)
        set(xor_remap_path "${CMAKE_CURRENT_SOURCE_DIR}/${xor_remap_test}")
        list(REMOVE_ITEM tests "${xor_remap_path}")
        if(MU_XOR_REMAP)
            get_filename_component(test_name ${xor_remap_path} NAME_WE)
            mu_add_test(NAME ${test_name} SOURCES ${xor_remap_path} LINK_LIBS ${MAMT_LINK_LIBS} XorRemap
                ${MAMT_UNPARSED_ARGUMENTS})
        else()
            set_property(GLOBAL APPEND PROPERTY MU_UNBUILT_TEST_SOURCES "${xor_remap_path}")
        endif()
    endforeach()
    foreach(test_source IN LISTS tests)
        get_filename_component(test_name ${test_source} NAME_WE)
        mu_add_test(NAME ${test_name} SOURCES ${test_source} LINK_LIBS ${MAMT_LINK_LIBS} ${MAMT_UNPARSED_ARGUMENTS})
    endforeach()
endfunction()

add_subdirectory(app)
add_subdirectory(audio)
add_subdirectory(characterization)
add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(gamelogic)
add_subdirectory(input)
add_subdirectory(network)
add_subdirectory(platform)
add_subdirectory(protocol)
add_subdirectory(render)
add_subdirectory(render_gl)
add_subdirectory(scenes)
add_subdirectory(support)
add_subdirectory(ui)
add_subdirectory(world)

# Un test_*.cpp que no quedó en ningún ejecutable (en una subcarpeta, o en una carpeta sin su
# CMakeLists.txt) no correría nunca y nadie lo notaría: el configure se corta y lo nombra.
file(GLOB_RECURSE all_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
get_property(registered_test_sources GLOBAL PROPERTY MU_REGISTERED_TEST_SOURCES)
get_property(unbuilt_test_sources GLOBAL PROPERTY MU_UNBUILT_TEST_SOURCES)
list(REMOVE_ITEM all_test_sources ${registered_test_sources} ${unbuilt_test_sources})
if(all_test_sources)
    list(JOIN all_test_sources "\n  " unregistered)
    message(FATAL_ERROR "Tests sin ejecutable (falta su CMakeLists.txt o su add_subdirectory):\n  ${unregistered}")
endif()
