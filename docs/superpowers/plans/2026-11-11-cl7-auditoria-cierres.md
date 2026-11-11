# Auditoría adversarial de seguridad de cierres

> Informe trasladado del workspace original para permitir la continuidad desde GitHub.
> Los artefactos auxiliares `.superpowers` citados no están versionados. La
> [evidencia de cierre](2026-11-11-cl7-evidencia-cierre.json) conserva los heads,
> las 67 refs originales observadas, los PR y las verificaciones principales.


Fecha: 2026-11-10. Revisor: Astra, esfuerzo high. Alcance: procedencia y cobertura de los 66 PRs #222–#287 frente a main `7a76c9d` y los seis reemplazos #288–#293. Lectura de artefactos, hashes, diffs y objetos Git; sin builds, tests, replay con escritura, modificaciones remotas ni subagentes. Sólo se escribe este informe.

**Veredicto:** no encontré otro PR de #222–#287 que deba reabrirse por trabajo único de código o documentación sin representación. El cierre de #185 excedió la cobertura acreditada: mantenerlo abierto como draft hasta su propia reconciliación. `CHECKPOINT185-REOPENED.json` registra que ya se corrigió, con su head original intacto. No hay evidencia de borrado de código ni de ramas.

No hay hallazgos P1/P2 pendientes sobre los 66 cierres revisados. Esta conclusión acredita sustitución del lote; no acredita completar Kalima, T16, CL7, QA nativo ni todos los pendientes documentados.

## Cadena comprobada, incluyendo las fuentes originales

1. `SUPERSEDED-PR-CLOSURE.json` contiene exactamente una fila para cada número 222–287: 66 únicos. `CLOSURE-BRANCH-INTEGRITY-AUDIT.json` verifica por API que las 67 ramas originales, incluida #185, mantienen sus SHAs, y que main sigue en `7a76c9d`. La integridad de refs demuestra conservación, no equivalencia funcional.
2. Para #222–#239, `../cl7-resto-traspaso/github-current-state.json` (ruta desde el directorio SDD), `github-current-live.json` y `baseline-recomposition.json` vinculan los heads originales con sus 18 deltas contra main. Trece heads cerrados coinciden directamente; los otros cinco tienen una cadena explícita: cuatro enmiendas #223–#226 en `published-research-remote-proof.json`, y #227 en `proposal-nine-227-live.json` más el objeto local `db55f35`. No se presupuso que sus heads iniciales siguieran vigentes.
3. Los 486 hunks atribuidos a esos 18 PRs forman parte del inventario de 973 hunks y 54 etapas. Contrasté **973/973 cuerpos de hunk y sus cardinalidades** contra los diffs de los árboles `before_tree`/`after_tree` registrados, sin aplicar parches. `hunk-replay-verification.json` conserva el replay histórico hasta `e78b4f2`; `baseline-recomposition.json` explicita las resoluciones de solapamientos, examinadas abajo.
4. Para todos los paths cambiados por cada uno de esos 18 deltas originales, comprobé existencia y blob en `a622`: no falta ningún path. La tabla distingue blobs exactos de archivos evolucionados; existencia por sí sola no se usa como prueba de conservación. `residual-manifest.json`, las fuentes de `STACK-MANIFEST.json` y los hunks conectan las ampliaciones con sus dueños posteriores.
5. Los **48 heads** cerrados #240–#287 coinciden uno por uno con `PUBLISHED-STACK-REMOTE-PROOF.json`; sus **48 árboles** coinciden con `STACK09-FIX-REPLAY-PROOF.json`. Comprobé que los **48 parches corregidos**, sus SHA-256 y sus bytes coinciden con `git diff --binary before_tree tree`. No se sustituyó este cotejo por igualdad del último árbol.
6. `CONSOLIDATION-COVERAGE.json` asigna exactamente una vez las unidades 01–48, más los ocho PRs base. `CONSOLIDATION-REPLAY-PROOF.json` y las pruebas remotas vinculan esa partición con los seis reemplazos. El checkpoint publicado y la prueba vigente conservan `a622e314211384f935addddb0c78c1943e96a271`; la auditoría directa observa #293 en `8414d764`. Los cambios intermedios posteriores no introducen un target diferente.

Todos los nombres de artefactos sin prefijo en este informe pertenecen a `task2-publication/`. La ruta completa del estado inicial es `.superpowers/sdd/cl7-resto-traspaso/github-current-state.json`.

## Los 18 PRs iniciales

«Exactos/evolucionados» compara los blobs del delta original contra main con el target, antes de las cuatro enmiendas documentales. Cada fila conserva todos sus paths; ninguno ausente.

