# T16: próximas familias ordinarias después del ciclo vivo Kalima

Revisión adversarial estática, 2026-11-11. Árbol de referencia: `a07067523b8123816cb609eab1dc95ce834f9c3d` (tree, no commit). Alcance: mapas 1, 4, 8, 10 y 33, sus consumidores inmediatos y las fronteras de familias vecinas. No se ejecutaron builds, tests, el cliente original, emulación ni VM; no se modificó fuente ni estado remoto. Este informe no certifica por sí solo el gate integrado, no vuelve a revisar Kalima y no cierra T16.

**Recomendación:** el próximo bloque funcional debe ser el ciclo vivo de **Dungeon y Lost Tower, mapas 1 y 4**, incluyendo recursos, creación, paso, efectos, materiales, dibujo, caminata y transición de vuelta. Comparten los especiales de Object2, pero no el cuerpo de objetos. Tarkan es el siguiente consumidor natural de la infraestructura; Icarus y Aida requieren contratos adicionales identificados abajo. No propongo un PR por helper ni habilitar los cinco mapas mediante una allowlist.

## Qué acredita hoy la evidencia

`Client/tests/scenes/test_game_t16_matrix.cpp` instancia 0..36, pero las filas 1/4/8/10/33 esperan un error que contiene `0x6374ce`. Realizan entrada por sesión, apertura de escena, un Step y un intento de Draw. No acreditan un primer frame completo ni un Step posterior sobre objetos vistos ni caminata. `Draw()` puede presentar el trabajo parcial; eso no equivale a `DrawCompleteFrame()`.

`Client/tests/scenes/test_game_t16_walk.cpp` instancia la caminata ordinaria sólo para 0/2/3. Tiene recta/codo, safeZone y borde, con progreso de posición y frames durante el recorrido. Ninguna de esas observaciones se extiende automáticamente a los cinco mapas de este informe. La cobertura separada de Kalima tampoco lo hace.

Tres fronteras reales impiden el ciclo:

1. `GameScene::DrawTheWorldPass`: después del terreno, si hay **cualquier** objeto vivo y el mapa no es 0/2/3/Kalima, devuelve `6374ce`, antes de cargar sus modelos y recorrer el dibujo. No depende de que ese objeto resulte visible.
2. `GameScene::StepTheObjects`: antes del primer dibujo, los objetos todavía no vistos permiten salir con éxito; posteriormente el guard devuelve `606560` para estas familias. Sólo Kalima tiene actualmente preludio antes de ese early-return.
3. `World::StepWorldObjectChecked` rechaza fuera de 0/2/3/55/Kalima antes del cuerpo común. `DrawGameWorldObjects` tiene una restricción análoga. Quitar estos guards dejaría pasar defaults que omiten cuerpos reales; no constituye un port.

En particular, el éxito del primer Step de la matriz no demuestra que `606560` esté completo. Icarus tiene trabajo nativo incluso sin un objeto visto: véase el preludio de abajo.

## Datos disponibles, comprobados en el archivo local

Se inspeccionaron los recursos de `Client/assets/Data` y se decodificaron estáticamente los registros `.obj`; no se ejecutó lógica del original. Las rutas se expresan con la capitalización lógica del cliente. Los cinco mapas tienen `.att`, `.map`, `.obj` y `TerrainHeight.OZB`. Cantidades de tipos son tipos colocados, no cantidad de BMD cargados.

| Mapa | Recursos del mundo / objetos | Registros / tipos | BMD normales ausentes para tipos colocados | Especiales presentes |
|---|---|---:|---|---|
| 1 Dungeon | World2 / Object2 | 4488 / 61 | tipo 60: Object61.bmd, 9 registros | DungeonStone01, Bat01, Rat01 en Object2 |
| 4 Lost Tower | World5 / Object5 | 5380 / 39 | tipo 40: Object41.bmd, 1 registro | los mismos tres de Object2, no Object5 |
| 8 Tarkan | World9 / Object9 | 3327 / 77 | tipos 3, 35, 38: 1, 38 y 6 registros | Object9/Bug02.bmd |
| 10 Icarus | World11 / Object11 | 670 / 15 | ninguno de los tipos colocados | Object11/cloud.bmd |
| 33 Aida | World34 / Object34 | 2979 / 70 | ninguno de los tipos colocados | Object1/Butterfly01, Object34/tree_eff, Object9/Bug02 |

