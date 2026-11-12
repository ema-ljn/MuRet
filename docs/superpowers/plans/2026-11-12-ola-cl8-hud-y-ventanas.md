# Plan de la ola CL8: el HUD y las ventanas

> **Para quien ejecute:** SUB-SKILL REQUERIDA: superpowers:executing-plans (en línea) o
> superpowers:subagent-driven-development, hito por hito. Los pasos llevan checkbox (`- [ ]`).
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), § Plan por olas (CL8) y
> § Lo que dejó la CL6. Escrito el 2026-11-12, después del barrido de la CL8 (cinco lectores del ASM: el HUD
> y el 2D, las ventanas y su administrador, el inventario, el chat y el paso de la UI; más el laboratorio
> `mu-online-reverse-lab`). Emanuel autorizó abrir el PR del plan y empezar a implementarlo el mismo día
> (07:42Z), mientras corría la revisión adversarial; lo que encuentre se corrige acá. Cada hito se
> detalla al arrancar, con las interfaces que dejen los anteriores; lo que se aprenda en uno se escribe acá
> antes del siguiente.
>
> **Quién escribe:** Claude o Codex. El modo tutor está apagado (2026-11-10).

**Objetivo:** el "terminada cuando" de la CL8 en el roadmap, **chat e inventario usables**, con todo lo que la
UI del juego hace en el original: el HUD de abajo con sus números, el cursor del juego, las teclas de la UI, el
sistema de paneles y sus `rand()`, el menú de Esc, las ventanas viejas, la ventana /move y la captura de
pantalla. En todos los mapas.

**Arquitectura:** en el juego del original conviven **cuatro mecanismos de ventanas**, no uno (§ Lo que
encontró el barrido). MuRet los porta como cuatro piezas con un único dueño, `UI::GameUi`, que escribe en un
solo lugar el orden del original: el bloque de UI del paso (`0x636ded..0x63705b`, dentro de `0x636290`) y la
pasada 2D del dibujo (`0x637641..0x6376eb`, dentro de `0x6371a0`). Cada llamada de otra ola es un **gancho**
que, mientras su ola no llegue, hace exactamente lo que el original hace en el estado en que esa parte no
dibuja ni manda nada: saca sus `rand()` y no toca nada más. Lo de GL va a `Render/`, la UI a `UI/`, los
paquetes a `Protocol/` y la lógica sin GL a `GameLogic/` o `Scenes/`.

**Stack:** C++20, OpenGL 3.3 core (`SpriteBatch`, `GlModelRenderer`), FreeType (el texto), SDL3 (la entrada de
texto), libjpeg-turbo 3.2.0 (ya está para leer los OZJ; ahora también escribe la captura), GoogleTest.
Ninguna biblioteca nueva.

**Spec:** el roadmap y el research que deja este barrido: 06 § HUD y ventanas (corregido acá), § El paso del
juego, § El dibujo del juego, § Texto; 02 (el WndProc, las teclas, el foco); 04 (los paquetes del chat y del
inventario); las filas nuevas del 07 (Q-C442 a Q-C458) y del 08 (P-C59 a P-C61). El mapa de las funciones del
barrido, con la ola que le dio cada lector, está en
[`2026-11-12-ola-cl8-funciones.tsv`](2026-11-12-ola-cl8-funciones.tsv) (1.125 funciones) y
[`2026-11-12-ola-cl8-tramos.tsv`](2026-11-12-ola-cl8-tramos.tsv) (los tramos de las funciones grandes): es la
entrada de la CL.M para esta zona del binario, no la matriz. Los cinco informes del barrido quedan fuera de Git,
en los archivos del proyecto (`analisis/2026-11-12-cl8/`); lo que vale de ellos está acá y en los docs.

## Restricciones de todo el plan

- Las de `AGENTS.md` y `docs/CODING_RULES.md`: `clang-format-18`, `misc-include-cleaner` limpio, el chequeo de
  conversiones de MSVC, `MURET_WARNINGS_AS_ERRORS=ON` y la suite entera con GL y los golden. El cliente no
  compila en el contenedor de la nube: el CI de GitHub es el que manda.
- **Un hito, un PR** contra `main` (Emanuel, 2026-11-11: PRs más grandes, por hito). Título `CL8 hito N: …`,
  secciones "Qué hace", "Research" y "Pruebas". Squash; para traer `main` a una rama ya empujada, merge commit
  (el force push está bloqueado). Revisión adversarial contra el ASM antes de abrir cada PR.
- **Fidelidad al ASM primero**, con las etiquetas del README: *estático*, *inferido*, *verificado*, *emulado*.
  **No se hacen emulaciones nuevas** (traspaso de la CL7): los `rand()` se cuentan leyendo el ASM; lo que la
  lectura no cierra va al 08 con su comprobación en la VM, al final de la ola.
- **El `rand()` cuenta.** Cada `rand()` de la UI se saca en el orden del original, por paso (25/s) o por frame
  (`OriginalFrameCarry`), con la tabla de protección modelada solo como sus `rand()` (`LogicalAccess`).
  Un gancho de otra ola saca lo que el original saca en el estado base; si ese estado deja de valer (por
  ejemplo, llega un paquete de evento), corta con error y la dirección, **después** de los `rand()` que el
  original saca antes, como en la CL7.
- **Cortes por ola** (Emanuel, 2026-11-12): lo que abre otra ola se corta con su dirección y queda en el 08
  (P-C59) con su ola; se resuelve en esa ola. Lo que solo resuelve la VM va al final.
- **Todos los mapas.** El HUD tiene casos por mapa (Tarkan 8, Chaos Castle 18..23, Castle Siege 30, Crywolf 34,
  los de evento): cada hito los porta o los corta con su dirección, nunca solo Lorencia. Los tests van en
  Lorencia (0) y Devias (2), y en el mapa del caso cuando lo hay.
- **Privacidad:** ningún test, log ni vector lleva texto de chat real, nombres de cuentas ni de personajes. Los
  tests del chat usan textos inventados; del cable se usan solo cabeceras, largos y paquetes sin nombres.

## Lo que encontró el barrido

