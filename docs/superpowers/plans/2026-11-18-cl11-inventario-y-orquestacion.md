# CL11 · Inventario y plan de ejecución

El objetivo es cerrar los recorridos de efectos de MuRet que todavía interrumpen una escena o una
habilidad. Cada lote cubre el alta, el paso, el dibujo y la limpieza que necesita su consumidor.
El ASM c32bbd5f fija los contratos de ejecución, con el pseudocódigo como guía de lectura.
Las representaciones nuevas usan efectos visuales propios de MuRet.
[Roadmap de CL11](../specs/2026-10-30-cliente-design.md#la-ola-de-efectos-visuales-cl11)

## Base verificada

| Referencia | Commit | Uso |
| --- | --- | --- |
| `main` actual | `35b6af6f` | Mismo árbol que `536b356d`, la cima de CL9 que midió Claude |
| Cima histórica del informe | `d3b01fdd` | Mismo árbol que `df2448f2` tras el rebase de #366 |
| Correcciones de audio | `a2d75a45` | #367, cambia recorridos de efectos en `MonsterHelpers` y los servicios de selección |
| Cima actual | `6c99cbe1` | #368, habilita la ruta de red de subida de nivel con constructores compartidos |

Binario de referencia `main.exe` SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
El cotejo de `.text` con el TSV de Claude reproduce las **1.463 direcciones y sus cinco destinos**.
El subtotal de **1.456** omite siete sitios que la matriz atribuye a código muerto.
Queda verificar las entradas indirectas calculadas de esas funciones.
[Matriz CL.M](../../../Client/docs/research/coverage/mainexe-roadmap.tsv)

| Destino | Operación | CALL directos brutos |
| --- | --- | ---: |
| `0x518cf0` | Alta de efecto con modelo | 378 |
| `0x530860` | Alta de joint | 269 |
| `0x53fe70` | Alta de partícula | 435 |
| `0x5491e0` | Alta de sprite | 378 |
| `0x52e590` | Dibujo de cintas | 3 |
| `0x52e470` | Alta o actualización de cinta | 22 |
| `0x52e500` | Paso de cintas | 1 |

Los tres sitios del dibujo son `0x63486f`, `0x637525` y `0x6375a1`. Al contar las cinco entradas
de alta o actualización, el bruto pasa a **1.482**. Salen **1.475 candidatos** si se aplica la
exclusión histórica de siete. El censo de siete destinos suma **1.486**.
`0x52e390` es un helper x87 de coseno. En sprites, `0x549471` distingue una textura en el dibujo
y `0x41496f` pasa la mezcla por un valor dinámico. El campo de subtipo del TSV requiere revisión
según la firma de cada familia.
[Render y paso de efectos](../../../Client/docs/research/06-render-escenas-y-ui.md#el-dibujo-del-juego)

**Tamaño de las raíces.** Las 43 entradas propias suman 71.229 B en CL.M. El recorrido de saltos
directos, condicionales e indexados de `0x5208a0` identifica 14.183 instrucciones en
`[0x5208a0, 0x52ce14)`, con su `ret` en `0x52ce13`. Son 50.548 B de cuerpo. Ghidra contabiliza
4.565 B discontinuos de ese cuerpo e incluye el `ret`. El tramo siguiente hasta
`0x52d220` contiene 1.036 B de tablas, mapas selectores y alineación. En las otras raíces,
85 B que sumaba el informe son relleno o funciones contiguas. Los 13 cuerpos suman
**162.715 B** y la suma con las 43 entradas propias da **233.944 B**. Esa suma incluye raíces
compartidas que también cubren trabajo ya hecho en CL7.

**Punto de partida para la cobertura.** El informe histórico etiqueta 341 llamadas como
portadas, 247 en rangos citados, 159 en un corte, 132 detrás de un corte por proximidad,
549 con función citada y 28 sin cita útil. Sus **400 de 661 regiones de switch sin cita**
orientan la lectura. Una región puede representar varios tipos o compartir una cola.
En `0x5208a0`, 213 de 233 llamadas están ausentes del export de Ghidra. La regla de tamaño
lineal del TSV marca 223 filas, incluidas 20 que sí figuran en el export.
Los 135 cortes históricos permiten priorizar rutas. Para medir avance funcional hace falta
comprobar la rama que llega a cada corte y el resto del ciclo con capacidad disponible.

## CL11.R · Primer lote

- [x] Fijar SHAs y cotejar por VA y destino las 1.463 filas suministradas con el desensamblado.
- [x] Verificar los límites del cuerpo de `0x5208a0` y los tres puntos de entrada de cintas.
- [x] Incorporar el censo reproducible al research a partir de la caché de `regen_cache.py`.
  El generador verifica el SHA de referencia, la unicidad de VA y el total por destino antes
  de emitir el TSV. Verificamos que el subconjunto original coincide con las 1.463 filas del
  informe. [Uso del generador](../../../Client/docs/research/scripts/tools/README.md#el-censo-bruto-de-efectos-cl11r)
- [x] Revisar las 22 altas de cintas, el paso y los tres dibujos contra `35b6af6f`.
  Veintiuna altas tienen su recorrido vinculado. En `0x4fd5a0`, el caso `0x194` en acción 3
  conserva una cinta y dos poses cuando hay espacio para la partícula `0x5dc/12`.
  El recorrido se detiene al llegar al alta pendiente en `0x4fd580`. Mantener la corrección
  en el lote de partículas y su consumidor.
  Queda probar una cinta viva en el pase específico del selector.
  [Evidencia por dirección](../../../Client/docs/research/coverage/cl11-cintas-c32.tsv)
- [ ] Indexar bloques de cada raíz por fase y destino de la tabla de salto e indicar las
  entradas que comparten bloque. Para cada llamada externa registrar la VA, la rama y su guarda,
  el tipo, el subtipo, los argumentos que lleguen por registros o memoria y sus posibles valores.
- [ ] Reconciliar las 549 llamadas sin resolver, las regiones sin cita y los cortes prioritarios.
  Registrar el símbolo C++ que consume la rama, el efecto previo y su estado de capacidad,
  el orden de RNG y efectos secundarios y qué test distingue implementación de diagnóstico.
  Un sitio con varias entradas conserva evidencia por rama.
- [ ] Vincular primero los efectos ya sostenidos por CL7.6, CL8 y CL9. Los efectos compartidos
  tienen una ficha con todos sus consumidores y la dirección de cada llamada.
  Mantener el alcance de cada ola según el roadmap.

### Controles del inventario

| Caso | Resultado que debe registrar CL11.R |
| --- | --- |
| `0x4d31b5` y `0x4d31e1` | #368 los conecta por `F3/05 → ApplyLevelUp → LevelUpEffect`. El TSV los marca sin cita en la base histórica. `TheLevelUpWireReachesTheWorldThroughTheRealSession` observa el recorrido actual. |
| `0x44267f` y `0x44274b` | El TSV los marca portados. La rama en `MonsterHelpers` llama a `Unported` con hueco y el test de `0x44274b` comprueba ese corte. La rama con el pool lleno puede continuar. |
| `0x441d7d` y `0x51fe9e` | #367 reutiliza `CreateParticleBurst`. El corte anterior `0x441774` permanece con capacidad. El helper se ejecuta desde otros consumidores. |
| `0x442cb9` | Con un hueco sigue el corte. En #367, si se llena el pool de modelos, el llamador pasa al sonido y puede encontrar el corte de partículas `0x442da1`. |
| `0x53d1a0` | La referencia en `OriginalRetainedMath.h` está escrita sin prefijo `0x`. Hay consumidor y test aunque el informe no la contó. |

Para repetir el censo usar GNU objdump con el PE del SHA fijado.
`objdump -d -j .text -M intel --no-show-raw-insn "$MURET_MAIN_EXE"` genera las instrucciones.
Comparar por conjunto de pares VA de instrucción y VA de destino. El PE estudiado produjo un
aviso de tabla de símbolos truncada. El barrido independiente de `.text` extraída reprodujo
los mismos pares. Los exports de Ghidra permiten consultar los segmentos de cada raíz.
[Entorno del research](../../../Client/docs/research/scripts/README.md#el-entorno)

## Reparto de los agentes

| Responsabilidad | Modelo | Entrega por lote |
| --- | --- | --- |
| Orquestación, dependencias y alcance del PR | GPT-6.2 Sol `xhigh` | SHA base, alcance, archivo responsable, pruebas y orden de integración |
| Búsqueda profunda en ASM y pseudocódigo | GPT-6.2 Astra `xhigh` | VAs, guardas, argumentos dinámicos, estado, RNG, orden y ramas pendientes |
| Implementación de un contrato acotado y correcciones | GPT-6.1 Sol `low` | Código propio sobre las APIs existentes y la prueba de regresión correspondiente |
| Cambios mecánicos, datos sintéticos y consolidación de evidencia | GPT-6 Luna `low` | Cambios en archivos asignados y verificación dirigida |
| Revisión adversarial | GPT-6.2 Astra `high` o `xhigh` | Contraste independiente de ASM y diff, saturación, orden y pruebas capaces de detectar la diferencia |

Los agentes de research trabajan en paralelo sobre raíces o consumidores distintos.
El orquestador asigna un responsable por archivo compartido. Si dos paquetes necesitan
el mismo cambio en un pool o en un pase, se integra primero ese contrato y se reutiliza.
El revisor recibe el binario, el diff y los tests y sigue las ramas desde la entrada de escena
o de red. Context7 sirve para consultar la API vigente de SDL y del backend de render.

## Primer piloto funcional

Validar el corte `0x5224ec` del paso de efectos con modelo. Las ramas de `0x5225bb` y
`0x52264e` figuran en un rango citado del TSV aunque MuRet se detiene antes de `0x52266a`.
`CreateCrywolfSpiritTrail` y `StepCrywolfSpiritTrail` proporcionan un punto de partida
ya implementado para el tipo `0x104`. El piloto comprueba reutilización del ciclo del hijo,
saturación parcial y continuación hasta el pedido de sonido y el paso común.
[Research de CL10 y dependencias de CL11](https://github.com/ema-ljn/MuRet/blob/a2d75a456c111f35ee8899e429c6163931fae525/Client/docs/research/2026-11-18-cl10-audio.md#dependencias-de-efectos-visuales)

- [x] Astra cotejó el constructor del hijo `0x5192a3` y su paso `0x5212e7`. El alta usa el primer
  hueco y conserva el RNG. El paso consume el sorteo común y lo libera tras seis pasos.
  El juego ya conocía el cuerpo del hijo y el selector usa el despachador común.
- [x] Ubicar la frontera siguiente. En vida 11 queda `0x521e13`, que requiere joints `0x617/0`,
  partícula `0x5c5/0` y el modelo `0x103/0`. El constructor y el paso de `0x103/0` siguen
  pendientes. La tabla de modelos de juego tampoco publica una malla para el hijo `0x104`.
  El piloto cubre el estado, el pedido de sonido y la continuación con ese recurso ausente.
- [ ] En `Client/tests/world/test_game_model_effects.cpp` cubrir estado 0 y vida 13 con
  capacidad normal, un hueco y pool lleno. Fijar los dos grupos de intentos, dos sorteos
  intermedios, el pedido `0x96`, el movimiento restante y el decremento.
- [ ] Conectar el paso de `Client/src/World/GameModelEffects.cpp` con el constructor existente.
  Comprobar el paso y la baja del hijo en la ruta real. Documentar cualquier diferencia
  del constructor compartido que aún requiera trabajo.
- [ ] Ejecutar `test_game_model_effects`, `test_crywolf_death_spirit`,
  `test_map_model_effect_services` y `test_selection_pipeline`. Probar el frame proyectado de
  cero pasos cuando se toque un pase. Verificar Linux y Windows sobre la cabeza final del PR.

En paralelo, CL11.1 puede investigar la rama de habilidad `0x39` que corta al crear
`0x60f/0x17`. Los sitios `0x4fa9ca`, `0x4fa9e8`, `0x4faa09`, `0x4faa81` y `0x4faa9f`
comparten constructor y paso. Con `2.3f < frame < 2.6f` se intentan las claves 2 y 3. Con
`frame > 5.0f` se intentan 0, 1 y 4 y se escribe el estado del ataque antes de pedir
`0x270`. Ambas ramas piden sonido aunque se llene el pool. El alta de este subtipo
no consume CRT. El paso del ataque conserva sus sorteos exteriores. MuRet tiene
pool, cola, dibujo y limpieza compartida. Quedan por implementar el estado específico
y el recorrido activo de este subtipo. Los joints de subida de nivel sirven como control
con otro subtipo. El caso que libera un hueco antes del alta pertenece a la habilidad
`0x10` y queda como control de saturación para otros lotes.
Usar `test_attack_frame_skills`, `test_game_joint_step` y `test_game_joint_drawing`
para observar los distintos puntos del recorrido.

## PRs e integración

| Sub-ola | Lotes iniciales | Criterio de agrupación |
| --- | ---: | --- |
| CL11.R | 1 | Censo, controles, dependencias y primer lote listo para estimar con evidencia |
| CL11.1 y CL11.2 | 2 como punto de partida | Ciclos de vida de joints y sus consumidores. Incluir los productores de cintas según el recorrido, después de revisar las 22 llamadas |
| CL11.3 | 5 a 7 | Efectos con modelo y partículas por grupos de constructores, pasos y pases comunes. Los sprites entran con sus productores |
| CL11.4 | 2 a 3 | Skills comunes del jugador, monstruos y eventos. Integrar cada consumidor con el ciclo que desbloquea y su guarda de capacidad |

La proyección inicial de Claude es **10 a 13 PRs**. Ajustarla al terminar CL11.R y medir
el piloto. Agrupar los casos que comparten dependencia y pruebas. Priorizar los cortes
de `0x4f5b70`, `0x4fd5f0` y `0x4f97b0` por frecuencia de la ruta y disponibilidad de los
constructores que necesitan.

CL11.R parte de `main`. Apilar el piloto sobre #367 si usa su audio o sus servicios de
selección. Incluir #368 como base cuando la ruta bajo prueba necesite `F3/05`.
Publicar cada lote al cerrar la compilación dirigida y la revisión para que el CI corra
sobre una cabeza estable. Después de un squash, comparar árboles y el diff del rango
para comprobar el rebase. Los resultados de CI quedan asociados a su SHA.

Registrar un corte retirado cuando el llamador complete su ruta con capacidad normal y
saturación, incluidos los efectos secundarios posteriores. Cada lote comprueba propietario
que desaparece, vencimiento, transición de mapa, consumo de CRT, redondeo y orden de pases
cuando intervengan. El tablero muestra ramas cotejadas con ASM, ramas implementadas y
ejercitadas, estado de integración y observaciones de VM. La VM va al final para contrastar
animación, materiales, mezcla, orden de dibujo y transiciones.
