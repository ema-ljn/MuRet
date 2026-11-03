# Client: la reimplementación del main.exe de MU Online 1.02.
#
# ClientLib tiene todo el código que no ve SDL3, ClientPlatform lo que sí (src/Platform/, CL3.1),
# ClientRender lo que habla con OpenGL (src/Render/Gl/, CL5.1) y Main es el ejecutable (CL0.2). Los
# tests van en tests/, un ejecutable por archivo como en sven-n/MuMain (tests/CMakeLists.txt). Las
# capas (Core, Data, Network, Protocol, Render, Scenes) van entrando a ClientLib ola por ola.

find_package(SDL3 CONFIG REQUIRED)
# La libjpeg de libjpeg-turbo, para los OZJ (JpegImage.cpp). Se pide la de libjpeg-turbo y no
# cualquier libjpeg (find_package(JPEG)): es la que se comparó con la 6b del original.
find_package(libjpeg-turbo CONFIG REQUIRED)
# glm (vcpkg.json, desde la CL5.0): la matriz del 2D de ClientRender (pregunta 6 de la CL5) y, desde
# la CL6.2, los cuaterniones y las matrices de Render/Models/ en ClientLib. Solo los headers: el
# puerto también arma una biblioteca (glm::glm), que no hace falta.
find_package(glm CONFIG REQUIRED)
# FreeType (vcpkg.json, desde la CL5.3): el texto en un bit de Render/Text/FreeTypeRasterizer.cpp
# (pregunta 7 de la CL5). Es el FindFreetype de CMake, que el puerto de vcpkg envuelve, y en Linux
# sin vcpkg encuentra el del sistema (libfreetype-dev).
find_package(Freetype REQUIRED)

# Los fuentes de src/ se juntan solos, como en MuMain: un .cpp nuevo entra al reconfigurar
# (CONFIGURE_DEPENDS lo mira en cada build). main.cpp es de Main, no de la biblioteca, src/Platform/
# es de ClientPlatform y src/Render/Gl/ de ClientRender: ClientLib no enlaza SDL3 ni ve GL (como
# los list(FILTER MU_CLIENT_SOURCES EXCLUDE REGEX ...) de src/CMakeLists.txt en MuMain).
file(GLOB_RECURSE client_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER client_sources EXCLUDE REGEX "/src/main\\.cpp$")
list(FILTER client_sources EXCLUDE REGEX "/src/Platform/")
list(FILTER client_sources EXCLUDE REGEX "/src/Render/Gl/")
add_library(ClientLib STATIC ${client_sources})
target_include_directories(ClientLib PUBLIC src)
target_compile_features(ClientLib PUBLIC cxx_std_20)
# En vcpkg, libjpeg-turbo es estática en Linux y dinámica en Windows: el target cambia de nombre.
target_link_libraries(ClientLib PRIVATE
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::jpeg>,libjpeg-turbo::jpeg,libjpeg-turbo::jpeg-static>)
# Sus headers quedan en FreeTypeRasterizer.cpp: nadie más los ve.
target_link_libraries(ClientLib PRIVATE Freetype::Freetype)
# PUBLIC: los headers de Render/Models/ usan sus tipos (pregunta 4 del plan de la CL6). Sin
# GLM_FORCE_* ni GLM_ENABLE_EXPERIMENTAL, así el precompiled header no cambia.
target_link_libraries(ClientLib PUBLIC glm::glm-header-only)
# Los sockets de Network/SocketApi.cpp (CL4.1): en Windows son de Winsock 2, ws2_32. En Linux están
# en la libc.
if(WIN32)
    target_link_libraries(ClientLib PRIVATE ws2_32)
endif()
muret_set_warnings(ClientLib)
muret_precompile_headers(ClientLib)
# libjpeg avisa de sus errores con longjmp, y JpegImage.cpp lo recibe con setjmp. MSVC avisa de
# cualquier setjmp en C++ (C4611: longjmp no corre destructores); el archivo está armado para que
# no haya ninguno en el medio (Decompress, en JpegImage.cpp).
if(MSVC)
    set_source_files_properties(src/Data/Textures/JpegImage.cpp PROPERTIES COMPILE_OPTIONS /wd4611)
endif()