**El alcance.** Desde las raíces de la UI del juego (la cola 2D de `0x6371a0`, el bloque de UI de `0x636290`,
`0x623e70`, `0x5ac8d0` y la captura), el grafo de llamadas da unas **650 funciones y 590 KB que MuRet no
porta**. De eso, **~250 KB son de la CL8** (unos 11 KB son plantillas de la STL que se reemplazan por la STL);
el resto son ramas de la CL9 (party, gremio, comercio, tiendas, eventos, NPC), de la CL10 (sonidos) y de la
CL11. Las funciones que más mezclan olas (`0x623e70` 40 KB, `0x5ca250` 60 KB, `0x5d8d50` 31 KB, `0x59ff10`
22 KB, `0x56c440` 42 KB, `0x6599a0` 12,5 KB) se portan como **despacho**: el esqueleto y los casos de la CL8,
con los demás casos como cortes por dirección.

**Los cuatro mecanismos de ventanas** (*estático*):

1. **Los paneles por bits** `[0x576352c]`: el inventario (`0x4000000`), el personaje (`0x2000000`), la party
   (4), /move (2), el quest (8), el gremio (`0x10`), el comercio (`0x20`), el almacén (`0x40`), la caja del caos
   (`0x100`), la tienda de NPC (`0x10000`), las tiendas personales (`0x800`, `0x20000`) y otros 15 ids, con
   `IsOpen` `0x659290`, `CanOpen` `0x659860`, `Open` `0x6599a0`, `Close` `0x65cae0`, `CloseByMask` `0x657ab0`
   y las teclas `0x657880`. **Son UI inmediata** en su mayoría: el paso las atiende en `0x5ee140` y el HUD las
   dibuja con funciones sueltas. Algunos ids tienen un objeto retenido detrás: los seis que arma `0x657450` desde
   `WinMain` (el gremio `0x10` y `0x40000`, los tres de Castle Siege y el diálogo de los paneles, con paso por
   virtual en `0x657930` y dibujo en `0x6579e0`), y otros delegan su estado (el 1 en el administrador viejo, el 8
   en `[0x710148]+0x21383`, el `0x200`, el `0x400`, el `0x8000`, el `0x400000`/`0x800000` y el `0x1000000`). Las vtables que el laboratorio llamó "inventario" y "personaje"
   (`0x6ba7c8`, `0x6ba804`) son de la UI nueva del login y la selección.
2. **El administrador viejo** `*0x5763514` (ventanas retenidas de vtable de 23 entradas, un mapa id → ventana,
   una lista en orden z, una cola de mensajes, la fábrica `0x48ee60`): solo lo usan los amigos, el correo, sus
   cajas de mensaje, la lista de ventanas minimizadas y el registro del chat. No es el del inventario, como
   decía el 06.
3. **La UI nueva** `0x7d53728` (la del login y la selección, ya portada en parte): en el juego (modo 5) queda
   **vacía e inerte**.
4. **Los diálogos del juego** `0x7d3a10c`/`0x7d3a110`: `0x623e70` los atiende por paso y `0x62db60` los dibuja
   por frame. `0x623e70` no es "la interfaz del juego" (como decían el 02 y el 06): es este sistema, más el menú
   de Esc (el `0x6e`) y las opciones (el `0x93`).

**El clic es primero de la UI** (*estático*). Una sola bandera, `0x775857c`, dice que el mouse es de la UI: se
pone en 0 al empezar el bloque, en 1 con y ≥ 432, en 1 desde cada ventana con el mouse en su rectángulo y en 1
con un diálogo abierto. `MoveHero` (`0x59adf0`) la lee para no girar (`0x59b37a`) ni caminar (`0x59b853`) y el
picking del frame (`0x59ec80`) para no buscar nada. El flanco del clic `0x7d366d4` se consume. **MuRet hoy
diverge:** `GameMovementState::facingBlocked` nunca se escribe y `ProcessInput` usa `mouseY > 431`, así que el
héroe gira (`18 .. 7A`) con el mouse sobre la barra y el original no.

**No hay minimapa en el 1.02.** El "modo 4" de `0x77f3b08` (`IsOpen(2)`) es la **ventana /move**
(Text 933). La fila del roadmap pasa a decir "CL8.1 barras y /move".

**Los `rand()` de la UI** (*estático*; los números finos van al 08, P-C60):

- `IsOpen(0x20)` e `IsOpen(0x10000)` leen un byte protegido (`0x7d53711`, `0x7d53710`) y sacan **1 `rand()`
  cada uno, aunque estén cerrados**; los demás ids, 0. `IsOpen(0)` con todo cerrado, `CanOpen`, `CloseByMask`
  y Esc los consultan: Esc con todo cerrado saca 2 antes de abrir el menú (Q-C447).
- **El paso quieto saca al menos 3.850 `rand()` en `0x59ff10`**, que corre en cada paso (`0x636ee6`; su única
  salida temprana es `0x59ff3f`): el tramo de la barra de skills abre el registro propio en `0x5a05e5` y lo
  cierra en `0x5a10e2` aunque no haya skills (3.848, Q-C458), y la cola `IsOpen(0)` de `0x5a5511` saca 2. Las
  teclas del chat (`0x59ff45..0x5a03c3`) no están contadas todavía: es un piso. MuRet hoy no saca ninguno.
- **El HUD saca decenas de miles de `rand()` por frame** por el registro propio (`0x7681eb4`, 3.848 por cierre):
  en el estado base, como mínimo 73.189 (`0x5ab810`, `0x5aac20` con sus 12 ranuras, `0x5a9290`, `0x5abcd0`,
  `0x5a8cc0`, `0x5a9630` y los `IsOpen`). Van después de los del 3D del mismo frame.
- El picking del frame (`0x59ec80`) saca 3.848 por frame con el mouse en el mundo y el héroe de clase 0 o 2,
  y 0 con el mouse en la UI. MuRet no lo porta todavía.
- Abrir el inventario cuesta 4; el personaje y los amigos, 3.848 más; cada `C3` de la UI, 2 (relleno y serial).
- **La vuelta de `WinMain`** (*estático*, Q-C457, P-C61): sin mensajes pendientes, abre y cierra el contador
  protegido de 4 bytes `0x5763578` (`0x4bbc57`, cierre en `0x4bdf6d`: 4 `rand()` mientras valga ≤ 30) y el foco
  `0x5763580` (`0x4bdf76..0x4be02f`: 1), y con foco llama al frame `0x63945d`, que después de `0x637700` vuelve
  a abrir y cerrar el contador (`0x63950d`, cierre en `0x63b066`: 4). **Son 9 `rand()` por frame del original**,
  en todas las escenas; cada ~31 vueltas el contador va por otra rama, sin leer. Además el WndProc lee el foco en
  cada mensaje que llega a su cola común (`0x4b335a..0x4b3414`): **1 por mensaje de Windows**, que no se puede
  reproducir paso a paso.

