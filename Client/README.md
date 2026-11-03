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
no la ve, y sus tests corren sin ventana. `main.cpp` queda en el armado, por fases, como los
pasos de `WinMain` en MuMain: la config antes de `SDL_Init`, el log, la ventana y el bucle. La
escena de prueba (`Scenes/TestScene`) y el medidor de `--stats` (`App/FrameRateMeter`) tienen su
archivo y sus tests.

Desde la CL4.5, con un host, `Main` se conecta: la `ServerSession` va del ConnectServer al login
aceptado en el GameServer y después manda el keepalive cada 20 s, consultada por el bucle en cada
frame. Todavía no hay escena de login (CL5): la cuenta sale del entorno, se elige el primer
servidor de la lista, y la ventana sigue con la escena de prueba.

Desde la CL5.1, lo que dibuja con OpenGL es el target `ClientRender` (`src/Render/Gl/`), el único
con GL además de `ClientPlatform`: `ClientLib` sigue sin verlo. Trae el shader del 2D, `GlTexture`
(sube una textura de la CL1 con su filtro y su wrap), un framebuffer para leer lo dibujado y
`GlRenderer`, que dibuja triángulos en píxeles con la y para arriba. La tabla de GL es la X-macro de
`Platform/GlFunctionList.h`: una función nueva es una línea ahí.

Las texturas de una escena son un juego, un dato (`Scenes/TextureSets.h`, el del login y el de la
carga): `Data::Textures::LoadTextureSet` las busca en `Data\` como el original (el nombre hasta el
primer punto y `.OZJ` u `.OZT`) y las lee, y `Gl::TextureTable` las sube a la tabla de la escena,
por el slot del original.

Desde la CL5.3, el texto (`Render/Text/`): las cuatro fuentes de `WinMain` con su alto por el
ancho de la pantalla (`FontSet`), los cambios de color `0x02` del chat (`InlineColors`), dónde va un
texto y cómo se corta en cajas de 256 columnas (`TextLayout`), y `FreeTypeRasterizer`, que mide y
dibuja con FreeType en un bit, detrás de `IGlyphRasterizer`. La fuente sale de `[Video] FontFile`.

## Compilar y correr

Hace falta vcpkg (las dependencias, SDL3, libjpeg-turbo, glm, FreeType y GoogleTest, están en
`vcpkg.json` y se bajan solas al configurar) y la variable `VCPKG_ROOT` apuntando a su clon; ver
el [README raíz](../README.md#compilar).
Desde la raíz del repo:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
./build/linux-x64-debug/Client/Main              # corre hasta que se cierre la ventana
./build/linux-x64-debug/Client/Main --frames 60  # dibuja 60 frames y sale
./build/linux-x64-debug/Client/Main --vsync off --stats  # sin VSync: FPS y pasos por segundo cada 5 s
./build/linux-x64-debug/Client/Main --vsync off --max-fps 144 --stats  # sin VSync, hasta 144 FPS
```

Al salir, `Main` imprime los frames y los pasos que corrió. La lógica va a 25 pasos por segundo con
cualquier tasa de frames (CL3.2). En Linux con Xvfb (sin retrazo, así que VSync no frena), `--stats`
da cientos de FPS y 25 pasos por segundo; con `--max-fps N`, hasta N FPS. Sin VSync y sin tope, el
bucle no duerme y usa un núcleo entero: `--max-fps` (o `[Video] MaxFps`) le pone un tope, como el
`FpsCap` de MuMain, que tampoco topea por defecto y también va solo con VSync apagado. Con VSync
el tope no se usa (el log lo dice): el swap ya frena al refresco, y un tope que no lo divide
dibujaría a saltos. Con `--pacing faithful` (el ritmo del original, VSync apagado), da 21,33 FPS y
25 pasos por segundo, y no lleva tope. En Windows queda por probar a mano (Emanuel).

En Windows: `cmake --preset windows-x64`, `cmake --build --preset windows-x64-debug` y
`build\windows-x64\Client\Debug\Main.exe`.

