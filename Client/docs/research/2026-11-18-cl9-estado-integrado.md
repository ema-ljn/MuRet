# CL9.x: estado integrado y condiciones de cierre

Actualización del 2026-11-18. Fuente de verdad: ASM de `main.exe`, SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
El pseudocódigo orienta la búsqueda; las instrucciones fijan anchos, signo, orden,
RNG, capacidad y accesos. El original no se ejecutó en el entorno de desarrollo.

Este documento reemplaza como estado operativo la fotografía del traspaso del 14/11.
No declara equivalencia dinámica completa ni usa porcentajes de citas como porcentaje
de funcionalidad. Los resultados del CI deben consultarse en la cabeza concreta de cada PR.

## Integración y PRs

Base de la revisión: `main` en `c8d6dbb3`, incluido #362. Los hitos 1, 2, 3, 4,
5, 7, 8, 9 y 11 ya tienen sus entregas principales integradas en esa base.
Eso no elimina sus fronteras de fidelidad descritas más abajo.

La entrega pendiente conserva la pila autorizada:

`main` ← [#347](https://github.com/ema-ljn/MuRet/pull/347) (H9/H10)
← [#348](https://github.com/ema-ljn/MuRet/pull/348) (H6)
← [#349](https://github.com/ema-ljn/MuRet/pull/349) (H12)
← `codex/cl9-h13-cierre` (H13 y conexiones finales de eventos).

Las tres ramas públicas se actualizaron mediante commits y merges normales, preservando
el arreglo remoto de pila Windows `4d844638`. No se mergeó ningún PR a `main`,
no se reescribió historia pública y no se cerraron entregas ajenas.

## Estado funcional de los trece hitos

| Hito | Implementación presente en el conjunto | Condición que impide certificar el cierre total |
| --- | --- | --- |
| H1: emote y jugadores | Viewport, estado y acciones; conservación de los arreglos previos de entrada y selección. | Comparación dinámica estricta; efectos externos y precisión original en los casos documentados de H1. |
| H2: monstruos, NPC y misiones | Altas/bajas, diálogos, premio A3 y cambio de partes de #362. NPC 12/13 conectados con H12; joints618 y llegadas Crywolf reutilizan sus kernels existentes. | CastleArrival134 y otras familias externas; casos de registro protegido. Conservar sus VAs y precondiciones. |
| H3: combate, daño y muerte | Entrega de #342 y fix de ataque #359 conservados; integración con estados del asedio. | Casos nativos indeterminados Q-C1050/Q-C1161 y efectos concretos de CL11; combate XP previo no certifica todos los casos. |
| H4: skills, cuervo y mascotas | Entrega principal integrada; receptores/equipo y familias necesarias para Siege/Crywolf ampliados. | Efectos de skills que siguen perteneciendo a CL11 y validación de sus recorridos especiales. |
| H5: ítems | Piso, usar/tirar, inventario y devolución del cursor antes del teleport. | Autoequipación de flechas P-C151/Q-C773: lectura de manos tras el cierre protegido. |
| H6: NPC, almacén y caos | Cargas, cálculos, respuestas, frutas, joyas, equipo, ventanas y sus cierres; [contratos y pruebas](2026-11-18-cl9-h6-cierre.md). | Validación del conjunto publicado y contraste VM; la frontera de flechas no se sustituye por una mano vacía. |
| H7: party, comercio, duelo y tiendas | Entrega principal integrada; servicios completos de cierre en entradas indirectas. | Interoperabilidad y casos especiales contra servidor/original; ninguna cita porcentual acredita estos recorridos. |
| H8: gremio, alianza y guerra | Entrega principal integrada; caché canónica y cierres del Guild Master por chat, quest y paneles. | Servicio compatible, roles reales y contraste dinámico de los flujos sociales. |
| H9: amigos y editor compartido | Edición occidental, cursor/selección, clipboard, foco y latch Tab; guard compartido de entrada. | Perfil XP de autoscroll horizontal, medida/caret e IME completo. Sigue abierto como fidelidad, aunque exista implementación funcional. |
| H10: chat y cartas | Editor compartido, listas, retrato y multiline; #347 incorpora las reparaciones de H9. | Misma frontera del EDIT y validación del servicio de amigos/cartas. |
| H11: eventos | Battle Soccer, marcador/telas, F3 24 y resultado incluso con HUD oculto; Crywolf, altas, ataques y cadenas necesarias. | Servidor de laboratorio sin Crywolf; observación original y fronteras de memoria/efectos concretas. |
| H12: ventanas del castillo | Registro/listas B4/B5, Senior, puertas/estatuas, impuestos, retiro/fabricación y zona de caza, desde NPC/recepción hasta paso/dibujo. | Estados iniciales/bytes residuales del original y comparación visual/servidor en VM. |
| H13: batalla del castillo | B2/B6/B7/B9/BA/BB, corona/bandos, holders, panel y minimapa, armas/puertas, mapa30, B1 y retorno. | Posiciones de pila sin inicializar, picking de CL7.4, familias134/61c/19f de CL11 y contraste completo de batalla/cambio de servidor. No se declara completamente jugable mientras esas rutas puedan cortar. |

## Contratos reparados en esta integración

- H12: apertura/cierre con consultas y `31` en orden; listas retenidas, permisos,
  inventario ocupado y diálogos conectados. Revisión adversarial corrigió textura/Alpha,
  punto de redondeo x87 de vida y alias de la recuperación de estatua. B2 1A escribe
  los impuestos canónicos usados por tienda y mezclas.
- H13: una sola fuente de holders para recepción, HUD y salida del viewport;
  limpieza antes de enmascarar la clave. B6 clase2 limpia incluso con cuenta0 y agrega
  en índice0; BB agrega desde índice+1 y cuenta0 conserva. B9 02 vuelve a pintar/subir
  la marca completa de 64×64 en cada recepción.
- B1 y retorno: cierre, opciones/macros, espera y conexión en orden. El saludo limpia
  la caché de gremios incluso sin movimiento pendiente. El reset intercala el primer
  `31`, F3 00, diálogo silencioso y entrenador como el ASM: el segundo `31` depende
  de que el entrenador esté vacío; ocupado muestra 1211 y continúa.
- Esc, CameraTour, selección y entradas indirectas reciben los servicios de mundo,
  inventario, castillo y sesión que necesitan sus cierres. El panel2000 tiene paso y dibujo.
- Mapa30 y Crywolf: se completaron familias concretas de modelos, partículas, joints,
  efectos persistentes, texturas, agua/cáusticas, RNG y sonido solicitado. Un pool lleno
  omite el alta de su elemento y continúa el caller; no se convierte en éxito vacío
  cuando falta un constructor con capacidad disponible.

Los detalles de familias del asedio están en
[mapa30](2026-11-18-h13-map-effects.md) y
[cuerpos y efectos](2026-11-18-h13-siege-body-chain.md).

La pasada final encontró y reparó callers que seguían cortando aunque sus kernels
ya estaban presentes. No se clasificaron automáticamente como deuda de otra ola:

| Ruta | Reparación contrastada con ASM |
| --- | --- |
| Esc desde la escena | `StepTheDialogs` presta el sender de NPC; cuatro regresiones pasan por `GameScene::Step`, sin sender artificial en el helper. El fix también se llevó a las bases H6/H12. |
| Misiles `432b63` / `42fd37` | ED/1 de Crywolf y E1/1 de fortaleza reciben posición/ángulos/luz, dueño y WORD de key originales; conservan la salida con pool lleno. |
| Joints618/2,3 | La creación admite los tipos normales de la tabla, además de349; mismo dueño Actor y ancho30, con los dos intentos en orden. |
| Llegada `4c9883` | Etapa3 pone alpha1 sin altas. El resto pone alpha0 y comparte con CameraTour cuatro615/7, tres5f6/1 y cuatro614/17; los cuatro primeros usan el mismo punto actor+(20,20,0). |
| Kalima `43bea5` | C5 crea C8 y C9 en posición del actor después del polvo y alcanza la cola del pulso; lleno sólo omite el alta. |
| Altar Crywolf51, estado2 | Dos pases reales de malla0,44→240, RGB acumulado, escala1.03 después de posar y byte88=0→1→ff; se conserva la precisión de FSIN con el auxiliar PC53. |

## Evidencia y verificación

Las regresiones de orden de reset y cierres Esc se observaron fallar antes de la
reparación sobre una build coherente; no se usaron fallos por ABI mezclada como RED.
En la integración R2 pasaron las suites focales: ServerSession221, CastleBattleReceive39,
SessionEventApplier22, ReturnToSelection10, GameDialogClosures3; después de reparar
contratos/fixtures, CastleSiegeReceive102, CastleWindows15 y GameHud35. GameUi60 se
verificó con el mismo conjunto fijo y el arreglo de Tab. Son resultados focales, no
un sustituto de la suite completa sobre la cabeza final.

La reinspección R3 confirmó el arreglo de Esc con RED0/4→GREEN4/4 y GameScene
completo34/34 sin saltos. Los misiles pasaron3/3 y los callers H2, junto con
CameraTour,55/55. El altar pasó de fallar en430a40 a aprobar su caso discriminante
y la suite completa CrywolfObjectServices13/13; GameScene34/34 volvió a pasar con
la biblioteca que contiene ese renderer. Son comprobaciones focales con headers/biblioteca coherentes y
hashes retenidos; no se les atribuye el resultado del CI de otra cabeza.

La publicación exige compilación completa con warnings como errores, CTest, GL requerido,
fuente real, formato18 e include-cleaner18, más Linux/Windows en cada cabeza publicada.
Las ejecuciones y posibles saltos se registran en el PR correspondiente; no heredar
el verde de la base ni afirmar que un test saltado está aprobado.

La recuperación de evidencia pudo materializar **62/62 archivos y 51/51 recursos**
del manifiesto histórico, comprobando tamaños y hashes, fuera de Git. Incluye los
sidecars de inventario derivados del cable real. Esto habilita tests previamente
saltados; **no equivale a ejecutar el original ni a observar c32 en la VM**. Las
capturas históricas de otro perfil no se renombran como evidencia nueva.

## Lo que necesita la VM u otra ola

El [runbook Castle/Crywolf](scripts/game/capture/RUNBOOK-cl9-castle-crywolf.md) fija
identidades, precondiciones, secuencias, VAs y entrega de evidencia. Casos prioritarios:

1. Polvo `529516..529520` / `525302..525310`, primera creación `61e/1` del efecto147
   (`52719e..5271d3`): posiciones de pila sin inicializar. El polvo de las acciones2/0xa
   en `431e86` tiene otra frontera: su **luz** puede venir de un local sin inicializar.
   No se sustituyen estos datos por ceros ni por otros campos del padre. Con capacidad disponible
   siguen siendo fronteras alcanzables; con pool lleno debe continuar el caller.
   La luz de `431e86` se reemplaza en el paso normal antes del dibujo; su estado
   intermedio desconocido no se presenta como una divergencia visual ya observada.
2. Autoequipar flechas `561b13`, P-C151/Q-C773: bytes y estado del registro protegido.
3. EDIT XP: autoscroll horizontal, multiline/caret y conversion/preedit del IME.
4. Selección interactiva de objetos: productor `59eb90`, CL7.4, que alimenta
   `selectedTargets[3]`. Escribir el índice en una fixture sólo prueba el consumidor.
5. Eventos/servicios: Castle Siege requiere `CityWar` y roles configurados; el servidor
   del laboratorio no implementa Crywolf. Sin servidor compatible el caso es BLOCKED.
6. Efectos externos CL11: mantener por familia el tipo, VA, prefijo y capacidad;
   ni su falta ni el audio pendiente de CL10 desaparecen por cerrar la lógica del evento.

Hay **implementaciones concretas todavía pendientes**, además de capturas VM:

| Familia externa | Caller normal que puede quedar bloqueado |
| --- | --- |
| CastleArrival134/0 | Altas de los tipos278/283, `42d513` / `42d5af`; key120 y dueño Actor. El Arrival61f existente no implementa134. |
| Efectos Siege61c /19f | `42dd48` / `42dd9c`, según tipo y paso del actor. |
| F8/1 | Crywolf `43257a` y fortaleza `42fd76`; descendencia60f/0x19 (25decimal) o0xb y105/3. El arreglo de ED/E1 no los sustituye. |
| C1A | Ataque Crywolf `4329db` y genérico `4f6780`; no equivale a las piedras C8/C9. |

Estos casos requieren portar constructor, paso, dibujo y descendencia con sus
contratos; obtener una captura no reemplaza esa implementación. Con espacio en
los pools pueden seguir cortando una ruta normal, por lo que el conjunto no se
presenta como Castle Siege/Crywolf universalmente jugables. Otros efectos CL11
de skills conservan sus límites individuales. La clasificación es una asignación
del trabajo pendiente, no una exención de su impacto funcional.

Para continuar, revisar primero los checks del PR sobre su SHA, resolver cualquier fallo
real y ejecutar los casos independientes del runbook. Un caso sin captura se registra
NOT_RUN/BLOCKED. Sólo después de cerrar estas fronteras y contrastar los recorridos se
podrá afirmar cierre completo de fidelidad CL9.
