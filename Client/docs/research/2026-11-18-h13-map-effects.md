# CL9 H13: mapa, actores y efectos de la batalla

Fuente: `main.exe` c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a. Contraste estático con ASM, tablas del PE leídas como datos y pseudocódigo sólo como guía. No se ejecutó el original. Este documento no acredita una comparación visual en la VM ni el cierre completo de H13.

## Estado y mapa

- `4294c0` conserva el valor anterior, publica el nuevo y, en mapa 30, recarga **cada vez** ATT 31/312 y TerrainLight/TerrainLight2, incluso con el mismo valor. El orden es atributos, luz y reloj de música en cero.
- `61a6ea` también elige ATT312 y TerrainLight2 al cargar el mapa con guerra activa.
- `44f340/44f560` cambian acción, sobrescriben flags 20000/0, actualizan paredes y finalmente piden sonido 1ed. El viewport usa la variante sin sobrescribir flags ni pedir sonido. Las lecturas de celda son Y y luego X, con cuatro sorteos por cierre exterior.
- `42ae60` compara **unionName** de la tabla del gremio. Libera la capa antes de comparar y conserva la prioridad nativa de los bits de atacante. El índice−1 usa el alias del miembro74.
- `413fe0/4147c0/414820` construyen y dibujan los ocho roles 6..13 con el atlas 637. El guild master elige las variantes de líder con guildName/unionName. El GM conserva su precedencia.
- `42ddb0` busca los cuatro puntos defensores `(8200,13000)`, `(10700,13000)`, `(9400,18200)`, `(9400,22700)`. Los atacantes necesitan ambas marcas y el mismo índice de gremio que Hero. Radio380 inclusivo; una distancia unordered entra, mientras una marca NaN sale por C3. Cada décimo paso emite 638.

## Emisiones y RNG

`42b540` conserva los sorteos de ambiente, piedras12b/12c y flechasDB. Las dos lecturas protegidas de Hero.Y son independientes; las emisiones llaman síncronamente al constructor y sus sorteos quedan entre los del caller. La fila usa desplazamiento inicial **menos n×60**, no división por n.

Las partículas638,5d9/5 y6ac/1 tienen constructor, paso y dibujo:

| Familia | Constructor | Paso propio |
|---|---|---|
|638|2 sorteos de XY, Z+240, vida 20|2 sorteos XY, Z+2, escala−.01 y RGB=vida×.05|
|5d9/5|3 sorteos: velocidadY, escala y rotación|escala−.04, rotación+5; sin damping ni crecimiento genérico|
|6ac/1|6 sorteos, incluidas vida y velocidades que se pisan; escala=input×.5|comparte los cuatro sorteos de6ac/0|

El pool lleno no consume los sorteos del constructor. El paso global de partículas conserva su viento y decrementa vida antes del caso; el último tick termina aunque marque muerto el lugar.

## Armas12b/12c

`44eea0/44efc0` conservan el dueño **Hero**, incluso si dispara otro actor; sonido 1ea también con tipo desconocido o pool lleno. El impacto remoto siempre crea onda 6eb/6 después del proyectil.

`51cfa4/51d1f0`: el tiro dirigido consume el sorteo inicial de velocidad aunque luego la pise con29. La dirección XY usa diferencia almacenada en float y producto por.001f, sin normalizar. Un lugar sin asignar no consume RNG.

`5208a0` consume un sorteo común por efecto vivo. El vuelo usa Z previa para la emisión y limita la cámara del tiro de Hero a800. La notificación B7 exige **ambos** bytes de key no nulos;12b añade vida < 250 y12c no. Los bytes se limpian después del intento de envío. El contacto asigna15/40 y luego el epílogo deja14/39; no ejecuta el impacto persistente en ese mismo tick.

`429ec0` busca el primer objeto activo, targetable y visible de su sector, en orden de inserción. La colisión aérea exige altura estrictamente menor que250 y conserva el cutoff90 anterior. El cuarto argumento de impacto sólo daña el objeto; el quinto argumento nativo no se lee.

`4295f0/429e40` conservan las acciones y radios. Fenrir elige acción local de sus manos y manda **5d** por el cable. El reset44dc40 precede al recorrido de200 efectos; cada proyectil seguido puede volver a escribir el destino de cámara.

`5f9580` deforma alturas radialmente con wrap de índices y sin piso. `5f8c10/5f8e40` refrescan una ventana8×8 centrada y restringida4..252; esta sombra usa coeficientes.5 incluso en mapa 30. `WorldTerrain.rawLight` retiene la fuente RGB que ese paso relee. El dibujo del terreno consulta las alturas vigentes cada frame; no hay un VBO de terreno por invalidar.

