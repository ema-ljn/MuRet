# Client: la reimplementación del main.exe de MU Online 1.02.
#
# Tres targets (CL0.2): ClientLib tiene todo el código, Main es el ejecutable y ClientTests los
# tests, y los dos últimos enlazan contra ClientLib. Las capas (Core, Platform, Data, Network,
# Protocol, Render, Scenes) van entrando a ClientLib ola por ola.

find_package(SDL3 CONFIG REQUIRED)
# La libjpeg de libjpeg-turbo, para los OZJ (JpegImage.cpp). Se pide la de libjpeg-turbo y no
# cualquier libjpeg (find_package(JPEG)): es la que se comparó con la 6b del original.
find_package(libjpeg-turbo CONFIG REQUIRED)

add_library(ClientLib STATIC
    src/App/CommandLine.cpp
    src/Core/ByteReader.cpp
    src/Core/Xor3.cpp
    src/Data/DataPath.cpp
    src/Data/Tables/GateTable.cpp
    src/Data/Tables/ItemTable.cpp
    src/Data/Tables/SkillTable.cpp
    src/Data/Tables/TableChecksum.cpp
    src/Data/Terrain/K16.cpp
    src/Data/Terrain/TerrainAttributes.cpp
    src/Data/Terrain/TerrainHeights.cpp
    src/Data/Terrain/TerrainLight.cpp
    src/Data/Terrain/TerrainMapping.cpp
    src/Data/Terrain/TerrainObjects.cpp
    src/Data/Text/TextTable.cpp
    src/Data/Textures/JpegImage.cpp
    src/Data/Textures/OzjTexture.cpp
    src/Data/Textures/OztTexture.cpp
    src/Network/EncryptedPacket.cpp
    src/Network/FoxAntiObfuscation.cpp
    src/Network/IdentityObfuscation.cpp
    src/Network/Packet.cpp
    src/Network/PacketReader.cpp
    src/Network/PacketSerials.cpp
    src/Network/PacketWriter.cpp
    src/Network/Crypto/SimpleModulus.cpp
    src/Network/Crypto/SimpleModulusKeyFile.cpp
    src/Network/Crypto/Xor32.cpp
    src/Protocol/PacketFields.cpp
    src/Protocol/Connection/Hello.cpp
    src/Protocol/Connection/Keepalive.cpp
    src/Protocol/Connection/Login.cpp
    src/Protocol/Connection/ServerList.cpp
)
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
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(ClientTests
        tests/app/CommandLineTest.cpp
        tests/core/ByteReaderTest.cpp
        tests/core/ExpectedTest.cpp
        tests/core/Xor3Test.cpp
        tests/support/FileBytes.cpp
        tests/support/FileBytesTest.cpp
        tests/support/Fnv1aTest.cpp
        tests/support/GoldenDir.cpp
        tests/support/GoldenDirTest.cpp
        tests/support/SameBytesTest.cpp
        tests/support/WireCapture.cpp
        tests/support/WireCaptureTest.cpp
        tests/data/DataPathTest.cpp
        tests/data/GateTableTest.cpp
        tests/data/GoldenDataFilesTest.cpp
        tests/data/GoldenGateTableTest.cpp
        tests/data/GoldenOztTextureTest.cpp
        tests/data/GoldenItemTableTest.cpp
        tests/data/GoldenJpegImageTest.cpp
        tests/data/GoldenOzjTextureTest.cpp
        tests/data/GoldenSkillTableTest.cpp
        tests/data/GoldenTableChecksumTest.cpp
        tests/data/GoldenTerrainAttributesTest.cpp
        tests/data/GoldenTerrainHeightsTest.cpp
        tests/data/GoldenTerrainLightTest.cpp
        tests/data/GoldenTerrainMappingTest.cpp
        tests/data/GoldenTerrainObjectsTest.cpp
        tests/data/GoldenTextTableTest.cpp
        tests/data/ItemTableTest.cpp
        tests/data/JpegImageTest.cpp
        tests/data/K16Test.cpp
        tests/data/OzjTextureTest.cpp
        tests/data/OztTextureTest.cpp
        tests/data/RecordTableTest.cpp
        tests/data/SkillTableTest.cpp
        tests/data/TableChecksumTest.cpp
        tests/data/TerrainAttributesTest.cpp
        tests/data/TerrainHeightsTest.cpp
        tests/data/TerrainLightTest.cpp
        tests/data/TerrainMappingTest.cpp
        tests/data/TerrainObjectsTest.cpp
        tests/data/TextTableTest.cpp
        tests/data/TextureTest.cpp
        tests/network/EncryptedPacketTest.cpp
        tests/network/FoxAntiObfuscationTest.cpp
        tests/network/GoldenEncryptedPacketTest.cpp
        tests/network/GoldenFoxAntiObfuscationTest.cpp
        tests/network/GoldenPacketReaderTest.cpp
        tests/network/GoldenSimpleModulusKeyFileTest.cpp
        tests/network/IdentityObfuscationTest.cpp
        tests/network/PacketReaderTest.cpp
        tests/network/PacketSerialsTest.cpp
        tests/network/PacketTest.cpp
        tests/network/PacketWriterTest.cpp
        tests/network/SimpleModulusKeyFileTest.cpp
        tests/network/SimpleModulusTest.cpp
        tests/network/Xor32Test.cpp
        tests/protocol/BuildConstantsTest.cpp
        tests/protocol/GoldenLoginTest.cpp
        tests/protocol/HelloTest.cpp
        tests/protocol/KeepaliveTest.cpp
        tests/protocol/LoginTest.cpp
        tests/protocol/PacketFieldsTest.cpp
        tests/protocol/ServerListTest.cpp
    )
    target_include_directories(ClientTests PRIVATE tests)
    # Los vectores JPEG y los de red (el README.md de tests/vectors/jpeg y de tests/vectors/net)
    # están en el repo: los tests los leen de acá.
    target_compile_definitions(ClientTests PRIVATE
        MURET_CLIENT_JPEG_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors/jpeg"
        MURET_CLIENT_NET_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors/net")
    target_link_libraries(ClientTests PRIVATE ClientLib GTest::gtest GTest::gtest_main)
    muret_set_warnings(ClientTests)

    # La carpeta golden llega a los tests como variable de entorno. Se puede fijar acá, al
    # configurar (-DMURET_CLIENT_GOLDEN_DIR=...), o en el entorno al correr ctest. La de los
    # volcados de memoria (MURET_CLIENT_MEMORY_DIR) y la del cable (MURET_CLIENT_WIRE_DIR) van
    # solo en el entorno: gtest_discover_tests parte en dos una lista de variables en ENVIRONMENT,
    # y la segunda se pierde.
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
