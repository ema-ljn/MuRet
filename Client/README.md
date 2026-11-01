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

Ola CL1.4: `Main` abre una ventana SDL3 de 640×480 con OpenGL 3.3 core, la limpia de un color e
imprime `GL_VERSION` y `GL_RENDERER`; todavía no se conecta. `ClientLib` resuelve rutas de `Data\`
como Windows (sin distinguir mayúsculas) y lee desde sus bytes `Text.bmd`, `Gate.bmd`, `Item.bmd`
y `Skill.bmd`, con el XOR3 y el checksum de las tablas `.bmd`, y el terreno de un mundo:
`EncTerrain<N>.map` (las capas de tiles), `EncTerrain<N>.att` (los atributos) y `EncTerrain<N>.obj`
(los objetos), con su cifrado K16, y `TerrainHeight.OZB` (las alturas). De las texturas, por ahora
el OZT (un TGA de 32 bits), en RGBA y en un buffer de lados potencia de 2 como el del original.

## Compilar y correr

Hace falta vcpkg (las dependencias, SDL3 y GoogleTest, están en `vcpkg.json` y se bajan solas al
configurar) y la variable `VCPKG_ROOT` apuntando a su clon; ver el [README raíz](../README.md#compilar).
Desde la raíz del repo:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
./build/linux-x64-debug/Client/Main              # corre hasta que se cierre la ventana
./build/linux-x64-debug/Client/Main --frames 60  # dibuja 60 frames y sale
```

En Windows: `cmake --preset windows-x64`, `cmake --build --preset windows-x64-debug` y
`build\windows-x64\Client\Debug\Main.exe`.

## Tests

```bash
ctest --preset linux-x64-debug     # en Windows: ctest --preset windows-x64-debug
```

Los tests golden leen archivos reales del cliente original, que MuRet no versiona. Se les pasa una
carpeta con el contenido de `Data\` (`Enc1.dat`, `Player\`, `World1\`…) en
`MURET_CLIENT_GOLDEN_DIR`, al correr o al configurar:

```bash
MURET_CLIENT_GOLDEN_DIR=/ruta/a/Data ctest --preset linux-x64-debug
cmake --preset linux-x64-debug -DMURET_CLIENT_GOLDEN_DIR=/ruta/a/Data   # queda en la caché
```

Sin la variable, se saltean (`Skipped`). Si apunta a una carpeta que no existe, o le falta el
archivo que el test pide, fallan.

Algunos además comparan con la memoria del `main.exe` original, con los archivos ya cargados: los
volcados de `client-research/mu-golden/memoria-cliente`, en el repo de research (fuera de Git).
La variable apunta a esa carpeta, la que tiene una carpeta por captura (`2026-10-09-devias`,
`2026-10-09-lorencia` y `2026-10-09-lorencia-obj`). Necesitan las dos carpetas, con la misma regla,
y la de los volcados va solo en el entorno:

```bash
MURET_CLIENT_GOLDEN_DIR=/ruta/a/Data MURET_CLIENT_MEMORY_DIR=/ruta/a/memoria-cliente ctest --preset linux-x64-debug
```

## Estructura (a medida que avanzan las olas)

```
Client/
  CMakeLists.txt
  src/
    main.cpp      arma las capas y arranca; sin lógica
    App/          bucle de juego con tick fijo          (CL3)
    Core/         config, log, reloj                    (CL0+)
    Platform/     ventana, contexto GL, input (SDL3)    (CL3)
    Data/         tablas .bmd, Text.bmd, terreno, OZJ/OZT, modelos: bytes a tipos, sin GL (CL1, CL6)
    Network/      socket, paquetes, C3/C4, FoxAnti      (CL2, CL4)
    Protocol/     opcodes, structs, dispatcher          (CL2, CL4, CL9)
    Render/       OpenGL 3.3: texturas, sprites, texto  (CL5+)
    Scenes/       login, selección, juego               (CL5, CL6)
    World/        mapa, cámara, objetos                 (CL7)
    GameLogic/    personaje, entidades, items, chat     (CL7+)
    UI/           widgets, ventanas, HUD                (CL5, CL8)
    Audio/        música y efectos                      (CL10)
  tests/          GoogleTest                            (CL0.2+)
  docs/research/  qué hace el original, byte a byte (01..09)
```