# Platform (CL3.1): lo único que ve SDL3. La ventana, el contexto GL, el loader, el intervalo de
# swap y la traducción de eventos. Sus tests (tests/platform/) lo enlazan aparte.
file(GLOB_RECURSE platform_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Platform/*.cpp")
add_library(ClientPlatform STATIC ${platform_sources})
target_link_libraries(ClientPlatform PUBLIC ClientLib SDL3::SDL3)
# El registro de Platform/SystemFonts.cpp (CL5.3): en Windows, advapi32, que MSVC ya enlaza por
# defecto; va explícito, como ws2_32.
if(WIN32)
    target_link_libraries(ClientPlatform PRIVATE advapi32)
endif()
# Los typedef PFNGL…PROC de las funciones de GL 1.x (glClear, glViewport); los de la 3.3 vienen
# siempre. Con ellos se tipan los punteros que da SDL_GL_GetProcAddress, y GlLoader.h los expone:
# por eso es PUBLIC (Main y los tests de platform/ los ven).
target_compile_definitions(ClientPlatform PUBLIC SDL_OPENGL_1_FUNCTION_TYPEDEFS)
muret_set_warnings(ClientPlatform)
# Su propio precompiled header y no el de ClientLib: SDL_OPENGL_1_FUNCTION_TYPEDEFS cambia las
# definiciones (cmake/PrecompiledHeaders.cmake). ClientRender y Main ven las mismas y lo reusan.
muret_precompile_headers(ClientPlatform)

# Render/Gl (CL5.1): lo que habla con OpenGL. Los nombres de GL con RAII, los shaders, las texturas,
# el framebuffer de los tests y el pipeline del 2D. Llama a GL por la tabla de ClientPlatform
# (GlFunctions), así que la enlaza; lo que arma qué dibujar (Render/ sin Gl/) queda en ClientLib y
# se prueba sin ventana (pregunta 4 del plan de la CL5). Sus tests, en tests/render_gl/.
file(GLOB_RECURSE render_gl_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Gl/*.cpp")
add_library(ClientRender STATIC ${render_gl_sources})
target_link_libraries(ClientRender PUBLIC ClientPlatform PRIVATE glm::glm-header-only)
muret_set_warnings(ClientRender)
muret_reuse_precompiled_headers(ClientRender ClientPlatform)

add_executable(Main
    src/main.cpp
)
# El frame de las escenas (Render/Gl/GlFrameTarget, CL5.4) es de ClientRender.
target_link_libraries(Main PRIVATE ClientRender ClientPlatform ClientLib)
# La versión que muestra el login (el texto 456 de Text.bmd, CL5.4): la del proyecto.
target_compile_definitions(Main PRIVATE MURET_VERSION="${PROJECT_VERSION}")
muret_set_warnings(Main)
muret_reuse_precompiled_headers(Main ClientPlatform)

if(MURET_BUILD_TESTS)
    add_subdirectory(tests)

    # Main arranca (encuentra SDL3, en Windows la DLL) y rechaza un argumento malo antes de abrir
    # la ventana, así que corre sin pantalla. Si faltara la DLL, no imprimiría nada y fallaría.
    add_test(NAME Client.Main.RejectsBadArguments COMMAND Main --frames 0)
    set_tests_properties(Client.Main.RejectsBadArguments PROPERTIES
        PASS_REGULAR_EXPRESSION "--frames necesita un número mayor que 0")
    # Un puerto malo del launcher corta antes de SDL_Init, con el mensaje de ParsePort (CL3.4). Con
    # PASS_REGULAR_EXPRESSION, CTest mira solo la salida y no el código de salida (EXIT_FAILURE).
    add_test(NAME Client.Main.RejectsBadPort COMMAND Main connect /u127.0.0.1 /p0)
    set_tests_properties(Client.Main.RejectsBadPort PROPERTIES
        PASS_REGULAR_EXPRESSION "el puerto va de 1 a 65535")
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
        COMMAND Main connect /u127.0.0.1 /p44405 --auto-login)
    set_tests_properties(Client.Main.ConnectWithoutCredentialsNamesThem PROPERTIES
        PASS_REGULAR_EXPRESSION "falta MURET_CLIENT_ACCOUNT; falta MURET_CLIENT_PASSWORD")
    # --auto-login sin un host no tiene a qué entrar: Resolve lo dice antes de abrir la ventana.
    add_test(NAME Client.Main.AutoLoginWithoutAServerSaysSo COMMAND Main --auto-login)
    set_tests_properties(Client.Main.AutoLoginWithoutAServerSaysSo PROPERTIES
        PASS_REGULAR_EXPRESSION "--auto-login necesita un servidor")
    # Sin --auto-login, la cuenta la teclea el jugador y el entorno no se mira: un host pide solo
    # las claves de la conexión. Una carpeta de datos vacía no las tiene, y la salida nombra
    # Enc1.dat y Dec2.dat, sin abrir la ventana.
    set(empty_data_folder "${CMAKE_CURRENT_BINARY_DIR}/datos-sin-claves")
    file(MAKE_DIRECTORY "${empty_data_folder}")
    add_test(NAME Client.Main.ConnectWithoutKeysNamesThem
        COMMAND Main connect /u127.0.0.1 /p44405 --data "${empty_data_folder}")
    set_tests_properties(Client.Main.ConnectWithoutKeysNamesThem PROPERTIES
        PASS_REGULAR_EXPRESSION "no está Enc1.dat; no está Dec2.dat")
    # Con una carpeta de datos y sin host, las escenas (CL5.4): primero Text.bmd, como WinMain antes
    # de la carga. Una carpeta sin él lo nombra, sin abrir la ventana.
    add_test(NAME Client.Main.ScenesWithoutTheTextTableNameIt COMMAND Main --data "${empty_data_folder}" --frames 1)
    set_tests_properties(Client.Main.ScenesWithoutTheTextTableNameIt PROPERTIES
        PASS_REGULAR_EXPRESSION "no está Local.Text.bmd en .*datos-sin-claves")
    # Ningún test de Main ve las variables de quien corre CTest: sin --auto-login no se leen, y con
    # él, el de arriba las necesita sin poner.
    set_tests_properties(Client.Main.RejectsBadArguments Client.Main.RejectsBadPort
        Client.Main.NamesTheMissingDataFolder Client.Main.ReadsTheDataPathInUtf8
        Client.Main.ConnectWithoutCredentialsNamesThem Client.Main.AutoLoginWithoutAServerSaysSo
        Client.Main.ConnectWithoutKeysNamesThem Client.Main.ScenesWithoutTheTextTableNameIt PROPERTIES
        ENVIRONMENT_MODIFICATION "MURET_CLIENT_ACCOUNT=unset:;MURET_CLIENT_PASSWORD=unset:")
endif()
