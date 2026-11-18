# CL9 H1: reparación propia y cierre verificable

> **Para el ejecutor:** aplicar `superpowers:executing-plans` o `superpowers:subagent-driven-development`, TDD y revisión independiente. Cada analista de binario posee una sola función; sólo el coordinador opera VMXP y escribe los contratos compartidos.

**Objetivo:** completar las diferencias propias demostradas de H1 y comparar los casos nativos alcanzables, sin atribuir sus huecos a H9 ni al servicio de amigos.

**Arquitectura:** conservar el pool durable de actores. Las telas deben tener el mismo dueño y ciclo de liberación que el actor, sin un detector aproximado de cambios de clave. El picking consume la cámara y la caja persistida que deja el dibujo, con su orden y precisión comprobados.

**Tecnologías:** C++20, GoogleTest, CMake/CTest, SDL3/GLM y exports ASM/PE c32; VMware/VIX para observación autorizada del original.

**Spec:** [CL9, Hito 1](2026-11-13-ola-cl9-gameplay.md#hito-1-el-emote-y-los-otros-jugadores-cl91-35-kb), research06, P-C152/P-C160..164 y los resultados H1/H9 del 14 y 15 de noviembre.

## Restricciones

- Original c32 SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, PE32 x86, base400000. Verificar módulo y base por sesión.
- El original y sus observadores sólo corren en XP. No escribir memoria, inyectar, suspender sus hilos ni simular tráfico entrante como observación nativa.
- Cliente VMXP en este host; servidor en otra PC. No operar perfiles de servidor ni repetir Agregar, que provoca desconexión en el entorno actual.
- Preservar la reparación H9 ya verificada y su respaldo; código en el worktree aislado. Raw, credenciales, nombres y estado generado fuera de Git.
- Un solo Ninja y máximo dos compiladores/tests. No consultar Ninja mientras otro Ninja escribe sus cachés.
- Los cortes H3/H4/CL10/CL11/H7/H10/H13 mantienen su dueño; cerrar H1 no declara implementadas esas integraciones.

## Foco de revisión

- Baja y alta del mismo slot/clase entre dibujos, incluso con la misma clave; ningún residuo de tela ni reloj.
- Cleanup de mapa con héroe superviviente y slots muertos; sólo los primeros400, no sentinel400.
- Reconstrucción de una clave viva: conservar las telas si esa rama del ASM no las libera.
- Cámara distinta de `ray.origin`, caja retenida después de mover y efectos laterales del picking.
- Comparación de distancia antes de redondear `best` a float, empates y NaN; no presuponer CW x87.
- Acción18 con clave ausente: observar el registro fantasma400 sin cambiar los400 actores publicados.

## 1. Contratos e inventario

- [x] Inventariar fuente/tests de todas las funcionalidades de H1 y separar cortes externos.
- [x] Contrastar `59e700`: tint/E8, exclusión del héroe según escena, caja de48B, origen de distancia y reducción de best.
- [x] Contrastar writer de caja/dibujo y CW efectivo; no implementar una aritmética extendida por suposición.
- [x] Contrastar `4cb750` y el registro fantasma400 antes de cambiar Q-C778.
- [x] Registrar el contrato por función y la evidencia que permite cerrar cada diferencia.

## 2. P-C161: ownership y liberación de telas

**Archivos:** `Scenes/WorldState.{h,cpp}`, `ActorCreation.cpp`, `MapLoad.cpp`, `GameScene.{h,cpp}`; tests de altas, bajas, carga de mapa y telas.

**Interfaz:** `WorldState::ActorCloths()` presta un span estable de `ActorPoolSize` elementos. `GameCharacterPass::cloths` conserva su interfaz; `GameScene` deja de poseer otra historia por slot.

- [x] RED: baja→alta misma clase, alta en slot muerto con tela, cleanup de mapa que conserva héroe y libera muertos; sentinel400 conservado y clave viva conserva tela.
- [x] Mover el dueño al mundo y liberar sólo en los callsites nativos. No usar `revision` ni key/alive como sustituto de esos eventos.
- [x] GREEN: comprobar tela, malla, cuerpo/reloj nuevos y continuidad normal; mantener Draw intermedio sin simulación extra.
- [x] Revisión independiente del ciclo y de los observadores efectivos.

## 3. P-C164 y efectos del picking

**Archivos:** `Scenes/CharacterPicking.{h,cpp}`, `GameUi.{h,cpp}`, `GameSceneUi.cpp`, publicación de bounds en cuerpo/monstruos y sus tests.

- [x] RED: tint escribe E8 sólo sobre actores elegibles; cámara separada; caja persistida contra posición nueva; empate representable y empate cuyo best se redondea hacia arriba.
- [x] Publicar cámara real desde `m_drawnCamera.position` y caja sólo donde el ASM la escribe; preservar ramas sin dibujo y la frontera de proyección.
- [x] Resolver precisión/CW con evidencia; conservar límites de lectura fuera de mapa explícitos y seguros.
- [x] GREEN y revisión independiente: no sustituir el bitmap de pruebas por una certificación universal de rendering.

## 4. Acción18 con clave ausente

**Archivos:** `Scenes/ActionReply.cpp`, `tests/scenes/test_action_reply.cpp`, research Q-C778.

- [x] RED: clave ausente escribe spare400, conserva publicados, respeta caballo oscuro, animación no disponible y efectos parciales ante cortes H3.
- [x] Reproducir el contrato de `4cb750`, sin salida temprana inventada.
- [x] GREEN y contraste por ramas con el ASM.

## 5. VM y verificación final

- [x] H1-E1: repetir casos alcanzables con texto sintético, hash/base/precondiciones y paquetes; distinguir parado/caminando y animación/modelo real.
- [x] H1-E2/E3 y P-C160: buscar mecánicas reales disponibles. Si no hay emisor/actor/skill compatible, registrar la precondición concreta sin fabricar estado.
- [x] Build, suite completa con corpus/GL, formato e includes del delta final. No reutilizar el resultado7074 anterior si cambia el código.
- [x] Revisión independiente y resultado por área: implementado, verificado estático, verificado nativo, pendiente o impedido.
- [x] Archivar nueva evidencia y entregar cambios revisables. No cerrar por un conteo de tests ni por trasladar un pendiente a otro hito.

Resultado y límites: [reparaciones de H1](2026-11-15-cl9-h1-resultado.md) y [contrato por función](../../../Client/docs/research/13-h1-actores-y-picking.md).

## 6. Hallazgo posterior: movimiento del héroe transformado

Emanuel equipó un anillo en XP y pidió comprobar su aparente inmovilidad. El modelo real es0x154,
con7 acciones. ASM de59adf0 y4f37c0 permite ese tipo; caminar usa2. La lectura nativa de flags78
contiene bit20 activo:56acd0 retorna antes de enviarD3 o activar Movement. No se
atribuye el origen del flag al anillo sin una observación separada.

Además apareció un hueco propio: MoveTheHero no presta esqueleto/sonidos al noPlayer, y
SetActorStop todavía corta su rama. La suite7091 corresponde al estado anterior a esta reparación.

Contrato acordado: ActorStopContext agrega al final actorSkeleton (puntero prestado) y modelSounds
(span de WORD signed), con defaults que conservan los callers player. El noPlayer lee celdaY y
luegoX mediante LogicalAccess separados, elige0 salvo172 en TerrainWall&1 (7), aplica SetAction
con su modelo y corre el sufijo de tabla/RNG. El recurso de sonidos conserva el fallback de ceros.

- [x] RED de helper y de integración: clic → D3 → caminar2 → llegada0 del transformado, sin alterar velocidades Player; prefijos y RNG de reposo.
- [x] Portar rama noPlayer de4f2250 y prestar modelos/sonidos en callers alcanzables, conservando fronteras de recursos explícitas.
- [x] Revisión independiente contra contratos por función y verificación final nueva.
- [x] Registrar el diagnóstico XP por gates y separar causa observada, origen del flag y hueco MuRet.

Diagnóstico del anillo: [modelo, gates, A/B y límites del cable](../../../Client/docs/research/14-transformacion-en-vmxp.md).
