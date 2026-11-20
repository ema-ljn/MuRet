# La arquitectura del cliente: capas, módulos y clases chicas (diseño)

> **Documento de trabajo, 2026-11-20.** Pedido de Emanuel: un plan para desacoplar, refactorizar,
> deduplicar y modularizar el cliente, porque "todo se hizo con prisa" y "todo termina en un
> ClientLib", sin perder lo que falta portar (huecos y trazas), que tiene que seguir rastreado. Las
> referencias que pidió son OpenMU y TrinityCore ("mis modelos para aprender cómo se organiza un
> proyecto grande"), con clases tan concisas como las de OpenMU, y mejorarlas donde se pueda. Pidió
> también evaluar si el engine va en su propio módulo.
>
> Este documento dice **cómo queda el código** y **qué no se negocia** (la fidelidad y el rastreo).
> El orden del trabajo, por épicas e hitos, está en el
> [plan](../plans/2026-11-20-refactor-arquitectura-cliente.md). Las mediciones se hicieron sobre
> `main` en `bb56680` (2026-11-20).

## Qué y para qué

- **Objetivo:** que cada cosa del cliente tenga un lugar obvio, con dependencias en un solo sentido,
  clases y archivos chicos con una responsabilidad, y sin la misma lógica escrita varias veces; que
  agregar el handler de un paquete o una ventana toque dos o tres archivos de su módulo y no seis
  repartidos en tres capas.
- **Objetivo:** que el port que falta (huecos, cortes, VM, filas de 08) siga medido por módulo, y
  que cada pendiente tenga un módulo de destino antes de portarse.
- **No es objetivo:** cambiar comportamiento. Ningún hito del refactor corrige un bug del port ni
  cambia lo que se dibuja, se suena o se manda: eso es un PR de port, aparte.
- **No es objetivo:** cambiar de motor gráfico, de lenguaje, de versión de C++ o de bibliotecas
  (ver [El motor aparte](#el-motor-aparte-sí-en-su-raíz-y-con-sus-targets)).
- **Sería un fracaso:** un refactor que cambie el orden de las llamadas o la cantidad de `rand()`
  (la fidelidad del port), que deje a los TSV de la referencia contando mal, o que frene el cierre
  de huecos y trazas durante semanas.

## Lo que medimos

Tres análisis estáticos (el cliente no compila en el contenedor de la nube; ver
`cloud-container-build`): el grafo de `#include`, las métricas de cada función (lizard) y la
detección de clones (jscpd y un detector propio por tokens). Los números redondos son de los scripts;
los que dicen "≈" son estimaciones.

### Un monolito en una carpeta

| carpeta | archivos | líneas |
| --- | ---: | ---: |
| `Scenes/` | 687 (349 `.cpp`, todos planos y en `Client::Scenes`) | **164.361** |
| `World/` | 148 | 28.289 |
| `UI/` | 115 | 26.588 |
| `Render/` | 98 | 11.269 |
| `Protocol/` | 97 | 11.101 |
| `Data/`, `App/`, `Core/`, `Network/`, `Platform/`, `GameLogic/`, `Audio/`, `Input/` y `main.cpp` | 273 | 27.451 |
| **total `Client/src`** | **1.418** | **269.059** |

`Scenes/` es el 61 % del cliente. Todo, salvo `Platform/` y `Render/Gl/`, compila en un solo
`ClientLib` con `GLOB_RECURSE`, y los 728 ejecutables de test lo enlazan entero. `Scenes/` no es
"las escenas": adentro están los actores, los monstruos, las skills, los efectos, el inventario, el
chat, los gremios, los eventos y el HUD.

### Dos objetos dios y dos centrales de despacho

- **`WorldState` y `WorldRuntime`** son la `.bss` del original como dos agregados: el pool de 401
  personajes, el registro propio, el mapa y ~90 miembros más (`hud`, `chat`, `guild`,
  `castleSiege`, `crywolf`, `quest`, `social`, `panels`, `music`...). Los incluyen 197 y 194
  archivos, 248 funciones reciben `WorldState&`, `Runtime()` se llama 1.273 veces, y cambiar un
  campo recompila ~420 unidades. `WorldRuntime.h` incluye 45 headers, 15 de `UI/`.
- **El camino de un paquete** cruza seis archivos en tres capas: el lector en `Protocol/X`, la
  alternativa en el `std::variant` de `App/WorldEvent.h` (124 tipos), la declaración y el registro
  en `App/ServerSession.h/.cpp` (2.878 líneas, 125 registros, 88 `On*` casi iguales: leer, avisar
  "se ignora…", `ApplyWorld`), la cadena de 123 `std::get_if` de `Scenes/SessionEventApplier.cpp` y
  el `Apply…` de `Scenes/XReply`. Es el `WorldSession` de TrinityCore (509 `Handle…`) en dos
  partes, y obliga a `/bigobj` en todo `ClientLib`.
- **`WorldEntryResources`** es la bolsa que recibe cada aplicador (75 usuarios): 20 campos, 6 de
  ellos `std::function`. Hay 74 structs `*Context` con 206 referencias y 164 punteros, y 335
  `std::function` en headers: el envío está recableado 47 veces y el sonido 22.
- **`GameScene`** (8 archivos, 3.430 líneas, ~55 métodos) hace nueve trabajos e incluye de 24 de
  los 25 módulos restantes. **`UI/PanelRegistry`** (`Open` con complejidad 92) y
  **`Scenes/GamePanels`** (15 `Lend*`) cablean a mano los paneles de cada función del juego.

### Dependencias al revés

Fuera de `Scenes/` las capas casi se respetan; lo que está mal es poco y está localizado:

| de → a | includes | qué es |
| --- | ---: | --- |
| `Render`, `UI`, `Platform`, `Scenes` → `App/WindowState.h` | 119 | un tipo de la ventana vive en `App` |
| `UI` → `Scenes/ITextDrawer.h`, `SceneTextures.h` | 33 | contratos de dibujo en `Scenes` |
| `Render` → `Scenes/` (`SceneCanvas`, `SceneText`, `IFrameTarget`, `IWorldView`, `Screenshots`, `CharacterDrawing`) | 8 | el frame y el dibujo de personajes al revés |
| `Data`, `GameLogic`, `UI` → `World/OriginalFtol.h` | 3 | matemática del original en `World`, duplicada en `World/Ftol.h` |
| 219 archivos → `Render/Models/Cloth.h` | 219 | casi todos por `CrtRandom`, el `rand()` del CRT (737 usos) |
| todo → `Scenes/CharacterDrawing.h` | 79 | define `IModelLibrary` (8 implementaciones, 75 falsos en tests) |

Adentro de `Scenes/`, la partición en 26 módulos candidatos ([anexo](../plans/2026-11-20-refactor-arquitectura-cliente/))
muestra que **los 26 forman un solo ciclo** y que 100 pares se incluyen mutuamente. Ningún header
vuelve sobre sí mismo: los ciclos vienen de `.cpp` que llaman a otro módulo. Bajar los headers de
estado y el kit de la UI corta 52 de los 100 pares; los 48 que quedan son seis nudos concretos
(el paso del personaje `0x508f00`, los aplicadores del viewport, la composición del HUD,
combate↔héroe, el cuervo de `WorldTransitionSupport` y el actor entero de `LetterPortrait`).

### Repetición

- **Copias exactas, pocas:** 1,3 % de `src` según jscpd. Con los literales normalizados, 5,0 %
  (13.472 líneas) y en los tests 14,0 %. Separando las tablas de monstruos (datos), el 22 % de lo
  repetido copia clones del original y el **54 % es repetición propia de MuRet**.
- **22 funciones del original portadas dos veces** (88 funciones, ≈1.250 líneas): `0x57ce80` en
  `HeroSkills.cpp` y en `Skill57ce80.cpp`, `0x4eac00` en `HeroSkillsElf` y en `HeroSkillsWizard`,
  `0x4eb860`, `0x5fccc0`, `0x4ce000`...
- **Ayudantes copiados:** `F32` (la conversión a `float` de la x87) en 115 archivos, aunque hay uno
  compartido; `TextOf` en 39 (18 idénticos); 24 `InRange`; el `Quad` de `0x621b60` en 10;
  `FieldsOf` en 11 archivos de `Protocol`; `FirstChaosCastleMap` definido 25 veces; 1.224
  `bit_cast<float>(0x…)` con 237 valores distintos; `White` 37 veces.
- **El error a mano:** 1.377 `return Core::Unexpected(x.Error())`, 973 de ellos en dos líneas,
  porque `Core::Expected` no tiene `AndThen` ni `Transform`.
- **Cuatro maneras de leer un paquete:** `Core::ByteReader` (28 archivos), dos clases `Fields`
  privadas (una chequea y la otra no) y `ByteOf`/`WordAt` sueltos en `Scenes`.
- **Tests:** 728 ejecutables; `LetterMeasure` copiado en 40 archivos, falsos de `IModelLibrary`
  en 60, 168 ayudantes definidos en cuatro o más archivos (14.500 líneas). `tests/scenes` (190 mil
  líneas) es más grande que `src/Scenes`.

### Tamaños

8.800 funciones: 505 pasan las 50 líneas, 94 las 100 y 14 las 200; 501 pasan complejidad 15 y 21
pasan 60; 196 tienen más de 6 parámetros (máximo 16, `FaunaJointCreation.cpp`). Los `.cpp` de
`Scenes/` tienen mediana de 241 líneas y p90 de 913. Para comparar, OpenMU: un handler de paquete
tiene mediana de 40 líneas y una acción del jugador, 53.

## Las referencias: qué se toma de cada una

Se leyeron en sus fuentes: OpenMU `master` f6b4243, TrinityCore `master` 5d07da5 y MuMain `main`
b9f4614. Ninguna entra como dependencia.

| patrón | de dónde | en MuRet |
| --- | --- | --- |
| la lógica no ve bytes ni red (`GameLogic` no referencia `Network`) | OpenMU, CODING_RULES §7 | **se adopta:** las funciones del juego reciben el paquete ya leído; las capas se hacen cumplir con targets de CMake y raíces de include |
| un handler por paquete, chico (mediana 40 líneas), agrupados por función (`MessageHandler/Items/…`) | OpenMU | **se adapta:** una función libre por paquete en `game/<Módulo>/In/`, sin interfaz, sin plugin y sin GUID |
| tabla declarativa opcode → handler (`DEFINE_HANDLER` en `Opcodes.cpp`) | TrinityCore | **se adapta:** una tabla `constexpr`, sin macro ni `unique_ptr` por fila, con la dirección del `case` original y filas `SinPortar{va}` |
| paquetes tipados por función (`WorldPackets::Chat::…`) | TrinityCore | **se adopta:** `Protocol/<Módulo>/` |
| la posición del campo en el tipo (`UpdateField<T, Block, Bit>`) | TrinityCore | **se adapta:** `Field<T, Offset, Endian>` para los paquetes y los formatos de `Data\` |
| el layout descrito como datos y comparado (DB2, con `ASSERT` al arrancar) | TrinityCore | **se mejora:** `static_assert` y tests en el build, no al arrancar |
| los paquetes generados desde XML (offsets solo en el XML) | OpenMU | **se pospone:** descriptores `constexpr` en C++ primero; un generador solo si la repetición sigue (CODING_RULES §10) |
| una acción por intención del jugador (`PlayerActions/<Función>/`) | OpenMU | **se adopta:** `game/<Módulo>/Out/`, que manda el paquete |
| "política → plugin, mecanismo → componente, puro → función libre" | OpenMU, `PlayerRefactoringPlan.md` | **se adopta sin la política:** la política es el original |
| plugins por reflexión, GUIDs, versiones de cliente, toggles, `async` | OpenMU | **se rechaza:** hay un solo cliente (1.02) y un solo hilo de juego |
| puntos de plugin sin orden | OpenMU (lo admite) | **se rechaza:** el orden de las llamadas y de los `rand()` es parte del contrato; todo reparto es una lista ordenada que copia al original |
| 190 interfaces de vista con 292 implementaciones | OpenMU | **se rechaza:** con un solo cliente, una interfaz con una implementación es ruido; interfaces solo en los bordes del motor y donde un test las necesita |
| el `Player` de 2.665 líneas ("diez clases con un sobretodo") | OpenMU (lo admite) | **se mejora:** el estado partido por módulo, y cada handler declara en su firma las partes que toca |
| `WorldSession` con 509 handlers | TrinityCore | **se rechaza:** es nuestro `ServerSession` |
| serializar en orden de stream (`Read()`/`Write()`) | TrinityCore | **se rechaza** para lo fijo: los offsets quedan visibles y se comparan con el research |
| "el dibujo no cambia el estado", con un frame de atraso (ADR 0003) | MuMain | **se rechaza:** cambia el momento y la secuencia de `rand()`; se separa *dónde vive* el código, no *cuándo corre* |
| carpetas por capa y por dominio (`GameLogic/{Items,Combat}`), funciones de ~40 líneas | MuMain | **se adopta** la forma; su `WSclient.cpp` (463 `case`) y los `Zzz*` no |

## Approaches

1. **Por épicas, intercalado con el port (elegido, a confirmar por Emanuel).** Primero la red de
   seguridad (el golden de comportamiento y el rastreo a prueba de mudanzas), después el motor
   aparte, el protocolo y el estado, y al final `Scenes/` partido en módulos, de a un grupo por
   hito. Cada hito es una mudanza generada por script (sin lógica, verificable) y un PR de diseño.
   El port sigue en paralelo: lo nuevo entra en el módulo de destino.
2. **Primero cerrar huecos y trazas, después refactorizar.** Cero riesgo de choque con el port,
   pero todo lo que falta entra en la estructura vieja (más `WorldRuntime`, más `ServerSession`) y
   después hay que moverlo igual.
3. **Congelar el port y hacerlo todo junto.** El refactor más rápido, pero frena el port semanas y
   junta todo el riesgo en pocos PRs gigantes que nadie puede revisar.
4. **Solo carpetas y targets.** Barato (mover archivos y partir el CMake), pero deja los objetos
   dios, el despacho central y la repetición: es el primer tercio del approach 1, no un plan.

## La arquitectura de destino

### Tres raíces, cada una con sus targets

```text
Client/src/
  engine/   el motor: no sabe del estado del juego (raíz de include propia)
    Core/      Expected, ByteReader, Log, IClock, Crash/, WrappingInt
    Math/      la matemática del original: Ftol, X87 (F32, Truncate), CrtRandom (el rand() del CRT),
               OriginalRotation, OriginalAngles, OriginalRetainedMath, HalfLifeMath
    Input/     teclado y mouse por frame, sin SDL
    Audio/     música y efectos detrás de Audio::
    Network/   socket no bloqueante, cifrado, armado de paquetes, Connection
    Data/      los formatos de Data\: Tables/, Text/, Terrain/, Textures/, Models/
    Platform/  SDL3: ventana, contexto GL, eventos, WindowState, VSyncMode, Resolution, portapapeles
    Render/    SpriteBatch, ModelBatch, texto, contratos del frame (IFrameTarget, ITextDrawer), Gl/
    Models/    esqueleto, pose, animación, skinning, tela, IModelLibrary
  game/     el juego del 1.02 (raíz de include propia; ve engine/)
    Protocol/  layouts y escritores de los paquetes, por módulo (Protocol/Guild/...)
    Kernel/    el estado compartido: GameContext, pool de actores, jugador, mapa, assets, terreno,
               y los puertos (mandar un paquete, pedir un sonido, un mensaje en pantalla)
    Ui/        el kit: widgets, ventanas viejas, el administrador de la UI nueva, paneles
    Actors/ Monsters/ Hero/ Combat/ Pets/ Items/ Npc/ Social/ Chat/ Guild/ Events/
    CastleSiege/ WorldObjects/ Effects/ Hud/      los módulos del juego (rebanadas verticales)
    Session/   la conexión con los servidores, el keepalive, la tabla de opcodes y el ruteo
    Scenes/    los guiones del frame: Login, Selección, Carga, Juego; SceneManager
  app/      main.cpp y App/: config, línea de comandos, bucle, la raíz de composición
```

- **El sentido de las dependencias** es `app → game → engine`, y adentro de cada raíz se usa solo
  lo que está más arriba en el listado: en `engine/`, `Core` y `Math` abajo de todo y `Models`
  arriba (`Render` le presta `TextureSlot` y `SpriteVertex`); en `game/`, `Protocol` abajo,
  después `Kernel`, `Ui` y los módulos, y arriba `Session` (su tabla de opcodes nombra los
  handlers de los módulos) y `Scenes`. Un módulo manda paquetes por el puerto de `Kernel`, que
  `Session` implementa: los handlers no conocen la sesión. Un módulo del juego no incluye las tripas
  de otro: incluye su header público, y los ciclos están prohibidos (`check_layers.py` en CI).
- **Cuánto falta para eso, medido con el mapa de destino:** el motor tiene 8 includes hacia arriba
  (`Render` → `Scenes` y `Actors`); en total suben de capa 336 includes en 54 pares de módulos
  (193 archivos), y entre los módulos del juego hay 1.395 includes en 145 pares, 49 de ellos
  mutuos.
- **Se hace cumplir con el compilador, no con la buena voluntad.** Cada raíz es su propia raíz de
  include: `engine/` no tiene `game/` en su ruta, así que un `#include "Scenes/…"` desde el motor no
  compila. Las carpetas que solo cambian de raíz no cambian sus includes (`"Core/Expected.h"`
  sigue igual, porque cambia la raíz y no la ruta relativa); cuando un archivo cambia de carpeta
  (`Scenes/X.h` → `Guild/X.h`), la mudanza reescribe sus includes y ninguna otra línea.
- **Targets:** uno por carpeta del motor (`MuRetCore`, `MuRetMath`, `MuRetInput`, `MuRetAudio`,
  `MuRetNetwork`, `MuRetData`, `MuRetPlatform`, `MuRetRender`, `MuRetModels`) y uno por módulo del
  juego, con `target_link_libraries` en el sentido de las capas. Los headers van en
  `FILE_SET HEADERS` con `VERIFY_INTERFACE_HEADER_SETS` (CMake 3.24; el repo pide 3.25): cada
  header compila solo, verificado en CI. `/bigobj` queda donde haga falta, no en todo.
- **Las banderas de fidelidad en un solo lugar:** `-ffp-contract=off` hoy está en `ClientLib`; con
  varios targets pasa a una función `muret_set_fidelity_flags(target)` que se aplica a todos los que
  hacen cuentas del original, y el job `math-release` sigue cubriéndolos.
- **Namespaces:** `Client::<Carpeta>` como hoy (regla 7); los módulos del juego pasan de
  `Client::Scenes` a `Client::<Módulo>` (`Client::Guild`) en su PR de diseño, no en la mudanza.

### El motor aparte: sí, en su raíz y con sus targets

Emanuel preguntó si conviene desacoplar el engine. **Sí, y es lo más barato del plan:**

- Ya está casi limpio: fuera de unos 40 includes al revés (`Render` y `Platform` hacia
  `App/WindowState.h`, `Render` hacia `Scenes/`, `Data` hacia `World/OriginalFtol.h`), `Core`,
  `Platform`, `Input`, `Audio`, `Network`, `Data` y `Render` no ven el juego. SDL llega a 24
  unidades (solo `Platform` y `main.cpp`) y FreeType a una.
- Con su raíz propia, la frontera la vigila el compilador; con sus targets, un cambio en el juego
  no recompila el motor y los tests del motor no enlazan 164 mil líneas que no usan.
- Deja la puerta abierta a cambiar solo el backend gráfico si algún día hace falta (SDL3 GPU o
  bgfx, `client-engine-migration`): todo GL ya está en `Render/Gl`.
- **Qué no es:** no es un motor genérico ni reutilizable fuera de MU. `Data/` lee los formatos de MU
  y `Math/` copia la matemática x87 del original; "motor" quiere decir "lo que no conoce el estado
  del juego".
- **Una mejora de paso:** `Render/Models` es simulación (esqueleto, pose, tela), no dibujo: pasa a
  `engine/Models/`, y su matemática x87 (la que también usan `GameLogic`, `World` y `Data`) a
  `engine/Math/`, junto con `Ftol` y `CrtRandom`. Hoy `Render` y `Render/Models` se incluyen en los
  dos sentidos (5 y 6 includes); con la matemática abajo queda un solo sentido.

### Anatomía de un módulo del juego

Cada módulo es una rebanada vertical (lo que hoy está repartido entre `Scenes/`, `UI/` y `World/`):

```text
game/Guild/
  Guild.h            el header público: lo que otros módulos pueden usar
  GuildState.h       su parte de la .bss del original (los globales que le tocan)
  In/                un handler por paquete que llega:  GuildList.cpp, GuildJoinResult.cpp…
  Out/               una acción por intención del jugador, que manda su paquete
  GuildWindow.cpp    la ventana y su dibujo (si la tiene)
  GuildMarks.cpp     la lógica propia
```

- **Un handler** (`In/`) es una función libre de 40 a 80 líneas: recibe el paquete ya leído con su
  layout y las partes del estado que toca, **declaradas en la firma**:
  `void OnGuildList(const Protocol::Guild::ListView& packet, GuildState& guild, Kernel::Messages& messages);`
  No conoce la sesión ni los sockets. Lo que necesita de afuera (sonido, envío, mensajes) entra por
  unos pocos puertos estables, no por bolsas de `std::function`.
- **Una acción** (`Out/`) es una intención ("pedir la lista del gremio") que valida contra el estado
  y manda su paquete con `Protocol::Write`.
- **El estado** de cada módulo es un struct simple con los campos del original que le tocan. El
  `GameContext` de `Kernel/` es su dueño (la raíz de composición) y presta a cada uno lo suyo.
- **Tamaños de referencia** (para revisar, no para cortar a ciegas): un handler hasta ~80 líneas,
  un archivo hasta ~400, una función hasta ~60. La excepción es la transcripción de un `switch` del
  original (`0x518cf0`, `0x530860`, `0x5208a0`), que se marca y puede quedar larga, partida en una
  función por `case` cuando el `case` es largo.

### El despacho: una tabla, un handler por paquete

```cpp
// game/Session/GameServerOpcodes.cpp (forma ilustrativa: salvo el F3 06, los valores son de ejemplo)
inline constexpr OpcodeTable GameServerOpcodes{
    Route<&Player::OnLevelUpPointResult>{C1, 0xF3, 0x06, 0x4d3200},
    Route<&Guild::OnGuildList>{C2, 0xNN, NoSubcode, 0x4dXXXX},
    SinPortar{C1, 0xNN, 0xNN, 0x4dXXXX},   // un case del original que todavía no tiene handler
    // …
};
```

- **Una fila por `case` del dispatcher original** (`0x4e2b10` y sus sub-dispatchers), con su
  dirección. Lo que falta portar es una fila `SinPortar{va}`, y un test compara la tabla con la
  lista de `case` del research: un paquete que falta es una fila, no un silencio. Es rastreo, en
  el código.
- `Route<&Fn>` deduce de la firma del handler qué layout leer y qué partes del `GameContext` pasarle,
  en tiempo de compilación y sin costo (sin reflexión, sin registro en el arranque). Las filas que
  el original copia al anillo de `0x77f27b4` antes del handler (`0x0B`, `0x12`, `0x1C`, `0x24`,
  `0x2A`) lo dicen en una columna.
- **El orden no cambia:** hoy el handler corre en línea dentro del despacho (`ApplyWorld` es
  síncrono) y sigue así. Se borran `App/WorldEvent.h` (el `variant` de 124 tipos),
  `ISessionEventSink`, `SessionEventApplier` y los 88 `On*` de `ServerSession`, que queda como
  `Session/Connection`: el keepalive, el serial, las claves y el flujo del login.

### El estado: de la `.bss` en dos agregados a partes por módulo

- **El registro de globales.** `trazas.tsv` ya tiene cada cita de una dirección de datos (desde
  `0x6b8000`) con su archivo y su símbolo. El generador de la referencia arma con eso
  `globales.tsv`: dirección del original → campo de MuRet → módulo. Es el "UpdateFields" de
  TrinityCore para nuestro caso: dónde vive en MuRet cada global del original, y qué módulo es su
  dueño. Guía el corte de `WorldRuntime`.
- **El corte.** De los 94 campos de `WorldRuntime`, 17 los usa un solo módulo (se mueven primero).
  Los que usan muchos (`hero` en 22 módulos, el pedido de sonido en 21, `ownRecordDepth` en 20, el
  mapa en 19) forman un núcleo chico en `Kernel/`. El arbitraje del mouse (`movement.pendingClick`,
  125 usos; `ui.mouseOnUi`; `ui.field`) es su propio estado. `WorldState` se parte en
  `ActorPool`, `PlayerState`, `MapState`, el scratch del frame y la fauna, y queda como la raíz que
  los junta. `world.Hero()` reemplaza los 210 `Actors()[…hero]`.
- **Lo contiguo no se parte.** Un struct que copia memoria contigua del original porque un
  desborde o una copia la cruza (el renglón de `TooltipLines`, Q-C591; los dos campos de
  `CharacterBalloons`, Q-C1503; los bloques que un `REP STOSD` o una función de reset vacían
  juntos) queda entero, marcado con `// Contiguo en el original: …` y su fila de 07.

### Las escenas como guiones del frame

`GameScene` deja de hacer nueve trabajos: queda como **la lista ordenada** de pasos y de dibujos
del frame del juego, cada uno con su dirección (`0x5208a0` → efectos, …), que llama a los módulos.
El orden de las llamadas del original vive en un solo lugar (el guion), y el trabajo, en los
módulos. Lo mismo para el login y la selección, y `main.cpp` (1.010 líneas) pasa a `app/App` como
raíz de composición.

### Direcciones, offsets y citas

- **Las citas en comentarios se quedan** (`// 0x4d3200: …`): son 55.800, el rastreo las lee, y una
  constante por dirección no agrega nada que el `funciones.tsv` no tenga.
- **Los layouts que importan llevan offsets en el tipo:** paquetes (`Field<T, Offset, Endian>`) y
  formatos de `Data\` (`static_assert(offsetof(...))` y `sizeof`). Hoy hay 9 `static_assert` de
  layout en todo `src`.
- **El estado en memoria no copia el layout del original**, así que ahí un `static_assert` no
  prueba nada: el nombre por dirección (`dword723bd8`) pasa a un nombre con sentido y la dirección
  queda en el comentario y en `globales.tsv`.

## Lo que no se negocia: la fidelidad

1. **El golden de comportamiento.** Antes de mover nada, un arnés corre escenas guionadas sin
   ventana (login → selección → Lorencia; caminar y pelear con skills; inventario, tienda y chat; la
   entrada a un evento) con reloj falso, paquetes grabados y entrada guionada, y guarda por frame un
   hash del dibujo, los paquetes mandados, la cantidad de `rand()` y un resumen del estado. Cada PR
   del refactor lo deja idéntico byte a byte.
2. **Las mudanzas no tocan lógica.** Las genera un script desde `mapa-modulos.tsv`, y
   `check_pure_move.py` verifica que las líneas de cada archivo movido sean las mismas (salvo
   `#include`, CMake y docs). Si `main` cambia, se vuelve a correr el script en vez de resolver
   conflictos a mano.
3. **El orden vive en dos lugares:** los guiones de las escenas y la tabla de opcodes. Nada de
   observadores sin orden: un reparto es una lista ordenada que copia al original.
4. **No se adopta "el dibujo no cambia el estado"** (ADR 0003 de MuMain): donde el original cambia
   estado al dibujar, la función recibe esa parte mutable y lo dice.
5. **Los tests y goldens actuales siguen verdes** en Linux y Windows, Debug y `math-release`.
6. **Lo contiguo queda contiguo** (ver arriba), y las 22 funciones portadas dos veces se unifican
   solo si las dos copias dan lo mismo (si no, una de las dos es un bug de port: PR aparte).

## Lo que no se pierde: el rastreo

- **Las clasificaciones a mano** (`entradas/cortes-vivos.tsv`, 1.761 filas; `sin-cablear`,
  `sustitutos`, `chat-funciones` y los `V` que se alcanzan jugando) hoy se reconocen por
  `archivo` + `línea`, así que cualquier mudanza las deja huérfanas. Pasan a reconocerse por un
  **ancla estable** (las direcciones, el nombre del corte y el texto normalizado de la línea, y el
  símbolo), y el generador resuelve el `archivo:línea` actual. Las mudanzas quedan además en
  `entradas/mudanzas.tsv` (ruta vieja → nueva).
- **La ola de cada cita** sale de `git blame`. Un commit sin `CLn` hereda la ola del anterior, así
  que una mudanza atribuiría miles de citas a la ola equivocada. El blame pasa a correr con `-M -C`
  y `--ignore-revs-file .git-blame-ignore-revs`, los commits del refactor llevan `AR` y no heredan
  ola, y el squash de cada mudanza se anota en `.git-blame-ignore-revs`.
- **Cada pendiente tiene módulo:** la referencia suma la columna `modulo` a `funciones`, `cortes`,
  `huecos` y `vm`, y `consultar.py modulo game/Guild` da lo que falta de ese módulo (cortes vivos,
  huecos, pasos de VM, filas de 08, paquetes `SinPortar`). Un hueco toma el módulo de quienes lo
  llaman.
- **`modulos.tsv`** (en `Client/`) es la fuente única de los módulos: carpeta, capa, de qué puede
  depender. La leen `check_layers.py`, el CMake y la referencia.

## Riesgos

| riesgo | cómo se ataja |
| --- | --- |
| un movimiento cambia el orden de llamadas o los `rand()` | el golden de comportamiento; mudanzas sin lógica |
| las tablas de la referencia cuentan mal después de mover | el ancla estable, `mudanzas.tsv` y el blame con `-M -C`; se regenera antes y después de cada mudanza y se comparan los números |
| choques con los PRs del port | las mudanzas son scripts que se vuelven a correr; un módulo en mudanza no recibe port ese día |
| el golden de comportamiento no corre sin ventana | `GameScene` ya corre sin ventana con datos reales y `RecordingFrameTarget` (`tests/scenes/test_game_scene_complete.cpp`), y el login con la sesión (`test_login_scene_session.cpp`); si una escena no puede, el arnés arranca por las que sí |
| MSVC (C1128, C4242, iteradores) | cada hito pasa el CI de Windows; las reglas de `client-msvc-ci-gotchas` |
| el plan se come semanas de port | ver el reparto de carriles en el plan; el refactor nunca ocupa los 3 agentes |

## Lo que queda a quien lo construya

Los nombres finales de los módulos chicos (si `Npc` y `Items` se parten), el corte exacto de los
archivos de confianza baja del mapa (52 de 687 en `Scenes/`), la forma interna de `Route<>` y del
`GameContext`, y cuándo partir un `switch` del original en una función por `case`.
