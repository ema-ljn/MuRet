# Continuidad y publicación de CL7

Fecha de corte: 2026-11-09 UTC. Base pública de esta entrega:
`4a4fd49709cefea42dda61887a3b2ecff76d65b2` (`main`). **CL7 sigue abierta.**
Este documento inventaría código productivo congelado pendiente de exportación y sus fronteras;
no incorpora código, evidencia privada ni una nueva validación del original.

El objetivo funcional sigue siendo entrar al mundo, equipar al héroe, caminar, dibujar y cambiar de
mapa conservando estado, recursos y orden de efectos. Abarca los mapas nominales 0–36 y las
regresiones de selección/login 54/55. Lorencia y Davias son escenarios de integración; no bastan
para certificar todas las familias. Los recursos ausentes de 5/35 requieren una política acreditada.

Referencias públicas de alcance: [plan de Lorencia](2026-11-05-ola-cl7-lorencia.md),
[roadmap del cliente](../specs/2026-10-30-cliente-design.md) y
[research de escenas](../../../Client/docs/research/06-render-escenas-y-ui.md).
La neblina visual conserva su seguimiento pospuesto; HUD, reglas de eventos/combate y audio
siguen en sus olas respectivas.

## Corte solicitado y handoff actual

Emanuel pidió detener la ejecución y subir el estado tal como estaba. El último worker fue
detenido y cerrado de forma confirmada; no se debe relanzar automáticamente.