Los cuatro primeros `.att` tienen 65540 bytes; Aida tiene **131076**, con celdas de dos bytes y el tratamiento del byte bajo que ya implementa `TerrainAttributes`. Los `.map` tienen 196610 bytes y los heightmaps 66620. El inventario no afirma que todos los BMD se hayan parseado en ejecución ni que todas sus texturas dependientes se hayan subido.

Los BMD ausentes no autorizan a inventar geometría ni a eliminar los objetos. Dungeon60 ya es un caso oculto/de registro; LostTower40 produce sprites por posición. Hay que preservar qué sucede nativamente sin malla y distinguirlo de un servicio de modelos ausente. `WorldModels::LoadObjects` actualmente tolera archivos opcionales ausentes; `ConfigureActions` sí exige la segunda acción de Dungeon40 (`61a13c`) y fija velocidad 0.4 (`61a136`). Esta validación ya existe, pero la matriz corta antes de acreditarla en el ciclo de escena.

No usar `135,125` como prueba de población: dentro de ±15 celdas hay 121/66/80/**0**/74 registros respectivamente. Ese conteo espacial no es una prueba de visibilidad del trapecio. Para Icarus conviene seleccionar una ruta sobre los datos reales cerca de sectores poblados, por ejemplo sector (5,4), antes de fijar coordenadas. En todos los mapas deben verificarse con `.att` las celdas de la recta, el codo y safeZone; este informe no inventa coordenadas caminables a partir de densidad.

## Contrato común que debe llegar a los consumidores

Fuente pública de autoridad: export ASM del build `c32bbd5f…a26a`, fijado al commit de research `91359b2ceffa4ce085ea19bc7bf11762fc2034a5`. Ruta base:

`https://github.com/EmaNLJN/mu-server-repack-research-102i/blob/91359b2ceffa4ce085ea19bc7bf11762fc2034a5/client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`

Cada dirección abajo identifica el archivo `00xxxxxx_FUN_00xxxxxx.asm`. El pseudocódigo se usó como navegación; los calls, ramas y orden citados se contrastaron con ASM. No es una auditoría exhaustiva de las tablas de salto ni de cada consumidor transitivo de efectos: las lecturas todavía necesarias quedan explícitas.

- `606560`: llama `6061c0` en `606572`, luego reinicia `7d365a0` en `60657a`. Por objeto vivo/visto de sector activo ejecuta **604ab0 → 6063d0 → 5ff960**. En sectores inactivos aún puede ejecutar `5ff960` para objetos vivos y borra seen. Al final decrementa `6c9ddc`. La implementación ordinaria actual sólo recorre `StepWorldObjectChecked`; no extenderla suponiendo que todos los otros helpers son no-op.
- `604ab0`: alpha, animación y estado de modelo anteceden las ramas de mapa. Los errores nuevos deben conservar el prefijo ya ejecutado, incluidos CRT, stores y emisiones. Si un caso común es suficiente, justificarlo por la rama nativa; no por un default de MuRet.
- `6046a0`: sectorización/visibilidad y publicación de seen; `5ffdf0` posa incluso varios objetos cuyo cuerpo no emite malla. `602ed0` toma un CRT común en `602ef4` antes de los efectos específicos. El orden de objetos, la pose compartida y el estado de modelo importan aunque el frame sólo compruebe vértices.
- `5ff960` no es incondicionalmente vacío: depende de mapa/tipo/contador retenidos (`6c9dd8`, `6c9dd4`, `6c9ddc`). Para el bloque ordinario se debe acreditar que esos productores están inactivos o portar el camino alcanzable. No activar artificialmente un evento para simular caminata; tampoco asumir que compartir un tipo 9/10 lo vuelve seguro.
- Recursos: `WorldModels` carga especiales y normales, configura acciones, asigna slots y sube texturas. `GameModels` ya sube GlowTextureSet, EffectTextureSet y GameTailTextureSet. **Chrome01/5c0 ya existe en GlowTextureSet**, y JointThunder01/614 en GameTailTextureSet; no duplicar sus uploads ni owners. Las texturas 716 y 623 requieren localizar su carga real y acreditar disponibilidad; no se encontraron declaradas en esos sets.
- `GameSceneFrameProjection` copia objetos/pools/estado. Todo nuevo owner debe remapearse a la copia correcta durante proyección, sobrevivir al ciclo durable y limpiarse al cambiar mapa. Un puntero a un objeto temporal no sirve.