**El ancho del mundo** (`0x5bb8d0`) depende de los paneles abiertos: 640 sin ninguno, 450 con uno de una lista
(el inventario, la C, 4, 8, `0x10`, `0x200`, `0x2000`, `0x40000`, `0x80000`, `0x100000`, `0x200000` o
`0x78abb00`) y 260 solo con ciertos pares (el inventario con la C, `0x10000`, `0x40`, `0x100`, `0x20`, `0x1000`,
`0x4000`, `0x800`, `0x20000` u `0x8000`; la C con 8 o `0x400`; el inventario con el `0x100000` de tipo 3);
`0x44dc20` y `0x44f100` pisan el resultado. **Con el inventario abierto consulta `0x10000` y `0x20`: 2 `rand()`
por llamada**, y tiene 19 llamadores (9 en `0x5fb570` por frame, `0x63733e`, `0x63738a`, `MoveHero` en
`0x59b188`, `0x5aac20`, `0x5ab810`, `0x5a7320`…). MuRet tiene el 640 fijo (`GameScene.cpp:131`,
`TerrainCulling.h`, `CharacterSelectScene.cpp`); la CL7 lo dejó así (decisión 4 de su plan).

**El GameServer 1.02 reconstruido no atiende** varios opcodes que el cliente manda desde la UI (`BC`, `3F`, `9F`,
`A9` del 1.02 sí, `AA`, `AB`, `AC`, `AF`, `B2`, `B7`, `BD`, `D0`, `E5`, y `C0`..`CA` de amigos y correo): el
cliente es la Season 1 completa. Para esos, el oráculo es el cliente (los bytes del ASM), no el servidor.

## Decisiones

1. **Sin CL8.0 de laboratorio:** el tutor está apagado. Los conceptos (UI retenida contra inmediata, el foco del
   mouse) se explican en los PRs.
2. **Los paneles no son clases.** `UI::PanelRegistry` reproduce los bits de `[0x576352c]` con su tabla, su orden
   fijo de consultas y sus `rand()`; cada id tiene ganchos (abrir, cerrar, paso, dibujo, rectángulo) que
   registra su ola, y su estado puede ser un byte propio (protegido o no) o delegado en otro objeto, como en el
   original. Un id sin dueño se comporta como cerrado (y su consulta saca lo que saca). La clase
   retenida (`UI::Window`) queda para lo que en el original es retenido: el administrador viejo y la UI nueva.
   La fila del roadmap cambia de "CL8.3 `UI::Window` y el administrador" a "CL8.3 los paneles, los diálogos y
   los dos administradores".
3. **Un solo dueño del orden:** `UI::GameUi` corre el bloque de UI del paso y la pasada 2D, y es el único lugar
   que llama a las piezas. Ningún hito porta su parte "por su lado". También son suyos los sitios de la UI que
   `0x637700` llama fuera de esos tramos, cada uno en su lugar: `0x623e70` en `0x637c5c` (por paso, **antes** de
   `0x636290`), `0x5ac8d0` en `0x63717f`, los relojes del chat `0x5547e0`/`0x5554b0` en `0x637cf3`/`0x637cf8`, el
   flanco de PrintScreen en `0x637cff` y la bandera del diálogo en `0x63705b`.
4. **Los ganchos sacan los `rand()` del estado base** y cortan con error y dirección cuando el estado deja de
   ser el base (decisión 4 de la CL7, generalizada).
5. **Usar y tirar ítems (`26`, `23`) van con la CL9.3**, con los ítems del piso y las pociones del HUD: "inventario
   usable" en la CL8 es abrirlo, verlo con sus modelos y su tooltip, moverlo, equipar y desequipar (`24`) y la
   ventana del personaje con sus puntos. Emanuel puede pedir que entren en el hito 4.
6. **Los opcodes que el GS 1.02 no atiende están en alcance** en su ola (el roadmap cubre todo el `main.exe`,
   Emanuel 2026-11-10); se prueban por bytes contra el ASM.
7. **El administrador viejo entra en la CL8** (el marco, las cajas de mensaje, la lista de ventanas, F5 y F6):
   el registro del chat es una de sus ventanas y el orden del mouse del paso pasa por él. Los amigos y el correo
   (su contenido y sus paquetes `C0`..`CA`) son de la CL9.4.
8. **El Esc, las opciones y los créditos del login y la selección** (`+0x3d0`, `+0x7e8`, `+0x4578` de la UI
   nueva) **entran en la CL8**, como dice el roadmap (§ Lo que dejó la CL6), en el hito de la UI nueva.
9. **Sin IME:** la entrada de texto es SDL3 (`SDL_EVENT_TEXT_INPUT`) pasada a Latin1, como el `TextField` del
   login. El IME coreano queda fuera (el `Text.bmd` es occidental y el original lo apaga en `0x467360`).
10. **El ancho del texto del chat** (los cortes a 256 y a 180 px de `GetTextExtentPointA`) sale de FreeType en
    640 × 480; la diferencia con GDI se registra (P-C60) y la VM la mide.
11. **La captura** usa el compresor de libjpeg-turbo (calidad 100, filas de abajo arriba). El archivo no tiene
    que ser igual byte a byte al del original; el nombre, el contador y el mensaje, sí.
12. **El salto del LCG:** `CrtRandom::Discard(n)` en O(log n), probado contra n llamadas a `Next()`, para los
    miles de `rand()` del HUD por frame.
13. **La vuelta de `WinMain`** (P-C61): MuRet saca sus 9 `rand()` por frame del original en todas las escenas,
    en el hito 1: 4 del contador y 1 del foco antes de los pasos y el dibujo, y 4 del contador después. La rama
    del contador de cada ~31 vueltas se lee en el hito 1 (si no cierra estática, va al 08). El `rand()` del foco
    por mensaje del WndProc no se modela: depende de cuántos mensajes manda Windows. Queda como divergencia
    registrada (Q-C457), salvo que Emanuel pida otra cosa.
