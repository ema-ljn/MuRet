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
endif()
