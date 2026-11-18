# CL9: traspaso para terminar la ola (actualizado 2026-11-16)

> **Entrega actual: cierre de H3 en [PR #342](https://github.com/ema-ljn/MuRet/pull/342).** #354 y #355
> ya están en `main`. H1 conserva su alcance ASM revisado; H9 mantiene pendientes propios del editor
> y del servicio. H3 incorpora auditoría de continuaciones y un combate normal observado en XP.
> La comparación estricta completa con MuRet y servidor sigue pendiente. Los porcentajes miden citas
> de direcciones, no equivalencia funcional ni porcentaje de pruebas nativas.

El 2026-11-14 Emanuel pidió dejar la CL9 lista para que **otros agentes terminen los PRs que faltan**.
El estado se actualizó el 2026-11-16, con `main` en `985496cd` (#355) y el cierre de #342.
Los porcentajes se reprodujeron sobre la unión de las mismas ramas, sustituyendo la cabeza antigua
de H3 por sus reparaciones: no se ganó ni perdió ninguna fila del inventario fijo. Las notas detalladas de integración más abajo conservan la base
histórica del 14/11, salvo las actualizaciones expresas; no se volvieron a auditar todos los hitos.
Las revisiones pendientes están en [`2026-11-14-cl9-traspaso/`](2026-11-14-cl9-traspaso/).

- Plan: [`2026-11-13-ola-cl9-gameplay.md`](2026-11-13-ola-cl9-gameplay.md), con las tablas de
  [funciones](2026-11-13-ola-cl9-funciones.tsv) y [cortes](2026-11-13-ola-cl9-cortes.tsv).
- Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
- Castle Siege (hitos 12 y 13) conserva su sección y sus pendientes más abajo.

**Por dónde seguir:**

1. Consultar el [resultado de H3](../../../Client/docs/research/15-h3-combate-y-muerte.md): compilación
   completa, 7.335 tests locales sin fallos ni saltos, incluidos 106 GL y 59 de referencias recuperadas.
   El CI de la cabeza publicada y su integración se registran en #342.
2. Continuar con H4 (#343), integrando H3 antes de resolver sus skills, mascotas y efectos pendientes.
3. Después, de a uno, integrar cada hito con su base según la cadena. Los conteos de conflictos
   históricos del resto del documento no se presentan como el estado actual de esas ramas.
4. Conservar los pendientes de editor de H9, integraciones sociales y Castle Siege (12 y 13).
5. Completar la comparación estricta pendiente de MuRet con VM y servidor, incluidos casos especiales.

## Estado por hito

**Cobertura por citas** = bytes de las filas con alguna dirección de su rango citada fuera de una línea
de corte en `Client/src/`, divididos por los bytes de todas las filas del hito. Se toma la unión de
`main`, el #354 y las cabezas de los hitos pendientes; no se suman dos veces los mismos bytes.
Es un indicador estático: una cita no demuestra que todas las ramas estén implementadas, y una función
implementada sin cita puede no contar. Por eso H9 puede tener 100 % de citas y seguir incompleto.

| Hito | Rama / revisión | PR | Cobertura por citas | Estado y siguiente paso |
| --- | --- | --- | ---: | --- |
| 1, emote y otros jugadores | `main` @ `985496cd` | #338, #353, #354 | 100,0 % | implementación propia contrastada con ASM; completar validación estricta en VM |
| 9, amigos y ventanas viejas | `main` @ `985496cd` | #339, #353, #354 | 100,0 % | editor occidental XP reparado; faltan IME completo y funciones del editor de mensaje/susurro; validación del servicio pendiente |
| 5, ítems del piso | `main` | #340 | 95,7 % | PR mergeado; auditoría de filas sin cita e integraciones pendientes |
| 2, monstruos, NPC, misiones | `main` | #341 | 99,7 % | PR mergeado; conservar cortes e integraciones del hito 3 |
| 3, pegar, daño, muerte | cierre de `claude/cl9-hito-3` | #342 | 100,0 % | alcance propio revisado contra ASM, 7.335 tests locales y combate normal en XP; fronteras de otros hitos y comparación estricta documentadas |
| 4, skills, cuervo, mascotas | `claude/cl9-hito-4` @ `6a9dd828` | #343 | 98,1 % | borrador, 7/7 fases; WIP sin tests finales y base hito 3 por integrar |
| 11, eventos | `claude/cl9-hito-11` @ `dae3ff19` | #344 | 94,9 % | borrador, 7/7 fases; integrar base hito 4 y revisar filas sin cita |
| 7, party, comercio, duelo, tiendas | `claude/cl9-hito-7` @ `d73ecbbd` | #345 | 75,7 % | borrador, 7/7 fases; integrar base hito 11 y auditar cobertura restante |
| 8, gremio, alianza, guerra | `claude/cl9-hito-8` @ `5f0a80f7` | #346 | 76,0 % | borrador, 7/7 fases; integrar base hito 7 y auditar cobertura restante |
| 10, chat con amigos y cartas | `claude/cl9-hito-10` @ `21c7b222` | #347 | 100,0 % | borrador, 5/5 fases; integrar base hito 8, H9 y combate; validar con servidor |
| 6, tienda del NPC, almacén, caos | `claude/cl9-hito-6` @ `0d70b071` | #348 | 78,0 % | borrador, fase 6 a medias; terminar arreglos de revisión, EDIT e integración |
| 12, ventanas del asedio | `claude/cl9-hito-12` @ `3ebd97ac` | #349 | 55,6 % | borrador, 1/4 fases; faltan ventanas y revisiones |
| 13, batalla del asedio | `claude/cl9-hito-13` @ `032b83d4` | — | 9,9 % | sin PR, fase 1 a medias; WIP sin compilar |

El total recalculado es **436.380 / 529.535 bytes = 82,4 %**, ponderado por bytes, no por el promedio de
los trece porcentajes. Las reparaciones de H1/H9/H3 no aumentan ese indicador: trabajan sobre rangos
que ya tenían citas. Las 1.141 filas del plan son el denominador; las ramas dentro de funciones de otras
olas, los cortes, la fidelidad del editor y los casos nativos se siguen por sus contratos y pendientes.
La unión incluye trabajo que todavía no entró en `main` y no representa una compilación integrada.

El recálculo usa los patrones `ADDR` y `CUT` de
[`build_roadmap_matrix.py`](../../../Client/docs/research/scripts/tools/build_roadmap_matrix.py),
los archivos `.cpp`/`.h` de `Client/src/` salvo `Input/Key.h`, y los rangos `[va, va + bytes)` del
[TSV por hito](2026-11-13-ola-cl9-funciones.tsv). Se excluyen líneas que coincidan con `CUT`, se
marca cada fila alcanzada por una cita y se suman sus bytes una sola vez entre las revisiones indicadas.
No se cuentan tests ni documentación como implementación. Frente a la foto anterior, se explicita la
selección de cabezas y se muestra un decimal, sin reutilizar los porcentajes redondeados.

Resultados que fijan el estado de H1/H9:

- [H1: reparaciones propias y límites de la validación](2026-11-15-cl9-h1-resultado.md).
- [H9: editor XP, codificación y pendientes propios](2026-11-15-cl9-h1-h9-resultado.md).
- [Runbook para retomar la validación en VM](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl9-vm.md).

Los cinco desbordes de stack de fixtures Windows se corrigieron en #342 y se integró #355,
sin duplicar el arreglo de entrada reservado a la otra sesión. La base `f3c13a72` aprobó
[Linux, Windows y formato](https://github.com/ema-ljn/MuRet/actions/runs/43779698488).
Los controles de la cabeza final se consultan en #342; no se hereda el estado de la base. Los demás PRs de la cadena siguen necesitando
reconciliar sus bases; no heredan el CI del hito anterior.

Resultado de H3: [contratos, regresiones y observación en XP](../../../Client/docs/research/15-h3-combate-y-muerte.md).
El 100 % de H3 es **11.766/11.766 bytes citados**, no ausencia de fronteras. El TSV fijo deja fuera
varios helpers/inline revisados en el cierre; `0x511070`, por ejemplo, fue reclasificado después de
congelar esa tabla. No se cambió el denominador para presentar esos arreglos como un aumento.

**Hitos 6, 7 y 8:** las fases y los informes previos no cierran la auditoría de sus filas sin cita. En el
7 y el 8 todavía representan alrededor del 24 % de los bytes. Hay contenedores reemplazados por STL y
helpers de ventanas viejas que deben revisarse individualmente antes de dar los hitos por cerrados.

Las ramas `claude/cl9-hito-N-*` (`-body`, `-fixa`, `-helpa`, `-step`, `-chaos`, `-sign`…) son respaldos
de fases: no se incluyen en este recálculo ni hace falta modificarlas.

## La pila y el orden de merge

Los hitos van en una sola cadena apilada, en este orden:

`main` ← #342 (3) ← #343 (4) ← #344 (11) ← #345 (7) ← #346 (8) ← #347 (10) ←
#348 (6) ← #349 (12) ← hito 13

- **Emanuel mergea** (squash) cuando un PR está listo y en verde. El agente no mergea: le avisa.
- Cuando el PR de abajo entra, GitHub cambia solo la base del siguiente a `main` (las ramas se borran solas
  al mergear). Pasó con el #340 después del #338 y con el #341 después del #340.
- **Nunca** force push, rebase, amend ni squash de commits ya pusheados: el auto-mode los bloquea y la rama
  remota es el respaldo. Siempre commits nuevos y merges.

### Actualizar una rama cuando su base entró por squash

**Caso A: la rama ya tiene mergeada la última versión de su base** (así entraron el #340 y el #341). El squash trae el
mismo contenido, pero git ve conflictos porque compara contra la base vieja. Se resuelve mergeando con la
cabeza de la base como base del merge:

```bash
git fetch origin main
T=$(git merge-tree --write-tree --merge-base=<cabeza de la base que entró> HEAD origin/main)
git merge --no-commit origin/main      # deja MERGE_HEAD; los conflictos no importan
git read-tree --reset -u "$T"
git commit                             # merge con dos padres
# Comprobación: el diff contra main es el del hito, nada más
diff <(git diff origin/main HEAD) <(git diff <cabeza de la base> HEAD^1)
```

Así se resolvieron el #340 en `115b0422` (base del merge `dbb77e7b`, la última cabeza del #338) y el #341 en
`92231db2` (base del merge `e95bca00`). El código queda idéntico al que ya estaba en verde.

**Caso B: la rama todavía no mergeó la última versión de su base** (del #343 en adelante). Cada rama trae
versiones viejas de los hitos de abajo, por eso los conflictos. Hay que:

1. Mergear la última cabeza de la base (`git merge --no-ff origin/claude/cl9-hito-<base>`) y resolver. Si la
   base ya entró, su rama se borró, pero la cabeza sigue en el PR: `git fetch origin pull/<N>/head`. Después,
   el caso A contra `main` con esa cabeza como base del merge. Mergear `main` directo da muchos más
   conflictos (en el hito 3 eran 83 contra 19).
2. Cablear lo que el PR lista en "Al armar la cadena" (más abajo, por hito), en commits aparte del merge.
3. Correr los chequeos locales y el CI, y escribir el texto final del PR con las secciones del #341: Qué
   hace, Lo que corta, Revisiones, Research y Pruebas. El texto del #342 muestra cómo contar el merge.
4. Pasarlo a listo y avisarle a Emanuel.

Así se hizo el #342: `123eed83` mergea el hito 2 final (`92231db2`, 19 archivos) y `de674ec6` es el caso A
contra `main`.

Se puede adelantar el paso 1 contra la rama de la base antes de que entre, pero después hay que repetir el
caso A cuando la base entre por squash. Por ejemplo, el hito 4 puede mergear hoy `de674ec6`; si el #342 entra
después, el hito 4 hace el caso A con `de674ec6` como base del merge.

### Cómo resolver un merge de la cadena

Emanuel pidió que el merge resuelva **solo los conflictos**. Lo demás (cablear, arreglos pendientes) va en
commits aparte o en otra tanda.

1. El código de los hitos de abajo es el final, tal como está en la cabeza de la base: pasó sus revisiones y
   su CI. Donde choca una copia vieja que trae la rama con la versión de la base, gana la base.
2. Lo propio del hito se conserva encima. Para separarlo: `git log --no-merges <merge-base>..HEAD` (los commits
   `CL9 hito <N>: …`) y `git diff <merge-base> HEAD -- <archivo>`. Si los dos lados cambiaron lo mismo, el
   hito se adapta a las interfaces finales de la base (nombres, firmas, parámetros nuevos) sin deshacer sus
   arreglos.
3. Docs (04, 06, 07, 08, el plan): quedan las filas y secciones de los dos lados. No se pierde ningún id, y
   si los dos editaron la misma fila se unen los textos.
4. Nunca `git checkout --theirs/--ours` de un archivo entero: hunk por hunk.
5. Compilar `ClientLib` y correr los tests del hito, los de los archivos en conflicto y los vecinos. Después
   clang-format, include-cleaner y el chequeo de estrechamiento de MSVC sobre lo que se tocó.
6. Al final, `git merge-tree --write-tree origin/main HEAD` tiene que salir limpio.

## Qué le falta a cada hito

### Hito 3 (#342)

- Se integraron H1/H2/H5 y #354/#355, resolviendo los conflictos por contrato. El arreglo de entrada
  realizado por la otra sesión se incorporó mediante `main`.
- Se revisaron los cortes falsos CL11: terremoto FA, E2, brillo A25, burst/scatter, chispas de golpe y
  las continuaciones con capacidad agotada. Un constructor realmente pendiente con espacio sigue
  cortando en su primera CALL, conservando el prefijo del original.
- `+0x3a4` tiene un único campo. El dibujo publica la colocación real, y el cuerpo publica el snapshot
  antes de caída/muerte. Los puntos de hueso y el cuervo consumen el snapshot que les corresponde.
- Se revisaron sonido, muerte, expiración y liberación de recursos. La reacción al golpe se integra con
  la tabla canónica usada por ataque/muerte; las demás lecturas heredadas de H1/H2 se conservan.
- XP/daño/muerte normales se observaron en el original de XP, con captura correlacionada y una
  regresión sintética del orden 17→16 sin shock. No sustituye la comparación completa de MuRet en vivo.
- Permanecen las fronteras de H4/CL11, nombres de gremio de H8 (Q-C1170), altar de Crywolf de H11,
  audio CL10, offset no inicializado Q-C1050 y estado residual del 17 corto Q-C1161. El informe detalla
  cada límite y la evidencia que falta; no se cierran por tener todos los tests locales en verde.
- Verificación local final: 7.335/7.335, sin fallos ni saltos. El estado remoto y el merge quedan
  registrados en #342. Emanuel autorizó expresamente al agente a mergearlo después de los controles
  de esta sesión; la regla general de la cadena no se amplía.

### Hito 4 (#343)

1. **El merge con el hito 3** (`de674ec6`, ya con los hitos 1, 5 y 2 finales y `main`): 26 archivos en
   conflicto. Son las docs 06, 07 y 08, `WorldEvent.h`, `ActorState.h`, `ArrowEffects.*`, `AttackStep.cpp`,
   `GameHud.cpp`, `GameMovement.h`, `GameMovementState.h`, `GamePanels.*`, `GameParticles.*`,
   `GameSceneLogic.cpp`, `GameWindowDrawing.cpp`, `HeroAttack.cpp`, `TextureSets.h`,
   `World/ParticleDrawing.cpp`, `World/Particles.*` y los tests `test_arrow_effects`, `test_character_step`,
   `test_game_ui` y `test_session_event_applier`.
   - En `HeroAttack.cpp`, `WalkIntoRange` queda con el cuerpo del hito 3 (con `CheckTheCrywolfAltar`) y la
     estructura del hito 4, que movió el cuerpo de `IsAttackable` a un ayudante compartido.
   - El último commit (`6a9dd828`) es un WIP cuyos tests nunca se corrieron. Si después del merge falla un
     test, hay que ver primero si ya fallaba en `6a9dd828`.
   - Un primer intento de este merge se frenó a pedido de Emanuel y se descartó: la rama está intacta.
2. **Terminar el arreglo CL11** del commit WIP `6a9dd828`: compila, pero los tests no se corrieron. Ya
   porta la Flame `0x500531` con `0x52ae2a`, los `0x5ac` /2 y /1, las banderas del hito 1 (`0x4e6725`,
   `0x4e676e`), el calco `0x61e`/3, las `rand()` del Inferno antes del corte `0x51fe9e`, el cuervo `0x5ac`/6
   y los joints `0x60f`/0 con llave 2. Siguen cortando `0x1b` (`0x504561`) y `0x4dbfc5`. Usa Q-C1131..Q-C1133
   y P-C360.
   - Tests a correr: `test_skill`, `test_skill_replies`, `test_skill_hits`, `test_skill_shine_particles`,
     `test_hero_skills`, `test_hero_skills_wizard`, `test_attack_skill_cases`, `test_attack_frame_skills`,
     `test_attack_step`, `test_raven_orders`, `test_raven_attack`, `test_pet_stats`, `test_pet_info`,
     `test_character_step`, `test_server_session`, `test_game_character_effects`, `test_model_effects`,
     `test_game_model_effects`, `test_viewport_players`, `test_monster_viewport`, `test_viewport`,
     `test_full_set_joints`, `test_helper_table`, `test_game_joint_step`, `test_hanging_joints`,
     `test_kill_beam_joints`, `test_game_characters`, `test_dark_spirit`, `test_game_scene`,
     `test_game_scene_viewports` y `test_game_scene_tail_integration`.
   - El `0x0e` llama a `BurstAt` del hito 3 (P-C360), y `ApplyEffectFlags` toma la tabla de efectos.
3. Al armar la cadena:
   - `HeroAttack.cpp` `WalkIntoRange` queda con el cuerpo del hito 3 y `CheckTheCrywolfAltar`. El chequeo del
     altar probablemente también va en los `PathFinding2` del hito 4.
   - Reconciliar P-C256 con Q-C1179 del hito 3.
   - Cablear los cortes de la flecha del hito 3.
   - Las pociones con el hito 5, los nombres del grupo (`0x58b06e`, `0x59272c`) con el hito 7.
   - Un solo `DrawInBox` (el del hito 2, misiones, y el del hito 4, mascota).
   - `OwnSkillIndex` duplica `0x59fb80` de `AttackStep.cpp`: unificar.

### Hito 11 (#344)

Terminado (`0ecc9761` más el arreglo chico `dae3ff19`). Al armar la cadena:

- La tabla de nombres de NPC del hito 2 (`0x54e7f0`, `0x54e8d0`, P-C174) va a `GameHudContext::npcName` en
  `GameSceneUi`; hasta entonces el `94` corta en el dibujo siguiente (P-C248).
- Reemplaza el corte del altar `0x4eac41` del hito 3 y el subconjunto de ítems de misión de `0x5b1c70` del
  hito 2.
- `0x402e50` (`CloseTheJewelWindow`) lo cablea el hito 6. La tabla de funciones pone `0x402e50` en el hito
  11, pero el corte va al 6.
- `eventNpcClosed` (hito 11) se une con `npcWindowClosing` (hito 6). `cursorWarpRequest` (hito 11) y
  `cursorWarpRequested` (hito 6) necesitan quien los consuma en la plataforma.
- `0x56b6a9` (`SetText` en los dos EDIT) con el hito 8.
- Quedó un desacuerdo documentado en `CrywolfContract.cpp:105` (`0x435b39` solo se alcanza desde
  `0x567ba4` con altar 0..4): quedó como estaba, con un comentario.

### Hito 7 (#345)

Terminado (`d73ecbbd`). Al armar la cadena:

- Un solo estado del duelo: `0x558bb0` lee `SocialState::duel` (P-C206 b), en vez de la copia de
  `attackRules` del hito 3.
- Un solo `FormatZen`: el de `Scenes/ZenText.h` (hito 2, `0x5b1130`). `ItemTooltipShopPrice.cpp` apunta ahí
  y se borra el de `GameInventoryHud.h`. El renombre del hito 6 `CheckTheShops` → `WriteTheShopPrice` choca
  en las últimas tres líneas.
- El EDIT de `[0x5763504]` lo porta el hito 6 (P-C368); después se cablean los cortes `0x630876`,
  `0x4b54f3`, `0x4b558a` y `0x4b55bc` (P-C362).
- Los nombres de la party del hito 4. Los hitos 6 y 8 portan su "no" de la tabla de P-C363.

### Hito 8 (#346)

Terminado (`5f0a80f7`). Al armar la cadena: `0x56b6a9` con el hito 11 y los nombres de gremio del hito 3.
El hito 12 unifica `GuildName`, `0x6600d0` y el `B2 1F` con este hito.

### Hito 10 (#347)

Terminado (`21c7b222`). Al armar la cadena:

- La acción `0x78` (P-C219) llama a `SetActorAttack` del hito 3 (hecho en la auditoría del 2026-11-18: antes, un
  clic en "anterior" de la ventana de escribir llegaba a un corte).
- Otras llamadas a `LatchTheLeftButton` toman un cuarto argumento; `GameHudContext` tiene `oldWindows`.
- Con el hito 4, la ayuda del botón de amigos (`0x5a9c98..0x5a9dd3`) corre justo antes de los íconos.

### Hito 6 (#348)

Pausado en la fase 6 (las revisiones y sus arreglos). Las dos revisiones independientes están hechas:
[A](2026-11-14-cl9-traspaso/hito6-revision-A.md) y [B](2026-11-14-cl9-traspaso/hito6-revision-B.md).

1. **Arreglos de la revisión A**: F1 (`a9ec131b`), F2 (`10ddc1d6`), F4 (`f519f274`), I2 e I4 (`0d70b071`,
   solo chequeados en sintaxis). Faltan:
   - F3: `PlaceInGrid` descarta fuera de la grilla, en vez de cerrar la sesión, desde los `31`, `32`, `86`
     y los `24` con 2, 3 y 5;
   - I1 (documentar) e I3 (el rótulo del corte del OK del `0x91`, cerca de `GameDialogSteps.cpp:860`).
2. **Arreglos de la revisión B**: F1 a F6, ninguno hecho (las texturas `0x121..0x125` y `0x646`, el `2` de
   Devil Square, `WarehouseFee`, P-C368, más mapas en los tests, `rightReleased`).
3. **Portar el EDIT `[0x5763504]`** según P-C368 y la F4 de la revisión B.
4. Chequeos completos: los tests corridos fueron `test_npc_windows`, `test_npc_window_replies`,
   `test_cursor_item`, `test_npc_window_steps` y parte de `test_server_session`; faltan MSVC e include-cleaner.
5. Decidir el dueño de `0x5d8d50` tipos 4 y 5 (`0x5d9b82`, `0x5d9c75`). `0x5e74f0` es la decisión 8 del plan
   (va a la auditoría de cierre).
6. Al armar la cadena: `0x402e50` del hito 11; el clic derecho con el almacén (`0x5c8e8f`) y los diálogos
   `0x63330f`/`0x6282dc` del hito 5; el `26` del `0xa9` (`0x629c85`, P-C193); `0x6270ed`, el tipo 5 y los
   campos del `0x91` (P-C339) y `0x5c1d90` comercio/tienda al equipo (P-C367) con el hito 7; `0x421d90` del
   hito 4; `FormatZen` reemplaza el corte `0x422a55` del hito 4; `JewelMixState::bundles.rows`.

Ids reservados sin usar: Q-C1154..Q-C1159, P-C380..P-C382.

### Hito 12 (#349) y hito 13

Ver la sección de Castle Siege.

## Castle Siege (hitos 12 y 13) y los filtros de seguridad

**Qué pasó.** El 2026-11-13 a las ~14:23Z, el primer agente del hito 12 ("las ventanas del asedio") se cortó
por un error de la API: los filtros de seguridad del modelo marcaron un mensaje de la conversación (el error
mismo dice que a veces pasa con conversaciones normales). Lo que dejó se guardó como `167f89ed`, un WIP sin
compilar: `Protocol/CastleSiege.*`, `Scenes/CastleSiege.h`, `UI/CastleSiegeState.*` y ediciones en
`PanelRegistry`. Emanuel decidió dejar el hito para el final de la ola.

Después, el 2026-11-14 entre las 00:12Z y las 00:29Z, otro agente hizo la fase 1 del hito 12 sin problemas.
El hito 13 arrancó a las 00:40Z y se **pausó por el límite de uso** a las 01:10Z, no por seguridad.

**Para seguir.** El trabajo es portar las ventanas, los paquetes y el HUD del evento Castle Siege del juego.
Conviene que el brief lo diga desde el principio. Si un agente se vuelve a cortar, se retoma en una sesión
nueva desde lo último commiteado, con piezas chicas (una ventana o un paquete por vez), o con otro agente.

### Hito 12 (#349): las ventanas del asedio

- Rama `claude/cl9-hito-12`, base `main` en `3495be9` (el plan), sin los hitos de abajo. El PR apunta al
  hito 6 y su texto quedó viejo (dice fase 0 de 4).
- **Fase 1 de 4, hecha:**
  - `98c4e320`: los paquetes del castillo (`B2` y su segundo nivel `0x4df1a0`/`0x4df080`, `B3`..`B6`,
    `B8`) y lo que mandan las ventanas;
  - `34faa44f`: el research (04, 07, 08) y el plan;
  - `aab26c8c` y `3ebd97ac`: las respuestas del diálogo del Castle Senior (`0x6623e0..0x667a40`).
- **Fase 2, las ventanas** (sin empezar): los pasos `0x6691c0`, `0x663b20` y `0x666190`; el dibujo; los
  paneles `0x64aca0`, `0x661090` y `0x6485f0`; `0x648a30`, `0x64d350` y `0x661310`; las listas `B4`/`B5`.
  Ver el ítem "Las ventanas" del hito 12 en el plan y P-C300.
- **Fase 3 y 4**: la revisión independiente contra el ASM y sus arreglos.
- **Unificar**: con el hito 8, `GuildName`, `0x6600d0` y el `B2 1F`; el byte 372 que el hito 1 ya tiene; un
  solo `RegisterCastleSiegeHandlers`.
- Ids: el bloque del hito es Q-C1360..Q-C1399 y P-C300..P-C309. Ya se usaron Q-C1360..Q-C1363 y P-C300..P-C303.

### Hito 13: la batalla del asedio

- Rama `claude/cl9-hito-13`, sale de la del hito 12 en `34faa44f`. Sin PR.
- **Hecho y probado:**
  - `c2b682c2`: los lectores `B9 01/02/03/05`, `BA`, `BB`, `B2 20`, `B1 00/01`;
  - `9b27532d`: `CastleBattleReceive`, `CastleBattleState`, `RegisterCastleBattleHandlers`;
  - `04b9a79d`: el HUD de quién tiene los interruptores (`0x5a6fc0`).
- **WIP sin compilar `032b83d4`**: el flujo del `B1` en la sesión (`OnMapServerMove`,
  `OnGameHelloAfterLogin`), `MapServerMoveApply`, `ResetForTheSelection` (`0x4c2480`) y `WriteMacroFile`.
- **Para retomar:**
  1. Compilar y correr `test_server_session`, `test_castle_battle_receive`, `test_game_hud`,
     `test_panel_registry` y `test_game_ui`; MSVC e include-cleaner (sobre todo `GameSceneUi.cpp`).
  2. Docs: 04 "Castle Siege, la batalla", 07 Q-C1400..Q-C1403, 08 P-C372, y achicar P-C302; el plan.
  3. Fase 2 (sin empezar): los cortes de la CL7 en el mapa 30 (`0x4294b0`, `0x42b00b`, `0x42b5f8`,
     `0x412193`, `0x4140d5`).
  4. La revisión contra el ASM y sus arreglos. Después, abrir el PR sobre el hito 12.
- Cadena: el `0x14` llama a `ForgetTheSwitchHolder` (`0x4ca799`); `0x4c9c60` del hito 2 escribe el byte 93 en
  `0x4c9ee9`; `0x447ae0` es del hito 8; `0x200000` del hito 12 quita el corte del `B9 03`.
- Ids del bloque: Q-C1400..Q-C1419 y P-C372..P-C379.
- El GameServer 0.97 del research no tiene Castle Siege: el oráculo es el ASM del cliente. El servidor de
  prueba lo trae apagado (`CityWar`); ver el plan de la ola, § Restricciones y la lista para la VM.

## Cómo se trabaja

### El proceso (Emanuel: lo más rápido posible, sin sacrificar el proceso)

1. **CL9.R primero**: cada paquete contra la función del GameServer 0.97 que lo arma o lo lee. Los layouts van
   en el research 04 y las diferencias en el 08, antes de portar.
2. **Fidelidad al ASM**: el `.asm` es la autoridad, el `.c` de Ghidra es una guía. Cada `rand()` de un camino
   portado se tira en su orden original. Se cita cada función o rama portada por su dirección. Las
   afirmaciones se etiquetan *estático*, *inferido*, *verificado* o *emulado*.
3. **Cortes**: lo que es de otra ola u otro hito queda como corte con su dirección y su destino, y cada corte
   nuevo tiene su fila en `Client/docs/research/08-pendientes.md`. Los sonidos (`0x41ed10`, CL10) no cortan:
   se anotan en `WorldRuntime::lastSoundId`. Lo que es de la CL11 (efectos, joints, partículas que MuRet no
   modela) corta y cierra el cliente hasta la CL11 (decisión de Emanuel del 2026-11-13).
4. **Lecciones de las revisiones**:
   - un corte salta solo con la condición exacta del original, después de los `rand()` que tira antes;
   - una clase de efecto, partícula o joint que MuRet ya crea no es CL11: se porta;
   - un handler de paquete nunca cierra la sesión con una entrada que el original tolera (paquete corto,
     clave desconocida, índice fuera de tabla, celda fuera de la grilla): hace lo que hace el original donde
     MuRet modela esa memoria y descarta el resto con un log sin texto de usuario (Q-C174, Q-C778).
5. **Todos los mapas**: los casos por mapa portados o cortados con dirección; tests en Lorencia (0), Devias
   (2) y el mapa del caso.
6. **Privacidad**: ningún texto de chat, cuenta, personaje ni gremio real en tests, logs o vectores.
7. **Revisión independiente** contra el ASM de cada hito antes de pasar el PR a listo, de un revisor que no
   escribió el código. Al final de la ola, la auditoría de cierre y la lista para la VM.

### Fuentes

- El ASM y el pseudocódigo por función:
  `client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/<addr8>_FUN_<addr8>.asm` (y `.c`) del repo de
  research `EmaNLJN/mu-server-repack-research-102i`. El binario original está en `.../original/main.exe`
  (base `0x400000`).
- El GameServer 0.97 reconstruido: `gameserver/src/reconstructed/win32-vc6/` del mismo repo.
- El research de MuRet: `Client/docs/research/` (04 protocolo, 06 juego y ventanas, 07, 08, 09, 10).

### Compilar y chequear

- Build normal: `cmake --preset linux-x64-debug`, `cmake --build --preset linux-x64-debug` y `ctest`
  (ver [`AGENTS.md`](../../../AGENTS.md) y [`Client/README.md`](../../../Client/README.md)). En un
  contenedor sin SDL3 se puede compilar `ClientLib` y los tests con `-DMURET_WARNINGS_AS_ERRORS=ON` y
  `-DMURET_PRECOMPILED_HEADERS=OFF`, compilando solo los targets `test_<archivo>` que hacen falta.
- `clang-format-18 -i` sobre cada archivo cambiado.
- `clang-tidy-18 -p <build> --checks=-*,misc-include-cleaner` sobre cada `.cpp` y `.h` cambiado, y sobre los que
  usan un símbolo que se movió de header (el CI lo corre sobre todo `Client/`).
- MSVC rompe en Debug cosas que GCC acepta:
  - el estrechamiento (C4242) en templates de la std: correr `clang++-18 -fsyntax-only -Wsystem-headers
    -Wno-everything -Wimplicit-int-conversion -Wimplicit-float-conversion -Wfloat-conversion` con el comando
    de `compile_commands.json`;
  - los iteradores de `std::array` y `std::vector` no son punteros (`auto`, no `auto*`);
  - `v.assign(n, v.front())` aborta (el valor no puede ser una referencia al contenedor);
  - un `WorldState` (~350 KB) en el stack de un test revienta el stack de 1 MB de MSVC: va en el heap.
  Un build con `-D_GLIBCXX_ASSERTIONS` ayuda a encontrar algunos de estos antes del CI.
- El CI de GitHub (Linux GCC Debug, Windows MSVC Debug y formato) es el juez final.

### Git y PRs

- Commits en castellano, con el asunto `CL9 hito N: …`.
- PRs: uno por hito, titulado `CL9 hito N: …`, con el texto del #341 como modelo.
- Ids de docs: antes de usar un Q-C o un P-C, buscarlo en todas las ramas `claude/cl9-hito-*`. Los libres
  seguros son Q-C1420 en adelante y P-C386 en adelante, además de los reservados que se listan arriba.

## Lo que queda para el final de la ola

- **La auditoría de cierre**: cada fila de la tabla de funciones con su port, su corte con dirección o su
  razón (las del 6, 7 y 8 primero); los cortes de `-cortes.tsv` reemplazados; las decisiones abiertas del
  plan (la 8, `0x5e74f0`).
- **La lista para la VM**: lo que cada PR dejó "para la VM" (por ejemplo, levantar y tirar con el cable
  abierto y el nombre con Alt, del hito 5).
- **Integraciones sueltas**: un solo `DrawInBox`, un solo `FormatZen`, un solo estado del duelo, y los
  consumidores de `cursorWarpRequest`/`cursorWarpRequested`.
