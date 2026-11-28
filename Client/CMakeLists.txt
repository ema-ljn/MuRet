# Client: la reimplementación del main.exe de MU Online 1.02.
#
# The engine (src/engine/) builds as four libraries, cut by what the code sees (ADR 0028): Engine sees neither SDL
# nor OpenGL, Platform sees SDL (Platform/Sdl/), OpenGL sees OpenGL (Render/Gl/) and Audio sees SDL3_mixer
# (Audio/Sdl/). ClientLib is the game and the app, static until AR4 and AR5, and Main is the executable (main.exe).
# The tests are in tests/, one executable per file as in sven-n/MuMain (tests/CMakeLists.txt).

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_mixer CONFIG REQUIRED)
# La libjpeg de libjpeg-turbo, para los OZJ (JpegImage.cpp). Se pide la de libjpeg-turbo y no
# cualquier libjpeg (find_package(JPEG)): es la que se comparó con la 6b del original.
find_package(libjpeg-turbo CONFIG REQUIRED)
# glm (vcpkg.json, desde la CL5.0): la matriz del 2D de OpenGL (pregunta 6 de la CL5) y, desde
# la CL6.2, los cuaterniones y las matrices de Models/ en Engine. Solo los headers: el
# puerto también arma una biblioteca (glm::glm), que no hace falta.
find_package(glm CONFIG REQUIRED)
# FreeType (vcpkg.json, desde la CL5.3): el texto en un bit de Render/Text/FreeTypeRasterizer.cpp
# (pregunta 7 de la CL5). Es el FindFreetype de CMake, que el puerto de vcpkg envuelve, y en Linux
# sin vcpkg encuentra el del sistema (libfreetype-dev).
find_package(Freetype REQUIRED)
# CLI11 (vcpkg.json, AR1.3): the command line's options, header only, used only by App/CommandLine.cpp.
find_package(CLI11 CONFIG REQUIRED)

mu_add_std_pch()