14. **El picking del juego** (`0x59ec80`): el hito 1 porta la compuerta de la bandera, la de Alt
    (`GetAsyncKeyState(0x12)` en `0x59ef37`) y el tramo que saca los `rand()` (`0x59efda..0x59f46e`, con la clase
    0 o 2); la búsqueda del ítem del piso, el NPC, el personaje y el objeto queda en
    sus olas (CL9.1, CL9.3) como cortes que dejan −1, que es el estado de la traza.

## Los hitos

Ocho hitos, un PR cada uno. Los KB son de binario aproximados (lo que el hito porta, más lo que solo deja como
gancho); el detalle por función está en el TSV.

### Hito 1: la UI del juego toma el mouse (CL8.3, ~12 KB propios y ganchos sobre ~45 KB)

El esqueleto de todo lo demás. Entra:

- [x] **El registro de paneles** `UI::PanelRegistry`: `0x659290` (con los dos bytes protegidos y su `rand()`),
      `0x659860`, `0x65cae0`, `0x657ab0` (el orden fijo de 26 entradas, cada cierre como gancho), el esqueleto y
      la salida común de `0x6599a0` (`0x65ca72`: sonidos como gancho de la CL10 y el `1` que devuelve aunque el
      caso falle, Q-C445), `0x657880` (Esc al bit más bajo), `0x657a90` (reemplaza la cuenta a mano de
      `WorldEntry.cpp` y `WorldTransitionSupport.cpp` y resuelve el corte de P-C58 con ventanas abiertas) y
      `0x657930`/`0x6579e0` como ganchos.
- [x] **El clic consumible y la bandera:** `0x7d366d4` (flanco), `0x7d36874` (abajo), `0x7d366d8` (rueda) y
      `0x775857c` en el estado del paso; `MoveHero` la lee en `0x59b37a` y `0x59b853` (se va `facingBlocked` y el
      `mouseY > 431`); `SendMove` mira `0x7d3a10c != 0x70` antes de `Movement = 1` (`0x56b387`) y cierra las
      ventanas contra el registro real; el corte de `0x78abb00` (`C1 03 97`, CL9.5) queda explícito.
      *Hecho (hito 1):* también el clic derecho (`0x7d36880`); la rueda queda para su lector, el volumen de las
      opciones (`0x625782`, hito 6).
- [x] **El bloque de UI del paso** `0x636ded..0x63705b` en `GameScene`, en su orden, con un gancho por llamada:
      la bandera, el mapa (`0x5ecdc0`), las colas de texto (`0x46b7b0`), la lista de ventanas, `0x490490`, el
      registro del chat, el clic sin dueño, `0x5ebca0`, `0x435bd0`, `0x5ee140` (sus puertas, sin hijas),
      `0x59ff10`, `0x5a7cc0`, `0x657930`, el lugar del registro del chat y la bandera del diálogo.
- [x] **`0x59ff10`, el esqueleto:** la salida temprana (`0x59ff3f`), el tramo de la barra de skills como gancho
      que abre y cierra el registro propio en su lugar (`0x5a05e5..0x5a10e2`, 3.848 por paso; la barra en sí es de
      la CL9.2), las letras de los paneles (G, P, T, C, I, V, F, M, D por `0x5a4ea6`, `0x5a4c45..0x5a5504`), B
      (`0x5a4ec2..0x5a4f2e`) y R (`0x5a4f34..0x5a54e6`, que abre el registro propio y lo cierra con la tecla
      apretada: 3.848 más; la reparación es de la CL9.3) y la cola `IsOpen(0)` (`0x5a5511`, 2). Los botones del
      HUD, las pociones, las macros y las teclas del chat quedan como ganchos de los hitos 2 y 3 y de la CL9;
      antes de cerrar el hito se cuentan los `rand()` de las teclas del chat (`0x59ff45..0x5a03c3`) en el estado
      base.
      *Hecho (hito 1):* los botones del HUD entraron acá (el hit-test y el panel que abre cada uno), y la cadena de
      Q/W/E saca 2 `rand()` por paso en el estado base (Q-C459): el paso quieto es de 3.852.
- [x] **Los diálogos, lo mínimo:** el par `0x7d3a10c`/`0x7d3a110`, `0x6236f0` y el prólogo de `0x623e70`
      (`0x623e70..0x623f78`: `IsOpen(1)`, el flanco de Esc, `0x657880`, abrir el `0x6e`). El menú en sí es del
      hito 6.
- [x] **La pasada 2D del juego** `0x637641..0x6376eb` en `GameScene`, en su orden, con el estado de mezcla y color
      que deja cada llamada y un gancho por llamada (la tabla de 06 § El dibujo del juego, que este hito
      completa); el picking (`0x59ec80`) con sus compuertas y su tramo de `rand()`; el ancho del mundo
      (`0x5bb8d0`) con su orden de consultas, llamado en cada sitio donde lo llama el original (sus `rand()`
      dependen de cuántas veces se llama), en lugar del 640 fijo.
      *Hecho (hito 1):* el picking y el ancho del mundo en `0x63733e`, en el giro de `MoveHero` (`0x59b188`) y en
      el picking (`0x5e4140`); los demás llamadores son de los hitos que los portan. Las llamadas de la pasada 2D
      quedan como ganchos en su orden, sin dibujar ni sacar `rand()`: la tabla del estado de GL va con el HUD
      (hito 2), que es el primero que dibuja.
- [x] **Los 9 `rand()` de la vuelta de `WinMain`** (decisión 13), en el bucle, para todas las escenas.
      *Hecho (hito 1):* `Scenes/WinMainLoop` (los dos contadores, con la rama de la vuelta 31 de Q-C461: 21 en
      lugar de 5) y `main.cpp`, que saca los 5 de la primera vuelta antes de entrar a la primera escena y, al final
      de cada vuelta del bucle con la ventana sin minimizar (`ILoopClient::FinishTheLoop`, `afterSceneFrame` del
      `SceneManager`), 4 + 5 por cada frame del original que corre en ella: uno por vuelta en el modo fiel, por el
      reloj en el desacoplado, como la tela. Con el frame retenido también, porque el original corre el frame sin
      presentarlo; minimizada no, porque el original va por la rama sin frame, que no se modela. Lo que queda
      distinto (P-C61): en el connect del login (Q-C85) el original está parado adentro del paso y MuRet sigue
      dando vueltas, y en el desacoplado, al volver de minimizada, el reloj pone al día hasta 64 vueltas de una vez.

