# Plan de la ola CL7, segunda parte: Lorencia

> **Para quien ejecute:** SUB-SKILL REQUERIDA: superpowers:executing-plans (en línea, como la CL6 y el
> login) o superpowers:subagent-driven-development, tarea por tarea. Los pasos llevan checkbox (`- [ ]`).
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), § Plan por olas (CL7) y
> § Lorencia (CL7.R2). Escrito el 2026-11-05, después del barrido de Lorencia (cuatro lectores: la entrada
> y la escena, la cámara, el caminar, los mapas y los gates). La primera sub-ola va paso a paso; las demás
> traen su contrato, sus nombres y sus tests, y se detallan al arrancar con las interfaces que dejen las
> anteriores. Lo que se aprenda en una se escribe acá antes de la siguiente.
>
> **Quién escribe los pasos:** Claude, como en la CL6 y el login, hasta que Emanuel diga que participa.

**Objetivo:** que en MuRet se entre a Lorencia como en el original: el mundo del mapa 0 con su cámara, sus
2.870 objetos, su luz, su pasto y lo que vive en él; caminar con el clic, con el camino, el paquete y el paso
del original; y pasar por un gate a los otros mapas que trae el `Data\`. Es el "terminada cuando" de la CL7
en el roadmap.

**Arquitectura:** la `GameScene` (la escena 5) hace lo que `0x636290` (el paso) y `0x6371a0` (el dibujo), en
su orden. Reusa lo que dejó el login (`LoadedWorld`, `WorldObjects`, `DrawCharacter`, los efectos, las
partículas, `OriginalFrameCarry`), con las diferencias de la escena 5: la cámara que sigue al héroe
(`0x635ae0`), los objetos antes que los personajes, la luz dinámica del terreno y los casos del mapa 0. Lo que
es lógica sin GL va a `World/` y `GameLogic/` (la cámara, el camino, el paso entre celdas, los gates); lo del
cable, a `Protocol/`; la sesión lleva los estados nuevos. Cada cálculo del original con azar o x87 se compara
con vectores del código real emulado (`Client/docs/research/scripts/`), como en el login.

**Stack:** C++20, OpenGL 3.3 core (el `GlModelRenderer`), glm 1.0.3, GoogleTest. Ninguna biblioteca nueva.

**Spec:** el roadmap de arriba y el research que dejó el barrido: 06 § La escena del juego, § La cámara del
juego, § Lo propio de Lorencia, § Caminar y § Los cambios de mapa; 04 (el `F3 03` que vuelve, `F3 10`, `D3`,
`D7`, `1C`, `F3 04`, `F3 12`, `0F`); 05 (`Gate.bmd`, los bits del `.att`, los modelos con nombre de `Object1`);
los quirks nuevos del 07 y P-C28 a P-C31 del 08. Los scripts del barrido (los que emulan el código real, arman los vectores y comparan contra la traza y el
cable) están en `Client/docs/research/scripts/game/`, y las herramientas para leer el binario en
`scripts/tools/`. Los informes del barrido quedaron fuera de Git; lo que vale de ellos está en esos docs.

## Restricciones de todo el plan

- Las de `AGENTS.md` y `docs/CODING_RULES.md`: `clang-format-18`, `misc-include-cleaner` de `clang-tidy-18`
  limpio en los `.cpp` y los `.h`, el chequeo de conversiones de MSVC (`msvc-conv-check.py`),
  `MURET_WARNINGS_AS_ERRORS=ON` y la suite entera con GL y los golden.
- Una sub-ola, un PR contra `main` (o apilado sobre el anterior si todavía no entró, avisándolo arriba del
  cuerpo), con su CI seguido hasta verde. Revisión adversarial al final de cada sub-ola, antes del PR, y sus
  arreglos con un test que falló primero.
- Merge commits para traer `main`; force-push solo si Emanuel lo pide para ese PR.
- Lo que solo diría una corrida del original va al 08 (P-C28 a P-C31), con lo que se captura en la VM.
- Las constantes del binario van por sus bits donde el cálculo tiene que dar lo mismo que el original; los
  pasos se comparan con vectores del código real emulado, guardados en `Client/tests/vectors/` con su
  generador en `Client/docs/research/scripts/` y su fila en el README de la carpeta de vectores.
- Lo que el original corre por paso va por paso de lógica (25 por segundo); lo que corre por frame dibujado
  va con `Render::Models::OriginalFrameCarry` (uno por cada 46,875 ms en Decoupled, uno por dibujo en
  Faithful).
- El `rand()` es el de la CRT compartido (`ClothWorld::random`), en el orden del original; cada sub-ola que
  saque `rand()` lo compara con el estado del emulado.
- Ningún log ni test lleva un nombre de personaje ni de cuenta. De un cable se guardan solo los paquetes que
  el test lee y que no traen nombres: la respuesta del `F3 03`, el `F3 10`, `D3`, `D7`, `1C`, `F3 04`,
  `F3 12`, `0F` y `A3`. Nunca el chat, el `F3 03` que sale, las listas de personajes, el C2 `12`, el `02`, el
  `F3 01` ni el `F3 02`.

### Autoridad para continuar (pedido de Emanuel, 2026-11-05)

Emanuel autorizó continuar todas las CL7.x mientras está ausente y pidió fidelidad al binario,
**sin inferencias ni nuevas emulaciones**. Las tareas que proponían generar expectativas con
Unicorn se ejecutan ahora con el ASM contrastado y evidencia del cliente original. Los fixtures
emulados ya existentes conservan su procedencia como regresiones; no se presentan como
capturas nativas. Un contrato todavía inferido se verifica estáticamente de forma concluyente
o con una observación real antes de implementarlo como hecho. Después de las capturas de Lorencia,
Emanuel avisó que el servidor se cerró y el cliente dejó de responder. Se continúa con el ASM y la
evidencia guardada; las comprobaciones que necesitan juego activo permanecen pendientes.
Cada sub-ola mantiene las revisiones adversariales de Astra 6 Ultra y Claude Opus 5.5 xhigh,
además de los checks y CI. Esto no da por terminada ninguna funcionalidad pendiente.

## El entorno

Lo que necesita un agente para ejecutar el plan, y de dónde sale cada cosa: el detalle está en
[`Client/docs/research/scripts/README.md` § El entorno](../../../Client/docs/research/scripts/README.md#el-entorno).

- **El repo alcanza** para compilar, correr los tests y leer el research: el `Data\` del cliente está en
  `Client/assets/Data`.
- **El `main.exe` (`c32bbd5f`)** hace falta para armar los vectores (unicorn sobre el código real) y para
  leer el binario. Sale de `EmaNLJN/mu-server-repack-research-102i`, en
  `client-research/mainexe-vmxp-c32bbd5f/original/main.exe`, con sus funciones decompiladas al lado. Va en
  `MURET_MAIN_EXE`.
- **La traza de GL de Lorencia y el cable** hacen falta para los golden de la traza y los vectores del cable.
  Salen de `EmaNLJN/mu-online-reverse-lab` (la traza y el PCAP del PR #45 de ese repo; el `muproxy.log` en
  `analysis/mainexe/evidence/`), y van en `MURET_CLIENT_GL_TRACE_DIR` y `MURET_CLIENT_WIRE_DIR`. El
  `muproxy.log` lleva el login del jugador: no entra a MuRet.
- **Python** con `Client/docs/research/scripts/requirements.txt` (capstone, unicorn, pefile, numpy,
  Pillow). Se corre con `-I` y desde afuera de la carpeta de los scripts.
- **El desensamblado entero y el grafo de llamadas** se regeneran con `scripts/tools/regen_cache.py` en
  `MURET_RESEARCH_CACHE` (por defecto `.research-cache/`, fuera de Git). Con eso andan `tools/funcof.py`
  (la función de una dirección y quién la llama), `tools/reach.py` (si una función alcanza otra),
  `tools/disas.py`, `tools/rd.py` (las constantes) y `tools/jt.py` (las tablas de saltos).
- **Los scripts de cada sub-ola** están en `scripts/game/` por tema (`camera/`, `protection/`, `world0/`,
  `walk/`, `wire/`, `maps/`), cada carpeta con su README: qué dirección reproduce cada uno y qué sub-ola lo
  usa. Ninguno guarda salidas en Git.

## Decisiones de este plan

Las tomó Emanuel el 2026-11-05: **fiel al original**. Donde el original tiene un bug, MuRet lo reproduce y
el 07 lo marca como "se reproduce: comportamiento del original". Lo que solo sumaría fluidez a más FPS entra
si no choca con eso; si choca, queda para más adelante.

1. **El héroe y la cámara se dibujan en el estado del paso**, sin interpolar, en los dos modos, como el
   original. Interpolarlos en Decoupled daría fluidez a más de 25 FPS, pero choca con el original: el paso
   prueba el clic con el rayo del último dibujo (Q-C165), y un héroe o una cámara interpolados mueven ese
   rayo. Queda para más adelante. El cuerpo sigue interpolando su animación entre pasos (CL6.2), que no
   toca el rayo.
2. **La carga del mapa corre adentro del paso**, como en el original (`0x61a560` dentro del handler), con el
   frame retenido (`HoldsTheLastFrame`) mientras dura y mientras se espera la respuesta. Nada de hilos. No
   choca con los FPS: mientras carga no se dibuja, igual que en el original.
3. **Las variables protegidas gastan sus `rand()` como en el original.** El original guarda la celda del
   héroe, el contador `0x77f3b04`, el contador de frames, el registro propio y otros en una tabla cifrada
   (`0x57630c8`). Cada acceso que se cierra los vuelve a cifrar con `rand()`: 4 una variable chica
   (`0x412890`, Q-C125) y **3.848 el registro propio** (`0x7681eb4`, de `0xf08` bytes, en `0x40b4d0`). Gastan
   en cada paso y en cada frame también funciones que MuRet no porta: `0x57cfc0`, de las habilidades, cierra
   el registro al final de casi cada paso de `MoveHero` (3.848 `rand()` por paso); `0x508f00` gasta 8 por
   personaje vivo y por paso; `0x635ae0`, 8 por frame dibujado, también en el login y la selección. MuRet
   guarda los valores en claro, pero cada acceso que corre en el original gasta lo mismo en el mismo lugar,
   lo porte MuRet o no (CL7.P). Los vectores del emulado se arman con la tabla viva, para que los cuenten.
4. **Sin HUD hasta la CL8.** El mundo va en (0, 0, 640, 432) como en el original, con el ancho de 640 fijo
   hasta que la CL8 traiga las ventanas (`0x5bb8d0`: 450 con una, 260 con dos). Del 2D de la escena 5 va
   solo el cursor del juego (`0x5ac2d0`, en `0x6376d7` y `0x6376e1`), como en el login y la selección: sin
   él no se vería dónde se hace clic. Lo demás del 2D queda en negro.
5. **El estado de la cámara es global, como en el original:** la D, el zoom, el yaw, el nivel, el far y la
   altura del `0x40` viven entre escenas, y el login y la selección también pasan por `0x635ae0` (CL7.3 B).
   Así se reproduce P-C27 b, el zoom que quedó del juego y corre la cámara del login, y la altura de 1.201
   que queda después del primer `0x40` (Q-C150).
6. **Lo que el cliente le dice al servidor se copia aunque sea raro:** el camino de 15 celdas sale como 14
   (Q-C158), el gate se pide desde el cliente con su `Gate.bmd` aunque difiera del `gate.txt` (Q-C182), y un
   rebote del servidor se acepta.

## Review Focus

1. **Entrar otra vez y cambiar de mapa** (juego → selección → juego, o un gate de ida y vuelta): el mundo se
   vuelve a cargar, los objetos y los efectos salen de cero, la cámara arranca con el yaw −45 y el héroe va
   donde dice la respuesta nueva; con el mismo mapa, se borran los otros personajes y no se recarga nada.
   Test en cada sub-ola que lo toque.
2. **Un archivo que falta** (un modelo o una textura de `Object1`, `World6` sin `TerrainHeight`, un mapa sin
   carpeta como el 35): el original saltea algunos sin avisar (Q-C23) y se cierra con otros (Q-C180); MuRet
   saltea lo mismo, con un aviso en el log, y se cierra donde el original se cierra, con el aviso que nombra
   el archivo.
3. **FPS altos o un frame largo:** los objetos, las hojas, los pájaros, los peces, los fuegos y el caminar
   avanzan por paso a cualquier FPS; el zoom y la D de la cámara, por frame del original; un frame de 2 s no
   mueve al héroe más de lo que caminaría en 50 pasos, ni le saltea una celda del camino.
4. **El orden de los `rand()`:** todo lo vivo de Lorencia saca del mismo `rand()`. Uno de más o de menos corre
   todo lo que viene después, incluidas la tela y el cetro, que ya son bit a bit. Desde la CL7.P, cada
   sub-ola compara el estado del `rand()` con el emulado con la tabla protegida viva, después de cada paso y
   de cada dibujo.
5. **La resolución** (640 × 480, 1024 × 768 y una ventana ancha): el pase del mundo va en (0, 0, 640, 432)
   escalado y con el aspecto del viewport (1,4819 en 1024 × 768); el rango de bloques sale de la vista de la
   franja; los bloques y los objetos del borde del trapecio no aparecen y desaparecen al caminar.

## Orden y hitos

| Sub-ola | Qué | Hito |
| --- | --- | --- |
| CL7.3 A | la cámara del juego, bit a bit (`0x635ae0`) | |
| CL7.3 B | la cámara compartida: el login y la selección por `0x635ae0`, y la altura del `0x40` | altura inicial y recreaciones implementadas; revisión y CI finales en curso |
| CL7.P | las variables protegidas y sus `rand()` (`0x57630c8`), con el contador del login completo | |
| CL7.L1 | la entrada al mundo y el pase del juego: el terreno de Lorencia desde el héroe | se ve Lorencia |
| CL7.L2 | los modelos con nombre y los objetos de Lorencia, en el orden de `0x6371a0` | |
| CL7.L3 | el héroe parado: el equipo del `F3 10`, la acción de parado, la aparición | **Lorencia con el héroe parado** |
| CL7.L4 | la luz dinámica del terreno y el pasto | |
| CL7.L5 | lo vivo: las hojas, los pájaros, los peces, los fuegos, la fuente | Lorencia viva |
| CL7.4 A | el mapa de paredes vivo y el camino (el A* de `0x4ead70`) | |
| CL7.4 B | el paquete de caminar (`D3`) y los `D3`/`D7` que llegan | implementado y revisado; CI pendiente |
| CL7.4 C | el paso entre celdas (el spline, la velocidad, correr) | |
| CL7.4 D | el picking del terreno | |
| CL7.4 E | caminar en la escena, con el recorte de los personajes | **se camina por Lorencia** |
| CL7.5 A | cada mundo del `Data\` carga, con sus modelos y su error | |
| CL7.5 B | `Gate.bmd` y la detección del gate | |
| CL7.5 C | el protocolo del cambio de mapa (`1C`, `F3 04`, `F3 12`) | |
| CL7.5 D | el cambio de mapa en la escena | **por un gate, a los otros mapas** |
| CL7.5 E | los casos de los otros mapas, como datos | |
| CL7.6 | los efectos del mundo y de los personajes que dejó la CL6 | |
| Cierre | el barrido profundo de Lorencia | |

La CL7.3 A y las CL7.4 A a C son lógica sin escena y pueden adelantarse. La CL7.P va antes de la L1: la
celda del héroe y el contador de warp son variables protegidas. Las L1 a L5 van en orden.

---

## Sub-ola CL7.3 A: la cámara del juego

**Contrato:** 06 § La cámara del juego. `0x635ae0` corre una vez por frame dibujado, dentro del dibujo
(`0x6371a0` → `0x637241`), con el héroe del último paso. El original da la cámara de la traza de Lorencia
bit a bit con el héroe en (14.350; 12.050; 165). PR propio.

### Tarea 1: el `VectorIRotate` de `0x5fcc30`

**Archivos:**
- Modificar: `Client/src/Render/Models/OriginalRotation.h`, `OriginalRotation.cpp`
- Test: `Client/tests/render/test_original_rotation.cpp`

**Interfaces:**
- Produce: `[[nodiscard]] glm::vec3 OriginalVectorIRotate(const glm::vec3& v, const OriginalMatrix& m);`
  con la transpuesta de la parte de giro, en double y cada componente guardada en float, en el orden de
  sumas del binario: `x' = (m8·z + m4·y) + x·m0`, `y' = (m1·x + m9·z) + m5·y`, `z' = (m2·x + m10·z) + m6·y`.

