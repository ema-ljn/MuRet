# H1: ciclo de actores y caja del picking

Contraste estático y observación acotada en XP, 2026-11-15. Binario PE32 x86,
SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`,
base `0x400000`. Todas las direcciones siguientes son VA de ese build. Se usaron
los exports ASM completos y los bytes del PE; el pseudocódigo es auxiliar.

## El dueño de las telas

`0x512870` libera el registro al tomar un slot libre; reconstruir una clave viva
conserva sus recursos. `0x510ff0` libera al dar de baja. La limpieza de mapa
recorre los primeros400 registros, incluso el héroe superviviente y los muertos;
el registro fantasma400 queda fuera. Las llamadas a `0x4fb1a0` no equivalen a
detectar después que cambió la clave o la clase: una baja y un alta pueden
ocurrir entre dos dibujos con la misma clave y clase.

MuRet aloja `CharacterCloth[ActorPoolSize]` en `WorldState` y presta spans desde
`ActorCloths()`. Cada evento anterior reinicializa el elemento correspondiente.
El reset incluye telas regulares, `OnTheMesh`, arrastre, reloj, flares y estado
del blend. La reconstrucción viva no lo resetea. `GameScene` presta ese mismo
almacenamiento al paso y al dibujo.

`ActorClothLifecycleTest.MapCleanupReleasesClothsInEveryPoolSlotExceptTheSentinel`
y `MapLoadTest.CreatingAnActorInAFreeSlotDropsItsClothAndLiveKeyRecreationKeepsIt`
ejercitan telas CPU reales, cleanup, baja/alta, clase repetida, clave viva y
sentinel. No constituyen una traza nativa de `OnTheMesh`; la cobertura de su
liberación se apoya además en la asignación completa del objeto dueño.

## Qué escribe la caja y cuándo

| Función | Contrato estático relevante |
| --- | --- |
| `0x4ebc80` | Escribe los48 bytes de `actor+0x13c..0x168`. En juego, modo distinto de2: origen = mínimo suministrado + traslación del modelo; tres ejes diagonales = máximo − mínimo, con términos cruzados cero. No escala ni gira los bounds. |
| `0x6001b0` | Prepara la pose del jugador. No llama a `0x4ebc80` y no publica caja. |
| `0x60cf70` | Parte del jugador: retorna salvo alpha estrictamente mayor que `0.01f`. Prepara la traslación desde el actor; contornos, iluminación y luego `0x4ebc80` en `0x60d186`, antes de efectos y cuerpo. Incluye el Angel de Chaos Castle. |
| `0x5ffdf0` | El cuerpo del monstruo también publica mediante `0x4ebc80`; conserva las traslaciones especiales de los tipos `0x18e`/`0x18f`. Las partes usan la posición del actor. |

Llegar al escritor no requiere que MuRet encuentre una malla, textura o modelo
dibujable. Una parte ausente también publica si alcanzó ese punto del original.
La pose, la sombra, las telas y el equipo colgante no reemplazan ese escritor.
Alpha igual a0.01 o NaN tampoco debe producir contornos, efectos ni cuerpo.

`ActorPickingBox` conserva las doce componentes; `DrawnCharacter::onPartBox`
marca la frontera del escritor. El dibujo intermedio usa cajas proyectadas y no
adelanta la historia durable del actor. La supresión del héroe conserva también
sus escrituras parciales en la proyección correspondiente.

Pruebas: `CharacterDrawingTest.PublishesPartBoxAtTheBodyPartDrawBoundary`, el
caso Angel de `GameCharacterRenderTest`,
`GameCharactersTest.ProjectedHeroSuppressionWritesOnlyTheProjectionBox` y
`CharacterPickingTest.PickingUsesTheLastPublishedBoxAfterTheActorMoves`.
El modo2 del editor transforma vértices y tiene otro contrato; no se expone
como parte del juego de H1.

## Lectura y aritmética de `0x59e700`

La primera pasada tiñe todos los actores elegibles, incluido el héroe: vivos,
vistos, alpha mayor que cero y máscara compatible. `0x59e779` escribe primero
`byteE8=1`; después aplica la tabla de luz por nivel y PK. La segunda pasada
excluye al héroe salvo en la escena4. El helper de H1 se usa en juego, escena5;
la selección de personajes conserva su ruta propia.

El segmento viene de `0x7d36834` a `0x7d366c0`, pero la distancia se mide desde
`0x7d36884/888/88c`: es otro global. Se lee la caja persistida, sin reconstruirla
desde una posición recién movida. El candidato calcula `(dz² + dy²) + dx²`;
`best` es float, comienza en bits `0x5368d4a5` y sólo se redondea al aceptar.
La comparación `FCOM` seguida de `TEST AH,1` acepta menor **o unordered**.
Un empate exactamente representable conserva el primero; un `best` redondeado
hacia arriba puede permitir al siguiente candidato ganar, aun algo más lejano.

El uso de x87 no prueba precisión extendida efectiva. Startup sigue
`0x6a24e9 → 0x6a26fb → [0x6e1b08] → 0x69f806 → 0x69f815 → 0x6a4b73`;
la conversión en `0x6adf2c/0x6adef7` termina en `FLDCW` en `0x6adf24`, con
PC53. Por eso se conserva `double`, el orden de operaciones, `best` float y
`-ffp-contract=off`. No se midió el CW de cada llamada tras ejecutar DLL/GL;
esto no certifica equivalencia bit a bit para todo entorno de ejecución.

Regresiones de `CharacterPickingTest` separan cámara y segmento, comprueban
E8/luz, caja vieja, empate exacto, redondeo ascendente y NaN como último
candidato. La prueba NaN mantiene una caja finita: debe fallar con la antigua
condición `!(distance < best)`. El picking intermedio restaura E8 y luz.

Fuera de las65536 celdas, el original lee memoria contigua; MuRet mantiene su
límite seguro. La party vacía y los consumidores de otros hitos conservan el
alcance documentado en P-C162. No se presenta esa frontera como equivalencia
universal del original.

## Acción18 y registro fantasma

`0x4cb750` llama a `0x5110d0` y usa el resultado400 sin comprobar ausencia,
vida ni clave. Sólo el caballo oscuro (`pet=0x1c16`) corta antes de escribir.
Orden: Movement=0, yaw=`(dirección sin signo − 1)·45`, X/Y del centro de la
celda destino. Z, celda y clave se conservan. El registro400 no se publica
como un actor de los primeros400.

Para `0x78/0x79`, la frontera H3 ocurre después de ese prefijo y antes del
sufijo de ataque; no se adelantan escrituras posteriores si falla el helper.
`0x12` publica sonido0x6f incluso si el modelo no tiene animación0x98.
`0x7a/0x7b` también se aplican al registro400. Repetir una acción no reinicia
por sí solo el frame, pero sí actualiza el prefijo de posición.

`ActionReplyTest` observa spare400, claves muertas/con bit alto, primer duplicado
vivo, caballo oscuro, stop/RNG, fallo H3 con prefijo conservado y escudo sin
animación. Es contraste estático con regresiones propias, sin fabricar un
paquete entrante en el original.

## Comparación nativa alcanzada

Se verificaron hash y base del módulo cargado. Observador externo de sólo
lectura y entradas normales de mouse/teclado; captura del switch VMware
filtrada por la conexión real. Sin parches, inyección, suspensión de hilos ni
modificaciones del servidor. Los archivos raw, nombres y PCAP son privados.

| Caso | Observación del original | Límite |
| --- | --- | --- |
| Elfa parada, `^^` | Dos acciones18, dirección2, acción8e, antes de un chat. Modelo0x20a con205 acciones; descriptor176 con12 keys.36 muestras de acción176 y Movement=0; frame1.28..10.879998. | Respalda la variante +1 del elfo y progreso de animación, sin certificar toda pose/vértices. |
| Elfa caminando, `^^` | Antes de Enter, acción14 decimal y Movement=1. Una acción18 antes del chat.38 muestras con acciones14/2, sin175/176; la caminata termina normalmente. | Respalda la compuerta caminando; lecturas secuenciales, no snapshot atómico ni traza de cada frame. |
| Knight, recurso cargado | Descriptor175 con12 keys en modelo0x20a de205 acciones. | Es lectura de recurso. El caso previo del Knight validó el wire parado, no una traza completa de esa animación. |

Ambos envíos de la elfa recibieron el eco del chat, sin errores de decodificación.
No se volvió a enviar Agregar+Enter. Emanuel equipó después un anillo: el modelo real0x154 tiene7 acciones y no
contiene175. En reposo usa acción0 y Movement=0: el gesto envía una acción18
antes del chat y conserva la acción0 durante38 muestras. Este caso no entra
en la compuerta1..12/8a, aunque el personaje esté quieto. La caminata de esa
transformación no se alcanzó: conserva bit20 de inmovilidad y56acd0 retorna
por él. El A/B de equipo y el paquete45 se detallan en
[el diagnóstico del anillo](14-transformacion-en-vmxp.md). Se repararon además
los préstamos de modelo/sonidos de caminar y el reposo noPlayer; sus pruebas
sintéticas no equivalen a haber caminado con ese estado en XP.
No se identificó un emisor real alcanzable
para DFf1 o para aplicar dos veces `0x02000000` al héroe y a otro actor. Esas
ramas siguen verificadas estáticamente y en tests propios; falta su comparación
nativa. Tampoco estas capturas acreditan los layouts12/45/25 con otro jugador
real de un servidor1.02 (P-C160).
