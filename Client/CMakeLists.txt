# Client: la reimplementación del main.exe de MU Online 1.02.
#
# ClientLib tiene todo el código y Main es el ejecutable (CL0.2). Los tests van en tests/, un
# ejecutable por archivo como en sven-n/MuMain (tests/CMakeLists.txt). Las capas (Core, Platform,
# Data, Network, Protocol, Render, Scenes) van entrando a ClientLib ola por ola.

find_package(SDL3 CONFIG REQUIRED)
# La libjpeg de libjpeg-turbo, para los OZJ (JpegImage.cpp). Se pide la de libjpeg-turbo y no
# cualquier libjpeg (find_package(JPEG)): es la que se comparó con la 6b del original.
find_package(libjpeg-turbo CONFIG REQUIRED)

# Los fuentes de src/ se juntan solos, como en MuMain: un .cpp nuevo entra al reconfigurar
# (CONFIGURE_DEPENDS lo mira en cada build). main.cpp es de Main, no de la biblioteca.
file(GLOB_RECURSE client_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER client_sources EXCLUDE REGEX "/src/main\\.cpp$")
add_library(ClientLib STATIC ${client_sources})
target_include_directories(ClientLib PUBLIC src)
target_compile_features(ClientLib PUBLIC cxx_std_20)
# En vcpkg, libjpeg-turbo es estática en Linux y dinámica en Windows: el target cambia de nombre.
target_link_libraries(ClientLib PRIVATE
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::jpeg>,libjpeg-turbo::jpeg,libjpeg-turbo::jpeg-static>)
muret_set_warnings(ClientLib)
# libjpeg avisa de sus errores con longjmp, y JpegImage.cpp lo recibe con setjmp. MSVC avisa de
# cualquier setjmp en C++ (C4611: longjmp no corre destructores); el archivo está armado para que
# no haya ninguno en el medio (Decompress, en JpegImage.cpp).
if(MSVC)
    set_source_files_properties(src/Data/Textures/JpegImage.cpp PROPERTIES COMPILE_OPTIONS /wd4611)
endif()

add_executable(Main
    src/main.cpp
)
target_link_libraries(Main PRIVATE ClientLib SDL3::SDL3)
# Los typedef PFNGL…PROC de las funciones de GL 1.x (glClear, glViewport); los de la 3.3 vienen
# siempre. Con ellos se tipan los punteros que da SDL_GL_GetProcAddress (main.cpp).
target_compile_definitions(Main PRIVATE SDL_OPENGL_1_FUNCTION_TYPEDEFS)
muret_set_warnings(Main)

if(MURET_BUILD_TESTS)
    add_subdirectory(tests)

    # Main arranca (encuentra SDL3, en Windows la DLL) y rechaza un argumento malo antes de abrir
    # la ventana, así que corre sin pantalla. Si faltara la DLL, no imprimiría nada y fallaría.
    add_test(NAME Client.Main.RejectsBadArguments COMMAND Main --frames 0)
    set_tests_properties(Client.Main.RejectsBadArguments PROPERTIES
        PASS_REGULAR_EXPRESSION "--frames necesita un número mayor que 0")
endif()