**Pruebas:** los `rand()` por paso con todo cerrado (los 3.848 de la barra de skills y los 2 de `0x5a5511`), con
Esc (2 y el `0x6e`, **antes** de los del paso), con R apretada (3.848 más), con el inventario abierto por `I` (4)
y cerrado (2), y con el inventario abierto (los 2 de cada llamada al ancho del mundo); cada id de `IsOpen` (solo `0x20` y `0x10000` sacan); el orden de
las 26 consultas de `CloseByMask`; un clic en y ≥ 432 que no camina ni gira (no sale `18 .. 7A` ni `D3`); el
ancho del mundo con cada panel y con los pares que dan 260; los 9 de la vuelta de `WinMain` en el login, la
selección y el juego. **Desbloquea:** todos los demás. **Cierra:** la divergencia del giro, el
`0x657a90` de P-C58, P-C41 (a) en lo que toca al bloque de UI.

### Hito 2: el HUD (CL8.1, ~42 KB)

- [x] **`0x5a7320` hijo por hijo** (31 llamadas, en su orden; 06 § HUD y ventanas, reescrita en este PR): los de
      la CL8 portados y los de otras olas como ganchos con su `rand()` del estado base (las flechas `0x5ab810`
      abren y cierran el registro propio siempre: 3.848).
- [x] **La barra:** `0x5a9290` (Menu01..04), `0x5a9630` (los orbes con el alto de Q-C444, la skill actual con su
      tinte, las celdas, los números, los botones apretados y sus ayudas), `0x5abcd0` (XP), `0x5a8cc0` (AG),
      `0x6218f0` (los dígitos), `0x5a6af0` (las barras chicas), `0x5ab810`, `0x557d80`, `0x5aac20` (los avisos de
      durabilidad), `0x5ab210` (el ayudante; la vida del invocado como gancho de la CL9.2 con su global),
      `0x42a660`/`0x5a63d0`/`0x5a6600` (los íconos de estado) y lo justo de `0x5a5860`/`0x40ca20`/`0x5531d0`
      para el ícono de la skill. La ayuda de los íconos de estado (`0x5a6600`) corta: es del hito 5.
- [x] **El 3D de la UI:** `0x5ac120` (la proyección de 1°, la cámara, los pases), las ranuras rápidas
      (`0x558110`, `0x558490`) y el camino `0x5e1480` → `0x5e0720` del ítem en la UI, que reusa el inventario.
      Hecho sin el ítem 3D: `0x5e1480` solo dibuja y llega con los ítems 3D del inventario en el hito 4 (08,
      P-C63).
- [x] **Los botones del HUD** en `0x59ff10` (`0x5a03c3..0x5a05e5`): el hit-test y el panel que abre cada uno.
- [x] **El cursor del juego** `0x5ac2d0` con su tabla de elección (06 § El cursor del juego) y **dibujado dos
      veces** (Q-C442); los casos del picking leen globals que valen −1 hasta su ola.
- [x] **El `rand()` del HUD por frame** con `LogicalAccess` y `CrtRandom::Discard`; `0x5f7440`, `0x5f7610` y
      `0x5f7840` (las ventanas y sus grillas) como despacho con sus `IsOpen`.

**Pruebas:** la traza de GL de Lorencia, frame 1 (`lorencia-frame.txt`, líneas 124230..126903): **todos** los
quads 2D del HUD tienen dueño (la barra, XP, AG, los orbes, la skill, las celdas, los números, las ranuras con
sus dos pociones y el pase `Chrome01`, la barra del invocado y los dos cursores); posición, UV, textura, color y
mezcla. Los `rand()` del HUD por frame en el estado base, contados del ASM. Devias y un mapa con caso propio
(Tarkan, las arenas de `0x5a7520`). **Desbloquea:** el hito 4 (el ítem 3D) y el 6.

**Hecho** (2026-11-12, `GameHudTest`): los 43 quads 2D del frame 1 de Lorencia en 1024 × 768 (la barra del
ayudante, que no es la del invocado, la barra de abajo, XP, AG, los orbes, la skill, las celdas, la vida, el maná, los
números de las ranuras y los dos cursores) con su textura, mezcla, color, esquinas y UV; los `rand()` del HUD:
73.129 en el estado base y 77.157 en el frame de la traza (06 § HUD y ventanas). El 3D de las ranuras (las pociones
y el pase `Chrome01`, traza 124479..126583) queda para el hito 4 con `0x5e1480`. Los cortes que abre el hito están
en 08, P-C63: el panel del asedio (`0x40f850`, el mapa 30) y la cuenta de DS/BC/CC, CL9.5.

### Hito 3: el chat (CL8.2, ~47 KB)

- [ ] **Las dos áreas de mensajes:** la vieja (`0x77d9868`, 120 renglones; `0x555190`, `0x5549a0`, `0x5554e0`,
      los relojes `0x5554b0` y `0x5547e0`), que también dibujan el login y la selección (P-C27 a, el 1240 de la
      selección), y la nueva (`0x576350c`, un registro del administrador viejo: `0x45b620`, `0x45bb20`,
      `0x459c70`, `0x45c460`, `0x45cbb0`, `0x45c140`, `0x45bfe0`, `0x45d8c0`, `0x45d990`, `0x45d760`,
      `0x45dc40`/`0x45ddc0`/`0x45de20`). Esto trae la base de las ventanas viejas (`0x45a210`: id, estado,
      rectángulo, cola de mensajes) que completa el hito 7. Un `MessageLog::Add(nombre, texto, tipo)` para los 98
      llamadores.
- [ ] **Los avisos centrados** (`0x554610`, `0x554810`) y **el cartel de `Slide.bmd`** (`[0x5763528]`,
      `0x46b3b0`, `0x46b090`, `0x46b7b0`).
- [ ] **Escribir y mandar:** los dos campos (50 y 10 bytes, Latin1, seleccionar todo al tomar el foco, Tab, el
      historial `0x469540`/`0x469790`), la caja `0x5aa9a0`, F2, F3, F4, PgUp/PgDn, arriba/abajo, el clic derecho en
      los renglones, Enter con todas sus compuertas (el filtro, el freno `0x5767940`, "webzen", `/whisper on/off`,
      muerto con `/move`), `0x56c440` solo con sus compuertas, `/filter`, `/N` y Chaos Castle, `C1 00`, `C1 02` y
      `0x554a40` (3.848). El predicado `TextInputBusy()` (`0x6578e0`) que frena las letras del hito 1.