- [x] **Paso 1:** test `OriginalRotationTest.TheInverseRotateSumsLikeTheBinary`: con
  `OriginalAngleMatrix({0, 0, -45})` y (0, −1.000, 0), sumado al héroe (14.350; 12.050) y guardado en float,
  da los bits `0x466b446d` y `0x46313b93` (los de la traza); y los casos de `world55/rotation.txt` con la
  transpuesta, contra una cuenta en double hecha a mano en el test.
- [x] **Paso 2:** verlo fallar (la función no existe).
- [x] **Paso 3:** escribir `OriginalVectorIRotate`.
- [x] **Paso 4:** verlo pasar, con `test_original_rotation` entero. Commit.

### Tarea 2: los vectores de `0x635ae0`

**Archivos:**
- Modificar: `Client/docs/research/scripts/game/camera/make_camera_vectors.py` (el borrador del barrido, que
  ya corre `0x635ae0` con `emu_camera.py`; su reimplementación en doubles es `camera_reference.py`)
- Crear: `Client/tests/vectors/game/camera.txt` y `Client/tests/vectors/game/README.md` (como el de
  `world55/`)

- [x] **Paso 1:** el generador corre `0x635ae0` real, frame por frame, con unos 3.000 frames en casos al
  azar (cinco semillas): el héroe, el byte del atributo de su celda (0, `0x01`, `0x40`, `0x80` y `0xc0`), el
  temblor, el reloj, el mapa (0, 2, 3, 5, 6 y `0x1e`), la escena (2, 4 y 5), "webzen" en el 6 (prendido y
  apagado), la D y el zoom de entrada, y los bordes del rectángulo del far (16.100, 19.000, 18.900 y 21.700,
  ±0,01). Cada frame lleva la entrada y la
  salida en hexadecimal: posición, ángulos, far, D, meta, zoom, nivel y la altura del `0x40`.