## Correr y configurar `Main`

Lo que depende del lugar (el servidor, la carpeta de datos, el video) sale de la línea de comandos
o de `muret-client.ini`: la línea de comandos pisa la config, y la config, los valores por
defecto. Todo se valida antes de abrir la ventana: un error termina `Main` con un mensaje que
empieza con `[error]` y nombra la opción, la clave o la ruta, sin abrir nada. Sin argumentos ni
config, `Main` corre con los valores por defecto, como en CI.

| Opción | Qué hace |
| --- | --- |
| `connect /u<host> /p<puerto>` | el servidor, como lo pasa el launcher. `connect` es opcional, las letras van en mayúscula o minúscula y en cualquier orden. El valor va pegado a la letra. El puerto va de 1 a 65535, y el host, de 1 a 255 bytes de ASCII visible: una IP o un nombre, que con acentos va en punycode (Q-C37). Uno sin el otro, o uno repetido, es un error (Q-C36), y `/y` y `/z`, también |
| `--config <ruta>` | lee esa config en vez de `muret-client.ini`; si no existe, es un error que la nombra |
| `--data <ruta>` | la carpeta de datos del cliente original, en vez de `[Data] Path`; si no existe o no es una carpeta, es un error que la nombra (Q-C35) |
| `--max-fps <N>` | el tope de FPS del modo normal con VSync apagado, en vez de `[Video] MaxFps`: 0 (sin tope), o de 25 (un frame por paso de la lógica) a 1000. Después de dibujar, `Main` duerme hasta la meta del frame: 1 s / N después de la del anterior, o de donde terminó el anterior si se pasó. Así lo que corre entre frames no se suma, y el tope nunca da menos FPS que no tenerlo. Con VSync no se usa. Con `--pacing faithful`, un tope que no es 0 es un error que nombra `--pacing faithful` y `--max-fps`; `--max-fps 0` vale, y saca un `MaxFps` de la config |

Las rutas de `--config` y `--data` se toman desde la carpeta actual, como cualquier argumento.
En Windows llegan en UTF-8, como las de la config, así que andan aunque tengan letras que no están
en la página ANSI; en Linux van tal cual las da el sistema, también un nombre que no es UTF-8.
Cada opción va una vez: una repetida es un error. Dos límites de Windows:

- Una `\` justo antes de una comilla cuenta como una comilla de verdad, y la parte entre comillas
  sigue abierta: `--data "C:\MU\Data\" --frames 60` llega como un solo argumento,
  `C:\MU\Data" --frames 60`, y las opciones que siguen se pierden. Va `"C:\MU\Data\\"` (dos `\`)
  o `"C:\MU\Data"`, sin la última.
- Un nombre con una mitad de UTF-16 suelta (NTFS lo deja, pero no es texto) llega con esa mitad
  cambiada por U+FFFD, porque SDL la cambia al pasar a UTF-8: el error dice que la ruta no existe.

Por ejemplo:

```bash
./build/linux-x64-debug/Client/Main connect /u127.0.0.1 /p44405 --data /ruta/a/Data
./build/linux-x64-debug/Client/Main --config ~/mu/otra.ini
```

Sin host no es un error: `Main` no mira el entorno ni las claves, y corre sin un servidor, como en
CI.

### Con un servidor: la cuenta y las claves

Con un host, de `/u` o de `[Server] Host`, `Main` necesita dos cosas más antes de abrir la ventana,
en este orden:

1. La cuenta y la clave del login, en `MURET_CLIENT_ACCOUNT` y `MURET_CLIENT_PASSWORD`, hasta que
   la escena de login las pida (CL5.5). No van en la config, que es un archivo, ni en la línea de
   comandos, que queda en el historial y en `ps`. Cada una va de 1 a 10 bytes, el campo del
   login: si falta, está vacía o pasa de 10, el error nombra la variable y nunca trae su valor.
   Se cargan sin que se vean en la pantalla:

   ```bash
   IFS= read -rs -p "cuenta: " MURET_CLIENT_ACCOUNT; echo
   IFS= read -rs -p "clave: " MURET_CLIENT_PASSWORD; echo
   export MURET_CLIENT_ACCOUNT MURET_CLIENT_PASSWORD
   ```

   ```powershell
   # -MaskInput es de PowerShell 7.1 o más nuevo.
   $env:MURET_CLIENT_ACCOUNT = Read-Host "cuenta" -MaskInput
   $env:MURET_CLIENT_PASSWORD = Read-Host "clave" -MaskInput
   ```

   Con `IFS=`, `read` no les saca los espacios del principio ni del final. Al terminar, la clave
   se borra del entorno de la consola: `unset MURET_CLIENT_PASSWORD` en bash, o
   `Remove-Item Env:MURET_CLIENT_PASSWORD` en PowerShell.

2. Las claves de la conexión con el GameServer, `Enc1.dat` y `Dec2.dat`, de la carpeta de datos
   (`--data` o `[Data] Path`), sin mirar mayúsculas. Sin carpeta, o si alguna no está o no carga,
   el error nombra las dos y dice qué pasó con cada una. No se versionan (D-C7).

Con las dos, `Main` arranca la red del sistema y la sesión, y el log dice cada paso: "se conecta
al ConnectServer", "conectó con el ConnectServer", la lista, la dirección del GameServer (sin
decirla), "el GameServer saludó: se manda el login", "el GameServer aceptó el login" y, desde
ahí, "se mandó el keepalive al GameServer" cada 20 s. Ni la cuenta ni la clave ni las direcciones
van al log. Si algo falla, el log dice qué (un servidor que rechaza, otra versión, un login
rechazado, una conexión que se pierde, o 30 s sin respuesta) y la ventana sigue abierta: la sesión
no se reconecta sola.

### Contra el servidor del laboratorio (a mano)

CI no tiene la VM del laboratorio, así que el login de punta a punta se prueba a mano (el paso 4
de la CL4.5, en el
[plan de la CL4](../docs/superpowers/plans/2026-11-02-ola-cl4-red-del-cliente.md)):

1. Levantar la VM del laboratorio con el ConnectServer y el GameServer andando.
2. Cargar la cuenta y la clave como dice la sección de arriba, con una cuenta del laboratorio.
3. Correr, con la IP del ConnectServer de la VM y la carpeta `Data` del cliente del laboratorio,
   que trae `Enc1.dat` y `Dec2.dat`:

   ```bash
   ./build/linux-x64-debug/Client/Main connect /u<ip del CS> /p44405 --data <Data del laboratorio>
   ```

4. Mirar el log durante dos minutos: tiene que llegar a "el GameServer aceptó el login" y, desde
   ahí, mostrar seis o siete "se mandó el keepalive al GameServer", uno cada 20 s, sin "se perdió
   la conexión con el GameServer". Con `[Server] Obfuscation = foxanti`, que es el valor por
   defecto: el GameServer del laboratorio usa FoxAnti.
5. Cerrar la ventana y borrar la clave del entorno (`unset MURET_CLIENT_PASSWORD`).

Si se captura el cable para comparar el login y el keepalive con los de la sesión real del 04
(P-C05), la captura queda fuera de Git, porque se descifra con las claves reales, y la comparación
se mira sin imprimir la cuenta. Acá se anota solo el resultado: todavía no se corrió.

### `muret-client.ini`

`Main` lo busca junto al ejecutable (la carpeta que da `SDL_GetBasePath`), no en la carpeta
actual. Si no está, usa los valores por defecto; con `--config`, el archivo tiene que existir. Una
carpeta en su lugar, un archivo de más de 64 KiB o uno que no se puede leer son errores.
Todas las claves son opcionales:

```ini
[Server]
Host = 127.0.0.1         ; de 1 a 255 bytes, sin espacios
Port = 44405             ; de 1 a 65535
Obfuscation = foxanti    ; foxanti o none

[Data]
Path = C:\MU\Data        ; la carpeta de datos del cliente original

[Video]
Width = 640
Height = 480
VSync = on               ; on, adaptive u off
Pacing = decoupled       ; decoupled o faithful
MaxFps = 0               ; tope de FPS con VSync off: 0 (sin tope), o de 25 a 1000
FontFile = letra.ttf     ; la fuente TrueType del texto
BoldFontFile = negrita.ttf ; su negrita (sin darla, se ensancha la de FontFile)

[Log]
Path = muret-client.log
```

| Clave | Por defecto | Qué es |
| --- | --- | --- |
| `[Server] Host` | ninguno | el host del ConnectServer; lo pide la red (CL4) |
| `[Server] Port` | 44405 | el puerto del ConnectServer, el compilado en el original (`0x6d5dc4`) |
| `[Server] Obfuscation` | `foxanti` | la forma de los paquetes que espera el servidor: `foxanti`, la capa de FoxAnti del servidor del laboratorio, o `none`, un servidor que habla como `main.exe`. `/u /p` no la cambia |
| `[Data] Path` | ninguna | la carpeta `Data\` de una instalación del cliente original |
| `[Video] Width`, `Height` | 640, 480 | el tamaño de la ventana al abrir, de 1 a 16384 |
| `[Video] VSync` | `on`, y `off` en el modo fiel | como `--vsync`. Con `Pacing = faithful` (o `--pacing faithful`), `on` o `adaptive` es un error que nombra `Pacing` y `VSync` |
| `[Video] Pacing` | `decoupled` | como `--pacing` |
| `[Video] MaxFps` | 0 (sin tope) | como `--max-fps`. Con `Pacing = faithful`, un tope que no es 0 es un error que nombra `Pacing` y `MaxFps` |
| `[Video] FontFile` | ninguna | la fuente TrueType (o una colección TTC: su primera cara) de todo el texto. El original pide la suya por nombre a Windows; MuRet necesita un archivo. Las cuatro fuentes del original salen de esta, cada una con su alto (`Render/Text/FontSet.h`) |
| `[Video] BoldFontFile` | ninguna | la de la negrita. Sin darla, la negrita es la de `FontFile` ensanchada un píxel, como la que simula Windows |
| `[Log] Path` | `muret-client.log` junto al ejecutable | el archivo del log |

Una ruta relativa de la config (`[Data] Path`, `[Log] Path` y las dos fuentes) se toma desde la
carpeta del ejecutable, no desde la actual; que una fuente exista lo mira FreeType al abrirla. Un
par de comillas dobles alrededor de una ruta o del host se saca (lo que da "Copiar como ruta" del
Explorador). El archivo es UTF-8, con o sin BOM, y con fin de línea `\n` o `\r\n`.

Las secciones, las claves y los valores de `Obfuscation`, `VSync` y `Pacing` no distinguen
mayúsculas. Un `;` al principio de la línea, o después de un espacio, empieza un comentario; `#`
no. Son errores, y el mensaje dice el archivo y la línea (`muret-client.ini, línea 3: ...`):

- una línea que no es `[Sección]` ni `Clave = valor`, o con texto después del `]`;
- una sección o una clave que no existen, una clave antes de la primera sección, repetida o sin
  valor;
- un valor que no se puede leer (un puerto de 0, un `VSync = sometimes`, un `Width = 480px`);
- una ruta que no es UTF-8 o que tiene caracteres de control, un host que no es ASCII visible, y
  un `\r` sin `\n`.

### El log

`Main` escribe el log en `stderr` y en el archivo de `[Log] Path`, por defecto `muret-client.log`
junto al ejecutable (la carpeta de `SDL_GetBasePath`). Los errores de la línea de comandos y de la
config, que llegan antes de saber qué archivo es, van solo a `stderr`. Cada línea empieza con su
nivel: `[debug]`, `[info]`, `[warning]` o `[error]`; el mínimo es uno solo, `info`, para las dos
salidas. Los mensajes de SDL entran al mismo log, con `SDL: ` adelante y su prioridad pasada a uno
de esos cuatro. Si el archivo no se puede abrir, el log va solo a `stderr`, con un aviso que nombra
la ruta. El archivo no se empieza de nuevo: cada corrida agrega al final, desde una línea `[info]
MuRet Client arranca`, como el `MuError.log` del original, así el log de una caída sigue ahí al
volver a abrir, y dos clientes abiertos a la vez escriben los dos sin pisarse. Para que no crezca
sin fin, al abrir se vacía si ya pasa de 1 MiB (el original corta el principio, Q-C48).

Lo que conviene buscar ahí: el aviso de un frame de más de 1 s, que cuenta 1 s (un breakpoint, o
una ventana arrastrada en Windows, Q-C44), y que empieza con `[warning] un frame pasó de 1000 ms`;
y los errores del arranque.

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

Los de `tests/render_gl/` (desde la CL5.1) abren una ventana escondida con un contexto 3.3 core
(`tests/support/GlTestContext.h`), dibujan en un framebuffer de 64 × 64 y leen los píxeles. Llevan
la etiqueta `gl`. Sin pantalla o sin OpenGL 3.3 se saltean, y con `MURET_CLIENT_REQUIRE_GL=1`
fallan con el motivo: así los corre el CI, en Xvfb, junto con los del lab de la CL5.0:

```bash
MURET_CLIENT_REQUIRE_GL=1 xvfb-run -a ctest --preset linux-x64-debug -L '^gl$' --no-tests=error
```

Los de FreeType (`FreeTypeRasterizerTest`, desde la CL5.3) necesitan una fuente TrueType, que
MuRet no versiona: `MURET_CLIENT_FONT_FILE` apunta al archivo, con la regla de las carpetas (sin
la variable se saltean; si no es un archivo, o FreeType no lo abre, fallan). No comparan con GDI:
miden y dibujan con esa fuente y miran que no haya suavizado, el alto de la celda y los avances.
En CI es una fuente del runner:

```bash
export MURET_CLIENT_FONT_FILE=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
ctest --preset linux-x64-debug -R FreeType
```

## Estructura (a medida que avanzan las olas)

```
Client/
  CMakeLists.txt
  src/
    main.cpp      arma las capas y arranca; sin lógica
    App/          línea de comandos, estado de la ventana, bucle con tick fijo (CL3); la sesión
                  con los servidores (CL4)
    Core/         Expected, ByteReader, Xor3; config, log, reloj (CL1+)
    Platform/     ventana, contexto GL, input: lo único que ve SDL3 (ClientPlatform, CL3)
    Data/         tablas .bmd, Text.bmd, terreno, OZJ/OZT, modelos: bytes a tipos, sin GL (CL1, CL6)
    Network/      socket, paquetes, C3/C4, FoxAnti      (CL2, CL4)
    Protocol/     opcodes, structs (el login, en Connection/), dispatcher (CL2, CL4, CL9); el
                  pedido de la mascota (A9), en Pet/ (CL4)
    Render/       OpenGL 3.3: texturas, sprites, texto  (CL5+); lo que llama a GL, en Gl/
                  (ClientRender, CL5.1)
    Scenes/       la escena de prueba (CL3); login, selección, juego (CL5, CL6)
    World/        mapa, cámara, objetos                 (CL7)
    GameLogic/    personaje, entidades, items, chat     (CL7+)
    UI/           widgets, ventanas, HUD                (CL5, CL8)
    Audio/        música y efectos                      (CL10)
  tests/          GoogleTest                            (CL0.2+)
  docs/research/  qué hace el original, byte a byte (01..09)
```

## Créditos

El texto se dibuja con [FreeType](https://freetype.org), que se usa bajo la FreeType License (FTL),
y la FTL pide decirlo así en la documentación:

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All
> rights reserved.