| PR | Hunks inventariados | Blobs exactos / evolucionados | Incorporación o sustitución trazada |
| --- | ---: | ---: | --- |
| #222 | 7 | 2 / 0 | Base de #288; roadmap y plan exactos. |
| #223 | 8 | 5 / 0 | Base de #288; código exacto; enmienda research reconciliada en #293. |
| #224 | 8 | 3 / 2 | Base de #288; owner ampliado en #291; research reconciliado en #293. |
| #225 | 9 | 0 / 5 | Base de #288; pose/partículas y conexiones posteriores en #290/#293. |
| #226 | 9 | 4 / 4 | Base de #288; kernel extendido y consumidores en #290/#293. |
| #227 | 27 | 4 / 6 | Unidad 02 → #288; dibujo y fixture heap; extensiones compartidas posteriores. |
| #228 | 4 | 0 / 2 | Base de #288; registro preservado al resolver MapLoad y conectar transiciones. |
| #229 | 39 | 19 / 5 | Unidades 03/04 → #288; catálogo/recursos; consumidores #291/#293. |
| #230 | 36 | 13 / 3 | Unidad 05 → #288, envíos 06 → #289, recepción 46 → #293. |
| #231 | 50 | 5 / 9 | Unidad 07 → #289; movimiento/owner #291 y conexiones #293. |
| #232 | 47 | 3 / 4 | Unidad 09 → #289; proveedor 10 en el mismo bloque; binding 44 → #293. |
| #233 | 24 | 7 / 3 | Unidad 08 → #289; resolución MapLoad/SessionEventApplier y binding 45/46 → #293. |
| #234 | 59 | 5 / 10 | Unidad 10 → #289; pasadas ampliadas y SceneManager en #293. |
| #235 | 56 | 4 / 4 | Unidad 11 → #289; binding 45 → #293. |
| #236 | 3 | 1 / 2 | Unidad 12 → #289; matriz refinada por movimiento/escenas #292/#293. |
| #237 | 48 | 9 / 0 | Research redistribuido por concepto en #288–#293; los nueve blobs finales son exactos. |
| #238 | 28 | 6 / 1 | Base de #288; prueba de selección ampliada en #293. |
| #239 | 24 | 11 / 1 | Base de #288; ActorState ampliado en #289/#291. |

## Sustituciones intencionales que no son pérdida

- **Solapamientos #232/#233/#234.** El árbol unión conserva el LoginScene completo de #232; el diff respecto del login anterior de #234 incorpora el actor durable, su RNG/cloth y el estado compartido. No son dos implementaciones independientes que debieran mantenerse a la vez. En #233, el diff contra la unión conserva sus cambios de selección y agrega los registros de #228, el clima de #230 y la corrección de posición/ángulo de la torre de #229. La resolución explícita recupera el borrado de partículas, el reset de darkSpirit y el préstamo de cloth/skeleton. No encontré un hunk huérfano en esos conflictos.
- **#223–#226, research de PR aislado.** Parte del texto agregado por las enmiendas no aparece literalmente al final: decía que el consumidor, pose retenida o conexión de escena llegaría en otro PR. `prepare_stack.py` mantiene esas enmiendas durante la pila y la unidad 48 reconcilia sólo documentación con el snapshot integrado. Las secciones finales conservan el contrato y describen los bindings ya incorporados. Es una sustitución de límites transitorios, no omisión de investigación independiente.
- **#237.** Verifiqué los nueve blobs contra `a622`: 9/9 idénticos. Los 113 registros de `research-ownership.json` redistribuyen atribución por concepto; no se necesita conservar un PR documental monolítico. El enlace de cierre a #293 es el extremo de la pila, pero la atribución real abarca #288–#293.
- **Windows.** Entre el snapshot `e78b4f2` y `a622` cambian exactamente los cinco paths portables declarados: FaunaBirdHandlers, test_character_drawing, test_game_gates, test_helper_table y test_world_entry. El fixture heap que llegó en el head cerrado de #227 se conserva; el diff directo de ese fixture contra el target sólo añade las extensiones posteriores de comportamiento/tests.
- **Recortes y correcciones de la pila.** El include World y la recepción aparecen juntos; LoginScene y sus proveedores se agrupan. Las normalizaciones de whitespace, includes todavía sin uso y expectativas intermedias RNG/golden tienen pruebas explícitas y se compensan al final. No se restaura deliberadamente el corte previo defectuoso como supuesto contenido único del PR cerrado.

## Las 48 unidades publicadas después

En cada fila el head cerrado coincide con la prueba remota y el árbol con el replay corregido. «Destino» es el bloque que incorpora su delta; sus dependencias son los bloques anteriores de la pila.

