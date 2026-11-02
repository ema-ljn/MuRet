# Los tests del cliente, como el tests/ de sven-n/MuMain: un CMakeLists.txt por módulo y un
# ejecutable por archivo. MuMain llama a mu_add_test a mano por cada archivo; acá el módulo junta
# sus test_*.cpp con file(GLOB) y llama a muret_add_test por cada uno (una diferencia elegida, en
# la regla 7). Cada TEST de GoogleTest es un test de CTest con el nombre de la suite
# (PacketReaderTest.CutsAC1), así que `ctest -R PacketReader` filtra igual que antes.
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Lo que comparten los tests (support/): leer archivos, las carpetas golden, el cable. Sus headers
# se incluyen como "support/...". Usa GoogleTest (GoldenDir marca los tests salteados).
file(GLOB support_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")
list(FILTER support_sources EXCLUDE REGEX "/test_[^/]*\\.cpp$")
add_library(ClientTestSupport STATIC ${support_sources})
target_include_directories(ClientTestSupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ClientTestSupport PUBLIC ClientLib GTest::gtest)
# Los vectores JPEG y los de red (el README.md de vectors/jpeg y de vectors/net) están en el repo:
# los tests los leen de acá.
target_compile_definitions(ClientTestSupport PUBLIC
    MURET_CLIENT_JPEG_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/jpeg"
    MURET_CLIENT_NET_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors/net")
muret_set_warnings(ClientTestSupport)

# La carpeta golden llega a los tests como variable de entorno. Se puede fijar acá, al configurar
# (-DMURET_CLIENT_GOLDEN_DIR=...), o en el entorno al correr ctest. La de los volcados de memoria
# (MURET_CLIENT_MEMORY_DIR) y la del cable (MURET_CLIENT_WIRE_DIR) van solo en el entorno:
# gtest_discover_tests parte en dos una lista de variables en ENVIRONMENT, y la segunda se pierde.
set(MURET_CLIENT_GOLDEN_DIR "" CACHE PATH "Copia del Data\\ del cliente original para los tests golden (vacío: se saltean)")

# Cuánto puede tardar un ejecutable en listar sus tests al compilar. El valor por defecto (5 s) no
# alcanza con los sanitizers en una máquina cargada.
set(MURET_TEST_DISCOVERY_TIMEOUT 60)

# Cuánto puede tardar cada TEST, en segundos: uno colgado falla en vez de frenar todo, como el
# TIMEOUT de mu_add_test en MuMain (120 s). Acá es más, porque los golden recorren los 2.380 JPEG
# del laboratorio y con los sanitizers tardan.
set(MURET_TEST_CASE_TIMEOUT 600)

# muret_add_test(NAME <ejecutable> SOURCES a.cpp [b.cpp ...] [LINK_LIBS lib ...])
#
# Como mu_add_test de MuMain: arma el ejecutable, lo enlaza con ClientLib, ClientTestSupport y
# GoogleTest (con su main), le pone los warnings del repo y registra cada TEST en CTest.
function(muret_add_test)
    cmake_parse_arguments(MAT "" "NAME" "SOURCES;LINK_LIBS" ${ARGN})
    add_executable(${MAT_NAME} ${MAT_SOURCES})
    # Anota cada fuente, con su ruta entera, para el chequeo del final de este archivo.
    foreach(source IN LISTS MAT_SOURCES)
        get_filename_component(source_path ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
        set_property(GLOBAL APPEND PROPERTY MURET_REGISTERED_TEST_SOURCES ${source_path})
    endforeach()
    target_link_libraries(${MAT_NAME} PRIVATE ClientTestSupport ClientLib GTest::gtest GTest::gtest_main
        ${MAT_LINK_LIBS})
    muret_set_warnings(${MAT_NAME})
    if(MURET_CLIENT_GOLDEN_DIR)
        gtest_discover_tests(${MAT_NAME} DISCOVERY_TIMEOUT ${MURET_TEST_DISCOVERY_TIMEOUT}
            PROPERTIES TIMEOUT ${MURET_TEST_CASE_TIMEOUT}
                       ENVIRONMENT "MURET_CLIENT_GOLDEN_DIR=${MURET_CLIENT_GOLDEN_DIR}")
    else()
        gtest_discover_tests(${MAT_NAME} DISCOVERY_TIMEOUT ${MURET_TEST_DISCOVERY_TIMEOUT}
            PROPERTIES TIMEOUT ${MURET_TEST_CASE_TIMEOUT})
    endif()
endfunction()

# muret_add_module_tests(): un ejecutable por cada test_*.cpp de la carpeta que lo llama, con el
# nombre del archivo (test_packet_reader). Un test nuevo entra al reconfigurar.
function(muret_add_module_tests)
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
    foreach(test_source IN LISTS tests)
        get_filename_component(test_name ${test_source} NAME_WE)
        muret_add_test(NAME ${test_name} SOURCES ${test_source})
    endforeach()
endfunction()

add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(input)
add_subdirectory(network)
add_subdirectory(platform)
add_subdirectory(protocol)
add_subdirectory(support)

# Un test_*.cpp que no quedó en ningún ejecutable (en una subcarpeta, o en una carpeta sin su
# CMakeLists.txt) no correría nunca y nadie lo notaría: el configure se corta y lo nombra.
file(GLOB_RECURSE all_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
get_property(registered_test_sources GLOBAL PROPERTY MURET_REGISTERED_TEST_SOURCES)
list(REMOVE_ITEM all_test_sources ${registered_test_sources})
if(all_test_sources)
    list(JOIN all_test_sources "\n  " unregistered)
    message(FATAL_ERROR "Tests sin ejecutable (falta su CMakeLists.txt o su add_subdirectory):\n  ${unregistered}")
endif()
