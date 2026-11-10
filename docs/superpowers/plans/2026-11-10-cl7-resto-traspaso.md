# CL7: caminar, fauna y cola del dibujo

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** completar la implementación no nativa restante del traspaso: hito 2, P-C55(c) y P-C55(f). VM al final, por pedido de Emanuel.

**Architecture:** `WorldState` conserva la historia durable. Reutilizar kernels puros y servicios de escena; red antes del paso, movimiento antes del nuevo TerrainWall y pases en orden original. Un concepto por commit/PR.

**Tech Stack:** C++20, CMake/vcpkg existentes, GoogleTest/CTest.

**Spec:** [traspaso](2026-11-10-cl7-traspaso.md), [hito 2](2026-11-09-cl7/hito2-caminar-y-gates.md), [hitos 3/4](2026-11-09-cl7/hito3-4-mundo-vivo.md), [research 06](../../../Client/docs/research/06-render-escenas-y-ui.md). Contrastar informes viejos con árbol `2110806` y ASM.

## Global Constraints

- AGENTS.md y docs/CODING_RULES.md; tutor apagado, código autorizado.
- Evidencia estática del build `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`; sin emulaciones nuevas ni protecciones.
- Preservar orden de rand, prefijos y estado entre escenas. Etapas pendientes con error explícito.
- RED→GREEN para comportamiento nuevo. ASM manda; no cambiar expectativas para ocultar fallos.
- Lorencia y Devias como referencia; matriz por familias, sin nombres privados ni capturas versionadas.
- PRs por concepto a main, sin merge ni configuración GitHub.
- Builds serializados con `/workspace/.tools/muret-build.lock`; cargar `/workspace/.tools/muret-env.sh`.
- VM al final; validación del port no sustituye evidencia nativa.

## Review Focus

- D3 válido conserva celda lógica adelantada y dirección, sin tumbar sesión.
- Guardas, Shift, barra inferior, clic pendiente y contadores conservan sus efectos exactos.
- Gates/regen distinguen mismo mapa, nuevo mapa y gate0, incluidos envíos y campos retenidos.
- Fauna conserva slots físicos, límites por familia y flujo CRT compartido.
- Frame conserva orden, modo Kalima y operaciones GL aun sin geometría.

## Propiedad de archivos

Root posee `GameScene.*`, `WorldState.*`, `WorldRuntime.h` y coordina campos de `ActorState.h`. Dominios independientes usan archivos disjuntos y reportes. Compartir firmas antes de integrar.

### Task 1: sesión y wire (H2 T1)

**Files:** `App/WorldEvent.h`, `ServerSession.*`, nuevos `TeleportHistory.*`, `Protocol/World.*`, tests app/protocol.

**Interfaces:** eventos D3/D7/1C/F304 y `SendWalk`, `RequestGate`, `ConfirmTeleport`, `RequestCrywolfState`, según research04.

- [ ] Fijar ASM de cifrado, orden del sink e historial.
- [ ] RED de cuatro eventos en orden, rechazo1C y deliveries con GS falso.
- [ ] Implementar y ejecutar regresiones; commit por concepto, revisión.

### Task 2: actores (H2 T2/T3/T6/T7)

**Files:** nuevos `Scenes/ActorWalk.*`, `ActorMotion.*`, `PlayerActionSpeeds.*`, `CharacterStep.*`, campos coordinados de ActorState, tests acciones/movimiento.

**Interfaces:** contexto de Walk/Stop con recursos y RNG; MovePath/UpdateCharacterPosition existentes. Velocidades compartidas sin modificar recursos const; actor durable con path/blanco/dirección/freno.

- [ ] ASM de Walk, Stop, posición y reintentos.
- [ ] RED de Run, acciones/velocidades, RNG, celda adelantada y llegada.
- [ ] Implementar adapters y otros actores; verificar safeZone/freno Lorencia y Devias; revisión.

### Task 3: héroe, clic y gates (H2 T3/T4/T5/T10)

**Files:** nuevos `Scenes/GameMovement.*`, GameScene, WorldRuntime, tests movimiento/clic/gates.

**Interfaces:** InputState, rayo/paredes del último dibujo/paso, reloj y ServerSession; mismo CRT durable.

- [ ] ASM de guardas, llegada terreno, cierre propio57cfc0 y caller gates.
- [ ] RED clic→D3 histórico, ventanas, Shift/barra, contador y destino gates.
- [ ] Conectar cabeza→movimiento→gates→clic→cierre→capa; verificar frame retenido.

### Task 4: aplicadores y transiciones (H2 T8/T9/T11–T15)

**Files:** nuevos aplicadores Scenes, SessionEventApplier, WorldEntry y tests.

**Interfaces:** eventos Task1, acciones/caminos Task2 y carga existente. F304 conserva campos propios según ASM, no reutilizar F303 indiscriminadamente.

- [ ] RED D3 héroe/otro/desconocido y D7/shock/vida/RNG; implementar.
- [ ] RED1C mismo/otro mapa, gate0 y F304; implementar prefijos/limpieza/envíos.
- [ ] Reentrada0↔3 y juego→selección→juego con GS falso; revisión.

### Task 5: fauna (P-C55c/H4.1–H4.8)

**Files:** módulos nuevos Scenes de fauna; World/Birds,Fish e interfaces; integración durable por root.

**Interfaces:**40 aves/10 peces estables; servicios de terreno/modelos/luz/CRT y pases de paso/dibujo; carga apaga vivos sin reconstruir registros.

- [ ] Completar ASM de handlers/dibujo/sombra por familias.
- [ ] RED slots/spawn/step/draw/RNG/reentrada; implementar servicios productivos.
- [ ] Integrar paso tras hojas y antes de helpers; dibujo en sus lugares; revisión.

### Task 6: cola del frame (P-C55f)

**Files:** estado/dibujo de ítems del piso, hojas, joints, efectos, cintas, sprites, partículas/daño; orquestación GameScene por root.

**Interfaces:** reutilizar DrawHelpers/DrawModelEffects/DrawFrameSprites/GameParticles con contratos correctos; ampliar hojas por mapa; modo Kalima y segundo pase Devias según ASM.

- [ ] ASM de6374fe hasta fin, inventario APIs y dependencias CL9.3.
- [ ] RED de contratos y errores de kernels encontrados; implementar cada concepto.
- [ ] Conectar fases en orden, verificar frames0/1/varios y prefijos de errores; revisión.

### Task 7: matriz, revisión y publicación

**Files:** tests mapas/familias, research04/06/07/08 y continuación del traspaso.

- [ ] Matriz movimiento/gates/regen por mapas, fronteras explícitas sin evidencia.
- [ ] Build/CTest completo, GL obligatorio, smoke, formato18/include-cleaner18.
- [ ] Astra6 high adversarial; corregir Important/Critical y repetir controles afectados.
- [ ] PRs por concepto y actualizar índice/pendientes reales. VM como fase final.