Se mantiene el contrato previamente revisado: con drawingEnabled y profundidad inicial cero, **PrepareFrame consume cuatro CRT** incluso si después hay hold; Render con frames sintéticos cero se compara contra el estado **posterior a Prepare**. Este bloque no introduce una excepción por mapa.

## Mapa 1: Dungeon

Productores presentes: creación de objetos, registros especiales 59/60 y carga de especiales. Especial `0xda` usa DungeonStone01; `0xaf/0xb0` son Bat/Rat. Compartir sus recursos con Lost Tower no implica compartir paso de objetos.

Trabajo observable pendiente en `604f95..60506c`:

- Tipos 22/23/24: estado del modelo/UV dependiente del reloj; son 17/17/50 objetos reales. Debe llegar al material dibujado y no sólo cambiar una estructura aislada.
- Tipos 39/40/51: ocultación; 27/26/6 registros. La pose/acción y el estado oculto requieren pruebas separadas de «no hay vértices».
- Tipo 41: fuego vía `52f360` con offset (0,-30,240); tipo 42 con (0,0,190), calls `605043/60505e`; hay 64/56 registros. Reutilizar el consumidor de fuego existente con el mismo reloj y pool durable.
- Tipo 52: `rand % 3` en `604fb4`, creación condicional de efecto con modelo `0xda` por `518cf0` en `604fdb`, y ocultación aun sin emisión; 29 registros. **El consumidor durable de ese efecto no está cubierto por GameModelEffects**, que actualmente acepta los tipos de Noria, Kalima, teleport, dragon/arrival. `CreateNoriaEffect(322..324)` no reemplaza esta creación.

Lectura/port acotado necesario: ramas `0xda` de `518cf0`, `5208a0` y sus pases de dibujo, vida/colisión y dependencias reales; no es necesario portar todos los efectos. Probar nacimiento, siguiente Step, dibujo posterior y expiración, incluyendo pool lleno y el CRT del intento que no emite. Bat/Rat y el terreno pueden reutilizar consumidores existentes si el frame de escena los acredita; no declararlos faltantes sólo por pertenecer a Dungeon.

No se encontró una necesidad de UI de evento para los casos ordinarios anteriores. La acción de Dungeon40 y sus recursos sí son precondiciones reales de carga.

## Mapa 4: Lost Tower

`604ab0` tiene un cuerpo propio: tipos 3/4 desplazan UV; 18/23 usan blendMesh1, 19/20 blendMesh4 y desplazamiento; 24 oculta y sortea `rand & 63` para crear efecto `0x5de`; 25 oculta. Los tipos relevantes están colocados: 3=211, 4=399, 18=68, 19=14, 20=6, 23=163, 24=95, 25=148.

Tipos 38/39 (777/335 registros) llaman **52f550**. Ese helper lee la acción del héroe (rango 13..33), distancia y velocidad previa, puede aplicar impulso/sonido `0x7c`, y evoluciona posición/ángulos/velocidad. No consume CRT por sí mismo en el ASM leído. `ObjectStepGlobals` tiene posición del héroe pero no su acción: falta un productor real de ese dato. No reemplazarlo por «el héroe está quieto»; cubrir rama activa y negativa. Esto es acoplamiento con estado del actor, no una razón para requerir toda la UI de combate.

`600afc..600c6d` materializa los tipos 23/19/20/3/4. Incluye **un CRT en 600b26**, aunque su valor no termine visible. 3/4 y 19/20 hacen pases adicionales con Chrome01/5c0, mallas distintas, color temporal y estado unlit que se restaura antes del cuerpo normal. El enum actual `ObjectDrawKind` no expresa esos pases. Probar secuencia de comandos/material y restauración en el objeto siguiente; contar cualquier vértice no detecta la omisión.

`602ed0` añade sprites por huesos para 19/20, con texturas 61e/5d5 y ángulos opuestos; tipo40 genera dos 5d5 por posición, aunque falta su BMD normal en el archivo local. Preservar la pose compartida, el CRT común de `602ef4` y el orden respecto al cuerpo. El efecto `0x5de` exige nacimiento/paso/dibujo y recurso reales: no confundirlo con LastTowerEffect de Noria (324). Los consumidores transitivos de 5de quedan para lectura dirigida.