- [ ] **Recibir:** `00`, `01` (sin el Guild Master), `02` (`0x554d20`, 3.848), `0C`, `0D` 0/1/2 (con la franja
      `+0x511` de la selección) y `F3 30` entero (`0x4dd570`: guarda las teclas de skills y Q/W/E para la CL9).
- [ ] **Los globos** (`0x557170`, `0x557560`, `0x557650`, `0x556dd0`, `0x5a9050`, `0x555840`, `0x555b00`,
      `0x513270`, `0x5132a0`; el `#` largo; la tabla compartida con el hover del hito 6) y **las macros**
      (`0x61c1c0`/`0x61c170` con `Data\Macro.txt`, Alt+dígito con la tecla abajo, Q-C449 y Q-C451, el bloqueo
      `0x77f3b74` y la barra que dibuja el HUD).

**Pruebas:** bytes de `C1 00` y `C1 02` con el XOR32 contra el ASM y el GS reconstruido (`CGChatRecv`,
`CGChatWhisperRecv`); el formato, los colores (Q-C450), el corte a 256 y a 180 px, la cuenta de 120 y el
corrimiento, los relojes de 151 y 301 pasos; los `rand()` (mandar 0, susurrar 3.848, recibir un `02` 3.848,
`F3 30` 3.848). Textos inventados. **Desbloquea:** la captura (su mensaje) y /move (manda por el chat).

### Hito 4: el inventario y el ítem en el cursor (CL8.4, ~40 KB)

- [ ] **El dibujo:** `0x5f2340`, `0x5ee220`, `0x5e2460`, `0x5e3660`, el zen, los ítems de la grilla con su modelo
      (`0x5f7610` → `0x5e1480`) y el ítem del cursor (`0x5f7b10`); las texturas `0x104`..`0x120` y `0x658` con su
      archivo, leído de `0x61ccb0`.
- [ ] **El paso:** las hijas del inventario en `0x5ee140`, `0x5be150` entero (P-C58), `0x5bec50` en las ramas de
      inventario y equipo, `0x5d8d50` y `0x5ca250` en sus tramos de la CL8.4 (el tramo de tramos del TSV), con
      `0x5c9b40`, `0x5c3d20`, `0x5bd750` y `0x449d80` (`C3 24` con la traba `0x78abb39`).
- [ ] **El equipo:** volver a vestir al héroe al equipar y desequipar (`ActorEquipment`, `0x550d10`), los
      derivados (`0x54e540`, P-C58) y el corte de `MoveHero` `0x59bd3e` (el `24` del lugar 1 al 0, en `C3` con 2
      `rand()`; 06 corregido).
- [ ] **Las respuestas:** `24` (`C3`), `F3 13`, `F3 14`, `2A` (durabilidad) y lo que el inventario muestre de
      `26`/`27`/`28` sin usar ítems.

**Pruebas:** el cable de la sesión (`wire/20261030T192219Z_8920d1`): los `C3 24` del cliente y del GS
descifrados con SimpleModulus (cabeceras y cuerpos sin nombres); la grilla de 8 × 8 con ítems de varios tamaños
(`0x5c9b40`); los `rand()` del paso con el inventario abierto (2 por `0x636f49` y `0x636fb2`, además de los 3.850 del paso
quieto y los 2 de cada llamada al ancho del mundo). **Desbloquea:**
el hito 5 y la CL9.3.

### Hito 5: los tooltips y la ventana del personaje (CL8.4, ~50 KB)

- [ ] **El tooltip** `0x5f7bc0` y sus tres cuerpos (`0x5b2aa0`, `0x5b8150`, `0x5b90b0`) con sus helpers, el
      cartel del set (`0x40eca0`) y la info del ítem del modo 3 de las superposiciones (`0x5afc00`, `0x5af590`,
      `0x5af9b0`).
- [ ] **La ventana del personaje** (`0x2000000`): el dibujo `0x5ee2e0`, el paso `0x5e4510` (los botones de
      puntos con el registro propio, 3.848, y `F3 06`), las pestañas y el `A9 01 00 08` (`0x424130`/`0x424230`,
      Q-C16), y los cálculos que muestra (`0x54d0f0`, `0x5516e0` y vecinas).

**Pruebas:** los renglones del tooltip por ítem (Item.bmd, los sets), sus colores y su orden contra el ASM; el
cable (`A9 01 00 08` una vez por apertura, 10 en la sesión); los `rand()` con la C abierta. El orden fino del
tooltip va a la VM (P-C60). **Desbloquea:** "inventario usable".

### Hito 6: los diálogos, el menú de Esc, las teclas y la captura (CL8.1 y CL8.3, ~36 KB)

- [ ] **Los diálogos:** `0x633300`, `0x632e30`, el despacho por id de `0x623e70` con sus tramos de la CL8 (los
      cierres, el OK genérico, el `0x6e` y el `0x93`; 06 lista los tramos) y su dibujo en `0x62db60`; cada ola de
      la CL9 registra sus ids en el mismo despacho.
- [ ] **El menú de Esc** (el `0x6e`: salir con `0x61c210` `F3 30` y 3.848, `Macro.txt`, `C3 F1 02 xx`; Q-C455) y
      **las opciones** (el `0x93`: auto-ataque `0x6c8e3c`, aviso de susurro `0x77f3b78`, volumen `0x7d3a118` como
      gancho de la CL10, brillo `0x6c9dcc` que ya lee `ItemGlow`); el diálogo de los paneles (`0x65feb0`,
      `0x6605b0`, `0x660830`, `0x660410`, `0x660350`, `0x6576a0`).
- [ ] **Las ventanas del paso que son marco:** la party (`0x5e3c80` sin el `43`), el botón del quest (`0x41b5a0`
      sin el contenido), la puerta de D (`0x40a450` sin `0x406050`) y los cortes sin `rand()` de las de la CL9.
- [ ] **Las teclas y las superposiciones:** `0x5ac8d0` (F1..F4; SPACE como corte de la CL9.3), `0x5b0550` con las
      ayudas F1 (`0x5af090`, `0x5afb00`) y **la ventana /move** (el paso `0x5ecdc0` → `0x449850` → `0x448580` y el dibujo `0x5b0550` → `0x449860` →
      `0x448fa0`, con
      `Data\Local\movereq.bmd`).
