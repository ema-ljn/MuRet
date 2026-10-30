# Client

Reimplementación del `main.exe` de MU Online 1.02 (build `c32bbd5f…`): el programa del jugador.
Se conecta al ConnectServer, elige un GameServer, hace login y muestra la selección de personaje y
el mundo 3D. En C++20, para Windows y Linux, con SDL3 y OpenGL 3.3.

La regla: **modernizar sin sacrificar fidelidad**. Los bytes en la red, la lectura de los archivos
`Data\` del cliente original y la velocidad de la lógica son los del original; lo moderno es la
implementación y lo que el original no fija (por ejemplo, dibujar a más FPS entre dos ticks).

Qué hace el original, byte a byte, está en el [research](docs/research/README.md) (un archivo por
tema), reconstruido desde la carpeta `client-research/` de
[EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i).
Qué hace MuRet y en qué orden está en el
[roadmap del cliente](../docs/superpowers/specs/2026-10-30-cliente-design.md).

MuRet **no incluye assets del juego**: el cliente lee los `Data\` de una instalación existente.

## Estado

Ola CL0.1: el ejecutable compila y corre, pero todavía no abre una ventana ni se conecta.

## Compilar y correr

Desde la raíz del repo:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
./build/linux-x64-debug/Client/Main
```

En Windows: `cmake --preset windows-x64`, `cmake --build --preset windows-x64-debug` y
`build\windows-x64\Client\Debug\Main.exe`.

## Estructura (a medida que avanzan las olas)

```
Client/
  CMakeLists.txt
  src/
    main.cpp      arma las capas y arranca; sin lógica
    App/          bucle de juego con tick fijo          (CL3)
    Core/         config, log, reloj                    (CL0+)
    Platform/     ventana, contexto GL, input (SDL3)    (CL3)
    Data/         .bmd, terreno, OZJ/OZT, Text.bmd      (CL1)
    Network/      socket, paquetes, C3/C4, FoxAnti      (CL2, CL4)
    Protocol/     opcodes, structs, dispatcher          (CL2, CL4, CL9)
    Render/       OpenGL 3.3: texturas, sprites, texto  (CL5+)
    Scenes/       login, selección, juego               (CL5, CL6)
    World/        mapa, cámara, objetos                 (CL7)
    GameLogic/    personaje, entidades, items, chat     (CL7+)
    UI/           widgets, ventanas, HUD                (CL5, CL8)
    Audio/        música y efectos                      (CL10)
  tests/          GoogleTest                            (CL0.2+)
  docs/research/  qué hace el original, byte a byte (01..08)
```
