# DataServer

Reimplementación del `Dataserver.exe` de MU Online 1.02: el único proceso que toca la
base `MuOnline`. Los GameServers le piden cuentas, personajes, baúl, gremios y seriales
de items por TCP (puerto 55960, paquetes C1/C2) y él lo traduce a SQL.

El comportamiento a respetar está en el
[contrato](../../docs/superpowers/specs/2026-10-25-dataserver-port-design.md#parte-2-comportamiento-del-dataserver-contrato),
reconstruido en [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i).

## Estado

Ola 0.1: el ejecutable compila y corre, pero todavía no abre sockets ni toca la base.

## Compilar y correr

Desde la raíz del repo:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
./build/linux-x64-debug/Server/DataServer/DataServer
```

En Windows: `cmake --preset windows-x64`, `cmake --build --preset windows-x64-debug` y
`build\windows-x64\Server\DataServer\Debug\DataServer.exe`.

## Estructura (a medida que avanzan las olas)

```
Server/DataServer/
  CMakeLists.txt
  src/
    main.cpp      arma las capas y arranca; sin lógica
    Core/         config, log, errores        (ola 1+)
    Network/      IoBackend, Session, FrameParser (olas 1-3)
    Protocol/     heads y structs             (ola 1)
    Dispatch/     cola y dispatcher           (ola 4)
    Handlers/     un archivo por dominio      (olas 5 y 7)
    Database/     repositorios Memory y Odbc  (olas 5 y 6)
  tests/          GoogleTest                  (ola 0.2+)
  docs/protocol/  un .md por dominio de heads
```