# Los fuentes se juntan solos, como en MuMain: un .cpp nuevo entra al reconfigurar (CONFIGURE_DEPENDS lo mira en
# cada build). Each folder of a backend is its library's, and the rest of src/engine/ is Engine's.
set(engine_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/engine")
set(backend_folders "/src/engine/(Platform/Sdl|Audio/Sdl|Render/Gl)/")
file(GLOB_RECURSE engine_sources CONFIGURE_DEPENDS "${engine_dir}/*.cpp")
file(GLOB_RECURSE engine_headers CONFIGURE_DEPENDS "${engine_dir}/*.h")
list(FILTER engine_sources EXCLUDE REGEX "${backend_folders}")
list(FILTER engine_headers EXCLUDE REGEX "${backend_folders}")
# RoundedMath is an object library with its own compile options, and XorRemap a static one (both below).
list(FILTER engine_sources EXCLUDE REGEX "/src/engine/(Math/RoundedMath|Network/XorRemap)/")
list(FILTER engine_headers EXCLUDE REGEX "/src/engine/Network/XorRemap/")
# JpegErrors.h names libjpeg's types, which only Engine's own files see: it is not a header for who links Engine.
list(FILTER engine_headers EXCLUDE REGEX "/Data/Textures/JpegErrors\\.h$")

mu_add_library(Engine SOURCES ${engine_sources} HEADERS ${engine_headers})
# En vcpkg, libjpeg-turbo es estática en Linux y dinámica en Windows: el target cambia de nombre.
target_link_libraries(Engine PRIVATE
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::jpeg>,libjpeg-turbo::jpeg,libjpeg-turbo::jpeg-static>)
# Sus headers quedan en FreeTypeRasterizer.cpp: nadie más los ve.
target_link_libraries(Engine PRIVATE Freetype::Freetype)
# PUBLIC: los headers de Models/ usan sus tipos (pregunta 4 del plan de la CL6). Sin
# GLM_FORCE_* ni GLM_ENABLE_EXPERIMENTAL, así el precompiled header no cambia.
target_link_libraries(Engine PUBLIC glm::glm-header-only)
add_subdirectory(src/engine/Math/RoundedMath)
target_sources(Engine PRIVATE $<TARGET_OBJECTS:RoundedMath>)
# Los sockets de Network/SocketApi.cpp (CL4.1): en Windows son de Winsock 2, ws2_32. En Linux están
# en la libc.
if(WIN32)
    target_link_libraries(Engine PRIVATE ws2_32)
endif()
# libjpeg avisa de sus errores con longjmp, y JpegImage.cpp y JpegWriter.cpp lo reciben con setjmp.
# MSVC avisa de cualquier setjmp en C++ (C4611: longjmp no corre destructores); los dos archivos
# están armados para que no haya ninguno en el medio (Decompress y Compress).
if(MSVC)
    set_source_files_properties(src/engine/Data/Textures/JpegImage.cpp src/engine/Data/Textures/JpegWriter.cpp
        PROPERTIES COMPILE_OPTIONS /wd4611)
endif()

# Platform (CL3.1): lo que ve SDL3 para la ventana y el sistema. La ventana, el contexto GL, el loader, el
# intervalo de swap y la traducción de eventos. Sus tests (tests/platform/) lo enlazan aparte.
file(GLOB_RECURSE platform_sources CONFIGURE_DEPENDS "${engine_dir}/Platform/Sdl/*.cpp")
file(GLOB_RECURSE platform_headers CONFIGURE_DEPENDS "${engine_dir}/Platform/Sdl/*.h")
# GlFunctionList.h is a list for GlLoader.h to expand, not a header to include alone.
list(FILTER platform_headers EXCLUDE REGEX "/GlFunctionList\\.h$")
# Its headers name SDL's types, so who links Platform sees SDL's headers; only who calls SDL links it.
mu_add_library(Platform SOURCES ${platform_sources} HEADERS ${platform_headers} LINK Engine SDL3::Headers)
target_link_libraries(Platform PRIVATE SDL3::SDL3)
# Linux: SDL3 is shared (cmake/triplets/), and its file goes beside the binaries as on Windows.
if(NOT WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET Platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE_DIR:Platform>/$<TARGET_SONAME_FILE_NAME:SDL3::SDL3-shared>
        VERBATIM)
endif()
# El registro de Platform/Sdl/SystemFonts.cpp (CL5.3): en Windows, advapi32, que MSVC ya enlaza por
# defecto; va explícito, como ws2_32.
if(WIN32)
    target_link_libraries(Platform PRIVATE advapi32)
endif()
# Los typedef PFNGL…PROC de las funciones de GL 1.x (glClear, glViewport); los de la 3.3 vienen
# siempre. Con ellos se tipan los punteros que da SDL_GL_GetProcAddress, y GlLoader.h los expone:
# por eso es PUBLIC (OpenGL, Main y los tests de platform/ los ven).
target_compile_definitions(Platform PUBLIC SDL_OPENGL_1_FUNCTION_TYPEDEFS)

# OpenGL (CL5.1): lo que habla con OpenGL. Los nombres de GL con RAII, los shaders, las texturas,
# el framebuffer de los tests y el pipeline del 2D. Llama a GL por la tabla de Platform
# (GlFunctions), así que la enlaza; lo que arma qué dibujar (Render/ sin Gl/) queda en Engine y
# se prueba sin ventana (pregunta 4 del plan de la CL5). Sus tests, en tests/render_gl/.
file(GLOB_RECURSE opengl_sources CONFIGURE_DEPENDS "${engine_dir}/Render/Gl/*.cpp")
file(GLOB_RECURSE opengl_headers CONFIGURE_DEPENDS "${engine_dir}/Render/Gl/*.h")
mu_add_library(OpenGL SOURCES ${opengl_sources} HEADERS ${opengl_headers} LINK Engine Platform)

# Audio: the SDL3_mixer backend of the sound's contracts, which stay in Engine. It opens SDL's audio itself and
# does not need the window.
file(GLOB_RECURSE audio_sources CONFIGURE_DEPENDS "${engine_dir}/Audio/Sdl/*.cpp")
file(GLOB_RECURSE audio_headers CONFIGURE_DEPENDS "${engine_dir}/Audio/Sdl/*.h")
mu_add_library(Audio SOURCES ${audio_sources} HEADERS ${audio_headers} LINK Engine)
target_link_libraries(Audio PRIVATE SDL3::SDL3
    $<IF:$<TARGET_EXISTS:SDL3_mixer::SDL3_mixer>,SDL3_mixer::SDL3_mixer,SDL3_mixer::SDL3_mixer-static>)

# ClientLib: the game and the app, everything in src/ outside engine/. main.cpp is Main's.
file(GLOB_RECURSE client_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER client_sources EXCLUDE REGEX "/src/app/main\\.cpp$")
list(FILTER client_sources EXCLUDE REGEX "/src/engine/")
# Each system's command line syntax (App/CommandLineSystem.h).
list(FILTER client_sources EXCLUDE REGEX "/src/app/App/CommandLine(Posix|Windows)\\.cpp$")
if(WIN32)
    list(APPEND client_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/app/App/CommandLineWindows.cpp")
else()
    list(APPEND client_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/app/App/CommandLinePosix.cpp")
endif()
# MU_XOR_REMAP picks Main's MakeObfuscation (below).
list(FILTER client_sources EXCLUDE REGEX "/src/app/App/MakeObfuscation[^/]*\\.cpp$")
add_library(ClientLib STATIC ${client_sources})
target_include_directories(ClientLib PUBLIC src src/game src/app)
target_compile_features(ClientLib PUBLIC cxx_std_20)
target_link_libraries(ClientLib PUBLIC Engine)
target_link_libraries(ClientLib PRIVATE CLI11::CLI11)
mu_set_warnings(ClientLib)
mu_set_fidelity_flags(ClientLib)
mu_precompile_headers(ClientLib)
# MSVC: game/Session/ServerSession.cpp, con los handlers que suman las ventanas de los NPC (hito 6 de la CL9) y el asedio
# (hito 12), pasa el tope de secciones de un .obj (C1128). /bigobj para toda la biblioteca, así su precompiled header
# compila con las mismas opciones que sus archivos, como en tests/.
if(MSVC)
    target_compile_options(ClientLib PRIVATE /bigobj)
endif()

# The XOR and remap layer is not a library of its own (ADR 0028): a static target that only Main,
# test_make_obfuscation and the layer's tests link. MU_MAKE_OBFUSCATION_SOURCE defines App/MakeObfuscation.h for
# the first two.
option(MU_XOR_REMAP "Build the XOR and remap layer the lab server expects" ON)
if(MU_XOR_REMAP)
    add_library(XorRemap STATIC src/engine/Network/XorRemap/XorRemapObfuscation.cpp)
    target_link_libraries(XorRemap PUBLIC Engine)
    mu_set_warnings(XorRemap)
    set(MU_MAKE_OBFUSCATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/App/MakeObfuscationXorRemap.cpp")
else()
    set(MU_MAKE_OBFUSCATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/App/MakeObfuscationNone.cpp")
endif()

add_executable(Main
    src/app/main.cpp
    ${MU_MAKE_OBFUSCATION_SOURCE}
)
# main.exe, as the original's (ADR 0028).
set_target_properties(Main PROPERTIES OUTPUT_NAME main)
target_link_libraries(Main PRIVATE ClientLib OpenGL Platform Audio Engine SDL3::SDL3)
if(MU_XOR_REMAP)
    target_link_libraries(Main PRIVATE XorRemap)
endif()
# La versión que muestra el login (el texto 456 de Text.bmd, CL5.4): la del proyecto.
target_compile_definitions(Main PRIVATE MU_VERSION="${PROJECT_VERSION}")
mu_set_warnings(Main)

if(MU_BUILD_TESTS)
    add_subdirectory(tests)

    # Main arranca (encuentra SDL3, en Windows la DLL) y rechaza un argumento malo antes de abrir
    # la ventana, así que corre sin pantalla. Si faltara la DLL, no imprimiría nada y fallaría.
    add_test(NAME Client.Main.RejectsBadArguments COMMAND Main --frames 0)
    set_tests_properties(Client.Main.RejectsBadArguments PROPERTIES
        PASS_REGULAR_EXPRESSION "--frames needs a number greater than 0")
    # A bad port stops before SDL_Init with ParsePort's message (CL3.4). PASS_REGULAR_EXPRESSION
    # checks only the output, not the exit code.
    add_test(NAME Client.Main.RejectsBadPort COMMAND Main --host 127.0.0.1 --port 0)
    set_tests_properties(Client.Main.RejectsBadPort PROPERTIES
        PASS_REGULAR_EXPRESSION "--port: the port must be from 1 to 65535")
    # The launcher's form is Windows only; Linux ignores it (AR1.3).
    if(WIN32)
        add_test(NAME Client.Main.RejectsBadLauncherPort COMMAND Main connect /u127.0.0.1 /p0)
        set_tests_properties(Client.Main.RejectsBadLauncherPort PROPERTIES
            PASS_REGULAR_EXPRESSION "/p: the port must be from 1 to 65535")
    endif()
    # Without PASS_REGULAR_EXPRESSION, a test passes only with exit code 0.
    add_test(NAME Client.Main.PrintsTheHelp COMMAND Main --help)
    add_test(NAME Client.Main.PrintsTheVersion COMMAND Main --version)
    set_tests_properties(Client.Main.PrintsTheVersion PROPERTIES
        PASS_REGULAR_EXPRESSION "MuRet client ${PROJECT_VERSION}")
    add_test(NAME Client.Main.AnUnknownOptionShowsTheUsage COMMAND Main --fullscreen)
    set_tests_properties(Client.Main.AnUnknownOptionShowsTheUsage PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown option \"--fullscreen\".*Usage: Main")
    # Q-C35: una carpeta de datos que no existe se nombra, antes de SDL_Init (Resolve).
    add_test(NAME Client.Main.NamesTheMissingDataFolder
        COMMAND Main --data ${CMAKE_CURRENT_BINARY_DIR}/no-existe-esta-carpeta-de-datos)
    set_tests_properties(Client.Main.NamesTheMissingDataFolder PROPERTIES
        PASS_REGULAR_EXPRESSION "no existe la carpeta de datos .*no-existe-esta-carpeta-de-datos")
    # Los argumentos llegan en UTF-8 (main.cpp incluye SDL_main.h): --data nombra un archivo con
    # "Ł" y "データ", que no están en la página ANSI. Si llegan bien, Resolve lo encuentra y dice
    # que no es una carpeta; sin SDL_main.h, en Windows llegarían cambiados por "?" y diría que no
    # existe. Mira el sistema de archivos y no la salida, que CTest puede leer en otra página. En
    # Linux pasa también sin SDL_main.h: los bytes van tal cual.
    set(utf8_data_file "${CMAKE_CURRENT_BINARY_DIR}/datos-Łukasz-データ")
    file(WRITE "${utf8_data_file}" "")
    add_test(NAME Client.Main.ReadsTheDataPathInUtf8 COMMAND Main --data "${utf8_data_file}")
    set_tests_properties(Client.Main.ReadsTheDataPathInUtf8 PROPERTIES
        PASS_REGULAR_EXPRESSION "la carpeta de datos .* no es una carpeta")
    # Con un host y --auto-login (CL5.5), primero la cuenta y la clave del entorno (pregunta 7 del
    # plan de la CL4): sin las variables, la salida nombra las dos, antes de mirar las claves y sin
    # red ni ventana.
    add_test(NAME Client.Main.ConnectWithoutCredentialsNamesThem
        COMMAND Main --host 127.0.0.1 --port 44405 --auto-login)
    set_tests_properties(Client.Main.ConnectWithoutCredentialsNamesThem PROPERTIES
        PASS_REGULAR_EXPRESSION "falta MU_CLIENT_ACCOUNT; falta MU_CLIENT_PASSWORD")
    # --auto-login sin un host no tiene a qué entrar: Resolve lo dice antes de abrir la ventana.
    add_test(NAME Client.Main.AutoLoginWithoutAServerSaysSo COMMAND Main --auto-login)
    set_tests_properties(Client.Main.AutoLoginWithoutAServerSaysSo PROPERTIES
        PASS_REGULAR_EXPRESSION "--auto-login needs a server")
    # Sin --auto-login, la cuenta la teclea el jugador y el entorno no se mira: un host pide solo
    # las claves de la conexión. Una carpeta de datos vacía no las tiene, y la salida nombra
    # Enc1.dat y Dec2.dat, sin abrir la ventana.
    set(empty_data_folder "${CMAKE_CURRENT_BINARY_DIR}/datos-sin-claves")
    file(MAKE_DIRECTORY "${empty_data_folder}")
    add_test(NAME Client.Main.ConnectWithoutKeysNamesThem
        COMMAND Main --host 127.0.0.1 --port 44405 --data "${empty_data_folder}")
    set_tests_properties(Client.Main.ConnectWithoutKeysNamesThem PROPERTIES
        PASS_REGULAR_EXPRESSION "no está Enc1.dat; no está Dec2.dat")
    # Con una carpeta de datos y sin host, las escenas (CL5.4): primero Text.bmd, como WinMain antes
    # de la carga. Una carpeta sin él lo nombra, sin abrir la ventana.
    add_test(NAME Client.Main.ScenesWithoutTheTextTableNameIt COMMAND Main --data "${empty_data_folder}" --frames 1)
    set_tests_properties(Client.Main.ScenesWithoutTheTextTableNameIt PROPERTIES
        PASS_REGULAR_EXPRESSION "no está Local.Text.bmd en .*datos-sin-claves")
    # Ningún test de Main ve las variables de quien corre CTest: sin --auto-login no se leen, y con
    # él, el de arriba las necesita sin poner.
    set_tests_properties(Client.Main.RejectsBadArguments Client.Main.RejectsBadPort Client.Main.PrintsTheHelp
        Client.Main.PrintsTheVersion Client.Main.AnUnknownOptionShowsTheUsage
        Client.Main.NamesTheMissingDataFolder Client.Main.ReadsTheDataPathInUtf8
        Client.Main.ConnectWithoutCredentialsNamesThem Client.Main.AutoLoginWithoutAServerSaysSo
        Client.Main.ConnectWithoutKeysNamesThem Client.Main.ScenesWithoutTheTextTableNameIt PROPERTIES
        ENVIRONMENT_MODIFICATION "MU_CLIENT_ACCOUNT=unset:;MU_CLIENT_PASSWORD=unset:")
    if(WIN32)
        set_tests_properties(Client.Main.RejectsBadLauncherPort PROPERTIES
            ENVIRONMENT_MODIFICATION "MU_CLIENT_ACCOUNT=unset:;MU_CLIENT_PASSWORD=unset:")
    endif()
endif()