- [x] **Paso 2:** correrlo con `MURET_MAIN_EXE` y guardar `camera.txt`, con su README.
- [x] **Paso 3:** commit.

### Tarea 3: `World::GameCamera`

**Archivos:**
- Crear: `Client/src/World/GameCamera.h`, `GameCamera.cpp`
- Test: `Client/tests/world/test_game_camera.cpp`

**Interfaces:**
- Consume: `Render::Camera3D`, `World::CullingScene`, `OriginalAngleMatrix`, `OriginalVectorIRotate`.
- Produce:
  - `struct GameCamera`, lo que queda de un frame al otro: `float yaw = -45` (`0x7d36870`, que escribe el
    primer paso de la escena 5), `float distance = 1000` (`0x7d36ba4`), `float zoom = 0` (`0x7d3689c`),
    `int level = 0` (`0x7d3a0ec`), `float farPlane = 2000` (`0x6d5d44`, que se lee con el nivel del frame
    anterior), `float fixedHeight = 1200` (`0x6c9d5c`, 1.201 desde el primer `0x40`).
  - `struct GameCameraInput { glm::vec3 hero; std::uint8_t cellAttribute; std::uint8_t map; CullingScene scene; float shake; float worldTime; bool webzen; glm::vec3 effectTarget; };`
    con el `World::CullingScene` que ya tiene `TerrainCulling.h` (2, 4 y 5, las escenas del original). El
    atributo es el byte bajo de la celda lógica del héroe (`+0x41c`, `+0x420`, la que va una adelante,
    Q-C173); `worldTime` es el float de `0x5767b88`, en ms, el que lee el balanceo del mapa 5;
    `effectTarget` (0, 0, 0) es "sin desvío" (`0x724588`).
  - `[[nodiscard]] Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input);`: una
    vuelta de `0x635ae0`, en el orden del binario (06): fov y ángulos, el far por el rectángulo o el nivel,
    el corrimiento `OriginalVectorIRotate((0, −D, 0), AngleMatrix(0, 0, yaw))` con la D de antes, el
    objetivo, la posición en el plano y en z (`(D − 150) + z0`, con el `0x40`), el pitch (−48,5 + temblor en
    la 5), el zoom (±10 con el `0x80`, hacia el héroe con el giro fijo de −45), los finales del mapa 5, el 6
    con "webzen" y el `0x1e`, y la D suavizada por un tercio hacia la meta.
  - `[[nodiscard]] Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input);`: la
    misma cámara sin avanzar el estado, con la D, el zoom y el far del último `StepGameCamera`, para los
    dibujos de Decoupled sin un frame del original (`OriginalFrameCarry` en 0), con el héroe del último paso.
  - El atributo se lee de la celda del servidor del héroe, que pasa por la tabla protegida y gasta sus
    `rand()` en el dibujo: lo agrega la CL7.P, con la celda del héroe de la L1.
  - El recorrido de `0xBD 00` (`CCameraMove`) no va: el `.h` lo dice, con su dirección.

- [x] **Paso 1:** los tests:
  - `GameCameraTest.TheCameraOfLorenciaIsTheOneInTheTrace`: el héroe (14.350; 12.050; 165), el atributo
    `0x01`, el mapa 0 y la escena 5 dan la posición con los bits `0x466b446d`, `0x46313b93` y `0x447dc000`,
    los ángulos (−48,5; 0; −45), el far 2.000, el fov 35 y el near 20;
  - `GameCameraVectorsTest.EachFrameMatchesTheRealCode`: `camera.txt`, bit a bit en cada campo;
  - `TheZoomGoesTo210InTwentyOneFramesAndBack`, `TheBit0x40LeavesTheFixedHeightAt1201`,
    `TheFarIs3000InsideTheRectangleOnAnyMap` (los cuatro bordes adentro y ±0,01 afuera, en el mapa 0 y en
    el 3), `TheFarUsesTheLevelOfTheFrameBefore`, `TheDistanceSettlesAt0x447a0001` (desde 1.400),
    `TheShakeAddsToThePitch` y `GameCameraOfRepeatsTheLastStep`.
- [x] **Paso 2:** verlos fallar.
- [x] **Paso 3:** escribir `GameCamera`.
- [x] **Paso 4:** verlos pasar; los mutantes del barrido (el signo del zoom, el −150, el orden de las sumas
  de `0x5fcc30`, la D vieja contra la nueva) tienen que romper alguno. Commit.

### Tarea 4: la traza de Lorencia con la cámara calculada

**Archivos:**
- Modificar: `Client/tests/render/test_golden_pass_3d_trace.cpp`, `Client/tests/world/test_golden_terrain_trace.cpp`

- [x] **Paso 1:** `GoldenPass3DTrace.TheLorenciaWorldPassMatches` y los dos de `GoldenTerrainTrace` de
  Lorencia toman la cámara de `StepGameCamera` con el héroe de la traza, no la escrita a mano.
- [x] **Paso 2:** verlos pasar (y fallar con el yaw en −44).
- [x] **Paso 3:** docs: 06 § La cámara del juego (lo que ya es MuRet), los quirks de la cámara del 07 con
  su columna de MuRet, P-C29 con lo que sigue abierto. Commit, la suite entera, la revisión adversarial y el
  PR.

---

