# Plan de la ola CL7, segunda parte: Lorencia

> **Para quien ejecute:** SUB-SKILL REQUERIDA: superpowers:executing-plans (en línea, como la CL6 y el
> login) o superpowers:subagent-driven-development, tarea por tarea. Los pasos llevan checkbox (`- [ ]`).
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), § Plan por olas (CL7) y
> § Lorencia (CL7.R2). Escrito el 2026-11-05, después del barrido de Lorencia (cuatro lectores: la entrada
> y la escena, la cámara, el caminar, los mapas y los gates). La primera sub-ola va paso a paso; las demás
> traen su contrato, sus nombres y sus tests, y se detallan al arrancar con las interfaces que dejen las
> anteriores. Lo que se aprenda en una se escribe acá antes de la siguiente.
>
> **Quién escribe los pasos:** Claude, como en la CL6 y el login, hasta que Emanuel diga que participa.

**Objetivo:** que en MuRet se entre a Lorencia como en el original: el mundo del mapa 0 con su cámara, sus
2.870 objetos, su luz, su pasto y lo que vive en él; caminar con el clic, con el camino, el paquete y el paso
del original; y pasar por un gate a los otros mapas que trae el `Data\`. Es el "terminada cuando" de la CL7
en el roadmap.

**Arquitectura:** la `GameScene` (la escena 5) hace lo que `0x636290` (el paso) y `0x6371a0` (el dibujo), en
su orden. Reusa lo que dejó el login (`LoadedWorld`, `WorldObjects`, `DrawCharacter`, los efectos, las
partículas, `OriginalFrameCarry`), con las diferencias de la escena 5: la cámara que sigue al héroe
(`0x635ae0`), los objetos antes que los personajes, la luz dinámica del terreno y los casos del mapa 0. Lo que
es lógica sin GL va a `World/` y `GameLogic/` (la cámara, el camino, el paso entre celdas, los gates); lo del
cable, a `Protocol/`; la sesión lleva los estados nuevos. Cada cálculo del original con azar o x87 se compara
con vectores del código real emulado (`Client/docs/research/scripts/`), como en el login.

**Stack:** C++20, OpenGL 3.3 core (el `GlModelRenderer`), glm 1.0.3, GoogleTest. Ninguna biblioteca nueva.

**Spec:** el roadmap de arriba y el research que dejó el barrido: 06 § La escena del juego, § La cámara del
juego, § Lo propio de Lorencia, § Caminar y § Los cambios de mapa; 04 (el `F3 03` que vuelve, `F3 10`, `D3`,
`D7`, `1C`, `F3 04`, `F3 12`, `0F`); 05 (`Gate.bmd`, los bits del `.att`, los modelos con nombre de `Object1`);
los quirks nuevos del 07 y P-C28 a P-C31 del 08. Los scripts del barrido (los que emulan el código real, arman los vectores y comparan contra la traza y el
cable) están en `Client/docs/research/scripts/game/`, y las herramientas para leer el binario en
`scripts/tools/`. Los informes del barrido quedaron fuera de Git; lo que vale de ellos está en esos docs.

## Restricciones de todo el plan

- Las de `AGENTS.md` y `docs/CODING_RULES.md`: `clang-format-18`, `misc-include-cleaner` de `clang-tidy-18`
  limpio en los `.cpp` y los `.h`, el chequeo de conversiones de MSVC (`msvc-conv-check.py`),
  `MURET_WARNINGS_AS_ERRORS=ON` y la suite entera con GL y los golden.
- Una sub-ola, un PR contra `main` (o apilado sobre el anterior si todavía no entró, avisándolo arriba del
  cuerpo), con su CI seguido hasta verde. Revisión adversarial al final de cada sub-ola, antes del PR, y sus
  arreglos con un test que falló primero.
- Merge commits para traer `main`; force-push solo si Emanuel lo pide para ese PR.
- Lo que solo diría una corrida del original va al 08 (P-C28 a P-C31), con lo que se captura en la VM.
- Las constantes del binario van por sus bits donde el cálculo tiene que dar lo mismo que el original; los
  pasos se comparan con vectores del código real emulado, guardados en `Client/tests/vectors/` con su
  generador en `Client/docs/research/scripts/` y su fila en el README de la carpeta de vectores.
- Lo que el original corre por paso va por paso de lógica (25 por segundo); lo que corre por frame dibujado
  va con `Render::Models::OriginalFrameCarry` (uno por cada 46,875 ms en Decoupled, uno por dibujo en
  Faithful).
- El `rand()` es el de la CRT compartido (`ClothWorld::random`), en el orden del original; cada sub-ola que
  saque `rand()` lo compara con el estado del emulado.
- Ningún log ni test lleva un nombre de personaje ni de cuenta. De un cable se guardan solo los paquetes que
  el test lee y que no traen nombres: la respuesta del `F3 03`, el `F3 10`, `D3`, `D7`, `1C`, `F3 04`,
  `F3 12`, `0F` y `A3`. Nunca el chat, el `F3 03` que sale, las listas de personajes, el C2 `12`, el `02`, el
  `F3 01` ni el `F3 02`.

### Autoridad para continuar (pedido de Emanuel, 2026-11-05)

Emanuel autorizó continuar todas las CL7.x mientras está ausente y pidió fidelidad al binario,
**sin inferencias ni nuevas emulaciones**. Las tareas que proponían generar expectativas con
Unicorn se ejecutan ahora con el ASM contrastado y evidencia del cliente original. Los fixtures
emulados ya existentes conservan su procedencia como regresiones; no se presentan como
capturas nativas. Un contrato todavía inferido se verifica estáticamente de forma concluyente
o con una observación real antes de implementarlo como hecho. Después de las capturas de Lorencia,
Emanuel avisó que el servidor se cerró y el cliente dejó de responder. Se continúa con el ASM y la
evidencia guardada; las comprobaciones que necesitan juego activo permanecen pendientes.
Cada sub-ola mantiene las revisiones adversariales de Astra 6 Ultra y Claude Opus 5.5 xhigh,
además de los checks y CI. Esto no da por terminada ninguna funcionalidad pendiente.

## El entorno

Lo que necesita un agente para ejecutar el plan, y de dónde sale cada cosa: el detalle está en
[`Client/docs/research/scripts/README.md` § El entorno](../../../Client/docs/research/scripts/README.md#el-entorno).

- **El repo alcanza** para compilar, correr los tests y leer el research: el `Data\` del cliente está en
  `Client/assets/Data`.
- **El `main.exe` (`c32bbd5f`)** hace falta para armar los vectores (unicorn sobre el código real) y para
  leer el binario. Sale de `EmaNLJN/mu-server-repack-research-102i`, en
  `client-research/mainexe-vmxp-c32bbd5f/original/main.exe`, con sus funciones decompiladas al lado. Va en
  `MURET_MAIN_EXE`.
- **La traza de GL de Lorencia y el cable** hacen falta para los golden de la traza y los vectores del cable.
  Salen de `EmaNLJN/mu-online-reverse-lab` (la traza y el PCAP del PR #45 de ese repo; el `muproxy.log` en
  `analysis/mainexe/evidence/`), y van en `MURET_CLIENT_GL_TRACE_DIR` y `MURET_CLIENT_WIRE_DIR`. El
  `muproxy.log` lleva el login del jugador: no entra a MuRet.
- **Python** con `Client/docs/research/scripts/requirements.txt` (capstone, unicorn, pefile, numpy,
  Pillow). Se corre con `-I` y desde afuera de la carpeta de los scripts.
- **El desensamblado entero y el grafo de llamadas** se regeneran con `scripts/tools/regen_cache.py` en
  `MURET_RESEARCH_CACHE` (por defecto `.research-cache/`, fuera de Git). Con eso andan `tools/funcof.py`
  (la función de una dirección y quién la llama), `tools/reach.py` (si una función alcanza otra),
  `tools/disas.py`, `tools/rd.py` (las constantes) y `tools/jt.py` (las tablas de saltos).
- **Los scripts de cada sub-ola** están en `scripts/game/` por tema (`camera/`, `protection/`, `world0/`,
  `walk/`, `wire/`, `maps/`), cada carpeta con su README: qué dirección reproduce cada uno y qué sub-ola lo
  usa. Ninguno guarda salidas en Git.

## Decisiones de este plan

Las tomó Emanuel el 2026-11-05: **fiel al original**. Donde el original tiene un bug, MuRet lo reproduce y
el 07 lo marca como "se reproduce: comportamiento del original". Lo que solo sumaría fluidez a más FPS entra
si no choca con eso; si choca, queda para más adelante.

1. **El héroe y la cámara se dibujan en el estado del paso**, sin interpolar, en los dos modos, como el
   original. Interpolarlos en Decoupled daría fluidez a más de 25 FPS, pero choca con el original: el paso
   prueba el clic con el rayo del último dibujo (Q-C165), y un héroe o una cámara interpolados mueven ese
   rayo. Queda para más adelante. El cuerpo sigue interpolando su animación entre pasos (CL6.2), que no
   toca el rayo.
2. **La carga del mapa corre adentro del paso**, como en el original (`0x61a560` dentro del handler), con el
   frame retenido (`HoldsTheLastFrame`) mientras dura y mientras se espera la respuesta. Nada de hilos. No
   choca con los FPS: mientras carga no se dibuja, igual que en el original.
3. **Las variables protegidas gastan sus `rand()` como en el original.** El original guarda la celda del
   héroe, el contador `0x77f3b04`, el contador de frames, el registro propio y otros en una tabla cifrada
   (`0x57630c8`). Cada acceso que se cierra los vuelve a cifrar con `rand()`: 4 una variable chica
   (`0x412890`, Q-C125) y **3.848 el registro propio** (`0x7681eb4`, de `0xf08` bytes, en `0x40b4d0`). Gastan
   en cada paso y en cada frame también funciones que MuRet no porta: `0x57cfc0`, de las habilidades, cierra
   el registro al final de casi cada paso de `MoveHero` (3.848 `rand()` por paso); `0x508f00` gasta 8 por
   personaje vivo y por paso; `0x635ae0`, 8 por frame dibujado, también en el login y la selección. MuRet
   guarda los valores en claro, pero cada acceso que corre en el original gasta lo mismo en el mismo lugar,
   lo porte MuRet o no (CL7.P). Los vectores del emulado se arman con la tabla viva, para que los cuenten.
4. **Sin HUD hasta la CL8.** El mundo va en (0, 0, 640, 432) como en el original, con el ancho de 640 fijo
   hasta que la CL8 traiga las ventanas (`0x5bb8d0`: 450 con una, 260 con dos). Del 2D de la escena 5 va
   solo el cursor del juego (`0x5ac2d0`, en `0x6376d7` y `0x6376e1`), como en el login y la selección: sin
   él no se vería dónde se hace clic. Lo demás del 2D queda en negro.
5. **El estado de la cámara es global, como en el original:** la D, el zoom, el yaw, el nivel, el far y la
   altura del `0x40` viven entre escenas, y el login y la selección también pasan por `0x635ae0` (CL7.3 B).
   Así se reproduce P-C27 b, el zoom que quedó del juego y corre la cámara del login, y la altura de 1.201
   que queda después del primer `0x40` (Q-C150).
6. **Lo que el cliente le dice al servidor se copia aunque sea raro:** el camino de 15 celdas sale como 14
   (Q-C158), el gate se pide desde el cliente con su `Gate.bmd` aunque difiera del `gate.txt` (Q-C182), y un
   rebote del servidor se acepta.

## Review Focus

1. **Entrar otra vez y cambiar de mapa** (juego → selección → juego, o un gate de ida y vuelta): el mundo se
   vuelve a cargar, los objetos y los efectos salen de cero, la cámara arranca con el yaw −45 y el héroe va
   donde dice la respuesta nueva; con el mismo mapa, se borran los otros personajes y no se recarga nada.
   Test en cada sub-ola que lo toque.
2. **Un archivo que falta** (un modelo o una textura de `Object1`, `World6` sin `TerrainHeight`, un mapa sin
   carpeta como el 35): el original saltea algunos sin avisar (Q-C23) y se cierra con otros (Q-C180); MuRet
   saltea lo mismo, con un aviso en el log, y se cierra donde el original se cierra, con el aviso que nombra
   el archivo.
3. **FPS altos o un frame largo:** los objetos, las hojas, los pájaros, los peces, los fuegos y el caminar
   avanzan por paso a cualquier FPS; el zoom y la D de la cámara, por frame del original; un frame de 2 s no
   mueve al héroe más de lo que caminaría en 50 pasos, ni le saltea una celda del camino.
4. **El orden de los `rand()`:** todo lo vivo de Lorencia saca del mismo `rand()`. Uno de más o de menos corre
   todo lo que viene después, incluidas la tela y el cetro, que ya son bit a bit. Desde la CL7.P, cada
   sub-ola compara el estado del `rand()` con el emulado con la tabla protegida viva, después de cada paso y
   de cada dibujo.
5. **La resolución** (640 × 480, 1024 × 768 y una ventana ancha): el pase del mundo va en (0, 0, 640, 432)
   escalado y con el aspecto del viewport (1,4819 en 1024 × 768); el rango de bloques sale de la vista de la
   franja; los bloques y los objetos del borde del trapecio no aparecen y desaparecen al caminar.

## Orden y hitos

Estado contrastado con `origin/main` **`43b2f26`**, el código y los merges de GitHub, 2026-11-07.
PR144, PR146, PR149–159, PR161 y PR162 están mergeados. Los componentes de esas entregas ya están incorporados
en `main`; sus consumidores pendientes se enumeran por separado.
«Componente puro» no significa que la funcionalidad esté integrada en la escena.

**Criterio de Emanuel, 2026-11-06:** el gameplay de CL7 debe funcionar en todos los mapas de juego
soportados por el original. Lorencia es el primer escenario de integración, no el criterio de cierre.
Resolver una carpeta, parsear datos o dibujar un frame no acredita movimiento ni transiciones. La
verificación por mapa debe cubrir entrada, caminar/picking/colisiones/altura, cámara/recorte,
objetos y efectos pertinentes, warp/reaparición/reentrada y el estado que se conserva o limpia.
Las condiciones espaciales y visuales propias de cada mapa pertenecen a CL7; las reglas y los
paquetes específicos de los eventos siguen en CL9. La matriz abarca los mapas de juego nominales
0 a 36; 54/55 conservan su regresión de selección/login. No se inventan los recursos faltantes de 5/35.

| Sub-ola | Estado verificable | Qué falta para su alcance |
| --- | --- | --- |
| CL7.3 A | integrado por [PR138](https://github.com/ema-ljn/MuRet/pull/138) | la integración de la cámara en el mundo jugable pertenece a L1 |
| CL7.3 B | integrado por [PR141](https://github.com/ema-ljn/MuRet/pull/141), CI verde | sub-ola completa; no cierra los demás efectos ni las escenas |
| CL7.P | fase documentada en [PR144](https://github.com/ema-ljn/MuRet/pull/144) e incremento modular incorporado por [PR151](https://github.com/ema-ljn/MuRet/pull/151); abierta | estado lógico inicial, fase nativa, nesting y orden observable de RNG; sin portar protecciones |
| CL7.L1 | lector F3/03 incorporado por [PR149](https://github.com/ema-ljn/MuRet/pull/149) y procedencia del descifrado por [PR154](https://github.com/ema-ljn/MuRet/pull/154); recepción ordenada F3/03–F3/10 incorporada por [PR162](https://github.com/ema-ljn/MuRet/pull/162) | aplicación con contexto histórico y consumo antes del frame, espera retenida, InWorld, carga del mapa recibido, héroe y pase 640×432 |
| CL7.L2 | modelos con nombre incorporados por [PR142](https://github.com/ema-ljn/MuRet/pull/142), margen individual/Bridge01 por [PR150](https://github.com/ema-ljn/MuRet/pull/150) | creación/paso/dibujo completos de 2.870 objetos y orden completo del pase |
| CL7.L3 | lector puro F3/10 incorporado por [PR159](https://github.com/ema-ljn/MuRet/pull/159), contrastado con siete capturas reales; recepción ordenada incorporada por [PR162](https://github.com/ema-ljn/MuRet/pull/162) | decoder semántico de ítems, inicialización/aplicación, equipo del mundo, matriz de parado, aparición y zonas seguras/mascota |
| CL7.L4 | pendiente | luz dinámica compartida y pasto, incluidos carga/RNG y vértices |
| CL7.L5 | pendiente | hojas/clima, pájaros, peces, fuegos y fuente |
| CL7.4 A | componente puro integrado por [PR143](https://github.com/ema-ljn/MuRet/pull/143) | caminar a distancia cero implementado; consumidor/RNG pendientes en4E/P. La búsqueda por radio queda fuera de ese contrato; no confundir pruebas dirigidas con los 171 caminos del cable |
| CL7.4 B | componente puro integrado por [PR139](https://github.com/ema-ljn/MuRet/pull/139) | consumidor y envío en la escena, CL7.4 E; precisión nativa del emisor pendiente |
| CL7.4 C | función completa de velocidad `0x508940` incorporada por [PR155](https://github.com/ema-ljn/MuRet/pull/155) | consumidor de velocidad, spline, celda lógica, giro, posición/altura, acciones y orden exterior; cinco muestras x87 abiertas en P-C32 |
| CL7.4 D | componente completo incorporado por [PR153](https://github.com/ema-ljn/MuRet/pull/153) | conectar al rayo y clic de la escena en 4E |
| CL7.4 E | pendiente | clic → camino → D3 → movimiento; cooldowns, D3/D7 recibidos y recorte |
| CL7.5 A | infraestructura genérica parcial; identidad efectiva de carga incorporada por [PR161](https://github.com/ema-ljn/MuRet/pull/161) | todas las familias de modelos/mapas, variantes 30/34, estados de carga y errores del Data original |
| CL7.5 B | carga y decisión pura incorporadas por [PR152](https://github.com/ema-ljn/MuRet/pull/152) | conectar caller/tiempo lazy/sesión/efectos; falta el consumidor en la escena |
| CL7.5 C | componente puro integrado por [PR140](https://github.com/ema-ljn/MuRet/pull/140) | espera de warp y aplicación en escena después de L1 |
| CL7.5 D | pendiente | carga dentro del paso, retención, limpieza selectiva, reconstrucción y vuelta por gates |
| CL7.5 E | componentes de cámara/terreno, paso de ondas de Kalima [PR157](https://github.com/ema-ljn/MuRet/pull/157) y núcleo completo del trapecio [PR158](https://github.com/ema-ljn/MuRet/pull/158) incorporados en main | integración temporal del trapecio, resto del agua de Kalima, particularidades de otras familias y frame integrado por mapa |
| CL7.6 y arrastres | nubes integradas por PR147; efectos restantes parciales | neblina visual pospuesta; fuego, selección, luces, armas y casco/polvo siguen abiertos |
| Cierre | abierto | barrido transversal con ramas, estado, orden, transiciones y evidencia nativa pendiente |

### Componentes incorporados (2026-11-07)

Los siguientes alcances de PR149–155, PR157–159, PR161 y PR162 ya forman parte del código de `main`.
PR156 incorporó la actualización documental previa del roadmap.

- [PR149](https://github.com/ema-ljn/MuRet/pull/149): lector puro completo F3/03, 23 campos;
  la aplicación a sesión/actor/escena sigue en L1. La procedencia se conserva en PR154.
- [PR150](https://github.com/ema-ljn/MuRet/pull/150): margen individual de recorte y Bridge01 de
  Lorencia; continúan los otros estados, poses, paso y dibujo de L2.
- [PR151](https://github.com/ema-ljn/MuRet/pull/151): incremento modular definido del contador lógico
  con lectura firmada; CL7.P conserva los contratos pendientes de fase/estado inicial/nesting/RNG.
- [PR152](https://github.com/ema-ljn/MuRet/pull/152): carga obligatoria de `Gate.bmd` y decisión pura
  de gates incorporadas. Caller, tiempo lazy y aplicación de las decisiones pendientes.
- [PR153](https://github.com/ema-ljn/MuRet/pull/153): componente completo de picking del terreno,
  con ambos retries, Target y DontMove. Las brechas de tests se corrigieron y pasaron la segunda
  revisión cruzada antes del merge. No integra la escena 4E.
- [PR154](https://github.com/ema-ljn/MuRet/pull/154): procedencia del descifrado por Packet hasta
  el handler, incluidas ambas reconstrucciones FoxAnti, fragmentación y resync; incorporada tras
  revisión cruzada. No implementa todavía la admisión F3/03 ni la entrada al mundo.
- [PR155](https://github.com/ema-ljn/MuRet/pull/155): función completa de velocidad `0x508940`,
  con Run por actor y latch/contador compartidos, incorporada tras revisión cruzada. El resto de
  CL7.4 C sigue abierto.

- [PR157](https://github.com/ema-ljn/MuRet/pull/157): paso entero de ondas de Kalima `0x41dbe0`,
  dos planos 256×256 dentro del dominio explícito de slots0/1. Faltan perturbación, otros planos,
  geometría, render y consumidor. El paso completo no cierra el agua de Kalima.
- [PR158](https://github.com/ema-ljn/MuRet/pull/158): núcleo completo del trapecio `0x5fb570`,
  diez perfiles, nivel efectivo explícito y consulta congelada; conserva el wrapper de login/selección.
  ADR0009 documenta los errores portables. Faltan el dueño/inicio real del estado, los pares
  cámara→trapecio en su cadencia y la integración en escenas. La proyección FP no acredita igualdad
  universal libm/x87 ni cierra los demás casos de mapa.
- [PR159](https://github.com/ema-ljn/MuRet/pull/159): lector puro completo F3/10, con slots y siete
  bytes opacos por entrada del wire, bajo entrada estable. ADR0010 fija el error de truncamiento.
  El cotejo de `0x4c55a0` no atribuye siete lecturas al callee ni prueba por sí solo C4→C2:
  el tamaño opaco se apoya en stride y captura; el C2, en el transporte/ADR0003. La inicialización,
  admisión, potenciales mutaciones, decoder, efectos/RNG y aplicación al héroe siguen en L3/P.
  El golden incorporado contrasta siete C4 reales y sus C2 normalizados con una referencia
  independiente, incluidas las 33 entradas y los 231 bytes opacos de ítems. Es evidencia de una
  captura histórica; no acredita la aplicación al héroe ni una nueva observación viva del original.

Los CI de los heads finales de PR157–159, PR161 y PR162 pasaron en Linux, Windows y formato.
Estas entregas no equivalen al cierre de L1, L2 ni P. PR144 ya incorporó la precisión documental de
fase: resto firmado 57 al dividir por 313, condicionado al snapshot previo y sin inicio nativo supuesto.

### Últimas incorporaciones y validación

- [PR161](https://github.com/ema-ljn/MuRet/pull/161), merge `740f9cd`: dos observaciones de mapa
  independientes y consumidor local de identidad efectiva en LoadedWorld; terreno, luz, texturas y
  vista reciben el mismo mapa efectivo, y 32 se publica como 9.
  Conserva los 256 resultados de WorldFolderNumber. No completa `0x61a560`, sus modelos o errores.
- [PR162](https://github.com/ema-ljn/MuRet/pull/162), merge `43b2f26`: una secuencia propietaria F3/03–F3/10
  conserva orden y multiplicidad sin cambiar estado ni deadline. Sólo recepción; no aplica el mundo.
  El aplicador deberá resolver contexto histórico, retención y progreso: el estado al drenar no
  reconstruye necesariamente el estado al recibir dentro de un mismo Poll. Debe distinguir el ID
  del wire del efectivo de carga, y consumir antes del paso/dibujo, incluso en un frame sin pasos.

Ambos PR tienen revisión cruzada Astra6Ultra/Opus5.5xhigh y sus correcciones obligatorias cerradas.
El ensamblado local `e988fe3` combina los heads finales revisados `425b633` y `06e2258` sobre
`61c568d`: build con warnings como errores,
2.648 tests incluidos 83 GL y cero fallos, errores, omisiones, deshabilitados o no ejecutados.
Se comprobaron los hashes de sus 14 archivos. Después de los merges, el árbol completo de
`origin/main` (`43b2f26`) coincide con el de `e988fe3`; por eso la evidencia corresponde también al
mismo contenido incorporado. No se ejecutó otra fullsuite por esta actualización documental ni se
acredita equivalencia completa con el original. Los CI de los heads finales son
[PR161](https://github.com/ema-ljn/MuRet/actions/runs/42343179748) y
[PR162](https://github.com/ema-ljn/MuRet/actions/runs/42343886929), ambos correctos en los tres jobs.

Una entrada observada del original en XP registró 4.500 muestras pasivas sin errores: selección,
carga y mundo en Lorencia, con `0x3c` observado antes de `0x3d`. La primera muestra en `0x3d` ya tenía
alpha 0,15. La captura es secuencial, no atómica ni sincronizada con pasos/frames; no acredita el primer
frame, el origen del stamp del constructor, el estado lógico inicial o la fase/nesting/RNG global.
El archivo crudo queda fuera de Git; SHA-256
`2bbb9975a9419ccb4602d359953ed2c7853bd46333d9d36f509624266e2fea74`.

L1 necesita efectos concretos de creación y equipo que cruzan L2/L3: el handler original los aplica
antes de publicar el héroe y `0x3d`. Los contratos de constructor, limpieza y equipo no permiten asumir un actor vacío,
un slot fijo ni equipo inicial vacío. No reemplazar esos efectos pendientes por no-ops para declarar L1.

### Siguiente tramo de integración

1. **L1 — aplicar la recepción:** fijar consumo, contexto histórico y tratamiento de fallo/reinicio
   dentro de cada Poll; conservar el orden de efectos antes de Step/Render, también sin pasos.
2. **L1 con los efectos concretos de L2/L3:** cargar el mapa recibido con su identidad efectiva,
   crear y equipar al héroe conservando el orden nativo de las publicaciones del héroe/estado y de
   los efectos de aparición, antes de presentar el mundo con cámara y pase 640×432. La espera
   debe retener el frame anterior.
3. **Completar Lorencia:** objetos/pases de L2, semántica de inventario y equipo de L3, luz/pasto
   de L4 y ambiente de L5; preservar estado y orden de RNG del original.
4. **Movimiento y transiciones:** integrar CL7.4 E y lo que falta de 4C; después conectar gates,
   warp, reconstrucción y regreso de CL7.5 B/C/D.
5. **Cobertura de todos los mapas y cierre:** completar familias, variantes y errores de 5A/5E,
   los arrastres de 6 y el barrido transversal. P acompaña estos tramos: siguen abiertos el estado
   lógico inicial, la fase, el nesting y el orden observable de RNG.

### Lo que todavía impide declarar completa toda CL7

- `GameScene::Render` sigue vacío, `HoldsTheLastFrame` devuelve false y la sesión no define `InWorld`:
  llega hasta `EnteringWorld`. La sesión ya recibe y conserva F3/03 y F3/10 en orden, pero
  `TakeWorldEvents` aún no tiene consumidor productivo que aplique el mundo. El picking, `GateCheck`
  y `UpdateMovementSpeed` siguen sin integrarse en la escena. La cámara,
  los modelos y los paquetes puros ya integrados no equivalen a tener Lorencia jugable. L1 debe unirlos sin
  adelantar la presentación ni perder el frame retenido durante la espera.
- L2 debe cubrir todos los objetos y sus casos de material, animación, luz, fuego, casas y fuente;
  L3 debe usar el equipo del mundo y el parado correcto, incluidos safe zone y ocultación del caballo.
  L4/L5 llevan la luz compartida, pasto y fauna/partículas, con sus efectos sobre el RNG.
- El caminar requiere integrar A/B/D, completar C y cerrar E con entrada, cable y movimiento.
  La velocidad ya está implementada; el giro conserva el pendiente nativo P-C32.
  No alcanza con que el pathfinder y el serializador pasen sus suites por separado.
- «Todos los mapas» todavía está abierto. Hay 20 carpetas físicas de mundo y 39 entradas nominales
  de mapa, con reutilización de carpetas: el mapa32 usa World10 y entra con el estado9; World6 está incompleto y el mapa35 no
  tiene su carpeta. Hace falta verificar cargas, modelos/efectos especiales y política de errores
  por familia, además de gates/warp y viajes de ida y vuelta. Una prueba de rutas no verifica dibujo.
- PR147 ya integra las nubes del login. Emanuel pospuso la comparación y el ajuste visual de
  neblina a seguimiento: no bloquean las otras CL7.x ni acreditan igualdad visual. El fuego `0x59`
  sigue abierto. PR145 retiró el glifo `_`; el caret rectangular y bitmap del EDIT sigue abierto.
  PR148 restauró el keepalive esperando credenciales; su comprobación con GS real queda pendiente.
  En selección faltan el mundo54 renderizado, altura del preview de creación y efectos propios,
  además del retorno desde el juego por F1/02 con resultado1 y renovación de la lista. El cierre del login
  conserva los pendientes de mensajes, mouse/cursor, puesta al día tras un frame retenido y tests
  de estado/cadencia; ver P-C25–27 en 08.
- CL7.P debe cerrar valores/fases lógicos y el consumo/orden de azar comprobable. No es una
  autorización para portar la VM, cifrado de protección, licencias o anticheat. Los contratos
  puros ya comprobados pueden avanzar en paralelo; la equivalencia global de cadencia/RNG no se
  declara cerrada mientras falten consumidores y orden.
- P-C32 registra cinco muestras diferidas de C0 en los tres helpers angulares. Intel deja ese bit
  indefinido tras las instrucciones interpuestas; los bytes están contrastados, pero no permiten
  elegir el resultado por sí solos. Se requiere observación nativa antes de portar esos helpers
  como equivalentes; no se reemplazan por una función matemática estándar por semejanza.
- Los 2.250 samples nativos de cámara a 40ms no son una captura de cada LogicStep ni de cada
  frame; las posiciones nativas guardadas son limitadas. Los vectores emulados históricos sirven
  como regresión, no como una nueva captura independiente. No se generan nuevas emulaciones.

### Orden para continuar y criterio de cierre

Se pueden paralelizar gates puros, movimiento/picking, contratos de cargas y efectos. La integración
requiere primero fijar en **P** los valores/fases lógicos que consumirá L1; PR144 por sí solo no
la desbloquea. Luego sigue **L1 → L2 → L3/L4/L5**, **4C/4D → 4E** y **5A/5B/5C → 5D/5E**. Cada concepto mantiene
su PR, pruebas discriminantes y revisiones de Astra 6 Ultra y Opus 5.5 xhigh. Los dictámenes favorables
del wrap de PR151 corresponden a esa unidad aritmética. La revisión histórica incompleta de Opus
sobre el contrato global de P no cuenta como aprobación de ese contrato, que sigue abierto.

El cierre exige inventario de funciones/ramas/llamadas, estado persistente, efectos secundarios y
orden; contratos pendientes explícitos; expectativas contrastadas con ASM/PE y observación nativa
cuando corresponda; pruebas de entrada/reentrada, mismo y distinto mapa, frames0/1/varios, ventanas
640×480/1024×768 y archivos ausentes. Tests verdes o un PR mergeado no sustituyen ese barrido.
HUD/ventanas del juego son CL8; NPC, otros jugadores y reglas de combate/eventos son CL9; audio es CL10.


---

## Sub-ola CL7.3 A: la cámara del juego

**Contrato:** 06 § La cámara del juego. `0x635ae0` corre una vez por frame dibujado, dentro del dibujo
(`0x6371a0` → `0x637241`), con el héroe del último paso. El original da la cámara de la traza de Lorencia
bit a bit con el héroe en (14.350; 12.050; 165). PR propio.

### Tarea 1: el `VectorIRotate` de `0x5fcc30`

**Archivos:**
- Modificar: `Client/src/Render/Models/OriginalRotation.h`, `OriginalRotation.cpp`
- Test: `Client/tests/render/test_original_rotation.cpp`

**Interfaces:**
- Produce: `[[nodiscard]] glm::vec3 OriginalVectorIRotate(const glm::vec3& v, const OriginalMatrix& m);`
  con la transpuesta de la parte de giro, en double y cada componente guardada en float, en el orden de
  sumas del binario: `x' = (m8·z + m4·y) + x·m0`, `y' = (m1·x + m9·z) + m5·y`, `z' = (m2·x + m10·z) + m6·y`.

