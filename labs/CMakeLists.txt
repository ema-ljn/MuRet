# labs/: el código de las N.0 del camino de aprendizaje
# (docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md). Compila en Windows y en
# Linux como el resto del repo, pero no es parte de ningún servidor ni del cliente.
#
# Hasta la 0.2 no hay GoogleTest: un test es un ejecutable que sale con 0 si pasa y con otro valor
# si falla, registrado con add_test(). CTest lo corre igual:
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
