# labs/: el código de las N.0 del camino de aprendizaje
# (docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md). Compila en Windows y en
# Linux como el resto del repo, pero no es parte de ningún servidor ni del cliente.
#
# Los tests de la 0.0 son ejecutables que salen con 0 si pasan y con otro valor si fallan,
# registrados con add_test(). Desde la CL2.0 hay GoogleTest, el mismo del cliente (vcpkg.json).
# CTest corre los dos igual:
#   ctest --test-dir build/linux-x64-debug --output-on-failure

# Misión de la 0.0 (docs/learning/ola-0.0-del-codigo-al-programa.md, actividad 5): la biblioteca
# estática y su test. PUBLIC en el include: quien enlace MuRetLabs también ve su header.
add_library(MuRetLabs STATIC
    0.0-enlace/HeadOffset.cpp
)
target_include_directories(MuRetLabs PUBLIC 0.0-enlace)
target_compile_features(MuRetLabs PUBLIC cxx_std_20)
muret_set_warnings(MuRetLabs)

add_executable(Lab00HeadOffsetTest 0.0-enlace/HeadOffsetTest.cpp)
target_link_libraries(Lab00HeadOffsetTest PRIVATE MuRetLabs)
muret_set_warnings(Lab00HeadOffsetTest)
add_test(NAME Lab00.HeadOffset COMMAND Lab00HeadOffsetTest)

# Jefe de la 0.0 (actividad 10): los mismos objetos en dos órdenes. Sin optimizar, nadie expande
# la función inline en su lugar: las dos unidades llaman al símbolo, y el linker se queda con la
# copia del primer objeto que ve. Con -O0 (/Ob0 en MSVC) el resultado no depende de Debug o
# Release; lo que pasa optimizando está en 0.0-enlace/README.md.
set(lab00_no_inline $<IF:$<CXX_COMPILER_ID:MSVC>,/Ob0,-O0>)

add_executable(Lab00OdrAB 0.0-enlace/jefe/OdrMain.cpp 0.0-enlace/jefe/OdrA.cpp 0.0-enlace/jefe/OdrB.cpp)
add_executable(Lab00OdrBA 0.0-enlace/jefe/OdrMain.cpp 0.0-enlace/jefe/OdrB.cpp 0.0-enlace/jefe/OdrA.cpp)
foreach(target Lab00OdrAB Lab00OdrBA)
    target_compile_options(${target} PRIVATE ${lab00_no_inline})
    muret_set_warnings(${target})
endforeach()

# Las dos unidades dan lo mismo, y qué dan depende del orden: la ODR rota, sin diagnóstico.
add_test(NAME Lab00.OdrBoss.AB COMMAND Lab00OdrAB)
set_tests_properties(Lab00.OdrBoss.AB PROPERTIES PASS_REGULAR_EXPRESSION "FromA=2 FromB=2")
add_test(NAME Lab00.OdrBoss.BA COMMAND Lab00OdrBA)
set_tests_properties(Lab00.OdrBoss.BA PROPERTIES PASS_REGULAR_EXPRESSION "FromA=3 FromB=3")

# La CL2.0 (docs/learning/ola-cl2.0-flujos-y-bloques.md): el corte de un flujo, los bits MSB primero
# y el inverso modular, en su propia biblioteca.
add_library(MuRetLabsCl20 STATIC
    cl2.0-flujos/BitStream.cpp
    cl2.0-flujos/FrameCutter.cpp
    cl2.0-flujos/ModularInverse.cpp
)
target_include_directories(MuRetLabsCl20 PUBLIC cl2.0-flujos)
target_compile_features(MuRetLabsCl20 PUBLIC cxx_std_20)
muret_set_warnings(MuRetLabsCl20)

# La CL3.0 (docs/learning/ola-cl3.0-el-bucle-de-juego.md): el paso fijo con acumulador y el reloj de
# GetTickCount; el bucle del original va en el jefe, dentro de su test.
add_library(MuRetLabsCl30 STATIC
    cl3.0-bucle/FixedStep.cpp
    cl3.0-bucle/TickGrid.cpp
)
target_include_directories(MuRetLabsCl30 PUBLIC cl3.0-bucle)
target_compile_features(MuRetLabsCl30 PUBLIC cxx_std_20)
muret_set_warnings(MuRetLabsCl30)

