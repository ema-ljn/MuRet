cmake_minimum_required(VERSION 3.25)

project(MuRet
    VERSION 0.1.0
    DESCRIPTION "Servidores y cliente de MU Online 1.02 reescritos en C++20"
    LANGUAGES CXX)

# C++20 estándar para todo el repo, sin extensiones del compilador (gnu++20).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json para clangd / clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MURET_WARNINGS_AS_ERRORS "Tratar los warnings como errores" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# Los tests se registran con add_test() y los corre CTest (ctest --test-dir build/<preset>).
enable_testing()

# Un subdirectorio por componente. Los próximos servidores se suman acá.
add_subdirectory(Server/DataServer)
add_subdirectory(Client)

# labs/: el código de las N.0 del camino de aprendizaje (docs/learning/).
add_subdirectory(labs)
