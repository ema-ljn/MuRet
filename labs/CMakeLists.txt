# labs/: el código de las N.0 del camino de aprendizaje
# (docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md). Compila en Windows y en
# Linux como el resto del repo, pero no es parte de ningún servidor ni del cliente.
#
# Hasta la 0.2 no hay GoogleTest: un test es un ejecutable que sale con 0 si pasa y con otro valor
# si falla, registrado con add_test(). CTest lo corre igual:
#   ctest --test-dir build/linux-x64-debug --output-on-failure

# TODO(human): misión de la 0.0 (docs/learning/ola-0.0-del-codigo-al-programa.md, actividad 5).
#   - la biblioteca estática MuRetLabs, con 0.0-enlace/HeadOffset.cpp
#   - el ejecutable del test, con 0.0-enlace/HeadOffsetTest.cpp, enlazado contra MuRetLabs
#   - su add_test()
# Cada target lleva muret_set_warnings(), como en Server/DataServer.
