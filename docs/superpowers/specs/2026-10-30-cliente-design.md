# Cliente en MuRet: roadmap de la reimplementación

> **Documento de trabajo, 2026-10-30.** Es el plan para reescribir el cliente de MU Online 1.02
> (`main.exe`) en MuRet: el objetivo, las decisiones de diseño, la estructura de `Client/` y las
> olas CL0 a CL11 con sus sub-olas. Queda vivo mientras haya olas abiertas.
>
> **Qué hace el original** no está acá: está en el research del cliente, un archivo por tema, en
> [`Client/docs/research/`](../../../Client/docs/research/README.md). Este roadmap decide qué hace
> MuRet y en qué orden; el research es la fuente de verdad del comportamiento.
>
> **Cómo se aprende** (sub-olas N.0 de fundamentos, formato de cada sesión, tutor, carril R) es lo
> mismo que en el servidor: el
> [camino de aprendizaje](2026-10-28-camino-de-aprendizaje-design.md). Este documento solo agrega
> lo propio del cliente.
>
> Escrito a partir de [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i)
> (carpeta `client-research/`) y de [EmaNLJN/mu-online-reverse-lab](https://github.com/EmaNLJN/mu-online-reverse-lab)
> (carpeta `analysis/decompilacion-maxima/`: DLLs de audio y datos, y el `Data\` del cliente), con [sven-n/MuMain](https://github.com/sven-n/MuMain) como
> referencia de cómo se organiza un cliente de MU moderno. Autor: Emanuel (ema-ljn), con Claude de
> tutor.

## Objetivo

Un cliente propio que **juegue contra un servidor de MU 1.02** como lo haría el `main.exe`
original, pero moderno:

- **Compatible por contrato:** los mismos bytes en la red (C1/C2/C3/C4, XOR32, los opcodes del
  build `c32bbd5f…`) y la misma lectura de los archivos `Data\` del cliente original. No se copia
  el layout interno ni la estructura de clases.
- **Moderno:** C++20, Windows y Linux, render con OpenGL 3.3 y shaders, capas separadas, RAII y
  tests. Nada de C99 ni de copiar la reimplementación de `mu-client`: esa es un oráculo.
- **Más fluido:** desacoplar la lógica del dibujo para correr a los FPS que dé la máquina (VSync,
  sin tope o con un tope opcional) sin cambiar la velocidad del juego (D-C4).

**Modernizar nunca sacrifica fidelidad.** Todo lo que el original hace observable se mantiene al
byte o al tick: lo que se manda y se entiende por la red, cómo se leen los archivos de `Data\`, la
velocidad de la lógica (movimiento, animaciones, cooldowns, keepalive) y lo que ve el jugador. Lo
moderno es la implementación (C++20, OpenGL 3.3, capas, tests) y lo que el original no fija: cuántos
frames se dibujan entre dos ticks, la resolución, el suavizado. Si una mejora cambiara algo
observable, va detrás de una opción y con un ADR, y por defecto queda lo que hace el original.
Cuántos frames se dibujan no es parte de eso: por eso el ritmo por defecto es el desacoplado de
D-C4, con la lógica a 25 pasos por segundo, como el original. En los frames largos se aparta: mide
el frame entero y lo recorta a 1 s, y el original no tiene tope (Q-C44 del
[07](../../../Client/docs/research/07-quirks-y-bugs.md)). El modo fiel (`Pacing = faithful`, los
21,33 FPS del original, sin ese tope) es la opción para comparar lado a lado.

Es un **proyecto de aprendizaje**, igual que el servidor: Emanuel escribe la lógica del cliente
sub-ola por sub-ola y Claude hace de tutor. Claude escribe el andamiaje (CMake, dependencias, CI,
plantillas de docs) cuando se lo piden y los pasos marcados `(Claude)` en los planes.

> Por ahora el modo tutor está apagado (Emanuel, 2026-11-10): los agentes escriben el código del cliente,
> sin el consentimiento del tutor. Ver [`AGENTS.md`](../../../AGENTS.md).

**Qué NO es el objetivo:** un cliente de Season 6 (eso es MuMain), ni portar el anticheat, ni
distribuir un cliente para jugar. La `Data\` del original está en `Client/assets/Data/`, tal cual,
porque es el original que MuRet reimagina y la base de sus tests (decidido por Emanuel el
2026-11-04); el cliente igual la lee de la carpeta que se le pase.

## Cómo trabajamos

Igual que el servidor ([roadmap del DataServer](2026-10-25-dataserver-port-design.md#cómo-trabajamos)):
olas por tema, sub-olas de un concepto, un PR chico por sub-ola, GoogleTest en verde, y el ritmo
explicación → mini-spec → Emanuel lo escribe → revisión con preguntas → cierre en `PROGRESS.md`.

Lo propio del cliente:

- **Las olas se llaman CL0 a CL11** para no chocar con las del servidor (0 a 14) ni con los
  paquetes C1/C2.
- **De lo puro a lo visual.** Primero bytes sin ventana (formatos y protocolo, todo testeable con
  arrays), después la ventana, después la red, y recién al final el 3D. Una sub-ola visual se
  "muestra" con una captura además de sus tests.
- **Research antes de lo que no se sabe.** Donde el research tiene huecos (modelos animados,
  terreno, audio; ver [08](../../../Client/docs/research/08-pendientes.md)), la ola arranca con una
  sub-ola CL*n*.R de carril R.
- **Las N.0 se comparten.** Si una ola del cliente necesita un fundamento que ya se vio en el
  servidor (bytes en la 1.0, sockets en la 2.0), se repasa en vez de repetirse. Las N.0 propias
  del cliente (bucle de juego, gráficos) están en la tabla de olas.

## Decisiones de diseño

La regla guía es la del servidor: el **contrato** (wire + archivos de datos) se respeta al byte; la
**implementación** es moderna y portable. Las decisiones grandes tendrán su ADR en `docs/adr/`.

| # | Tema | Original (MSVC 6) | MuRet | Por qué / qué se aprende |
| --- | --- | --- | --- | --- |
| D-C1 | Lenguaje | C++98 sin STL visible, globales | C++20 como el resto de MuRet, sin globales del juego | RAII, valores en vez de estado global, `std::span`, un `Expected` propio (`std::expected` es de C++23) |
| D-C2 | Build | proyecto de VC6 | el mismo CMake con presets del repo; dependencias por vcpkg (SDL3, libjpeg-turbo, glm, FreeType, GoogleTest). El loader de GL es propio: una tabla de punteros pedidos a `SDL_GL_GetProcAddress` (CL0.3; el puerto `glad` de vcpkg trae el perfil de compatibilidad y pide Python). Desde la CL5.1 la tabla es una X-macro (`Platform/GlFunctionList.h`, 46 funciones, una línea cada una), y `LoadGlFunctions` mira la versión y el perfil antes de pedir el resto. Las matrices son de **glm** (header-only, `glm::glm-header-only` en `ClientRender`; `glm::ortho(0, W, 0, H)` es el `gluOrtho2D` del 2D). El texto es **FreeType** (CL5.3), sin SDL3_ttf ni stb_truetype: en `ClientLib`, detrás de `Render::Text::IGlyphRasterizer`, y del puerto sin sus features por defecto (`brotli`, `bzip2`, `png` y `zlib`, que una TTF no usa) | un solo build para los dos sistemas |
| D-C3 | Ventana, input y gráficos | Win32 + `wgl` + OpenGL 1.x de función fija | **SDL3** para ventana, input y audio, y **OpenGL 3.3 core** con shaders propios detrás de `Render::` (decidido por Emanuel el 2026-10-30) | el pipeline gráfico moderno (buffers, shaders, matrices) sin la complejidad de Vulkan; mismo stack de ventana que MuMain |
| D-C4 | Ritmo del frame | pasos de lógica de 40 ms (25 por segundo) y una espera activa de 40 ms medida con `GetTickCount`, que con ticks de 15,625 ms termina a los 46,875 ms: **21,33 FPS**, medido en la selección y en Lorencia, y desde el segundo frame 75 pasos de lógica cada 64 frames, 11 de ellos con 2 (7 u 8 cada 6), *análisis* de la aritmética del bucle; el acumulador no tiene tope y no mide los pasos, y sin foco no corre el frame ([02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md), CL3.R) | **tick de lógica de 40 ms** con el mismo bucle de alcanzar que el original + **render desacoplado** con interpolación, VSync por defecto; sin VSync, sin tope o con un tope opcional (`[Video] MaxFps`, como el `FpsCap` de MuMain, que tampoco topea por defecto); como el [ADR 0001 de MuMain](https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md). Un **modo fiel** en la config reproduce la espera del original con un reloj de 15,625 ms (46,875 ms por frame), para comparar lado a lado | el bucle de juego con paso fijo; más FPS sin que el juego vaya más rápido ni cambie lo que se manda al servidor |
| D-C5 | Red | `WSAAsyncSelect` en el hilo de la ventana | un socket TCP no bloqueante que el bucle consulta en cada frame; un solo hilo | E/S no bloqueante; por qué un cliente no necesita IOCP |
| D-C6 | FoxAnti | DLL que parchea el binario | `Network::IObfuscation` opcional, con una implementación identidad y una FoxAnti (solo la capa de paquetes), elegida por config según el servidor | separar el contrato de un servidor concreto del cliente |
| D-C7 | Datos | cargadores que escriben globales; `MessageBox` si falta algo | funciones puras bytes → tipos, errores como valor con un `Expected` propio (decidido por Emanuel el 2026-11-01, L-3 del camino); la `Data\` del original se versiona en `Client/assets/Data/`, tal cual (decidido por Emanuel el 2026-11-04) | testear sin ventana; una sola `Data\` de referencia para todos |
| D-C8 | Quirks | se replican tal cual | contrato de datos se reproduce; bugs internos se corrigen con test viejo/nuevo (regla de [07](../../../Client/docs/research/07-quirks-y-bugs.md)) | distinguir contrato de accidente |
| D-C9 | Tests | — | GoogleTest; los de datos reales leen `Client/assets/Data/`, la base del proyecto, o la carpeta de `MURET_CLIENT_GOLDEN_DIR` si está. Si la carpeta no existe o le falta el archivo pedido, fallan (CL0.2). Hasta el 2026-11-04, sin la variable se salteaban; desde ese día corren siempre, también en CI (decidido por Emanuel: no depender de otras copias). Los volcados de memoria, el cable y las trazas siguen afuera, y sin su variable se saltean | oráculos que corren en cada PR |
| D-C10 | Config | registro de Windows; el ConnectServer viene compilado y el launcher lo puede cambiar por el registro; los argumentos no se leen, y el parser de `/u /p` está sin usar ([02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md#del-launcher-al-juego)) | archivo de config + argumentos; se acepta `connect /u<ip> /p<puerto>`, como se lo lanza en el laboratorio | nada hardcodeado |
| D-C11 | Resolución | 640 × 480 de referencia | lo que el original escala (`0x620d50` y `0x620d80`) se diseña en 640 × 480 y se escala a la ventana real, cada eje por su lado; las ventanas de la UI nueva (`0x65f300`: la lista, la cuenta, los avisos) van en píxeles del framebuffer, centradas como en el original, con sus textos en 640 × 480 (corregido en la CL5.5, con la pregunta 10 del plan de la CL5); resoluciones y pantallas anchas modernas. La ventana no cambia de tamaño, y su resolución es una de las cinco del original (640 × 480, 800 × 600, 1024 × 768, 1280 × 1024 y 1600 × 1200) o un modo real del monitor, el del escritorio o uno de pantalla completa (decidido con Emanuel el 2026-11-03; `App::IsValidResolution`) | separar coordenadas lógicas de físicas |
| D-C12 | Texto | GDI → textura, caché por texto, IME coreano | FreeType en un bit (el hinting del intérprete v35, sin suavizado) → la caja de 256 × 32 del original, con una caché propia (CL5.3); se conserva el byte `0x02` de color. Los textos son de un byte por letra, leídos como Latin-1 (Q-C67) | rasterizado de fuentes, hinting, codificaciones de un byte |
| D-C13 | Audio | `wzAudio.dll` (derivado de CoolPlayer 215: MP3 y Ogg) + DirectSound (WAV) | audio de SDL3 detrás de `Audio::`, con decodificadores de MP3, Ogg y WAV de terceros; la ABI de `wzAudio` es el contrato de comportamiento (volumen, posición, ecualizador) | mezcla de audio, streaming |
| D-C14 | Anticheat | watchdog, hash, `0x2B11` | **no se porta** | fuera de alcance (`AGENTS.md`) |

### Preguntas abiertas (se deciden al arrancar la ola que las necesita)

1. **Biblioteca JPEG (CL1).** ~~¿libjpeg-turbo de vcpkg alcanza para ser byte a byte con la
   libjpeg 6b del original en los tiles de `mu-golden`? Si no, ¿se acepta una diferencia mínima o
   se escribe el decodificador (como hizo `mu-client`)?~~ **Decidido por Emanuel el 2026-11-01:
   libjpeg-turbo**, con la API de la 6b y sus parámetros por defecto. Da los mismos bytes que la 6b
   en los 2.380 JPEG del laboratorio y en los tiles de `mu-golden`. Fuera de ellos difiere: lo que
   se puede llevar a la 6b se lleva (rechaza los JPEG aritméticos, sin pérdida, de 12 bits o sin
   las tablas de Huffman que pide un scan, y agranda el 4:4:0 como ella) y lo demás queda escrito en Q-C33 del
   [07](../../../Client/docs/research/07-quirks-y-bugs.md). Antes había elegido un decodificador
   propio compatible con la 6b: su plan queda en pausa, por si turbo no alcanza
   ([`2026-11-01-cl1.4-decodificador-jpeg.md`](../plans/2026-11-01-cl1.4-decodificador-jpeg.md)).
2. **Código compartido con los servidores (CL2).** El corte C1/C2 lo necesitan el cliente, el
   DataServer (el `FrameParser` de su ola 1) y el ConnectServer (ola 10); C3/C4, el XOR32 y
   FoxAnti, solo el cliente, porque el GameServer queda fuera del roadmap del servidor. ¿Nace un
   `Common/` en la raíz cuando lo usen dos, como dice el roadmap del servidor para
   `Server/Common/`? El [plan de la CL2](../plans/2026-11-02-ola-cl2-protocolo-sin-red.md)
   recomienda decidirlo en la 1.2 del servidor.
3. **Orden entre servidor y cliente.** ~~Recomendación: intercalar, CL1 después de la ola 1 del
   servidor (reusa su 1.0 de bytes) y CL4 después de la ola 2 (reusa sockets).~~ **Decidido por
   Emanuel el 2026-11-01: el cliente va directo**, sin esperar al servidor. La CL0.2 trae ella
   misma vcpkg y GoogleTest; lo que el servidor iba a enseñar antes (bytes, sockets) se aprende
   en la ola del cliente que lo necesita.
4. **Contra qué servidor se prueba (CL4).** Primero el GS del laboratorio (con la capa FoxAnti);
   cuando existan, los servidores de MuRet.

## Diseño de destino

```text
                        ConnectServer (44405)  ·  GameServer (55901)
                                   ▲  TCP, C1/C2/C3/C4
┌──────────────────────────────────┴──────────────────────────────┐
│ Network   Socket no bloqueante · PacketReader/Writer · SimpleModulus · IObfuscation │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ paquete descifrado
┌──────────────────────────────────▼──────────────────────────────┐
│ Protocol  Dispatcher opcode→handler · structs · constantes del build │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ eventos (se movió un jugador, llegó un mensaje)
┌──────────────────────────────────▼──────────────────────────────┐
│ GameLogic  personaje · jugadores y monstruos visibles · items · chat │  tick fijo (D-C4)
└───────────────┬──────────────────────────────────┬──────────────┘
                │ estado (solo lectura)            │ comandos (moverse, atacar)
┌───────────────▼──────────────┐       ┌──────────▼───────────────┐
│ Scenes  Login · Selección · Juego │ ◄── │ Input / UI  ventanas, HUD │
└───────────────┬──────────────┘       └──────────────────────────┘
┌───────────────▼──────────────────────────────────────────────────┐
│ Render   OpenGL 3.3: texturas, sprites, texto, terreno, modelos   │  cada frame, interpolado
└───────────────┬──────────────────────────────────────────────────┘
┌───────────────▼──────────────────────────────────────────────────┐
│ Data     .bmd, terreno, OZJ/OZT, Text.bmd   ·   Platform (SDL3)    │
└───────────────────────────────────────────────────────────────────┘
```

Reglas de capas (como la regla 5 de [CODING_RULES](../../CODING_RULES.md)): **Data** no sabe de
render ni de red; **Network** no sabe del juego; los **handlers** de `Protocol` traducen bytes a
eventos y no tocan render ni sockets; **Render** lee el estado y nunca lo escribe; solo `main.cpp`
y `App` conocen a todos. Los namespaces llevan el componente adelante (`Client::Network`).

## Estructura de `Client/`

La forma copia la de MuMain (`src/source/{App,Core,Data,Network,Render,Scenes,UI,World,GameLogic,Audio}`),
recortada a lo que tiene el 1.02 y con los tests al lado, como `Server/DataServer/`. Cuatro cosas
de MuMain no se copian (revisado contra su `src/source/` el 2026-11-01):

- **Leer un archivo no es dibujarlo.** MuMain lee el OZT en `Render/Textures/OztFile`, descifra los
  modelos en `Render/Models/BmdModulusCipher` y carga el terreno en `Render/Terrain`, junto con
  OpenGL. En MuRet, `Data/` va de bytes a tipos sin GL (se prueba sin ventana) y `Render/` solo
  sube a la GPU lo que `Data/` ya leyó.
- **`Platform/` es una capa, no una carpeta de `Core/`** (MuMain tiene `Core/Platform`). `ClientLib`
  no enlaza SDL3, así que ni `Data` ni `Core` pueden depender de la ventana. `Platform` va en su
  propio target, `ClientPlatform`, y es lo único que ve SDL3: `Input` no conoce SDL, y `Platform`
  le pasa los eventos ya traducidos ([plan de la CL3](../plans/2026-11-02-ola-cl3-plataforma-y-bucle.md)).
- **Sin `Core/Globals` ni `Core/Utilities`** (`_struct.h`, `Singleton`, `_GlobalFunctions`): D-C1.
- **La `Data\` va en `Client/assets/Data/`**, como MuMain versiona `src/bin/Data`, pero la del 1.02
  original y sin tocar: es la referencia que se reimagina (decidido por Emanuel el 2026-11-04).

`Data/` se divide por lo que se lee y no por la extensión: en MU, `.bmd` son las tablas
(`Item`, `Skill`, `Gate`) y también los modelos (`Player.bmd`), que son otro formato. Las tablas
van en `Data/Tables/` y los modelos, cuando lleguen, en `Data/Models/`. Las subcarpetas por
dominio de MuMain (`GameLogic/{Combat,Items,Skills}`, `Render/{Terrain,Textures,Text,Models}`)
sirven de guía cuando lleguen sus olas.

```text
Client/
  CMakeLists.txt           target Main (y desde CL0.2, ClientLib y tests/)
  README.md                qué es, cómo compilar y correr, estado
  docs/research/           QUÉ HACE EL ORIGINAL (01..09)
  src/
    main.cpp               arma las capas y arranca; sin lógica
    App/                   bucle con tick fijo, config, línea de comandos (CL3)
    Core/                  Expected, ByteReader, Xor3, Log, IClock         (CL0+)
    Platform/              ventana, contexto GL, reloj y eventos sobre SDL3; target ClientPlatform (CL3)
    Input/                 teclado y mouse por frame, sin SDL              (CL3)
    Data/                  Tables/, Text/, Terrain/, Textures/, Models/   (CL1, CL6)
    Network/               Socket, Packet, PacketReader/Writer, Crypto/, IObfuscation (CL2, CL4)
    Protocol/              opcodes, structs, Dispatcher, handlers por dominio (CL2, CL4, CL9)
    Render/                Renderer, Shader, Texture, SpriteBatch, Text, Terrain, Models (CL5+)
    Scenes/                Login, CharacterSelect, Game                    (CL5, CL6)
    World/                 Map, Camera, objetos del mapa                   (CL6.4, CL7)
    GameLogic/             personaje, viewport de entidades, items, chat   (CL6, CL7+)
    UI/                    widgets, ventanas, HUD                          (CL5, CL6.5, CL8)
    Audio/                 música y efectos                                (CL10)
  tests/                   data/ network/ protocol/ app/ … (GoogleTest)
```

El ejecutable se llama `Main`, como el `main.exe` original y el target de MuMain.

## Plan por olas

Cada sub-ola es un PR chico. "Terminada cuando" es la condición objetiva de cierre. La columna
"Research" apunta a [`Client/docs/research/`](../../../Client/docs/research/README.md).

| Ola | Tema | Sub-olas (un PR cada una) | Conceptos | Research |
| --- | --- | --- | --- | --- |
| CL0 | Fundaciones del cliente | CL0.1 esqueleto `Client/` + este roadmap + el research (este PR) · CL0.2 `ClientLib` + los tests y la opción `MURET_CLIENT_GOLDEN_DIR` (trae ella misma vcpkg y GoogleTest, sin esperar la 0.2 del servidor) · CL0.3 SDL3 con OpenGL 3.3 core y su loader, CI que compila y testea en Windows y Linux | targets, dependencias, tests salteables | 01 |
| CL1 | Formatos sin ventana | CL1.0 la 1.0 con material del cliente (bytes, endianness, archivos binarios) y el `Expected` propio · CL1.1 XOR3, checksum y `Text.bmd` · CL1.2 `Item`, `Skill` y `Gate.bmd` · CL1.3 terreno: K16, `.map`, `.att`, `.obj`, alturas · CL1.4 texturas OZT y OZJ · ~~CL1.5 `message.wtf`~~ (salió: es del launcher, y `main.exe` no lo lee) | lectura de formatos, errores como valor, tests golden | 05, 07 |
| CL2 | Protocolo sin red | CL2.0 flujos y bloques (la N.0: el corte como máquina de estados, aritmética modular, bits MSB primero) · CL2.1 `PacketReader` C1/C2/C3/C4 · CL2.2 `PacketWriter` + XOR32 · CL2.3 `SimpleModulus` y `Enc1`/`Dec2.dat`, seriales · CL2.4 `IObfuscation` (identidad y FoxAnti) · CL2.5 structs del login: `F4` (lista y elección), `F1` (saludo, login y aviso) y `0x0E` | máquinas de estado, cifrado por bloques, bits MSB primero | 03, 04, 07 |
| CL3 | Plataforma y bucle de juego | CL3.0 el bucle de juego (paso fijo, interpolación, VSync) · CL3.R qué depende del paso de 40 ms (P-C03) · CL3.1 ventana y contexto GL con SDL3 · CL3.2 bucle con tick fijo y render desacoplado, con reloj falso en los tests · CL3.3 input (teclado por frame, mouse) · CL3.4 config + línea de comandos + log | game loop, tiempo, eventos del SO | 02, 08 |
| CL4 | Red del cliente | CL4.0 sockets no bloqueantes · CL4.R la conexión del original (P-C11, Q-C11) · CL4.1 socket no bloqueante portable consultado por el bucle, con la cola de envío · CL4.2 `Dispatcher` y el opcode desconocido · CL4.3 ConnectServer: saludo, lista, IP del GS · CL4.4 GameServer: saludo, versión y login · CL4.5 keepalive y desconexión | E/S no bloqueante, estados de una conexión | 03, 04 |
| CL5 | Render 2D y la escena de login | CL5.0 el pipeline gráfico (buffers, shaders, matrices, texturas) · CL5.R las escenas de carga y de login en el binario (P-C04, P-C13) · CL5.1 `Renderer` y texturas desde OZT/OZJ · CL5.2 `SpriteBatch` en 640 × 480 escalado · CL5.3 texto con caché y colores `0x02` · CL5.4 `SceneManager`, la escena de carga y la `LoginScene` · CL5.5 UI de login conectada a CL4: lista de servidores, cuenta y clave. El mundo 3D detrás del login (el mapa 55, `World56`), el logo y el personaje llegan con la CL6 y la CL7 | GPU, shaders, coordenadas lógicas | 06, 02 |
| CL6 | Modelos y selección de personaje | CL6.0 huesos y cuaterniones (rotaciones, slerp, una jerarquía de huesos y el skinning, a mano contra glm) · CL6.R lectura guiada del render de modelos (P-C01, ya reproducido contra las trazas) y flujo `F3` (P-C06) · CL6.1 parseo de modelos · CL6.2 skinning y animación · CL6.3 `CharacterSelectScene` con preview 3D y los paquetes de selección · CL6.4 el logo y el personaje del login · CL6.5 crear y borrar personajes (`F3 01`, `F3 02`) con la barra de la selección, la ventana de crear y su vista previa · CL6.6 la tela de la clase 4 (la capa del Dark Lord), exacta del binario | jerarquías de huesos, interpolación, cuaterniones | 05, 06, 08 |
| CL7 | Mundo | CL7.R lectura guiada del renderer de terreno (P-C02, ya identificado y verificado), y qué mapas conoce el original y cómo pasa de uno a otro (P-C12), con el barrido del login (abajo) · **arranca por el login**: el mundo del mapa 55, el dragón, la base de los efectos con los del login y el cuervo, con la cámara fija de la escena 2 · CL7.1 terreno con capas, alfa y luz · CL7.2 objetos del `.obj` · CL7.3 cámara · CL7.4 picking y movimiento (`0xD3`/`0xD7`): caminar en Lorencia · CL7.5 los otros mapas: cargar cualquier mundo del `Data\`, cambiar de mapa (gates y warps) y lo propio de cada uno · CL7.6 los efectos del mundo y de los personajes (lo que dejó la CL6, abajo) | mallas, frustum culling, rayos | 05, 06 |
| CL8 | HUD y ventanas | CL8.1 el HUD (barras, números, cursor, teclas, /move y captura) · CL8.2 chat · CL8.3 los paneles, los diálogos del juego y los dos administradores de ventanas · CL8.4 inventario, tooltips y ventana del personaje | UI retenida vs inmediata, el foco del mouse, eventos | 06 |
| CL9 | Gameplay | CL9.R por dominio (P-C09), y los mapas de evento (P-C12) · CL9.1 otros jugadores, monstruos y NPC (viewport), el emote y las misiones · CL9.2 combate, skills, cuervo y mascotas · CL9.3 items del piso, usar y tirar, tiendas de NPC, almacén y caja del caos · CL9.4 party, comercio, duelo, tiendas personales, gremio, amigos y correo · CL9.5 los mapas de evento que conozca el original y Castle Siege: entrada, reglas, paquetes y UI | modelado de dominio, consistencia con el servidor | 04 |
| CL10 | Audio | CL10.R formatos y disparadores (P-C07) · CL10.1 música · CL10.2 efectos | audio digital, mezcla, streaming | 08 |
| CL11 | Efectos visuales completos | CL11.R inventario de familias, casos y consumidores que no cubran ya CL7.6, CL8 o CL9 · CL11.1 joints y estelas · CL11.2 cintas · CL11.3 casos restantes de efectos con modelo, sprites y partículas · CL11.4 efectos visuales de combate, skills y eventos, conectados a sus disparadores de CL9 | ciclos de vida, pools, orden de pases y estado de GL | 06, 08 |

**"Terminada cuando", por ola:** CL0 — `Main` compila y corre en los dos sistemas, en CI. CL1 — cada
formato pasa su round-trip y, con `mu-golden`, su comparación byte a byte. CL2 — pasan los vectores
del cable y los fixtures de `mu-client` para corte, XOR32, C3/C4, FoxAnti, login y keepalive. CL3 —
una ventana a la tasa del monitor con la lógica a tick fijo, verificado con un reloj falso. CL4 — el
cliente pasa el login contra el GS del laboratorio. CL5 — la pantalla de login se ve y se usa. CL6 —
el personaje aparece animado en la selección. CL7 — se camina por Lorencia y, por un gate, por los
otros mapas que trae el `Data\`. CL8 — chat e inventario usables. CL9 — una sesión de juego estable,
también en los mapas de evento. CL10 — música y efectos de audio. CL11 — cada familia y caso visual
en alcance tiene creación, paso, dibujo y limpieza documentados y contrastados con el ASM; sus
consumidores en escenas, personajes y skills están conectados, y se registra por separado lo que
solo comprobó el análisis estático y lo observado en la VM. Una familia o un caso pendiente impide
dar por completa la ola.

### La ola de efectos visuales (CL11)

Agregada por el pedido de Emanuel del 2026-11-10 ([traspaso, tarea 9](../plans/2026-11-10-cl7-traspaso.md#qué-falta-en-orden)).
**Se conserva la numeración de CL0 a CL10.** CL11 identifica el trabajo de efectos que faltaba
nombrar; su número no obliga a esperar al audio. Cada consumidor necesita sus efectos antes de
cerrar su propia ola, en el orden de llamadas del original.

La base está en [Los efectos del login](../../../Client/docs/research/06-render-escenas-y-ui.md#los-efectos-del-login)
y [El dibujo del juego](../../../Client/docs/research/06-render-escenas-y-ui.md#el-dibujo-del-juego):
efectos con modelo (`0x518cf0`, `0x5208a0`, `0x52d620`), joints (`0x530860`, `0x535840`,
`0x53c6a0`), cintas (`0x52e590`), sprites (`0x5491e0`, `0x549510`, `0x549440`) y partículas
(`0x53fe70`, `0x5446e0`, `0x548550`). Son raíces de lectura, no un inventario exhaustivo de
casos ni una afirmación de que esas funciones estén completas en MuRet.

**Las obligaciones ya asignadas se mantienen:** CL7.6 conserva el login, el mundo, la luz de
selección, las acciones del caballo y las armas que describe [Lo que dejó la CL6](#lo-que-dejó-la-cl6),
además de las partículas, el clima y los pases del mundo de sus hitos 3 y 4. En particular P-C53,
P-C54 y P-C55 de [08](../../../Client/docs/research/08-pendientes.md) y el frame de cero pasos de
entrada siguen siendo trabajo de CL7. Los números de daño (`0x548eb0`) conservan CL9.2; los ítems
del piso (`0x608a00`), CL9.3; las ventanas y el HUD, CL8; los sonidos, CL10.

CL11 completa las familias y los casos restantes e integra los efectos de combate, skills y
eventos con CL9, que conserva sus reglas, estado y paquetes. CL11.R debe enumerar qué casos
pertenecen ya a otra ola y cuáles le quedan, con los campos persistentes, el RNG compartido, los
límites de los pools y el orden de creación, paso, dibujo y limpieza. Una función compartida se
registra una sola vez, con sus consumidores y el detalle de casos por ola: no se vuelve a portar
la base ni se traslada a CL11 un bloqueo del cierre de CL7. El QA y las observaciones nativas se
estiman aparte; una suite en verde no acredita equivalencia visual completa.

### Mapeo completo del decompilado a las olas

**CL.M, la matriz completa (2026-11-13), con la revisión independiente pendiente.** El
[plan de cobertura](../plans/2026-11-10-cobertura-roadmap-cliente.md) dejó una fila por VA del
inventario en [`mainexe-roadmap.tsv`](../../../Client/docs/research/coverage/mainexe-roadmap.tsv): una
ola responsable para cada función de juego en alcance —todas se llaman `FUN_*`— y una justificación
para cada biblioteca, thunk o exclusión. Las funciones compartidas tienen una sola ola responsable y
sus otras olas consumidoras. El método, los conteos y el avance están en
[10](../../../Client/docs/research/10-cobertura-roadmap.md): 3.594 funciones de juego (2.329.285
bytes; la CL7 el 30,8 %, la CL8 el 37,0 %, la CL9 el 20,6 %), 1.887 de bibliotecas, 129 excluidas y
38 thunks, sin pendientes. La exclusión del anticheat del propio `main.exe` alcanza también a
GameGuard, enlazado en el exe (72 funciones, 29 KB), que hasta ahora no se había identificado.

El [inventario fuente](https://github.com/EmaNLJN/mu-server-repack-research-102i/blob/91359b2ceffa4ce085ea19bc7bf11762fc2034a5/client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv)
contiene **5.648 funciones internas, incluidas 4.403 con nombre `FUN_*` y 38 thunks**. Los
`FUN_*` de este inventario no son thunks. La suma
de `size_bytes` es 2.494.367 bytes; 2.477.342 corresponden a `FUN_*`. Esto cuenta registros y
tamaños del inventario, no funciones de juego pendientes de portar. El TSV no distingue juego de
CRT/bibliotecas ni trae olas. Los unos 821 KB y el 36 % del traspaso se reproducen con la matriz:
807 KB, el 35,5 % del código de juego, es lo que tiene ola solo por las reglas de CL.M o por el
barrido de la CL8, posterior al traspaso ([10](../../../Client/docs/research/10-cobertura-roadmap.md#los-821-kb-y-el-36--del-traspaso)).

CL.M cierra cuando las 5.648 VAs estén reconciliadas, no quede ninguna clasificación o asignación
pendiente y el informe reproduzca los conteos y tamaños por ola, bibliotecas y exclusiones. Asignar
una función no significa haberla leído entera, implementado ni verificado: esos estados se
registran aparte. CL.M no agrega funcionalidades fuera del 1.02 ni cambia las exclusiones de
[Fuera de alcance](#fuera-de-alcance).

**Planes detallados:** se escriben al arrancar cada ola, como pide el camino de aprendizaje
(L-10). Ya están los de la [CL0](../plans/2026-10-30-ola-cl0-fundaciones-cliente.md), la
[CL1](../plans/2026-11-01-ola-cl1-formatos-sin-ventana.md), la
[CL2](../plans/2026-11-02-ola-cl2-protocolo-sin-red.md), la
[CL3](../plans/2026-11-02-ola-cl3-plataforma-y-bucle.md), la
[CL4](../plans/2026-11-02-ola-cl4-red-del-cliente.md), la
[CL5](../plans/2026-11-02-ola-cl5-render-2d-y-login.md), la
[CL6](../plans/2026-11-03-ola-cl6-modelos-y-seleccion.md) y los dos de la CL7: el
[login](../plans/2026-11-05-ola-cl7-el-login-completo.md) y
[Lorencia](../plans/2026-11-05-ola-cl7-lorencia.md).

### Lo que dejó la CL6

Cada plan lo suma al escribirse. El detalle está en las sub-olas del
[plan de la CL6](../plans/2026-11-03-ola-cl6-modelos-y-seleccion.md#al-cerrar-la-cl6) y en el
[08](../../../Client/docs/research/08-pendientes.md).

- **CL7.**
  - **Los mundos de la selección y del login:** el mapa 54 (`World55`) y el 55 (`World56`) van en el
    pase 3D de la CL6.3, antes de los personajes y con la misma cámara, como `0x5fc260` antes de
    `0x510d00`.
  - **Lo que pide el terreno:** la z del objeto de la vista previa (hoy en (50, 50, 0)) y la zona
    segura de la celda del jinete, que apaga el caballo.
  - **El `F3 03`:** su respuesta (`0x4c3940`) y el tope de `EnteringWorld` (CL7.L1). Las velocidades del
    keepalive (`ServerSession::SetKeepaliveSpeeds`) no salen de esa respuesta: `0x4c3940` no escribe `+0x4c`
    ni `+0x58`, y quién las escribe queda en P-C28.
  - **El movimiento:** los multiplicadores de `0x4f9580` (son de la velocidad de la animación, no tocan
    caminar ni correr: 06 § Caminar), y que la cámara del juego (CL7.3) dé un
    `Render::Camera3D`.
  - **Volver a la selección desde el juego** (el `F1 02` con 1): `SceneTransitions` ya lo deja, pero
    `CharacterSelectScene::Enter` pide la lista solo en `LoggedIn`, y con la sesión en
    `EnteringWorld` la escena quedaría con la lista vieja y sin poder elegir, crear ni borrar.
- **CL7.6, los efectos.** La CL7.R la parte en dos; la nueva [CL11](#la-ola-de-efectos-visuales-cl11)
  conserva esta parte de CL7. Lo del login va al
  principio de la CL7, en la CL7.6 A a C ([abajo](#el-login-completo-cl7r)): el dragón, la base de los
  efectos con las brasas, el aliento del caballo (`0x5f2`; antes se lo daba como polvo, que solo sale
  con el jinete en la `0x4b`), el brillo del cetro y los 30 destellos de la primera tela del Dark Lord
  (`0x549440` → `0x549290`, leídos en la CL7.R), y el cuervo. Lo demás queda al final de la CL7, en el
  pase 3D del mundo con su cámara:
  - la luz de abajo del elegido (`0x53fe70`, la `chasellight`): `0x61ca30` ya carga su textura, la
    `0x729`, y la selección no la dibuja;
  - los efectos de las acciones del caballo (el polvo de los cascos con el jinete en la `0x4b`);
  - los de las armas, salvo el cetro del login.
- **CL8.**
  - **El menú de la selección:** la ventana de `+0x3d0`, que abren Esc y el botón del menú, como el
    menú del login (P-C13); y la de `+0x7e8`. Las dos, sin leer.
  - **El área de mensajes (CL8.2):** 120 líneas, `0x555190`. La selección la vacía y escribe una
    línea con el texto 1240 como formato y los dos números de `pkt[5]` de la lista.
  - **El `0x0D`:** el string del primer aviso de tipo 1 va a la izquierda de la franja de la barra
    (`+0x511`), cuando MuRet lea ese paquete.
  - La CL6.5 no pasó a la CL8: entró entera en la CL6, salvo lo de esta lista.
- **CL9.**
  - **Los modelos de los monstruos** (CL9.1), con el research ya corregido. Falta `Monster50` en el
    laboratorio, y el original se cierra cuando aparece el tipo 58
    ([09](../../../Client/docs/research/09-datos-disponibles.md#el-data-del-cliente-contra-lo-que-pide-mainexe)).
  - **La pose de los NPC** (Q-C108).
  - **Q-C95,** con el primer modelo que se desvanezca.
  - **La tela sobre la malla de un modelo** (la vtable `0x6b8d04`, de algunos monstruos y tres
    pantalones), y los colores de la tela de los duelos y las guerras de guild (P-C22 e y f).

### El login completo (CL7.R)

Plan: [`2026-11-05-ola-cl7-el-login-completo.md`](../plans/2026-11-05-ola-cl7-el-login-completo.md).
Emanuel comparó el login con un video del original: faltaba todo lo que rodea al Dark Lord. El
barrido de la CL7.R ([06](../../../Client/docs/research/06-render-escenas-y-ui.md#el-mundo-del-login),
de El mundo del login a Lo demás del login) lo leyó entero, y la CL7 arranca por ahí, en este orden:

1. **El mundo del mapa 55** (CL7.1 y CL7.2): el terreno (el lago, el agua animada, las celdas con el bit `0x08` y el
   slot `0x122`) y los 276 objetos del `.obj` con sus casos (los remolinos, el farol, los marcadores
   ocultos, las hojas, los haces de luz y las banderas), en el orden del original.
2. **El dragón de fondo** (CL7.6 A): el efecto `0xb4` con `Object56\Dragon.bmd`, que vuela en ocho sin azar,
   contra los pasos emulados del código real.
3. **La base de los efectos y los del login** (CL7.6 B): el billboard, los sprites de un frame, las partículas
   y las hojas; las brasas, el aliento del caballo, el brillo del cetro y los 30 destellos de la capa.
4. **El cuervo** (CL7.6 C; `Skill\DarkSpirit.bmd`, que estaba en la CL8 con las mascotas): su paso, bit a bit
   contra el emulado, su dibujo, su sombra y su estela.

Después sigue la CL7 con Lorencia (CL7.3 en adelante). Quedan para sus olas la música y el clic
(CL10) y las ventanas de Esc y de los créditos (CL8). El cursor del juego y la cabeza que gira al azar
entraron con el cierre del login.

### Lorencia (CL7.R2)

Plan: [`2026-11-05-ola-cl7-lorencia.md`](../plans/2026-11-05-ola-cl7-lorencia.md). El barrido de Lorencia
leyó la escena 5 en cuatro partes ([06](../../../Client/docs/research/06-render-escenas-y-ui.md#la-escena-del-juego),
de La escena del juego a Los cambios de mapa): la entrada al mundo y la escena, la cámara, el caminar, y los
mapas con los gates. Cambia el orden de las sub-olas que quedaban:

1. **La cámara primero** (CL7.3 A): `0x635ae0`, bit a bit contra el código emulado y la traza. El mouse no
   la mueve: el roadmap la daba como "la cámara que se mueve", y es la que sigue al héroe.
2. **Lorencia quieta** (CL7.L1 a L5, nuevas): la entrada al mundo con la respuesta del `F3 03`, los modelos
   con nombre de `Object1`, el héroe parado con el equipo del `F3 10`, la luz dinámica del terreno, el pasto
   y lo que vive (hojas, pájaros, peces, fuegos).
3. **Caminar** (CL7.4 A a E): el camino de `0x4ead70`, el `D3` que sale y los que llegan (el `D7` que llega
   es el daño, no el movimiento), el paso entre celdas, el picking del terreno y la escena.
4. **Los otros mapas** (CL7.5 A a E): cada mundo del `Data\`, `Gate.bmd` con la detección del cliente, el
   `1C`, el `F3 04` y el `F3 12`, el cambio de mapa adentro del paso y los casos de cada mapa como datos.
5. **Los efectos que dejó la CL6** (CL7.6), y un cierre como el del login.

### El HUD y las ventanas (CL8)

Plan: [`2026-11-12-ola-cl8-hud-y-ventanas.md`](../plans/2026-11-12-ola-cl8-hud-y-ventanas.md). El barrido de la
CL8 (2026-11-12) leyó la UI del juego en cinco partes (el HUD y el 2D, las ventanas y su administrador, el
inventario, el chat y el paso de la UI; [06](../../../Client/docs/research/06-render-escenas-y-ui.md#hud-y-ventanas))
y cambió la forma de la ola:

1. **No hay minimapa en el 1.02:** el modo 4 de las superposiciones es la ventana /move. La CL8.1 es el HUD con
   /move, las teclas y la captura.
2. **Las ventanas del juego no son clases:** el inventario, el personaje, la party y las demás son bits de un
   registro de paneles, dibujados en modo inmediato. Las clases retenidas son solo las del administrador viejo
   (amigos, correo, cajas de mensaje, el registro del chat) y las de la UI nueva del login. La CL8.3 son los
   paneles, los diálogos del juego (`0x623e70`, con el menú de Esc) y los dos administradores.
3. **Ocho hitos, un PR por hito:** la UI toma el mouse (los paneles, el bloque de UI del paso y la pasada 2D), el
   HUD, el chat, el inventario, los tooltips y la ventana del personaje, los diálogos con el menú de Esc y las
   teclas, las ventanas viejas, y la UI nueva con el Esc, las opciones y los créditos del login y la selección.
4. **Usar y tirar ítems** (`26`, `23`) quedan en la CL9.3, con los ítems del piso.

### El gameplay (CL9)

Plan: [`2026-11-13-ola-cl9-gameplay.md`](../plans/2026-11-13-ola-cl9-gameplay.md), con la tabla de
[funciones](../plans/2026-11-13-ola-cl9-funciones.tsv) y la de [cortes](../plans/2026-11-13-ola-cl9-cortes.tsv).
El barrido (2026-11-13) repartió las 1.014 funciones de la CL9 de la matriz (469 KB), las 127 de otras olas sin
portar que solo llama la CL9 (48 KB) y los 240 cortes de MuRet hacia la CL9:

1. **Nadie entra al viewport:** MuRet no registra ningún handler de la CL9, así que en un servidor real el héroe
   está solo. Los hitos 1 y 2 (otros jugadores; monstruos, NPC y misiones) son el portón de la ola.
2. **El emote abre la ola** (hito 1): `0x576eb0` busca 95 textos, 15 de ellos literales del binario que la CL8
   no mira (`;`, `^^`…), y con el héroe parado manda dos `C1 18` iguales.
3. **Trece hitos, un PR por hito:** el emote y los otros jugadores; monstruos, NPC y misiones; atacar, el daño
   y la muerte; las skills, el cuervo y las mascotas; los ítems del piso; las tiendas de NPC, el almacén y la
   caja del caos; la party, el comercio, el duelo, las tiendas personales y los comandos; el gremio; los
   amigos; el chat entre amigos y las cartas; los eventos; y Castle Siege en dos (las ventanas y la batalla).
4. **La matriz se equivoca en diez opcodes** de la tabla del generador (`0x20`, `0x5D`, `0x3F`, `0x51`, `0xCA`,
   `0x9C`, `0xA3`, `0x9F`, `0x0B`, `0x2C`, `0xA9`): el plan usa las sub-olas corregidas y el generador se
   corrige con la revisión independiente de la CL.M.

## Cómo el research se vuelve test

| Sección del research | Ola | Tipo de test |
| --- | --- | --- |
| Tablas `.bmd`, terreno, texturas ([05](../../../Client/docs/research/05-formatos-de-datos.md)) | CL1 | round-trip sintético + byte a byte contra `mu-golden` |
| Corte y rearmado ([03](../../../Client/docs/research/03-red-y-cifrado.md)) | CL2 | vectores de `red_reference.py --selftest` |
| XOR32, C3/C4, FoxAnti ([03](../../../Client/docs/research/03-red-y-cifrado.md)) | CL2 | fixtures de `mu-client` (los del C2, armados de nuevo); C3/C4 reales del cable con `MURET_CLIENT_WIRE_DIR` |
| Login y keepalive ([04](../../../Client/docs/research/04-protocolo.md)) | CL2, CL4 | bytes exactos; integración con CS y GS falsos |
| Ritmo del frame ([02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md)) | CL3 | reloj falso: ticks por segundo fijos a cualquier FPS; en modo fiel, 64 frames cada 3 s y, desde el segundo frame, 75 pasos cada 64 frames, 11 de ellos con 2 (7 u 8 cada 6) |
| Escenas, texto, cámara, terreno ([06](../../../Client/docs/research/06-render-escenas-y-ui.md)) | CL5, CL7 | máquina de escenas, colores `0x02`; matrices de cámara y terreno de Lorencia contra las trazas de GL del original |
| Modelos y animación ([05](../../../Client/docs/research/05-formatos-de-datos.md), [06](../../../Client/docs/research/06-render-escenas-y-ui.md)) | CL6 | skinning del personaje de la selección contra la traza (golden); avance de la animación por paso con la velocidad de cada acción y reloj falso; mascota por tiempo (Q-C20); UV de brillo contra la traza |
| Efectos visuales ([06](../../../Client/docs/research/06-render-escenas-y-ui.md#los-efectos-del-login), [08](../../../Client/docs/research/08-pendientes.md)) | CL7.6, CL9.2, CL11 | casos por familia: alta, paso, dibujo y limpieza, campos persistentes y RNG en orden; trazas disponibles y observación nativa registradas con su nivel de evidencia |
| Quirks ([07](../../../Client/docs/research/07-quirks-y-bugs.md)) | donde toque | test viejo vs nuevo, o reproducción del contrato |

## Cambios al repo de este PR

| Archivo | Cambio |
| --- | --- |
| `Client/` | nace: `CMakeLists.txt`, `src/main.cpp`, `README.md`, `docs/research/` 01..09 |
| `CMakeLists.txt` raíz | `add_subdirectory(Client)` |
| `README.md`, `AGENTS.md`, `CLAUDE.md` | el cliente como componente y dónde está cada cosa |
| `docs/superpowers/specs/` | este roadmap |
| `docs/superpowers/plans/` | el plan de la ola CL0 |
| `docs/learning/README.md` y `PROGRESS.md` | dónde entra el cliente en el camino |

## Fuera de alcance

- El anticheat de FoxAnti y del propio `main.exe` (ResourceGuard, el hash anti-tamper y GameGuard:
  [10](../../../Client/docs/research/10-cobertura-roadmap.md#bibliotecas-thunks-y-exclusiones)).
- El launcher (`mu.exe`) y lo suyo: el auto-update, `mumsg.dll` y `message.wtf`, el paso de
  `WinMain` que termina la actualización (`0x4b96b0`,
  [05](../../../Client/docs/research/05-formatos-de-datos.md#el-pipeline-de-las-dlls)) y el
  servidor que deja en el registro para el juego (`wzPopLaunchInfo`,
  [02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md#del-launcher-al-juego)).
- Distribuir un cliente jugable con datos. La `Data\` del original está en el repo como referencia
  (2026-11-04), no como parte de un cliente para jugar.
- Funciones de clientes posteriores al 1.02 (Season 6, MU Helper, tienda).
- Cambiar quién escribe la lógica del cliente: sigue siendo Emanuel.
