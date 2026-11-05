# Plan de la ola CL7, primera parte: el login completo

> **Para quien ejecute:** SUB-SKILL REQUERIDA: superpowers:executing-plans (en línea, como la CL6)
> o superpowers:subagent-driven-development, tarea por tarea. Los pasos llevan checkbox (`- [ ]`).
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), § El login
> completo (CL7.R). Escrito el 2026-11-05. Cada sub-ola trae su contrato, sus nombres y sus tests;
> lo que se aprenda en una se escribe acá antes de arrancar la siguiente, y las de la CL7.6 se
> detallan paso a paso al arrancar, con las interfaces que dejen las anteriores.
>
> **Quién escribe los pasos:** Claude, como en la CL6, hasta que Emanuel diga que participa.

**Objetivo:** que el login de MuRet muestre la escena 2 del original entera: el terreno del mapa
55 (el lago del fondo), sus 276 objetos (el piso de rocas y hojas, el castillo, los remolinos), el
dragón que vuela, las brasas, el aliento del caballo, el brillo del cetro, los destellos de la capa
y el cuervo, cada cosa como la calcula el `main.exe`.

**Arquitectura:** el mundo se dibuja en el mismo `ModelBatch` del pase 3D del login, en el orden
del original: terreno → personajes (con el cuervo al final del Dark Lord) → caballo → objetos del
mundo (el logo entre ellos) → efectos. `World/` nace como el lado de la CPU del mundo (sin GL): las
celdas del terreno como triángulos, los objetos con su creación, su paso y su dibujo, y los
efectos. El login deja de usar `IWorldView` (la escena dibuja su mundo, como ya dibuja su logo), y
el `EmptyWorldView` queda para las escenas que no tienen uno.

**Stack:** C++20, OpenGL 3.3 core (el `GlModelRenderer` de la CL6.3, que suma las mezclas que
falten), glm 1.0.3, GoogleTest. Ninguna biblioteca nueva.

