# H1: reparaciones propias y validación del original

Se corrigieron las diferencias propias demostradas de H1: ciclo de telas por
slot, caja persistida del picking, cámara, tintado y acción18 sobre el registro
fantasma400. El detalle de direcciones, guardas y observadores está en
[research13](../../../Client/docs/research/13-h1-actores-y-picking.md).

La implementación y su verificación estática se separan de la validación nativa
todavía parcial. H1 no se declara100% validado. El servicio de amigos no es la
causa de estas diferencias de H1; H9 conserva su
[resultado propio](2026-11-15-cl9-h1-h9-resultado.md).

## Diferencias corregidas

| Área | Comportamiento resultante y prueba |
| --- | --- |
| Telas, P-C161 | `WorldState` posee401 estados de tela. Alta libre, baja y cleanup liberan exactamente sus slots; reconstrucción viva conserva, sentinel400 queda fuera de cleanup. Dos regresiones usan telas CPU reales. |
| Caja del picking | Se leen los48 bytes publicados por el último dibujo; moverse no los recalcula. Partes de jugador, Angel y monstruos publican en su frontera nativa, aun sin modelo dibujable. Pose, sombra y equipo colgante no publican. Proyecciones intermedias conservan historia separada. |
| Cámara y comparación, P-C164 | Cámara real distinta de `ray.origin`; orden de suma, PC53 del startup, best float, empate con redondeo y aceptación NaN. El CW durante DLL/GL permanece sin observar; el acceso fuera de grilla conserva límite seguro. |
| Tintado | Primera pasada incluye al héroe elegible, escribe E8 antes de luz/PK; segunda pasada lo excluye de selección en Game. Picking intermedio restaura E8 y luz durables. |
| Guardas de partes | Alpha igual a0.01 o NaN omite publicación, contorno, efectos y cuerpo, incluido Angel de Chaos Castle. |
| Acción18, Q-C778 | Clave ausente/muerta/con bit alto usa spare400. Movimiento, yaw y posición preceden a la acción. Caballo oscuro, stop, falta de animación y prefijo anterior al corte H3 conservan el orden nativo. |
| Héroe transformado | Caminar, detenerse y llegar usan el esqueleto propio y la tabla de sonidos. Reposo noPlayer completa accesosY→X, selección0/7 y sufijo/RNG; NIL/empty conserva animación. Se corrigió también la tabla de Player/subtipo209 al llegar. Clic→D3→2→0 se verifica en integración sintética. |

La revisión independiente encontró que Angel saltaba el nuevo callback, que la
prueba NaN no distinguía la condición anterior y que la guarda alpha debía
evitar todo el dibujo. Se corrigieron esos casos y se reforzaron sus
observadores. La revisión del coordinador detectó además la exclusión indebida
del héroe en tintado. La última revisión no dejó P1/P2 observados en ciclo de telas, acción18, picking
de Game/escena5 ni en la integración del transformado. En esta última, RED
reprodujo el corte sin modelo y la rama de reposo pendiente; otra regresión
reprodujo la tabla omitida de Player/subtipo209. Sus10 suites focales pasan
198 tests. El ordenY→X de reposo se acredita estáticamente; el consumo final
de RNG por sí solo no distingue permutar esas dos lecturas.

## VMXP

El servidor sigue en otra PC. Se verificaron SHA-256 c32 y base400000 por sesión.
Las entradas del gesto y la caminata fueron automatizadas; no requirieron que
Emanuel sincronizara acciones entre ventanas. Sólo se leyeron campos en claro,
sin alterar memoria, inyectar, suspender hilos ni enviar paquetes fabricados.