**Hecho (2026-11-05, resuelto por Codex).** `OriginalVectorIRotate`, `World::GameCamera`,
`StepGameCamera` y `GameCameraOf`; 3.000 frames del código real emulado en 60 casos, cinco semillas,
con todos los campos de cámara y estado comparados por bits. Las cámaras fijas, el mapa 6 con su
rama especial, el mapa 5, el desvío de efectos, los bordes del far y el estado previo están cubiertos.
`GameCameraOf` conserva solo la D usada antes del suavizado para los dibujos intermedios, y toma
el héroe del último paso lógico sin avanzar zoom, D ni far. La integración de las escenas es
CL7.3 B; los ocho `rand()` protegidos por frame son CL7.P.

Los golden del pase y de las celdas de Lorencia usan la cámara calculada desde el héroe de la traza.
Build GCC 15.2 con warnings como errores y sin PCH; `misc-include-cleaner` 18 en los ocho `.cpp`/`.h`
cambiados y `clang-format-18`, limpios. Suite sin GL: 2.165 casos registrados, cero fallos, 30
salteados porque no se configuraron volcados privados ni las fuentes originales; GL: 83/83 en
Xvfb y Mesa. Los tests de red y GL requirieron salir del sandbox para abrir sockets locales y el
display. `msvc-conv-check.py` no está versionado ni disponible en este entorno; la compilación de
MSVC con warnings como errores queda a cargo del job de Windows del PR.

Astra 6 Ultra encontró que las 400 entradas de giro no observaban una permutación de sumandos.
Se agregaron tres casos de cancelación: el mutante falla y la implementación pasa. También
fallan los mutantes del signo del zoom, de −150 y de usar D nueva, y el que avanza la cámara al
consultar antes del primer paso. La comparación con x87 nativo del balanceo del mapa 5 encontró
un ULP de diferencia con relojes largos: Unicorn coincide con MuRet; queda documentado como
límite del oráculo en 06 y P-C29 a, pendiente de captura del original. Los vectores incluyen
esos relojes. La revisión de Opus 5.5 con esfuerzo máximo no encontró errores de comportamiento; pidió un
oráculo independiente para el giro inverso y que el golden de cada celda observe su cámara. Se
agregaron 403 salidas reales de `0x5fcc30` y una comparación de giros/traslación con la traza; los
mutantes de suma y yaw −44 fallan. Se corrigieron los comentarios que exageraban su cobertura.
La supuesta falta de variación del nivel y del objetivo era un falso positivo: ambos estaban ya
en los 3.000 frames, que el paquete entregado a Opus no incluía. Astra contrastó esas conclusiones
y revisó los arreglos. El chequeo directo de ASM y pseudocódigo de `0x5fcc30` confirma índices,
agrupación y únicos guardados float; copiar literalmente el pseudocódigo reordenaría las sumas. El contraste de
`0x635ae0` cotejó 462 instrucciones, constantes y tablas con el PE: sin discrepancias lógicas en
el alcance incluido; la tabla de correspondencia queda en 06 § La cámara del juego. La CL7
completa sigue abierta: el cierre exige el barrido de todas sus funcionalidades en pasadas
complementarias, según el pedido de Emanuel registrado en `AGENTS.md`.

La auditoría adicional solicitada para el PR 138, con Opus 5.5 xhigh y Astra 6 Ultra, encontró
que los tres casos de cancelación no detectaban las dos asociaciones alternativas de cada
componente. Se conservan los 403 fixtures históricos y se agregan tres casos analíticos:
las seis alternativas mutantes fallan. También se quitó el bucle inverso que repetía la fórmula
de producción y se endureció la lectura de bits del fixture, sin restringir las matrices 3 × 4.
El contraste de los 130 bytes de `0x44dc60` descubrió que prueba C3 sin C2: se corrigió la
selección del objetivo ante NaN con excepciones enmascaradas y se cubrieron Z aislada y −0.
Las constantes nombradas conservan los tipos, bits, agrupación y límites de redondeo. La fuente
de Unicorn 2.1.4 confirma que su FSIN/FCOS usa double y `sin`/`cos` de C; esos fixtures siguen
siendo regresiones históricas, sin nueva ejecución de emulación ni afirmación de equivalencia
x87 nativa universal. La evidencia y los límites quedan en 06 § La cámara del juego.

Las revisiones finales de Opus 5.5 xhigh y Astra 6 Ultra no dejaron P1/P2 abiertos. El control
positivo del detector de salida descubrió que fortify podía eludir `--wrap=printf`; el harness
ahora lo comprueba y cubre los errores previos a la suspensión. El informe admite una cadencia
indeterminada sin dejar el Markdown incompleto. Son 44 tests de herramientas, 26 numéricos
focales y la suite completa de 2.255 casos registrados, cero fallos y 30 omisiones opcionales.
Los tres lectores migrados a C++20 terminaron con código 0 en XP: cámara coincidente, serie
de 2.250 muestras completa y un único incremento de suspensión retirado. La recuperación
conserva la propiedad del handle cuando CloseHandle falla; descartamos la propuesta de
abandonarlo, porque un fallo no acredita que se haya cerrado. La CL7 completa sigue abierta.

## Sub-ola CL7.3 B: la cámara compartida

**Contrato:** 06 § La cámara del juego (las escenas 2 y 4 también pasan por `0x635ae0`, el zoom que queda de
una escena a otra, la altura del `0x40`), P-C27 b y Q-C150, y la decisión 5. PR propio. Se detalla al
arrancar; lo que se sabe:

- **Qué:** el `World::GameCamera` vive en el contexto (`SceneContext::camera`, como el `ClothWorld`), lo
  arma `main.cpp` una vez con los valores del arranque (la D de 1.000 del inicializador estático `0x635ad0`,
  el zoom en 0), y nadie lo vuelve a armar. La escena 5 escribe el yaw −45 en su primer paso (`0x636838`).
  El login (`0x6350b0`) y la selección (`0x634680`) sacan su cámara de `StepGameCamera` con las escenas 2 y
  4, en vez de `LoginCamera()` y `SelectionCamera()`. La altura del `0x40` (1.200, y 1.201 desde el primer
  `0x40` de la cámara) sale de los `constexpr` de `TerrainHeight.h` y `TerrainCells.h` y pasa al estado
  compartido.
- **Tests:** con el zoom en 0, las cámaras del login y de la selección dan los bits de hoy: los vectores de
  las escenas 2 y 4 de `camera.txt`, y los golden del login y de la selección siguen en verde (hasta la CL7.P,
  que suma los 8 `rand()` protegidos de cada frame de `0x635ae0` y cambia el `rand()` del login). Con el zoom de
  210 que dejó el juego, la del login empieza en (12.636,6; 12.819,4) y vuelve de a 10 por frame (P-C27 b,
  emulado). `TerrainHeightAt` con una celda `0x40` da 1.200 antes del primer `0x40` de la cámara y 1.201
  después. Review Focus 1: del juego al login y de vuelta.

**Estado de la implementación:** integrada la construcción inicial que mantenía B parcial.
La cámara y sus entradas viven una vez en main; el carry es local a cada entrada según ADR0005.
`SelectionState` conserva cinco historiales y CPU entre escenas. Login fija yaw0 y escribe HeroXYZ
al dibujar; su avatar visible es slot2, distinto de Hero0. El primer paso de juego sigue fijando
−45, sin adelantar L1. La altura fija compartida llega a terreno, brasas, nacimiento del cuervo y sombra.

- [x] Constructor contrastado contra ASM/PE: Height(50,50), atributo nuevo, word318 anterior,
  offset30/90 antes del único guardado float y reset posterior. Sin sumar0 en la otra rama.
- [x] F3/00 y altas F3/01 aceptadas publican eventos propios y ordenados, también idénticos.
  Main los aplica tras Poll y antes de Step/Request/Start. Bajas y limpiezas conservan historial;
  login sólo reemplaza el de slot2. Una recepción anterior al cierre conserva sus efectos.
- [x] Primera lista construye con55; el siguiente Step habilitado carga54, también con lista vacía.
  CPU se transfiere al salir del login; GPU se libera una sola vez. Faltar tablas es un error explícito.