Este mapa puede entregarse con Dungeon como un concepto completo de objetos ordinarios vivos y caminata, conservando los negativos de las familias siguientes.

## Mapa 8: Tarkan

Tiene datos reales y creación/registro del tipo78. No es una variante de Dungeon por compartir la entrada de `604ab0`.

- Antes de animar, tipo8 multiplica la velocidad por la constante de `6b8a44`; fijar sus bits por evidencia estática al implementarlo. Tipos 2, 4, 7, 11/12/13, 61/65/66, 72/73/75/79, 82 tienen combinaciones de UV, blend, seno/ángulos y luces dinámicas. 63/64 se ocultan. No reducir el port a los tipos que casualmente estén cerca del spawn.
- `602ed0`: tipo60 crea una vez 20 humos `5f2/6` y oculta; 70 usa gate `%5` y `5f2/7`; 76 usa ventana de reloj y `5f2/4`; 83 combina ventana dependiente de ángulo/ftol, `5f2/8`, más humo y efectos con modelo **200/201**. 63/64 producen sprite716 por hueso; el tipo0 usa 5c6.
- `600c72..600cf1`: tipo81 hace dos pases de malla0, uno normal y otro con flags0x44 y 5c0. Hay dos objetos reales de ese tipo.
- Ya existen `IsMap8Smoke` y kernels 5f2 subtipos4/6/7/8 en `Particles.cpp`, accesibles con el overload de reloj vivo. Conectar el productor a esa ruta, comprobar nacimiento/paso/dibujo y el error por binding ausente; no reimplementar humo como un efecto de otra familia.

Tipos 0/4/64/66 no aparecen en este `.obj`, aunque tienen ramas nativas: requieren tests dirigidos o quedar explícitamente fuera de la cobertura de datos, nunca declararse ejercitados por el frame real. Sí están presentes 60=82, 63=12, 70=3, 76=19, 83=11. Los efectos 200/201 y el recurso716 son dependencias pendientes, junto con su orden de consumo CRT. No se acreditó que una UI de evento sea necesaria para este ciclo ordinario.

## Mapa 10: Icarus

**No empezar por ampliar el guard de dibujo.** Su primer Step ya puede producir efectos antes de que haya objetos vistos:

1. `6061c0`, tras los predicados anteriores, reconoce mapa10 en `606211..606219` y llama `605d70` en `60621f`.
2. `605d70` usa gate `%50`; en la rama activa produce luz dinámica, efecto con modelo **0xbb**, selector contra el contador retenido y, condicionalmente, dos joints **615/0**. Incluye un rand descartado en `605dbb`: debe conservarse. El especial cloud.bmd ya está en WorldSpecialModels; su ciclo de efecto no queda implementado por cargarlo.
3. El preludio toma además `%10` en `606226`, y en su rama activa consume posiciones aleatorias y crea efecto con modelo **5ac** (`6062d9`). No equivale al sprite de la misma textura.
4. Se reinicia el contador. Por objeto visto, **después de 604ab0**, `6063d0` incrementa contador, consulta selector y gates, y puede emitir sprite **623** y dos joints **614/6** con owner del objeto. Las ramas fallidas decrementan el contador. Debe conservarse el contador entre pasos, no recalcular una lista equivalente.

En `604ab0` la rama específica de mapa10 no añade el cuerpo de Tarkan: debe conservarse el trabajo común y luego los helpers anteriores. Los consumidores actuales de joints tienen infraestructura de dibujo para614/615 y variantes de otras familias, pero **eso no acredita creación/paso de614/6 ni615/0**. Revisión dirigida de esos subtipos, owners y recursos, no sustitución por614/14.

En dibujo, `6046b2..6046b7` establece margen adicional **-10** para Icarus. `602ed0` crea una vez clouds622 con owner: 20 para tipos0/1/2 y 10 para3/4/5, subtipo igual al tipo, luego oculta; tipo10 emite partícula5ac por hueso. `Particles.cpp` actualmente reconoce clouds8 y login10, no el ciclo owner0..5. Evitar usar cloud8 como aproximación.

El terreno se omite en el pase actual de mapa10. Eso no permite omitir altura/pathfinder/atributos de caminata. Las ramas globales de material en `6002e0` que mencionan mapa10 incluyen tipos de personajes/monstruos altos (por ejemplo171); no todas son requisitos de los objetos colocados0..15. Separar esos negativos de los materiales efectivamente alcanzados. Fauna/lluvia Icarus ya tienen partes implementadas; la siguiente prueba debe reutilizarlas y detectar el primer consumidor que todavía corte.

