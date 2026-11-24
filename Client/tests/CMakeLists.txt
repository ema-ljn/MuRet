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

# Lo que comparten los tests (support/): leer archivos, las carpetas golden, el cable. Sus headers
# se incluyen como "support/...". Usa GoogleTest (GoldenDir marca los tests salteados).
file(GLOB support_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")
list(FILTER support_sources EXCLUDE REGEX "/test_[^/]*\\.cpp$")
list(FILTER support_sources EXCLUDE REGEX "/support/GlTestContext\\.cpp$")
add_library(ClientTestSupport STATIC ${support_sources})
target_include_directories(ClientTestSupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ClientTestSupport PUBLIC ClientLib GTest::gtest)
# Los vectores JPEG, los de red, los de la tela y los de los objetos del mapa 55 (el README.md de
# vectors/jpeg, vectors/net, vectors/cloth y vectors/world55) están en el repo:
# los tests los leen de acá. La Data del original también (Client/assets/Data): es la carpeta
# golden cuando MU_CLIENT_GOLDEN_DIR no está.
cmake_path(SET mu_repo_data_dir NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/Data")
target_compile_definitions(ClientTestSupport PUBLIC
    MU_CLIENT_JPEG_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/jpeg"
    MU_CLIENT_NET_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/net"
    MU_CLIENT_CLOTH_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/cloth"
    MU_CLIENT_WORLD55_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/world55"
    MU_CLIENT_REPO_DATA_DIR="${mu_repo_data_dir}")
mu_set_warnings(ClientTestSupport)
mu_precompile_headers(ClientTestSupport <gtest/gtest.h>)

# Lo de support/ que abre OpenGL (CL5.1): la ventana escondida con su contexto 3.3 core y la regla
# de MU_CLIENT_REQUIRE_GL. Va aparte para que el resto de los tests no enlace SDL3 ni GL.
add_library(ClientGlTestSupport STATIC support/GlTestContext.cpp)
target_link_libraries(ClientGlTestSupport PUBLIC ClientTestSupport ClientRender)
mu_set_warnings(ClientGlTestSupport)
mu_precompile_headers(ClientGlTestSupport <gtest/gtest.h>)

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
# Como mu_add_test de MuMain: arma el ejecutable, lo enlaza con ClientLib, ClientTestSupport y
# GoogleTest (con su main), le pone los warnings del repo y registra cada TEST en CTest. LABEL le
# pone una etiqueta de CTest a cada uno (`ctest -L`), y TIMEOUT cambia su tope, que si no es
# MU_TEST_CASE_TIMEOUT.
function(mu_add_test)
    cmake_parse_arguments(MAT "" "NAME;LABEL;TIMEOUT" "SOURCES;LINK_LIBS" ${ARGN})
    add_executable(${MAT_NAME} ${MAT_SOURCES})
    # Anota cada fuente, con su ruta entera, para el chequeo del final de este archivo.
    foreach(source IN LISTS MAT_SOURCES)
        get_filename_component(source_path ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
        set_property(GLOBAL APPEND PROPERTY MU_REGISTERED_TEST_SOURCES ${source_path})
    endforeach()
    target_link_libraries(${MAT_NAME} PRIVATE ClientTestSupport ClientLib GTest::gtest GTest::gtest_main
        ${MAT_LINK_LIBS})
    mu_set_warnings(${MAT_NAME})
    # El precompiled header de ClientGlTestSupport si el test ve ClientPlatform, que suma una
    # definición; si no, el de ClientTestSupport (cmake/PrecompiledHeaders.cmake).
    set(pch_from ClientTestSupport)
    foreach(platform_lib IN ITEMS ClientPlatform ClientRender ClientGlTestSupport)
        if(platform_lib IN_LIST MAT_LINK_LIBS)
            set(pch_from ClientGlTestSupport)
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

# mu_add_module_tests([OTHER_SYSTEM <file> ...] [LINK_LIBS lib ...] [LABEL <label>]
# [TIMEOUT <seconds>]): one executable for each test_*.cpp of the calling folder, named after the
# file (test_packet_reader), with the rest passed on to mu_add_test. A new test joins on
# reconfigure. OTHER_SYSTEM lists the tests of another system's code, which this one does not build.
function(mu_add_module_tests)
    cmake_parse_arguments(MAMT "" "" "OTHER_SYSTEM" ${ARGN})
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
    foreach(other_system_test IN LISTS MAMT_OTHER_SYSTEM)
        set(other_system_path "${CMAKE_CURRENT_SOURCE_DIR}/${other_system_test}")
        list(REMOVE_ITEM tests "${other_system_path}")
        set_property(GLOBAL APPEND PROPERTY MU_OTHER_SYSTEM_TEST_SOURCES "${other_system_path}")
    endforeach()
    foreach(test_source IN LISTS tests)
        get_filename_component(test_name ${test_source} NAME_WE)
        mu_add_test(NAME ${test_name} SOURCES ${test_source} ${MAMT_UNPARSED_ARGUMENTS})
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
get_property(other_system_test_sources GLOBAL PROPERTY MU_OTHER_SYSTEM_TEST_SOURCES)
list(REMOVE_ITEM all_test_sources ${registered_test_sources} ${other_system_test_sources})
if(all_test_sources)
    list(JOIN all_test_sources "\n  " unregistered)
    message(FATAL_ERROR "Tests sin ejecutable (falta su CMakeLists.txt o su add_subdirectory):\n  ${unregistered}")
endif()
