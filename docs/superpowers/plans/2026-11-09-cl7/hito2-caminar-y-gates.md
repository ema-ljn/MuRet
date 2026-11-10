> Informe escrito el 2026-11-09 contra ramas que ya no existen (`refs/pr/183`, #184, #185). Sirve como lista de tareas y de lecturas; antes de usar un dato, verificalo contra `main`, el ASM y el research (`Client/docs/research/`). Lo que ya está hecho y lo que falta, en [el traspaso](../2026-11-10-cl7-traspaso.md).

# Hito 2 de CL7 (caminar y transiciones): qué hay, qué falta y en qué orden

Lo leí todo con `git show` y `git grep`, sin tocar nada. Las referencias son estas: `main` = `4a4fd49`; PR #184 = `claude/project-thread-02udvb`, que suma 40 commits sobre main; `refs/pr/183` = `add8620`, que es `6a4d117` (PR182) más el slice de Fauna.

**En una línea:** en main están todos los componentes puros. PR #184 suma el paso entre celdas y la posición. PR183 cablea el clic, el paso del héroe y el pedido de gate, pero casi sin tests. D3 y D7 recibidos, los otros personajes, la zona segura por paso, el 1C completo, el F3 04 y la matriz 0–36 no están en ningún lado.

**Un hallazgo que conviene saber ya:** en PR183, `SessionEventApplier::ApplyWorld` devuelve `Unexpected` para D3 y D7. Eso pasa por `CheckApplication` y termina en `Fail(ApplicationFailed)`. Con un GS real, **el primer eco D3 de un clic tumba la sesión**.

---

## 1. Qué ya existe

### main (`4a4fd49`): componentes puros, sin integrar

| Pieza | Archivo:función | Tests |
|---|---|---|
| Mapa de paredes vivo `0x508f00` (sin su RNG) | `World/TerrainWalls.cpp: TerrainWalls::Rebuild` | `world/test_terrain_walls.cpp` (3), incluido `GoldenTerrainWalls.TheLorenciaFileCharacterBitDisappearsOnTheFirstStep` |
| `PathFinding2` + A* (`0x4eac00`/`0x4ead70`): dos intentos, tope de 15, sin la rama por radio | `GameLogic/PathFinder.cpp: PathFinder::Find`; `Path.h`, `PathCell.h` | `gamelogic/test_path_finder.cpp` (23). No se repitieron los 171 caminos del cable |
| D3 de salida, `WalkUpdateTimeMax`, lectores de D3 y D7 | `Protocol/Movement.cpp: WriteWalk`, `WalkUpdateTimeMax`, `ReadWalk`, `ReadDamage` | `protocol/test_movement.cpp` (23) |
| Velocidad `0x508940` | `GameLogic/MovementSpeed.cpp: UpdateMovementSpeed` | `gamelogic/test_movement_speed.cpp` (20) |
| Picking `0x5fc260…` y su consumidor `0x59ddf3..0x59deb3` | `World/TerrainPicking.cpp: PickTerrain`, `PickTerrainTarget` | `world/test_terrain_picking.cpp` (29), `world/test_golden_terrain_picking.cpp` (2) |
| `Gate.bmd` | `Data/Tables/GateTable.cpp: LoadGateTable`, `GateRecord: ReadGateRecord`; `Scenes/GameData` exige el archivo | `data/test_gate_table.cpp` (3), `test_golden_gate_table.cpp` (2), `test_gate_record.cpp` (2), `scenes/test_game_data.cpp` |
| Decisión de gates `0x599ef0` | `GameLogic/GateCheck.cpp: GateCheck` (+ `GateCheckState.h`, `GateDecision.h`) | `gamelogic/test_gate_check.cpp` (27), `test_golden_gate_check.cpp` (2) |
| Formatos de warp | `Protocol/World.cpp: ReadTeleport`, `ReadRegen`, `WriteGateRequest`, `WriteTeleportDone`, `WriteCrywolfState` | `protocol/test_world.cpp` (16) |
| Altura `0x5f93c0` | `World/TerrainHeight: TerrainHeightAt` | sólo indirectos (golden loaded world, shared camera) |

Lo que main todavía no tiene:
- `GameScene::Render` está vacío y `HoldsTheLastFrame()` devuelve false.
- `SessionState` termina en `EnteringWorld`.
- `WorldEvent` sólo trae F3 03 y F3 10: D3, D7, 1C y F3 04 no están registrados.

### PR #184 (main + unidades)

| Pieza | Archivo:función | Tests |
|---|---|---|
| `MovePath` `0x4ea270` | `GameLogic/MovePath.cpp: MovePath` | `gamelogic/test_move_path.cpp` (19) |
| `MoveCharacterPosition` `0x508a80` | `GameLogic/CharacterPosition.cpp: UpdateCharacterPosition` | `gamelogic/test_character_position.cpp` (12) |
| Helpers angulares con el perfil de ADR0012; `AngleDistance` ahora recibe el modo como `uint8` | `Render/Models/OriginalAngles.cpp: CreateAngle`, `TurnAngle`, `AngleDistance` | `render/test_original_angles.cpp` (16), incluido `NativePc53NearestCorpus` |
| phi retenido de `CreateAngle`: corrige el ULP de R5-22 (ADR0014 sigue como candidato, falta Windows) | `Render/Models/OriginalRetainedMath.cpp: Detail::AtanDegreesProductPC53` | `render/test_original_retained_math.cpp` (6), `OriginalAnglesTest.RetainsPhiThroughNativeNonunitProducts` |
| ftol | `World/OriginalFtol.cpp: OriginalFtolLow` | — |
| Sink de sesión (#167) | `App/ISessionEventSink.h`, `Scenes/SessionEventApplier.cpp`; `ApplyWorld` devuelve error para todo; `SessionFailure::ApplicationFailed` | `scenes/test_session_event_applier.cpp`, `app/test_server_session.cpp` |

### refs/pr/183 (WIP)

Según su handoff, el último run completo dio 559 casos con 1 fallo y los checks de formato e includes salieron con exit1.

**Escena y movimiento:**
- `Scenes/GameMovement.cpp`:
  - `GameScene::UpdateHeroHeadTarget` (`59b1c2..59b287`)
  - `CheckGates` (caller de `0x599ef0`)
  - `ProcessHeroInput` (`0x59b846..`)
  - `UseGroundClick` (`0x59ddef..0x59e248` + SendMove)
  - `RefreshHeroTerrainLayer` (`0x59e2ba`)
- `Scenes/GameScene.cpp`:
  - `Step` (el latch del clic pendiente)
  - `StepWorld` (el orden)
  - `StepHeroPrefix` (guardas, `warpCounter` −1 con acceso protegido, corte con >30)
  - `RebuildWalls`
  - `PrepareFrame` / `HoldsTheLastFrame` (contador >30)
  - `Render` guarda `pickingRay`, `terrainRange` y `terrainPolygon` del último dibujo
- `Scenes/ActorMotion.cpp: MoveActorAlongPath` (Walk → MovePath → Stop o Position).
- `Scenes/ActorActions.cpp: SetActorStop` (`0x4f2250`) y `SetActorWalk` (`0x4f37c0`); las ramas sin binding devuelven error.
- `Render/Models/LogicalAccess` (4 o 3.848 `rand()` por cierre).
- Estado: `Scenes/WorldState`, `ActorState.h`, `WorldRuntime.h`, `WorldResources`.

**Transiciones:**
- `Scenes/WorldEntry.cpp`: `ApplyTeleport` (`0x4c6e10`), `ApplyRegen` (`0x4c4b80`), `LoadMap` (`0x61a560` parcial), `ClearActors`, `ConstructActor` (`0x511290`).
- `App/ServerSession`: `OnWalk`, `OnDamage`, `OnTeleport` (con `TeleportHistory` y rechazo sin cifrar), `OnRegen`, `RequestGate`, `SendWalk`, `ConfirmTeleport`, `RequestCrywolfState`; además `SessionState::InWorld` y `Protocol::World::WriteRejectedTeleport`.

**Tests de PR183 que tocan este hito:**
- `GameSceneTest.HeroHeadTargetUsesMouseAndYawBeforeTheMovementPhase`
- `GameSceneTest.WarpGateReadsAndClosesOnceEvenWhenItRetainsTheFrame`
- `LeafWorldLoadingTest.RealLoadPublishesTheOrderedDaviasRowsAndSameMapTeleportDoesNotReloadThem`
- `WorldEntryPipelineTest.*`, que es la entrada (hito 1)

**No hay ningún test** de clic→D3, `MoveActorAlongPath`, `SetActorWalk`/`SetActorStop`, envío de gate, 1C con carga ni F3 04.

---

## 2. Qué falta para el criterio de cierre

**Héroe (`MoveHero` `0x59adf0`, 06 § Caminar)**
- **Llegada:** `0x77f3ba8 = ftol(ángulo)`, `0x77f3ba4 = 0` y `0x5650c0(héroe, héroe, 0)` no están en PR183. Qué hace `0x5650c0` con MovementType 0 (terreno) es **sin evidencia**: el 06 sólo dice «pegar, levantar, hablar».
- **`0x57cfc0` desde `0x59e255`** (3.848 `rand()`, «casi todos los pasos»): PR183 lo deja fuera a propósito. La condición exacta es **sin evidencia**; hay que inventariar `0x57d051..0x57d10d`. Es CL7.P, pero rompe el orden del RNG (Review Focus 4).
- **Productores de `0x77f3b18`, `0x77f3bac` y `0x4033d0`:** **sin evidencia** (P-C30 e). PR183 los modela como flags en false.
- **Miss del retry:** PR183 corta la rama del terreno cuando el retry falla. Hay que contrastarlo estáticamente con `0x59de39..0x59deb3`.

**Paredes, zona segura y freno**
- `0x505de0` (`0x50686e..0x5068b2`) reescribe `+0x3e2` en cada paso con el bit `0x01` de la **celda lógica**. Falta: PR183 sólo lo calcula al construir.
- `+0x3e5` es el freno de 60 pasos: lo pone `0x4c9790` (qué paquete lo dispara es **sin evidencia**) y lo baja `0x505de0`. El campo `movementCooldown` existe en PR183 pero nadie lo escribe.

**Otros personajes (`0x508ba0`)**
- Falta entero: el bucle de PR183 sólo anima.
- El 06 dice «Siempre `MoveCharacterPosition`». Antes de portarlo hay que contrastar si está dentro o fuera de la rama de `Movement`.

**D3 recibido (`0x4c6790`)**
- Hoy es un error explícito.
- Para el tipo `0x186`, «el ángulo hacia el centro» no tiene la fórmula en el research: hay que contrastarlo en el ASM.

**D7 recibido (`0x4caae0`) y `SetPlayerShock` (`0x4f53a0`)**
- Hoy es un error explícito.
- Dos cosas son **sin evidencia** y piden inventario de ASM: cuántos `rand()` gasta el sonido de golpe por clase (`0x4f5488..`) y si el número de daño (`0x548e40`) consume `rand()`.

**Gates (caller de `0x599ef0`, en `0x59b841`)**
- **Destino 30/31:** PR183 corta *antes* del envío. El original manda el pedido, pone 9.999.999 y *después* llama a `0x61c210` y `0x61c170("Data\\Macro.txt")`. El cuerpo de `0x61c210` es **sin evidencia**.
- Faltan tres cosas: el bloqueo de `0x576794c` cuando el pedido es el gate 0 (plan 5B), `0x77f3ba4 = 0` y `clearWarpPending` antes del pedido.
- Los avisos 261, 263, 350 y 569 van al área de mensajes, que es CL8.

**1C (`0x4c6e10`)**
- **gate 0:** `0x53d930` (acción `0x93`, efecto `0x5c6`, sonido `0x76`) y `0x576794c = 0`. Hoy es error.
- **Mismo mapa:** `0x607b40`, `0x510f70(0x57678bc)` y `0x451d10`→`0x451590` no están. PR183 sólo resetea `cloth`, así que **quedan los demás personajes**.
- **Cola del paso 5:** faltan el cuervo (`0x420d60`), la tormenta (`0x723c50 = 0`), `0x78abb0c` y `0x78abb10` en 0 y `0x6c8e4d = 0xff`.
- **Sin leer en el research (sin evidencia):** `0x5be150`, y `0x447d70` para los mapas 9 y 11–23; PR183 devuelve error en esos mapas, y eso incluye el 32, que pasa a 9.
- `0x512a30` en Chaos Castle: el 06 lo nombra sólo al entrar; PR183 lo llama también al salir.
- `0x657a90`, `0x65dc30` y el renglón 484 son CL8. Los sonidos de Chaos Castle son CL10, como disparadores.

**F3 04 (`0x4c4b80`)**
- «Empieza como `0x4c3940`»: hay que contrastar ese prefijo.
- Hay que conservar `+0x368`, `+0x369`, `+0x370`, `+0x375` y `+0x1f2`; PR183 sólo conserva el pk.
- `+0x3e2 = 1` es *inferido*.
- El 06 dice «con dos `0x512a30`» al cambiar de mapa; PR183 lo condiciona a los mapas 18–23.

**Reentrada**
- **Ida y vuelta por gates:** no está.
- **Juego → selección → juego:** depende del `F1 02` con 1 (`0x4c2710`). La limpieza de `0x4c275c` está **sin leer**. P-C27 b asigna ese disparador a CL8, pero el plan lo lista como bloqueo de CL7: hay que decidirlo.

**Todos los mapas**
- Q-C180 (5 y 35 cierran el cliente) no está implementado.
- Faltan Kalima `0x4377a0`, el mapa 30 (`0x42a760`), las macros 30/31 y la luz del 34 según el estado (Q-C181, CL9).
- Según el `Gate.bmd` real (lo decodifiqué), hay gates de entrada en los mapas 0, 1, 2, 3, 4, 6, 7, 8, 10, 30, 31, 33 y 34. Los demás sólo se alcanzan por 1C, F3 03 o F3 04.
- Toda la caminata del cable es de Lorencia: para los otros mapas sólo hay evidencia estática.

---

## 3. Tareas ordenadas (un PR chico cada una)

**Prerequisitos del hito 1 / CL7.P, que no son de este hito pero lo bloquean.** Se extraen de PR183 con un único `WorldState`:
- **D0a.** `WorldState`, `ActorState`, `WorldRuntime`, `WorldResources` y `LogicalAccess`.
- **D0b.** `InWorld` + `ApplyWorldEntry`/`ApplyInventory` + `LoadMap`/`ConstructActor`.
- **D0c.** `GameScene` con `drawingEnabled`, `PrepareFrame`/`HoldsTheLastFrame` (contador >30) y `Render` guardando rayo, rango y polígono del último dibujo (Q-C165).

Base de trabajo: PR #184.

**T1. Sesión: recibir y mandar caminar y warp**
- **Archivos:** `App/WorldEvent.h`, `App/ServerSession.*`, `App/TeleportHistory.*`, `Protocol/World.*` (`WriteRejectedTeleport`).
- **Contrato:** registrar D3, D7, 1C y F3 04 y aplicarlos en el orden del cable, antes del paso. Un 1C sin cifrar va al historial y se contesta con C3 `C1 06 F1 03 00 1C` (*inferido*); el F3 04 no mira ese flag. Escritores: `SendWalk`, `RequestGate`, `ConfirmTeleport`, `RequestCrywolfState`.
- **Tests que fallan en main:** `ServerSessionTest.WalkDamageTeleportAndRegenReachTheSinkInWireOrder`, `…UnencryptedTeleportIsRecordedAndAnsweredWithF103`, `…GateRequestAndTeleportDoneUseTheirDelivery`.
- **Depende de:** PR #184 (#167), D0b.

**T2. `SetPlayerStop` y `SetPlayerWalk` (`0x4f2250`, `0x4f37c0`)**
- **Archivos:** `Scenes/ActorActions.*`, `PlayerActionSpeeds.h`, test nuevo `scenes/test_actor_actions.cpp`.
- **Contrato:** 06 § Caminar; Q-C171, Q-C172, Q-C179. Las ramas sin binding devuelven error *después* de su prefijo.
- **Tests:**
  - `Run` sube sólo con clase 3/4, botas +5 fuera de los mapas 7, 24–29 y 36, o guantes +5 dentro de ellos; vuelve a 0 en zona segura.
  - Las acciones 13–20, 73 y 75 van a 0,33 y las 22–33 a 0,34, a la mitad con `+0x78 & 2`; la 21 no se toca.
  - `rand() & 63` en el héroe y `& 15` en los otros se consumen.
  - Las acciones de Stop.
- **Depende de:** D0a.

**T3. Paso del héroe: prefijo de `MoveHero` y rama de caminar**
- **Archivos:** `Scenes/ActorMotion.*`, `GameScene.cpp` (`StepHeroPrefix`), `scenes/test_hero_movement.cpp`.
- **Contrato:**
  - Guardas `0x59ae24..0x59afd6`; contador −1 en `0x59af01` y corte con >30.
  - Cabeza antes que el movimiento.
  - `+0x78 & 0x20` → Stop; si no, Walk + `MovePath(héroe, 1)`.
  - Llegada con `0x77f3ba8`, `0x77f3ba4` y `0x5650c0`, este último como frontera explícita.
  - Si no llegó: limpia `0x200000` y llama a `0x508a80`.
- **Tests:**
  - La celda lógica va una adelante (Q-C173).
  - 12 o 15 unidades por paso según `Run`.
  - La llegada fija XY.
  - Con el contador en 31 no se mueve.
  - 4 `rand()` por cierre de `+0x41c`/`+0x420`.
  - Un frame de 2 s no pasa de 50 pasos (Review Focus 3).
  - Los 49 pasos para 6 celdas de la emulación, sólo como regresión histórica.
- **Depende de:** T2, PR #184.

**T4. Clic → camino → D3**
- **Archivos:** `Scenes/GameMovement.cpp`, `WorldRuntime.h`, `scenes/test_game_scene_walk.cpp`.
- **Contrato:** `0x59b846..0x59e2ba` y `0x56acd0`; Q-C158, Q-C164..169, Q-C177.
- **Tests con GS falso:**
  - Un clic en el piso manda el D3 del cable: (b6,73)…(b3,74) da `C1 08 D3 B6 73 73 76 70` antes del XOR32.
  - Apretado sale un D3 por celda.
  - Un clic dentro de la ventana se atiende al vencer.
  - Con Shift no camina; con `mouse.y > 431` no camina y `MouseUpdateTime` no sube.
  - Con la celda lógica a 2 o más de la que se ve, no recalcula.
  - Con 15 celdas, el cable lleva 14 y `MouseUpdateTimeMax` = 49.
  - Con la ventana `0x8000`, no manda pero conserva el camino.
  - Se escribe `0x77f36d0`.
- **Depende de:** T1, T3, D0c, CL7.4 D.

**T5. Cierre del registro propio al final de `MoveHero` (`0x57cfc0`, CL7.P)**
- **Primero:** el inventario estático de la condición.
- **Test:** +3.848 `rand()` exactamente en las ramas que lo alcanzan.
- **Depende de:** T4.

**T6. Paredes, zona segura y freno por paso (`0x508f00`, `0x505de0`)**
- **Archivos:** `GameScene.cpp` (`RebuildWalls` y el bucle `0x508ea0`).
- **Tests:**
  - El camino del paso N ve las paredes del N−1.
  - La celda (221, 11) se limpia en el primer paso.
  - La zona segura sigue a la celda lógica adelantada.
  - 8 `rand()` por actor vivo.
  - `+0x3e5` baja.
- **Depende de:** T3.

**T7. Otros personajes (`0x508ba0`)**
- **Tests:**
  - Al llegar, yaw = (dir−1)·45, y la dirección 0 da −45 (Q-C175).
  - Quieto, con celda ≠ blanco, reintenta en cada paso.
  - No reintenta con el tipo `0x159` ni con `+0x3e5` ≠ 0.
  - El héroe se saltea.
- **Depende de:** T2, T6.

**T8. D3 recibido (`0x4c6790`)**
- **Archivos:** `SessionEventApplier.cpp` + un aplicador nuevo.
- **Tests:**
  - El eco con el héroe quieto mueve la celda lógica y no la posición (Q-C176).
  - Con el héroe caminando sólo cambia `+0x388`.
  - A otro personaje: blanco + `PathFinding2` con las paredes vigentes; si falla, Stop.
  - Un id desconocido no escribe nada (Q-C174).
  - El nibble `f` se conserva.
- **Depende de:** T1, T7.

**T9. D7 recibido y Shock**
- **Antes:** el inventario de `0x4f5488..` y `0x548e40`.
- **Tests:**
  - Con el bit 15 se para el héroe que camina (acción `0xc2`, `Movement = 0`).
  - Sin el bit, al héroe nunca; a otro jugador si `rand() & 1 == 0`, y se gasta ese `rand()`.
  - Con `+0x376 == 0x113`, nunca.
  - La vida del héroe tiene piso en 0 y gasta 3.848 `rand()`.
- **Depende de:** T8.

**T10. Pedido de gate en el paso**
- **Contrato:** 06 «Pisar un gate» y plan 5B «Pendiente de integración». La frontera de 30/31 va *después* del envío.
- **Tests:**
  - Gate 23 (213..217 × 246..247): manda `C3 [C1 06 1C 17 00 00]` y retiene el frame hasta el 1C.
  - Con nivel 9: aviso 350, 20 pasos quietos y después 30 sin gates.
  - La ventana de 3 s.
  - Casos de otros mapas: 18 (0→2), 25 (3→0), 62 (4→10 sin alas), 45 (Atlans montado), 102 (frontera 30/31 después del envío).
- **Depende de:** T4, T1.

**T11. 1C al mismo mapa (gate ≠ 0)**
- **Contrato:** los pasos 2, 4 (sin carga), 5 y 6 del 06, y «Lo que queda y lo que no».
- **Tests:**
  - Con los 1C de la PCAP: se van los demás personajes, los ítems del piso y la lista; quedan objetos, efectos, cámara y zoom.
  - Sale un solo `F3 12`.
  - Contador en 30, `MouseUpdateTimeMax` en 6.
  - Efecto `0x61f` y alpha en 0.
  - Se recrea el cuervo.
- **Depende de:** T10, D0.

**T12. 1C a otro mapa: la carga dentro del handler**
- **Contrato:** `0x61a560` en orden, `BD 00` en el 34, z con las alturas nuevas, y el `F3 12` después de cargar.
- **Test:** con un GS falso, la secuencia 0→0→3→7→0.
- **Depende de:** T11, CL7.5 A.

**T13. 1C con gate 0**
- **Contrato:** `0x53d930`, sin F3 12, sin limpieza y sin tocar el contador.
- **Depende de:** T11.

**T14. F3 04**
- **Tests:** los dos F3 04 históricos; ningún F3 12; el contador no se toca; los campos conservados; mismo y distinto mapa.
- **Depende de:** T12.

**T15. Reentrada**
- Ida y vuelta 0↔3 por los gates 23 y 25.
- Juego → selección → juego: falta leer `0x4c275c` y decidir si el `F1 02` es CL7 o CL8.
- **Depende de:** T12, T14.

**T16. Matriz de mapas 0–36**
- **Por cada mapa:**
  - Entrada por 1C y vuelta.
  - Caminar recto, en codo, en el borde y en zona segura.
  - Picking y altura, incluido el `0x40` del 30 (Q-C150).
- **Además:**
  - Los gates de los 13 mapas fuente.
  - El F3 04.
  - 5 y 35 cierran con el aviso (Q-C180).
  - 32 carga `World10` y queda en 9.
  - Los mapas sin `0x447d70` quedan como frontera explícita, no como «pasa».
- **Depende de:** T12–T15, CL7.5 A y E.

Se puede ir en paralelo: T1 con T2, T7 con T10, T13 con T14.

---

## 4. Lo que sólo se cierra con observación nativa (VM, fuera de Git)

- **P-C30 a (clic pendiente):** punto de parada en `0x59b882` leyendo `0x7d366d4` y `0x77f3b10`, con un camino de 15 y un segundo clic a los 10 pasos.
- **P-C30 b (eco con tope 14):** leer `+0x41c`/`+0x420` de `[0x743a170]` con las funciones de la tabla protegida, después de un clic a unas 20 celdas.
- **P-C30 c:** GLIntercept de 30–60 frames en recta y en codo, con y sin botas +5.
- **P-C30 d + P-C32:** CW/SW dentro de `0x4ea270`, `0x508a80` y `0x56acd0`. Además, en `4e9499`, `4e973d`, `4e95aa`, `4e95fe` y `4e9699`: CW/SW antes y después, AX, el ZF que consume el salto, ST0 y entradas, en la sesión XP caminando. El run3 de ADR0012 es una prueba local, no una sesión XP.
- **P-C30 e, g, h, i, j:**
  - (e) escritores de `0x77f3bac` y `0x77f3b18`, y el `ecx` de `0x4033d0`;
  - (g) quién manda `C1 05 18 dir 7A`;
  - (h) una captura con `C2 13`;
  - (i) apretar, pasar a la barra y volver;
  - (j) punto de parada en `0x59de06` contra el último `0x620480`.
- **P-C31 a, b, c, g, h, i:**
  - (a) pisar el gate 23 con nivel ≥ 10, con cable y video 640×480;
  - (b) nivel < 10: el aviso 350 y unos 0,8 s congelado;
  - (c) los 1,2 s después de un warp;
  - (g) 0→3 con traza GL;
  - (h) un `/move` al mismo mapa;
  - (i) el rebote del gate 115.
- **P-C29 c:** que caminando el `glTranslatef` sea el héroe más (707,1068; −707,1068; 850).
- **Inferidos sin captura en el 08:** `+0x3e2 = 1` tras F3 04 (bastaría leerlo después de revivir), el `C1 06 F1 03 00 1C` ante un 1C sin cifrar y que los tiempos del `F3 12` sean la carga.

Lo que es «sin evidencia» en el punto 2 se cierra leyendo el ASM, sin VM: `0x5650c0`, `0x57cfc0`, la ubicación de `MoveCharacterPosition` en `0x508ba0`, el ángulo del tipo `0x186`, `0x4f5488..`, `0x548e40`, `0x61c210`, `0x447d70`, `0x5be150`, `0x4c275c`, `0x4c9790` y el prefijo de `0x4c4b80`.

El único archivo que escribí es el script descartable con el que decodifiqué `Gate.bmd`: `/tmp/claude-0/-home-user-muret/73ff3e7e-faf7-5ccf-a8f1-a15b11734593/scratchpad/gates.py`.