Aceptación adicional: Step inicial con cero objetos vistos y seeds de gate activo/inactivo; contador retenido distinto de cero; owner real que se mueve/desaparece; proyección que no escribe a owner durable; entrada poblada distinta de Home; dos frames y varios Steps para que la creación única no se repita.

## Mapa 33: Aida

El módulo `425070` reconoce **33 y54**. `4250e0` sólo devuelve ese predicado, sin crear efectos: no atribuir a la carga lo que produce el paso/dibujo. `WorldObjectMapCreation` ya expresa ese contrato.

`4250f0` es el paso específico: 25/28 UV; 30/71 sorteo de luces; 41 alpha0.5; 56/57/58/59/62/63/67/70 ocultos; 64 velocidad0.05; petición de sonido **0x246** y retorno de handler aceptado. El retorno controla la cadena posterior: implementarlo sólo como «map33 aceptado» perdería stores y sonido.

`425260` concentra efectos del frame: sprites5c6 por huesos en30/71; estado/color/UV y pase específico para41; partículas6f0/2,6f2/4,6f1/2 en56/57/58; clouds622/1 con owner en59/62/63; efectos con modelo **ae** (60) y **b3** (70), consultando tiempo además de CRT; partículas5de/7 en67; pases de malla/UV en65/66. Estos tipos están presentes en el archivo; no se resuelve probando un árbol genérico.

Los kernels y texturas waterfall incorporados por Kalima son reutilizables sólo cuando coincidan **tipo/subtipo/argumentos/scene**. Falta acreditar esos subtipos de Aida, no basta que el número de textura esté en EffectTextureSet. Cloud622/1 y efectos ae/b3 necesitan consumidores y owners. `GameModelEffects` no los acepta hoy. La disponibilidad de Butterfly01 no prueba el ciclo ae/subtipo3; para b3 hay que seguir la selección de recurso del efecto, no asumir que lo cubre la tabla de especiales de Aida.

El wrapper material `425d90` llama `428df0`; los casos de criaturas1b7/1b8/1bb no corresponden a los objetos colocados0..71. No exigir toda esa familia de monstruos como condición inventada para el paseo ordinario, pero conservar su frontera si aparece un actor real que la alcance. Compartir predicado con54 obliga a regresión de selección y de recursos al salir/volver; no demuestra que Aida dependa de toda la UI de selección/eventos.

## Qué no incluir como alias ordinario

| Familia vecina | Tratamiento del próximo bloque |
|---|---|
| 0/2/3 | Regresión de consumidores comunes, CRT, materiales y owners. No reabrir su alcance completo. |
| 31 Land of Trials | Comparte Butterfly y clouds8, pero tiene creación/paso43ffxx propios. Mantener negativo de escena y estudiar después como módulo; no habilitar por parecido con33. |
| 54 selección / 55 login | Regresión por predicados, modelos y pools compartidos. No son nuevas filas ordinarias T16. |
| 9/32 y11..23 | Familias de evento: remapeos de datos y state/UI/consumidores propios. Mapa32 puede resolver World10, lo cual no lo convierte en Tarkan ni en Icarus. No eludir447d70. |
| 6/7 | Arena/Atlans mantienen cámara, actor/agua y cuerpos propios; no cubiertos por este port. |
| 30/34 | Mantener dependencias explícitas de estado de Siege/Crywolf y sus handlers. |
| 5/35 | Datos ausentes ya declarados; no son un hallazgo nuevo ni razón para bloquear1/4. |

No se inspeccionaron exhaustivamente los otros37 mapas ni el checkpoint185. Las posibles ramas activas de `5ff960` requieren trazar el productor de sus globals cuando se aborde su evento; su mera existencia no convierte cada paseo normal en un requisito de UI completa.

## Próximo bloque ejecutable y criterio de cierre

**Unidad funcional propuesta: mapas1/4 de entrada a reentrada, con objetos vivos.** Paths principales: `World/WorldObjects.*`, `Scenes/WorldObjectsDrawing.*`, `Scenes/GameScene.cpp`, `Scenes/WorldModels.cpp` y `WorldSpecialModels.cpp`, `World/ModelEffects.*`, `World/GameModelEffects.*`, los productores/consumidores concretos de partículas y `Scenes/GameSceneFrameProjection.*`. Cambiar sólo los necesarios tras seguir da/5de; no fragmentar el resultado por helper.