## Puertas y dibujo

Los tipos 3d..40 resuelven la tabla de constructor `51f9bc+(tipo−9)` a selector 1 y destino51c05e: **sólo vida 2** además del prefijo común. La tabla de paso `52cef4+(tipo−9)` da selector 55→52cb98: sorteo común, sin animación por ser tipo<ad, movimiento51fc80 y decremento de vida.

`42c500` restaura hidden −1 con field08=0. Con99 y seen, crea un efecto del mismo tipo/subtipo 1 y pide1ec si antes estaba en−1; después pone **hidden 0**, no−2. Siempre ejecuta la cola42b480.

`42cc10` en guerra dibuja sólo la malla 3 del tipo 13, con base, chrome y shiny. Los fragmentos de puerta llaman4ee770 sobre la malla 0 posada, sin otro cuerpo base.

`6002e0` para12b/c requiere dibujo específico: el12b normal tiene base blanca y chrome rojo con blendLight 1;12c conserva luz. El impacto cambia hidden 99→−2 **antes** de4ee770(12d/12e), sin base. Los fragmentos12d/e restauran blanco donde el ASM lo escribe;12f..133 conservan su luz.10a/b tienen sus pases y colores propios. La cola602365 usa flags78&2, el último color y blendMesh−2. Alpha<.01 o NaN sale antes de mutar el impacto.

El helper de vértices y los constructores/pasos12d..133 están en el bloque hermano `VertexEffects`/`SiegeVertexEffects`; no se sustituyen por un callback vacío. Una emisión en un slot posterior puede avanzar o dibujarse en el mismo recorrido ascendente.

`42ea00`, al golpear 19b/19c: puerta abierta no emite; los flags8000/10000 de Hero omiten sólo la onda 114/6. La puerta cerrada conserva cinco grupos con selector de altura, subtipo y modelo131..133; la estatua conserva cinco intentos de10b. Los sorteos del constructor quedan intercalados en cada intento.114/2 y6 se comparten con la cadena115/1 de Crywolf; sus cuerpos conservan el pase2 y la cola602365.

`517af8/520179` consulta el bit100 del ATT con las conversiones nativas de5f82c0, sin redondear antes el producto por−.01. En guerra, el radio de la skill incluye los actores de tipo1 aunque su key no sea el blanco; el bit100 corta antes de recogerlos. En el choque de flecha, el estado de guerra basta incluso fuera del mapa30: pone velocidad y vector en0 y divide la vida por2, antes del epílogo común.

Los misilesED/EE conservan el pase42 adicional de6002e0 antes de su cola602596. Ésta usa el índice durable del pool como dueño y vuelve a consultar la pose global **después** de publicarla; no conserva un span que pueda quedar colgando. La ausencia de BMD no elimina las cuatro partículas ni los diez sprites del original. Una matriz nunca publicada conserva el cero de BSS. El helper de esta cola y sus constructores/pasos vienen del bloque hermano de Crywolf.

## Espíritu de muerte de Crywolf

`51c952` crea147/0 con vida30, escala.3, velocidad local(0,−26,0), ritmo10 e iteraciones1. El offset del blanco viene de **la luz del argumento**, que el caller arma como(0,0,boundsTop); no es una copia de su posición. Conserva los ángulos de entrada en+28, y el cuerpo empieza con pitch/roll0 y ese yaw.147/1 copia el DWORD60 del ActorState del blanco, usa escala.2, alpha=(20−vida)×.2 y crea5db/0 de luz(.3,.4,1).5db/0 comparte los tres sorteos, paso y atlas de5d9/0; su material real esEffect\Fire03.jpg.

`52709b` lee la posición viva del blanco más el offset y recorre i=1 mientras i<iteraciones, conservando el sorteo del pitch y4e9810. El ritmo suma.4; con vida<10 suma.1 adicional antes de61e/1. Cada vuelta transforma la velocidad, mueve el espíritu y crea104/0. Después, las iteraciones suman.1 y se pide1ca. El epílogo anima y descuenta vida, pero **147 salta51fc80**: no se mueve otra vez.104/0 nace con vida6, descenso80, RGB.5 y yaw45; su paso resta el descenso, suma60 a éste y fija blendLight=vida×.05.145/147/104 conservan cuerpo2 y la cola602365.