**Spec:** el roadmap de arriba y su contrato,
[06 § El mundo del login a § Lo demás del login](../../../Client/docs/research/06-render-escenas-y-ui.md#el-mundo-del-login),
más [06 § Terreno](../../../Client/docs/research/06-render-escenas-y-ui.md#terreno) (la cadena de
`0x5fc260`, verificada contra Lorencia), los quirks Q-C19 y Q-C127 a Q-C139 del 07 y P-C23 del 08.

## Restricciones de todo el plan

- Las de `AGENTS.md` y `docs/CODING_RULES.md`: `clang-format-18`, `misc-include-cleaner` de
  `clang-tidy-18` limpio en los `.cpp` y los `.h`, el chequeo de conversiones de MSVC
  (`msvc-conv-check.py`), `MURET_WARNINGS_AS_ERRORS=ON` y la suite entera con GL y los golden.
- Una sub-ola, un PR contra `main`, con su CI seguido hasta verde. Revisión adversarial al final de
  cada sub-ola, antes del PR, y sus arreglos con un test que falló primero.
- Merge commits para traer `main`; force-push solo si Emanuel lo pide para ese PR.
- Lo que solo diría una corrida del original va al 08 (P-C23), con lo que se captura en la VM.
- Las constantes del binario van por sus bits cuando el cálculo tiene que dar lo mismo que el
  original (como en la tela), y los pasos se comparan con los vectores del código real emulado
  (`Client/docs/research/scripts/login/`), que se guardan en `Client/tests/vectors/`.
- Lo que va por paso de lógica avanza a 25 por segundo; lo que el original hace por frame dibujado
  va por tiempo, un frame del original cada 46,875 ms (Q-C20), salvo lo que se arma en el dibujo
  y no se acumula (los sprites).
- Ningún log ni test lleva un nombre de personaje real.

## Review Focus

1. **Entrar al login de nuevo** (login → selección → login): el original vuelve a cargar el mundo y
   vacía la tabla de efectos (`0x6070a0`): el dragón sale otra vez desde la partida, los objetos
   con su estado inicial y el cuervo desde (50, 50). Test en cada sub-ola que lo toque.
2. **Un archivo que falta** (un `Object<k>.bmd`, una textura, `dragon.bmd`, la carpeta `World56`):
   el original saltea el modelo sin avisar (Q-C23) y el resto sigue; MuRet igual, con un aviso en
   el log, sin dejar de dibujar el login.
3. **FPS altos o un frame largo**: los objetos, el dragón, el cuervo, las brasas y las partículas
   avanzan por paso (25 por segundo) a cualquier FPS; un frame de 2 s no los teletransporta más
   allá de lo que avanzarían 50 pasos.
4. **La resolución** (640 × 480, 1024 × 768 y una ventana ancha): el pase 3D del login va en el
   viewport (0, 25, 640, 430) escalado; el terreno y los objetos no se salen de él.
5. **El orden con mezcla**: los aditivos que no escriben la profundidad (haces de luz, remolinos,
   brasas) se dibujan después de lo opaco que tapan, en el orden del original; un cambio de orden se
   ve. Test del orden de los tramos del lote en cada sub-ola.

---

## Sub-ola CL7.1: el terreno del login

**Contrato:** 06 § Terreno (la cadena de `0x5fc260`, verificada contra la traza de Lorencia) y
06 § El terreno del login (el rango de bloques, el bit `0x08`, el agua, el slot `0x122`). PR propio.

### Tarea 1: el estado de GL de cada bloque de la traza

**Archivos:**
- Modificar: `Client/tests/support/GlTrace.h`, `GlTrace.cpp`
- Test: `Client/tests/support/test_gl_trace.cpp`

**Interfaces:**
- Produce: `GlTraceBlock::state` con `blend` (`std::optional<std::pair<std::string, std::string>>`,
  el último `glBlendFunc` con `GL_BLEND` prendido), `alphaTest` (bool), `cullFace` (bool),
  `depthTest` (bool) y `depthMask` (bool) al llamar a `glBegin`.

- [ ] **Paso 1:** test `GlTraceTest.ABlockKeepsTheStateOfGl`: una traza escrita a mano con
  `glEnable(GL_BLEND)`, `glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA)`, `glDisable(GL_CULL_FACE)`,
  `glEnable(GL_ALPHA_TEST)`, `glDepthMask(false)` y un `glBegin(GL_TRIANGLE_FAN)`: el bloque lleva
  ese estado; uno anterior a esas llamadas, el inicial (sin mezcla, culling, depth test, alpha test
  apagado, depth mask true).
- [ ] **Paso 2:** correrlo y verlo fallar (el campo no existe).
- [ ] **Paso 3:** el parser guarda esas llamadas en vez de saltearlas.
- [ ] **Paso 4:** correr `test_gl_trace` y el golden de la traza (`test_golden_pass_3d_trace`): en
  verde.
- [ ] **Paso 5:** leer en la traza de Lorencia el estado del pase opaco y del de alfa del terreno, y
  escribirlo en 06 § Terreno (*verificado*): si el de alfa lleva alpha test, culling y depth mask.
  Commit.

### Tarea 2: las celdas del terreno como triángulos

**Archivos:**
- Crear: `Client/src/World/TerrainCells.h`, `TerrainCells.cpp`
- Test: `Client/tests/world/test_terrain_cells.cpp`, `Client/tests/world/test_golden_terrain_trace.cpp`

**Interfaces:**
- Consume: `Data::Terrain::TerrainMapping`, `TerrainHeights`, `TerrainAttributes`,
  `World::TerrainLighting`, `Render::ModelBatch`.
- Produce:
  - `struct TerrainView { const TerrainMapping* mapping; const TerrainHeights* heights; const TerrainAttributes* attributes; const TerrainLighting* lighting; std::array<float, 256> tileWidths; };`
  - `void DrawTerrainCell(Render::ModelBatch& batch, const TerrainView& view, std::uint8_t x, std::uint8_t y, const TerrainWater& water);`
    — el pase opaco y, si toca, el de alfa, como `0x5fac90` y `0x5fa830`: los cuatro vértices
    (x · 100, y · 100, altura), u = x · 64 / ancho (`0x5fa5c0`), la luz del píxel (x, 255 − y), el
    alfa por vértice, el slot `capa + 0x23` (255 → `0x122`), la celda con el bit `0x08` sin nada.
    El fan de 4 vértices va en dos triángulos (0, 1, 2) y (0, 2, 3).
  - `ModelBlend::TerrainAlpha` si la tarea 1 da un estado que ninguna mezcla de ahora tiene.

- [ ] **Paso 1:** tests sintéticos de `TerrainCellsTest`: `AnOpaqueCellIsDrawnOnceWithTheSecondLayer`
  (las cuatro esquinas en alfa 255 → un pase con la capa 2), `ACellWithAlphaIsDrawnTwice` (capa 1
  opaca y capa 2 con el alfa de cada vértice), `ALayerOf255IsTheSlot0x122` (Q-C19),
  `TheBit0x08HidesTheCell`, `TheLightIsFlippedInY` (el píxel (x, 255 − y)) y
  `TheHeightsOfWorld56GoByThree` (con `TerrainHeightsScaleOfWorld56`).
- [ ] **Paso 2:** verlos fallar.
- [ ] **Paso 3:** `DrawTerrainCell`.
- [ ] **Paso 4:** verlos pasar.
- [ ] **Paso 5:** golden `GoldenTerrainTrace.TheCellsOfLorenciaMatchTheTrace`: los 329 pases opacos
  y los 141 de alfa de la traza de Lorencia, celda por celda, con la geometría, las UV, la textura
  (el slot de la traza por el nombre de GL, como en `test_golden_pass_3d_trace`) y el alfa iguales
  a menos de 0,001; la luz, igual a ±1 % donde el rojo pase de 8, salvo las 123 que suman luces
  dinámicas (06 § Terreno). Local, fuera de CI, con `MURET_CLIENT_GL_TRACE_DIR`.
- [ ] **Paso 6:** commit.

### Tarea 3: qué bloques, y en qué orden

**Archivos:**
- Modificar: `Client/src/World/TerrainCells.h`, `.cpp`
- Test: los dos de la tarea 2

**Interfaces:**
- Produce: `struct TerrainRange { int firstBlockX, lastBlockX, firstBlockY, lastBlockY; };`,
  `TerrainRange TerrainRangeOf(const Render::Camera3D& camera)` (los cinco puntos de `0x5fbb00`) y
  `void DrawTerrain(Render::ModelBatch&, const TerrainView&, const TerrainRange&, const TerrainWater&)`:
  los bloques de 4 × 4 por (fila, columna), las celdas de cada uno por (y, x) y el pase de alfa
  enseguida del opaco de su celda.

- [ ] **Paso 1:** leer `0x5fbb00`, `0x5fba90` y `0x5fc0e0` en el asm (las constantes por sus bits) y
  escribir en 06 § El terreno del login lo que hoy está *inferido*: el far por 0,9, el medio ancho
  y el alto, y el frustum por bloque y por celda.
- [ ] **Paso 2:** tests `TerrainCellsTest.TheLoginSeesBlocksFrom0To128And120To248` (con
  `LoginCamera()`) y `TheCellsGoByBlockAndThenByRow`; y en el golden de Lorencia, que los 28
  bloques y las 141 celdas con alfa salgan en el orden de la traza.
- [ ] **Paso 3:** verlos fallar, implementar, verlos pasar. Commit.

### Tarea 4: el agua

**Archivos:**
- Modificar: `Client/src/World/TerrainCells.h`, `.cpp`
- Test: `Client/tests/world/test_terrain_cells.cpp`

**Interfaces:**
- Produce: `struct TerrainWater { float offset; std::array<float, 256> waves; };` y
  `TerrainWater WaterAt(float clockMs, const TerrainRange&)`: offset = `(ftol(T) % 20000) · 5e-5`,
  ola de cada x = `sin(x · 5 + (ftol(T) % 720000) · 0,002) · 10`. La capa 5 (`TileWater01`) suma a
  u el offset y a v la ola · 0,002 cuando la capa 1 es 5 en una celda con alguna esquina de alfa
  < 1, o en el pase de alfa con la capa 2 en 5 (`0x5fa830` → `0x5fa5c0`).

- [ ] **Paso 1:** tests `TheWaterMovesWithTheClock` (T = 0, 10.000 y 20.000) y
  `OnlyLayer5IsWater`. Verlos fallar, implementar, verlos pasar. Commit.

### Tarea 5: las texturas del mundo

**Archivos:**
- Modificar: `Client/src/Scenes/TextureSets.h` (o un `WorldTextures` en `World/`, lo que pida la
  carga por mapa)
- Test: `Client/tests/scenes/test_texture_sets.cpp` (o el del archivo nuevo)

**Interfaces:**
- Produce: `WorldTextureSet(std::uint8_t map)` con los tiles de `World<N>\` en `0x23..0x30`, las TGA
  del pasto en `0x32..0x34`, `leaf01` y `leaf02` en `0x64` y `0x65`, la lluvia en `0x66..0x68`
  (Q-C136) y `Interface\accept_box01.jpg` en `0x122` si la carga de `0x61ae60` no la sube ya.

- [ ] **Paso 1:** test `WorldTexturesTest.World56HasTheTilesOfTheOriginal` (los nombres, los slots y
  el muestreo, `GL_NEAREST` y repitiendo) y `TheSlot0x122IsTheAcceptBox`. Verlos fallar,
  implementar, verlos pasar. Commit.

### Tarea 6: el terreno en el login

**Archivos:**
- Modificar: `Client/src/Scenes/LoginScene.h`, `.cpp`
- Test: `Client/tests/scenes/test_login_scene.cpp`

**Interfaces:**
- Consume: `DrawTerrain`, `TerrainRangeOf`, `WaterAt`, `WorldTextureSet`.
- Produce: `LoginScene` carga el `.map`, el `.att` y las alturas de `World56` al entrar (con la luz
  que ya carga) y dibuja el terreno primero en el pase 3D; `canvas.world` deja de usarse en el login.

- [ ] **Paso 1:** tests `LoginSceneTest.TheTerrainGoesBeforeTheCharacter` (el primer tramo del lote
  es del terreno, y el del personaje viene después) y `WithoutWorld56TheLoginStillDraws` (sin la
  carpeta: un aviso y el login de siempre). Verlos fallar, implementar, verlos pasar.
- [ ] **Paso 2:** una captura del login con Xvfb (fuera de Git): el lago al fondo, detrás del
  personaje.
- [ ] **Paso 3:** 06, P-C23 (a), PROGRESS y este plan. Revisión adversarial, arreglos, PR y CI.

---

## Sub-ola CL7.2: los objetos del mapa 55

**Contrato:** 06 § El mundo del login. PR propio.

### Tarea 1: los modelos de `Object56\` y sus texturas

**Archivos:**
- Crear: `Client/src/World/WorldModels.h`, `.cpp`
- Test: `Client/tests/world/test_world_models.cpp`, golden contra `lor_slots.bin` donde aplique

**Interfaces:**
- Produce: `WorldModelFiles(std::uint8_t map)`: el modelo k (0 a 159) de `Object<map+1>\Object<k+1>.bmd`
  (`"%s0%d.bmd"` hasta el 9), y en el mapa 55 el `0xb4` de `Object56\Dragon.bmd` (sin distinguir
  mayúsculas, Q-C127); las texturas desde `0x41c`, con la regla de `ModelTextureSlots`.

- [ ] **Paso 1:** tests `WorldModelsTest.World56LoadsObjectKPlusOne`, `AMissingModelIsSkipped` (el
  tipo 82, sin `Object83.bmd`), `TheDragonIsModel0xb4` y
  `TheLogoTexturesFollowTheWorld` (`Logo03` desde el próximo que deja el mundo: hoy
  `LoginLogoFirstSlot` está *inferido* en `0x455`; con la carga real sale del cálculo, Q-C92). Verlos
  fallar, implementar, verlos pasar. Commit.

### Tarea 2: los objetos del `.obj` al crearse

**Archivos:**
- Crear: `Client/src/World/WorldObjects.h`, `.cpp`
- Crear: `Client/tests/vectors/world55/objects.txt` (de `run_objs.py`) y su lector en
  `Client/tests/support/`
- Test: `Client/tests/world/test_world_objects.cpp`

**Interfaces:**
- Produce: `struct WorldObject { std::uint16_t type; Render::Models::ObjectTransform transform; Render::Models::ObjectState state; glm::vec3 light; float speed; float alpha, targetAlpha; bool hidden; };`
  y `std::vector<WorldObject> CreateWorldObjects(std::span<const TerrainObject>, std::uint8_t map, SceneId scene)`
  con las reglas de `0x606660`: los valores por defecto, el farol 60 a escala 0,8 y luz 0,2 (escenas
  2 y 4), los remolinos 86 y 90 en (8.600; 25.000; 5.000) con escala 60 y 110 (mapa 55) y el `0xb4`
  oculto (es el dueño del dragón).

- [ ] **Paso 1:** sacar los vectores con `run_objs.py` (la creación de cada tipo del mapa 55) y
  escribir el lector. Test `WorldObjectsTest.EachTypeIsCreatedAsTheBinary`. Verlo fallar,
  implementar, verlo pasar. Commit.

### Tarea 3: el paso de los objetos

**Interfaces:**
- Produce: `void StepWorldObjects(std::vector<WorldObject>&, std::uint8_t map)`, como `0x604ab0`
  sobre los que se dibujaron en el frame anterior: el alpha hacia su meta, la animación a su
  velocidad, el remolino 86 con −0,6° de yaw, alpha 0,2, luz (1, 0, 0) y blendMesh −2, el 90 con
  −0,23°, y los tipos 84, 85, 87 y 89 ocultos.

- [ ] **Paso 1:** test `WorldObjectsTest.ThreeStepsOfEachTypeMatchTheBinary` (los tres pasos de
  `run_objs.py`) y `OnlyTheObjectsDrawnLastFrameStep` (Q-C135). Verlos fallar, implementar, verlos
  pasar. Commit.

### Tarea 4: el dibujo de los objetos

**Interfaces:**
- Produce: `void DrawWorldObjects(Render::ModelBatch&, const IModelLibrary&, std::span<WorldObject>, const TerrainLighting&, Duration clock)`:
  sector por sector (x, después y) y en el orden del archivo, un `RenderBody(2, …)` por objeto con
  el color de `0x5ffce0` y la luz de cada normal; el blendMesh −2 por la rama aditiva; el caso del
  90 con `ModelBlend::Subtractive` (`GL_ZERO, GL_ONE_MINUS_SRC_COLOR`, sin culling ni escribir la
  profundidad), que se suma a `GlModelRenderer`.

- [ ] **Paso 1:** tests `WorldObjectsTest.EachTypeDrawsAsTheBinary` (los `RenderBody` y
  `RenderMesh` de `run_objs.py`: el modo, el alpha, el blendMesh, la luz, las mallas),
  `TheTwoRedWhirlpoolsAddUp` y `ObjectsGoSectorBySector`. Verlos fallar, implementar, verlos pasar.
  Commit.

### Tarea 5: los objetos en el login

- [ ] **Paso 1:** el login crea los objetos al entrar, los avanza en cada paso y los dibuja después
  del caballo; el logo pasa a ser uno más de la lista (`0xa2` en su sector). Tests
  `LoginSceneTest.TheObjectsGoAfterTheHorse`, `TheLogoIsAWorldObject` y
  `EnteringAgainCreatesTheObjectsAgain`. Verlos fallar, implementar, verlos pasar.
- [ ] **Paso 2:** captura con Xvfb (fuera de Git): el piso de rocas y hojas bajo el caballo, el
  castillo y los remolinos.
- [ ] **Paso 3:** docs, revisión adversarial, arreglos, PR y CI.

---

## Sub-ola CL7.6 A: el dragón de fondo

**Contrato:** 06 § El dragón de fondo. PR propio. Se detalla al arrancar; lo que se sabe:

- **Archivos:** `Client/src/World/Effects.h`, `.cpp` (la tabla de efectos con modelo de `0x518cf0`,
  200 lugares, y su paso `0x5208a0`, con el caso `0xb4` solo); `Client/tests/vectors/world55/dragon.txt`
  (de `dragon_emu.py`, 1.500 pasos).
- **Tests:** `DragonTest.FifteenHundredStepsMatchTheBinary` (posición, ángulos, estado, frame y vida
  por paso, bit a bit), `TheDragonDrawsAfterTheObjects`, `EnteringAgainStartsTheDragonAgain`
  (Review Focus 1).
- **El dibujo:** un `RenderBody` opaco con la luz del terreno + 1, sin interpolar entre keys (el
  frame es entero).

## Sub-ola CL7.6 B: la base de los efectos y los del login

**Contrato:** 06 § Los efectos del login. PR propio. Se detalla al arrancar; lo que se sabe:

- **Archivos:** `Client/src/World/Particles.h` (`0x53fe70`, 3.000 lugares, el paso `0x5446e0`),
  `Sprites.h` (`0x5491e0`, la lista de un frame), `Leaves.h` (`0x530490`, 80 en el mapa 55), y el
  billboard de `0x621400` en `Render/` (el cuadrado en el plano de la pantalla, con la rotación y la
  fila 0 arriba).
- **Fuentes:** las brasas (`0x42b1e0`, `0x5303a0`), el aliento del caballo (`0x444010`, hueso 27),
  el cetro (`0x509340` caso `0x620`, cinco sprites), el farol (`0x602ed0` caso `0x3c`, dos
  sprites, Q-C140) y los 30 destellos de la capa (`CharacterCloth::Flares()`). Las nubes y el fuego del `0x59` esperan la captura (P-C23 e).
- **Tests:** la caja y la velocidad de nacimiento de las brasas y su caída sobre un terreno plano;
  la vida, la luz y la subida del aliento paso a paso; los cinco sprites del cetro con sus rangos y
  su color (1; 0,3; 0); la escala y la luz de los 30 destellos con un T fijo; el orden: hojas →
  sprites → partículas, después de los efectos con modelo.
- **El `rand()`:** cada sistema saca del mismo generador de la CRT que la tela, en el orden del
  paso del original (06 § Los efectos del login, los `rand()` del frame). Ruling a confirmar al
  arrancar: si se comparte con la tela o va aparte.

## Sub-ola CL7.6 C: el cuervo

**Contrato:** 06 § El cuervo. PR propio. Se detalla al arrancar; lo que se sabe:

- **Archivos:** el modelo `0xc0` (`Skill\DarkSpirit.bmd`) y sus texturas como la quinta llamada de
  `0x615700` en `GameModels`; `Client/src/Scenes/DarkSpirit.h`, `.cpp` (el objeto, su paso y su
  dibujo); `Client/tests/vectors/raven/` (de `emulate_raven.py`, varias semillas).
- **Tests:** `DarkSpiritTest.TheStepMatchesTheBinary` (bit a bit, como la tela),
  `ItIsBornAtCell0AndJumpsToTheRider` (Q-C130), `ItFliesAt350OverTheRiderInMap55` (Q-C131),
  `ItDrawsMeshes3And2WithItsShadow` (Q-C132) y sus chispas (`0x5c6`) con las partículas de la CL7.6 B.
- **La textura:** los slots `0x3c7..0x3ca` y `dksbody.tga` en `0x26f`, contra `lor_slots.bin`
  (`ModelTextureSlotsGoldenTest`).

## Cierre: el barrido profundo del login

**Contrato:** el login entero contra el binario, después de la CL7.6 C. PR propio (docs y los arreglos
chicos que salgan, cada uno con su test).

- **Qué:** recorrer de nuevo, llamada por llamada, la entrada (`0x6349a0` y lo que la llama), el paso
  (`0x634c60`) y el dibujo (`0x6350b0`, con el 2D y la UI), y compararlos con `LoginScene`: lo que
  MuRet hace distinto, lo que falta y lo que hace de más, con el efecto que tiene en pantalla.
- **Cómo:** revisores nuevos por partes que no se pisan (la entrada y el bucle de `0x637700`, el paso,
  el dibujo del mundo, el 2D y la UI), con el asm y el emulador; cada hallazgo con su dirección y su
  evidencia.
- **Resultado:** lo que se arregla en el PR (con un test que falle antes), lo que queda como pendiente
  en 08 (con la prueba en la VM) y la lista en 06 de lo que el login de MuRet todavía no hace.
- **Hecho:** cuatro revisores (la entrada y el bucle, el paso, el dibujo 3D, el 2D con la UI y la cámara)
  y nueve arreglos, cada uno con su test: el cursor del juego (`0x5ac2d0`), el mundo quieto mientras
  conecta (Q-C85), la selección que retiene el frame hasta la lista, la entrada a una escena que no cuenta
  en el modo normal (ADR 0005), el login sin sesión que no se dibuja antes de su primer paso, el cetro por
  frame del original, la cabeza del Dark Lord (bit a bit, `head.txt`), el gesto con el `rand()` de la CRT y
  el fundido del caballo con un solo redondeo. Lo que queda, en 06 § Lo demás del login y en P-C27 (con
  P-C23 e para las nubes y el fuego).

Después: la CL7 sigue con Lorencia (CL7.3 del roadmap en adelante, y lo que la CL7.1 y la CL7.2 no
hayan cubierto de otros mapas), con su propio plan.