- [ ] **El hover y ALT** (`0x5bbbc0`: los nombres por los globos; los carteles del piso como corte de la CL9.3) y
      **los textos 3D** (`0x4383f0`; el productor de Kalima es de la CL9.5).
- [ ] **La captura:** el flanco de PrintScreen (`0x637cff`), el nombre y el contador (`0x637700`), `0x6202e0`,
      el JPEG y el mensaje Text 459 por el área de mensajes.

**Pruebas:** los bytes de `F1 02 xx` y `F3 30` y su orden; los `rand()` de salir (3.848 + 2); el nombre de la
captura con un reloj falso; /move con `movereq.bmd`. **Desbloquea:** "teclas de la UI" y "captura".

### Hito 7: las ventanas viejas (CL8.3, ~41 KB, de los que ~11 son STL)

- [ ] **El administrador** (`0x48e470`, la fábrica `0x48ee60` sin los casos de amigos y correo, `0x490390`,
      `0x490490` con Esc, F5 y F6, `0x490200`, `0x4906a0`, `0x4914e0`, `0x490b70`), **la ventana con marco**
      (`0x491ca0`, `0x492500`, `0x492ac0`, `0x492180`, la paleta `0x491960`), los controles viejos (botón, lista,
      campo, barra), **la caja de mensaje** (`0x6b9cb4`, casos 5, 8, 9 y 10) y **la lista de ventanas**
      (`[0x5763524]`).
- [ ] Los cortes de Chaos Castle de `WorldTransitions.cpp` (`0x4906a0`/`0x4914e0`) se resuelven acá.

**Pruebas:** una caja de mensaje abierta a mano: el marco contra el ASM (no hay traza con ventanas viejas: va a
la VM); el orden del mouse (lista, de arriba hacia abajo, registro del chat); Esc según `IsOpen(1)` (Q-C446); F6
dos veces (Q-C448). **Desbloquea:** amigos y correo en la CL9.4.

### Hito 8: la UI nueva generalizada y lo que quedó del login (CL8.3, ~14 KB)

- [ ] **El administrador de la UI nueva** (`0x65fc30`, el arrastre `0x65f720`/`0x669f40`/`0x65fbb0`,
      `0x65f610`/`0x65f690`/`0x65f6c0`, los modos y el vaciado del modo 5 `0x65f5f0`): mueve el orden del foco y
      del mouse de cada ventana del login a un solo lugar (Q-C84) y cierra P-C08.
- [ ] **El Esc del login y la selección** (`+0x3d0`: `0x6478c0`, `0x647d40`, `0x647f60` con su `C3 F1 02`),
      **las opciones** (`+0x7e8`: `0x644120`, `0x644730`) y **los créditos** (`+0x4578`: `0x63f020`, `0x63f630`,
      con `Data\Local\credit.bmd`).

**Pruebas:** las del login y la selección siguen verdes; el arrastre y los `rand()` del `C3 F1 02`. Puede ir en
cualquier momento después del hito 1.

### Orden

1 → 2 → 3 → 4 → 5, con el 6 después del 2 y el 3, el 7 después del 3 y el 8 en cualquier momento después del
1. Al final, una **auditoría de cierre** contra el ASM, como la de la CL7, y la lista de lo que queda para la VM.

## Los cortes hacia otras olas

Cada uno queda en el código con su dirección y en el 08 (P-C59). Los principales:

| Ola | Qué | Direcciones |
| --- | --- | --- |
| CL9.1 | NPC y monstruos: picking de NPC y personajes, barras sobre los monstruos, diálogo de NPC (`0x8d`), quest, emotes, mensajes de monstruos | `0x59ec80` (búsqueda), `0x42a4d0`/`0x42a2b0`, `0x62d756..0x62da58`, `0x632f70`, `0x41ca40`, `0x418fb0..0x41b170`, `0x576eb0`, `0x4fc790`/`0x4fc8c0`/`0x4fcd00` |
| CL9.2 | Combate y skills: el cursor de ataque, la barra de skills, el invocado, los estados | `0x558bb0`, `0x5a05e5..0x5a1170`, `0x5ab210` (invocado), `0x42a660` (los bits), `0x5531d0` |
| CL9.3 | Ítems: el piso, levantar (SPACE), usar (`26`, Q/W/E), tirar (`23`), tiendas, almacén, caja del caos, joyas, ítem de diálogo | `0x5ba790`, `0x5650c0`, `0x5a2b47..0x5a4c45`, `0x5c4110` (`26`), `0x5df430..0x5dfe34` (`23`), `0x5f4720`, `0x5f4e30`, `0x5febf0`, `0x5fd4f0`, `0x5e7aa0`, `0x633110`, `0x6259c6..0x62601e` |
| CL9.4 | Party, gremio, comercio, duelo, tiendas personales, amigos y correo, comandos | `0x5a89b0`, `0x5f0be0`, `0x5a7640`, `0x5a6cc0`, `0x5f37f0`, `0x5f3b40`, `0x5f3030`, `0x406050`, `0x56c440` (comandos sociales), casos 1..7 de `0x48ee60`, `0x64f3e0`, `0x654900` |
| CL9.5 | Mapas de evento: Castle Siege, Crywolf, Blood Castle, Chaos Castle, Devil Square, chips | `0x5a6fc0`, `0x413ca0`, `0x652c90`, `0x5a7520` (humo), `0x5a7eb0`, `0x5a7cc0`, `0x40bab0`, `0x4365c0`, `0x5f58f0`, `0x5e6520`, `0x56b4db..0x56b78f`, `0x64aca0`/`0x661090`/`0x6485f0` |
| CL10 | Sonidos de la UI | `0x41ed10` (`0x19`, `0x1c`, `0x22`, `0x26`), el volumen `0x41eed0` |
| CL11 | Joints de evento | `0x530860` (desde `0x5a7d70`, muerto: Q-C456) |

## Los datos

- **Texturas:** las 256 rutas `Interface\…` del binario están en `Client/assets/Data/Interface`. El HUD usa los
  slots 2..10, `0x65a`, `0x68d` (cursores), `0xe6..0xee`, `0xf7`, `0xfa`, `0x101`, `0x12a`, `0x12b`, `0x68e`; el
  chat `0xef`, `0xf8`, `0xf9`; las cajas `0xf0..0xf4`, `0xfb..0xfd`; las ventanas viejas `0x645..0x651`. Falta
  mapear los slots del inventario (`0x104`..`0x120`, `0x658`) con su archivo en `0x61ccb0` (hito 4).
