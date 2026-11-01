# Cliente en MuRet: roadmap de la reimplementación

> **Documento de trabajo, 2026-10-30.** Es el plan para reescribir el cliente de MU Online 1.02
> (`main.exe`) en MuRet: el objetivo, las decisiones de diseño, la estructura de `Client/` y las
> olas CL0 a CL10 con sus sub-olas. Queda vivo mientras haya olas abiertas.
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
- **Más fluido:** desacoplar la lógica del dibujo para correr a los FPS que dé la máquina (VSync o
  sin tope) sin cambiar la velocidad del juego (D-C4).

**Modernizar nunca sacrifica fidelidad.** Todo lo que el original hace observable se mantiene al
byte o al tick: lo que se manda y se entiende por la red, cómo se leen los archivos de `Data\`, la
velocidad de la lógica (movimiento, animaciones, cooldowns, keepalive) y lo que ve el jugador. Lo
moderno es la implementación (C++20, OpenGL 3.3, capas, tests) y lo que el original no fija:
cuántos frames se dibujan entre dos ticks, la resolución, el suavizado. Si una mejora cambiara algo
observable, va detrás de una opción y con un ADR, y el modo por defecto es el fiel.

Es un **proyecto de aprendizaje**, igual que el servidor: Emanuel escribe la lógica del cliente
sub-ola por sub-ola y Claude hace de tutor. Claude escribe el andamiaje (CMake, dependencias, CI,
plantillas de docs) cuando se lo piden y los pasos marcados `(Claude)` en los planes.

**Qué NO es el objetivo:** un cliente de Season 6 (eso es MuMain), ni portar el anticheat, ni
servir los assets del juego. MuRet no versiona ningún archivo de `Data\`: el jugador apunta el
cliente a una instalación existente.

## Cómo trabajamos

Igual que el servidor ([roadmap del DataServer](2026-10-25-dataserver-port-design.md#cómo-trabajamos)):
olas por tema, sub-olas de un concepto, un PR chico por sub-ola, GoogleTest en verde, y el ritmo
explicación → mini-spec → Emanuel lo escribe → revisión con preguntas → cierre en `PROGRESS.md`.

Lo propio del cliente:

- **Las olas se llaman CL0 a CL10** para no chocar con las del servidor (0 a 14) ni con los
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
| D-C1 | Lenguaje | C++98 sin STL visible, globales | C++20 como el resto de MuRet, sin globales del juego | RAII, valores en vez de estado global, `std::span`, `std::expected` |
| D-C2 | Build | proyecto de VC6 | el mismo CMake con presets del repo; dependencias por vcpkg (SDL3, una biblioteca JPEG, FreeType o SDL3_ttf, GoogleTest). El loader de GL es propio: una tabla de punteros pedidos a `SDL_GL_GetProcAddress` (CL0.3; el puerto `glad` de vcpkg trae el perfil de compatibilidad y pide Python). Si en la CL3 la tabla crece demasiado, se revisa | un solo build para los dos sistemas |
| D-C3 | Ventana, input y gráficos | Win32 + `wgl` + OpenGL 1.x de función fija | **SDL3** para ventana, input y audio, y **OpenGL 3.3 core** con shaders propios detrás de `Render::` (decidido por Emanuel el 2026-10-30) | el pipeline gráfico moderno (buffers, shaders, matrices) sin la complejidad de Vulkan; mismo stack de ventana que MuMain |
| D-C4 | Ritmo del frame | pasos de lógica de 40 ms (25 por segundo) y una espera activa de 40 ms medida con `GetTickCount`, que con ticks de 15,625 ms termina a los 46,875 ms: **21,33 FPS**, medido en la selección y en Lorencia, y 7 pasos de lógica cada 6 frames, *inferido* de la aritmética del bucle ([02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md)) | **tick de lógica de 40 ms** con el mismo bucle de alcanzar que el original + **render desacoplado** con interpolación, VSync por defecto, sin tope opcional; como el [ADR 0001 de MuMain](https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md). Un **modo fiel** en la config reproduce la espera del original con un reloj de 15,625 ms (46,875 ms por frame), para comparar lado a lado | el bucle de juego con paso fijo; más FPS sin que el juego vaya más rápido ni cambie lo que se manda al servidor |
| D-C5 | Red | `WSAAsyncSelect` en el hilo de la ventana | un socket TCP no bloqueante que el bucle consulta en cada frame; un solo hilo | E/S no bloqueante; por qué un cliente no necesita IOCP |
| D-C6 | FoxAnti | DLL que parchea el binario | `Network::Obfuscation` opcional, con una implementación identidad y una FoxAnti (solo la capa de paquetes), elegida por config según el servidor | separar el contrato de un servidor concreto del cliente |
| D-C7 | Datos | cargadores que escriben globales; `MessageBox` si falta algo | funciones puras bytes → tipos, errores con `std::expected`; los assets no se versionan | testear sin ventana; datos de prueba sin violar derechos |
| D-C8 | Quirks | se replican tal cual | contrato de datos se reproduce; bugs internos se corrigen con test viejo/nuevo (regla de [07](../../../Client/docs/research/07-quirks-y-bugs.md)) | distinguir contrato de accidente |
| D-C9 | Tests | — | GoogleTest; los de datos reales leen `MURET_CLIENT_GOLDEN_DIR`, una carpeta con el contenido de `Data\` (por ejemplo `analysis/decompilacion-maxima/assets/original-data` de `mu-online-reverse-lab`). Sin la variable, se saltean; si apunta a una carpeta que no existe o le falta el archivo pedido, fallan (CL0.2) | oráculos sin meter assets al repo |
| D-C10 | Config | registro de Windows + `connect /u<ip> /p<puerto>` | archivo de config + argumentos; se sigue aceptando `connect /u /p` | nada hardcodeado |
| D-C11 | Resolución | 640 × 480 de referencia | la UI se diseña en 640 × 480 y se escala a la ventana real; resoluciones y pantallas anchas modernas | separar coordenadas lógicas de físicas |
| D-C12 | Texto | GDI → textura, caché por texto, IME coreano | FreeType o SDL3_ttf → textura, la misma caché; se conserva el byte `0x02` de color | rasterizado de fuentes, UTF-8 |
| D-C13 | Audio | `wzAudio.dll` (derivado de CoolPlayer 215: MP3 y Ogg) + DirectSound (WAV) | audio de SDL3 detrás de `Audio::`, con decodificadores de MP3, Ogg y WAV de terceros; la ABI de `wzAudio` es el contrato de comportamiento (volumen, posición, ecualizador) | mezcla de audio, streaming |
| D-C14 | Anticheat | watchdog, hash, `0x2B11` | **no se porta** | fuera de alcance (`AGENTS.md`) |

### Preguntas abiertas (se deciden al arrancar la ola que las necesita)

1. **Biblioteca JPEG (CL1).** ¿libjpeg-turbo de vcpkg alcanza para ser byte a byte con la libjpeg
   6b del original en los tiles de `mu-golden`? Si no, ¿se acepta una diferencia mínima o se
   escribe el decodificador (como hizo `mu-client`)?
2. **Código compartido con los servidores (CL2).** El corte C1/C2/C3/C4 y el cifrado C3/C4 los
   necesitan el cliente y el GameServer (y el ConnectServer de la ola 10). ¿Nace un `Common/` en
   la raíz cuando lo usen dos, como dice el roadmap del servidor para `Server/Common/`?
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
│ Network   Socket no bloqueante · PacketReader/Writer · SimpleModulus · Obfuscation │
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
recortada a lo que tiene el 1.02 y con los tests al lado, como `Server/DataServer/`:

```text
Client/
  CMakeLists.txt           target Main (y desde CL0.2, ClientLib + ClientTests)
  README.md                qué es, cómo compilar y correr, estado
  docs/research/           QUÉ HACE EL ORIGINAL (01..09)
  src/
    main.cpp               arma las capas y arranca; sin lógica
    App/                   Application, bucle de juego con tick fijo     (CL3)
    Core/                  Config, Log, Clock                              (CL0+)
    Platform/              ventana, contexto GL, input sobre SDL3          (CL3)
    Data/                  Bmd/, Terrain/, Textures/, Text/                (CL1)
    Network/               Socket, PacketReader/Writer, Crypto/, Obfuscation/ (CL2, CL4)
    Protocol/              opcodes, structs, Dispatcher, handlers por dominio (CL2, CL4, CL9)
    Render/                Renderer, Shader, Texture, SpriteBatch, Text, Terrain, Models (CL5+)
    Scenes/                Login, CharacterSelect, Game                    (CL5, CL6)
    World/                 Map, Camera, objetos del mapa                   (CL7)
    GameLogic/             personaje, viewport de entidades, items, chat   (CL7+)
    UI/                    widgets, ventanas, HUD                          (CL5, CL8)
    Audio/                 música y efectos                                (CL10)
  tests/                   data/ network/ protocol/ app/ … (GoogleTest)