Los61e/1 llevan al147 durable como dueño. `ModelEffect::owner.effect` y `JointOwnerKind::ModelEffect` conservan esa identidad en619/1, que lee su posición viva al girar y emitir el sprite final. No se interpreta el layout de unModelEffect como ActorState.

La primera61e/1 de cada paso con vida<10 recibe **[EBP−28] antes de que5271d3 escriba ese local**. Con el pool lleno,518cf0 no lo lee; con hueco se requiere la captura nativa de ese vector y se informa5271a8 si no está disponible. Las siguientes vueltas usan el vector ya calculado. El cálculo de altura de5273ff es local muerto y no cambia el estado observable del efecto. Esta lectura de pila impide certificar el recorrido completo sin la VM, aunque constructor, cuerpo y dependencias estén implementados.

## Misil E1/1 de Crywolf

`43252d` crea E1/1 al paso14 del modelo1b2, con ActorState de dueño y los bits de su WORD8a como key. El constructor51c713 crea610/13 con el **efecto durable** de dueño, ancho20 y key40; después copia el contador propio con su cierre CRT. El joint nace con vida40, key0,16 puntos, RGB(.7,.7,.3), mezcla4 y velocidad **70** (bits428c0000 en531ff9). Conserva el FirstQuad con el ancho anterior del slot. Su paso mueve antes de consultar si el dueño sigue vivo; aunque éste haya muerto, sigue con fade, un sorteo, luz dinámica, cola y vida. El fade bajo15 usa3f555555, y la luz es L=(rand&3+8)×.03, RGB(.4L,L,.8L), radio2. El dibujo común conserva la media textura y mezcla inversa aditiva.

`526961` emite dos60f/5 en las vidas pares mayores25. Cada grupo conserva tres sorteos del punto antes de los cuatro del constructor. La órbita nace con vida40 y descenso(rand%100×.01×5)+1, gira en radio40, baja por ese descenso y mueve sólo la X de su ancla +2.5; se achica.002 y apaga RGB por3f68ba2e con vida<=20. El billboard se dibuja ya en vida40. Con0/1/2 huecos se conservan7/11/15 sorteos del paso de E1/1, incluido el común. Su BMD ArrowNature conserva cuerpo2 y cola602365.

## Callers compartidos de los misiles

El productor de Crywolf432b30..432b63 (mapa34, modelo1b5, paso15) crea ED/1; el de fortaleza42fd04..42fd37 (mapa35, modelo1b2, paso14) crea E1/1. Ambos pasan posición, ángulos y luz del actor, subtipo1, actor de dueño, su WORD8a como key y cuatro ceros. El caller no sortea y retorna AL1 después del intento, también con el pool de modelos lleno. El constructor aceptado conserva el cierre del registro propio; un pool de joints lleno no omite esa copia. Los tests nuevos verifican argumentos, dueño durable, contador copiado, pools llenos y la salida antes de la cola que lee la skill. Su ejecución RED/GREEN requiere el WorldState de la integración estable y se registra por separado de la sintaxis local.

## Verificación y límites

Se verificaron por binarios focales GCC14: altas de monstruos15, capas28, preludio/marcador7, partículas5, bendición3, constructor de proyectiles3, vuelo5, colisión/deformación4, ondas2, recursos reales del minimapa1, derrumbe/efecto de puerta3, dibujo21, cola de misiles3, caller del golpe4, consulta de atributos15, espíritu/trail/owner durable8 y dependenciasE1/1 cinco. Los tests de dibujo incluyen variantes de guerra, impacto one-shot, alpha NaN, colores de la cola adicional, pose global redimensionada y colaED/EE sin BMD. El caller con constructores reales conserva55 sorteos con cinco fragmentos aceptados y15 con el pool lleno. Estos binarios usaron layouts coherentes por compilación; no acreditan una biblioteca mezclada con headers nuevos.

Las nuevas pruebas B7, SkillHits y choque de flecha con `WorldState`, MapLoad, viewport y recarga requieren la compilación integrada estable. Linux/Windows completos, revisión adversarial e interacción real en VM se deben registrar aparte.

El límite de los fragmentos 131 y10a/b que sortean polvo sobre `[EBP−28]` sin inicializar está documentado en `2026-11-18-h13-siege-body-chain.md`. No se inventa una posición para ese contenido de pila; con pool de partículas lleno, el callee no lo lee y continúa. Esta frontera impide declarar equivalencia dinámica total sin capturas del original. El perfil aritmético del port usa binary32/binary64 y PC53/nearest; no certifica cualquier control word o excepción x87.