- [x] **Paso 1:** test `OriginalRotationTest.TheInverseRotateSumsLikeTheBinary`: con
  `OriginalAngleMatrix({0, 0, -45})` y (0, −1.000, 0), sumado al héroe (14.350; 12.050) y guardado en float,
  da los bits `0x466b446d` y `0x46313b93` (los de la traza); y los casos de `world55/rotation.txt` con la
  transpuesta, contra una cuenta en double hecha a mano en el test.
- [x] **Paso 2:** verlo fallar (la función no existe).
- [x] **Paso 3:** escribir `OriginalVectorIRotate`.
- [x] **Paso 4:** verlo pasar, con `test_original_rotation` entero. Commit.

### Tarea 2: los vectores de `0x635ae0`

**Archivos:**
- Modificar: `Client/docs/research/scripts/game/camera/make_camera_vectors.py` (el borrador del barrido, que
  ya corre `0x635ae0` con `emu_camera.py`; su reimplementación en doubles es `camera_reference.py`)
- Crear: `Client/tests/vectors/game/camera.txt` y `Client/tests/vectors/game/README.md` (como el de
  `world55/`)

- [x] **Paso 1:** el generador corre `0x635ae0` real, frame por frame, con unos 3.000 frames en casos al
  azar (cinco semillas): el héroe, el byte del atributo de su celda (0, `0x01`, `0x40`, `0x80` y `0xc0`), el
  temblor, el reloj, el mapa (0, 2, 3, 5, 6 y `0x1e`), la escena (2, 4 y 5), "webzen" en el 6 (prendido y
  apagado), la D y el zoom de entrada, y los bordes del rectángulo del far (16.100, 19.000, 18.900 y 21.700,
  ±0,01). Cada frame lleva la entrada y la
  salida en hexadecimal: posición, ángulos, far, D, meta, zoom, nivel y la altura del `0x40`.
