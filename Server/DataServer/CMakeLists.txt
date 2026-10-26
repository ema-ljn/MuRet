# DataServer: el proceso que traduce los pedidos de los GameServers a SQL.
# Por ahora es solo el ejecutable; las capas (Network, Protocol, Dispatch,
# Handlers, Database) entran como bibliotecas a partir de la ola 1.

add_executable(DataServer
    src/main.cpp
)

target_compile_features(DataServer PRIVATE cxx_std_20)
muret_set_warnings(DataServer)
