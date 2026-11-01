# Client: la reimplementación del main.exe de MU Online 1.02.
#
# Tres targets (CL0.2): ClientLib tiene todo el código, Main es el ejecutable y ClientTests los
# tests, y los dos últimos enlazan contra ClientLib. Las capas (Platform, Data, Network, Protocol,
# Render, Scenes) van entrando a ClientLib ola por ola.

find_package(SDL3 CONFIG REQUIRED)

add_library(ClientLib STATIC
    src/Data/DataPath.cpp
)
target_include_directories(ClientLib PUBLIC src)
target_compile_features(ClientLib PUBLIC cxx_std_20)
muret_set_warnings(ClientLib)

add_executable(Main
    src/main.cpp
)
target_link_libraries(Main PRIVATE ClientLib SDL3::SDL3)
muret_set_warnings(Main)

if(MURET_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(ClientTests
        tests/support/GoldenDir.cpp
        tests/data/DataPathTest.cpp
        tests/data/GoldenDataFilesTest.cpp
    )
    target_include_directories(ClientTests PRIVATE tests)
    target_link_libraries(ClientTests PRIVATE ClientLib GTest::gtest GTest::gtest_main)
    muret_set_warnings(ClientTests)

    # La carpeta golden llega a los tests como variable de entorno. Se puede fijar acá, al
    # configurar (-DMURET_CLIENT_GOLDEN_DIR=...), o en el entorno al correr ctest.
    set(MURET_CLIENT_GOLDEN_DIR "" CACHE PATH "Copia del Data\\ del cliente original para los tests golden (vacío: se saltean)")
    if(MURET_CLIENT_GOLDEN_DIR)
        gtest_discover_tests(ClientTests PROPERTIES ENVIRONMENT "MURET_CLIENT_GOLDEN_DIR=${MURET_CLIENT_GOLDEN_DIR}")
    else()
        gtest_discover_tests(ClientTests)
    endif()
endif()