- [x] **Paso 2:** correrlo con `MURET_MAIN_EXE` y guardar `camera.txt`, con su README.
- [x] **Paso 3:** commit.

### Tarea 3: `World::GameCamera`

**Archivos:**
- Crear: `Client/src/World/GameCamera.h`, `GameCamera.cpp`
- Test: `Client/tests/world/test_game_camera.cpp`

**Interfaces:**
- Consume: `Render::Camera3D`, `World::CullingScene`, `OriginalAngleMatrix`, `OriginalVectorIRotate`.
- Produce:
  - `struct GameCamera`, lo que queda de un frame al otro: `float yaw = -45` (`0x7d36870`, que escribe el
    primer paso de la escena 5), `float distance = 1000` (`0x7d36ba4`), `float zoom = 0` (`0x7d3689c`),
    `int level = 0` (`0x7d3a0ec`), `float farPlane = 2000` (`0x6d5d44`, que se lee con el nivel del frame
    anterior), `float fixedHeight = 1200` (`0x6c9d5c`, 1.201 desde el primer `0x40`).
  - `struct GameCameraInput { glm::vec3 hero; std::uint8_t cellAttribute; std::uint8_t map; CullingScene scene; float shake; float worldTime; bool webzen; glm::vec3 effectTarget; };`
    con el `World::CullingScene` que ya tiene `TerrainCulling.h` (2, 4 y 5, las escenas del original). El
    atributo es el byte bajo de la celda lógica del héroe (`+0x41c`, `+0x420`, la que va una adelante,
    Q-C173); `worldTime` es el float de `0x5767b88`, en ms, el que lee el balanceo del mapa 5;
    `effectTarget` (0, 0, 0) es "sin desvío" (`0x724588`).
  - `[[nodiscard]] Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input);`: una
    vuelta de `0x635ae0`, en el orden del binario (06): fov y ángulos, el far por el rectángulo o el nivel,
    el corrimiento `OriginalVectorIRotate((0, −D, 0), AngleMatrix(0, 0, yaw))` con la D de antes, el
    objetivo, la posición en el plano y en z (`(D − 150) + z0`, con el `0x40`), el pitch (−48,5 + temblor en
    la 5), el zoom (±10 con el `0x80`, hacia el héroe con el giro fijo de −45), los finales del mapa 5, el 6
    con "webzen" y el `0x1e`, y la D suavizada por un tercio hacia la meta.
  - `[[nodiscard]] Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input);`: la
    misma cámara sin avanzar el estado, con la D, el zoom y el far del último `StepGameCamera`, para los
    dibujos de Decoupled sin un frame del original (`OriginalFrameCarry` en 0), con el héroe del último paso.
  - El atributo se lee de la celda del servidor del héroe, que pasa por la tabla protegida y gasta sus
    `rand()` en el dibujo: lo agrega la CL7.P, con la celda del héroe de la L1.
  - El recorrido de `0xBD 00` (`CCameraMove`) no va: el `.h` lo dice, con su dirección.