| PR original | Unidad / concepto | Head cerrado | Destino |
| --- | --- | --- | --- |
| #240 | 01 · entry-fixture-heap | `1783798f7e66` | #288 |
| #241 | 02 · player-drawing | `03fba22474fd` | #288 |
| #242 | 03 · special-model-catalog | `475287b3c6af` | #288 |
| #243 | 04 · physical-model-resources | `c607a8c6c36b` | #288 |
| #244 | 05 · weather-0f | `5accd1b15a2b` | #288 |
| #245 | 06 · world-send-history | `889759e0e624` | #289 |
| #246 | 07 · character-step | `a221a803756d` | #289 |
| #247 | 08 · selection-state | `d32701d963e1` | #289 |
| #248 | 09 · login-actor | `4696d75d11a0` | #289 |
| #249 | 10 · initial-game-pass | `fa8d1226e8df` | #289 |
| #250 | 11 · selection-preview | `5dd0bbb0dd7e` | #289 |
| #251 | 12 · map-matrix | `ee544b23b2f0` | #289 |
| #252 | 13 · runtime-layout | `7276491dc862` | #290 |
| #253 | 14 · particle-material | `a5a0f601c119` | #290 |
| #254 | 15 · shared-pose | `92d7691408f8` | #290 |
| #255 | 16 · particle-kernels | `a6a85a46468f` | #290 |
| #256 | 17 · model-effects | `efa9fb726eab` | #290 |
| #257 | 18 · tail-drawing | `e47b3021a7bf` | #290 |
| #258 | 19 · floor-step | `17213f049828` | #290 |
| #259 | 20 · projection | `e3ebc5c4c802` | #290 |
| #260 | 21 · tail-passes | `ce4e2ce0a877` | #290 |
| #261 | 22 · fauna-state | `31d18194f3a3` | #291 |
| #262 | 23 · fauna-step | `a0b19e0a8fa2` | #291 |
| #263 | 24 · fauna-drawing | `7cb82787ca06` | #291 |
| #264 | 25 · fauna-joints | `536871887eec` | #291 |
| #265 | 26 · fauna-effects | `dedd20d7eda0` | #291 |
| #266 | 27 · leaves | `5558311f588d` | #291 |
| #267 | 28 · world-owner | `1ed0ae5529a8` | #291 |
| #268 | 29 · actor-motion | `7f7c45179d75` | #291 |
| #269 | 30 · actor-shock | `c9817aa52386` | #291 |
| #270 | 31 · world-object-step | `4c6aa0c3d967` | #291 |
| #271 | 32 · noria-objects | `403ad1aa08c2` | #291 |
| #272 | 33 · gates | `ec8f12d535ad` | #292 |
| #273 | 34 · hero-facing | `9455c55138dc` | #292 |
| #274 | 35 · hero-movement | `11cef9bae4a4` | #292 |
| #275 | 36 · movement-replies | `34785f8da286` | #292 |
| #276 | 37 · map-binding | `6badfbf2f22c` | #292 |
| #277 | 38 · world-transitions | `b62ebca237c6` | #292 |
| #278 | 39 · weather-resources | `b55d253b518c` | #293 |
| #279 | 40 · framebuffer | `0d2187789617` | #293 |
| #280 | 41 · particle-binding | `d3773e6644f0` | #293 |
| #281 | 42 · object-binding | `a683b47ffad5` | #293 |
| #282 | 43 · character-binding | `ded6b097ac8c` | #293 |
| #283 | 44 · login-binding | `db43295e94f3` | #293 |
| #284 | 45 · selection-binding | `44bf4bfcc552` | #293 |
| #285 | 46 · event-binding | `528d1e63a4fa` | #293 |
| #286 | 47 · scene-integration | `f177b8d06e78` | #293 |
| #287 | 48 · documentation-reconciliation | `e57a11752e89` | #293 |

## Límites y acción

**Reabrir adicionalmente dentro de #222–#287: ninguno identificado.** Mantener #185 abierto: su head `46053fb10ecc2357c57f5b67bca7343ebf4f419e` no figura como fuente de esta reconciliación; que sea un checkpoint viejo de #182/#183 sobre #184 no acredita cobertura. No revisé sus 18 mil líneas ni infiero que se hayan perdido.

El resultado se apoya en los objetos/tree/diffs locales y en los registros de respuestas remotas conservados; no volví a descargar todos los commits remotos ni ejecuté suites. La igualdad final es el último eslabón, complementado por la correspondencia individual de originales, enmiendas, hunks y unidades. No es una nueva revisión funcional completa de todas las líneas ni prueba de ausencia de bugs. Los pendientes funcionales y de evidencia ya declarados permanecen pendientes.
