> Informe escrito el 2026-11-09 contra ramas que ya no existen (`refs/pr/183`, #184, #185). Sirve como lista de tareas y de lecturas; antes de usar un dato, verificalo contra `main`, el ASM y el research (`Client/docs/research/`). Lo que ya está hecho y lo que falta, en [el traspaso](../2026-11-10-cl7-traspaso.md).

## Informe: hito 1 de la CL7 (entrada al mundo, héroe y equipo)

Usé `origin/main` = 4a4fd49. Ojo: el `main` local está viejo, en 87f7b9a. Como base de trabajo tomé `claude/project-thread-02udvb` (PR184). Ya incluye PR167: sink síncrono y `ApplyWorld` con error. No tiene `InWorld`, `WorldState`, `LogicalAccess`, `OwnCharacterRecord`, `WorldEntry` ni `ActorActions`. Todo eso vive solo en `refs/pr/183`. Las pruebas de 183 que nombro existen, pero el snapshot no tiene ninguna corrida registrada.

---

### 1. Qué ya hace `refs/pr/183`

| Pieza | archivo:función | Original | Prueba o frontera |
|---|---|---|---|
| Estado `InWorld` y `PublishWorldEntry`/`JoinedWorld` | `App/SessionState.h`, `ServerSession.cpp:PublishWorldEntry` | `0x4c4854` (`0x3d`) | Indirecta: `WorldEntryPipelineTest.AppliesTheEncryptedEntryBeforeAZeroStepRenderUsingTheDurableOwner`. No hay test de sesión. |
| F3 03 y F3 10 aplicados en el dispatch con el contexto vivo | `ServerSession.cpp:OnWorldEntryReply/OnInventoryReply` → `CheckApplication` | `0x4e2b10` | `ServerSessionTest.WorldEvents*` (orden y multiplicidad), `EnteringWorldHasNoTimeout` |
| Aplicador productivo | `Scenes/SessionEventApplier.cpp:ApplyWorld` (constructor con `SceneContext`) | — | D3 y D7 devuelven error **a propósito**. Sin contexto, también error. |
| Respuesta F3 03 | `Scenes/WorldEntry.cpp:ApplyWorldEntry` | `0x4c3940`, pasos 1–16 de 06 | Los dos `WorldEntryPipelineTest.*` (el otro es `ConstructsRealMapsAndRequestsCrywolfStateOnlyForThirtyFour`) |
| Carga del mapa | `WorldEntry.cpp:LoadMap` | `0x61a560`, `0x510f70`, `0x447d70` | Mismos tests. Error explícito en los mapas 9 y 11–23: «falta el controlador … 447d70». |
| Construcción del héroe | `WorldEntry.cpp:ConstructActor` | `0x511290` | Celda, zona segura `.att&1`, (c+0,5)·100, altura +30/+90 con el `0x318` previo `0x1c15`, ángulo (d−1)·45, stamp. Probado en los dos tests del pipeline. |
| Equipo al actor y stats derivados | `Scenes/ActorEquipment.cpp:ApplyActorEquipment` → `OwnDerivedStats.cpp:UpdateOwnDerivedStatsTail` | `0x512a30`, cola `5512bf..5513b1` | Sin test directo. El pipeline mira `bodyParts[0]` = `0x1012`, nivel 15, excelente. |
| Stats de F3 03 y vaciados del registro | `Scenes/OwnCharacterRecord.cpp:ApplyStats/ClearEquipmentForEntry/ClearEquipmentForInventory` | `0x4c40e1..0x4c4180`, `0x4c47c8`, `0x4c563c`, Q-C144 | Los 3 `OwnCharacterRecordTest.*` |
| Inventario F3 10 | `WorldEntry.cpp:ApplyInventory` | `0x4c55a0` → `0x5bc740` | **Sin test.** Errores explícitos: auxiliares `443d40/421d60`, ID `0x1a04`, `0x180b` con L>6, efectos de los slots 8, 1, 10 y 11, grilla. |
| Acción de parado | `Scenes/ActorActions.cpp:SetActorStop` | `0x4f2250` | Indirecta: `GameSceneTest.FinishingTheBodyStopsTheCanonicalActorBeforeItsHeadStep`. Errores: tipo≠`0x20a`, mapas de vuelo (`5f82a0`), `0xca`/C1 18, subtype 209..211. |
| Contador de espera y bandera | `WorldLoadingScene.cpp:Enter` (9.999.999 solo con `loadingOnceFlag==0`); `WorldState::PrepareSelectedActor` la baja | `0x6356f0`, `0x7d3a13e` | **Sin test** |
| Frame retenido | `GameScene.cpp:PrepareFrame/HoldsTheLastFrame`, `IScene::PrepareFrame`, `SceneManager::HoldsTheLastFrame` | `0x6371a6`, `0x6371c1..0x63722e` | `GameSceneTest.WarpGateReadsAndClosesOnceEvenWhenItRetainsTheFrame` (31/30, profundidad 0/1/255) |
| Primer paso | `GameScene.cpp:Step/SendEntryRequest` (yaw −45, F3 03 con el nombre del actor durable) | `0x636290`, `0x636838`, `0x636478..0x63661e` | `GameSceneTest.TheFirstStepSendsF303`, `TheActorNameRequestPadsEmptyAndUsesAllTenFullBytes` |
| Decremento del contador | `GameScene.cpp:StepHeroPrefix` | `0x59af01` | Indirecta |
| Pase del juego | `GameScene.cpp:Render`: franja 640×48, trapecio de 640, mundo 640×432, terreno (salvo mapa 10), objetos, suelo, personajes, hojas, efectos, sprites, partículas, cursor | `0x6371a0` | Pipeline: cero pasos, `vertices>0`, rayo, mapa 10, niveles del trapecio |

**Lo que en 183 es un no-op silencioso** (sin error, pese a que el plan dice «no no-ops»):
- No llama a los 8 helpers previos a la cola de `550d10`. Los stops `40e1b0`/`54fe10` aparecen solo en el handoff, no en el código.
- No gasta los 3.848 de `0x57cfc0` por paso. El comentario está en `GameMovement.cpp:RefreshHeroTerrainLayer` y contradice la decisión 3 del plan.
- No actualiza la zona segura por paso (`0x505de0`).
- No oculta el caballo en zona segura (`0x4440b8`) y no lo dibuja en el pase.
- No hace los stores `+0x1e3`, `0x77f3b67`, `0x6c8e4d`, `0x78abb0c/10`, ni las llamadas `0x553780(1,0)` y `0x657a90`.
- No manda el `A0`.
- Con un F3 03 en claro no manda el `F1 03`.

**Donde el código va más allá del research público** (falta la lectura estática documentada):
- El research dice que la respuesta F3 03 cierra el registro propio en 3 sitios. El código cierra 4: los 3 en línea más uno dentro de `512a30`.
- El research dice que el F3 10 lo cierra una vez. El código lo cierra 1 + k veces, una más por cada entrada de equipo.
- El decoder de los 7 bytes opacos, la ubicación en la grilla y los vaciados de `scalar1` y `flags1b`. El research solo dice «tipo −1».
- Las ramas `0x48`, `0xb`/`0xc`, la condición de mapa <11 o >23 y el sufijo `rand()%16` de `SetActorStop`.
- Cuántos accesos protegidos hace `0x511290`.
- Que la clave `0x57678bc` venga del `playerIndex` del F1 00.
- `ISessionEventSink` dice que la sesión mutable sirve solo para `PublishWorldEntry`, pero el aplicador también manda `BD`.
- `WorldRuntime.map` queda en −1 durante la espera. El original deja ahí el 54 que escribe `0x633f20` (06 § Los personajes de la lista).

**Riesgo en producción:** 183 enruta D3, D7, 1C y F3 04 al aplicador, y D3 y D7 devuelven error. Con un GS real, el primer D3 que llega después de entrar cierra la sesión con `ApplicationFailed`.

---

### 2. Qué falta, por criterio de cierre

| Criterio | Falta | Respaldo |
|---|---|---|
| Aplicador productivo con contexto histórico | Pasar F3 03 y F3 10 sobre PR184 **sin** D3, D7, 1C ni F3 04 (son del hito 2). Probar F3 03 y F3 10 en el mismo Poll, F3 10 con cantidad 0 y F3 10 antes del F3 03. | 04 § El dispatcher; 06 § Caminar («WinMain despacha todos los paquetes de la cola antes de `0x637700`»); 04 § La entrada al mundo |
| | La rama en claro de `0x4c3940` (`0x4c396a` → `F1 03`) | 06 § La entrada al mundo. Lo que lleva el paquete: sin evidencia en el research. |
| | La admisión y la inicialización común de `0x4c55a0`, más el decoder de `0x5bc740` | 04 § El F3 10 dice «pendientes L3/P»: sin evidencia en el research |
| Espera con el frame retenido | Tests de punta a punta: el mosaico se ve y no hay Present hasta que el contador llega a ≤30 | 06 § La carga del mundo (escena 3); § La entrada al mundo, pasos 2–4; § El dibujo del juego `0x6371a6`/`0x63722e`; 02 § Qué corre en cada paso |
| | Qué dibuja y presenta `0x6356f0` cuando la bandera `0x7d3a13e` ya no es 0. Hoy 183 presenta un frame vacío. | Sin evidencia en el research (06 describe solo «la primera vez») |
| | Pasos y `rand()` durante la espera, y el mapa vivo en ese momento | P-C28 b |
| Publicación del héroe y de `InWorld` en orden nativo | El orden 5→16 ya está en 183. Faltan los stores `+0x1e3=0` (fila 8), `0x77f3b67=0` (fila 9), `0x6c8e4d=0xff` (fila 14) y `0x78abb0c/10=0` (fila 15). | 06, tabla de `0x4c3940` |
| | Las llamadas `0x553780(1,0)` y `0x657a90` | Sin evidencia en el research (no se leyeron) |
| | Un test que observe el orden: ver la sección 3, T7 | — |
| Equipo y stats antes del primer frame, también con cero pasos | Stats de F3 03: ya están | 04, tabla de la entrada; Q-C144 |
| | Equipo del registro viejo, aplicado antes de vaciarlo | 06, filas 5–7 |
| | F3 10 en el mismo Poll | P-C28 a |
| | `0x550d10` con `40e1b0`/`54fe10` | Sin evidencia en el research: 04 solo dice «recalcula los atributos (`0x550d10`)» desde el A9 |
| | Visibilidad del héroe en un frame de cero pasos. `+0x16c` lo escribe `0x508ea0` en cada paso y el alpha es 0 (fila 13). | Sin evidencia de qué deja `0x511290` en `+0x16c`; P-C28 f |
| | Luz del héroe desde el buffer dinámico | 06 § El héroe; depende de `0x5fbf60` (L4) |
| Zona segura | Al construir: ya está | 06 § El héroe; § Los personajes de la lista (`0x511b9f..0x511bb9`) |
| | **Por paso, falta**: `0x505de0` (`0x50686e..0x5068b2`), con la celda lógica de adelante | 06 § Caminar, último párrafo |
| | Acción de parado con arma de una mano | P-C14 c, P-C28 d |
| Caballo | Paso con ocultación en la celda segura del jinete: `0x444010`/`0x4440b8` | 06 § El caballo oscuro |
| | Dibujo `0x4453b0` en `0x637508`, después de los peces y antes de las hojas | 06 § El dibujo del juego |
| | Quién crea el caballo en la escena 5 (llamador de `0x443fa0`) | Sin evidencia en el research |
| Reentrada | Segundo F3 03: `0x61a560` siempre, `0x510f70` por clave. El héroe viejo con la misma clave sobrevive. | 06 § La carga del mapa, paso 3. Sin test. |
| | Juego → selección: `0x4c2710` con 1 (Q-C79), baja `0x7d3a101` en `0x4c2f39`/`0x4c38f9`. 183 lo ignora. | 02 § Las cinco escenas; 04 § F1 02 |
| | La limpieza en `0x4c275c`, y quién vuelve a 0 `0x7d3a140` | Sin evidencia en el research |

---

### 3. Tareas ordenadas (un PR chico cada una)

Todas salen de PR184 y traen el código desde 183. Por la autoridad del plan, los tests usan valores contrastados con el ASM y datos del cable o la traza ya existentes, sin emulación nueva. «R» quiere decir que primero hay una lectura estática que documentar en 06.

1. **T1 · `InWorld`.**
   - Archivos: `App/SessionState.h`, `ServerSession.h/.cpp` (`PublishWorldEntry`, `JoinedWorld`; `Awaited` y `WithConnectServer` también para `InWorld`), `ISessionEventSink.h` (agregar BD al contrato), `tests/support/AppPrinters.h`, `test_server_session.cpp`.
   - Contrato: 06, fila 9 (`0x4c4854`); ADR 0007.
   - Tests: `PublishingTheEntryFromTheSinkMovesToInWorld`, `InWorldHasNoReplyTimeoutAndKeepsTheKeepalive`, `AFailureAfterPublishingEndsInApplicationFailed`. Hoy fallan porque no compilan.
   - Depende de: PR184.

2. **T2 · `LogicalAccess`.**
   - Archivos: `Render/Models/LogicalAccess.h/.cpp`, `tests/render/test_logical_access.cpp`.
   - Contrato: 06 § La tabla de protección (`0x40b4d0` 3.848; `0x412890`/`0x40b800` 4; byte de cuenta); Q-C125, Q-C183. Hay que alinear el nombre con el `ProtectedValue<T>` del plan.
   - Tests: `TheLastCloseSpendsItsRandoms`, `NestedClosesSpendNothing`, `TheDepthByteWraps`.
   - Depende de: PR184.

3. **T3 · `OwnCharacterRecord`** (con R: qué escribe `0x4c47c8`/`0x4c563c` además del tipo).
   - Archivos: `Scenes/OwnCharacterRecord.h/.cpp`, test.
   - Contrato: 04, tabla de la entrada; 06 § La lógica; fila 7; Q-C144.
   - Tests: los 3 `OwnCharacterRecordTest.*`.
   - Depende de: PR184.

4. **T4 · Pool durable.**
   - Archivos: `WorldState`, `WorldRuntime`, `ActorState`, `SelectionState(WorldState&)`, `CharacterSelectScene`, `main.cpp`.
   - Contrato: 06 § Los personajes de la lista: 401 registros, `0x510f70`, y el mapa 54 de `0x633f20`, que tiene que quedar en `WorldRuntime.map`.
   - Tests: los 5 `WorldStateTest.*`, y `SelectionPipelineTest.TwoIdenticalAcceptedCreationsApplyBeforeTheSceneReadsTheSnapshot`, que el handoff deja con una corrida pendiente.
   - Depende de: T3.

5. **T5 · La espera.**
   - Archivos: `WorldLoadingScene`, `WorldState::PrepareSelectedActor`, `SelectionState::BeginSelection` (`0x633f30`), `IScene::PrepareFrame`, `SceneManager`, `GameScene` (`PrepareFrame`/`Hold`, `StepWorld` condicionado a `drawingEnabled`, decremento `0x59af01`).
   - Contrato: 06 § La carga del mundo (escena 3); § La entrada al mundo, pasos 1–4 y 6; § El dibujo del juego; § El paso del juego (`0x636da3`); 02 § Qué corre en cada paso.
   - Tests:
     - `GameSceneTest.HoldsTheFrameUntilTheWorldArrives`, que hoy falla porque `HoldsTheLastFrame` devuelve false.
     - `WarpGateReadsAndClosesOnce…`.
     - `WorldLoadingSceneTest.TheFirstLoadArmsTheCounterAndTheFlag` y `ARaisedFlagKeepsTheCounter`.
     - `SceneManagerTest.TheMosaicStaysUntilTheCounterIsThirty`.
     - Ningún test fija cuántos pasos corren en la espera.
   - Depende de: T1, T2, T4.

6. **T6 · R+código de `0x512a30` y `0x4f2250`.**
   - Archivos: `ActorEquipment.cpp` (sin la cola de `550d10`), `ActorActions.cpp`, tests nuevos.
   - Contrato: 04 § El F3 10; 06 § Caminar (`SetPlayerStop`); P-C14 c. Las ramas que no tienen respaldo esperan a la R.
   - Tests: `ActorEquipmentTest.ProjectsTheTwelveOwnSlots`, `ActorStopTest.SafeZoneOrNoWeaponStands1Or2ForTheElf`, `…WeaponMatrix`, `…DarkHorseOutsideIs4a`, `…FenrirIs76To79`, `…SittingIsAnExplicitFrontier`.
   - Depende de: T4.

7. **T7 · `ApplyWorldEntry`** (`LoadMap`, `ConstructActor`, el aplicador solo para F3 03).
   - Archivos: `WorldEntry.h/.cpp`, `WorldResources`, `SessionEventApplier`, `main.cpp`; `PlayerIndex` va después de la R sobre `0x57678bc`.
   - Contrato: 06, tabla de `0x4c3940`; § El héroe; § La carga del mapa; Q-C58, Q-C144, Q-C145, Q-C146, Q-C175, Q-C180.
   - Tests:
     - Hay que invertir `SessionEventApplierTest.WorldApplicationIsExplicitlyUnavailableForEntryAndEmptyInventory`.
     - `ConstructsRealMaps…`.
     - `TheCableReplyGives13350_12750AndMinus45` (vector L1 del plan).
     - `TheHeroSlotIsTheRandAfterTheGrassOffsets`.
     - `TheOldRecordDressesTheHeroBeforeItIsEmptied`.
     - `AnEquipmentFailureLeavesTheHeroUnpublished`: un IDIV por cero, sesión `Failed`, contador sin cambiar.
     - `AStopFailureKeepsCounterThirtyAndTwoArrivals`: observa el prefijo.
     - `MapThirtyTwoLoadsWorldTenAsNine`, `AnEventMapStopsAt447d70`, `AMissingTerrainHeightNamesTheFile`.
     - `TheEntryClosesTheOwnRecordAtItsThreeSites`: con profundidad 0; el cierre de `512a30` queda pendiente de la R.
   - Depende de: T1, T2, T4, T6.

8. **T8 · Pase y paso del juego.**
   - Archivos: `GameScene::Render`/`StepWorld` de 183.
   - Contrato: 06 § El dibujo del juego y § El paso del juego.
   - Tests: el pipeline de cero pasos (hoy falla porque `Render` está vacío) y `GoldenTerrainTrace.TheGameSceneDrawsTheCellsOfLorencia` (plan L1).
   - Depende de: T5, T7, y además de la capa Leaf/runtime de PR182 (hito 4). Sin el provider de hojas, `Render` corta con error en el mapa 0.

9. **T9 · `ApplyInventory`** (R: decoder, grilla, cierres de R, cantidad 0).
   - Contrato: 04 § El F3 10; P-C28 a.
   - Tests: `ACountOfZeroStillEmpties`, `TheFiveCableSlotsDressTheHeroInTheSamePollBeforeAZeroStepFrame`, `RepeatedSlotsApplyEach`, y uno por cada frontera explícita.
   - Depende de: T7, T8.

10. **T10 · `0x550d10`** (R completa de `0x550d10`, `40e1b0`, `54fe10`, `54e540`, `54e590` en 06).
    - Archivos: `OwnDerivedStats`, `OwnItemOptions` (PR177), `ActorEquipment`.
    - Hasta que exista la R, los stops tienen que ser errores explícitos, no el código de hoy que se los saltea.
    - Tests: `OwnDerivedStatsTest.*` y uno por helper.
    - Depende de: T6, PR177.

11. **T11 · Zona segura por paso.**
    - Contrato: `0x505de0` dentro de `0x508ea0`, en el orden `0x508ba0` → `0x4fd5f0` → `0x505de0` (06 § El paso del juego y § Caminar).
    - Test: `GameSceneTest.TheSafeZoneFollowsTheLogicalCellAhead`. Hoy falla también en 183.
    - Depende de: T8.

12. **T12 · Caballo.**
    - Archivos: `DarkHorse.cpp:StepDarkHorse` (`0x4440b8`), la llamada en `GameScene::Render` (`0x637508`), la creación (después de su R).
    - Tests: `DarkHorseTest.InTheRidersSafeCellTheAlphaGoesToZeroAndKeepsTheSpeed`, `GameSceneTest.TheHorseGoesAfterFishBeforeLeaves`.
    - Depende de: T9, T11.

13. **T13 · Reentrada A: segundo F3 03** (mismo mapa y otro).
    - Test: `ASecondEntryReloadsAndRedrawsTheHeroSlot`. Verifica que el héroe viejo con la misma clave sobrevive.
    - Depende de: T7, T8.

14. **T14 · Reentrada B: `F1 02` con 1** (R sobre `0x4c275c`).
    - Archivos: `ServerSession::OnLogoutResult`, `SceneTransitions`, `SceneManager`, bajar `drawingEnabled`.
    - Tests: `ServerSessionTest.AnEncryptedLogoutWithOneGoesToTheSelection`, `AClearOneChangesNothing` (Q-C79), `SceneManagerTest.GameSelectionGameHoldsAndReloadsAgain`.
    - Depende de: T5, T13.

15. **T15 · Barrido del hito.**
    - Resoluciones 640×480, 1024×768 y ancha (Review Focus 5); archivos ausentes (Review Focus 2); frames 0, 1 y varios.
    - Depende de: todas las anteriores.

---

### 4. Lo que no se cierra sin observación nativa (VM, `main.exe` c32bbd5f)

1. **P-C28 a/f · Primer frame.**
   - Breakpoints en `0x4c3940`, `0x4c55a0` y `0x6371a0`, o una traza de GL de los primeros 20 frames.
   - Medir: si los dos paquetes se aplican en la misma vuelta de WinMain y cuántos `0x636290` corren antes del primer `0x6371a0` que devuelve verdadero, o sea, si existe un frame de cero pasos.
   - Medir también el héroe en ese frame: `+0x16c` y `+0x174`, y si se ve vestido.
2. **P-C28 b · La espera.** Estado de `rand()` en `[TlsGetValue([0x6e1fb0])+0x14]` en `0x6362fb` y al entrar a `0x4c3940`. Además: cuántos decrementos hace `0x59af01` y cuánto vale `0x6c9dd0`.
3. **CL7.P · Estado inicial.**
   - El byte de cuenta de las entradas `0x77f3b04` y `0x7681eb4` de `0x57630c8`, en `0x6356f0` y al entrar a `0x4c3940`.
   - El estado de `rand()` al entrar y al salir de `0x4c3940` y de `0x4c55a0`. Con eso se decide 3 contra 4 cierres en la entrada y 1 contra 1+k en el F3 10.
4. **P-C28 c · Q-C144.** Volcado de `[0x7681eb0]+0x20..0x4c` después de entrar.
5. **P-C28 d / P-C14 c · Parado al entrar.** `+0x111` del héroe al entrar en zona segura con arma de una mano, y el cable del `C1 05 18 01 7a`.
6. **Stats `550d10`.** Volcado del registro propio (`0x4e`, `0x60`, `0x62`, `0xee2..0xf03`) en `0x5513b1`, con el acceso todavía abierto, después del F3 10. Más el estado de `rand()`.
7. **Caballo.** Con un DL que lleve `0x1a04` en el slot 8:
   - Breakpoint en `0x443fa0` para ver quién lo crea durante `0x4c55a0`/`0x5bc740`.
   - `+0x174` en la tabla `0x7d335d0`, dentro y fuera de la zona segura de Lorencia.
   - Video a 640×480.
8. **Zona segura por paso.** `+0x3e2` del héroe en cada paso mientras cruza el borde del pueblo. Confirma que usa la celda lógica de adelante.
9. **P-C28 h.** Quién escribe `[0x710148]+4`, y el `A0`.
10. **Reentrada.** Lorencia → `F1 02` con 1 → selección → Lorencia, y un segundo F3 03:
    - `0x7d3a101`, `0x7d3a13e`, `0x7d3a140`, `0x77f3b04` y el yaw `0x7d36870` en cada fase.
    - Cuántos actores quedan con `+0x1f4` igual a la clave.
    - Qué limpia `0x4c275c`.