- **Tablas que MuRet no lee todavía:** `Data\Macro.txt` (hito 3), `Data\Local\Slide.bmd` (hito 3, `0x46b3b0`),
  `Data\Local\movereq.bmd` (hito 6), `Data\Local\credit.bmd` (hito 8). `Quest.bmd` y `Dialog.bmd` son de la
  CL9.1. Todas están en el `Data\`. `Filter.bmd` está vacío: el filtro del chat nunca salta.

## Los oráculos

- **La traza de GL de Lorencia** (frame 1): todos los quads 2D del HUD y los dos cursores (hito 2). No hay traza
  con ventanas abiertas.
- **El cable** (`wire/20261030T192219Z_8920d1`, 3,5 minutos): `A9 01 00 08` ×10 (la C), 23 `C3` del cliente con
  el serial 0..22, `C3 24` en los dos sentidos, `C1 26`/`27`/`2A`/`23`. Solo cabeceras y cuerpos sin nombres.
- **El ASM:** la cuenta estática de los `rand()` (sin emulaciones nuevas), los bytes de cada paquete, los
  layouts y los colores.
- **El GameServer reconstruido** para lo que atiende (`CGChatRecv`, `CGChatWhisperRecv`, `PMSG_NOTICE`,
  `CGCloseWindow` = el `31`).

## Lo que queda para la VM (al final de la ola)

En el 08 (P-C60): una traza de GL con el inventario, la C, una caja de mensaje vieja y el menú de Esc abiertos;
los `rand()` reales por paso (los ≥ 3.850 de `0x59ff10`) y por frame (el HUD, el picking) con un contador en
`0x69fd70`; los rectángulos reales de las ventanas con una y dos abiertas; el ancho de `GetTextExtentPointA`;
el orden fino del tooltip; qué abre F4 (`0x45a9d0`) y qué son `[0x5763530]` (`0x4125b0`) y la imagen de
`0x65dd70`; el ancho del texto de la línea 124268 de la traza (Q-nuevo del HUD); el foco protegido por mensaje
(P-C61).

## Las correcciones al research que trae este PR

- **06 § HUD y ventanas**, reescrita: `0x5a89b0` es la vida de la party (las barras propias son `0x5a9630`), no
  hay minimapa, `0x4383f0` es la cola de textos 3D (no los números de daño, que son `0x548eb0`), `0x5bbbc0` no
  dibuja barras de vida, ALT muestra los nombres de los ítems del piso, la fábrica `0x48ee60` es de amigos y
  correo (el inventario y los demás son paneles por bits), el id de la ventana vieja va en `+0x40`.
- **02** y **06** (`0x623e70`): es el sistema de diálogos con el menú de Esc y las opciones; los clics de las
  ventanas se consumen en el bloque `0x636ded..0x63705b`.
- **04:** el `A9 01 00 08` abre la ventana del personaje (`0x2000000`); el `31` es `CGCloseWindow`
  (`protocol.cpp:443`).
- **06** (el `24` del cambio de mano en `MoveHero`, `0x59bf1c`): sale como `C3`, con 2 `rand()`.
- **Laboratorio** (no se toca; se anota acá): paso12 llama "barra HP/MP/XP" a `0x5f2340` (es el inventario);
  paso15/paso17 toman `0x6ba7c8`/`0x6ba804` por inventario y personaje (son la UI nueva) y `0x6b9bf8` por una
  vtable de ventana (es la del administrador; la del caso 7 es `0x6b9bfc`); paso17 pone `0x5b0550` en el paso
  (es del dibujo); paso14 dice que `0x59ff10` corre por frame (es por paso) y que Q/W/E mandan "acción dirigida"
  (es `26`, usar ítem); paso5 dice "9..15" para el `0D` (es 10..15) y "%s : %s" para el `02` (es "%s: %s"); el
  `0x2000000` no es el almacén (es la C; el almacén es `0x40`). Las vtables viejas miden 23 entradas: leerlas
  "hasta la primera que no es función" las corta en la 17.

## Riesgos y dudas abiertas

- **El orden del bloque de UI** junta cuatro dominios (HUD, chat, inventario, ventanas): por eso el hito 1 lo arma
  entero con ganchos y nadie lo porta por su lado.
- **Los `rand()` del HUD por frame** son muchos y su cuenta exacta (`0x5aac20` tiene caminos de uno y dos
  cierres por ranura) pide lectura fina; si la lectura no cierra, se porta con la cuenta leída y la VM confirma.
- **El foco protegido por mensaje** (Q-C457): la secuencia de la CRT del original depende de los mensajes de
  Windows entre frames; MuRet reproduce la cuenta y el orden dentro de cada paso y cada frame.
- **El quest** cruza la CL8 (el botón y la ventana) y la CL9.1 (los datos con los NPC): el corte queda en
  `0x41b5a0`.
- Sin leer a fondo: `0x492ac0`, `0x490b70`, los números del marco viejo, `0x65f720`, `0x660410`, `0x5ee2e0`,
  `0x5b2aa0`, las ramas de la CL9 de `0x5ca250` y `0x5d8d50`, `0x406050`. Cada hito los lee antes de portar.

## Lo que cambió con la revisión adversarial

Una revisión independiente contra el ASM (2026-11-12) confirmó los `rand()` de `IsOpen`, el piso de 73.189 del
HUD por frame, los 3.848 del picking, los 2 de Esc, los 4 de abrir el inventario, el `rand()` del foco por
mensaje, la bandera del clic, el cursor doble y el orden de la pasada 2D. Corrigió tres cuentas y cuatro
detalles, ya aplicados arriba:

- El paso quieto saca **≥ 3.850** `rand()`, no 2: la barra de skills de `0x59ff10` abre y cierra el registro
  propio en cada paso (Q-C458). Faltaban las letras B y R.
- La vuelta de `WinMain` saca **9** por frame del original, no 1: el contador protegido `0x5763578` antes y
  después del frame, además del foco.
- El ancho del mundo saca 2 por llamada con el inventario abierto, y 260 sale solo con ciertos pares.
- Hay sitios de la UI fuera de los dos tramos (`0x623e70` antes del paso, `0x5ac8d0`, los relojes, PrintScreen),
  algunos paneles tienen un objeto retenido detrás, el picking también mira Alt, a /move le faltaba el paso
  `0x449850`, e `IsOpen(0x80)` siempre da 0.
