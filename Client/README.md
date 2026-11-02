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

Ola CL1: `Main` abre una ventana SDL3 de 640×480 con OpenGL 3.3 core, la limpia de un color e
imprime `GL_VERSION` y `GL_RENDERER`; todavía no se conecta. `ClientLib` resuelve rutas de `Data\`
como Windows (sin distinguir mayúsculas) y lee desde sus bytes `Text.bmd`, `Gate.bmd`, `Item.bmd`
y `Skill.bmd`, con el XOR3 y el checksum de las tablas `.bmd`, y el terreno de un mundo:
`EncTerrain<N>.map` (las capas de tiles), `EncTerrain<N>.att` (los atributos) y `EncTerrain<N>.obj`
(los objetos), con su cifrado K16, `TerrainHeight.OZB` (las alturas) y `TerrainLight.OZJ` (la luz).
De las texturas, el OZT (un TGA de 32 bits, en RGBA) y el OZJ (un JPEG, en RGB, con libjpeg-turbo),
en un buffer de lados potencia de 2 como el del original. Con eso termina el código de la CL1:
`message.wtf` no entra, porque es del launcher y `main.exe` no lo lee. La próxima es la CL2, el
protocolo sin red, con su [plan](../docs/superpowers/plans/2026-11-02-ola-cl2-protocolo-sin-red.md).

Desde la CL2.5, `Protocol/Connection` lee y escribe los paquetes del login, campo por campo y sin
red: pedir la lista de servidores (`F4 02`), leerla (`F4 06`), elegir uno (`F4 03`) y leer su
dirección, el saludo del GameServer (`F1 00`), el login (`F1 01`) y su respuesta, el aviso del
checksum malo (`F1 03`) y el keepalive (`0E`). Lo que sale es un `Outgoing`: el paquete, con el
XOR32 si lo lleva, y si va en claro o como C3. La versión y el serial del build están en
`Protocol/BuildConstants.h`, y el XOR3 pasó a `Core`, porque lo usan las tablas y el login.

Desde la CL3.1, la ventana y el contexto GL salieron de `main.cpp` a `Platform/`: `SdlContext`
(el `SDL_Init` y el `SDL_Quit`), `Window` (la ventana con su contexto 3.3 core, la tabla de GL de
`GlLoader` y el VSync de `SwapInterval`) y `PumpEvents`, que pasa los eventos de SDL a
`App::WindowState`. `Platform/` es el target `ClientPlatform`, el único que enlaza SDL3: `ClientLib`
no la ve, y sus tests corren sin ventana. `main.cpp` queda en el armado, con el bucle del hola
mundo de la CL0.3 hasta la CL3.2.

## Compilar y correr

Hace falta vcpkg (las dependencias, SDL3, libjpeg-turbo y GoogleTest, están en `vcpkg.json` y se
bajan solas al configurar) y la variable `VCPKG_ROOT` apuntando a su clon; ver el
[README raíz](../README.md#compilar).
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

Cada `tests/<módulo>/test_<algo>.cpp` es su propio ejecutable (`test_packet_reader`), como en
MuMain, y cada `TEST` es un test de CTest con el nombre de su suite: `ctest -R PacketReader` corre
los del lector. Lo que comparten está en `tests/support/` (la biblioteca `ClientTestSupport`).

Los tests golden leen archivos reales del cliente original, que MuRet no versiona. Se les pasa una
carpeta con el contenido de `Data\` (`Enc1.dat`, `Player\`, `World1\`…) en
`MURET_CLIENT_GOLDEN_DIR`, al correr o al configurar:

```bash
MURET_CLIENT_GOLDEN_DIR=/ruta/a/Data ctest --preset linux-x64-debug
cmake --preset linux-x64-debug -DMURET_CLIENT_GOLDEN_DIR=/ruta/a/Data   # queda en la caché
```

Sin la variable, se saltean (`Skipped`). Si apunta a una carpeta que no existe, o le falta el
archivo que el test pide, fallan.

Los OZJ, además, se comparan con lo que da la IJG libjpeg 6b del original, que no está en CI: el
tamaño y un hash de cada uno están en `tests/vectors/jpeg/lab-6b.txt`, y cómo se sacan, en el
README de esa carpeta. Es lo que hay que correr al cambiar de versión de libjpeg-turbo.

Algunos además comparan con la memoria del `main.exe` original, con los archivos ya cargados: los
volcados de `client-research/mu-golden/memoria-cliente`, en el repo de research (fuera de Git).
La variable apunta a esa carpeta, la que tiene una carpeta por captura (`2026-10-09-devias`,
`2026-10-09-lorencia` y `2026-10-09-lorencia-obj`). Necesitan las dos carpetas, con la misma regla,
y la de los volcados va solo en el entorno:

```bash
MURET_CLIENT_GOLDEN_DIR=/ruta/a/Data MURET_CLIENT_MEMORY_DIR=/ruta/a/memoria-cliente ctest --preset linux-x64-debug
```

Los de la red leen el cable del laboratorio: el `muproxy.log` que su proxy escribió entre el
cliente original y los servidores, en `analysis/mainexe/evidence/` de `mu-online-reverse-lab`.
Lleva el login del jugador, así que queda fuera de Git, y los tests cuentan paquetes y no imprimen
un byte del cable, ni al fallar. `MURET_CLIENT_WIRE_DIR` apunta a la carpeta que lo tiene, con la
misma regla, solo en el entorno, y no necesita las otras dos.

Desde la CL2.3, la carpeta del cable también tiene las claves del servidor con el que se capturó,
`Dec1.dat` y `Enc2.dat`, de `gameserver/evidence/keyfiles/` en `mu-server-repack-research-102i`:
con ellas el servidor descifra lo que manda el cliente y cifra lo que le contesta. Como `evidence/`
no las tiene, la carpeta se arma aparte, con enlaces a los tres archivos:

```bash
mkdir -p /ruta/a/cable
ln -s /ruta/a/mu-online-reverse-lab/analysis/mainexe/evidence/muproxy.log /ruta/a/cable/
ln -s /ruta/a/mu-server-repack-research-102i/gameserver/evidence/keyfiles/Dec1.dat /ruta/a/cable/
ln -s /ruta/a/mu-server-repack-research-102i/gameserver/evidence/keyfiles/Enc2.dat /ruta/a/cable/
MURET_CLIENT_WIRE_DIR=/ruta/a/cable ctest --preset linux-x64-debug
```

Los tests que comparan estas claves con las del cliente, `Enc1.dat` y `Dec2.dat`, necesitan
también la carpeta golden, y no muestran un valor de una clave, ni al fallar.

Los del login (`GoldenLogin`, desde la CL2.5) usan solo la carpeta del cable, con `Dec1.dat`:
eligen del cable los login, los keepalive, los saludos y las respuestas, y miran cabeceras,
tamaños, opcodes y la versión. De la cuenta, la clave y el serial no miran nada.

## Estructura (a medida que avanzan las olas)

```
Client/
  CMakeLists.txt
  src/
    main.cpp      arma las capas y arranca; sin lógica
    App/          línea de comandos, estado de la ventana, bucle con tick fijo (CL3)
    Core/         Expected, ByteReader, Xor3; config, log, reloj (CL1+)
    Platform/     ventana, contexto GL, input: lo único que ve SDL3 (ClientPlatform, CL3)
    Data/         tablas .bmd, Text.bmd, terreno, OZJ/OZT, modelos: bytes a tipos, sin GL (CL1, CL6)
    Network/      socket, paquetes, C3/C4, FoxAnti      (CL2, CL4)
    Protocol/     opcodes, structs (el login, en Connection/), dispatcher (CL2, CL4, CL9)
    Render/       OpenGL 3.3: texturas, sprites, texto  (CL5+)
    Scenes/       login, selección, juego               (CL5, CL6)
    World/        mapa, cámara, objetos                 (CL7)
    GameLogic/    personaje, entidades, items, chat     (CL7+)
    UI/           widgets, ventanas, HUD                (CL5, CL8)
    Audio/        música y efectos                      (CL10)
  tests/          GoogleTest                            (CL0.2+)
  docs/research/  qué hace el original, byte a byte (01..09)
```
