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

- [x] Fijar ASM de cifrado, orden del sink e historial.
- [x] RED de cuatro eventos en orden, rechazo1C y deliveries con GS falso.
- [x] Implementar y ejecutar regresiones; revisión y publicación en #230.

### Task 2: actores (H2 T2/T3/T6/T7)

**Files:** nuevos `Scenes/ActorWalk.*`, `ActorMotion.*`, `PlayerActionSpeeds.*`, `CharacterStep.*`, campos coordinados de ActorState, tests acciones/movimiento.

**Interfaces:** contexto de Walk/Stop con recursos y RNG; MovePath/UpdateCharacterPosition existentes. Velocidades compartidas sin modificar recursos const; actor durable con path/blanco/dirección/freno.

- [x] ASM de Walk, Stop, posición y reintentos.
- [x] RED de Run, acciones/velocidades, RNG, celda adelantada y llegada.
- [x] Implementar adapters y otros actores; verificar safeZone/freno Lorencia y Devias; revisión.

### Task 3: héroe, clic y gates (H2 T3/T4/T5/T10)

**Files:** nuevos `Scenes/GameMovement.*`, GameScene, WorldRuntime, tests movimiento/clic/gates.

**Interfaces:** InputState, rayo/paredes del último dibujo/paso, reloj y ServerSession; mismo CRT durable.

- [x] ASM de guardas, llegada terreno, cierre propio57cfc0 y caller gates.
- [x] RED clic→D3 histórico, ventanas, Shift/barra, contador y destino gates.
- [x] Conectar cabeza→movimiento→gates→clic→cierre→capa; verificar frame retenido.

### Task 4: aplicadores y transiciones (H2 T8/T9/T11–T15)

**Files:** nuevos aplicadores Scenes, SessionEventApplier, WorldEntry y tests.

**Interfaces:** eventos Task1, acciones/caminos Task2 y carga existente. F304 conserva campos propios según ASM, no reutilizar F303 indiscriminadamente.

- [x] RED D3 héroe/otro/desconocido y D7/shock/vida/RNG; implementar.
- [x] RED1C mismo/otro mapa, gate0 y F304; implementar prefijos/limpieza/envíos.
- [x] Reentrada0↔3 y juego→selección→juego con GS falso; revisión.

### Task 5: fauna (P-C55c/H4.1–H4.8)

**Files:** módulos nuevos Scenes de fauna; World/Birds,Fish e interfaces; integración durable por root.

**Interfaces:**40 aves/10 peces estables; servicios de terreno/modelos/luz/CRT y pases de paso/dibujo; carga apaga vivos sin reconstruir registros.

- [ ] Completar ASM de handlers/dibujo/sombra por familias.
- [x] RED slots/spawn/step/draw/RNG/reentrada; implementar servicios productivos.
- [x] Integrar paso tras hojas y antes de helpers; dibujo en sus lugares; revisión.

### Task 6: cola del frame (P-C55f)

**Files:** estado/dibujo de ítems del piso, hojas, joints, efectos, cintas, sprites, partículas/daño; orquestación GameScene por root.

**Interfaces:** reutilizar DrawHelpers/DrawModelEffects/DrawFrameSprites/GameParticles con contratos correctos; ampliar hojas por mapa; modo Kalima y segundo pase Devias según ASM.

- [x] ASM de6374fe hasta fin, inventario APIs y dependencias CL9.3.
- [x] RED→GREEN de ramas portadas, errores/prefijos y recursos físicos.
- [ ] Completar familias/opciones del piso y productores/ramas CL9–CL11; backend Kalima.
- [x] Conectar fases en orden, verificar cuadros originales/intermedios y prefijos; revisión.

### Task 7: matriz, revisión y publicación

**Files:** tests mapas/familias, research04/06/07/08 y continuación del traspaso.

- [x] Matriz movimiento/gates/regen por mapas, fronteras explícitas sin evidencia.
- [x] Build/CTest completo, GL obligatorio, smoke, formato18/include-cleaner18.
- [x] Astra 6 high adversarial; corregir Important/Critical y repetir controles afectados.
- [ ] PRs por concepto y actualizar índice/pendientes reales. VM como fase final.

## Evidencia de integración y límites

Verificación posterior al último fix: **4.451 registrados, 4.380 pasados, 71 omitidos por evidencia
externa y cero fallos**. Los 96 casos GL obligatorios pasan también por separado, sin omitidos.
Cuatro arranques verifican VSync on/adaptive, modo fiel y carga de Data hasta Login con fuente
TrueType explícita en Linux; no acreditan autenticación contra un GS original.

Formato 18 sobre 223 archivos C++; include-cleaner18 limpio en la pasada anterior y los31
archivos del delta tardío. Hashes de esos 223 archivos coinciden con el tip local y el árbol
verificado. La adaptación de dos fixtures se fundamenta en las cinco texturas nuevas del login
y los uniforms de inicialización620db0 antes de los helpers de cola.

`test_game_scene_complete.cpp` cubre F3030/2/3, cuadros intermedios, espera con lógica activa
y1C0→3→0 conservando escena/personaje. `test_game_scene_wait.cpp` carga terreno54 real antes
de F303. La matriz0–36 acredita kernels y fronteras, sin declarar completos otros loaders.

Astra 6 high revisó wire, movimiento, fauna, cola y el delta final. Se corrigieron todos los
Important reportados. La última revisión estática no encontró otro Important; la ejecución de
pruebas corresponde a root. Se corrigió el préstamo de huesos1/4 de la fuente, la carga64..68,
el atlas1 y recursos de joints. La segunda capa Kalima tiene orquestador con consulta723c48,
orden y modos; faltan su loader/backend y el préstamo acreditado del modelo firmado−1.

Publicados #238 (Skill.bmd) y #239 (caminar); actualizados por fast-forward #229 (recursos) y #230 (wire).
Los PR anteriores siguen sin merge. El resto del conjunto se conserva por concepto en la rama
`codex/cl7b-integracion-validada`, con índice y dependencias en #237; necesita extracción y
comprobación aislada antes de convertirse en nuevos PR listos.

Siguen abiertos T16 universal, loaders/eventos especiales, ramas/productores CL9–CL11 y
observación nativa del perfil numérico/pose/CRT. La VM continúa al final por pedido de Emanuel.

El CI Windows detectó alias en el fixture de fauna; se copian los valores antes de reasignar
el vector. Sus cinco tests focales y la suite Linux completa se volvieron a ejecutar.
