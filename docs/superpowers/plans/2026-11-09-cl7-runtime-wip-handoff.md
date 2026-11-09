# CL7: checkpoint WIP de mundo, Leaf y runtime Particle

Emanuel pidió detener la ejecución y publicar el estado actual para que otro agente continúe.
El worker fue detenido de forma confirmada y no debe reabrirse automáticamente. Este checkpoint
conserva código y tests tal como quedaron al corte; no es una entrega aprobada ni cierre de CL7.
No se ejecutaron nuevas pruebas ni se corrigieron algoritmos después de la detención.

## Cómo retomar

```sh
git fetch origin
git switch --create cl7-continuation --track origin/EmaNLJN/cl7-pub-leaf-runtime
```

La rama parte de main `4a4fd49709cefea42dda61887a3b2ecff76d65b2` y contiene la composición
actual de 231 archivos de código/tests/configuración, más este handoff. Se publica junta como
checkpoint de continuidad por pedido del usuario; todavía falta separar y revisar sus capas.
Conserva únicamente fuentes del proyecto y tests, sin caches, logs, evidencias privadas, dumps,
assets nuevos, credenciales ni configuración de máquina.

## Contenido y dependencias

Incluye el sustrato canónico de mundo/recursos/actores/sesión requerido por la escena, APIs de
objetos y movimiento, paso/dibujo Leaf, reloj durable, metadata física raw32 y la integración
Step/Draw de partículas en SceneContext/GameScene/Main. Las funciones auxiliares y sus pruebas
son las ya implementadas; no se abrió ningún cuerpo nuevo para preparar este checkpoint.

Hay código compartido con estos PRs públicos: [163](https://github.com/ema-ljn/MuRet/pull/163),
[165](https://github.com/ema-ljn/MuRet/pull/165), [167](https://github.com/ema-ljn/MuRet/pull/167),
[168](https://github.com/ema-ljn/MuRet/pull/168), [172](https://github.com/ema-ljn/MuRet/pull/172),
[174](https://github.com/ema-ljn/MuRet/pull/174), [178](https://github.com/ema-ljn/MuRet/pull/178),
[180](https://github.com/ema-ljn/MuRet/pull/180) y [181](https://github.com/ema-ljn/MuRet/pull/181).
No fusionar todos automáticamente: reconciliar preimágenes, APIs y módulos matemáticos para
evitar duplicar definiciones o reintroducir snapshots anteriores. La historia de este checkpoint
no pretende representar los merges de esos padres.

La [guía general PR176](https://github.com/ema-ljn/MuRet/pull/176) conserva el mapa de unidades
y cinco hitos funcionales abiertos. Query [PR175](https://github.com/ema-ljn/MuRet/pull/175) y
los kernels Fauna [PR179](https://github.com/ema-ljn/MuRet/pull/179) siguen como dependencias
explícitas de sus consumidores. El acceso de entorno Fauna se publica como slice dependiente
de este checkpoint; no confundirlo con un adapter completo ni con Main integrado.

## Última evidencia disponible al cortar

- Main compiló con Werror, PCH desactivado y un job; sus ocho casos CLI pasaron. Esto no acredita
  juego completo desde el entrypoint.
- La última corrida archivada de 34 ejecutables registró 559 casos, un fallo y cero skips.
  No se renueva esa cifra como PASS del snapshot actual.
- El fallo era una expectativa previa de revision cero en SelectionPipeline después de dos
  recepciones síncronas aceptadas. El hunk autorizado ya está en el código: test renombrado a
  `TwoIdenticalAcceptedCreationsApplyBeforeTheSceneReadsTheSnapshot`, revision previa dos,
  comprobaciones posteriores de no replay conservadas. Falta registrar la nueva corrida.
- La última comprobación global de formato registró exit1; quedó al menos
  `test_login_cloud_drawing.cpp` señalado. La última pasada de include-cleaner registró exit1
  en `test_login_scene_loopback.cpp`. Corregir/verificar estos puntos en la continuación; no
  asumir que un resultado archivado cubre cambios posteriores.
- Las adaptaciones previas de material pasaron sus suites completas 38/38 y cuatro casos raw32.
  Sus resultados acotados no validan toda esta composición ni sustituyen las pruebas pendientes.

Los informes y manifests originales permanecen en los catálogos locales ignorados
`cl7-parallel-acceleration-20261109` y `cl7-leaf-scene-integration-20261109`; no se exporta
su contenido privado. El catálogo de publicación conserva el manifiesto SHA256 del código al
corte y el receipt de stop. Quien trabaje sin ese catálogo debe tratar las afirmaciones nativas
como autoridad pendiente de acceso, sin inventar semántica a partir de este resumen.

## Primeros pasos del siguiente agente

1. Leer AGENTS.md, CODING_RULES y la guía general; comprobar HEAD y diff antes de editar.
2. Reconfigurar con dependencias habituales: C++20 y Boost fijado por el manifiesto, sin instalar
   versiones arbitrarias ni cambiar configuración global.
3. Construir Main y las suites afectadas; repetir primero SelectionPipeline con el hunk ya
   aplicado. Preservar revision dos antes y después de Poll/Step, sin replay de eventos.
4. Completar formato/includes sobre los hunks de compatibilidad; registrar resultados y no
   desactivar tests para obtener verde. Mantener los cinco grupos iniciales de sprites del login,
   su Additive, geometría, pase/vista y orden; el sufijo acredita 622/10 Subtractive y
   5f2/0 + 5c6/5 Additive por sus emisores y ramas cerradas.
5. Revalidar mundo/recursos, Leaf, partículas runtime, metadata y callers públicos de sesión.
   Separar omisiones por datos de tests realmente ejecutados.
6. Partir la composición en capas revisables o continuar el checkpoint con commits acotados.
   Mantener un único WorldState/ActorState/pool/RNG/reloj/registro físico; no crear DTOs rivales.
7. Al integrar PR177, evitar las definiciones duplicadas de opciones que pudiera conservar
   OwnDerivedStats: usar OwnItemOptions y conservar la cola aparte, sin activar stats completos.

Comandos orientativos desde el checkout, con VCPKG_ROOT configurado por el usuario:

```sh
cmake --preset linux-x64-debug -DMURET_WARNINGS_AS_ERRORS=ON -DMURET_PRECOMPILED_HEADERS=OFF
cmake --build --preset linux-x64-debug --target Main test_selection_pipeline -j1
ctest --preset linux-x64-debug -R '^(SelectionPipelineTest[.]|Client[.]Main[.])' --output-on-failure --no-tests=error
```

Después construir los targets presentes `test_game_scene`, `test_game_scene_leaves`,
`test_particle_runtime_integration`, `test_gl_frame_target`, `test_texture_raw32_integration`,
`test_server_session` y los callers de sesión. Confirmar índices con CTest; un filtro vacío no
es validación. GL real usa el mecanismo offscreen existente cuando esté disponible.

## Fronteras que no se cierran al publicar

Los emisores T01/E04–E08 y productores de Traversal/StepContext siguen sin routing completo
por Main. Fauna conserva obligaciones de servicios reales, Mode y vigencia de heroFlag. Equipo
y stats 550d10 mantienen sus stops 40e1b0/54fe10. Las familias Leaf y materiales todavía sin
soporte conservan sus errores y prefijos; el backend portable no acredita FSIN universal.
No abrir protecciones, no emulación nueva y no tratar capturas visuales como equivalencia.

Este checkpoint se mantiene DRAFT y no debe mergearse sólo porque compile o porque CI cambie
a verde. El siguiente agente debe explicar el alcance de sus correcciones y sus límites.