Orden de dependencias dentro del bloque:

1. Fijar contratos ASM acotados da/5de y52f550, incluyendo errores, pools llenos, bits de constantes, CRT y material. Mantener cuerpos/predicados de otras familias explícitos.
2. Conectar recursos y datos del héroe; portar productores y consumidores juntos. Reutilizar fuego, modelos, Chrome01, sprites y pose existentes. No descartar una emisión porque el pool de destino aún no tiene binding.
3. Integrar paso→seen del dibujo→siguiente paso, y efectos durables/proyectados. Sólo entonces levantar las fronteras específicas de1/4 en todos los niveles.
4. Añadir a la matriz frames completos poblados para1/4; conservar el inventario explícito de cortes en8/10/33/31. Añadir caminata real de1/4 con recta, codo, safeZone y borde verificados contra sus atributos.
5. Probar entrada/salida/reentrada mediante sesión y SceneManager, reutilizando el flujo existente. Recursos físicos, slots y owners deben volver a ser válidos, sin depender de un frame previo guardado ni limpiar a mano los pools.

Evidencia mínima que diferencia un port de una allowlist:

- Primer frame poblado **nuevo y presentado**, objetos de los tipos críticos realmente vistos; siguiente Step y frame con sus cambios. Registrar comandos/material/partículas o efectos concretos, no sólo `Failure == null` ni cantidad total de vértices.
- Click/rayo real→D3 literal esperado independientemente del encoder→eco FakeSocket independiente→movimiento y altura, con cambios de celdas y posición. Los objetivos deben estar en sectores poblados de los datos reales.
- Por rama aleatoria: gate activo/inactivo y estado CRT esperado de fuente independiente; incluir sorteos descartados, fracaso por pool lleno y prefijos antes de error. No calcular el esperado llamando al helper productivo.
- Dungeon52/da y LostTower24/5de: efecto vivo después del productor, siguiente paso, dibujo y fin; no ignorar `nullopt` como «no había efecto». LostTower38/39: acción dentro/fuera del rango, distancia y velocidad previa; comprobar sonido/transformación del objeto, no sólo que la llamada ocurrió.
- Materiales LostTower3/4/19/20 y sprites40: orden, textura física, flags, pose, color/unlit restaurados para el objeto siguiente; modelo ausente donde los datos realmente lo presentan.
- Negativos: binding realmente requerido ausente, acción Dungeon40 ausente, recurso obligatorio faltante/malformado según contrato, fuera del trapecio, alpha oculto, pools llenos, familia no portada. Validar error y prefijo, sin exigir error para cada BMD opcional ausente.
- Render proyectado frames0: medir desde después de Prepare, sin nuevas mutaciones durables ni CRT; conservar el control independiente del +4 común de Prepare. Repetición de frame y transición deben detectar owners viejos.

Después de1/4, Tarkan puede ser una segunda unidad funcional (productores UV/luz/humo/materiales y efectos200/201 completos). Icarus debe agrupar preludio/selector, cloud owners, joints y paseo; Aida debe agrupar su módulo y consumidores de efectos con regresión54. Esta secuencia permite avanzar T16 sin proclamar «familias ordinarias completas» por una muestra genérica.

## Límites y veredicto

**T16 sigue abierta para estas cinco familias.** La matriz actual declara correctamente su corte, pero no acredita cuerpos, recursos cargados en escena ni locomoción de ellas. Los datos disponibles permiten empezar1/4 sin esperar una UI de evento completa. Hay dependencias concretas de efectos y estado del héroe que deben entregarse con los cuerpos; en Icarus también hay productores antes del primer frame.

Este informe es un inventario de port y aceptación, no un resultado de tests ni un cierre exhaustivo de todos los cases nativos. Quedan lecturas dirigidas de los consumidores da/5de,200/201,bb/5ac,614/6,615/0,ae/b3, los recursos716/623 y variantes exactas de partículas Aida. Ninguna de esas lagunas debe rellenarse con success, no-op o una guarda por mapa. Los hallazgos no alteran la aceptación ya revisada de Kalima ni el contrato común de Prepare.