# La CL4.0 (docs/learning/ola-cl4.0-sockets-no-bloqueantes.md): el socket con RAII, el par en
# 127.0.0.1 y la cola con cursor. En Windows, los sockets son de Winsock 2 (ws2_32).
add_library(MuRetLabsCl40 STATIC
    cl4.0-sockets/DrainQueue.cpp
    cl4.0-sockets/LabSocket.cpp
    cl4.0-sockets/LoopbackPair.cpp
)
target_include_directories(MuRetLabsCl40 PUBLIC cl4.0-sockets)
target_compile_features(MuRetLabsCl40 PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(MuRetLabsCl40 PUBLIC ws2_32)
endif()
muret_set_warnings(MuRetLabsCl40)

# La CL5.0 (docs/learning/ola-cl5.0-el-pipeline-grafico.md): el quad de 0x621b60, la matriz de
# gluOrtho2D escrita a mano y el fan pasado a triángulos. Sin GL: corre en los dos sistemas.
add_library(MuRetLabsCl50 STATIC
    cl5.0-pipeline/Fan.cpp
    cl5.0-pipeline/OriginalQuad.cpp
    cl5.0-pipeline/Ortho.cpp
)
target_include_directories(MuRetLabsCl50 PUBLIC cl5.0-pipeline)
target_compile_features(MuRetLabsCl50 PUBLIC cxx_std_20)
muret_set_warnings(MuRetLabsCl50)

# Lo de la CL5.0 que habla con OpenGL (cl5.0-pipeline/gl/): la ventana escondida con el contexto
# 3.3 core, la tabla de funciones del lab, los nombres de GL con RAII, el framebuffer y el quad
# dibujado. Enlaza ClientPlatform por lo que ya tiene el cliente: SdlContext, los deleters de la
# ventana y del contexto, y la tabla de GL con su pedido, que mira la versión y el perfil (CL5.1).
add_library(MuRetLabsCl50Gl STATIC
    cl5.0-pipeline/gl/GlName.cpp
    cl5.0-pipeline/gl/HiddenGlWindow.cpp
    cl5.0-pipeline/gl/LabFramebuffer.cpp
    cl5.0-pipeline/gl/LabGl.cpp
    cl5.0-pipeline/gl/LabQuadRenderer.cpp
    cl5.0-pipeline/gl/LabTexture.cpp
)
target_include_directories(MuRetLabsCl50Gl PUBLIC cl5.0-pipeline/gl)
target_link_libraries(MuRetLabsCl50Gl PUBLIC MuRetLabsCl50 ClientPlatform)
muret_set_warnings(MuRetLabsCl50Gl)

if(MURET_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    # Los tests de las misiones y el jefe (Q-C03). Cada test de GoogleTest es un test de CTest,
    # con el prefijo LabCl20. para no mezclarse con los del cliente. Los archivos se llaman
    # test_<algo>.cpp como en el cliente, pero van en un solo ejecutable: el lab es la unidad que se
    # aprende (docs/CODING_RULES.md, regla 7).
    add_executable(LabCl20Tests
        cl2.0-flujos/test_bit_stream.cpp
        cl2.0-flujos/test_frame_cutter.cpp
        cl2.0-flujos/test_modular_inverse.cpp
        cl2.0-flujos/jefe/test_old_cut.cpp
    )
    target_link_libraries(LabCl20Tests PRIVATE MuRetLabsCl20 GTest::gtest GTest::gtest_main)
    muret_set_warnings(LabCl20Tests)
    gtest_discover_tests(LabCl20Tests TEST_PREFIX "LabCl20.")

    # Los de la CL3.0, con el prefijo LabCl30. y en un solo ejecutable, como los de la CL2.0.
    add_executable(LabCl30Tests
        cl3.0-bucle/test_fixed_step.cpp
        cl3.0-bucle/test_tick_grid.cpp
        cl3.0-bucle/jefe/test_original_loop.cpp
    )
    target_link_libraries(LabCl30Tests PRIVATE MuRetLabsCl30 GTest::gtest GTest::gtest_main)
    muret_set_warnings(LabCl30Tests)
    gtest_discover_tests(LabCl30Tests TEST_PREFIX "LabCl30.")

    # Los de la CL4.0, con el prefijo LabCl40.: los del socket y el par usan sockets de verdad en
    # 127.0.0.1, con un puerto que elige el sistema, así que corren en CI sin red de afuera.
    add_executable(LabCl40Tests
        cl4.0-sockets/test_drain_queue.cpp
        cl4.0-sockets/test_lab_socket.cpp
        cl4.0-sockets/test_loopback_pair.cpp
        cl4.0-sockets/jefe/test_original_drain.cpp
    )
    target_link_libraries(LabCl40Tests PRIVATE MuRetLabsCl40 GTest::gtest GTest::gtest_main)
    muret_set_warnings(LabCl40Tests)
    gtest_discover_tests(LabCl40Tests TEST_PREFIX "LabCl40.")

    # glm (vcpkg.json): la CL5.0 compara su matriz a mano con glm::ortho. Solo los headers: el
    # puerto de vcpkg también arma una biblioteca (glm::glm), que el lab no necesita.
    find_package(glm CONFIG REQUIRED)

    # Los de la CL5.0 sin GL, con el prefijo LabCl50.: corren en los dos sistemas.
    add_executable(LabCl50Tests
        cl5.0-pipeline/test_fan.cpp
        cl5.0-pipeline/test_original_quad.cpp
        cl5.0-pipeline/test_ortho.cpp
        cl5.0-pipeline/jefe/test_original_pipeline.cpp
    )
    target_link_libraries(LabCl50Tests PRIVATE MuRetLabsCl50 glm::glm-header-only GTest::gtest GTest::gtest_main)
    muret_set_warnings(LabCl50Tests)
    gtest_discover_tests(LabCl50Tests TEST_PREFIX "LabCl50.")

    # Los de cl5.0-pipeline/gl/, con el prefijo LabCl50Gl. y la etiqueta gl de CTest: los que
    # dibujan abren una ventana escondida con un contexto 3.3 core. Sin pantalla o sin 3.3 se
    # saltean, salvo con MURET_CLIENT_REQUIRE_GL=1, que los hace fallar: el paso de Xvfb del CI
    # corre `ctest -L '^gl$'` con esa variable (la pregunta 9 del plan de la CL5). Cada uno tiene
    # 60 s: un driver colgado en Xvfb falla el test en vez de frenar el CI hasta su tope de 6 h.
    add_executable(LabCl50GlTests
        cl5.0-pipeline/gl/test_gl_name.cpp
        cl5.0-pipeline/gl/test_hidden_gl_window.cpp
        cl5.0-pipeline/gl/test_lab_framebuffer.cpp
        cl5.0-pipeline/gl/test_lab_texture.cpp
    )
    target_link_libraries(LabCl50GlTests PRIVATE MuRetLabsCl50Gl GTest::gtest GTest::gtest_main)
    muret_set_warnings(LabCl50GlTests)
    gtest_discover_tests(LabCl50GlTests TEST_PREFIX "LabCl50Gl." PROPERTIES LABELS gl TIMEOUT 60)
endif()
