# Client: la reimplementación del main.exe de MU Online 1.02.
#
# Tres targets (CL0.2): ClientLib tiene todo el código, Main es el ejecutable y ClientTests los
# tests, y los dos últimos enlazan contra ClientLib. Las capas (Platform, Data, Network, Protocol,
# Render, Scenes) van entrando a ClientLib ola por ola.

find_package(SDL3 CONFIG REQUIRED)

add_library(ClientLib STATIC
    src/App/CommandLine.cpp
    src/Data/ByteReader.cpp
    src/Data/DataPath.cpp
    src/Data/Tables/TableChecksum.cpp
    src/Data/Tables/Xor3.cpp
)
target_include_directories(ClientLib PUBLIC src)
target_compile_features(ClientLib PUBLIC cxx_std_20)
muret_set_warnings(ClientLib)

add_executable(Main
    src/main.cpp
)
target_link_libraries(Main PRIVATE ClientLib SDL3::SDL3)
# Los typedef PFNGL…PROC de las funciones de GL 1.x (glClear, glViewport); los de la 3.3 vienen
# siempre. Con ellos se tipan los punteros que da SDL_GL_GetProcAddress (main.cpp).
target_compile_definitions(Main PRIVATE SDL_OPENGL_1_FUNCTION_TYPEDEFS)
muret_set_warnings(Main)

if(MURET_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(ClientTests
        tests/app/CommandLineTest.cpp
        tests/core/ExpectedTest.cpp
        tests/support/FileBytes.cpp
        tests/support/FileBytesTest.cpp
        tests/support/GoldenDir.cpp
        tests/support/GoldenDirTest.cpp
        tests/data/ByteReaderTest.cpp
        tests/data/DataPathTest.cpp
        tests/data/GoldenDataFilesTest.cpp
        tests/data/GoldenTableChecksumTest.cpp
        tests/data/TableChecksumTest.cpp
        tests/data/Xor3Test.cpp
    )
    target_include_directories(ClientTests PRIVATE tests)
    target_link_libraries(ClientTests PRIVATE ClientLib GTest::gtest GTest::gtest_main)
    muret_set_warnings(ClientTests)

    # La carpeta golden llega a los tests como variable de entorno. Se puede fijar acá, al
    # configurar (-DMURET_CLIENT_GOLDEN_DIR=...), o en el entorno al correr ctest. La de los
    # volcados de memoria (MURET_CLIENT_MEMORY_DIR) va solo en el entorno: gtest_discover_tests
    # parte en dos una lista de variables en ENVIRONMENT, y la segunda se pierde.
    set(MURET_CLIENT_GOLDEN_DIR "" CACHE PATH "Copia del Data\\ del cliente original para los tests golden (vacío: se saltean)")
    if(MURET_CLIENT_GOLDEN_DIR)
        gtest_discover_tests(ClientTests PROPERTIES ENVIRONMENT "MURET_CLIENT_GOLDEN_DIR=${MURET_CLIENT_GOLDEN_DIR}")
    else()
        gtest_discover_tests(ClientTests)
    endif()

    # Main arranca (encuentra SDL3, en Windows la DLL) y rechaza un argumento malo antes de abrir
    # la ventana, así que corre sin pantalla. Si faltara la DLL, no imprimiría nada y fallaría.
    add_test(NAME Client.Main.RejectsBadArguments COMMAND Main --frames 0)
    set_tests_properties(Client.Main.RejectsBadArguments PROPERTIES
        PASS_REGULAR_EXPRESSION "--frames necesita un número mayor que 0")
endif()