- [x] Cámara antes de Z163 en los cinco slots; una recreación posterior reemplaza esa Z sin
  esperar otro dibujo. El primer LoginStep consume la Z nueva para sus brasas.
- [x] Visuales se reconstruyen por revisión, sin espejo XY repetido en cada Step; Render refleja
  altas/bajas recibidas sin Step. Un slot repetido conserva todas las construcciones en el evento
  y sólo su última entrada en la vista UI; dibujo por índice de slot.
- [x] Tests de unidad, transferencia CPU y escenas observan esos efectos, incluyendo SceneManager
  real, Poll minimizado, destrucción a login, recepciones idénticas, lista vacía y accepted+close.
  Las regresiones previas de cámara, terreno, cuervo y brasas se conservan.
- [x] Contraste estático cruzado Astra6Ultra de constructor, fases y ownership/historial documentado
  en 06 § Los personajes de la lista. Sin nueva ejecución ni emulación del original. Los fixtures
  históricos permanecen identificados como tales y no acreditan equivalencia x87 universal.
- [ ] Revisión final del código integrado por Astra6Ultra y Opus5.5xhigh, correcciones y verificación
  final de suite completa, formato e includes.
- [ ] CI Linux/Windows del HEAD final del PR141 y publicación sin borrador.

El contador/RNG protegido de CL7.P, la celda/entrada al mundo de L1, terreno y efectos completos
propios de selección y las otras sub-olas siguen su propio alcance; no son partes diferidas de
la integración de cámara de B.

## Sub-ola CL7.P: las variables protegidas

**Contrato:** 06 § La tabla de protección, 07 Q-C125 y Q-C183, P-C22 a y la decisión 3. PR propio. Empieza con un lector del binario, como el
barrido, y se detalla con lo que lea. Lo que se sabe:

- **Qué falta leer:**
  - las funciones de la tabla `0x57630c8`: `0x40ad90`, `0x40b240`, `0x40b2f0`, `0x40b440`, `0x40b4d0`,
    `0x40b570`, `0x40b800`, `0x412810` y `0x412890`, y el hash de `0x633f96..0x63417a`;
  - qué variables guarda: el contador de frames `0x7d3a0f0`, el contador de warp `0x77f3b04`, la celda
    lógica de cada personaje (`+0x41c` y `+0x420`), el registro propio `0x7681eb4` (`0xf08` bytes), y las que
    salgan;
  - cuántos `rand()` gasta cada clase de acceso: 4 al cerrar una variable chica (`0x412890`), 3.848 al
    cerrar el registro propio (`0x40b4d0`, uno por byte), y lo que den abrir, leer, escribir y rearmar;
  - **cada acceso que corre** en el paso y en el dibujo de las escenas 2, 4 y 5, **lo porte MuRet o no**:
    como mínimo `0x57cfc0` (las habilidades, al final de casi cada paso de `MoveHero`: 3.848 por paso, y
    más por los otros caminos de `MoveHero` y los cierres en línea),
    `0x508f00` (8 por personaje vivo y por paso), `0x635ae0` (8 por frame dibujado), `0x4c3940` (tres cierres
    del registro al entrar: 11.544), el `F3 10` y el `D7` al héroe. Donde MuRet no tiene la función, deja el
    gasto en el mismo lugar del paso o del dibujo, con un nombre que diga de qué función es.
- **Qué se escribe:** `GameLogic::ProtectedValue<T>`. Guarda el valor en claro y, en cada acceso que el
  original cierra, saca del `CrtRandom` que le pasan los `rand()` de ese acceso. Entra primero en el contador
  de frames del login, donde completa Q-C125 con los 4 del +1 después de la escena y los del rearmado cada
  313 pasos. Si el orden de P-C22 a sale de lo mismo, también se arregla. Después lo usan la celda del héroe
  y el contador de warp de la L1.
- **Tests:**
  - los vectores del paso real del login con la tabla viva (sobre `game/protection/protection_rand.py` y
    `camera_rand.py`, que ya cuentan los 3.848 y los 4 de cada cierre): el estado del `rand()` después de cada uno de
    1.000 pasos (con tres rearmados), igual en MuRet;
  - un test por clase de acceso, contra el emulado.

## Sub-ola CL7.L1: la entrada al mundo y el pase del juego

**Contrato:** 06 § La escena del juego (de la selección al primer frame, la respuesta `0x4c3940`, el pase de
`0x6371a0`). PR propio. Se detalla al arrancar; lo que se sabe:

- **El protocolo:** `Protocol::Character::ReadCharacterSelectResult` en `CharacterSelect.h`, el C3 rearmado
  de `0x34` bytes con `TooShort`: la celda (`[4]`, `[5]`), el mapa (`[6]`), la dirección (`[7]`) y los stats
  de 04 § La entrada al mundo; lo que cae fuera del paquete (`[0x34..0x37]`) va en 0 con un aviso
  (Q-C144). El tope de `EnteringWorld` que dejó la CL6 se cierra con la respuesta; las velocidades del
  keepalive no salen de ella (`0x4c3940` no escribe `+0x4c` ni `+0x58`, P-C28 i) y quedan las de hoy.
- **La sesión:** `SessionState::InWorld` (el `0x3d`) y `ServerSession::JoinedWorld()`.
- **Las variables protegidas:** la celda del servidor del héroe y el contador `0x77f3b04` (9.999.999 hasta la
  respuesta, 30 con ella) son `ProtectedValue` de la CL7.P; sus accesos gastan los `rand()` del original.
- **El cursor:** la `GameScene` dibuja el cursor del juego al final, como el login (la decisión 4).
- **La escena:** `GameScene` retiene el frame desde el `F3 03` hasta la respuesta (en pantalla queda el
  mosaico de LSBg, como cuando `0x6371a0` devuelve 0 con el contador en 9.999.999). Con la respuesta carga el
  mundo del mapa (`LoadedWorld` con `WorldFolder`), arma al héroe como `0x511290` (`(celda + 0,5) · 100`, la
  altura de `TerrainHeightAt`, el ángulo `(dir − 1) · 45` y la zona segura del `.att & 1`), pone el yaw en
  −45 (el primer paso, `0x636838`) y dibuja el pase del juego: la franja (0, 0, 640, 48) para el rango, con
  `TerrainRangeOf(vista, cámara, 640 · 0,0015625)`; el trapecio `TerrainPolygonOf(Game, mapa, objetivo,
  yaw, 640)`; y el mundo (0, 0, 640, 432) con el terreno. La cámara, de CL7.3 A, con `OriginalFrameCarry`.
- **Tests:** el vector del cable (el C3 `F3 03` descifrado con `game/wire/decode.py`, sin nombres: la celda (133, 127), el mapa 0 y la
  dirección 0 dan (13.350; 12.750; altura) y −45°); `ServerSessionTest`: `EnteringWorld` pasa a `InWorld`;
  `GameSceneTest.HoldsTheFrameUntilTheWorldArrives` y `LoadsTheMapOfTheReply`; el golden
  `GoldenTerrainTrace.TheGameSceneDrawsTheCellsOfLorencia`: la `GameScene` con el héroe de la traza en
  (143, 120) dibuja las 329 celdas de la traza en su orden; Review Focus 1 (entrar dos veces).

## Sub-ola CL7.L2: los modelos y los objetos de Lorencia