- [x] **Paso 1:** los tests:
  - `GameCameraTest.TheCameraOfLorenciaIsTheOneInTheTrace`: el héroe (14.350; 12.050; 165), el atributo
    `0x01`, el mapa 0 y la escena 5 dan la posición con los bits `0x466b446d`, `0x46313b93` y `0x447dc000`,
    los ángulos (−48,5; 0; −45), el far 2.000, el fov 35 y el near 20;
  - `GameCameraVectorsTest.EachFrameMatchesTheRealCode`: `camera.txt`, bit a bit en cada campo;
  - `TheZoomGoesTo210InTwentyOneFramesAndBack`, `TheBit0x40LeavesTheFixedHeightAt1201`,
    `TheFarIs3000InsideTheRectangleOnAnyMap` (los cuatro bordes adentro y ±0,01 afuera, en el mapa 0 y en
    el 3), `TheFarUsesTheLevelOfTheFrameBefore`, `TheDistanceSettlesAt0x447a0001` (desde 1.400),
    `TheShakeAddsToThePitch` y `GameCameraOfRepeatsTheLastStep`.
- [x] **Paso 2:** verlos fallar.
- [x] **Paso 3:** escribir `GameCamera`.
- [x] **Paso 4:** verlos pasar; los mutantes del barrido (el signo del zoom, el −150, el orden de las sumas
  de `0x5fcc30`, la D vieja contra la nueva) tienen que romper alguno. Commit.

### Tarea 4: la traza de Lorencia con la cámara calculada

**Archivos:**
- Modificar: `Client/tests/render/test_golden_pass_3d_trace.cpp`, `Client/tests/world/test_golden_terrain_trace.cpp`

- [x] **Paso 1:** `GoldenPass3DTrace.TheLorenciaWorldPassMatches` y los dos de `GoldenTerrainTrace` de
  Lorencia toman la cámara de `StepGameCamera` con el héroe de la traza, no la escrita a mano.
- [x] **Paso 2:** verlos pasar (y fallar con el yaw en −44).
- [x] **Paso 3:** docs: 06 § La cámara del juego (lo que ya es MuRet), los quirks de la cámara del 07 con
  su columna de MuRet, P-C29 con lo que sigue abierto. Commit, la suite entera, la revisión adversarial y el
  PR.

---

**Hecho (2026-11-05, resuelto por Codex).** `OriginalVectorIRotate`, `World::GameCamera`,
`StepGameCamera` y `GameCameraOf`; 3.000 frames del código real emulado en 60 casos, cinco semillas,
con todos los campos de cámara y estado comparados por bits. Las cámaras fijas, el mapa 6 con su
rama especial, el mapa 5, el desvío de efectos, los bordes del far y el estado previo están cubiertos.
`GameCameraOf` conserva solo la D usada antes del suavizado para los dibujos intermedios, y toma
el héroe del último paso lógico sin avanzar zoom, D ni far. La integración de las escenas es
CL7.3 B; los ocho `rand()` protegidos por frame son CL7.P.

Los golden del pase y de las celdas de Lorencia usan la cámara calculada desde el héroe de la traza.
Build GCC 15.2 con warnings como errores y sin PCH; `misc-include-cleaner` 18 en los ocho `.cpp`/`.h`
cambiados y `clang-format-18`, limpios. Suite sin GL: 2.165 casos registrados, cero fallos, 30
salteados porque no se configuraron volcados privados ni las fuentes originales; GL: 83/83 en
Xvfb y Mesa. Los tests de red y GL requirieron salir del sandbox para abrir sockets locales y el
display. `msvc-conv-check.py` no está versionado ni disponible en este entorno; la compilación de
MSVC con warnings como errores queda a cargo del job de Windows del PR.

Astra 6 Ultra encontró que las 400 entradas de giro no observaban una permutación de sumandos.
Se agregaron tres casos de cancelación: el mutante falla y la implementación pasa. También
fallan los mutantes del signo del zoom, de −150 y de usar D nueva, y el que avanza la cámara al
consultar antes del primer paso. La comparación con x87 nativo del balanceo del mapa 5 encontró
un ULP de diferencia con relojes largos: Unicorn coincide con MuRet; queda documentado como
límite del oráculo en 06 y P-C29 a, pendiente de captura del original. Los vectores incluyen
esos relojes. La revisión de Opus 5.5 con esfuerzo máximo no encontró errores de comportamiento; pidió un
oráculo independiente para el giro inverso y que el golden de cada celda observe su cámara. Se
agregaron 403 salidas reales de `0x5fcc30` y una comparación de giros/traslación con la traza; los
mutantes de suma y yaw −44 fallan. Se corrigieron los comentarios que exageraban su cobertura.
La supuesta falta de variación del nivel y del objetivo era un falso positivo: ambos estaban ya
en los 3.000 frames, que el paquete entregado a Opus no incluía. Astra contrastó esas conclusiones
y revisó los arreglos. El chequeo directo de ASM y pseudocódigo de `0x5fcc30` confirma índices,
agrupación y únicos guardados float; copiar literalmente el pseudocódigo reordenaría las sumas. El contraste de
`0x635ae0` cotejó 462 instrucciones, constantes y tablas con el PE: sin discrepancias lógicas en
el alcance incluido; la tabla de correspondencia queda en 06 § La cámara del juego. La CL7
completa sigue abierta: el cierre exige el barrido de todas sus funcionalidades en pasadas
complementarias, según el pedido de Emanuel registrado en `AGENTS.md`.

La auditoría adicional solicitada para el PR 138, con Opus 5.5 xhigh y Astra 6 Ultra, encontró
que los tres casos de cancelación no detectaban las dos asociaciones alternativas de cada
componente. Se conservan los 403 fixtures históricos y se agregan tres casos analíticos:
las seis alternativas mutantes fallan. También se quitó el bucle inverso que repetía la fórmula
de producción y se endureció la lectura de bits del fixture, sin restringir las matrices 3 × 4.
El contraste de los 130 bytes de `0x44dc60` descubrió que prueba C3 sin C2: se corrigió la
selección del objetivo ante NaN con excepciones enmascaradas y se cubrieron Z aislada y −0.
Las constantes nombradas conservan los tipos, bits, agrupación y límites de redondeo. La fuente
de Unicorn 2.1.4 confirma que su FSIN/FCOS usa double y `sin`/`cos` de C; esos fixtures siguen
siendo regresiones históricas, sin nueva ejecución de emulación ni afirmación de equivalencia
x87 nativa universal. La evidencia y los límites quedan en 06 § La cámara del juego.

Las revisiones finales de Opus 5.5 xhigh y Astra 6 Ultra no dejaron P1/P2 abiertos. El control
positivo del detector de salida descubrió que fortify podía eludir `--wrap=printf`; el harness
ahora lo comprueba y cubre los errores previos a la suspensión. El informe admite una cadencia
indeterminada sin dejar el Markdown incompleto. Son 44 tests de herramientas, 26 numéricos
focales y la suite completa de 2.255 casos registrados, cero fallos y 30 omisiones opcionales.
Los tres lectores migrados a C++20 terminaron con código 0 en XP: cámara coincidente, serie
de 2.250 muestras completa y un único incremento de suspensión retirado. La recuperación
conserva la propiedad del handle cuando CloseHandle falla; descartamos la propuesta de
abandonarlo, porque un fallo no acredita que se haya cerrado. La CL7 completa sigue abierta.

## Sub-ola CL7.3 B: la cámara compartida

**Contrato:** 06 § La cámara del juego (las escenas 2 y 4 también pasan por `0x635ae0`, el zoom que queda de
una escena a otra, la altura del `0x40`), P-C27 b y Q-C150, y la decisión 5. PR propio. Se detalla al
arrancar; lo que se sabe:

- **Qué:** el `World::GameCamera` vive en el contexto (`SceneContext::camera`, como el `ClothWorld`), lo
  arma `main.cpp` una vez con los valores del arranque (la D de 1.000 del inicializador estático `0x635ad0`,
  el zoom en 0), y nadie lo vuelve a armar. La escena 5 escribe el yaw −45 en su primer paso (`0x636838`).
  El login (`0x6350b0`) y la selección (`0x634680`) sacan su cámara de `StepGameCamera` con las escenas 2 y
  4, en vez de `LoginCamera()` y `SelectionCamera()`. La altura del `0x40` (1.200, y 1.201 desde el primer
  `0x40` de la cámara) sale de los `constexpr` de `TerrainHeight.h` y `TerrainCells.h` y pasa al estado
  compartido.