| Caso del runbook | Resultado observado | Estado |
| --- | --- | --- |
| H1-E1, elfa parada | `^^`: dos acciones18 idénticas, acción8e/dirección2, antes del chat. Descriptor176 con12 keys;36 muestras de animación176 con frame avanzando y Movement=0. | Coincide en este subcaso. |
| H1-E1, elfa caminando | Pre-Enter: acción14 decimal/Movement=1. Una acción18 y después chat;38 muestras conservan caminata y retorno a reposo, sin175/176. | Coincide en este subcaso. |
| H1-E1, Knight | La captura anterior valida dos acciones antes del chat; nueva lectura confirma descriptor175 con12 keys cargado. | No se mezcla la lectura del recurso con una traza completa de animación del Knight. |
| H1-E1, transformado parado | Modelo real0x154,7 acciones, sin175; clase Knight. Acción0/Movement=0: un18 antes del chat y38 muestras que conservan acción0. | Coincide con la compuerta por acción; estar quieto no garantiza dos envíos. |
| Transformado caminando | La sesión conserva bit20 de inmovilidad. No se observó Movement=1 con los intentos y se verificó el retorno de56acd0 por ese bit. | Precondición no alcanzada; el gesto caminando de este modelo no se declara validado. |
| H1-E2, DFf1 | No se identificó una mecánica real alcanzable que produzca ese paquete con tipo f1. | Comparación nativa pendiente; contrato, altas, pool lleno y RNG cubiertos estáticamente y en tests propios. |
| H1-E3, bendición | No se identificó un emisor disponible para repetir bit02000000 en héroe y otro actor. | Comparación nativa pendiente; aviso repetido y orden previo al OR cubiertos en tests propios. |
| Picking nativo adicional | Dos posiciones de hover ensayadas no produjeron índice seleccionado; las lecturas fueron−1. | Intentos sin caso positivo; no cuentan como validación del picking. |

Las capturas de la elfa incluyen eco del servidor y decodificación sin errores.
Los PCAP, nombres, capturas, helpers y manifiestos permanecen privados. Las
lecturas del observador son secuenciales; no certifican cada frame o la pose
completa. El A/B del anillo confirmó0x154/bit20 al equiparlo y0x20a/flags0 al quitarlo.
El BMD coincide con el corpus y sus acciones0/2 tienen6/7 keys cargadas. El45
recibido trae22 bytes por registro, con nombre en12 y bit20 en el campo8..11;
el cliente consume28 y lee destino/dirección después del nombre. Falta explicar
por qué se envía ese bit; el tamaño corto por sí solo no lo demuestra.
[Diagnóstico completo del anillo](../../../Client/docs/research/14-transformacion-en-vmxp.md).
Los layouts completos12/45/25 y DF con otro jugador real continúan en P-C160.
No se repitió Agregar+Enter ni se operó un perfil de servidor local.

## Verificación local

La verificación final en Linux aprobó **7.108/7.108 tests**, sin fallos ni
salteados, en222,42s. Incluye59/59 casos históricos y106 de OpenGL, con corpus,
fuente y GL obligatorios. Los198 focales de la reparación también pasan.
Formato e include-cleaner:118 archivos C++, sin hallazgos. El arranque real
`Main --frames 60` completó Loading→Login con482 modelos y exit0. No se generaron
cores nuevos; se conservaron los8 registros previos. No hay una corrida nueva
de CI Windows para este delta local.

Las suites anteriores7074/7091 y los fallos/RED de desarrollo se preservan.
El test de propagación de error de WorldEntry usa ahora la ausencia de servicio
al sentarse, en vez del corte noPlayer que se acaba de implementar; conserva
contador30, dos efectos de llegada y estado parcial. La revisión independiente
confirmó ese propósito.

Las reparaciones se entregan en un PR a `main`, junto con el delta de H9.
El cierre de implementación de H1 se limita al alcance ASM revisado; la
validación estricta restante en VM y contra el servidor se hará más adelante.
Código y evidencia quedan recuperables mediante parches y manifiestos privados.

## Qué queda para certificar H1

No se encontraron diferencias propias pendientes en el alcance estático
revisado. Faltan los casos nativos indicados y las observaciones de P-C160/P-C164.
Los cortes de combate H3, efectos H4/CL11, audio CL10, party/social H7/H8,
chats/cartas H10 y transferencia H13 mantienen sus dueños explícitos; este
trabajo no los declara implementados. No se atribuyen esos pendientes al
servicio de amigos ni se cierran por un conteo de tests.
