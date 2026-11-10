> Informe escrito el 2026-11-09 contra ramas que ya no existen (`refs/pr/183`, #184, #185). Sirve como lista de tareas y de lecturas; antes de usar un dato, verificalo contra `main`, el ASM y el research (`Client/docs/research/`). Lo que ya está hecho y lo que falta, en [el traspaso](../2026-11-10-cl7-traspaso.md).

## Informe: cierre de los hitos 3 y 4 de la CL7

**Base que leí.** `main` = `origin/main` 4a4fd49. Tu `main` local está viejo (87f7b9a), así que usé el remoto. PR #184 = `claude/project-thread-02udvb` @ fb131dd. El checkpoint es `refs/pr/183` @ add8620, que sale de 4a4fd49 y **no** sale de #184. Fue una tarea de solo lectura: no compilé nada. Lo que digo de compilación viene de los handoffs.

**Resumen.**
- **Kernels.** Casi todos están escritos: creación de objetos para todos los mapas, paso y dibujo por mapa, fuego, luz radial, `LightAt`, aves, peces, hojas y partículas.
- **Lo que llega a la escena.** En `pr/183` solo llegan hojas (C1–C5, la U3 sin lluvia y la caída), partículas (Step/Draw con verificación de errores) y la creación en `WorldEntry`.
- **Lo que no llega.** El paso y el dibujo de objetos siguen por la ruta del login. La luz dinámica no tiene dueño. El pasto no se dibuja. Aves y peces no tienen servicios productivos, ni paso, ni dibujo. `0x602ed0` está declarado pero no tiene cuerpo.

---

## 1. Qué ya existe

### En `main`
| Pieza | Archivo:función | Estado |
|---|---|---|
| Luz base sombreada (`0x5f8b10`/`0x5f8d90`, tabla `0x7c0ccc4`) | `World/TerrainLighting.h:ShadeTerrainLight`, `LoadTerrainLighting` | Conectada en el login |
| Copia de la luz por ventana, primer bloque de `0x5fbf60` | `TerrainLighting.h:CopyTerrainLightWindow` | Kernel suelto, **sin caller en ningún ref** |
| Luz radial `0x5f9780` (#164) | `TerrainLighting.h:AddTerrainLight` | Kernel; sin caller en main |
| `0x5f99e0` (#173) | `TerrainLighting.h:LightAt` | Conectada, pero sobre la base sombreada y no sobre la dinámica |
| Olas de `0x5fbf60` | `World/TerrainCells.h:UpdateWaves` vía `Scenes/LoadedWorld::Step` | Conectadas. Falta la rama de tormenta (P-C24 c) |
| Terreno, pases opaco y de alfa | `LoadedWorld::DrawTerrain` | Conectado, **sin pasto** |
| Objetos del login (mapa 55) | `World/WorldObjects:StepWorldObject(obj,map,skel)`, `DrawOf`, `DrawOrder`; `Scenes/WorldObjectsDrawing:StepWorldObjects`, `DrawWorldObjects`+`ObjectFrameEffects` | Conectados en `LoginScene` |
| Modelos | `Scenes/WorldModels::Load` (los 112 de Lorencia, Bird01/Fish01, el dragón del 55, lazo genérico) | Conectados. Faltan los especiales de los demás mapas y las texturas comunes |
| Metadata de texturas (#169) | `TextureMetadataOf` / `TextureSlotOf` | Conectada |
| `GameScene::Render` | `Scenes/GameScene.cpp:49` | **Vacío** |

### En PR #184 (sobre main)
| Pieza | Archivo:función | Estado |
|---|---|---|
| Creación de todos los mapas (#163) | `World/WorldObjects.cpp:CreateWorldObjects(records, WorldObjectCreationContext&)`, `ApplyDirectMapCases` (0/1/2/3/7/8/55), `World/WorldObjectMapCreation.cpp:ApplyDefaultWorldObjectCreation` (405950→438a80→42c6b0→43ff70→42ee30→4250e0→4303d0), `WorldObjectRegistrations:RegisterWorldObject`, `IWorldObjectCreationServices` | Kernel con golden de los 2.870 (`test_golden_lorencia_world_objects`). En escena solo la usa el login, en modo proyección. Documentado en 06 § Creación común (diff de #184) |
| Alpha `0x4e9980` (#178) | `World/ObjectAlpha:StepObjectAlpha` | Conectada a través del `StepWorldObject` del login |
| Aves (#179) | `World/Birds:StepBirds`, `BirdStepCount`, `FinishOrdinaryBirdMotion`, `BirdGlowLight`; `IBirdStepServices` | Kernel. Solo tiene dobles de test |
| Peces (#179) | `World/Fish:StepFish` y políticas; `IFishStepServices` | Kernel. Solo dobles de test |
| Ondas del clima (#174) | `World/LeafWeather:StepLeafWeatherWaves`; `ILeafWaveEvaluator` | Kernel |
| Consulta del terreno (#175) | `World/TerrainAttributeQuery:TerrainAttributeQueryAt` | Kernel |
| Partículas (#168/#180) | `World/Particles:CreateChecked`, `StepParticlesChecked`; `World/ParticleDrawing:DrawParticlesChecked`; `PortableParticleTrigonometry`, `OriginalRetainedMath` | Kernel. Tipos que soporta: 0x5d9/0–3, 0x5f2/{0,2,4,6,7,8}, 0x5c6/{5,9}, 0x607/0, 0x622/{8,10}, 0x67/0, 0x68/0 y 0x60f/3 |
| Backend material GL (#181) | `Render/ModelBatchParticleBackend`, `ModelBatchParticleState`, `Scenes/ParticleDrawServices` | Kernel; ningún caller productivo en #184. `Type620850`/`Type620870` devuelven error |

### En `refs/pr/183`
| Pieza | Archivo:función | Estado |
|---|---|---|
| Paso de la escena | `Scenes/GameScene.cpp:StepWorld`. Orden: `terrain.Step` → `StepWorldObjects` **del login** (l.287) → `StepLeaves` → caballo → héroe → actores → `StepEffects` → `StepParticles` | Conectado |
| Dibujo de la escena | `GameScene::Render`. Orden: `DrawTerrain` (con la luz base) → `DrawWorldObjects` **del login** con el rand común (l.584) → suelo → `DrawCharacter` (con `LightAt` sobre la base, l.609) → `DrawWorldLeaves` → efectos → sprites → `DrawParticlesChecked` | Conectado. Faltan pájaros, peces, el dibujo del caballo y el pasto |
| Providers de Main | `main.cpp:617–651`: `PortableLeafWaveEvaluator`, `PortableParticleTrigonometry`, `ModelBatchParticleBackend`, `LeafDrawingServices`, `ParticleDrawServices` | Conectados |
| Creación en la carga | `Scenes/WorldEntry.cpp:LoadMap` con `CreationServices` (efectos 0xb4, 0x61f, 0x135 y 0x142–0x144) | Conectada. Los mapas 9 y 11–23 cortan en l.181 («controlador 447d70»). El mapa 30 corta sin `state7239Ec` |
| Tabla del pasto de `0x5f8300` | `World/WorldTerrain.cpp:LoadWorldTerrain` → `grassOffsets` (65.536 rand) | Se produce solo en el juego; **sin consumidor**. El login no gasta esos rand (Q-C146 dice que sí los gasta) |
| Paso de objetos por mapa | `World/WorldObjects.cpp:538 StepWorldObject(obj, WorldObjectStepContext&)`; `WorldObjectMapStep:Prepare…/SceneCases/MapCases` (0–8, 11–17 y la cadena default de 6 hooks); `WorldObjectFire:EmitWorldObjectFire` (52f360); `WorldObjectSecondaryStep` (52f550); `IWorldObjectStepServices` | **Kernel suelto**: sin caller de escena, sin servicio productivo y con un solo test (`test_world_resources`) |
| Dibujo de objetos | `World/WorldObjectDrawing:DrawWorldObject` (6002e0, todos los mapas); `Scenes/WorldObjectsDrawing:DrawBoundWorldObject` (602e90); `IWorldObjectDrawingServices` | Kernel suelto, sin servicio ni caller. **`DrawWorldObjectEffects` (602ed0) está declarado y no tiene definición.** Usa `std::sin` en 6 sitios |
| Poses (`0x5ffdf0`, buffer global) | `Scenes/WorldObjectRenderState::Pose/Transform/Bones` (dueño: `WorldResources`) | `Pose` sin caller |
| Acceso Fauna | `Scenes/FaunaStepServices:FaunaEnvironmentAccess` | Kernel. No implementa `IBirdStepServices` ni `IFishStepServices`, no se instancia y pide un buffer dinámico que no existe |
| Hojas y clima | `World/Leaves:StepWorldLeaves`; creadores C1–C5 enlazados en `GameScene.cpp:108–151`; U3 sin lluvia y la caída enlazados; `LeafWeather:Prepare…/AdvanceLeafWeatherPhase`; `Scenes/WorldFaunaDrawing:DrawWorldLeaves` (el nombre engaña: es 5306a0) | Conectadas. Hay kernels sin enlazar: `CreateNoriaAtlansLeaf` (C6), `CreateEmberLeaf` (C7), `StepExtendedRainLeaf` (U1), `StepEventRainLeaf` (U2) y `StepGameplayLeaf` (U3 completo) |

**Divergencias que hay que reconciliar entre #184 y `pr/183`:**
- `pr/183` no tiene Birds/Fish, `OwnItemOptions` ni `ParticleBillboard`.
- Hay diferencias en `WorldObjects.h/.cpp` (campos `field8a`/`renderMask`/`renderFlags`/`output40`/`output4c` y el paso con contexto), en `LeafWeather` y en `SessionEventApplier`/`ServerSession`.
- `Particles.cpp` de `pr/183` vuelve a meter `LegacyFtolLow`.
- `WorldResources::Release` no limpia el byte vivo de `registrations`. `0x6070a0` sí lo hace con `0x7d34818`, pero esa tabla figura como *inferida* en 06 § La carga.
- `LoadedWorld::Step` usa `IClock::Now()` y no `WorldRuntime.worldTime`.

---

## 2. Qué falta para el criterio de cierre

### Hito 3
1. **Luz dinámica compartida** (06 § La luz del terreno en un punto, § Lo propio de Lorencia):
   - Falta el dueño de `0x7b4ccc4`.
   - Falta la copia de `0x5fbf60` en `0x636de8`, antes de los objetos.
   - Los lectores tienen que pasar al buffer dinámico: `0x5fa260`/`0x5fa350`, `0x5f99e0` (objetos, personajes y efectos) y el pasto.
   - Estado inicial de `0x7b4ccc4` al cargar: **sin evidencia**.
2. **Paso con servicios reales** (`0x606560`→`0x604ab0`, 06 § Lo propio de Lorencia). Para Lorencia alcanza con `CreateParticle` (0x5d9/0–3, 0x5f2/0 y 0x5f2/2) más la luz dinámica.
   - Los mapas 1–8 y 11–17 ya tienen código en `pr/183`, pero **sin sección en el research público**.
   - Hooks default `438a90`, `42c850`, `43ffc0`, `42ee40`, `4250f0`, `4303e0`: **sin evidencia pública**.
   - `0x6063d0` y `0x5ff960`, que corren en el mismo paso: **sin evidencia** (cuerpo sin leer).
3. **Dibujo con servicios reales y poses** (`0x6046a0`→`0x602e90`→`0x5ffdf0`→`0x6002e0`, 06 § Modelos y objetos, § Lo propio de Lorencia).
   - `0x602ed0` del mapa 0: el 0x38 con dos `0x5ac` en los huesos `0x68b211c`/`0x68b22cc`. El 0x69 con luz 1, la V con el reloj y, al 50 %, dos `0x5f2` en `0x68b184c`/`0x68b18dc`. El subtipo de esas `0x5f2` es **sin evidencia** en 06.
   - `0x604580` para los mapas 2–4, 6, 8–17: **sin evidencia pública**.
4. **Orden del pase de `0x6371a0`** (06 § El dibujo del juego): faltan los pájaros después del suelo, los peces después de los personajes, el caballo `0x4453b0`, la segunda pasada de hojas en el mapa 2 y los huecos vacíos (ítems, joints, cintas, números).
5. **Pasto** (`0x5fc190` en modo 2 → `0x5fa830`, 06 § Lo propio de Lorencia y § El terreno del login).
   - Condiciones: alfa ≤ 0 en las cuatro esquinas, `0x7b4ccb4 == 0`, el bit de `0x6c9d58` (apagado en 18–23 y en 30), por celda nada en 11–17, y el slot `0x32` + capa 1 tiene que estar cargado.
   - Geometría: alto 128, −50 en x arriba, la ola de `0x78acbd8` en y, u = x·0,25 + `grassOffsets[y&0xff]`, color del buffer dinámico.
   - `0x549130` (las marcas del suelo): **sin evidencia**.
6. **Recursos** (06 § La entrada al mundo, paso 4; 05 § Los modelos de cada mundo):
   - Texturas comunes: `drop01` en 0x607, `wt00..31` en 0x41–0x60, `light01` en 0x68, el pasto en 0x32–0x34 y la lluvia en 0x66–0x68.
   - Los especiales por mapa de `0x617325` (hoy solo están el 0 y el 55).
   - La textura del fuego `0x5d9`: la carga `0x61ccb0` y su nombre es **sin evidencia** (MuRet solo trae el subconjunto del login). Sin ella, el primer fuego hace fallar `Dimensions` en el dibujo de partículas.
7. **Creación por familia.**
   - `0x7239ec` (Castle 30): productor **sin evidencia** (CL9).
   - `0x447d70` (mapas 9, 11–17 y 18–23): cuerpo **sin evidencia**.
   - Los `.att` de 30 y 34 no cargan (CL7.5 A).
   - Los ajustes posteriores al lazo en `0x61a126` (modelo 40 del mapa 1, byte `+0xc8` del mapa 8): 05 dice «sin leer qué son».

### Hito 4
1. **Aves y peces.**
   - Faltan los pools durables: 40×0x1d4 y 10.
   - Faltan los servicios completos, el paso en su lugar (después de las hojas, antes del caballo) y el dibujo `0x446b60`/`0x446f00` con sombra `0x4eff40`.
   - Handlers `4455e0`, `445890`, `445a30`, `445a90`, `445d60`, `439aa0`, `445ec0`, `4e9a40` y `622110`: **sin evidencia en el research público**, solo aparecen en comentarios de #179.
   - Dibujo de aves y peces: 06 solo da la traza (5 pájaros más 5 sombras con alfa 0,2).
2. **Mode = `0x723c50` (la tormenta).**
   - Inicial 0 en el login (estático, P-C24 c). Lo apaga el `1C` (`0x4c6e10`).
   - Lo prende `0x4d4a90`; qué paquete llega ahí es **sin evidencia**.
   - Afecta las aves, la rama de tormenta de `0x5fbf60` y la luz de prefijo.
3. **heroFlag = Hero `+0x3e2`.** Lo escriben `0x505de0` en cada paso (`0x50686e..0x5068b2`, bit 0x01 de la celda lógica, 06 § Caminar), `0x511290` y el `F3 04` (=1). No está portado: `pr/183` no lo escribe en cada paso.
4. **Emisores** (los «T01/E04–E08» del handoff).
   - Los IDs vienen de un catálogo privado; su asignación exacta es **sin evidencia pública**.
   - En el código, el paso emite por `IWorldObjectStepServices::CreateParticle/CreateModelEffect` y el dibujo por `IWorldObjectDrawingServices::Sprite/Particle/RuntimeEffect/Joint`.
   - El dibujo de otras familias pide 0x5da/8 y 9, 0x5c5/0, 0x5f2/0x26 y 0x5c6/15. Ninguno está en `Particles` y no tienen kernel público.
5. **Hojas.**
   - C6 y C7 tienen kernel pero no están enlazados. C8 (`443996`, mapa 31) y C9 (`4292c0`, mapas 33/54): **sin evidencia**.
   - U1/U2/U3 (rama 0x66): los kernels existen. Les falta el splash hacia `Particles` 0x67/0x68 y el impacto `5f82c0` → `TerrainAttributeQueryAt`.
6. **`0F`** (04 § El clima: `C1 04 0F xy`; nibble alto 0 → meta 0; 1 → (xy&0xf)·6; otro → nada): no hay ni lector ni aplicador. Escribe `leafWeather.target`.
7. **Writer `430510`** (selector de Crywolf `0x5763358`): **sin evidencia**; es CL9.
8. **Fuera de este hito:** los materiales `0x620850`/`0x620870` (los alcanzan 0x5ac/6 y 0x5ed/5) y el fog. Son cuerpos **sin evidencia** y siguen como error explícito.

---

## 3. Tareas en orden (un PR chico por fila)

**Prerrequisitos.** No son de estos hitos, pero sin ellos no se puede empezar.
- **P1.** Integrar #184 y resolver las divergencias de la §1 contra `pr/183`. Quedan una sola versión de `Particles`, de `LeafWeather` y de `WorldObjects`.
- **P2.** Extraer de `pr/183` la capa de estado: `WorldRuntime`, `WorldState`, `WorldResources`, los campos de `SceneContext` y el reloj raw32 (`Core/MillisecondTime`, `Platform/SystemMillisecondClock32`).
- **P3.** Extraer `WorldEntry::LoadMap` con `CreationServices`. Va con el hito 1.

### Hito 3
| # | PR | Archivos | Contrato y tests | Depende de |
|---|---|---|---|---|
| H3.1 | Research: paso y dibujo por mapa | `06` § nueva «Paso y dibujo por mapa» | Contrastar con ASM lo que `pr/183` ya porta: `0x604ab0` en 1–8 y 11–17, 52f550, `0x6002e0`/`0x604580` por mapa. Marcar «sin leer» `0x6063d0`, `0x5ff960`, los hooks y 602ed0 donde no se cierre. Solo docs | — |
| H3.2 | Dueño de la luz dinámica | `Scenes/LoadedWorld.{h,cpp}` (`DynamicLighting()`), `WorldResources` | `Step` hace `CopyTerrainLightWindow(base→dinámica, rango)` y **después** las olas. Tests: celdas fuera de la ventana intactas, wrap de +3, la recarga reinicia. El inicial queda como frontera hasta la captura V1 | P2 |
| H3.3 | Lectores de la dinámica | `LoadedWorld::DrawTerrain` (`TerrainView.lighting`), `GameScene` (`LightAt` de objetos, personajes y efectos) | Test: un `AddTerrainLight` antes del dibujo sube el cuadrado ±4. Golden de la traza: 23 vértices alrededor del FireLight01 #1933, con tolerancia | H3.2 |
| H3.4 | Paso productivo, Lorencia | `Scenes/WorldObjectStepServices.{h,cpp}` (nuevo), `GameScene::StepWorld`, `WorldObjectsDrawing::StepWorldObjects` (variante con contexto) | `StepWorldObject(obj, ctx)` sobre los vistos, en `DrawOrder`; `IWorldObjectStepModels`=`WorldObjectRenderState`. `CreateParticle`→`Particles::CreateChecked` con el mismo `CrtRandom`; `HandleMap` devuelve error. Tests con CRT sembrado y expectativas sacadas del ASM: 3+1+1(+1) rand de 52f360, r 4 y 3, Light01..03 ocultos después del primer paso (Q-C148), techos con tile 4, mismo rand que las hojas | H3.2, P1 |
| H3.5 | Paso, familia «switch propio» A: mapas 1/4, 2, 3 | Tests de `WorldObjectMapStep` | Casos de `StepMapOne..Four` + 52f550 contra H3.1; `PlaySound`→`WorldRuntime.lastSoundId` | H3.1, H3.4 |
| H3.6 | Paso, familia B: 5, 6, 7, 8 y 11–17 | ídem | `0x607`/0 y `0x5c6`/9 por `CreateParticle`; mapa 5: política de recursos ausentes | H3.5 |
| H3.7 | Servicios de dibujo y poses, Lorencia | `Scenes/WorldObjectDrawingServices.{h,cpp}` (nuevo, sobre `ModelBatch` y `WorldObjectRenderState`), `GameScene::Render` | Gate 602e90 → `WorldObjectRenderState::Pose` (5ffdf0) → `DrawWorldObject`. Reemplaza el `DrawWorldObjects` del login en la escena 5 y conserva el rand común de 602ef4. Tests: 0x69 con cuatro `RenderMesh`; golden GL de los 120 bloques de la traza (slot, mezcla, orden) | H3.4, H3.11 |
| H3.8 | Cuerpo de 602ed0 en el mapa 0 | `World/WorldObjectDrawing.cpp:DrawWorldObjectEffects` | 0x38: dos `0x5ac` en los huesos del buffer global. 0x69: luz, V, 50 % de `0x5f2` (subtipo según ASM). Test del lugar contra la traza (13.094; 12.142; 283) | H3.7 |
| H3.9 | 602ed0/6002e0 de las demás familias | ídem, un PR por familia | Solo lo que H3.1 respalde; el resto, error explícito | H3.1, H3.8 |
| H3.10 | Orden del pase | `GameScene::Render` | Huecos vacíos donde corresponda; test `TheGamePassGoesInTheOrderOf0x6371a0` con un lote espía | H3.7 |
| H3.11 | Recursos comunes de 0x616ec0 | `WorldModels`/`WorldResources`, `TextureSets.h` | drop01, wt, light01, pasto 0x32–0x34, lluvia 0x66–0x68 (rain03 pisa light01). Golden de slots contra `lor_slots.bin`. Leer `0x61ccb0` y documentar el nombre de 0x5d9 antes de cargarla | P2 |
| H3.12 | Especiales de `0x617325` | `WorldModels`; un PR por grupo: {1,4,3,6,8}, {2,7,10}, {11–23, Kalima, 31, 33, 34} | La tabla de 05, incluidas las creaciones a coordenada fija de 2 y 3. El 5 sigue la política de ausentes | H3.11 |
| H3.13 | Pasto | `World/TerrainCells` (`DrawGrassCell`), `LoadedWorld::DrawTerrain` | Contrato del §2.5 del hito 3. Golden: 25 quads de la traza, z 165–293, x+50, u en {0; 0,25; 0,5; 0,75} | H3.3, H3.11 |
| H3.14 | 65.536 rand en todas las cargas | `LoginScene::LoadTheTerrain` y la selección, pasando `WorldTerrainLoad.random` | Test de cuenta de draws (Q-C146) | P2 |
| H3.15 | `Release` limpia el byte vivo de las registraciones | `WorldResources::Release` | Contrastar antes `0x6070a0` (la tabla está *inferida*). Test de reentrada | P3 |

### Hito 4
| # | PR | Archivos | Contrato y tests | Depende de |
|---|---|---|---|---|
| H4.1 | Pools de fauna | `WorldResources` (aves 40×`BirdRecord`, peces 10), `Release` | Solo apaga el byte vivo (06 § La carga, paso 2). Test de reentrada | P2 |
| H4.2 | heroFlag | Escritor de `ActorState.safeZone` en el bucle de actores (tramo 0x50686e..0x5068b2) | Celda lógica, bit 0x01. Test: el paso de aves ve el valor del paso anterior | P2 |
| H4.3 | Mode | `WorldRuntime.storm` (inicial 0, P-C24 c), el `1C` lo pone en 0, rama de tormenta de `UpdateWaves` | `0x4d4a90` queda como frontera documentada | H4.1 |
| H4.4 | Research de aves y peces | `06` § Los pájaros / § Los peces | Handlers de la §2.1 del hito 4 y los dibujos 446b60/446f00; solo docs | — |
| H4.5 | Aves productivas, familia ordinaria y Lorencia | `Scenes/BirdStepServices` (sobre `FaunaEnvironmentAccess` + H3.2), `GameScene` después de `StepLeaves` | `StepBirds` con la base física estable. Tests: tope 5 (`0x44642a/430`), luz de prefijo sobre la dinámica | H4.1–H4.4, H3.2 |
| H4.6 | Aves en Atlans 7, Icarus 10 y Kalima | ídem | Handlers de spawn por familia | H4.5 |
| H4.7 | Peces productivos | `Scenes/FishStepServices`, `GameScene` después de las aves | `StepFish`; Lorencia con capa 1 = 5 y hasta 3; otras familias según `FishDirectSpawnAllowed` | H4.4, H4.1 |
| H4.8 | Dibujo de aves y peces | Nuevos `Scenes/BirdDrawing`, `FishDrawing`; huecos de H3.10 | Test de traza: 5 pájaros más 5 sombras (alfa 0,2) | H4.5, H4.7, H3.10 |
| H4.9 | Lluvia U1/U2/U3 | Binders de `GameScene.cpp:153` | Splash → `Particles::CreateChecked(0x67/0x68)`; impacto → `TerrainAttributeQueryAt` | P1, H3.11 |
| H4.10 | Hojas C6 y C7 | Binders de `GameScene.cpp:75` | C6 en mapas 3/7. C7 solo con el byte `0x7239ec` prestado, si no error. C8/C9 quedan como error | P1 |
| H4.11 | `0F` | `Protocol/World` (lector), `SessionEventApplier` → `leafWeather.target` | Los tres casos de 04 § El clima | P2 |
| H4.12 | Fuente y fuegos integrados en Lorencia | Test de escena | 3 pasos y un dibujo: orden de los rand y conteos 0x5d9/0x5f2 contra la traza (23 partículas, después de las hojas) | H3.8, H4.8 |
| H4.13 | Matriz por familia | `test_game_scene_families` | Por mapa nominal: crear → 3 pasos → dibujar, sin `Unsupported` donde hay evidencia y con error explícito donde no | todo lo anterior |

---

## 4. Lo que no se cierra sin observar el original en la VM

| # | Qué falta | Captura concreta |
|---|---|---|
| V1 | Inicial y contenido de `0x7b4ccc4` frente a `0x7c0ccc4`; los 4 vértices de la fila 136 (P-C28 e) | Volcado de las dos tablas en un frame de Lorencia, y un punto de parada en `0x5f9780` con y de 133 a 139. Además, volcar `0x7b4ccc4` justo después de `0x61a560` |
| V2 | Productor de la tormenta (P-C24 c) | Cable más punto de parada en las escrituras de `0x723c50` y en la entrada de `0x4d4a90` |
| V3 | Vigencia de `+0x3e2` frente al paso de aves | Puntos de parada en `0x446120` (lectura de hero+0x3e2) y en `0x50686e`, cruzando el borde de la zona segura |
| V4 | Cantidad de aves y peces en 30 s; un pez en el agua de la traza (P-C28 j) | Traza GL de varios frames, o puntos de parada en `0x446120`/`0x447030` |
| V5 | Light01..03 visibles solo en el primer frame (Q-C148, P-C28 f) | Traza GL de los primeros 20 frames después de entrar |
| V6 | Lluvia con el `0F` en Lorencia: ¿se ve o solo suena? ¿usa el slot 0x66? (P-C31 d) | Traza GL y video con un `0F` de lluvia |
| V7 | La taberna: techos en alfa 0 y sin hojas (P-C31 e) | Traza GL con el héroe en la capa 1 = 4 |
| V8 | Perfil x87 de las ondas de hojas, de partículas y del `SinSpill` de aves (el puerto no acredita FSIN universal) | CW/SW y ST0 al entrar a `0x530490`, `0x5446e0` y `0x446120` |
| V9 | Dibujo de otras familias (602ed0/6002e0, hojas C6, fauna de 7/10/Kalima). Solo existe traza de Lorencia y del login | GLIntercept, 10 frames parado, en los mapas 2, 3, 7, 10 y uno de Kalima, más el cambio 0→3 (P-C31 g) |

Lo que es pura lectura del binario no necesita VM. Se resuelve con ASM: el nombre de la textura de `0x61ccb0`, `0x6063d0`/`0x5ff960`, `0x447d70`, `0x620850`/`0x620870`, los handlers de aves y peces, C8/C9, `430510` y los hooks default.

### Fuentes
- `/home/user/muret/docs/superpowers/plans/2026-11-05-ola-cl7-lorencia.md` (en `origin/main`)
- `/home/user/muret/Client/docs/research/06-render-escenas-y-ui.md` (en `origin/main`; #184 le suma la sección «Creación común»)
- Handoffs: `refs/pr/183:docs/superpowers/plans/2026-11-09-cl7-runtime-wip-handoff.md` y `…-fauna-access-wip-handoff.md`
- Guía de continuidad: `refs/pr/176:docs/superpowers/plans/2026-11-09-cl7-continuation.md`