- **Tests:** con el zoom en 0, las cámaras del login y de la selección dan los bits de hoy: los vectores de
  las escenas 2 y 4 de `camera.txt`, y los golden del login y de la selección siguen en verde (hasta la CL7.P,
  que suma los 8 `rand()` protegidos de cada frame de `0x635ae0` y cambia el `rand()` del login). Con el zoom de
  210 que dejó el juego, la del login empieza en (12.636,6; 12.819,4) y vuelve de a 10 por frame (P-C27 b,
  emulado). `TerrainHeightAt` con una celda `0x40` da 1.200 antes del primer `0x40` de la cámara y 1.201
  después. Review Focus 1: del juego al login y de vuelta.

**Estado de la implementación: completa (PR141).** Cerrada la construcción inicial que mantenía B parcial.
La cámara y sus entradas viven una vez en main; el carry es local a cada entrada según ADR0005.
`SelectionState` conserva cinco historiales y CPU entre escenas. Login fija yaw0 y escribe HeroXYZ
al dibujar; su avatar visible es slot2, distinto de Hero0. El primer paso de juego sigue fijando
−45, sin adelantar L1. La altura fija compartida llega a terreno, brasas, nacimiento del cuervo y sombra.

- [x] Constructor contrastado contra ASM/PE: Height(50,50), atributo nuevo, word318 anterior,
  offset30/90 antes del único guardado float y reset posterior. Sin sumar0 en la otra rama.
- [x] F3/00 y altas F3/01 aceptadas publican eventos propios y ordenados, también idénticos.
  Main los aplica tras Poll y antes de Step/Request/Start. Bajas y limpiezas conservan historial;
  login sólo reemplaza el de slot2. Una recepción anterior al cierre conserva sus efectos.
- [x] Primera lista construye con55; el siguiente Step habilitado carga54, también con lista vacía.
  CPU se transfiere al salir del login; GPU se libera una sola vez. Faltar tablas es un error explícito.
- [x] Cámara antes de Z163 en los cinco slots; una recreación posterior reemplaza esa Z sin
  esperar otro dibujo. El primer LoginStep consume la Z nueva para sus brasas.
- [x] Visuales se reconstruyen por revisión, sin espejo XY repetido en cada Step; Render refleja
  altas/bajas recibidas sin Step. Un slot repetido conserva todas las construcciones en el evento
  y sólo su última entrada en la vista UI; dibujo por índice de slot.
- [x] Tests de unidad, transferencia CPU y escenas observan esos efectos, incluyendo SceneManager
  real, Poll minimizado, destrucción a login, recepciones idénticas, lista vacía y accepted+close.
  Las regresiones previas de cámara, terreno, cuervo y brasas se conservan.
- [x] Contraste estático cruzado Astra6Ultra de constructor, fases y ownership/historial documentado
  en 06 § Los personajes de la lista. Sin nueva ejecución ni emulación del original. Los fixtures
  históricos permanecen identificados como tales y no acreditan equivalencia x87 universal.
- [x] Revisión final cruzada por Astra6Ultra y Opus5.5xhigh, sin P1/P2 pendientes tras las
  correcciones. Se acreditó Enter→pedido→Poll síncrono y el rechazo de F3/00 anterior al pedido;
  quedaron cubiertos alta idéntica/pose, baja sin Step, reinicio y Enter fallido. La condición
  final de Opus sobre el test de snapshot único se cumplió conservando el evento crudo.
- [x] Suite completa: **2.347 tests, 83GL, cero fallos y cero omisiones**; build C++20 sin PCH,
  warnings como errores, formato18 e include-cleaner limpios. El mutante que vuelve a SameEntry
  hace fallar la pose de una creación idéntica; restaurado el código, pasa la suite.

