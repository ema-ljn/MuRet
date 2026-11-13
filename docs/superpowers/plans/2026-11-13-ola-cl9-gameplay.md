# Plan de la ola CL9: el gameplay

> **Para quien ejecute:** SUB-SKILL REQUERIDA: superpowers:executing-plans (en línea) o
> superpowers:subagent-driven-development, hito por hito. Los pasos llevan checkbox (`- [ ]`).
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), § Plan por olas (CL9),
> § Lo que dejó la CL6 y § Mapeo completo del decompilado a las olas. Escrito el 2026-11-13, después del
> barrido de la CL9 sobre la matriz de la CL.M (`mainexe-roadmap.tsv`, #334). Emanuel aprobó escribir el plan
> (10:28Z) y **lo revisa antes de que arranque la implementación**. Cada hito se detalla al arrancar, con las
> interfaces que dejen los anteriores; lo que se aprenda en uno se escribe acá antes del siguiente.
>
> **Quién escribe:** Claude o Codex. El modo tutor está apagado (2026-11-10).

**Objetivo:** el "terminada cuando" de la CL9 en el roadmap, **una sesión de juego estable, también en los
mapas de evento**: los otros jugadores, los monstruos y los NPC en el viewport, el emote, pegar y morir, las
skills, los ítems del piso, las tiendas, el almacén y la caja del caos, la party, el comercio, el duelo, las
tiendas personales, el gremio, los amigos y el correo, los eventos (Blood Castle, Devil Square, Chaos Castle,
Kalima, Crywolf) y Castle Siege. En todos los mapas.

**Arquitectura:** la CL9 no trae piezas nuevas de fondo; llena las que dejaron la CL4, la CL7 y la CL8:

- **Los paquetes** que llegan entran por el `Dispatcher` (CL4.2) con un handler por opcode, como hoy hace
  `App::ServerSession`; los que se mandan, por un escritor puro en `Protocol/` (como `WriteFacing`). Hoy
  MuRet no registra ningún handler de la CL9 (solo manda algunos pedidos de la entrada al mundo: `C1 42`,
  `BD 00`, `B9 02`, `A9`): llegan como `Unknown` y van una vez al log.
- **El viewport** son los 400 registros de `0x438` B de `0x743a16c` (`WorldState::Actors()`), que la CL7 ya
  modela para el héroe y que el `D3` y el `D7` de otros ya buscan por la llave (`FindActor`,
  `0x5110d0`), sin encontrar a nadie porque nadie los crea.
- **Las ventanas** son las de la CL8: los paneles por bits (`UI::PanelRegistry`), los diálogos de `0x623e70`,
  el administrador viejo (amigos y correo) y los paneles con objeto retenido (`0x657450`: el gremio, Castle
  Siege). Cada hito registra los ganchos de sus ids y reemplaza los cortes de la CL8 por la lógica.
- **Las funciones que mezclan olas** (`0x5650c0` 23 KB, `0x56c440` 42 KB, `0x576eb0` 24 KB, `0x623e70`,
  `0x5d8d50`, `0x5ca250`, `0x59ff10`, `0x50ba30`, `0x4fd5f0`) ya son **despacho** desde la CL7 y la CL8: cada
  hito llena sus casos en el esqueleto que hay. Ningún hito lo reescribe.

Lo de GL va a `Render/`, la UI a `UI/`, los paquetes a `Protocol/` y la lógica sin GL a `GameLogic/` o
`Scenes/`.

**Stack:** C++20, OpenGL 3.3 core, FreeType, SDL3, libjpeg-turbo 3.2.0, GoogleTest. Ninguna biblioteca nueva.

**Spec:** el roadmap y el research: 04 (el protocolo y el dispatcher `0x4e2b10`), 06 (§ HUD y ventanas, § Los
diálogos del juego, § El paso del juego), 08 (P-C09, P-C12, P-C22 e y f, P-C58, P-C59, P-C63, P-C87, P-C90,
P-C95, P-C116, P-C117), 09 (los datos) y 10 (la matriz). El reparto de este plan está en dos tablas:
[`2026-11-13-ola-cl9-funciones.tsv`](2026-11-13-ola-cl9-funciones.tsv) (1.141 filas: las 1.014 funciones de
la CL9 y 127 de otras olas sin portar que la CL9 necesita, cada una con su sub-ola, su hito, cómo se asignó,
el opcode que atiende y los paquetes que manda) y
[`2026-11-13-ola-cl9-cortes.tsv`](2026-11-13-ola-cl9-cortes.tsv) (los 240 cortes de MuRet hacia la CL9, en
119 funciones de otras olas, con su hito). Las dos son la entrada de cada hito: el hito N porta las filas con
`hito = N` y reemplaza los cortes con `hito = N`.

## Restricciones de todo el plan

- Las de `AGENTS.md` y `docs/CODING_RULES.md`: `clang-format-18`, `misc-include-cleaner` limpio, el chequeo de
  conversiones de MSVC (`client-msvc-ci-gotchas`: C4242 en plantillas de la STL, iteradores de arreglos, 1 MB
  de pila), `MURET_WARNINGS_AS_ERRORS=ON` y la suite entera con GL y los golden. El cliente no compila en el
  contenedor de la nube: el CI de GitHub es el que manda.
- **Un hito, un PR** contra `main` (Emanuel, 2026-11-11: PRs grandes, por hito). Título `CL9 hito N: …`,
  secciones "Qué hace", "Research" y "Pruebas". Squash; para traer `main` a una rama ya empujada, merge commit
  (el force push está bloqueado). Revisión adversarial contra el ASM antes de abrir cada PR.
- **Cada hito empieza por su CL9.R** (P-C09): el handler del cliente contra la función del GameServer
  reconstruido que le corresponde, cuando existe, y lo que no cierra va al 04 o al 08 antes de portar.
- **Fidelidad al ASM primero**, con las etiquetas del README: *estático*, *inferido*, *verificado*, *emulado*.
  **No se hacen emulaciones nuevas** (traspaso de la CL7): los `rand()` se cuentan leyendo el ASM; lo que la
  lectura no cierra va al 08 con su comprobación en la VM, al final de la ola.
- **El `rand()` cuenta,** como en la CL7 y la CL8: cada `rand()` de un handler, una ventana o un paso se saca
  en su orden, con los bytes protegidos modelados como sus `rand()` (`LogicalAccess`). Los ganchos de la CL8
  que este plan reemplaza sacan hoy lo del estado base; al portar la rama, la cuenta tiene que seguir igual en
  ese estado y sumar la de la rama.
- **Cortes por ola** (Emanuel, 2026-11-12): lo que abre la CL10 (sonidos, `0x41ed10`) o la CL11 (efectos con
  modelo, joints, cintas, partículas) se corta con su dirección y queda en el 08 con su ola. Lo que solo
  resuelve la VM va al final.
- **Todos los mapas.** Los handlers de la CL9 tienen casos por mapa (Blood Castle 11..17, Chaos Castle 18..23,
  Devil Square 9 y 32, Kalima 24..29 y 36, Castle Siege 30, Crywolf 34, Atlans, Tarkan, Icarus): cada hito
  los porta o los corta con su dirección, nunca solo Lorencia. Los tests van en Lorencia (0) y Devias (2), y en
  el mapa del caso cuando lo hay.
- **El oráculo es el cliente** para lo que el GameServer 1.02 reconstruido no atiende (es 0.97 sin Castle
  Siege: `3F`, `9F`, `A9`, `AA`..`AF`, `B1`..`BD`, `C0`..`CB`, `E1`..`EB`, el `BC` y el `BD`; plan de la CL8,
  § Lo que encontró el barrido): los bytes salen del ASM.
- **Privacidad:** ningún test, log ni vector lleva texto de chat real, nombres de cuentas, de personajes ni de
  gremios. Los tests usan nombres y textos inventados; del cable se usan solo cabeceras, largos y cuerpos sin
  nombres.

## Lo que encontró el barrido

**El alcance** (*estático*, sobre la matriz de la CL.M). La CL9 es dueña de **1.014 funciones y 469,3 KB**:

| Sub-ola | Funciones | KB | Qué es |
| --- | ---: | ---: | --- |
| CL9.1 | 40 | 23,9 | los otros jugadores, los monstruos, los NPC, el emote, las misiones |
| CL9.2 | 54 | 64,7 | pegar, el daño, la muerte, las skills, el cuervo, las mascotas |
| CL9.3 | 76 | 51,5 | los ítems del piso, usar y tirar, las tiendas de NPC, el almacén, la caja del caos |
| CL9.4 | 599 | 220,0 | la party, el comercio, el duelo, las tiendas personales, el gremio, los amigos, el correo |
| CL9.5 | 245 | 109,1 | los eventos y Castle Siege |

Además: **127 funciones de otras olas (47,9 KB) que la matriz deja sin portar y solo llaman funciones de la
CL9** (103 de la CL8, casi todas controles de las ventanas viejas; 17 de la CL7, entre ellas `0x5141e0`, la
alta de un monstruo por tipo, 11,6 KB), y **las ramas de la CL9 adentro de funciones de otras olas**: los 240
cortes que MuRet ya tiene (191 en funciones de la CL8, 41 de la CL7) más el tramo de los 22 casos del emote
(`0x576eb0`, unos 22 KB, que el proxy de la CL.M no ve porque `0x576960` ya está portada). Todo junto da unos
**620 KB**; la CL.M decía 559 porque no contaba las 127 ni el tramo del emote.

28 de las 1.014 (2,4 KB) son código muerto (nadie las llama ni las apunta): quedan en el hito de la función con
la que comparten datos y no se portan; se anotan.

**Cómo se repartió cada función** (columna `como` de la tabla): por el opcode que atiende (101, el caso del
dispatcher `0x4e2b10`), por el paquete que manda (39, el patrón `MOV …,0xc1` seguido del opcode en el ASM),
a mano (100, leídas), por el texto de la matriz (26), y el resto por el grafo de llamadas: quien la llama (560)
o lo que llama (61), y la sub-ola de la matriz cuando el grafo no decide (127). Esas 748 son **inferidas**:
cada hito revisa sus filas al arrancar y mueve las que no sean suyas (con una línea en este plan).

**Nadie entra al viewport** (*estático*). MuRet no registra `0x12` (otros jugadores), `0x13` (monstruos y
NPC), `0x14` (bajas), `0x1F`, `0x45` ni `0x68`, así que en un servidor real el héroe está solo: el `D3` y el
`D7` de otros ya están portados pero no encuentran a nadie. Eso hace del viewport el portón de toda la ola:
los hitos 1 y 2 van primero. La matriz pone `0x4c8860` (el `0x12`) y `0x4e0980` (el `0x68`) en la CL7, pero
ninguno está registrado; los porta este plan.

**El emote** (*estático*, `0x576eb0` y `0x576a60`; P-C87). Es el corte que más se ve hoy: lo dispara casi
cualquier chat. Lo que hace el original:

- `0x576eb0` busca en lo dicho (`0x576960`, ya portada por la CL8) **95 textos**: los 80 de `Text.bmd`
  (270..348 y 783) **y 15 literales del binario** (`0x6c90e0..0x6c912c`): `;`, `T_T`, `-.-`, `-_-`, `^^`,
  `^.^`, `^_^`, `hustle` y siete en CP949 (`ㅠ.ㅠ`, `ㅜ.ㅜ`, `ㅡ.ㅡ`, `ㅡ.,ㅡ`, `ㅡ,.ㅡ`, `/ㅡ`, `ㅡ^`). **La CL8
  solo mira los 80 de `Text.bmd`**: hoy, un chat con `;` o `^^` se manda sin emote en MuRet y con emote en
  el original. El hito 1 lo corrige.
- El orden de búsqueda decide el caso: los textos van en el orden de la tabla de abajo, y el 291, el 317 y el
  348 se prueban al final, cada uno solo.
- Cada caso llama `0x576a60(héroe, héroe, animación, acción)` (el héroe es `[0x743a170]`) y después **arma y manda su propio
  `C1 05 18 [dir] [acción]`**, sin mirar qué hizo `0x576a60`.
- `0x576a60` solo actúa si la acción actual del héroe (`+0x111`) está entre 1 y 12 o es `0x8a`; si no, vuelve
  sin nada. Si actúa: con un elfo (`+0x1d4 & 7 == 2`) suma 1 a la animación salvo que esté entre `0xb7` y
  `0xc0`, la pone (`0x4ea030`) y manda otro `C1 05 18 [dir] [acción]`. **Con el héroe parado (las acciones 1
  a 12 van antes de las de caminar, `0x0d..0x21`), un emote manda dos `C1 18` iguales**; caminando, pegando
  o en otra acción, uno solo y sin animarse.
- `dir = ftol((ángulo + 22,5) × (1/45) + 1) & 0xff`, módulo 8 con signo (`0x6b87dc` = 22,5, `0x6b87d8` =
  0,0222…, `0x6b87d4` = 1,0), el ángulo de `+0x24` del héroe. Es la misma cuenta que el giro de `MoveHero`
  (`WriteFacing`, `C1 05 18 [dir] 7A`).
- El GameServer manda la acción a los del viewport, **no al que la hizo** (`CGActionRecv`, `0x425cd0` del GS):
  el emote propio se ve por `0x576a60`, el de los demás por el `0x18` que llega (`0x4cb750`), que traduce la
  acción del servidor a la animación (`0x83` → `0x99`, o `0x9a` con un elfo; `0x8b` → `0xa9`/`0xaa`…).
- Ninguna de las tres funciones (`0x576eb0`, `0x576a60`, `0x4cb750`) llama a `rand()`, y un `C1` no saca
  `rand()` (solo los `C3`, plan de la CL8).

| Caso | Textos | Animación | Acción |
| --- | --- | --- | --- |
| `0x57ca85` | 270..277 | `0x99` | `0x83` |
| `0x57c686` | 278..280 | `0x9b` | `0x84` |
| `0x57c2a2` | 281..286 | `0x9d` | `0x85` |
| `0x57bea3` | 287..290 | `0xa3` | `0x86` |
| `0x57babf` | 292..295 | `0xa1` | `0x87` |
| `0x57b6c0` | 296..302 | `0xa5` | `0x88` |
| `0x57b2dc` | `;`, 303..305 | `0xa9` | `0x8b` |
| `0x57aef9` | `ㅠ.ㅠ`, `ㅜ.ㅜ`, `T_T`, 306..309 | `0xa7` | `0x89` |
| `0x57ab15` | `ㅡ.ㅡ`, `ㅡ.,ㅡ`, `ㅡ,.ㅡ`, `-.-`, `-_-`, 310, 311 | `0xab` | `0x8c` |
| `0x57a731` | `^^`, `^.^`, `^_^`, 312..316 | `0xaf` | `0x8e` |
| `0x57a333` | 318..321 | `0x9f` | `0x8a` |
| `0x579f35` | 322..325 | `0xad` | `0x8d` |
| `0x579b36` | 326..329 | `0xb1` | `0x8f` |
| `0x579737` | 330..334 | `0xb3` | `0x90` |
| `0x579353` | 335..338 | `0xb5` | `0x91` |
| `0x578f54` | 339..341 | `0xb7` | `0x92` |
| `0x578b70` | 342, 343, `/ㅡ`, `ㅡ^` | `0xb8` | `0x93` |
| `0x578771` | 344..347 | `0xc0` | `0x94` |
| `0x57838d` | 783, `hustle` | `0xbc` | `0x98` |
| `0x577796` | 291 (solo, después de los demás) | `0xbd` | `0x99` |
| `0x577b92` | 317 (solo) | `0xbf` | `0x9b` |
| `0x577f8e` | 348 (solo) | `0xbe` | `0x9a` |

`0x576a60` también lo llaman los handlers de la guerra de gremios (`0x4d8260`, el `0x62`, y `0x4d8870`, el
`0x63`, con animación `0xa7` y acción `0x89`): es del hito 8. Y mandan `C1 18` otras cinco funciones: sentarse
(`0x4f2250`, tramo `0x4f2416..0x4f2bea`, hoy cortado), los comandos (`0x56c440`, `0x406050`), `MoveHero`
(`0x59adf0`, ya portado) y dos de la CL7 sin leer (`0x4295f0`, con Fenrir `0x1c37` y la acción `0x89`, y
`0x4e1010`, del quest).

**La matriz se equivoca en once opcodes** (la tabla `OPCODE_WAVE` del generador): `0x20` (ítems del piso) es
CL9.3, no CL9.1; `0x5D` (bajas del gremio en el viewport) y `0x3F` (las tiendas personales) y `0x51` (la
respuesta del gremio) son CL9.4, no CL9.1 ni CL9.5; `0xCA` (el chat de amigos) es CL9.4, no CL9.1; `0x9C` (la
experiencia de Blood Castle, el mismo struct que el `0x16`) es CL9.2, no CL9.1; `0xA3` (el resultado del quest)
y `0x9F` (el objeto del quest `[0x710148]`) son CL9.1, no CL9.2 ni CL9.5; `0x0B` (el estado de los eventos) es
CL9.5, no CL9.2; `0x2C` (la fruta de stats) es CL9.3, no CL9.2; `0xA9` (la mascota y el cuervo, `0x723960`)
es CL9.2, no CL9.3; y `0xA0`..`0xA2` (las misiones) van a la CL9.1, que la matriz dejaba sin sub-ola. La
tabla de funciones de este plan ya usa las sub-olas corregidas.

**Los datos que faltan** (09): `Monster50` (el tipo 58 cierra el original y en MuRet corta con su
dirección), los sonidos (CL10). `Quest.bmd`, `Dialog.bmd` y los `Monster*.bmd` están en `Client/assets/Data`.

## Decisiones

1. **Trece hitos, un PR cada uno**, agrupados por lo que se juega, no por sub-ola: Emanuel encontró demasiados
   los 120 a 250 PRs del cálculo de abajo arriba (2026-11-10). Las sub-olas quedan como etiquetas de las filas.
2. **El viewport primero:** el hito 1 crea a los otros jugadores y el 2 a los monstruos y los NPC. Todo lo demás
   (pegar, comerciar, la party, los eventos) necesita a alguien del otro lado.
3. **El emote abre la ola** (Emanuel, 2026-11-13): es el corte que más se ve y es chico, y su `0x18` de vuelta
   necesita a los otros jugadores, así que va en el mismo hito que el viewport de jugadores.
4. **Las misiones van con los NPC** (hito 2): son chicas (12 KB), salen de hablar con un NPC y el corte de la
   CL8 (`0x41b5a0`) ya las ata al NPC.
5. **Las 127 funciones ajenas** (CL7, CL8 y otras con `implementation_state = unknown` que solo llama la CL9)
   las porta el hito que las necesita, aunque la matriz les dé otra ola; su fila dice cuál. La matriz se
   actualiza cuando se porten, como con cualquier otra.
6. **Amigos y correo son dos hitos** (9 y 10) y pueden ir en cualquier momento: solo necesitan el administrador
   viejo de la CL8. El 9 lleva los controles de las ventanas viejas que la CL8 dejó sin portar (14 KB).
7. **Castle Siege son dos hitos:** las ventanas del castillo (12, las del NPC y los paneles `0x80000` y
   `0x200000`) y la batalla con su HUD (13, el mapa 30). El servidor del laboratorio tiene el
   asedio apagado (`CityWar`) y no tiene Crywolf: el 13 y la parte de Crywolf del 11 se prueban contra el ASM,
   y la VM al final.
8. **Los dos cortes sin hito** (`?` en la tabla de cortes): el panel `0x2000` (`0x5e74f0`, sin identificar y
   que no se alcanza, porque `Open(0x2000)` corta antes) y el área de mensajes (`0x555190`). Se deciden en la
   auditoría de cierre o en el primer hito que los alcance.
9. **La tabla `OPCODE_WAVE` del generador** se corrige con la revisión independiente de la CL.M, no en este PR:
   regenerar la matriz hoy cambia 913 filas, y eso no es parte de un plan.

## Los hitos

Trece hitos, un PR cada uno. Los KB son de binario aproximados: los propios de la CL9, más las ajenas sin
portar y las ramas que el hito llena en funciones de otras olas. El detalle por función está en las tablas.

### Hito 1: el emote y los otros jugadores (CL9.1, ~35 KB)

- [x] **El emote entero** (§ Lo que encontró el barrido): `UI::CheckTheEmotes` pasa a `FindTheEmote`, que
      devuelve el caso con los 95 textos en el orden del original (incluidos los 15 literales, comparados con
      `0x576960` sobre los bytes CP949 tal cual); `0x576a60` con su compuerta (`+0x111` en 1..12 o `0x8a`), el +1
      del elfo y `0x4ea030`; los dos `C1 05 18`, con `Protocol::Movement::WriteAction(dir, acción)` (el
      `WriteFacing` de hoy pasa a ser `WriteAction(dir, 0x7a)`); y la compuerta de la CL8 (montado fuera de la
      zona segura, nada) queda como está. Se va el corte de `ChatCommands.cpp` y P-C87 se cierra.
- [x] **Sentarse** (`0x4f2250`, tramo `0x4f2416..0x4f2bea`): el `C1 18` que hoy corta en `ActorActions.cpp`.
- [x] **Los otros jugadores entran y salen:** el `0x12` (`0x4c8860` con su cola de 10 ranuras de `0x100` B,
      `0x77f3ba0`), el alta (`0x512870`, `0x5139f0` con la apariencia, el nombre en `+0x1d9`, el GM por
      "webzen", las banderas de `+0x78` con `0x4e6380` y `0x42ab40`, el `+0x360` del `0x4c89e1`), el `0x14`
      (`0x4ca780`, con el caso `0x13d`, `0x510ff0` y la limpieza de `0x451430`/`0x451630`), el `0x45`
      (`0x4c92a0`, los transformados, *inferido*), el `0x25` (el equipo de otro, `0x4c8380`) y el `0xDF` (la
      posición, `0x4c6b40` y `0x51fe10`).
- [x] **La acción de otro** (`0x18`, `0x4cb750`): la tabla acción → animación con sus variantes de elfo, los
      casos `0x12` (sonido `0x6f`, CL10), `0x78`/`0x79` (`0x4f4a10`, es del hito 3: corta), `0x7a`/`0x7b`
      (`0x4f2250`) y `0x7e`; el modelo `0x1c16` no se toca.
- [x] **Lo que el héroe ya hace con otros:** el `D3` y el `D7` de otros (hoy portados) los encuentran; el
      picking de personajes de `0x59ec80` (el corte de la CL8 que deja −1) y el elegido (`0x59ee0b`), con sus
      `rand()`; las búsquedas muertas `0x5132d0`, `0x5178f0` y `0x517920` se anotan.
- [x] Lo que cada alta llama de la CL11 (el efecto de llegada `0x518cf0` ya portado en parte, los joints
      `0x530860`) se llama hasta donde está portado y corta con su dirección donde no.

**Pruebas:** los 95 textos contra su caso (un test por fila de la tabla, y el orden: un texto que contiene dos
gana el de arriba; el 291, el 317 y el 348 al final); `;` y `^^` hacen el emote (hoy no); los bytes de los dos
`C1 05 18` con el héroe parado y del único con el héroe caminando; el +1 del elfo y su excepción
(`0xb7..0xc0`); `dir` en los bordes de cada octante y con ángulos negativos; el `0x18` de otro con cada acción
y clase; el `0x12`, el `0x14` y el `0x45` desde vectores armados con los structs del ASM (sin nombres reales);
cero `rand()` en el emote y en el `0x18`. **Desbloquea:** 2, 3 y 7. **Cierra:** P-C87, el corte de
`0x4f2250`.

### Hito 2: los monstruos, los NPC y las misiones (CL9.1, ~40 KB)

- [ ] **El alta de un monstruo o un NPC por tipo** (`0x5141e0`, 11,6 KB, con los `Monster*.bmd` y la pose de
      los NPC de Q-C108): el `0x13` (`0x4c9c60`), el `0x1F` (`0x4ca0b0`, con los tipos `0x98..0x9e` por
      `0x517400`), el `0x68` (`0x4e0980`) y el `0x1A` (`0x4cedd0`). El tipo 58 corta con su dirección
      (`Monster50` no está). Las partes de monstruo de `0x50ba30` y `0x4fd5f0` que la CL7 dejó cortadas, y los
      monstruos que la revisión de la CL.M dejó en la CL7 por `0x50ba30` (`0x4271d0`, `0x441400`, `0x434180`).
- [ ] **La tela sobre la malla** (P-C22 f, vtable `0x6b8d04`: los monstruos `0x59..0x8f`; los tres pantalones
      también la usan), y Q-C95 con el primer modelo que se desvanezca (§ Lo que dejó la CL6).
- [ ] **Lo que se ve sobre ellos:** las barras de vida (`0x42a4d0`, `0x42a2b0`), NewFace (`0x42d890`,
      `0x42da90`, con los monstruos `0x11e` y `0x11f`), los mensajes de monstruo (`0x4fc790`, `0x4fc8c0`,
      `0x4fcd00`) y el picking de NPC y monstruos en `0x59ec80`.
- [ ] **Hablar con un NPC:** el `C1 30` desde la interacción de llegada (`0x5650c0`, el corte de
      `GameMovement.cpp`, solo su rama de NPC), el diálogo `0x8d` (`0x62d91d`, `0x632f70`, la lista
      `0x7d3a0fc`). Lo que abre cada NPC (tienda, almacén, caja del caos) es del hito 6.
- [ ] **Las misiones:** `A0`..`A3` (`0x418e20`, `0x418ee0`, `0x4198b0`, `0x4ddb80`), el `9F`, el objeto
      `[0x710148]`, la ventana del quest (`0x41ca40`, `0x41ba20`, `0x41c600`, `0x41b5a0`, `0x41b9e0`) y sus pasos
      (`0x418fb0..0x41b170`, `0x419bd0` y `0x41a9e0`, que mandan `A2`, y `0x41b170`, que manda `9F`), con
      `Quest.bmd`.

**Pruebas:** el alta de un monstruo de cada familia de `0x5141e0` (un caso por rama de la tabla de tipos), en
Lorencia, Devias y un mapa de evento; el `0x1F` con un tipo `0x98..0x9e`; el corte del tipo 58; los `rand()`
de cada alta; el `C1 30` y el `A2` byte a byte; el `A3` con sus tres resultados (`0xc8`, `0xc9`, `0xca`).
**Desbloquea:** 3, 5, 6 y 11.

### Hito 3: atacar, el daño y la muerte (CL9.2, ~20 KB)

- [ ] **Pegar:** la rama de ataque de `0x5650c0` (el `D7` del cliente en `0x565a79`, que FoxAnti manda como
      `A4`) y de `0x57cfc0`, el freno de 300 ms (`0x576791c`), el cursor de ataque (`0x558bb0`).
- [ ] **El daño y la muerte de otros:** las ramas de otros de `0x4caae0` (el `D7` que llega), el `0x17`
      (`0x4d0a30`, morir, con los casos `0x28` y `0x3b`), el `0x16` (`0x4cf200`, la experiencia), el `0x9C`
      (`0x4cfce0`), `0x4f4a10` (las acciones `0x78`/`0x79` del `0x18`, sin leer) y los números de daño
      (`0x548e40`, `0x548eb0`).

**Pruebas:** el `D7`/`A4` que sale al pegar; la muerte de un monstruo y la de otro jugador; la experiencia del
`0x16` y del `0x9C` contra el struct del GS (`GCKillPlayerExpSend`); los `rand()` del paso con un monstruo en
el viewport. **Desbloquea:** 4 y 11.

### Hito 4: las skills, el cuervo y las mascotas (CL9.2, ~60 KB)

- [ ] **Usar una skill:** las cuatro funciones por clase (`0x559890`, `0x55b6f0`, `0x55cbc0`, `0x55faa0`,
      32 KB, que mandan `C1 19`, `1E`, `D3`, `DF` y `26`), las demoras (`0x5521f0`, `0x552990`, `0x552c50`,
      `0x552f10`) y la barra de skills (`0x5a05e5..0x5a1170`, el tramo de `0x59ff10` que hoy solo saca sus
      3.848 `rand()`; los números `0x59f810`).
- [ ] **Lo que llega:** el `0x19` (`0x4cc530`, 6,5 KB, con las acciones `0x94`, `0x51` y `0x5b` de
      `0x50f828`), el `0x1E` (`0x4ce000`), el `0x1B` (`0x4cbe90`), el `0x07` (los estados de un personaje,
      `GCSend_07` del GS: `0x4cbe00`, con `0x4e6910`), el `0x48`, el `0x69`, el `0x73` (`0x4d5b10`),
      el `B7`, la lista de skills (`F3 11`, `0x4c5070`) y el invocado (`F3 20`, `0x5ab210`).
- [ ] **El cuervo y las mascotas:** las órdenes del `A7` (los paquetes de `0x416a20`, `0x418920` desde
      `0x41835a`, el objetivo de `+0x3b0` y los estados 2, 5 y 6 del cuervo), el `A8`, el `A9` (`0x421db0`,
      `0x4225d0`, `0x422930`, la ventana `0x424160`, el tooltip `0x422990`), `0x420cd0`, y los íconos de estado
      (`0x42a660`).

**Pruebas:** cada skill de cada clase manda sus bytes; el `0x19` y el `0x1E` sobre el héroe y sobre otro; las
demoras; los `rand()` de la barra con skills (hoy se sacan sin ninguna); el cuervo con cada orden.
**Desbloquea:** 11 y 13.

### Hito 5: los ítems del piso, usar y tirar (CL9.3, ~20 KB)

- [ ] **El piso:** el `0x20` (`0x4d0ba0`, la tabla `0x77f46c8` de `0x220` B por ítem, `0x608350`, `0x607fb0`,
      `0x608880`), los nombres con Alt (`0x5ba790`, `0x608a00`), levantar con SPACE y por llegada (`C1 22`, la
      rama de ítem de `0x5650c0` y `0x59ec80`) y lo que falta del `0x22` y el `0x23` (`0x4d0cc0`, `0x4d10d0`).
- [ ] **Usar y tirar** (decisión 5 de la CL8): el `26` (`0x5c4110`, Q/W/E `0x5a2b47..0x5a4c45`), el `23`
      (`0x5df430..0x5dfe34`), las joyas sobre un ítem (`0x5fd4b0`, `0x5fd4f0`, `0x5d82a8`) y la fruta de stats
      (`0x2C`, `0x4de500`).

**Pruebas:** el `0x20` con varios ítems y su dibujo en la traza; levantar y tirar byte a byte; las pociones
de Q/W/E; los `rand()` de la búsqueda del ítem del piso en el picking. **Desbloquea:** —.

### Hito 6: las tiendas de NPC, el almacén, la caja del caos y reparar (CL9.3, ~60 KB)

- [ ] **Lo que abre un NPC:** el `0x30` (`0x4d1e00`, que abre la tienda `0x10000` en `0x4d288e` y, según el
      NPC, las demás ventanas), los diálogos de la CL9.3 de P-C90 (`0x8e`, `0xa1`, `0xa2`, `0xa4`, `0xa6`,
      `0x91`, `0x94`, `0x96`, `0x98`, `0x99`, `0x9a`, `0xa3`, `0xa8`, `0xa9`, `0xaa`, el `0x8f` con
      `0x5e7aa0`) y sus textos (`0x633300`), con los 16 cortes de `0x623e70`.
- [ ] **Las ventanas:** la tienda (`0x5f4720`), el almacén (`0x5f4e30`, `0x5e7bf0`, `0x5e9b10`, el teclado de
      `0x78abb20`), la caja del caos (`0x5febf0`, `0x5fe610`), reparar (`0x44b910`, la R de `0x59ff10`), el
      `BC` (`0x401f30`, `0x402500`) y los precios (`0x5b1200`, `0x5b2d23`).
- [ ] **Los paquetes:** lo que falta de `31`..`34` (`0x4c5f60`, `0x5bc740` y `0x5be150` están en parte), `81`..`83`,
      `86`/`87`.

**Pruebas:** comprar, vender, reparar, guardar y sacar del almacén, una mezcla, byte a byte; el `rand()` del
byte protegido de `0x10000` con la tienda abierta; el ancho del mundo con cada ventana. **Desbloquea:** —.

### Hito 7: la party, el comercio, el duelo, las tiendas personales y los comandos (CL9.4, ~55 KB)

- [ ] **El comercio:** `0x36`..`0x3D` (`0x4d4af0`, `0x4d5570`), la ventana `0x5f3b40` con el byte protegido
      de `0x20`.
- [ ] **Las tiendas personales:** el `0x3F` y su segundo nivel (`0x4db120`, `0x4dae80`, `0x4dafe0`,
      `0x4db700`, `0x4db890`, `0x4dbcb0`), las ventanas `0x5f3030`, `0x5eb020` y `0x623750`, y los carteles
      `[LOJA]` sobre las cabezas (`0x451c20` y su familia).
- [ ] **La party:** `0x40`..`0x44` (`0x4d67e0`), la vida de la party (`0x5a89b0`), la ventana `0x5f0be0`,
      echar a alguien (`0x5e3d03`, `C1 04 43`), la cantidad del grupo (`0x78aba48`, que lee el dibujo en
      `0x59e680`).
- [ ] **El duelo:** `AA`..`AD` (`0x4dac90`), el marcador (`0x5a6cc0`, `0x5f37f0`, `0x78abad9`) y el color de
      la tela con el nombre del rival (P-C22 e, `0x50c929..0x50cc29`, y qué es `+0x3e3`).
- [ ] **Los comandos sobre otro:** la ventana de comandos (`0x406050`, desde `0x406097`; `0x40a650`) y los
      comandos sociales de `0x56c440` (`/trade`, `/party`, `/guild`, `/GuildWar`, `/BattleSoccer`, que hoy cortan
      con su dirección).

**Pruebas:** una vuelta entera de comercio con otro jugador (pedido, ítems, zen, OK) byte a byte; abrir una
tienda personal y ver la de otro; armar una party y echar a alguien; un duelo; cada comando. **Desbloquea:** 8.

### Hito 8: el gremio, la alianza y la guerra de gremios (CL9.4, ~78 KB)

- [x] **Los paquetes del gremio:** `0x50`..`0x56` (`0x4d74b0`, `0x4d6b60`, `0x4d7a20`, `0x4d7b60`,
      `0x4d7c30`), la marca en el viewport (`0x5C`, `0x5D`, `0x50d533`), `0x66` (`0x447b90`), las burbujas
      (`0x556dd0`).
- [ ] **La ventana del gremio** (el panel `0x10`, `0x64f3e0`, con sus tres pestañas `0x64fdf0`, `0x6507d0` y
      `0x651400`; el `0x40000`, `0x654900`), crear el gremio (`0x6552b0`, `0x655d70`, `54`/`55`), las clases de
      las listas (`0x46b800`..`0x46d630`) y el diálogo de los paneles (`0x65feb0`, `0x6600d0`, `0x660410`,
      `0x6605b0`, `0x660830`) que abren los paquetes.
- [ ] **La alianza y los rivales:** `E1`, `E5`, `E6`, `E9` (`0x4d9810`, `0x6565e0`, `0x4d9940`, `0x4d9e50`),
      con las ventanas `0x64e600`, `0x64def0`, `0x650c10`, `0x653c00` y `0x650830` (`EB`).
- [ ] **La guerra de gremios:** `0x60`..`0x64` (`0x4d8150`, `0x4d8260`, `0x4d8870`, con `0x576a60` del hito
      1), el marcador (`0x5a7640`) y el color de la tela en la guerra (P-C22 e, el resto que deje el hito 7).

**Pruebas:** crear un gremio, sumar a alguien, salir; la marca de otro en el viewport; una alianza; una guerra
con su marcador y los dos `C1 18`. **Desbloquea:** 12.

**Fase 1 (2026-11-13):** los 18 paquetes que llegan (`50`..`54`, `56`, `5D`, `60`..`64`, `66`, `E1`, `E5`, `E6`,
`E9`, `EB`) con lo que escriben, sus respuestas (`51`, `52`, `54`, `E6`, `E9`, los `C1 18` de la guerra con
`0x576a60` del hito 1), el globo (`0x556dd0`), las dos ventanas al cerrarse (`0x6522f0`, `0x6570b0`), abrirlas desde
el registro, y el diálogo de los paneles (`0x65feb0`..`0x6605b0` y dónde quedan sus botones). Queda para la fase 2
(08, P-C281): el dibujo y el paso de las ventanas, las listas, el alta (`0x6552b0`, `0x655d70`, `0x4e3450`), las
ventanas de la alianza que mandan, el dibujo del diálogo, el marcador (`0x5a7640`), la marca en el viewport y el
color de la tela.

**Fase 2 (2026-11-13):** el `65` y el `67` (el gremio y la alianza de cada personaje; el `5C` del GS no lo atiende el
1.02), la marca (`0x5f16a0`, cada una en su slot), la marca sobre el personaje (`0x50d533`), el marcador de la guerra
(`0x5a7640`) y el dibujo del diálogo de los paneles (`0x660830`; el campo y el reloj, P-C285). Queda (08, P-C281): el
dibujo y el paso de las ventanas con sus pestañas, las listas con `0x480e00` y `0x4824d0`, el alta, las ventanas de la
alianza que mandan y el color de la tela en la guerra, que espera la parte del duelo del hito 7.

### Hito 9: los amigos (CL9.4, ~89 KB)

- [x] **Los controles de las ventanas viejas** que la CL8 dejó sin portar (57 funciones de la CL8.3, 14 KB: las
      listas, los campos de texto, los botones de `0x45adf0`..`0x4aa7a0`; los que son la STL se reemplazan por la
      STL) y las vecinas de `0x479960` que leen las flechas (`0x47c870`, `0x47f7b0` y las chicas; `0x480e00` y
      `0x4824d0` van con el 8, `0x4852b0` y `0x486910` con el 12). El EDIT, simplificado (08, P-C181).
- [x] **La lista de amigos:** `C0` (`0x4dc010`, `0x49b220`, `0x49c260`), `C1`..`C4` (`0x4dc3d0`, `0x4dc5b0`,
      `0x4dc690`, `0x4dc720`), los casos de la fábrica `0x48ee60` que no son del correo, F5 y F6
      (`0x490538`, `0x490561`) y la ventana de amigos (`0x4905a2`). Sin los dibujos (08, P-C180); lo del hito 10,
      cortado (P-C183); las filas del TSV que faltan, en P-C185.

**Pruebas:** la lista con varios amigos, agregar y borrar uno, el cambio de estado; los `rand()` de abrir la
ventana (los 3.848 del registro propio de la CL8). Si el PR pasa de ~90 KB, se parte en los controles y los
amigos. **Desbloquea:** 10.

### Hito 10: el chat entre amigos y las cartas (CL9.4, ~39 KB)

- [ ] **El chat entre amigos:** `CA` y `CB` (`0x4dce10`, `0x4dd020`, `0x493530`), la ventana de chat
      (`0x496530`, con su "max chat count over"), invitar (`0x49d0b0`) y `0x4944c0`.
- [ ] **Las cartas:** `C5`..`C9` (`0x4dc9e0`, `0x4dcb70`, `0x4dcd90`, `0x49eb60`, `0x49ee20`, `0x49f870`), las
      ventanas de escribir y leer (`0x49a1f0`, `0x4a11b0`) y el retrato (`0x497c10`).

**Pruebas:** abrir un chat con un amigo e invitar a otro; mandar, leer y borrar una carta, byte a byte.

### Hito 11: los eventos (CL9.5, ~26 KB)

- [ ] **El estado de los eventos:** el `0x0B` (`0x4455c0` y su cola), el `0x46` (`0x4dab90`), el `0x47`
      (`0x4d6990`), `0x90`..`0x96` (`0x4da8c0`, la cuenta mm:ss; `0x4dd8a0`, los chips), `0x99`, `0x9A`
      (`0x4da780`), `0x9B`, `0x9D`, `0x9E`, `AF` (`0x4daa90`), `BC` (`0x4da540`), `F3 23` y `F3 40`.
- [ ] **Las ventanas y el HUD de los eventos:** el NPC de evento (`0x5f58f0`), la cuenta y los resultados
      (`0x5a7eb0`, `0x5f6550`), `0x78abb00` (`0x5e5920`, `0x5e6520`), los contadores de Devil Square, Blood
      Castle y Chaos Castle (`0x40bab0`, `0x40c2d0`, `0x405090`), el `C1 03 97` de `SendMove`.
- [ ] **Las reglas por mapa:** el arma de Blood Castle (`0x50de84`), las trampas de Chaos Castle (`0x405597`,
      `0x405d49`), el `AF 01` (`0x4fbee8`), Kalima, y Crywolf (`BD`, `0x42fe80`, `0x42fe50`, `0x430a40`,
      `0x61a6f4`; P-C58).

**Pruebas:** entrar a Blood Castle, Devil Square y Chaos Castle con vectores de cada paquete; las cuentas;
los `rand()` de los contadores; Crywolf solo contra el ASM. **Desbloquea:** —.

### Hito 12: Castle Siege, las ventanas del castillo (CL9.5, ~78 KB)

- [ ] **Los paquetes del castillo:** `B2` y su segundo nivel (`0x4df1a0`, `0x4df080`), `B3`..`B6`, `B8`
      (`0x4dfc60`, `0x4dfce0`, `0x4dfd80`).
- [ ] **Las ventanas:** las del NPC del castillo (`0x6691c0` y las que llama, `0x6623e0`..`0x667a40`, que
      mandan los `B2`), `0x648a30` (`B2`, `B9`), `0x64d350` (`B4`, `B5`), `0x661310` (`B2`, `B3`), los paneles
      `0x80000` (`0x64aca0`) y `0x200000` (`0x64a060`), `0x661090` y `0x6485f0`, y sus listas (`0x46f320`,
      `0x4706f0`).

**Pruebas:** cada `B2` que sale de cada ventana, byte a byte; las listas del `B4`; los `rand()` de abrir cada
panel. **Desbloquea:** 13.

### Hito 13: Castle Siege, la batalla (CL9.5, ~30 KB)

- [ ] **El mapa 30 en guerra:** `B1` (el cambio de servidor del mapa), `B9`, `BA` (`0x4cf1b0`), `BB`
      (`0x4e0f30`), el panel del asedio (`0x40f850`, `0x4125b0`; P-C63), la información del asedio
      (`0x5a6fc0`, `0x5767924`), el HUD (`0x6579e0` y `0x6694c0`), las armas (`0x44ea60`), las puertas
      (`0x44ff00`), las skills de batalla (`0x652c90`, `0x4294b0`, `0x413ca0`) y `0x44dd50`.
- [ ] **Los cortes de la CL7 en el mapa 30** (`0x4294b0`, `0x42b00b`, `0x42b5f8`, `0x412193`, `0x4140d5`).

**Pruebas:** contra el ASM (el servidor del laboratorio tiene el asedio apagado); los `rand()` del HUD en el
mapa 30.

### Orden

1 → 2 → 3 → 4 → 11; el 5 y el 6 después del 2; el 7 después del 1 y el 8 después del 7; el 9 → 10 en
cualquier momento (solo necesitan la CL8); el 12 → 13 al final, después del 4 y del 8. Al final, una
**auditoría de cierre** contra el ASM, como las de la CL7 y la CL8, y la lista de lo que queda para la VM.

## Los cortes hacia otras olas

Cada uno queda en el código con su dirección y en el 08, como en la CL8:

| Ola | Qué | Dónde |
| --- | --- | --- |
| CL10 | Los sonidos de cada handler y ventana | `0x41ed10` (por ejemplo el `0x6f` del `0x18`, el `0x76` del `0x1A`, el `0x66` del `A3`) |
| CL11 | Los efectos con modelo, joints y partículas de las altas, las skills y los eventos | `0x518cf0`, `0x530860`, `0x5208a0`, las raíces de efectos de la matriz |

## Los datos

- **Modelos:** `Monster*.bmd` (falta `Monster50`), los NPC y los objetos de evento, en `Client/assets/Data`.
- **Tablas:** `Quest.bmd` (hito 2), `Dialog.bmd` (hito 2), `Text.bmd` (ya se lee; el emote usa 270..348 y 783).
- **Literales del binario:** los 15 textos del emote (`0x6c90e0..0x6c912c`), en CP949.

## Los oráculos

- **El ASM:** los bytes de cada paquete, los layouts, las tablas por tipo, la cuenta estática de `rand()`.
- **El GameServer reconstruido** (`gameserver/src/reconstructed/win32-vc6`) para lo que atiende: `CGActionRecv`
  (el `18`), `GCDamageSend` (el `15`), `GCKillPlayerExpSend` (el `16`), `GCDiePlayerSend` (el `17`),
  `GCMagicAttackNumberSend` (el `19`), las tiendas (`30`..`34`), el comercio (`36`..`3D`), la party (`40`..`44`),
  el gremio (`50`..`56`, `60`..`64`), el almacén (`81`..`83`), la caja del caos (`86`, `87`), Devil Square y
  Blood Castle (`90`..`9B`), el quest (`A0`..`A3`).
- **El cable** de la CL8 (`wire/20261030T192219Z_8920d1`): solo cabeceras; no tiene emotes ni viewport con otros.
- **Los golden** de los modelos de monstruo, si la CL6 los dejó.

## Lo que queda para la VM (al final de la ola)

Un emote con el cable abierto (los dos `C1 18`, P-C87); el viewport con otro jugador y con monstruos (sus
`rand()` por frame con un contador en `0x69fd70`); una vuelta de comercio, una party, un gremio y una guerra;
Blood Castle, Devil Square y Chaos Castle de punta a punta; Castle Siege con `CityWar` prendido en el
servidor; Crywolf no se puede (el servidor no lo tiene) y queda contra el ASM.

## Las correcciones al research que trae este PR

- **La matriz** (10): los once opcodes de § Lo que encontró el barrido. La tabla de funciones de este plan ya
  los corrige; la tabla del generador, con la revisión independiente de la CL.M (decisión 9).
- **La matriz:** `0x4c8860` (el `0x12`), `0x4e0980` (el `0x68`) y `0x5141e0` están en la CL7 pero no están
  portados ni registrados; `0x576a60` está en la CL8 y es el emote. Los porta este plan (hitos 1 y 2).
- **P-C87** (08): el emote busca 95 textos, no 80 (faltaban los 15 literales); la fila lo dice ahora.
- **Los comentarios de MuRet:** `GameHud.cpp` decía que las tiendas personales (`0x5f3030`) son de la CL9.3 (son
  de la CL9.4) y `GameSceneUi.cpp` nombraba una CL9.6 que no existe (`0x5a7eb0` es de la CL9.5). Corregidos.

## Riesgos y dudas abiertas

- **El reparto por el grafo es inferido** para 748 funciones: cada hito revisa sus filas al arrancar.
- **Las funciones de despacho** (`0x5650c0`, `0x56c440`, `0x623e70`, `0x5d8d50`) reciben casos de varios
  hitos: cada uno toca solo sus casos, y la auditoría de cierre revisa que no quede ninguno.
- **El hito 9** es el más grande por los controles viejos; si crece, se parte (está dicho en el hito).
- **Castle Siege y Crywolf** no se pueden probar contra el servidor del laboratorio tal como está: el asedio
  necesita `CityWar` prendido y Crywolf no existe en el servidor.
- **Los 28 muertos** se anotan y no se portan; si alguno resulta vivo (por un puntero que el grafo no ve), se
  porta en su hito.
- Sin leer a fondo: `0x4295f0` y `0x4e1010` (mandan `C1 18`), `0x406050` (17 KB), las ramas de la CL9 de
  `0x5ca250` y `0x5d8d50`, el panel `0x2000`. Cada hito los lee antes de portar.
