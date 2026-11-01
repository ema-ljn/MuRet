# Client: la reimplementación del main.exe de MU Online 1.02.
# Por ahora es solo el ejecutable; las capas (Platform, Data, Network, Protocol,
# Render, Scenes) entran como bibliotecas a partir de la ola CL0.2.

add_executable(Main
    src/main.cpp
)

target_compile_features(Main PRIVATE cxx_std_20)
muret_set_warnings(Main)