```

El ejecutable se llama `Main`, como el `main.exe` original y el target de MuMain.

## Plan por olas

Cada sub-ola es un PR chico. "Terminada cuando" es la condición objetiva de cierre. La columna
"Research" apunta a [`Client/docs/research/`](../../../Client/docs/research/README.md).

| Ola | Tema | Sub-olas (un PR cada una) | Conceptos | Research |
| --- | --- | --- | --- | --- |
| CL0 | Fundaciones del cliente | CL0.1 esqueleto `Client/` + este roadmap + el research (este PR) · CL0.2 `ClientLib` + `ClientTests` y la opción `MURET_CLIENT_GOLDEN_DIR` (trae ella misma vcpkg y GoogleTest, sin esperar la 0.2 del servidor) · CL0.3 SDL3 con OpenGL 3.3 core y su loader, CI que compila y testea en Windows y Linux | targets, dependencias, tests salteables | 01 |
| CL1 | Formatos sin ventana | CL1.0 repaso de la 1.0 (bytes, endianness) + archivos binarios · CL1.1 XOR3, checksum y `Text.bmd` · CL1.2 `Item`, `Skill` y `Gate.bmd` · CL1.3 terreno: K16, `.map`, `.att`, `.obj`, alturas · CL1.4 texturas OZT y OZJ · CL1.5 `message.wtf` | lectura de formatos, `std::expected`, tests golden | 05, 07 |
| CL2 | Protocolo sin red | CL2.1 `PacketReader` C1/C2/C3/C4 · CL2.2 `PacketWriter` + XOR32 · CL2.3 `SimpleModulus` y `Enc1`/`Dec2.dat`, seriales · CL2.4 `Obfuscation` (identidad y FoxAnti) · CL2.5 structs del login: `F4 02`, `F1 01`, `0x0E` | máquinas de estado, cifrado por bloques, bits MSB primero | 03, 04, 07 |
| CL3 | Plataforma y bucle de juego | CL3.0 el bucle de juego (paso fijo, interpolación, VSync) · CL3.R qué depende del paso de 40 ms (P-C03) · CL3.1 ventana y contexto GL con SDL3 · CL3.2 bucle con tick fijo y render desacoplado, con reloj falso en los tests · CL3.3 input (teclado por frame, mouse) · CL3.4 config + línea de comandos + log | game loop, tiempo, eventos del SO | 02, 08 |
| CL4 | Red del cliente | CL4.1 socket no bloqueante portable consultado por el bucle · CL4.2 ConnectServer: saludo, lista, IP del GS · CL4.3 GameServer: saludo, versión y login · CL4.4 keepalive, desconexión y cola de envío · CL4.5 `Dispatcher` y el opcode desconocido | E/S no bloqueante, estados de una conexión | 03, 04 |
| CL5 | Render 2D y la escena de login | CL5.0 el pipeline gráfico (buffers, shaders, matrices, texturas) · CL5.1 `Renderer` y texturas desde OZT/OZJ · CL5.2 `SpriteBatch` en 640 × 480 escalado · CL5.3 texto con caché y colores `0x02` · CL5.4 `SceneManager` y `LoginScene` · CL5.5 UI de login conectada a CL4: lista de servidores, cuenta y clave | GPU, shaders, coordenadas lógicas | 06 |
| CL6 | Modelos y selección de personaje | CL6.R lectura guiada del render de modelos (P-C01, ya reproducido contra las trazas) y flujo `F3` (P-C06) · CL6.1 parseo de modelos · CL6.2 skinning y animación · CL6.3 `CharacterSelectScene` con preview 3D y los paquetes de selección | jerarquías de huesos, interpolación, cuaterniones | 05, 06, 08 |
| CL7 | Mundo | CL7.R lectura guiada del renderer de terreno (P-C02, ya identificado y verificado) · CL7.1 terreno con capas, alfa y luz · CL7.2 objetos del `.obj` · CL7.3 cámara · CL7.4 picking y movimiento (`0xD3`/`0xD7`): caminar en Lorencia | mallas, frustum culling, rayos | 05, 06 |
| CL8 | HUD y ventanas | CL8.1 barras y minimapa · CL8.2 chat · CL8.3 `UI::Window` y el administrador · CL8.4 inventario | UI retenida vs inmediata, eventos | 06 |
| CL9 | Gameplay | CL9.R por dominio (P-C09) · CL9.1 otros jugadores y monstruos (viewport) · CL9.2 combate y skills · CL9.3 items · CL9.4 party, gremio y comercio | modelado de dominio, consistencia con el servidor | 04 |
| CL10 | Audio | CL10.R formatos y disparadores (P-C07) · CL10.1 música · CL10.2 efectos | audio digital, mezcla, streaming | 08 |

**"Terminada cuando", por ola:** CL0 — `Main` compila y corre en los dos sistemas, en CI. CL1 —
cada formato pasa su round-trip y, con `mu-golden`, su comparación byte a byte. CL2 — pasan los
vectores del cable y los fixtures de `mu-client` para corte, XOR32, C3/C4, FoxAnti, login y
keepalive. CL3 — una ventana a la tasa del monitor con la lógica a tick fijo, verificado con un
reloj falso. CL4 — el cliente pasa el login contra el GS del laboratorio. CL5 — la pantalla de
login se ve y se usa. CL6 — el personaje aparece animado en la selección. CL7 — se camina por
Lorencia. CL8 — chat e inventario usables. CL9 — una sesión de juego estable. CL10 — música y
efectos.

**Planes detallados:** se escriben al arrancar cada ola, como pide el camino de aprendizaje
(L-10). Ya está el de la [CL0](../plans/2026-10-30-ola-cl0-fundaciones-cliente.md).

## Cómo el research se vuelve test

| Sección del research | Ola | Tipo de test |
| --- | --- | --- |
| Tablas `.bmd`, terreno, texturas ([05](../../../Client/docs/research/05-formatos-de-datos.md)) | CL1 | round-trip sintético + byte a byte contra `mu-golden` |
| Corte y rearmado ([03](../../../Client/docs/research/03-red-y-cifrado.md)) | CL2 | vectores de `red_reference.py --selftest` |
| XOR32, C3/C4, FoxAnti ([03](../../../Client/docs/research/03-red-y-cifrado.md)) | CL2 | fixtures de `mu-client`; C3 reales del cable con `mu-golden` |
| Login y keepalive ([04](../../../Client/docs/research/04-protocolo.md)) | CL2, CL4 | bytes exactos; integración con CS y GS falsos |
| Ritmo del frame ([02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md)) | CL3 | reloj falso: ticks por segundo fijos a cualquier FPS; en modo fiel, 64 frames cada 3 s y 7 pasos cada 6 frames |
| Escenas, texto, cámara, terreno ([06](../../../Client/docs/research/06-render-escenas-y-ui.md)) | CL5, CL7 | máquina de escenas, colores `0x02`; matrices de cámara y terreno de Lorencia contra las trazas de GL del original |
| Modelos y animación ([05](../../../Client/docs/research/05-formatos-de-datos.md), [06](../../../Client/docs/research/06-render-escenas-y-ui.md)) | CL6 | skinning del personaje de la selección contra la traza (golden); avance de la animación por paso con la velocidad de cada acción y reloj falso; mascota por tiempo (Q-C20); UV de brillo contra la traza |
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

- El anticheat de FoxAnti y del propio `main.exe`.
- Versionar assets del juego o distribuir un cliente jugable con datos.
- Funciones de clientes posteriores al 1.02 (Season 6, MU Helper, tienda).
- Cambiar quién escribe la lógica del cliente: sigue siendo Emanuel.