**El punto de entrada actual es [PR182](https://github.com/ema-ljn/MuRet/pull/182)**, checkpoint
WIP de mundo/escena/Leaf/runtime, con su [handoff completo](https://github.com/ema-ljn/MuRet/blob/EmaNLJN/cl7-pub-leaf-runtime/docs/superpowers/plans/2026-11-09-cl7-runtime-wip-handoff.md).
Conserva código/tests al corte y documenta el último fallo de fixture, el hunk ya aplicado sin
una corrida posterior sellada y los pendientes de formato/includes. No se continuó corrigiendo
ni se renovó la validación después de detenerlo.

El slice FaunaEnvironmentAccess está en [PR183](https://github.com/ema-ljn/MuRet/pull/183),
apilado en la rama `EmaNLJN/cl7-fauna-access-wip`, sobre el
commit `6a4d11739fb01042863914ddb96510f7bf89a337` de182. Su handoff está en
`docs/superpowers/plans/2026-11-09-cl7-fauna-access-wip-handoff.md` de esa rama.
La combinación con el padre no fue compilada/probada tras el corte.

Los siguientes PRs por unidad siguen disponibles para review o para partir/reconciliar el
checkpoint. No fusionarlos todos sin revisar solapamientos; publicar el estado WIP no equivale
a integración aprobada ni cierre de CL7.

## Punto de continuidad remoto

Este lote ya tiene los siguientes borradores públicos. Cada PR incluye código/tests y su handoff
acotado; la publicación permite retomar el trabajo y no significa aprobación ni cierre de CL7.

| PR | Qué puede retomar otro agente | Base / validación local actual |
| --- | --- | --- |
| [175](https://github.com/ema-ljn/MuRet/pull/175) | Consulta canónica de atributos y pendiente TQ03 | `main`; 2659 ejecutados PASS, 77 skips, cero fallos |
| [176](https://github.com/ema-ljn/MuRet/pull/176) | Este mapa de continuidad y dependencias | Sólo documentación, controles documentales |
| [177](https://github.com/ema-ljn/MuRet/pull/177) | `OwnItemOptions`: helpers ordinarios; no equipo completo | `main`; 2660 ejecutados PASS, 70 skips, cero fallos |
| [178](https://github.com/ema-ljn/MuRet/pull/178) | `ObjectAlpha` y delegación pública acreditada | `main`; 2680 ejecutados PASS, 70 skips, cero fallos |
| [179](https://github.com/ema-ljn/MuRet/pull/179) | Kernels Birds/Fish con servicios requeridos | `main`; 2725 ejecutados PASS, 70 skips, cero fallos |
| [180](https://github.com/ema-ljn/MuRet/pull/180) | CPU partículas y backend portable explícito | Stack sobre PR168; 187 focales + 8 CLI Main, sin skips |
| [181](https://github.com/ema-ljn/MuRet/pull/181) | Backend material GL y API física raw32 | `main`; 50 focales/Main + 6 metadata, incluidos 27 GL, sin skips |

Los conteos son de árboles diferentes y no se suman. Las omisiones no son golden aprobados.
Los checks remotos deben consultarse en cada head; estos resultados locales no los sustituyen.

La capa canónica de mundo/escena, Leaf y runtime Particle quedó publicada como checkpoint
WIP182 al interrumpir el export, y el acceso de entorno queda apilado sobre ella. Falta terminar
validación y revisión de capas, no seguir esperando una nueva exportación privada. Su fuente
congelada aislada no enlazaba por falta de WorldRuntime y la combinación nueva no se validó;
no se atribuye éxito standalone. Conservar un único WorldState y las APIs canónicas.

Al integrar PR180, reconciliar los cuatro paths matemáticos compartidos con PR172. Al integrar
PR181 y la base de escena, conservar `TextureMetadataOf(uint32_t)` y `TextureSlotOf(uint32_t)`
con el registro físico canónico; no reintroducir la API signed de snapshots anteriores. El
registro positivo real de `GameModels` todavía pertenece a esa capa pendiente, no al backend solo.

## Base y padres públicos comprobados

Estado consultado con `gh` de sólo lectura el día del corte. Un check correcto acredita el head
consultado: no convierte un borrador en merge ni prueba el funcionamiento de sus consumidores.

| Padre | Identidad y estado observado | Uso y límite |
| --- | --- | --- |
| [PR166](https://github.com/ema-ljn/MuRet/pull/166) | Mergeado; incluido en la base | `ItemState` y carga de ítems. No completa equipo/stats. |
| [PR164](https://github.com/ema-ljn/MuRet/pull/164), [PR173](https://github.com/ema-ljn/MuRet/pull/173) | Mergeados; incluidos en la base | Luz radial/RGB y precisión de `LightAt`. No completan pasto ni L4. |
| [PR169](https://github.com/ema-ljn/MuRet/pull/169), [PR171](https://github.com/ema-ljn/MuRet/pull/171) | Mergeados; incluidos en la base | Metadata de texturas y copia acotada de versión; conservar estas correcciones. |
| [PR163](https://github.com/ema-ljn/MuRet/pull/163) | Borrador abierto; head `126f0f6155825e3ca996a6402bc11a8b0938ea7d` | Creación de objetos, registros y servicios. Faltan proveedores reales y composición Step/Draw. |
| [PR165](https://github.com/ema-ljn/MuRet/pull/165) | Borrador abierto; head `31a5d13df65d3d17327b43cafc8ebaec2b43c807` | Paso/posición y ángulos; no es la integración completa de caminar. |
| [PR167](https://github.com/ema-ljn/MuRet/pull/167) | Borrador abierto; head `04e03dee1ae907cdce74704440bd5cc680d60a48` | Aplicación ordenada de eventos; `ApplyWorld` mantiene su frontera explícita. |
| [PR168](https://github.com/ema-ljn/MuRet/pull/168) | Borrador abierto; head `5c6d5f85d89794f4d7a48da96e377e5cbb7cf7b1` | Padre exacto de la composición CPU; conservar las correcciones existentes de tipo 68. |
| [PR172](https://github.com/ema-ljn/MuRet/pull/172) | Borrador abierto; head `fc74be3425e42e3b01018506f0e8b032c27e8b5a`; base PR165 | Matemática retenida compartida. Tiene solapamiento con la composición CPU; no importarla dos veces. |
| [PR174](https://github.com/ema-ljn/MuRet/pull/174) | Borrador abierto; head `771e924466707f39dd041e8a4f33a5149509cd4c` | Orden de evaluación de ondas Leaf y evaluador obligatorio. |
| [PR175](https://github.com/ema-ljn/MuRet/pull/175) | Borrador abierto; head `87f9baf22f0ff895ea5b318659189bc4a2b173dd`; base de esta entrega | Consulta canónica de atributos. Al consultar, formato aprobado y Linux/Windows aún sin conclusión. |

PR163/165/167/168/172/174 tenían formato, Linux y Windows correctos en sus heads consultados.
PR172 está apilado sobre PR165, aunque la integración final se publica hacia `main`.
Antes de rebasar hay que comprobar el diff del padre y conservar únicamente la dependencia necesaria.
Las URLs nuevas de Query, stats, Alpha, fauna, CPU y materiales constan en la tabla remota anterior.
Entorno y la base/runtime se conservaron al corte en PR183/182 respectivamente; su revisión y validación siguen pendientes.

## Lectura correcta del estado histórico

Las autoridades privadas son los catálogos locales ignorados
`cl7-parallel-acceleration-20261109` y `cl7-leaf-scene-integration-20261109`.
Sus nombres lógicos permiten localizar el handoff autorizado; no forman parte del diff público.
El catálogo de publicación conserva el STATE anterior como historia, no como cola vigente.

| Marca anterior | Estado que debe usar quien retome |
| --- | --- |
| Luz y metadata pendientes de publicar | PR164/169/173 ya están en la base pública. |
| Leaf detenido en el adapter o en GEN | Entrega final Leaf con aceptación independiente acotada. Aún requiere separar padres para publicarse sobre `main`. |
| CPU con un fallo de sentinel y fixes QUALITY pendientes | La composición final incluye los fixes y el sentinel; el resultado anterior permanece histórico. |
| Birds pendiente de F1–F5 | Producto y observadores corregidos; QA de la composición congelada cierra F1–F5 dentro de su alcance. |
| Fish R1/R2 sin discriminación | Existe un fix posterior de observadores; la QA previa no revisó ese fix y no se le atribuye su aprobación. |
| Runtime Particle todavía en curso | Hay entrega final de Step/Draw/Main/GL, condicionada a sus padres y con emisores aún sin conectar. |
| `completed` en contratos o tareas | No demuestra un caller integrado, un proveedor real ni cierre funcional de CL7. |

No se publica el inventario de caches, herramientas, contratos, capturas o experimentos como
pendiente de producto. La base limpia tampoco contiene por ese hecho las implementaciones
congeladas de otros checkouts.

## Unidades productivas y fronteras de integración

Cada fila es una unidad revisable. «Lista acotada» describe el paquete fuente congelado;
el export sobre una base pública necesita su propio manifiesto y validación.

| Unidad / catálogo lógico | Alcance cerrado y API que se conserva | Dependencia y frontera de publicación |
| --- | --- | --- |
| Query: `terrain-attribute-query`, `terrain-query-fix` | `TerrainAttributeQueryAt`, `ITerrainAttributeQuerySource`; coordenadas y escala vivas, conversión canónica y EAX completo | PR175 ya abierto. Publicar antes de sus consumidores; no sustituir por un índice de celda byte. |
| Stats: `stats-ordinary-options`; export `stats-options` | Helpers completos `54e540`/`54e590`; `OwnItemOptionsView` presta campos y región de opciones del `ItemState` público | Export final de tres archivos `OwnItemOptions.h/.cpp` y `test_own_item_options.cpp`. Excluye la cola del archivo histórico, `550d10`, `ActorEquipment` y activación de WorldEntry. |
| Alpha: `object-alpha-helper`; export `object-alpha` | `StepObjectAlpha(const float&, float&, float&, uint8_t)`; alias, relecturas, modos y orden de stores | Export final de cuatro archivos: helper/test más include y delegación mínimos en `WorldObjects`; su preimagen pública fue acreditada. No queda pendiente ese caller de modo cero; otros callers son otra integración. |
| Fauna: `birds-lifecycle` y sus dos fixes; `fish-lifecycle`, `fish-observability-fix` | `StepBirds`/`StepFish` e interfaces obligatorias `IBirdStepServices`/`IFishStepServices`; modelos, vistas y pools mínimos canónicos | Exportar kernels y regresiones sin `Main`, Leaf o Core completos. Servicios reales, Alpha/query y binding de identidad son dependencias explícitas; un doble de test no las satisface en producción. |
| CPU: `particle-composition` / `quality-import` | Create/Step/Draw de `60f/3` y `5f2/4,6,7,8`, D2 y `PortableParticleTrigonometry`; unión final de 29 archivos, fixes y sentinel incluidos | Parte de PR168 exacto. Comparar `OriginalRetainedMath` y configuración con PR172 antes de partir capas. No reintroducir el padre histórico fallido ni tocar el wiring de escena aquí. |
| Materiales: `particle-material-backend`, `particle-material-observability-fix` | `ModelBatchParticleBackend`, operaciones parciales del lote y extensión del renderer existente; tests finales | Reutiliza metadata pública y tipos mínimos de petición Leaf. Separar imports de delta propio; no reemplazar `CharacterDrawing` completo ni crear renderer/caché paralelo. |
| Leaf: catálogo `cl7-leaf-scene-integration-20261109`, entrega final | Evaluador real, estado durable, reloj, cámara C3, dibujo Davias y proveedor GEN/World1; invalidación/carga de recursos | Tiene aceptación acotada de integración. Requiere extracción de padres de escena/recursos y PR174/math/materiales; el seed histórico completo no es publicable como esta unidad. |
| Particle runtime: `particle-runtime-integration`, entrega final | `SceneContext`, `GameScene`, `Main` y regresiones; Step/Draw checked con backend durable compartido | Delta exclusivo de nueve archivos sobre Leaf/CPU/materiales. No volver a importar sus padres ni atribuir al delta los algoritmos de esos owners. |
| Entorno: `fauna-step-services` | `FaunaEnvironmentAccess` en `FaunaStepServices.h/.cpp` y test; acceso real a mundo, RNG, terreno y luz | Depende de Query, `WorldState`, `ClothWorld`, terreno y buffer dinámico canónicos. Es acceso de entorno acotado, no adapter completo de Birds/Fish ni binding en `Main`. |

El inventario incluye además infraestructura de juego ya usada por las composiciones privadas:
`WorldEntry` y equipo, `WorldState`/`WorldRuntime` y actores durables, `WorldResources`,
pasos de objetos, `WorldObjectRenderer` y efectos de llegada. Su existencia no acredita una exportación independiente.
El owner de runtime debe identificar el mínimo padre público de cada hunk que use y devolver
una frontera exacta cuando falte. No copiar un seed o Core completo para ocultar esa dependencia.
La cola de stats y la integración total de equipo siguen abiertas, aunque existan helpers locales.

Identidades de referencia para evitar mezclar generaciones: manifest final Leaf
`c77bb79560940b684d9e5a6117bb4f44ff48e4810d406a2bf95f3eeff68a0645`;
manifest final runtime Particle
`ef0ca2cd3d72cbeece16bd788185fd6d7af58e8ee4cbd25c699f53534d0b468f`;
manifest final de composición CPU
`0d141bbe03649495ea165ef3a06ab89e65a41acd654474802ad68db9d285af50`.
Son identidades de paquetes, no commits públicos ni certificados de todo CL7.

## ABI, proveedores y ruta ejecutada

Conservar firmas, ancho de campos, préstamo y postestado de error de los contratos cerrados.
No crear un segundo `ItemState`, `ActorState`, pool, RNG, reloj o metadata para hacer encajar una
extracción. Una referencia requerida sin proveedor acreditado sigue siendo una frontera abierta.

| Superficie | Obligación del integrador |
| --- | --- |
| Opciones de ítems | Vista prestada sobre el `ItemState` real y destino de dos bytes; preservar count/tag/durabilidad vivos, alias y wrap. `54e540`/`54e590` no equivalen a stats completos `550d10`. |
| Birds | Slot físico e índice corresponden al mismo pool estable; identidad de actor/modelo/acción prestada sigue válida durante callbacks. Preservar PC53/RN, orden de servicios y resultados completos de handlers. |
| Fish | Position y flag pertenecen al mismo actor seleccionado por `WorldRuntime.hero`, con selector vivo. El flag es byte crudo; no normalizarlo a booleano ni fijar el héroe al slot cero. |
| Entorno Fauna | `mode`, `heroFlag`, `scene`, escala de query y altura fija son referencias externas requeridas. El buffer de luz es el dinámico, distinto de la luz base sombreada. Destruir el servicio antes de liberar/sustituir su terreno y reconstruirlo para el nuevo. |
| Tiempo y matemática | Consumir el tiempo publicado del mundo y el evaluador explícito; no usar reloj de frame ni valores por defecto. El backend CPU declara Boost113 → retenido64 → producto PC53/RN. Esto no certifica exactitud universal FSIN/FCOS ni todas las plataformas. |
| Lote y recursos | Leaf y Particle comparten `GameModels`, `ModelBatch`, backend y vista durables. Invalidar conocimiento de texturas antes de release/recarga; las operaciones sólo de estado también deben llegar al renderer. |
| Errores | Mantener prefijos, stores, draws RNG y orden ya consumados. Los fallos alcanzados no revierten ese estado; escena/manager deben detener el sufijo y la presentación según su contrato. |

En la **base pública**, `GameScene::Render` sigue sin dibujo de juego y la sesión llega a
`EnteringWorld`; PR167 todavía no está incorporado. Los parsers, cámara y carga parcial públicos
no forman una entrada jugable completa.

En la **composición congelada Leaf + Particle runtime**, `Main` construye proveedores durables;
el pase publica su vista antes de dibujar; `GameScene` consume el mismo pool, RNG y tiempo por
`StepParticlesChecked`/`DrawParticlesChecked`. Esa ruta reemplaza el dibujo legacy de partículas
en la escena de juego, preservando la ruta anterior del login. Es un estado de esa composición,
no una afirmación sobre `main` ni todos los mapas.

Persisten en esa composición los callers legacy de `StepWorldObjects` y
`DrawWorldObjects`/`ObjectFrameEffects`. Los emisores T01 y E04–E08 necesitan conectarse a
`IWorldObjectStepServices::CreateParticle` y `WorldObjectRendererBindings::particle` con los
productores canónicos de Traversal/StepContext. Publicar kernels o poner un callback sin caller
alcanzable no completa esa ruta.

Para Fauna, `ActorState.safeZone` está identificado como campo del flag, pero falta acreditar su
vigencia por paso y algunas fases. El estado Mode de Birds no tiene aún binding canónico completo.
La recuperación documental de productores no autoriza inicializarlo a cero ni sustituir valores
ausentes. Los servicios de spawn/movimiento/pose/acción/finalización, efectos y audio lógico siguen
siendo obligaciones separadas del acceso al entorno.

## COVERAGE: cinco hitos que siguen abiertos

**Cerrado** significa una unidad pública incorporada con alcance definido. **Scoped** significa
evidencia favorable limitada al paquete y dominio indicados. **Abierto** requiere integración o
evidencia adicional. Todos los hitos siguientes están abiertos; no se asignan porcentajes.

| Hito | Cerrado: componentes disponibles | Scoped: avance local/congelado | Abierto: criterio para cerrar el hito |
| --- | --- | --- | --- |
| 1. Entrada, héroe y equipo | Recepción ordenada F3/03–F3/10, identidad efectiva de mapa e `ItemState` | PR167; aplicación local de mundo; helpers ordinarios de opciones | Aplicador productivo con contexto histórico, espera retenida, publicación de héroe/InWorld y equipo/stats antes del frame, incluso con cero pasos. `40e1b0`/`54fe10` siguen como stops; los helpers no cierran esa cadena. |
| 2. Movimiento y transiciones | Camino/serialización/picking, velocidad, formatos de warp y decisión pura de gates | PR165/172 y wiring local de clic/paso | Clic → camino → D3 → posición; D3/D7 recibidos, cooldowns, gates, warp/reaparición/reentrada, altura/colisión y limpieza selectiva en su orden. Cerrar los límites numéricos declarados sin atribuir al backend paridad universal. |
| 3. Creación, paso, dibujo y luz | Recursos parciales, identidad efectiva, luz radial y `LightAt` | PR163; Step/Draw de objetos y recursos usados por Leaf; backend material real acotado | Proveedores reales de creación/paso/dibujo, poses, orden del pase, luz compartida y pasto por familia. Error explícito no equivale a rama implementada. Resolver fronteras de tipos/materiales/recursos sin modelos vacíos. |
| 4. Fauna, clima y partículas | Metadata pública; componentes anteriores de partículas | PR168/174/175; CPU final, Alpha, kernels Birds/Fish, entorno y runtime con GL acotado | Binding completo de servicios, Mode/flag vigentes, emisores T01/E04–E08, estado y RNG compartidos. Cubrir familias alcanzadas, prefijos y transiciones; siguen fronteras Leaf C6–C9/fallback, U1/U2/U3 tipo66, productor 0F y writer de clima `430510`. |
| 5. Barrido transversal | Regresiones de unidades incorporadas | Suites focales, mutantes y composiciones delimitadas | Matriz por mapa y familia de entrada, caminar, cámara/recorte, objetos/efectos, ida/vuelta, mismo/distinto mapa, recursos ausentes y estado persistente. Conservar regresiones 54/55 y pendientes de P, selección/login; contrastar qué observa cada test. CI verde no cierra este hito. |

Los materiales Type `620850`/`620870` siguen sin soporte en el paquete; fog no representado
conserva su error tardío y prefijo. El dominio de préstamos válidos de actores de Leaf es una
precondición explícita del port; su aceptación acotada no certifica corrupción, concurrencia ni
todas las rutas originales. Estas fronteras deben sobrevivir a cada rebase y a la descripción del PR.

## Evidencia histórica y nueva verificación

Los siguientes resultados pertenecen a los writers y revisiones de los paquetes congelados.
Este documento **no ejecutó nuevamente C++ ni GL**, y los conteos no se suman: hay regresiones y
padres compartidos.

Ya se observaron dos exports sobre la base pública, distintos de sus corridas privadas históricas:
`stats-options` registra 50/50 (ocho helpers, 34 ItemState y ocho Main), y `object-alpha` registra
44/44 (28 Alpha, ocho WorldObjects y ocho Main), ambos sin skips, con Werror/PCHoff y `-j1`.
Sus writers registran formato/includes 18 y header aislado; este handoff sólo contrasta sus
documentos y manifiestos. Ninguno repitió sus mutantes históricos. Los demás exports deben
añadir sus propias corridas y hashes al entregar, sin heredar esos resultados.

| Paquete fuente | Resultado registrado y límite |
| --- | --- |
| Opciones | 44 focales del árbol privado incluyen tests de productor y cola; no son 44 pruebas de los dos helpers. QA no encontró defecto funcional y retiene Q1/Q2 de observabilidad. |
| Alpha | 45 focales, siete mutantes semánticos; helper completo en su dominio, sin integración Fauna/GL. |
| Birds corregido | 46 casos con guards Main; QA F1–F5 favorable acotada. No certifica proveedores reales. |
| Fish posterior al fix | 35 focales; diez mutantes que antes sobrevivían quedan discriminados. Falta contraste independiente del último fix, sin atribuirlo a la QA anterior. |
| CPU final | 186 focales y ocho CLI Main; QA de coexistencia favorable acotada. Reemplaza el resultado anterior 181/182, no lo borra. |
| Materiales posterior al fix | 40 focales, incluidos 27 GL reales, y cuatro mutantes. Q1–Q3 tienen nuevos observadores; revisar Q4 de vínculo fuente/objeto/biblioteca/ejecutable al exportar. |
| Leaf final | 238 casos, incluidos 20 GL; aceptación independiente acotada. No standalone sobre `main` ni ALLMAPS. |
| Particle runtime | 164 focales más ocho CLI Main; incluye casos GL reales y controles contra ruta legacy. Main CLI no es gameplay desde su entrypoint. |
| Entorno Fauna | 51 casos de entorno/query/luz/mundo y siete mutantes; no adapter completo ni GL. |

Opciones conserva dos límites: el porcentaje no discrimina un sentinel reintroducido dentro del
bucle; el postestado tampoco demuestra ausencia de accesos redundantes. No convertir esos límites
en bugs confirmados ni en cobertura universal. Materiales y Fish tienen fixes de observadores
posteriores a sus QA previas; el export debe contrastar sus postimágenes y assertions finales.

### Comandos para el árbol público compuesto

Usar un build privado del checkout, con variables locales definidas por quien integra:
`CL7_BUILD_DIR` para el build y `CL7_VCPKG_INSTALLED_DIR` para la instalación existente de vcpkg
en modo de sólo lectura. No versionar esos valores. `VCPKG_ROOT` conserva su significado habitual.

```bash
cmake --preset linux-x64-debug -B "$CL7_BUILD_DIR" \
  -DMURET_WARNINGS_AS_ERRORS=ON -DMURET_PRECOMPILED_HEADERS=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DVCPKG_MANIFEST_INSTALL=OFF \
  -DVCPKG_INSTALLED_DIR="$CL7_VCPKG_INSTALLED_DIR"
cmake --build "$CL7_BUILD_DIR" --target Main -j1
ctest --test-dir "$CL7_BUILD_DIR" -R '^Client\.Main\.' \
  --output-on-failure --no-tests=error -j1
```

Construir sólo los targets de la unidad presentes en su export. Los nombres son los de sus
archivos de test; opciones ya usa el nombre separado de su export final.

| Unidad | Targets focales del paquete fuente |
| --- | --- |
| Query | `test_terrain_attribute_query` |
| Opciones | `test_own_item_options`, `test_item_state`; el target histórico `test_own_derived_stats` no forma parte de este export |
| Alpha | `test_object_alpha`, `test_world_objects` |
| Fauna | `test_bird_lifecycle`, `test_birds`, `test_bird_motion_tail`, `test_fish_lifecycle`, `test_fish` |
| CPU | `test_particle_60f3`, `test_particle_map8_5f2`, `test_particle_retained_math`, `test_particle_lifecycle_composition`; conservar las regresiones de PR168 listadas por el export |
| Materiales | `test_particle_material_backend`, `test_gl_particle_material_backend`, `test_model_batch`, `test_gl_model_renderer` |
| Leaf/runtime | `test_game_scene_leaves`, `test_particle_runtime_integration`, `test_gl_frame_target`; añadir los callers/recursos que enumere el manifiesto del export |
| Entorno | `test_fauna_step_services`, `test_terrain_attribute_query`; añadir luz/mundo afectados según sus padres exactos |

Ejemplo concreto para Query, ejecutable tras integrar PR175:

```bash
cmake --build "$CL7_BUILD_DIR" --target test_terrain_attribute_query -j1
ctest --test-dir "$CL7_BUILD_DIR" -R '^TerrainAttributeQuery' \
  --output-on-failure --no-tests=error -j1
```

Ejemplo para materiales con GL obligatorio:

```bash
cmake --build "$CL7_BUILD_DIR" --target test_particle_material_backend \
  test_gl_particle_material_backend test_model_batch test_gl_model_renderer -j1
SDL_VIDEODRIVER=offscreen MURET_CLIENT_REQUIRE_GL=1 \
  ctest --test-dir "$CL7_BUILD_DIR" \
  -R '^(ParticleMaterialBackend|GlParticleMaterialBackend|ModelBatch|GlModelRenderer)' \
  --output-on-failure --no-tests=error -j1
```

Comprobar con `ctest --test-dir "$CL7_BUILD_DIR" -N` los nombres y el número esperado del
manifiesto; un filtro no vacío tampoco acredita por sí solo que incluyó todas las suites.
Para cada unidad se registran comandos, exits, fallos y skips del árbol exportado. No reutilizar
los conteos históricos como si fueran esta corrida ni repetir fullsuite durante cada export.

Antes de entregar, comprobar `git diff --check`, índice intacto, lista exclusiva de archivos y
hashes de pre/postimágenes. Aplicar `clang-format-18 --dry-run --Werror` y `clang-tidy-18` con
`-checks='-*,misc-include-cleaner'` a los fuentes y headers cambiados usando ese build sin PCH;
compilar además cada header nuevo de forma aislada. Si una herramienta no está disponible,
registrarlo como limitación, sin afirmar que pasó. Las corridas nativas Windows/macOS no se
deducen del build Linux ni de los CI históricos de otros heads.

## Dependencias, ownership y checklist de publicación

Orden parcial: base pública → Query/Alpha/opciones; PR168 + matemática reconciliada con PR172 →
CPU; metadata + tipos/lote mínimos → materiales; padres de escena/recursos + PR174/math/materiales
→ Leaf; Leaf + CPU + materiales → runtime Particle. Entorno requiere Query y sus owners canónicos;
Fauna requiere los kernels y cada servicio completo antes de conectar `Main`.
Los frentes independientes pueden avanzar sin convertir una dependencia privada en pública por
declaración. Una extracción que necesite otro concepto devuelve su padre exacto para un PR separado.

| Unidad / owner | Frontera exclusiva y comprobación antes de publicar |
| --- | --- |
| Coordinador / Query | Publicar PR175 y actualizar head, checks y enlace al incorporar; los demás consumen su API sin editarla. |
| Export opciones | Sólo helpers, vista y tests; excluir cola/equipo; comprobar ABI con ItemState y conservar Q1/Q2. |
| Export Alpha | Tres archivos nuevos y delegación mínima de WorldObjects acreditada en la base; cuatro archivos en total. No tocar kernels Fauna. |
| Export Fauna | Kernels/interfaces/pools mínimos y fixes en orden. No editar `FaunaStepServices` ni inventar servicios; declarar bindings y QA final Fish pendientes. |
| Export CPU | Una implementación de Particles/D2/math. Inventariar exactamente el solapamiento de math, CMake y manifiesto de dependencias con PR172; preservar correcciones de PR168. |
| Export materiales | Backend/lote/renderer y tests finales; consumir tipos de petición acordados. Mantener fog/tipos sin soporte y comprobar observadores y procedencia Q1–Q4. |
| Export Leaf/runtime | Único owner de hunks de `SceneContext`, `GameScene`, `Main`, recursos y callers mínimos. Mapear cada hunk a su padre; publicar delta runtime una vez sobre Leaf/CPU/materiales. |
| Export entorno | Tres archivos de `FaunaStepServices`; Query es dependency-only. No asumir propiedad de WorldState/Cloth/terreno/luz ni conectar un full adapter sin productores. |
| Coordinador / routing y cierre | Asignar después emisores/contextos, productores de Fauna, equipo/stats y familias restantes. La asignación debe identificar paths/hunks libres antes de que otro writer los toque. |

En `CharacterDrawing`, `Particles`, `OriginalRetainedMath` y tipos compartidos de render se
intercambian APIs finales y hashes; no se mantienen dos escritores simultáneos. El coordinador
resuelve la capa compartida y los otros paquetes la consumen por identidad. Los cambios a
dependencias o configuración que ya pertenezcan a un padre se revisan allí; este lote no habilita
cambios adicionales a workflows o configuración de GitHub.

Checklist común de cada unidad:

- [ ] Registrar base pública exacta, padres públicos y dependencias aún sin publicar.
- [ ] Comprobar manifiesto y hashes antes de extraer; después, comprobar cada postimagen.
- [ ] Entregar un patch exclusivo respecto de la base declarada y distinguirlo de imports/padres.
- [ ] Mantener una unidad/concepto por PR y evitar cambios ajenos, duplicados de ABI y defaults/noops.
- [ ] Revalidar Main CLI y focales del árbol resultante, registro de tests, formato/includes 18 y headers.
- [ ] Mantener expectativas y controles negativos discriminantes; justificar cualquier adaptación de fixture.
- [ ] Adjuntar handoff sanitizado con alcance, límites, comandos y fronteras; excluir datos, builds y evidencia privada.
- [ ] El coordinador actualiza URLs/heads/checks nuevos, revisión y estado de integración; el writer no publica.
- [ ] Actualizar COVERAGE sólo al aportar evidencia del criterio del hito, sin inferir cierre de CI verde.

La siguiente continuidad productiva es validar y reconciliar los checkpoints remotos con sus
padres mínimos, conectar los productores/callers aún abiertos y comprobar la composición por familia. Si una frontera
requiere una autoridad funcional ausente, devolver el punto exacto al coordinador y conservar el
slice verificable; no ampliar su semántica ni declararlo completo.