**Compuerta de integración:** [checks Linux/Windows del HEAD del PR141](https://github.com/ema-ljn/MuRet/pull/141/checks).
Su estado se consulta en GitHub; el PR se publica listo para revisión sólo tras comprobar ambos
jobs en verde. Esta compuerta sigue siendo obligatoria, sin copiar un estado de CI que queda
obsoleto con el siguiente commit. En el primer envío, MSVC detectó en un test una conversión
`int`→`optional<uint8_t>`; el slot se tipó explícitamente, conservando el valor y la cobertura.

El contador/RNG protegido de CL7.P, la celda/entrada al mundo de L1, terreno y efectos completos
propios de selección y las otras sub-olas siguen su propio alcance; no son partes diferidas de
la integración de cámara de B.

## Sub-ola CL7.P: las variables protegidas

**Contrato:** 06 § La tabla de protección, 07 Q-C125 y Q-C183, P-C22 a y la decisión 3. PR propio. Empieza con un lector del binario, como el
barrido, y se detalla con lo que lea. Lo que se sabe:

- **Qué falta leer:**
  - las funciones de la tabla `0x57630c8`: `0x40ad90`, `0x40b240`, `0x40b2f0`, `0x40b440`, `0x40b4d0`,
    `0x40b570`, `0x40b800`, `0x412810` y `0x412890`, y el hash de `0x633f96..0x63417a`;
  - qué variables guarda: el contador de frames `0x7d3a0f0`, el contador de warp `0x77f3b04`, la celda
    lógica de cada personaje (`+0x41c` y `+0x420`), el registro propio `0x7681eb4` (`0xf08` bytes), y las que
    salgan;
  - cuántos `rand()` gasta cada clase de acceso: 4 al cerrar una variable chica (`0x412890`), 3.848 al
    cerrar el registro propio (`0x40b4d0`, uno por byte), y lo que den abrir, leer, escribir y rearmar;
  - **cada acceso que corre** en el paso y en el dibujo de las escenas 2, 4 y 5, **lo porte MuRet o no**:
    como mínimo `0x57cfc0` (las habilidades, al final de casi cada paso de `MoveHero`: 3.848 por paso, y
    más por los otros caminos de `MoveHero` y los cierres en línea),
    `0x508f00` (8 por personaje vivo y por paso), `0x635ae0` (8 por frame dibujado), `0x4c3940` (tres sitios
    de cierre del registro al entrar: subtotal de 11.544 sólo si cada uno completa el último cierre,
    con identidad y preservación de sus accesos acreditadas; no un total universal), el `F3 10` y el `D7`
    al héroe. Donde MuRet no tiene la función, deja el
    gasto en el mismo lugar del paso o del dibujo, con un nombre que diga de qué función es.
- **Qué se escribe:** `GameLogic::ProtectedValue<T>`. Guarda el valor en claro y, en cada acceso que el
  original cierra, saca del `CrtRandom` que le pasan los `rand()` de ese acceso. Entra primero en el contador
  de frames del login, donde completa Q-C125 con los 4 del +1 después de la escena y los del rearmado:
  `0x6378f9` compara el resto firmado del snapshot previo con **57**, al dividir por **313**;
  no se dispara en los múltiplos de 313. Si el orden de P-C22 a sale de lo mismo, también se arregla. Después lo usan la celda del héroe
  y el contador de warp de la L1.
- **Tests:**
  - los vectores del paso real del login con la tabla viva (sobre `game/protection/protection_rand.py` y
    `camera_rand.py`, que ya cuentan los 3.848 y los 4 de cada cierre): el estado del `rand()` después de cada uno de
    1.000 pasos, igual en MuRet; la cantidad de rearmados depende del valor lógico inicial y de otras
    escrituras. Si empieza en 0, sólo se incrementa una vez por paso y no hay otras escrituras,
    hay cuatro, en los snapshots
    57, 370, 683 y 996. Esa condición no demuestra que el inicio nativo sea 0;
  - un test por clase de acceso. Según la autoridad vigente, no se generan nuevos fixtures emulados:
    el contrato se contrasta con ASM y evidencia nativa existente, conservando la procedencia de las
    regresiones históricas.

**Contraste estático del contador:** `0x637700` toma el snapshot antes del cierre de `0x6378e6` y
después decide la fase 57. El incremento ocurre tras el paso de la escena, sobre el valor reabierto,
no necesariamente sobre aquel snapshot. Cada refresco completo tiene cuatro llamadas RNG directas;
los dos cierres y las llamadas transitivas requieren sus propios contratos. La fase y esos sitios
directos están verificados; no cierran el presupuesto total de RNG ni desbloquean por sí solos L1.
El incremento envuelve en 32 bits y el predicado interpreta esos bits con signo: C++20 debe conservar
ambas propiedades sin introducir overflow con signo indefinido.

## Sub-ola CL7.L1: la entrada al mundo y el pase del juego

**Contrato:** 06 § La escena del juego (de la selección al primer frame, la respuesta `0x4c3940`, el pase de
`0x6371a0`). PR propio. Se detalla al arrancar; lo que se sabe:

**Estado incorporado:** PR149 aporta el lector y PR162 la recepción ordenada junto con F3/10.
La cola todavía no tiene consumidor productivo. La aplicación de este apartado sigue pendiente;
el contexto y la vigencia de cada recepción deben resolverse antes de conectar la escena.

- **El protocolo:** `Protocol::Character::ReadCharacterSelectResult` en `CharacterSelect.h`, el C3 rearmado
  de `0x34` bytes con `TooShort`: la celda (`[4]`, `[5]`), el mapa (`[6]`), la dirección (`[7]`) y los stats
  de 04 § La entrada al mundo; lo que cae fuera del paquete (`[0x34..0x37]`) va en 0 con un aviso
  (Q-C144). La recepción incorporada no cambia estado ni deadline; la transición a InWorld depende
  de aplicar la respuesta. Las velocidades del keepalive no salen de ella (`0x4c3940` no escribe
  `+0x4c` ni `+0x58`, P-C28 i) y quedan las de hoy.
- **La sesión:** `SessionState::InWorld` (el `0x3d`) y `ServerSession::JoinedWorld()`.
- **Las variables protegidas:** la celda del servidor del héroe y el contador `0x77f3b04` son valores
  lógicos de CL7.P, sin portar las transformaciones de protección. `0x6356f0` escribe 9.999.999
  sólo si el byte `0x7d3a13e` era cero, y lo pone en 1 antes de cargar las texturas. Si ya era no cero,
  no reinicia el contador ni recrea esos recursos. La respuesta escribe 30 después de publicar `0x3d`;
  MoveHero decrementa los valores positivos en la ruta que alcanza ese bloque,
  también durante la espera normal. No se presupone el valor exacto antes del reply ni un número fijo
  de pasos. Los accesos conservan nesting y gasto de RNG del último cierre bajo sus condiciones.
- **El cursor:** la `GameScene` dibuja el cursor del juego al final, como el login (la decisión 4).
- **La escena:** `GameScene` retiene el frame mientras el snapshot firmado del contador sea mayor
  que 30; su lectura se cierra antes de comparar y antes de llamar a la cámara. En la espera normal
  queda el mosaico de LSBg. El store inicial no es una constante retenida hasta la respuesta. Con ella carga el
  mundo del mapa (`LoadedWorld` con `WorldFolder`), arma al héroe como `0x511290` (`(celda + 0,5) · 100`, la
  altura de `TerrainHeightAt`, el ángulo `(dir − 1) · 45` y la zona segura del `.att & 1`). La altura
  también depende del campo previo del actor `+0x318`: si era `0x1c15`, la zona segura vale cero y el
  mapa observado no es −1, agrega 90 en mapas 8/10 o 30 en los restantes. El slot procede del RNG;
  la limpieza no acredita un actor vacío. El equipo se aplica antes de publicar el héroe y antes de
  vaciar las fuentes, como `0x4c3940` → `0x511290` → `0x512a30`. Pone el yaw en
  −45 (el primer paso, `0x636838`) y dibuja el pase del juego: la franja (0, 0, 640, 48) para el rango, con
  `TerrainRangeOf(vista, cámara, 640 · 0,0015625)`; el trapecio `TerrainPolygonOf(Game, mapa, objetivo,
  yaw, 640)`; y el mundo (0, 0, 640, 432) con el terreno. La cámara, de CL7.3 A, con `OriginalFrameCarry`.
- **Tests:** el vector del cable (el C3 `F3 03` descifrado con `game/wire/decode.py`, sin nombres: la celda (133, 127), el mapa 0 y la
  dirección 0 dan (13.350; 12.750; altura) y −45°); `ServerSessionTest`: `EnteringWorld` pasa a `InWorld`;
  `GameSceneTest.HoldsTheFrameUntilTheWorldArrives` y `LoadsTheMapOfTheReply`; el golden
  `GoldenTerrainTrace.TheGameSceneDrawsTheCellsOfLorencia`: la `GameScene` con el héroe de la traza en
  (143, 120) dibuja las 329 celdas de la traza en su orden; Review Focus 1 (entrar dos veces).

## Sub-ola CL7.L2: los modelos y los objetos de Lorencia

**Contrato:** 06 § Lo propio de Lorencia (la creación, el paso, `0x602ed0` y `0x6002e0` del mapa 0) y 05 (la
tabla con nombre de `Object1`). PR propio. Se detalla al arrancar; lo que se sabe:

**Entrega acotada de modelos:** `LorenciaModelFiles` conserva las 112 solicitudes de los 39 grupos
de `0x619a21..0x619ea1`, con su orden, numeración y nombres literales. `WorldModels` carga y asigna
Bird01 antes de Fish01 desde el slot `0x69`, reinicia el cursor en `0x41c` antes de los modelos con
nombre y asigna sus texturas por ID ascendente. Con los slots de `GameModels` anteriores reutiliza
las tres texturas ya cargadas y obtiene las 99 nuevas de `0x41c..0x47e`; el golden compara los 101
nombres con `lor_slots.bin`. La carga aislada, sin esos slots anteriores, tiene otra asignación y
se prueba por separado. La tabla se contrasta con el ASM; no se generan expectativas emuladas nuevas.
Siguen pendientes las texturas comunes (`drop`, `wt`, `light`), la creación, el paso y el dibujo de los
objetos, sus efectos y el pase completo de la escena. Esta entrega no cierra L2 ni CL7.5 A.

- **Los modelos:** `WorldModels` con una tabla por mapa: la del 0 (los 112 con nombre de `0x619a10..0x619edf`, tipo ←
  nombre, como dato), los especiales desde `0x69` (`0xad` `Bird01`, `0xb2` `Fish01`), `Object8\wt00..31` en
  `0x41..0x60`, `light01` en `0x68` (que después pisa `rain03`, Q-C136), `drop01` en `0x607`, y las texturas de los 160 de
  `Object1\`. El lazo genérico `Object<k+1>.bmd` sigue para los otros mapas.
- **Los objetos:** los casos del mapa 0 en `CreateWorldObjects` (cajas, velocidades, blendMesh, el
  `PoseBox01` oculto, la lista de poses), `StepWorldObject` (los techos `0x7d`/`0x7e` con el tile del héroe,
  los fuegos de `0x52f360` con sus tres `rand()` de salto y el de la luz, las lámparas y las velas, el
  titileo de `0x75`/`0x7a`, la V que corre de `0x76`/`0x77`) y el dibujo (`0x69` con cuatro `RenderMesh`).
  Los fuegos y las luces sacan sus `rand()` acá, en el orden del original, y dejan sus pedidos (luz al
  terreno, partícula) en una lista que usan L4 y L5.
- **El pase:** el orden de `0x6371a0`: terreno → objetos → efectos del suelo → pájaros → personajes → ítems
  del piso (`0x608a00`) → peces → caballo → hojas → joints (`0x53c6a0`) → efectos con modelo → cintas
  (`0x52e590`) → sprites → `0x549440` → partículas → números de daño (`0x548eb0`). Los que todavía no tienen
  contenido (los ítems, los joints, las cintas y los números) quedan en su lugar, vacíos, para sus olas.
- **Tests:** el golden de los slots contra el volcado (`lor_slots.bin`, que lee `game/world0/slots.py`: `0x41c..0x47e` son las 99 texturas
  de `Object1\` en su orden, y `bird.tga` y `fish.tga` en `0x69` y `0x6a`); los vectores del código real
  (`make_world0_vectors.py`, sobre `game/world0/run_objs0.py` y `lor_objs.py`, como `make_world55_vectors.py`): los 2.870 registros al crearse, en tres pasos y
  en el dibujo, con el estado del `rand()` y los pedidos en orden; la traza: los 120 bloques de objetos de
  Lorencia contra el lote (los vértices con tolerancia, el slot, la mezcla y el orden), sin los personajes ni
  los pájaros; `TheGamePassGoesInTheOrderOf0x6371a0`; Review Focus 2 (un modelo que falta).

## Sub-ola CL7.L3: el héroe parado

**Contrato:** 06 § La escena del juego (el héroe: `0x511290`, el `F3 10`, `0x4f2250`, la aparición, los dos
`0x61f`). PR propio. Se detalla al arrancar; lo que se sabe:

**Estado incorporado:** PR159 aporta el lector completo y su golden de siete capturas;
PR162 conserva la recepción F3/10 en orden con F3/03. La semántica de los siete bytes opacos,
la aplicación al equipo y los efectos del héroe siguen pendientes.

- **El equipo:** `Protocol::Character::ReadInventoryList` del `F3 10` (C4, entradas de 8 bytes: el lugar y
  7 del ítem), los 12 lugares del equipo a la apariencia, como `0x5bc740` → `0x512a30`. La ventana del
  inventario es de la CL8.4.
- **La acción de parado** (`0x4f2250`, `SetPlayerStop`, P-C14 c): la escribe esta sub-ola entera, con
  `Run = 0`, y la CL7.4 C la usa; por el arma, la zona segura, la mascota, las alas y el
  Fenrir; las ramas que mandan un `C1 .. 18` lo mandan.
- **La aparición:** el alpha de 0 a 1 de a 0,05 por paso; los dos efectos de llegada `0x61f` (20 pasos, el
  calco `0x61e` en el suelo de `0x52dc20` con los dos giros de `0x53d2c0`); el cuervo (`0x420d60`) con el
  Dark Lord con `0x1c17`; la luz de la escena 5 (la del terreno en su lugar, sin el 0,3 del login); el
  `rand() % 400` del lugar en la tabla, que gasta un `rand()`.
- **Tests:** el `F3 10` del cable (cinco ítems, uno en la mano derecha); `0x4f2250` emulado sobre una matriz
  de arma, zona segura y mascota; el `0x61f` emulado en sus 20 pasos; la luz del héroe en (143,5; 120,5)
  contra la traza; el héroe opaco en el paso 20.

## Sub-ola CL7.L4: la luz dinámica y el pasto

**Contrato:** 06 § Lo propio de Lorencia (`0x5fbf60`, `0x5f9780`, `0x5f99e0`, el pase del pasto de
`0x5fa830`). PR propio. Se detalla al arrancar; lo que se sabe:

- **La luz:** el buffer dinámico (`0x7b4ccc4`), que `0x5fbf60` rearma en cada paso desde la luz sombreada en
  la ventana del rango; `0x5f9780` (el cuadrado ±r, la caída `(r − d) / r` y el piso en 0); `0x5f99e0`, que
  lo leen los objetos y el héroe; las fuentes de L2 (el fuego con r 4, las lámparas y las velas con r 3).
- **El pasto:** el segundo `0x5fc190` en el modo 2: un quad por celda con las cuatro esquinas en alfa 0, de
  alto el doble de la textura, la punta corrida −50 en x y la ola en y, la u con el corrimiento de la fila,
  los 65.536 `(rand() % 4) · 0,25` de `0x5f8300` al cargar el mapa (que gastan sus `rand()` también en el
  login).
- **Tests:** vectores emulados de `0x5f9780` y `0x52f360` con semilla; la traza (los vértices más brillantes
  que el OZJ, con `game/world0/brighter.py`): los colores del terreno con
  el `FireLight01` #1933 (los vértices de su cuadrado suben; con tolerancia, porque el salto y la luz salen
  del `rand()`), y los 4 de la fila 136 anotados como pendientes; los 25 quads del pasto con la geometría
  exacta y la u en uno de {0; 0,25; 0,5; 0,75}; la cuenta de los 65.536 `rand()` contra el emulado.

## Sub-ola CL7.L5: lo vivo de Lorencia

**Contrato:** 06 § Lo propio de Lorencia (las hojas, los pájaros, los peces, las partículas de los objetos,
la fuente, el `0F`). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** las 80 hojas del mapa 0 (`0x52fa60`, `0x5301a0` y `0x5303a0`, que no corren con el héroe en el
  tile 4) y el `0F` como entrada de la lluvia (en Lorencia solo el sonido); los pájaros (`0x446120` y
  `0x446b60`, con sus sombras de alfa 0,2; el tope, 5 o 6, se cierra con el emulado) y los peces (`0x447030`
  y `0x446f00`, hasta 3, en las celdas de agua); las partículas `0x5d9` (el fuego, subtipos 0 a 3) y `0x5f2`
  con el subtipo 2; la fuente (`0x602ed0` del `0x69`); los dos `0x5ac` del `MerchantAnimal01`.
- **Tests:** vectores emulados por paso con semilla (como `make_leaf_vectors.py` y
  `make_particle_vectors.py`); la traza por cuenta, textura, mezcla y orden (los 80 quads `0x64`, los 5
  pájaros con sus 5 sombras y las partículas aditivas después de las hojas); Review Focus 3 (a 144 FPS, lo
  mismo por segundo).

---

## Sub-ola CL7.4 A: el mapa de paredes vivo y el camino

**Contrato:** 06 § Caminar (el `TerrainWall` vivo de `0x508f00`, `PathFinding2` de `0x4eac00` y el A* de
`0x4ead70`). PR propio. Se detalla al arrancar; lo que se sabe:

**Entrega pura de caminar:** `TerrainWalls::Rebuild` recibe celdas ya decodificadas: limpia el bit
`0x02` y marca únicamente actores vivos, con `+0x389 == 0` y tipo distinto de 8. `PathFinder::Find`
reproduce la rama de distancia cero, su árbol, prioridad, vecinos, padres, topes y corte a 15. El
parámetro explícito `clearGoal` conserva el borrado de todo el WORD y su efecto sobre el segundo
intento; el consumidor decidirá cuándo corresponde el caso de asedio. La asignación del mapa vivo
en cada paso, los cierres de coordenadas y su RNG, el clic, el envío y el avance del personaje quedan
para CL7.P y CL7.4 C–E. No se afirma portar la rama de búsqueda por radio.

La verificación de esta entrega usa ASM cotejado con el PE y casos literales independientes, incluidos
los límites 500/501 y 50/51 y el borrado de la zona segura antes del segundo intento. No se encontraron
fixtures históricos de caminos disponibles y no se ejecutaron los generadores emulados que proponía
el plan anterior. Por tanto, las comparaciones de miles de vectores y de los 171 caminos del cable
que figuran abajo siguen pendientes; los tests propios no se presentan como esas comparaciones.

- **Qué:** `World::TerrainWalls` (el WORD vivo por celda: se borra el `0x02` de las 65.536 y se marca la
  celda lógica de cada personaje vivo, no moribundo y de tipo distinto de 8, en cada paso;
  la celda con máscara de `0x5f82a0`);
  `GameLogic::PathFinder` con la lista abierta como un árbol propio que inserta y saca como `0x4eb770` y
  `0x4c0dd0` (los iguales a la derecha, se saca el de más a la izquierda), los ocho vecinos en el orden de
  `0x6d5d80`, la prioridad h + el costo del padre, la h (que da 4 en la diagonal a la meta, lo mismo que en
  la de al lado: Q-C159), los topes de 500 y 50,
  y la salida con el inicio en x o y igual a 0; `PathFinding2` con sus dos intentos (en la zona segura el
  segundo pasa por encima de los personajes) y el corte en 15.
- **Tests:** `PathFinderTest.MatchesTheEmulatedCode` (`make_path_vectors.py`, sobre `game/walk/emu_path.py`,
  `path_limits.py`, `path_edges.py` y `heuristic.py`: miles de casos sobre el `.att`
  de Lorencia y sobre mapas sintéticos con `0x01`, `0x02`, `0x04`, `0x20` y `0x22`, y los bordes);
  `TheStartOnRowOrColumnZeroFails`, `TheExactSearchGivesUpAfter500AndTheNearestAfter50`,
  `OnlyTheFirstFifteenCellsAreKept`, `TheDiagonalNextToTheGoalCostsLikeTheSide` (h(1, 1) = h(1, 0) = 4),
  `AnOccupiedGoalEndsOnTheDiagonalByTheTieOrder`,
  `InTheSafeZoneTheSecondTryWalksThroughCharacters`, `TheWallsLoseTheCharacterBitEveryStep` (la celda
  (221, 11) de Lorencia); `TheWireWalksMatch`: los 171 caminos del cable (solo celdas y direcciones; `game/walk/wire_vs_path.py`), con los
  2 distintos anotados (una celda ocupada que el cable no muestra).

## Sub-ola CL7.4 B: el paquete de caminar y lo que llega

**Contrato:** 04 (`D3` C->S y S->C, `D7` S->C es el daño) y 06 § Caminar (`SendMove` de `0x56acd0`). PR
propio, apilado sobre CL7.3 A. Implementación pura, sin integrar clic, movimiento ni sesión.

- **Qué:** `Protocol::Movement`: `WriteWalk(path, target, angle)` (`C1 len D3 X Y (dir << 4 | n − 1)` y los
  pasos de a dos nibbles, con XOR32 y sin C3; 15 celdas salen como 14; la dirección final por el blanco o, con
  una celda, por el ángulo), `WalkUpdateTimeMax(pathNum)` (0, 5 o 3 · n + 4), `ReadWalk` del `D3` S->C y
  `ReadDamage` del `D7` S->C (el id sin el bit 15, el bit, el daño y el tipo).
- [x] `Movement.h/.cpp` y 23 casos de `MovementTest`: cantidad local 1..15, wire limitado a 14,
  todas las direcciones y nibbles, blanco respecto de la última celda transmitida, fallback al último
  paso transmitido, restas sin wrap, tiempo sobre la cantidad local, rama exclusiva del ángulo,
  lectores BE con IDs sintéticos asimétricos y bits independientes, truncados y bytes extra.
- [x] Contraste estático de `0x56acd0` y constantes del PE `c32bbd5f`; se verificó además el helper
  `0x69fd3c`: `0x69fd4b` fuerza truncamiento y `0x69fd55` usa `FISTP m64int`, con retorno EDX:EAX.
  En el lector `0x4c6790`, `0x4c6798..0x4c67a9` mantiene el ID BE completo y `0x4c67dc` conserva
  cuatro bits de dirección. El ASM prevalece sobre el pseudocódigo.
- [x] Cable documentado: (b6,73) … (b3,74) da `C1 08 D3 EC 23 E2 0B 08`, y con FoxAnti
  `C1 08 A3 9C 53 92 7B 78`. El blanco lejano del test es sintético y explícito: el cable no lo lleva.
  Por el pedido nuevo de Emanuel, no se ejecutó el generador emulado antes propuesto.
- [x] Revisiones cruzadas de Astra 6 Ultra y Opus 5.5 xhigh, y revisión de los arreglos. Se reforzaron
  los casos de lectores y de celda/paso transmitidos; nueve mutantes fallan por sus aserciones.
  La receta double es un contrato numérico declarado: una muestra nativa observó CW `0x027f`
  fuera de `SendMove`. Falta observarla dentro de su aritmética para afirmar equivalencia nativa
  incondicional (P-C30). Los casos estáticos no se presentan como ejecuciones nativas.
- [x] Build sin PCH y con warnings como errores, formato e includes; 2.271 casos registrados en la
  suite completa con GL/Mesa, trazas y cable: cero fallos y 30 omisiones opcionales.
- [x] Windows/MSVC, Linux y formato del [CI de PR139](https://github.com/ema-ljn/MuRet/actions/runs/42158143752)
  en verde, comprobados después del merge.

## Sub-ola CL7.4 C: el paso entre celdas

**Contrato:** 06 § Caminar (`MovePath` de `0x4ea270`, `MoveCharacterPosition` de `0x508a80`, la velocidad
de `0x508940`, `SetPlayerWalk` de `0x4f37c0`; `SetPlayerStop` viene de la L3). PR propio. Se detalla al
arrancar; lo que se sabe:

- **Qué:** en `GameLogic::CharacterMovement`: actualizar la celda lógica al empezar cada tramo;
  cuando q3 avanza el índice sin llegar, la celda coincide con ese índice al retornar y se adelanta
  en la siguiente llamada. No imponer «una adelante» en todos los retornos. Conservar los pesos
  Catmull-Rom, umbral20, máximo de un subpunto por llamada y llegada que guarda XY sin ejecutar giro.
  El giro y su umbral45 mantienen los spills y muestras del original; P-C32 impide cerrar todavía
  los tres helpers. Position avanza hacia la orientación y aplica altura/correcciones por mapa/equipo.
- **Velocidad ya implementada:** `GameLogic::UpdateMovementSpeed` (PR155) cubre toda `0x508940`,
  incluidas las velocidades12/15/16/17/19, las mitades por máscara0x2, Run unsigned8 y los globales
  compartidos D0/D4. No lee clase/botas/guantes ni resetea Run por segura: esa política pertenece al
  prefijo de `SetPlayerWalk`. Con ítem1c37, ese prefijo habilitado incrementa D4, no Run.
- **Acciones y efectos pendientes:** `SetPlayerWalk` reescribe22 velocidades compartidas de acciones
  y sus mitades; no incluye0x15. Preservar prioridades, helpers y efectos posteriores aunque una rama
  omita cambiar la acción. Los pasos prueban restos firmados por64/16 del RNG compartido; no sustituir
  su orden ni su cantidad condicionada por una probabilidad o un gasto fijo agregado al final.
- **Tests:** usar expectativas analíticas contrastadas con ASM y evidencia nativa existente, sin
  nuevas emulaciones. Los vectores históricos conservan su procedencia. Cubrir rectas/codos/zigzag,
  longitudes2..15, clases/equipo/mapas, ambas fronteras del subpunto, llegada, estado lógico y los
  writes compartidos; observar retorno y estado, no sólo posición final. PR155 ya cubre los anchos,
  prioridades y efectos completos de velocidad. Giro requiere P-C32; el orden exterior de todos los
  personajes y los efectos/RNG transitivos siguen abiertos.

## Sub-ola CL7.4 D: el picking del terreno

**Contrato:** 06 § Caminar (`0x5fc260(1)` → `0x5fc190` → `0x5fc0e0` → `0x5fac90` con `Flag`, `0x622440` y
`0x622170`), más el consumidor acotado `0x59ddf3..0x59deb3`. PR propio.

- **Qué:** `World::TerrainPicking` puro con estado explícito: clear SelectFlag → reset maxT (conserva
  hit/XY), blockY→blockX→row→col, frustum en ambos niveles, hidden pickeable y 0x40 por esquina.
  Primera cara012; sólo al fallar,023. Gana menor t entre intentos reales; igualdad reemplaza.
  Bordes estrictos, backface, bounds inclusivos y prioridad de proyección x/y por ±0,5 del ASM.
  Retry interno sólo tras hit; **otro retry externo siempre**, cuyo AL habilita TargetX/Y DWORD y
  DontMove. Productos por0,01f retenidos antes de trunc signed64 y máscara0xff. Miss/error conserva
  target. No integra aún escena, cadencia/UI, Shift ni caminar (CL7.4E).
- **Tests:** `TerrainPickingTest` usa literales/geometría analítica para triángulos, fronteras,
  nearest, hidden/fixedHeight, frustum/override, rangos, persistencia, igualdad del retry,
  target/truncamiento/cursor y errores. `GoldenTerrainPicking` usa Data de Lorencia, Devias,
  World19/56 y el rayo de `GameCamera` de Lorencia con selección retenida. Son pruebas del componente,
  sin muestra nativa de picking; la secuencia interna sin efecto final distinguible se revisa en
  fuente. `MatchesTheEmulatedCode`/generador eran propuesta histórica: no se ejecutaron ni crearon
  nuevos vectores emulados. Contrato PC53/nearest declarado; CW nativa, startup y CL7.4E pendientes.

## Sub-ola CL7.4 E: caminar en la escena

**Contrato:** 06 § Caminar (`MoveHero` de `0x59adf0`, el orden del paso de `0x636290`, los otros con su
camino) y 06 § La cámara del juego (el recorte de los personajes en el paso con el trapecio del dibujo
anterior; los cinco planos y la esfera de los efectos). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** en la `GameScene`: el orden del paso (la UI, `MoveHero` con las paredes de antes, después las
  paredes de este paso y cada personaje); la rama del terreno de `MoveHero` con `MouseUpdateTime/Max`, un
  clic pendiente propio que no se borra con `ConsumeEdges` (el `0x7d366d4` del original), apretado, Shift,
  la barra de abajo, la condición de la acción, "a menos de 2 celdas", la misma celda y la hora; el picking
  con el rayo del último dibujo (la escena guarda la cámara, el viewport y el mouse del último `Render`); el
  `D3` del héroe (la dirección, y la celda lógica si está quieto) y el de los otros (su camino, el reintento,
  la llegada con `(dir − 1) · 45`); el `D7` que para a un jugador con `0x4f53a0` (siempre con el bit 15, y
  sin él la mitad de las veces, con `rand() & 1`, si no es el héroe; Q-C178); los cinco planos de
  `0x5fd000` con la esfera de `0x5fbf00`; el héroe y la cámara en el estado del paso (la decisión 1).
- **Tests:** con un GS falso: un clic en el piso manda los bytes de B; apretado cerca del héroe, un `A3` por
  celda y no por paso; un clic dentro de la espera se atiende al vencer (*inferido*, P-C30); con Shift o con
  el mouse en la barra, no camina; un `D3` de otro lo hace caminar; el eco con el héroe quieto le corre la
  celda lógica y no la posición; el `D7` con el bit 15 lo para, y sin el bit para a otro jugador cuando el
  `rand() & 1` da 0, gastando ese `rand()`; la repetición de una conexión del cable sin
  los paquetes con nombres (los caminos y las celdas); Review Focus 3 (un frame de 2 s).

---

## Sub-ola CL7.5 A: cada mundo del `Data\` carga

**Contrato:** 05 (los modelos especiales de cada mapa) y 06 § Los cambios de mapa (lo que carga
`0x61a560`). PR propio. Se detalla al arrancar; lo que se sabe:

**Estado incorporado:** PR161 conserva dos observaciones independientes del mapa y conecta la
identidad efectiva dentro de LoadedWorld: terreno, sombreado, tiles y vista usan el mismo ID.
La proyección local estable normaliza 32 a 9 y conserva los 256 resultados de WorldFolderNumber.
El getter LoadedMap aún no tiene consumidor productivo; siguen pendientes la integración del mapa
recibido, las fases de modelos, las variantes 30/34 y los errores del loader completo.

- **Qué:** la tabla de los especiales de `0x616ec0` por mapa como dato (1 y 4, 2, 3, 6, 7, 8, 10, 11 a 17,
  18 a 23, Kalima, 30, 31, 33, 34); las texturas fijas de `0x61a560` (las hojas y las lluvias de `0x64` a
  `0x68`); los mapas 30 y 34 con su `.att` y su luz según el estado del mundo, como "no va" donde el estado es
  de la CL9; el mapa 5 sin alturas da el aviso que nombra `World6\TerrainHeight` y cierra, como el original
  (Q-C180).
- **Tests:** golden: para cada mapa de `OriginalMaps` menos el 35, el terreno, el `.obj` y los modelos
  cargan, y los tipos del `.obj` sin modelo son los del research (`tests/vectors/worlds/missing-models.txt`);
  el mapa 5 da el aviso de las alturas y pide cerrar; el registro −515 de `World7` no rompe nada; Review
  Focus 2.

## Sub-ola CL7.5 B: `Gate.bmd` y la detección

**Contrato:** 05 § `Gate.bmd` y 06 § Los cambios de mapa (`0x599ef0`). PR propio.

**Estado:** implementadas la carga y la decisión pura. Esto no cierra toda CL7.5 B ni CL7: falta integrar
el caller, el tiempo de la sesión y los efectos de CL7.5 D.

- **Datos:** `Data::Tables::GateRecord` copia los nueve campos del registro descifrado sobre la
  `GateTable` existente. `GameData` exige `Data/Gate.bmd` y carga sólo su prefijo de 2.304 bytes, ignorando
  cualquier cola. Los errores de ruta, lectura y archivo corto siguen Q-C12/Q-C24; no afirman equivalencia
  con los errores nativos.
- **Decisión:** `GameLogic::GateCheck` recibe todo el estado del paso: celda, mapa, nivel, clase, equipo
  actual, montura alternativa activa, contador, guardas, pending y ms transcurridos. Devuelve el índice
  pedido opcional (incluido 0), los avisos acumulados en orden, la espera y `clearWarpPending`. Conserva el
  flag exactamente 1, el rectángulo inclusivo sin margen ni normalización, los umbrales signed/unsigned,
  los 2/3 de las clases base 3/4 y las precedencias de gate 28, Atlans e Icarus. Puede emitir varios avisos
  y como máximo un pedido por paso. La preview sólo interviene en Atlans.
- **Tests:** registros sintéticos y fuera de tabla; cuatro gates reales de Lorencia (1, 18, 23 y 102),
  esquinas, lados externos y nivel justo/bajo, más el rectángulo invertido del 103; límites de clases,
  ítems, mapas, tiempo y contador; orden de avisos y pending en gates solapados. La carga cubre ausencia,
  ruta case-insensitive, archivo corto, directorio y cola grande; el lector de prefijos cubre sus límites
  de representación y errores antes de reservar. Los fixtures de las escenas incluyen la tabla requerida.
- **Pendiente de integración:** el caller debe poblar todo el estado, decrementar el contador positivo
  una sola vez antes de la consulta y conservar el umbral 30/31. Los ms son una entrada pura: no equivalen
  a las lecturas lazy de `GetTickCount` por candidato del original. Si hay limpieza y pedido, aplica
  primero `clearWarpPending`; gate 0 activa el bloqueo antes del envío. Después del envío vienen el
  contador 9.999.999, los hooks de destino 30/31 (`0x61c210` y luego `0x61c170("Data\\Macro.txt")`),
  pending en 1 y los resets de selección, en ese orden. Sesión, transporte, presentación de mensajes,
  retención de frame, respuestas y efectos siguen en C/D; estado protegido y su RNG, en el carril P.

## Sub-ola CL7.5 C: el protocolo del cambio de mapa

**Contrato:** 04 (`1C`, `F3 04`, `F3 12`, `BD`). El componente puro tiene PR propio;
la espera de warp de `ServerSession` se integra después de L1, cuando exista `InWorld`.

- **Qué:** `Protocol::World`: `ReadTeleport` (`1C`: gate, mapa, x, y, dirección), `ReadRegen` (`F3 04`),
  `WriteGateRequest` (`C1 06 1C gate 00 00` en C3), `WriteTeleportDone` (`C1 04 F3 12`, C1 con XOR32) y
  `WriteCrywolfState` (`C1 04 BD 00`); la `ServerSession` con la espera del warp.
- **Tests:** los cuatro `1C` del PCAP del PR #45 de `mu-online-reverse-lab` (con `game/wire/pcap2log.py` y
  `decode.py`) y sus cuatro `F3 12` (`C1 04 F3 68` en el cable), byte a
  byte; los dos `F3 04` del `muproxy.log`; el `1C` C->S contra el encoder de C3 con serial fijo (sin captura:
  *estático*, P-C31).

- [x] `Protocol::World` puro: cinco operaciones, sin carga de mapas, sockets ni estado de escena.
  `Regen` usa campos LE y acepta relleno no cero; ambos lectores devuelven dirección cruda,
  distinguen WrongPacket/TooShort y siguen la convención de aceptar campos sobrantes.
- [x] Contrastes estáticos independientes de `0x4c4b80` y `0x4c6e10`, y construcción del pedido
  en `0x599ef0`, contra los bytes del PE `c32bbd5f`. No se generaron expectativas emuladas.
- [x] Dieciséis tests focales: cuatro teleports nativos documentados, campos asimétricos,
  truncamientos, tipos/subcódigos, extremos y escritores con XOR32 y Delivery exactos.
- [x] Dos `F3 04` históricos, con sus rellenos no cero, y cadena del pedido de gate atravesando
  el encoder C3 con serial 0x42 y clave identidad sintética. El C3 esperado se derivó aparte;
  no es una captura nativa de gate ni una prueba de las claves del juego. Los tests de Network
  acreditan su cifrador; este caso fija la conexión del escritor puro con esa cadena.
- [x] Astra 6 Ultra y Opus 5.5 xhigh revisaron el componente y el refuerzo, sin P1/P2 abiertos.
  Ambos mutantes de campos/Delivery fallan con tests nuevos y anteriores; no se presentan
  como huecos que los catorce tests iniciales no detectaban.
- [x] Suite completa: 2.264 casos registrados con GL/Mesa, trazas y cable, cero fallos y 30
  omisiones opcionales. Build con warnings como errores y sin PCH, formato e includes limpios.
- [x] Windows/MSVC, Linux y formato del [CI de PR140](https://github.com/ema-ljn/MuRet/actions/runs/42166735524)
  en verde, comprobados después del merge.
- [ ] Integrar la espera de warp de `ServerSession` después de L1; CL7.5 C todavía no está completa.

## Sub-ola CL7.5 D: el cambio de mapa en la escena

**Contrato:** 06 § Los cambios de mapa (el `1C` en `0x4c6e10`, el `F3 04` en `0x4c4b80`, lo que se borra y lo
que queda, la pantalla congelada). PR propio. Se detalla al arrancar; lo que se sabe:

- **Qué:** el pedido: en cada paso, `MoveHero` llama a `0x599ef0` (`0x59b841`) con la `GateCheck` de la
  5.B; al pisar un gate manda el `1C` en C3, pone el contador en 9.999.999 y retiene el frame hasta el `1C`
  que vuelve; con el nivel bajo, el aviso 350 y el contador en 50 (20 pasos sin dibujo ni movimiento). La
  vuelta: la `GameScene` hace el cambio en el paso (la decisión 2), borra lo que se va (los otros, los ítems
  del piso y los elegidos siempre; los objetos, los efectos y las texturas solo con otro mapa), carga el
  mundo nuevo, pone al héroe con la z de las alturas nuevas y el ángulo `(dir − 1) · 45`, manda el `F3 12`
  después de cargar, pone el contador en 30, crea el `0x61f` y el cuervo otra vez. El `F3 04` hace lo mismo
  con el héroe y el mundo, pero no manda el `F3 12` ni toca el contador: no pone 30 ni retiene el frame.
- **Tests:** pisar el gate 23 de Lorencia (213..217 × 246..247) manda `C3 [C1 06 1C 17 00 00]` y retiene el
  frame hasta el `1C`; con el nivel bajo, el aviso y 20 pasos quietos; con un GS falso que manda la
  secuencia del cable (0 → 0 → 3 → 7 → 0): la carpeta cargada en
  cada paso; un `F3 12` por cada `1C` con gate y ninguno con el gate en 0 ni con el `F3 04`; el mismo mapa no
  recarga pero borra a los otros; los efectos se vacían solo con otro mapa; ningún frame presentado mientras
  espera; Review Focus 1.

## Sub-ola CL7.5 E: los casos de los otros mapas, como datos

**Contrato:** 06 § Los cambios de mapa (los ganchos por mapa) y los quirks del 07. PR propio. Se detalla al
arrancar; lo que se sabe:

- **Qué:** la tabla por mapa: el color de fondo del frame (`0x637700`: Icarus, Kalima y negro), el far (el 30
  y el rectángulo, de CL7.3 A), la niebla del 30, Icarus sin terreno y con nubes, la sombra y el agua del 7,
  la nieve y los techos de Devias, la arena del 8 y el temblor del 5. Incluir el terreno de agua propio
  de Kalima (`0x4377a0`, `0x41cc70`), sin sustituirlo por tiles genéricos, y los ganchos de mapas de
  evento que condicionan el gameplay espacial o su presentación. Inventariar sus contratos contra
  ASM antes de implementarlos. Las reglas y paquetes específicos de cada evento siguen en CL9.
- **Tests:** la tabla contra valores contrastados estáticamente con el ASM/PE (`0x637700`, `0x6371a0`
  y `0x635ae0`), sin nuevas emulaciones. Por cada mapa nominal, incluidos los que comparten carpeta:
  entrada, movimiento recto/codo/borde y zona segura, picking y altura, cámara/recorte/objetos,
  mismo/distinto mapa, warp/reaparición y regreso al anterior, con estado retenido/limpiado correcto.
  Los casos particulares se derivan del contrato de cada familia; un smoke de carga/frame es sólo
  una comprobación inicial. Verificar los pases con GL cuando el resultado dependa del dibujo.
  El mapa 32 carga World10 y pasa al estado9; 5/35 conservan los errores documentados por recursos
  faltantes. No presentarlos como mapas jugables probados con el Data actual.

---

## Sub-ola CL7.6: los efectos del mundo y de los personajes

**Contrato:** el roadmap, § Lo que dejó la CL6 (CL7.6). Se detalla al arrancar, después de la CL7.5: la luz
de abajo del elegido en la selección (`0x53fe70`, la `chasellight` del slot `0x729`), los efectos de las
acciones del caballo (el polvo de los cascos con el jinete en la `0x4b`) y los de las armas, salvo el cetro
del login. Uno o más PRs, según lo que salga al leerlos.

## Cierre: el barrido profundo de CL7 en todos los mapas

**Contrato:** la escena 5 entera contra el binario, después de la CL7.5 D/E, en cada mapa y familia.
PR propio, como el del login: revisores nuevos por partes (la entrada y el bucle, el paso, el dibujo
3D, el caminar y los mapas) con ASM/PE y evidencia nativa existente, sin nuevas emulaciones; lo que
se arregla en el PR (con un test que falle antes), lo que queda en 08 y la lista en 06
de lo que la escena 5 de MuRet todavía no hace.

Después: la CL8 (el HUD y las ventanas), con el ancho del mundo de `0x5bb8d0` y el área de mensajes.
