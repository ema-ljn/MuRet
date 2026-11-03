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
option(MURET_BUILD_TESTS "Compilar los tests de GoogleTest" ON)

# Los fuentes están en UTF-8 (comentarios y mensajes en castellano). Sin /utf-8, MSVC los lee con
# la página de códigos de Windows: los acentos de los mensajes salen rotos y algunos bytes dan
# el warning C4819, que con /WX corta la compilación.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# <windows.h> (lo incluye SDL_opengl.h, entre otros) define min y max como macros y rompe
# std::min y std::max en cualquier archivo que lo vea después.
add_compile_definitions($<$<PLATFORM_ID:Windows>:NOMINMAX>)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(PrecompiledHeaders)

# Los tests se registran con add_test() o gtest_discover_tests() y los corre CTest
# (ctest --preset <preset>).
enable_testing()

# Un subdirectorio por componente. Los próximos servidores se suman acá.
add_subdirectory(Server/DataServer)
add_subdirectory(Client)

# labs/: el código de las N.0 del camino de aprendizaje (docs/learning/).
add_subdirectory(labs)