**Contrato:** 06 § Lo propio de Lorencia (la creación, el paso, `0x602ed0` y `0x6002e0` del mapa 0) y 05 (la
tabla con nombre de `Object1`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Los modelos:** `WorldModels` con una tabla por mapa: la del 0 (los 112 con nombre de `0x619a10..0x619edf`, tipo ←
  nombre, como dato), los especiales desde `0x69` (`0xad` `Bird01`, `0xb2` `Fish01`), `Object8\wt00..31` en
  `0x41..0x60`, `light01` en `0x68` (que después pisa `rain03`, Q-C136), `drop01` en `0x607`, y las texturas de los 160 de
  `Object1\`. El lazo genérico `Object<k+1>.bmd` sigue para los otros mapas.
- **Los objetos:** los casos del mapa 0 en `CreateWorldObjects` (cajas, velocidades, blendMesh, el
  `PoseBox01` oculto, la lista de poses), `StepWorldObject` (los techos `0x7d`/`0x7e` con el tile del héroe,
  los fuegos de `0x52f360` con sus tres `rand()` de salto y el de la luz, las lámparas y las velas, el
  titileo de `0x75`/`0x7a`, la V que corre de `0x76`/`0x77`) y el dibujo (`0x69` con cuatro `RenderMesh`).
  Los fuegos y las luces sacan sus `rand()` acá, en el orden del original, y dejan sus pedidos (luz al
  terreno, partícula) en una lista que usan L4 y L5.
- **El pase:** el orden de `0x6371a0`: terreno → objetos → efectos del suelo → pájaros → personajes → ítems
  del piso (`0x608a00`) → peces → caballo → hojas → joints (`0x53c6a0`) → efectos con modelo → cintas
  (`0x52e590`) → sprites → `0x549440` → partículas → números de daño (`0x548eb0`). Los que todavía no tienen
  contenido (los ítems, los joints, las cintas y los números) quedan en su lugar, vacíos, para sus olas.
- **Tests:** el golden de los slots contra el volcado (`lor_slots.bin`, que lee `game/world0/slots.py`: `0x41c..0x47e` son las 99 texturas
  de `Object1\` en su orden, y `bird.tga` y `fish.tga` en `0x69` y `0x6a`); los vectores del código real
  (`make_world0_vectors.py`, sobre `game/world0/run_objs0.py` y `lor_objs.py`, como `make_world55_vectors.py`): los 2.870 registros al crearse, en tres pasos y
  en el dibujo, con el estado del `rand()` y los pedidos en orden; la traza: los 120 bloques de objetos de
  Lorencia contra el lote (los vértices con tolerancia, el slot, la mezcla y el orden), sin los personajes ni
  los pájaros; `TheGamePassGoesInTheOrderOf0x6371a0`; Review Focus 2 (un modelo que falta).

## Sub-ola CL7.L3: el héroe parado

**Contrato:** 06 § La escena del juego (el héroe: `0x511290`, el `F3 10`, `0x4f2250`, la aparición, los dos
`0x61f`). PR propio. Se detalla al arrancar; lo que se sabe:

- **El equipo:** `Protocol::Character::ReadInventoryList` del `F3 10` (C4, entradas de 8 bytes: el lugar y
  7 del ítem), los 12 lugares del equipo a la apariencia, como `0x5bc740` → `0x512a30`. La ventana del
  inventario es de la CL8.4.
- **La acción de parado** (`0x4f2250`, `SetPlayerStop`, P-C14 c): la escribe esta sub-ola entera, con
  `Run = 0`, y la CL7.4 C la usa; por el arma, la zona segura, la mascota, las alas y el
  Fenrir; las ramas que mandan un `C1 .. 18` lo mandan.
- **La aparición:** el alpha de 0 a 1 de a 0,05 por paso; los dos efectos de llegada `0x61f` (20 pasos, el
  calco `0x61e` en el suelo de `0x52dc20` con los dos giros de `0x53d2c0`); el cuervo (`0x420d60`) con el
  Dark Lord con `0x1c17`; la luz de la escena 5 (la del terreno en su lugar, sin el 0,3 del login); el
  `rand() % 400` del lugar en la tabla, que gasta un `rand()`.
- **Tests:** el `F3 10` del cable (cinco ítems, uno en la mano derecha); `0x4f2250` emulado sobre una matriz
  de arma, zona segura y mascota; el `0x61f` emulado en sus 20 pasos; la luz del héroe en (143,5; 120,5)
  contra la traza; el héroe opaco en el paso 20.

## Sub-ola CL7.L4: la luz dinámica y el pasto

**Contrato:** 06 § Lo propio de Lorencia (`0x5fbf60`, `0x5f9780`, `0x5f99e0`, el pase del pasto de
`0x5fa830`). PR propio. Se detalla al arrancar; lo que se sabe:

- **La luz:** el buffer dinámico (`0x7b4ccc4`), que `0x5fbf60` rearma en cada paso desde la luz sombreada en
  la ventana del rango; `0x5f9780` (el cuadrado ±r, la caída `(r − d) / r` y el piso en 0); `0x5f99e0`, que
  lo leen los objetos y el héroe; las fuentes de L2 (el fuego con r 4, las lámparas y las velas con r 3).
- **El pasto:** el segundo `0x5fc190` en el modo 2: un quad por celda con las cuatro esquinas en alfa 0, de
  alto el doble de la textura, la punta corrida −50 en x y la ola en y, la u con el corrimiento de la fila,
  los 65.536 `(rand() % 4) · 0,25` de `0x5f8300` al cargar el mapa (que gastan sus `rand()` también en el
  login).
- **Tests:** vectores emulados de `0x5f9780` y `0x52f360` con semilla; la traza (los vértices más brillantes
  que el OZJ, con `game/world0/brighter.py`): los colores del terreno con
  el `FireLight01` #1933 (los vértices de su cuadrado suben; con tolerancia, porque el salto y la luz salen
  del `rand()`), y los 4 de la fila 136 anotados como pendientes; los 25 quads del pasto con la geometría
  exacta y la u en uno de {0; 0,25; 0,5; 0,75}; la cuenta de los 65.536 `rand()` contra el emulado.

## Sub-ola CL7.L5: lo vivo de Lorencia

**Contrato:** 06 § Lo propio de Lorencia (las hojas, los pájaros, los peces, las partículas de los objetos,
la fuente, el `0F`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** las 80 hojas del mapa 0 (`0x52fa60`, `0x5301a0` y `0x5303a0`, que no corren con el héroe en el
  tile 4) y el `0F` como entrada de la lluvia (en Lorencia solo el sonido); los pájaros (`0x446120` y
  `0x446b60`, con sus sombras de alfa 0,2; el tope, 5 o 6, se cierra con el emulado) y los peces (`0x447030`
  y `0x446f00`, hasta 3, en las celdas de agua); las partículas `0x5d9` (el fuego, subtipos 0 a 3) y `0x5f2`
  con el subtipo 2; la fuente (`0x602ed0` del `0x69`); los dos `0x5ac` del `MerchantAnimal01`.
- **Tests:** vectores emulados por paso con semilla (como `make_leaf_vectors.py` y
  `make_particle_vectors.py`); la traza por cuenta, textura, mezcla y orden (los 80 quads `0x64`, los 5
  pájaros con sus 5 sombras y las partículas aditivas después de las hojas); Review Focus 3 (a 144 FPS, lo
  mismo por segundo).

---

## Sub-ola CL7.4 A: el mapa de paredes vivo y el camino

**Contrato:** 06 § Caminar (el `TerrainWall` vivo de `0x508f00`, `PathFinding2` de `0x4eac00` y el A* de
`0x4ead70`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** `World::TerrainWalls` (el WORD vivo por celda: se borra el `0x02` de las 65.536 y se marca la
  celda lógica de cada personaje vivo, en cada paso; la celda con máscara de `0x5f82a0`);
  `GameLogic::PathFinder` con la lista abierta como un árbol propio que inserta y saca como `0x4eb770` y
  `0x4c0dd0` (los iguales a la derecha, se saca el de más a la izquierda), los ocho vecinos en el orden de
  `0x6d5d80`, la prioridad h + el costo del padre, la h (que da 4 en la diagonal a la meta, lo mismo que en
  la de al lado: Q-C159), los topes de 500 y 50,
  y la salida con el inicio en x o y igual a 0; `PathFinding2` con sus dos intentos (en la zona segura el
  segundo pasa por encima de los personajes) y el corte en 15.
- **Tests:** `PathFinderTest.MatchesTheEmulatedCode` (`make_path_vectors.py`, sobre `game/walk/emu_path.py`,
  `path_limits.py`, `path_edges.py` y `heuristic.py`: miles de casos sobre el `.att`
  de Lorencia y sobre mapas sintéticos con `0x01`, `0x02`, `0x04`, `0x20` y `0x22`, y los bordes);
  `TheStartOnRowOrColumnZeroFails`, `TheExactSearchGivesUpAfter500AndTheNearestAfter50`,
  `OnlyTheFirstFifteenCellsAreKept`, `TheDiagonalNextToTheGoalCostsLikeTheSide` (h(1, 1) = h(1, 0) = 4),
  `AnOccupiedGoalEndsOnTheDiagonalByTheTieOrder`,
  `InTheSafeZoneTheSecondTryWalksThroughCharacters`, `TheWallsLoseTheCharacterBitEveryStep` (la celda
  (221, 11) de Lorencia); `TheWireWalksMatch`: los 171 caminos del cable (solo celdas y direcciones; `game/walk/wire_vs_path.py`), con los
  2 distintos anotados (una celda ocupada que el cable no muestra).

## Sub-ola CL7.4 B: el paquete de caminar y lo que llega

**Contrato:** 04 (`D3` C->S y S->C, `D7` S->C es el daño) y 06 § Caminar (`SendMove` de `0x56acd0`). PR
propio, apilado sobre CL7.3 A. Implementación pura, sin integrar clic, movimiento ni sesión.

- **Qué:** `Protocol::Movement`: `WriteWalk(path, target, angle)` (`C1 len D3 X Y (dir << 4 | n − 1)` y los
  pasos de a dos nibbles, con XOR32 y sin C3; 15 celdas salen como 14; la dirección final por el blanco o, con
  una celda, por el ángulo), `WalkUpdateTimeMax(pathNum)` (0, 5 o 3 · n + 4), `ReadWalk` del `D3` S->C y
  `ReadDamage` del `D7` S->C (el id sin el bit 15, el bit, el daño y el tipo).
- [x] `Movement.h/.cpp` y 23 casos de `MovementTest`: cantidad local 1..15, wire limitado a 14,
  todas las direcciones y nibbles, blanco respecto de la última celda transmitida, fallback al último
  paso transmitido, restas sin wrap, tiempo sobre la cantidad local, rama exclusiva del ángulo,
  lectores BE con IDs sintéticos asimétricos y bits independientes, truncados y bytes extra.
- [x] Contraste estático de `0x56acd0` y constantes del PE `c32bbd5f`; se verificó además el helper
  `0x69fd3c`: `0x69fd4b` fuerza truncamiento y `0x69fd55` usa `FISTP m64int`, con retorno EDX:EAX.
  En el lector `0x4c6790`, `0x4c6798..0x4c67a9` mantiene el ID BE completo y `0x4c67dc` conserva
  cuatro bits de dirección. El ASM prevalece sobre el pseudocódigo.
- [x] Cable documentado: (b6,73) … (b3,74) da `C1 08 D3 EC 23 E2 0B 08`, y con FoxAnti
  `C1 08 A3 9C 53 92 7B 78`. El blanco lejano del test es sintético y explícito: el cable no lo lleva.
  Por el pedido nuevo de Emanuel, no se ejecutó el generador emulado antes propuesto.
- [x] Revisiones cruzadas de Astra 6 Ultra y Opus 5.5 xhigh, y revisión de los arreglos. Se reforzaron
  los casos de lectores y de celda/paso transmitidos; nueve mutantes fallan por sus aserciones.
  La receta double es un contrato numérico declarado: una muestra nativa observó CW `0x027f`
  fuera de `SendMove`. Falta observarla dentro de su aritmética para afirmar equivalencia nativa
  incondicional (P-C30). Los casos estáticos no se presentan como ejecuciones nativas.
- [x] Build sin PCH y con warnings como errores, formato e includes; 2.271 casos registrados en la
  suite completa con GL/Mesa, trazas y cable: cero fallos y 30 omisiones opcionales.
- [ ] Windows/MSVC y CI del PR en verde.

## Sub-ola CL7.4 C: el paso entre celdas

**Contrato:** 06 § Caminar (`MovePath` de `0x4ea270`, `MoveCharacterPosition` de `0x508a80`, la velocidad
de `0x508940`, `SetPlayerWalk` de `0x4f37c0`; `SetPlayerStop` viene de la L3). PR propio. Se detalla al
arrancar; lo que se sabe:

- **Qué:** en `GameLogic::CharacterMovement`: la celda lógica una adelante; el sub-punto de Catmull-Rom con
  los cuatro pesos y el umbral de 20; la llegada que salta al centro; el giro (de golpe desde 45°, a la mitad
  por debajo, con `OriginalAngles`); el avance hacia donde mira con `OriginalVectorRotate` y la altura del
  terreno; la velocidad (12 caminando, 15 corriendo; correr solo con botas +5, o guantes +5 en Atlans, o
  siendo MG o DL; en la zona segura no se corre); las velocidades de las acciones de caminar (0,33 y 0,34)
  que `0x4f37c0` pisa en el modelo compartido de los jugadores; el `rand()` de los pasos (1/64 el héroe,
  1/16 los otros); la zona segura por la celda lógica.
- **Tests:** `CharacterMovementTest.MatchesTheEmulatedStep` (`make_movement_vectors.py`, sobre `game/walk/emu_walk.py`:
  rectas, codos, zigzag, caminos de 2 a 15, DK, elfo, MG y DL, con botas +5 y sin, y un no jugador);
  `TheLogicalCellIsOneAhead`, `WalkingIsTwelveAndRunningFifteen`, `WithoutPlusFiveBootsThereIsNoRunning`,
  `TheSafeZoneResetsRun`, `ArrivalSnapsToTheCellCentre`, `TurnsSnapFromFortyFiveAndHalveBelow`,
  `WalkSetsTheSharedActionSpeeds`, `TheFootstepsSpendRand`.

## Sub-ola CL7.4 D: el picking del terreno

**Contrato:** 06 § Caminar (`0x5fc260(1)` → `0x5fc190` → `0x5fc0e0` → `0x5fac90` con `Flag`, `0x622440` y
`0x622170`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** `World::TerrainPicking`: la recta contra las dos caras de cada celda visible, en el orden de los
  bloques y las celdas del recorte; gana la más cercana a la cámara; las celdas con `0x08` también se
  clickean; el segundo llamado sobre la última celda elegida (aunque el rayo de ahora no haya tocado nada);
  `TargetX/Y` con `ftol(punto · 0,01) & 0xff`.
- **Tests:** `TerrainPickingTest.MatchesTheEmulatedCode` (`make_terrain_pick_vectors.py`: `0x622440` con
  segmentos y caras al azar y sus bordes, y `0x5fc260(1)` con cámaras y mouse al azar sobre Lorencia);
  `TheNearestFaceWins`, `CellsThatAreNotDrawnCanBeClicked`, `AMissKeepsTheLastSelection`; un *smoke* con la
  cámara de la traza: el rayo del centro de la pantalla cae en una celda de las que dibuja.

## Sub-ola CL7.4 E: caminar en la escena

**Contrato:** 06 § Caminar (`MoveHero` de `0x59adf0`, el orden del paso de `0x636290`, los otros con su
camino) y 06 § La cámara del juego (el recorte de los personajes en el paso con el trapecio del dibujo
anterior; los cinco planos y la esfera de los efectos). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** en la `GameScene`: el orden del paso (la UI, `MoveHero` con las paredes de antes, después las
  paredes de este paso y cada personaje); la rama del terreno de `MoveHero` con `MouseUpdateTime/Max`, un
  clic pendiente propio que no se borra con `ConsumeEdges` (el `0x7d366d4` del original), apretado, Shift,
  la barra de abajo, la condición de la acción, "a menos de 2 celdas", la misma celda y la hora; el picking
  con el rayo del último dibujo (la escena guarda la cámara, el viewport y el mouse del último `Render`); el
  `D3` del héroe (la dirección, y la celda lógica si está quieto) y el de los otros (su camino, el reintento,
  la llegada con `(dir − 1) · 45`); el `D7` que para a un jugador con `0x4f53a0` (siempre con el bit 15, y
  sin él la mitad de las veces, con `rand() & 1`, si no es el héroe; Q-C178); los cinco planos de
  `0x5fd000` con la esfera de `0x5fbf00`; el héroe y la cámara en el estado del paso (la decisión 1).
- **Tests:** con un GS falso: un clic en el piso manda los bytes de B; apretado cerca del héroe, un `A3` por
  celda y no por paso; un clic dentro de la espera se atiende al vencer (*inferido*, P-C30); con Shift o con
  el mouse en la barra, no camina; un `D3` de otro lo hace caminar; el eco con el héroe quieto le corre la
  celda lógica y no la posición; el `D7` con el bit 15 lo para, y sin el bit para a otro jugador cuando el
  `rand() & 1` da 0, gastando ese `rand()`; la repetición de una conexión del cable sin
  los paquetes con nombres (los caminos y las celdas); Review Focus 3 (un frame de 2 s).

---

## Sub-ola CL7.5 A: cada mundo del `Data\` carga

**Contrato:** 05 (los modelos especiales de cada mapa) y 06 § Los cambios de mapa (lo que carga
`0x61a560`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** la tabla de los especiales de `0x616ec0` por mapa como dato (1 y 4, 2, 3, 6, 7, 8, 10, 11 a 17,
  18 a 23, Kalima, 30, 31, 33, 34); las texturas fijas de `0x61a560` (las hojas y las lluvias de `0x64` a
  `0x68`); los mapas 30 y 34 con su `.att` y su luz según el estado del mundo, como "no va" donde el estado es
  de la CL9; el mapa 5 sin alturas da el aviso que nombra `World6\TerrainHeight` y cierra, como el original
  (Q-C180).
- **Tests:** golden: para cada mapa de `OriginalMaps` menos el 35, el terreno, el `.obj` y los modelos
  cargan, y los tipos del `.obj` sin modelo son los del research (`tests/vectors/worlds/missing-models.txt`);
  el mapa 5 da el aviso de las alturas y pide cerrar; el registro −515 de `World7` no rompe nada; Review
  Focus 2.

## Sub-ola CL7.5 B: `Gate.bmd` y la detección

**Contrato:** 05 (`Gate.bmd`) y 06 § Los cambios de mapa (`0x599ef0`). PR propio. Se detalla al arrancar; lo
que se sabe:

- **Qué:** `Data::Tables::GateRecord` (flag, mapa, x1, y1, x2, y2, destino, dirección, nivel) sobre la
  `GateTable` que ya existe, cargada en `GameData`; `GameLogic::GateCheck`, una función pura: con la celda,
  el mapa, el nivel, la clase, la montura y las alas, el contador de warp y los ms desde el último `1C`,
  devuelve nada, pedir el gate N, o un aviso (el texto y la espera de 50 pasos).
- **Tests:** los cuatro gates de Lorencia con el `Gate.bmd` real (descifrado como `game/maps/gate.py`; adentro, en el borde, afuera, el nivel
  justo); los 2/3 de las clases 3 y 4, el 28 en silencio, Atlans montado, Icarus sin alas; los 3 s y los 30
  pasos; uno solo por paso aunque se pisen dos.

## Sub-ola CL7.5 C: el protocolo del cambio de mapa

**Contrato:** 04 (`1C`, `F3 04`, `F3 12`, `BD`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** `Protocol::World`: `ReadTeleport` (`1C`: gate, mapa, x, y, dirección), `ReadRegen` (`F3 04`),
  `WriteGateRequest` (`C1 06 1C gate 00 00` en C3), `WriteTeleportDone` (`C1 04 F3 12`, C1 con XOR32) y
  `WriteCrywolfState` (`C1 04 BD 00`); la `ServerSession` con la espera del warp.
- **Tests:** los cuatro `1C` del PCAP del PR #45 de `mu-online-reverse-lab` (con `game/wire/pcap2log.py` y
  `decode.py`) y sus cuatro `F3 12` (`C1 04 F3 68` en el cable), byte a
  byte; los dos `F3 04` del `muproxy.log`; el `1C` C->S contra el encoder de C3 con serial fijo (sin captura:
  *estático*, P-C31).

## Sub-ola CL7.5 D: el cambio de mapa en la escena

**Contrato:** 06 § Los cambios de mapa (el `1C` en `0x4c6e10`, el `F3 04` en `0x4c4b80`, lo que se borra y lo
que queda, la pantalla congelada). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** el pedido: en cada paso, `MoveHero` llama a `0x599ef0` (`0x59b841`) con la `GateCheck` de la
  5.B; al pisar un gate manda el `1C` en C3, pone el contador en 9.999.999 y retiene el frame hasta el `1C`
  que vuelve; con el nivel bajo, el aviso 350 y el contador en 50 (20 pasos sin dibujo ni movimiento). La
  vuelta: la `GameScene` hace el cambio en el paso (la decisión 2), borra lo que se va (los otros, los ítems
  del piso y los elegidos siempre; los objetos, los efectos y las texturas solo con otro mapa), carga el
  mundo nuevo, pone al héroe con la z de las alturas nuevas y el ángulo `(dir − 1) · 45`, manda el `F3 12`
  después de cargar, pone el contador en 30, crea el `0x61f` y el cuervo otra vez. El `F3 04` hace lo mismo
  con el héroe y el mundo, pero no manda el `F3 12` ni toca el contador: no pone 30 ni retiene el frame.
- **Tests:** pisar el gate 23 de Lorencia (213..217 × 246..247) manda `C3 [C1 06 1C 17 00 00]` y retiene el
  frame hasta el `1C`; con el nivel bajo, el aviso y 20 pasos quietos; con un GS falso que manda la
  secuencia del cable (0 → 0 → 3 → 7 → 0): la carpeta cargada en
  cada paso; un `F3 12` por cada `1C` con gate y ninguno con el gate en 0 ni con el `F3 04`; el mismo mapa no
  recarga pero borra a los otros; los efectos se vacían solo con otro mapa; ningún frame presentado mientras
  espera; Review Focus 1.

## Sub-ola CL7.5 E: los casos de los otros mapas, como datos

**Contrato:** 06 § Los cambios de mapa (los ganchos por mapa) y los quirks del 07. PR propio. Se detalla al
arrancar; lo que se sabe:

- **Qué:** la tabla por mapa: el color de fondo del frame (`0x637700`: Icarus, Kalima y negro), el far (el 30
  y el rectángulo, de CL7.3 A), la niebla del 30, Icarus sin terreno y con nubes, la sombra y el agua del 7,
  la nieve y los techos de Devias, la arena del 8, el temblor del 5. Kalima (su agua) y los mapas de evento,
  a la CL9.
- **Tests:** la tabla contra un vector sacado del asm por script (los inmediatos de `0x637700`, `0x6371a0` y
  `0x635ae0`); cargar cada mundo y dibujar un frame sin errores, sin GL real.

---

## Sub-ola CL7.6: los efectos del mundo y de los personajes

**Contrato:** el roadmap, § Lo que dejó la CL6 (CL7.6). Se detalla al arrancar, después de la CL7.5: la luz
de abajo del elegido en la selección (`0x53fe70`, la `chasellight` del slot `0x729`), los efectos de las
acciones del caballo (el polvo de los cascos con el jinete en la `0x4b`) y los de las armas, salvo el cetro
del login. Uno o más PRs, según lo que salga al leerlos.

## Cierre: el barrido profundo de Lorencia

**Contrato:** la escena 5 entera contra el binario, después de la CL7.5 D. PR propio, como el del login:
revisores nuevos por partes (la entrada y el bucle, el paso, el dibujo 3D, el caminar y los mapas) con el asm
y el emulador; lo que se arregla en el PR (con un test que falle antes), lo que queda en 08 y la lista en 06
de lo que la escena 5 de MuRet todavía no hace.

Después: la CL8 (el HUD y las ventanas), con el ancho del mundo de `0x5bb8d0` y el área de mensajes.
