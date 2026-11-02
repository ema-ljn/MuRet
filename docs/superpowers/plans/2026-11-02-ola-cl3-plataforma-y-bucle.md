# Plan de la ola CL3: Plataforma y bucle de juego

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-02, con lo aprendido en la CL2. Cada sub-ola trae su contrato, sus nombres y
> sus tests; lo que se aprenda en una se escribe acá antes de arrancar la siguiente.
>
> **Quién escribe los pasos `TODO(human)`:** por pedido de Emanuel (2026-11-02, pregunta 1 de la
> CL2), Claude los resuelve también, hasta que él diga que participa. En
> [PROGRESS](../../learning/PROGRESS.md) quedan como resueltos por Claude.

**Objetivo:** sacar el hola mundo de la CL0.3 de `main.cpp` y dejar un cliente con capas: una
ventana con su contexto OpenGL 3.3 core, un bucle con la lógica en pasos fijos de 40 ms y el
dibujo desacoplado e interpolado (D-C4), el teclado y el mouse por frame, y la config, la línea de
comandos y el log. El bucle se prueba entero con un reloj falso, sin ventana y sin tiempo real, y
un modo fiel reproduce el ritmo del original: 21,33 frames por segundo y 75 pasos cada 64 frames
(7 u 8 cada 6).

**Arquitectura:** dos capas nuevas, como dice el roadmap. `Client::Platform`
(`Client/src/Platform/`) es lo único que ve SDL3: ventana, contexto GL, el loader, el reloj real,
la traducción de eventos y el log de SDL. Va en un target propio, `ClientPlatform`, porque
`ClientLib` no enlaza SDL3 (roadmap, "Estructura de `Client/`"). `Client::Core` suma el `IClock`
(la interfaz) y el `Log`, como dice el roadmap; `Client::App` (`Client/src/App/`) tiene el bucle,
el estado de la ventana, la config y la línea de comandos. La config va en `App` y no en `Core`,
como decía el árbol del roadmap: lleva tipos del cliente (el modo del bucle, VSync, el servidor), y
el árbol se corrigió con este plan. El input se parte igual que la plataforma: el estado por
frame (`Client::Input`, `Client/src/Input/`, en `ClientLib`) no conoce SDL, y `Platform` le pasa
los eventos ya traducidos. Todo lo que no es `Platform` va en `ClientLib` y se prueba sin SDL.
`main.cpp` arma todo y no tiene lógica. Como toda N.0, la CL3.0 vive en `labs/`.

**Stack:** C++20 (`std::chrono` con `std::chrono::nanoseconds` como unidad, `steady_clock` en el
lab, interfaces virtuales para inyectar el reloj, `std::bitset` para las teclas, `std::format`
para el log, `std::mutex` en la salida del log), SDL3 (ventana, `SDL_GL_*`, eventos,
`SDL_GetTicksNS`, `SDL_DelayPrecise`, `SDL_SetLogOutputFunction`), GoogleTest. El paso fijo y el
modo fiel son del MU, y SDL3 ya está. Puede entrar una biblioteca, `inih`, para la config
(pregunta 5, `CODING_RULES.md` §10); el log es propio (pregunta 4).

**Guía de estudio:** `docs/learning/ola-cl3.0-el-bucle-de-juego.md`, que se escribe al arrancar la
CL3.0 (L-10). De la CL3.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [02-arranque-y-ciclo-de-vida](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md):
  `WinMain` y el WndProc, el ritmo (lógica a 25 por segundo, dibujo a 21,33), qué corre por paso
  y qué por frame, la línea de comandos y la config.
- [08-pendientes](../../../Client/docs/research/08-pendientes.md): P-C03 (qué depende del paso de
  40 ms) y P-C08 (input), los dos de la CL3.R.
- Los quirks Q-C20 y Q-C22 (relojes por frame, se corrigen en la CL6), y Q-C34 a Q-C37 (arranque y
  línea de comandos, CL3.4) de [07](../../../Client/docs/research/07-quirks-y-bugs.md).
- En `client-research/` del repo de research, el decompilado de `mainexe-vmxp-c32bbd5f`:
  `00637700_FUN_00637700.c/.asm` (el render loop), `00459b60_FUN_00459b60.c` (el delta con QPC) y
  `004eb620_FUN_004eb620.c` (el reloj del frame). La medición de
  [mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
  (`graphics-01/`, los CSV de swaps). El
  [ADR 0001 de MuMain](https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md)
  como referencia de un bucle moderno con el paso del original.

**Lo que dice el binario del bucle** (`0x637700`, *estático*, releído para este plan):

| Qué | Dónde | Qué hace |
| --- | --- | --- |
| el acumulador | `DAT_006d5ee4`, un `int` con signo | `CMP [0x6d5ee4],0x28` en `0x637791` + `JL` en `0x637798`: corre pasos mientras valga 40 o más. En la `.data` vale 40: el primer frame ya corre 1 paso, y nada lo reinicia por su dirección al cambiar de escena (CL3.R) |
| un paso | `0x638167` | `SUB EAX,0x28`: cada paso resta 40 ms; no hay tope de pasos por frame |
| el inicio de la medición | `0x63852b`, `GetTickCount()` | se toma **después** de los pasos, justo antes de dibujar |
| la espera | `0x6385fd`..`0x638624` | `while (GetTickCount() - inicio < 0x28)`: activa, sin dormir. La resta es de `DWORD` y la comparación con signo (`CMP ...,0x28` en `0x63860f` + `JGE` en `0x638616`) |
| lo que suma | `0x638632` | `DAT_006d5ee4 += GetTickCount() - inicio`: solo el dibujo, el swap y la espera |
| el otro delta | `0x459b60`, en `0x637700` antes de los pasos | QPC (o `timeGetTime`), con tope de 200 ms; va a `0x65ee60` y `0x65fc30`, no al acumulador |
| VSync | `0x69e830`, `0x69e860` | `wglSwapIntervalEXT` solo en dos stubs que nadie llama: el original no pide VSync |

El pseudocódigo del 02 ya pone `inicio = GetTickCount()` después de los pasos, pero no dice lo
que implica: **el tiempo de los pasos no se acumula**. Lo nuevo es que **el acumulador no tiene
tope**: un frame de 10 s corre 250 pasos seguidos. Lo que pasa fuera de la medición (los pasos, la
red, el bucle de mensajes, una ventana arrastrada) se pierde de a ticks enteros, y solo cuando
cruza el borde de un tick: con pasos de 10 ms, un frame de 1 paso no pierde nada y uno de 2 pasos
pierde un tick. La CL3.R lo pasa al 02 y la CL3.2 lo prueba en el modo fiel.

**El ritmo exacto del modo fiel.** "7 pasos cada 6 frames" (02, D-C4 del roadmap y su fila "Ritmo
del frame") no es exacto: cada frame suma 46,875 ms de media, así que 64 frames suman 3.000 ms y
**75 pasos cada 64 frames**, con 11 frames de 2 pasos. 6 frames seguidos suman 7 u 8 (los frames
31 a 36 dan 2, 1, 1, 1, 1, 2). Los tests fijan esos números, y la CL3.R corrige los tres lugares.

**Terminada cuando** (roadmap): una ventana a la tasa del monitor con la lógica a tick fijo,
verificado con un reloj falso. En concreto:
- con el reloj falso, 25 pasos por segundo simulado con 1, 25, 60, 144 y 240 frames por segundo, y
  3 pasos seguidos ante un frame de 130 ms;
- en el modo fiel, con un reloj de 64 ticks por segundo, 64 frames cada 3 s y 75 pasos cada 64
  frames, 11 de ellos con 2 pasos (0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2 en los primeros 13);
- `Main` abre la ventana en Linux (Xvfb, en CI) y en Windows (a mano), corre el bucle y sale con
  `--frames N` imprimiendo frames y pasos;
- `Main connect /u<ip> /p<puerto>` y la config eligen el servidor, y un puerto malo o una carpeta
  de datos que no existe terminan con un error que los nombra, sin abrir la ventana; sin config ni
  argumentos, `Main --frames 60` sigue andando como en CI.

## Preguntas para decidir al empezar

1. **Quién escribe la CL3.** *Decidido (Emanuel, 2026-11-02, en la CL2): Claude escribe todo*, la
   CL3.0 en `labs/` y los pasos `TODO(human)` incluidos, hasta que digas que participás. Los pasos
   marcados "(Emanuel)" los hace Claude por ahora. Las preguntas 2 a 7 siguen con su
   recomendación mientras no digas otra cosa.
2. **Tope del frame en el modo normal.** El original no tiene tope (`0x637791`): un frame largo,
   por un breakpoint o un corte del driver, corre todos los pasos que debe. Pero no mide lo que
   pasa fuera del dibujo, así que una ventana arrastrada no le suma nada. MuRet mide el frame
   entero, y sin tope, 10 s en el debugger serían 250 pasos y una espiral si cada paso tarda más
   de 40 ms. El 02 pide un test con **1 frame por segundo y 25 pasos**, así que un tope menor que
   1 s lo rompe. *Recomendación:* `MaxFrameTime = 1 s` (25 pasos como mucho por frame): un frame
   más largo cuenta 1 s, y el resto se pierde con un aviso en el log. Es un cambio contra el
   original solo después de un corte de más de 1 s, y entra en 07 como quirk nuevo (CL3.2).
3. **Dónde vive `Platform`.** *Recomendación:* un target `ClientPlatform` (estático) que enlaza
   `ClientLib` y `SDL3::SDL3`; `Main` enlaza los dos. Los tests de `ClientLib` no enlazan SDL3, y
   lo poco de `Platform` que se prueba sin ventana (el chequeo de los punteros de GL, la versión,
   el intervalo de swap) va a su módulo, `Client/tests/platform/`: un ejecutable por archivo de
   test, como los demás (`muret_add_test`), que enlaza también `ClientPlatform` y corre en los dos
   sistemas porque no abre ventana.
4. **El log: propio o `spdlog`.** `spdlog` está en vcpkg y resuelve el problema (niveles, salidas,
   archivo, y loggers `_st` para un solo hilo), pero trae `fmt`. Lo que se necesita son cuatro
   niveles, una salida a `stderr` y otra a un archivo, una de prueba, y un `mutex`, porque SDL
   puede loguear desde sus hilos. *Recomendación:* propio, de pocas líneas, con `std::format` (GCC
   13 del runner y MSVC 2022 lo tienen), y la razón escrita en el header. Si al llegar a la CL4
   hace falta más (rotación, varios hilos propios), se revisa.
5. **El formato de la config.** El original usa el registro y un `config.ini` (`[LOGIN] Version`,
   02). *Recomendación:* un INI (`muret-client.ini`: secciones, `clave = valor`, `;` de
   comentario) leído con **`inih`** (puerto `inih` de vcpkg, con la feature `cpp` para
   `INIReader`): lee justo eso y da la línea del primer error, así que, por §10, resuelve este
   problema. El primer paso de la CL3.4 confirma el puerto y su comportamiento (comentarios al
   final de la línea, claves repetidas, una línea sin `=`) antes de escribir nada; si algo no
   coincide, queda el parser propio, con la razón escrita.
6. **La línea de comandos** (Q-C36, el 02 deja `/u` sin `/p` para la CL3). El parser sin usar del
   original busca `/u` y `/p` en cualquier lugar, sin mayúsculas, y no necesita el `connect`; con
   `/u` y sin `/p` devuelve 0 con el host ya copiado. *Recomendación:* `connect` es opcional y se
   ignora (lo pasa el launcher); `/u` sin `/p` y `/p` sin `/u` son errores que lo dicen, sin usar
   la config para completar: un `connect` a medias es un error de quien lanza el cliente. El host
   va de 1 a 255 bytes (256 es error, Q-C37). CLI11 y cxxopts se miraron: piensan en `--opción
   valor` o, en CLI11 con opciones al estilo Windows, en `/u valor` y `/u:valor`, no en `/u10.0.0.1`
   pegado y sin mayúsculas; el parser a mano de la CL0.3 sigue, y se confirma con Context7 en el
   paso 1 de la CL3.4.
7. **La ventana en CI de Windows.** El runner de Windows no tiene OpenGL 3.3 (solo el 1.1 de GDI),
   y por eso la ventana de la CL0.3 corre solo en Linux con Xvfb. *Recomendación:* igual: en
   Windows, CI compila todo y corre todos los tests, los de `platform/` incluidos; la ventana se
   prueba a mano y queda escrito en el README. Mesa para Windows (`opengl32.dll` de llvmpipe) se
   puede sumar después si un bug de Windows se escapa.

## Lo que más puede morder

Cada uno tiene su test en la sub-ola que lo dice. En la review se miran primero:

- **Un reloj que vuelve para atrás.** `steady_clock` no puede, pero el reloj falso sí, y un
  `Duration` negativo sumado al acumulador lo deja negativo para siempre. El frame cuenta 0 y el
  siguiente se mide desde el valor nuevo (CL3.2, `AClockThatGoesBackRunsNoSteps`).
- **Un frame enorme después de un breakpoint.** Sin tope, 10 s son 250 pasos de golpe; con el tope
  de la pregunta 2, 25, y un aviso en el log (CL3.2, `AFrameLongerThanTheCapRunsOnlyTheCap`).
- **VSync apagado o un reloj que no avanza.** Sin tope, dos frames pueden leer el mismo instante:
  0 pasos, el alfa igual y ninguna división por cero. En el modo fiel, VSync va apagado como en
  el original: con VSync a 60 Hz, la espera de 46,875 ms se engancharía a la grilla de 16,67 ms
  (CL3.1 y CL3.2).
- **La ventana minimizada o tapada.** En muchos drivers, `SDL_GL_SwapWindow` vuelve enseguida con
  la ventana minimizada y el bucle se come un núcleo. La lógica sigue (el keepalive de la CL4 no se
  puede cortar), el dibujo no, y el bucle duerme hasta el próximo paso (CL3.2,
  `AMinimizedWindowStepsWithoutDrawing`). En el modo fiel la espera de `GetTickCount` sigue aunque
  no se dibuje: sin ella el acumulador no suma y no corre nunca más un paso
  (`FaithfulPacerTest.MinimizedKeepsTheWait`). El original sin foco no corre el frame (CL3.R,
  paso 5): MuRet sigue con sus pasos, y la diferencia queda escrita.
- **Una tecla mantenida entre frames, o apretada y soltada en el mismo frame.** Mantenida, está
  abajo en todos los frames pero "se apretó" una sola vez. Apretada y soltada entre dos lecturas,
  `SDL_GetKeyboardState` no la ve nunca (lo dice su documentación): por eso el estado se arma con
  los eventos. Y un frame con 2 pasos no ve el mismo apretón dos veces, ni uno con 0 pasos lo
  pierde (CL3.3).
- **El modo fiel que mide de más.** Si la medición empieza antes de los pasos y no en `0x63852b`,
  un paso lento frena el juego menos que en el original: con pasos de 10 ms, 73 pasos en 3 s en
  vez de 70 (CL3.2, `TimeSpentInStepsIsNotAccumulated`).

---

## Sub-ola CL3.0: el bucle de juego (N.0, en `labs/`)

**Concepto:**
- el bucle de juego: leer la entrada, avanzar la lógica, dibujar; por qué un paso variable
  (`Update(dt)`) hace que la física y los tiempos dependan de los FPS, y el paso fijo con
  acumulador ("Fix your timestep"): se suma lo que tardó el frame y se corren pasos de 40 ms
  mientras alcance;
- la interpolación: el dibujo ve el estado entre dos pasos, con `alfa = acumulador / paso`, y por
  qué eso no cambia nada observable de la lógica;
- relojes: de pared contra monótono, unidad contra resolución, y por qué `GetTickCount` con un
  tick de 15,625 ms convierte una espera de 40 ms en una de 46,875;
- VSync: el swap que espera el retrazo, y por qué ata los FPS al monitor;
- la espiral de la muerte: si un paso tarda más que su paso, cada frame debe más pasos que el
  anterior, y por qué se pone un tope.

Del C++: `std::chrono::duration` y sus unidades, `duration_cast` (trunca hacia cero), duraciones
enteras contra de punto flotante, `steady_clock::is_steady`, y una interfaz virtual chica para
inyectar el reloj (el reloj falso de los tests).

**Archivos:**
- Crear: `docs/learning/ola-cl3.0-el-bucle-de-juego.md` (la guía; Claude)
- Crear: `labs/cl3.0-bucle/FixedStep.h/.cpp`, `TickGrid.h/.cpp`, sus tests (`test_fixed_step.cpp`
  y `test_tick_grid.cpp`), `README.md` y `jefe/test_original_loop.cpp` (Emanuel)
- Modificar: `labs/CMakeLists.txt` (la biblioteca `MuRetLabsCl30` y el ejecutable `LabCl30Tests`
  con `gtest_discover_tests(... TEST_PREFIX "LabCl30.")`, como `MuRetLabsCl20` y `LabCl20Tests`
  de la CL2.0: GoogleTest ya está en `labs/` desde #28, y el lab es un solo ejecutable con su lista
  a mano, regla 7), `labs/README.md`

- [x] **Paso 1 (Claude):** la guía, justo antes de empezar.
- [x] **Paso 2 (Emanuel, `TODO(human)`; lo resolvió Claude):** `Labs::FixedStep` con milisegundos
  enteros: `int Advance(int frameMs)` devuelve cuántos pasos de 40 ms corren y deja el resto.
  Primero los tests: un frame de 40 da 1 paso y resto 0; uno de 39, 0 pasos y resto 39; uno de 130,
  3 pasos y resto 10; 25 frames de 40 dan 25 pasos, y 1 de 1.000, también 25.
- [x] **Paso 3 (Emanuel; lo resolvió Claude):** `Labs::TickGrid`, el reloj de `GetTickCount`: con
  `tick` en microsegundos (15.625 o 10.000), `Ms(t)` es el valor que leería `GetTickCount` en el
  microsegundo `t` (`(t / tick) * tick / 1000`, entero). Tests: con 15.625, `Ms` en los ticks 0 a 4
  da 0, 15, 31, 46 y 62; con 10.000, 0, 10, 20, 30 y 40.
- [x] **Paso 4 (Emanuel, jefe; lo resolvió Claude):** el bucle de `0x637700` sobre una
  `TickGrid`: el acumulador empieza en 40, como en la `.data` del original (CL3.R), corren pasos
  mientras valga 40 o más, `inicio = Ms(ahora)`, la espera activa de 40 ms avanzando el tiempo de a
  1 µs, y el acumulador suma `Ms(ahora) - inicio`. Tests: con 15.625 µs, 64 frames ocupan 3.000.000
  µs (21,33 FPS) y los pasos de los primeros 13 frames son 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2;
  con 10.000 µs, 25 frames por segundo y 1 paso por frame desde el primero. Si el bucle empezara en
  0, el primer frame daría 0 pasos y el resto, lo mismo.
- [x] **Paso 5 (Emanuel, jefe, la espiral; lo resolvió Claude):** el mismo bucle con pasos que
  tardan 50 ms, corriendo frames mientras el reloj no llegue a 10 s. Como el original no mide los
  pasos, el juego se frena en vez de espiralar: exactamente 99 frames y 115 pasos, no 250
  (empezando en 0 serían 100 y 116). Al lado, `FixedStep` midiendo el frame entero y sin tope: el
  primer frame mide 40 ms y cada uno de los siguientes, lo que tardaron los pasos del anterior; los
  pasos por frame son 1, 1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 8, 10, 12, 15, la espiral.
- [x] **Paso 6 (Emanuel; lo resolvió Claude):** comparar, y anotarlo en `labs/README.md`: el
  `FixedStep` con milisegundos enteros contra uno con `std::chrono::nanoseconds` (qué se pierde al
  truncar, y por qué el original no pierde nada con `int`), y `steady_clock` contra `system_clock`
  (cuál puede ir para atrás y cuándo).
- [x] **Paso 7 (carril R; lo resolvió Claude):** en Ghidra, el `JL` de `0x637798`, el `SUB EAX,0x28`
  de `0x638167` y el `ADD` de `0x638632`: confirmar que el acumulador es con signo y que nada lo
  acota.
- [x] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "LabCl30"
  ```

Hecho así: con el acumulador en 0, como estaban escritos al principio, salieron todos los números de
los pasos 2 a 5, y antes de los tests los dio también un modelo en Python del mismo bucle; el jefe
suma el invariante corregido (desde el frame 2, 75 pasos y 11 frames de 2 cada 64, y 7 u 8 cada 6,
en 300 frames). Un frame negativo cuenta 0 en el `FixedStep` del lab, y la suma va en 64 bits para
que un frame de `INT_MAX` no se pase de rango. Para el paso 6, el lab trae un `ChronoFixedStep` con
`std::chrono::nanoseconds` al lado del de ms: 60 frames que suman 1 s dan 25 pasos con él y 24
truncando cada frame a ms; un frame de más de `INT_MAX` pasos (unos 994 días) cuenta `INT_MAX`, para
que ni la suma ni la cantidad se pasen de rango. El carril R confirmó la tabla de arriba (el `JL` de
`0x637798`, ningún tope entre el `SUB` y el `JMP 0x637791` de `0x6381a5`, y ninguna otra instrucción
de `.text` que nombre `0x6d5ee4`) y trajo algo nuevo: en el `.data` del archivo, `0x6d5ee4` vale
`0x28`, así que **el acumulador arranca en 40** y el primer frame de `0x637700` corre 1 paso, no 0.
Después de las revisiones, el jefe arranca en 40, como el original, y los pasos 4 y 5 dicen sus
números; `StartingAtZeroOnlyChangesTheFirstFrame` muestra que arrancando en 0 solo cambia el primer
frame (73 pasos en 64 frames en vez de 74). Lo que cambia en 40, para la CL3.R y el PR B de la CL3.2
(los dos simuladores de las revisiones dieron lo mismo):

- el primer frame corre 1 paso y no 0: los primeros 13 de `SeventyFiveStepsEverySixtyFourFrames` y
  de "Terminada cuando" empiezan en 1;
- 64 frames corren 74 pasos y no 73, con los mismos 80 ms en el acumulador
  (`SixtyFourFramesEveryThreeSeconds` y los "73 corridos" de `MinimizedKeepsTheWait`);
- con pasos de 10 ms, `TimeSpentInStepsIsNotAccumulated` da 61 frames y 71 pasos, no 70, y
  midiendo desde antes de los pasos, 64 y 74, no 73;
- la espiral del paso 5, 99 frames y 115 pasos, con el reloj en 10,03125 s y no en 10,125;
- con el tick de 10 ms, 1 paso por frame desde el primero y no desde el segundo;
- en `ADrawLongerThanTheWaitAddsItWhole` y `HasNoCap` el primer frame corre 1 paso antes de
  dibujar; los 3 y los 250 pasos del frame siguiente no cambian.

No cambian los 3 s de 64 frames, las ventanas desde el frame 2 (75 y 11 cada 64, 7 u 8 cada 6), ni
lo que mide cada frame (46 una vez y 47 siete veces cada 8). El jefe suma también un dibujo que
tarda (uno de 10 s da 250 pasos en el frame siguiente: no hay tope), un reloj de 1 ms en el que
cada frame mide 40 exactos, y una hora de inicio, para cruzar la vuelta del `DWORD` a los 2^32 ms:
64 frames que la cruzan siguen tardando 3 s, con los mismos pasos. Lo que se vio está en el README
del lab, actividades 12 y 13.

## Sub-ola CL3.R: qué depende del paso de 40 ms (P-C03)

**Concepto:** carril R antes de desacoplar. El dibujo interpolado solo es fiel si todo lo que la
lógica hace por paso sigue por paso, y lo que el original hace por frame se decide uno por uno
(Q-C20 ya dice que la mascota por frame es un bug).

**Archivos:** modificar `Client/docs/research/02-arranque-y-ciclo-de-vida.md` (lo nuevo del bucle,
de la tabla de arriba, y la tabla de lo que corre por paso con lo que se lea), `08-pendientes.md`
(P-C03 y la parte de P-C08 que cierre), `07-quirks-y-bugs.md` (un quirk nuevo por el acumulador
sin tope, si la CL3.2 lo cambia, y lo que aparezca), el roadmap (la fila D-C4 y la fila "Ritmo del
frame" de "Cómo el research se vuelve test") y este plan.

- [ ] **Paso 1:** pasar al 02 lo que dice la tabla "Lo que dice el binario del bucle": acumulador
  con signo y sin tope, la medición desde `0x63852b` (los pasos no se miden, y lo de fuera se
  pierde de a ticks enteros), y el delta de `0x459b60` con tope de 200 ms que va a
  `0x65ee60`/`0x65fc30` y no al acumulador. Corregir "7 pasos cada 6 frames" en el 02, en la fila
  D-C4 del roadmap y en su fila "Ritmo del frame": 75 pasos cada 64 frames, con 11 de 2 pasos, y
  7 u 8 cada 6.
- [ ] **Paso 2:** leer `0x636290` (la lógica de la escena 5) y `0x623e70` (40 KB, solo en el
  juego): para cada cosa que avanza, si avanza por paso, por frame o por reloj (`timeGetTime`,
  `GetTickCount`). Una tabla en el 02: qué, dónde, por paso o por frame, y qué hace MuRet.
- [ ] **Paso 3:** quién usa `0x65ee60` y `0x65fc30` (el objeto del delta con tope): si algo de la
  lógica avanza por tiempo real y no por paso, el modo normal lo tiene que medir igual.
- [ ] **Paso 4:** los usos de `DAT_05767940` y `DAT_077f3b74` (las cuentas regresivas) y de
  `DAT_07d2d360` (el ciclo de 32 pasos): qué los lee, y si el que los lee dibuja.
- [ ] **Paso 5 (P-C08):** las llamadas a `GetAsyncKeyState`: si se leen en el frame o en el paso,
  si alguna usa el bit bajo ("se apretó desde la última llamada") o solo el alto ("está abajo"),
  y qué teclas lee cada escena. El resto del WndProc (`0x4af4e0`): qué hace con el mouse.
- [ ] **Paso 6 (el foco):** en el bucle de `WinMain`, `0x4be019`..`0x4be04a` lee
  `DAT_05763580` (el foco, de `WM_ACTIVATE`) y llama al frame (`0x63945d`) solo si no es cero; si
  es cero salta a `0x4be057`: `SetForegroundWindow` y `SetFocus` y, en una rama con los contadores
  del hash, `SetTimer(0x3e9, 1000)` y `PostMessageA(..., 0x10, ...)`, un `WM_CLOSE`
  (`0x4be1f2`..`0x4be217`). También consulta `IsIconic`. *Estático*, visto para este plan: el
  original sin foco probablemente no corre ningún frame, ni pasos ni dibujo. Leer las dos ramas,
  pasarlo al 02 con sus direcciones, y anotar en 07 la diferencia: MuRet sigue con sus pasos sin
  foco o minimizado (el keepalive de la CL4 no puede parar).
- [ ] **Paso 7:** cerrar P-C03 en el 08 (o dejar escrito qué queda para la CL6 y la CL7, que
  dibujan lo que se mueve), y actualizar este plan con lo que cambie.

## Sub-ola CL3.1: ventana y contexto GL con SDL3

**Concepto:** RAII sobre una API de C (SDL3), una capa que encierra la plataforma, y el contexto
de OpenGL como un recurso que vive dentro de la ventana.

**Contrato** (02 § De `entry` al bucle de mensajes, § El WndProc):

| Qué | Original | MuRet |
| --- | --- | --- |
| ventana | `0x4b8ff0`: `RegisterClassA("MU")` + `CreateWindowExA`, a la resolución del registro | `Platform::Window`, 640 × 480 por defecto (D-C11), redimensionable, título `MuRet` |
| contexto | `0x4b8e10`: `ChoosePixelFormat` + `wglCreateContext`, OpenGL 1.x | OpenGL 3.3 core con doble buffer (D-C3), pedido antes de crear la ventana |
| VSync | no lo pide (`0x69e830` y `0x69e860` no se llaman) | `VSyncMode::On` por defecto, `Adaptive` y `Off` por config; en el modo fiel, `Off` |
| cerrar | `WM_DESTROY`: cierra todo y `PostQuitMessage(0)` | `SDL_EVENT_QUIT` o `SDL_EVENT_WINDOW_CLOSE_REQUESTED`: sale el bucle |
| foco | `WM_ACTIVATE` guarda el foco en `DAT_05763580`, y sin foco `WinMain` no llama al frame (`0x4be041`; CL3.R, paso 6) | `SDL_EVENT_WINDOW_FOCUS_LOST`/`_GAINED` al `WindowState`; sin foco, el bucle sigue |
| minimizar | sin foco no corre el frame; también consulta `IsIconic` (CL3.R, paso 6) | `SDL_EVENT_WINDOW_MINIMIZED`/`_RESTORED`, y `SDL_EVENT_WINDOW_OCCLUDED`/`_EXPOSED` igual: no se dibuja, la lógica sigue (CL3.2) |
| tamaño | resolución fija | `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`: el viewport sigue a la ventana |
| cursor | `WM_SETCURSOR` lo esconde: lo dibuja el juego | queda el del sistema hasta que el juego lo dibuje (CL5) |

**Archivos:** crear `Client/src/Platform/SdlContext.h/.cpp` (`SDL_Init`/`SDL_Quit` con RAII, lo
que hoy es `SdlVideo`), `Window.h/.cpp` (ventana y contexto, hoy `CreateGlWindow`),
`GlLoader.h/.cpp` (la tabla de punteros de la CL0.3, sin cambiar su forma, D-C2),
`SwapInterval.h/.cpp` y `WindowEvents.h/.cpp` (de `SDL_Event` a `App::WindowEvent`); crear
`Client/src/App/WindowState.h/.cpp` y `VSyncMode.h`; `Client/tests/app/test_window_state.cpp`, y
`Client/tests/platform/test_gl_loader.cpp` y `test_swap_interval.cpp` con su `CMakeLists.txt` (el
módulo nuevo, ver el paso 4). Modificar `Client/CMakeLists.txt` (`ClientPlatform`, y sacar
`src/Platform/` del glob de `ClientLib`), `Client/tests/CMakeLists.txt` (el
`add_subdirectory(platform)`), `Client/src/main.cpp` (queda el armado), `Client/README.md` y este
plan. El árbol del roadmap ya
tiene `Input/`, `ClientPlatform` y la config en `App/` (se corrigió con este plan).

**Interfaces:**

```cpp
namespace Client::App
{
enum class WindowEvent : std::uint8_t
{
    CloseRequested, Minimized, Restored, Occluded, Exposed, FocusLost, FocusGained, PixelSizeChanged
};

struct PixelSize { int width = 0; int height = 0; };

struct WindowState
{
    bool running = true;
    bool minimized = false; // también tapada: no se dibuja
    bool focused = true;
    PixelSize pixelSize;
};

// Pura: lo que cambia cada evento. `size` solo cuenta con PixelSizeChanged.
WindowState Apply(WindowState state, WindowEvent event, PixelSize size = {});

// En App y no en Platform: la config (CL3.4) lo lee sin depender de SDL.
enum class VSyncMode : std::uint8_t { On, Adaptive, Off };
} // namespace Client::App

namespace Client::Platform
{
// El intervalo que se le pasa a SDL_GL_SetSwapInterval: 1, -1 o 0.
int SwapIntervalFor(App::VSyncMode vsync);
// Pide el intervalo; si Adaptive falla, prueba con On (lo recomienda la documentación de SDL3).
// Devuelve el que quedó.
App::VSyncMode ApplySwapInterval(App::VSyncMode wanted);

// Lo que hoy es IsValidProcAddress de main.cpp, sobre el entero: algunos drivers de Windows dan
// 1, 2, 3 o -1 por una función que no tienen.
bool IsValidProcAddress(std::uintptr_t address);
bool HasRequiredGlVersion(int major, int minor); // 3.3 o más

// SDL_Init(SDL_INIT_VIDEO) en Create y SDL_Quit en el destructor; no se copia ni se mueve.
class SdlContext
{
public:
    static Core::Expected<std::unique_ptr<SdlContext>, std::string> Create();
    ~SdlContext();
};

struct WindowOptions
{
    int width = 640;
    int height = 480;
    std::string title = "MuRet";
    bool resizable = true;
    App::VSyncMode vsync = App::VSyncMode::On;
};

class Window
{
public:
    // Necesita un SdlContext vivo. Crea la ventana, el contexto 3.3 core, carga GL y pide VSync.
    static Core::Expected<Window, std::string> Create(const SdlContext& sdl, const WindowOptions& options);
    void Swap();
    App::PixelSize PixelSize() const;
    App::VSyncMode VSync() const; // el que quedó después de ApplySwapInterval
};
// Vacía la cola de SDL: cada evento de ventana va a `state` (desde la CL3.3 también recibe el
// InputState).
void PumpEvents(App::WindowState& state);
} // namespace Client::Platform
```

`Window` guarda la ventana y el contexto con `std::unique_ptr` y sus deleters, en ese orden (el
contexto se destruye primero), como la CL0.3.

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests, la tabla de `Apply`: para cada
  evento, qué campo cambia, y qué pasa con un `Restored` sin `Minimized` antes (nada: queda
  igual) y con un `PixelSizeChanged` de 0 × 0 (en Windows llega al minimizar: no se toca el
  tamaño).
- [ ] **Paso 2:** los tests: `WindowStateTest.CloseRequestedStopsRunning`,
  `MinimizedAndRestored`, `OccludedIsLikeMinimized`, `FocusLostKeepsRunning` (solo cambia
  `focused`), `PixelSizeChangedUpdatesTheSize`, `AZeroPixelSizeIsIgnored`;
  `GlLoaderTest.RejectsTheInvalidAddresses` (0, 1, 2, 3 y `UINTPTR_MAX` no; 4 sí),
  `GlLoaderTest.RequiresThreeThree` (3.3, 3.4, 4.5 sí; 3.2 y 2.1 no);
  `SwapIntervalTest.MapsEachMode` (`On` 1, `Adaptive` -1, `Off` 0).
- [ ] **Paso 3:** `WindowState`, y `Platform` con lo que ya hace `main.cpp`. `main.cpp` queda en el
  armado: `SdlContext`, `Window` y el bucle de la CL0.3, que la CL3.2 reemplaza.
- [ ] **Paso 4:** CMake. El `file(GLOB_RECURSE client_sources ... "src/*.cpp")` de `ClientLib`
  juntaría también `src/Platform/*.cpp`, y `ClientLib` no puede enlazar SDL3: después del filtro
  de `main.cpp` va `list(FILTER client_sources EXCLUDE REGEX "/src/Platform/")`, como los
  `list(FILTER MU_CLIENT_SOURCES EXCLUDE REGEX ...)` de `src/CMakeLists.txt` en MuMain.
  `ClientPlatform` junta `src/Platform/*.cpp` con su propio `file(GLOB ... CONFIGURE_DEPENDS)`,
  enlaza `ClientLib` y `SDL3::SDL3` y lleva `SDL_OPENGL_1_FUNCTION_TYPEDEFS` como `PUBLIC` (sale
  de `Main`; `GlLoader.h` expone los tipos `PFNGL…PROC`). `muret_add_module_tests()` no pasa
  `LINK_LIBS`, así que `Client/tests/platform/CMakeLists.txt` llama a mano, uno por archivo,
  `muret_add_test(NAME test_gl_loader SOURCES test_gl_loader.cpp LINK_LIBS ClientPlatform)`, y
  lo mismo con `test_swap_interval` (y con `test_input_events` en la CL3.3). Corren sin ventana:
  `muret_add_test` registra cada `TEST` con `gtest_discover_tests`. El paso de Xvfb del CI no
  cambia en esta sub-ola; la CL3.2 le suma los modos.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "WindowState|GlLoader|SwapInterval|Client.Main"
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60
  ```

## Sub-ola CL3.2: bucle con tick fijo y render desacoplado, con reloj falso

**Concepto:** el paso fijo con acumulador e interpolación, con el reloj inyectado: el mismo
`GameLoop` corre con el reloj de SDL en `Main` y con uno falso en los tests, donde un frame
"tarda" lo que el test dice. Va en dos PRs: **PR A**, el modo normal (paso fijo, interpolación,
reloj falso, `SdlClock`); **PR B**, el modo fiel.

**Contrato** (02 § El ritmo, y la tabla "Lo que dice el binario del bucle"):

| Qué | Original | MuRet, modo normal (`Decoupled`) | MuRet, modo fiel (`Faithful`) |
| --- | --- | --- | --- |
| paso | 40 ms (`0x28`, `0x637791` y `0x638167`) | 40 ms | 40 ms |
| qué se acumula | `GetTickCount` del dibujo, el swap y la espera (`0x63852b`..`0x638632`) | el frame entero, de un `Now()` al siguiente | lo mismo que el original, con un reloj de 64 ticks por segundo |
| espera | activa hasta 40 ms de `GetTickCount` (`0x6385fd`..`0x638624`) | ninguna: VSync o sin tope | duerme hasta el tick en que se cumplen los 40 ms |
| tope | ninguno | `MaxFrameTime` (pregunta 2); un frame negativo cuenta 0 | ninguno, como el original; las reglas de `FixedStep` no se aplican |
| dibujo | el estado después de los pasos | interpolado con `alfa = acumulador / paso`, menor que 1 | `alfa = 0`, el estado después de los pasos |
| minimizado | sin foco no corre el frame (CL3.R, paso 6) | pasos sí, dibujo no; duerme hasta el próximo paso | pasos sí, dibujo no, y la espera de `GetTickCount` sigue |

**Archivos:**
- **PR A:** crear `Client/src/Core/IClock.h` (la interfaz), `Client/src/App/GameLoop.h/.cpp`,
  `FixedStep.h/.cpp` y `LoopTiming.h` (las constantes); `Client/src/Platform/SdlClock.h/.cpp`;
  `Client/tests/support/FakeClock.h/.cpp` y `RecordingClient.h/.cpp`, con su
  `Client/tests/support/test_fake_clock.cpp`, y `Client/tests/app/test_fixed_step.cpp` y
  `test_game_loop.cpp`. Los tests entran solos: cada módulo junta sus `test_*.cpp`, y los `.cpp` de
  `support/` que no son tests van a `ClientTestSupport`. Modificar `main.cpp` (una escena de prueba:
  el fondo cambia de color con el ciclo de 32 pasos, como `DAT_07d2d360`, interpolado),
  `App/CommandLine.h/.cpp` y su test (`--pacing`, `--vsync` y `--stats`; la CL3.4 los suma a la
  config), `.github/workflows/ci.yml` (el paso de Xvfb corre también `--vsync off`), 07 (el quirk
  nuevo de la pregunta 2), PROGRESS y este plan.
- **PR B:** crear `Client/src/App/FaithfulPacer.h/.cpp` y
  `Client/tests/app/test_faithful_pacer.cpp`; modificar `GameLoop`, `ci.yml` (`--pacing faithful` en
  Xvfb), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Core
{
using Duration = std::chrono::nanoseconds;

// El reloj del bucle. Now() es monótono en el real; el falso puede ir para atrás a propósito.
class IClock
{
public:
    virtual ~IClock() = default;
    virtual Duration Now() = 0;
    virtual void SleepFor(Duration duration) = 0;
};
} // namespace Client::Core

namespace Client::App
{
// 02 § El ritmo: CMP [0x6d5ee4],0x28 en 0x637791, SUB EAX,0x28 en 0x638167.
inline constexpr Core::Duration LogicStep = std::chrono::milliseconds{40};
// Pregunta 2: un frame más largo cuenta como este; el resto se pierde con un aviso.
inline constexpr Core::Duration MaxFrameTime = std::chrono::seconds{1};
// El tick de GetTickCount en la VM XP: 15,625 ms, 64 por segundo (02 § El ritmo).
inline constexpr Core::Duration FaithfulTick = std::chrono::microseconds{15'625};
// La espera de 0x6385fd..0x638624 (CMP ...,0x28 + JGE en 0x63860f), en los ms de GetTickCount.
inline constexpr std::int32_t FaithfulWaitMs = 40;

// Lo que el bucle llama: la lógica, un paso por vez, y el dibujo. En la CL3.3, Step recibe el
// InputState (ver ahí).
class ILoopClient
{
public:
    virtual ~ILoopClient() = default;
    virtual void Step() = 0;
    virtual void Render(float alpha) = 0; // alpha en [0, 1)
};

class FixedStep
{
public:
    // Suma el frame (negativo cuenta 0; más que MaxFrameTime cuenta MaxFrameTime; MaxFrameTime
    // justo no se recorta) y devuelve cuántos pasos corren.
    int Advance(Core::Duration frameTime);
    // acumulado / paso, recortado a std::nextafter(1.0f, 0.0f): con 39.999.999 ns el float
    // redondearía a 1.
    float Alpha() const;
    Core::Duration Accumulated() const;
    bool LastFrameWasClamped() const;
};

// Los ms que leería GetTickCount en `now`, de a FaithfulTick: DWORD, como el original.
std::uint32_t TickCountMs(Core::Duration now);

// El ritmo del original, sin las reglas de FixedStep: sin tope y en ms enteros.
class FaithfulPacer
{
public:
    // Los pasos que tocan (mientras el acumulador valga 40 o más), como 0x637791.
    int BeginFrame();
    // Toma el inicio (0x63852b). Se llama después de los pasos y antes de dibujar.
    void StartMeasure(Core::IClock& clock);
    // Duerme de tick en tick mientras static_cast<std::int32_t>(TickCountMs(now) - inicio) sea
    // menor que 40, y suma esa diferencia al acumulador (0x638632).
    void EndFrame(Core::IClock& clock);
    std::int32_t AccumulatedMs() const;
    std::int32_t LastMeasuredMs() const;
};

enum class PacingMode : std::uint8_t { Decoupled, Faithful };

struct FrameStats
{
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    std::uint64_t clampedFrames = 0;   // solo Decoupled
    Core::Duration accumulated{};      // Decoupled: FixedStep::Accumulated(); Faithful: AccumulatedMs()
    std::int32_t lastMeasuredMs = 0;   // solo Faithful: lo que sumó el último frame
};

class GameLoop
{
public:
    GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode);
    // Un frame: los pasos que tocan y el dibujo si `state.minimized` es falso. En Faithful, la
    // espera corre siempre, también minimizada. Devuelve cuántos pasos corrió.
    int RunFrame(const WindowState& state);
    const FrameStats& Stats() const;
    float LastAlpha() const;
};
} // namespace Client::App
```

El reloj falso y el cliente de prueba (`Client/tests/support/`):

```cpp
namespace Client::Tests
{
class FakeClock final : public Core::IClock
{
public:
    Core::Duration Now() override;            // el tiempo actual, sin moverlo
    void SleepFor(Core::Duration d) override; // lo mueve `d` y lo anota
    void Advance(Core::Duration d);           // lo mueve (d puede ser negativo)
    std::span<const Core::Duration> Sleeps() const;
};

// Cuenta pasos y dibujos, guarda cada alfa, y hace "tardar" a cada paso y a cada dibujo lo que
// se le diga moviendo el FakeClock: así un test arma un frame de 130 ms o un breakpoint de 10 s.
class RecordingClient final : public App::ILoopClient
{
public:
    explicit RecordingClient(FakeClock& clock);
    void Step() override;
    void Render(float alpha) override;
    Core::Duration stepCost{};
    Core::Duration renderCost{};
    std::uint64_t steps = 0;
    std::vector<float> alphas; // uno por dibujo
};
} // namespace Client::Tests
```

En `Decoupled`, el frame se mide de un `Now()` al siguiente, al empezar `RunFrame`. Para frames que
no dan nanosegundos enteros (240 por segundo), los tests avanzan `(i + 1) * 1 s / n - i * 1 s / n`
en el frame `i`, así que n frames suman 1 s exacto. En `Faithful`, `RunFrame` llama
`BeginFrame`, corre esos pasos, `StartMeasure`, dibuja si no está minimizada y `EndFrame`.
`SdlClock` usa `SDL_GetTicksNS` y `SDL_DelayPrecise`, que espera activamente el último tramo: con
`SDL_DelayNS` o `std::this_thread::sleep_for`, un despertar tarde puede caer en el cuarto tick y
dar un frame de 62 ms. Los alfas se comparan con `EXPECT_NEAR` (1e-6).

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests del paso 2, a mano: los pasos por
  frame del modo fiel en los primeros 7 frames, con el reloj de 64 ticks y los ms que mide cada
  uno (`TickCountMs` en los ticks 0, 3, 6 y 9 da 0, 46, 93 y 140). El paso 2 trae el resultado.
- [ ] **Paso 2 (PR A):** los tests:
  - `FakeClockTest`: `Now()` no se mueve solo; `SleepFor(5 ms)` lo mueve 5 ms y lo anota;
    `Advance(-3 ms)` lo lleva para atrás;
  - `FixedStepTest.FortyIsOneStep` (40 ms: 1 paso, alfa 0), `ThirtyNineIsNone` (0 pasos, alfa
    0,975), `OneHundredThirtyIsThree` (3 pasos y alfa 0,25: el alcanzar del 02),
    `AlphaIsTheFractionLeft` (50 ms: 1 paso, alfa 0,25), `AlphaStaysBelowOne` (39.999.999 ns: 0
    pasos y alfa menor que 1), `ANegativeFrameCountsZero`, `TheCapIsOneSecond` (1 s: 25 pasos y
    no recortado; 1 s + 1 ns: 25 pasos y `LastFrameWasClamped()`; 10 s: 25 pasos y recortado);
  - `GameLoopTest.TwentyFiveStepsPerSecondAtAnyFrameRate`, parametrizado con 1, 25, 60, 144 y
    240 frames por segundo: en 4 s simulados, 100 pasos (el primer frame solo fija el inicio);
  - `GameLoopTest.ASlowFrameCatchesUpWithThreeSteps`: un dibujo de 130 ms, y el frame siguiente
    corre 3 pasos con alfa 0,25;
  - `GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap`: un dibujo de 10 s (el breakpoint) da 25
    pasos y `clampedFrames == 1`, y el siguiente frame normal vuelve a 1 paso cada 40 ms;
  - `GameLoopTest.AClockThatGoesBackRunsNoSteps`: con el acumulador en 0, el reloj vuelve 5 ms: 0
    pasos, `accumulated` sigue en 0 y el alfa en 0; el siguiente frame, 40 ms después, da 1 paso;
  - `GameLoopTest.AClockThatDoesNotMoveRunsNoSteps`: dos frames en el mismo instante (VSync
    apagado), 0 pasos, el mismo alfa y ningún NaN;
  - `GameLoopTest.AMinimizedWindowStepsWithoutDrawing`: minimizada desde t = 0, 26 llamadas a
    `RunFrame` dan 25 pasos y 0 dibujos, y `Sleeps()` suma 1,04 s (cada frame duerme lo que falta
    para el próximo paso): el bucle durmió en vez de girar;
  - `GameLoopTest.AlphaGrowsBetweenSteps`: con 240 FPS, el alfa sube de frame en frame dentro de un
    paso, siempre en [0, 1), y vuelve a bajar después de cada paso;
  - `CommandLineTest.ParsesPacing` (`decoupled`, `faithful`; otro valor es error),
    `ParsesVSync` (`on`, `adaptive`, `off`), `ParsesStats`, y
    `FaithfulWithVSyncOnIsAnError` (`--pacing faithful --vsync on`: el mensaje nombra las dos
    opciones; en el modo fiel VSync va apagado: con `--vsync off` o sin `--vsync` se acepta).
- [ ] **Paso 3 (PR A):** `FixedStep`, `GameLoop` en `Decoupled` y `SdlClock`. `main.cpp` arma el
  bucle con la escena de prueba e imprime `frames` y `pasos` al salir; con `--stats`, los FPS y
  los pasos por segundo cada 5 s.
- [ ] **Paso 4 (PR A):** 07: el acumulador sin tope del original contra `MaxFrameTime`, con su
  test (`GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap`; el del original llega en el PR B).
- [ ] **Paso 5 (PR B):** los tests del modo fiel, cada uno con el `FakeClock` en t = 0 y dibujos
  de 0 ms salvo que diga otra cosa:
  - `FaithfulPacerTest.TickCountMsFollowsTheGrid`: 0, 15, 31, 46 y 62 en los ticks 0 a 4;
  - `FaithfulPacerTest.SixtyFourFramesEveryThreeSeconds`: 64 frames terminan con el reloj en 3 s
    exactos (192 ticks), y el frame 65 empieza con 2 pasos: 73 corridos y 80 ms en el acumulador,
    los 75 de 3 s a 25 por segundo;
  - `FaithfulPacerTest.SeventyFiveStepsEverySixtyFourFrames`: los pasos de los primeros 13 frames
    son 0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2; desde el frame 2, cada 64 frames seguidos suman 75,
    con 11 de 2 pasos, y cada 6 seguidos suman 7 u 8 (los frames 31 a 36 dan 2, 1, 1, 1, 1, 2);
    mirado en los primeros 300 frames;
  - `FaithfulPacerTest.EachFrameMeasuresThreeTicks`: cada frame suma 46 o 47 ms
    (`LastMeasuredMs`);
  - `FaithfulPacerTest.ADrawLongerThanTheWaitAddsItWhole`: con el reloj en el tick 0, un dibujo de
    130 ms suma 125 ms (8 ticks) sin esperar, y el frame siguiente corre 3 pasos;
  - `FaithfulPacerTest.TimeSpentInStepsIsNotAccumulated`: con pasos de 10 ms, llamando a
    `RunFrame` mientras `Now()` sea menor que 3 s, corren 61 frames y 70 pasos, y el reloj termina
    en 3,015625 s. Midiendo desde antes de los pasos serían 64 frames y 73: el test dice "70 y no
    73";
  - `FaithfulPacerTest.HasNoCap`: un dibujo de 10 s suma 10.000 ms y el frame siguiente corre 250
    pasos;
  - `FaithfulPacerTest.MinimizedKeepsTheWait`: minimizada, 64 frames siguen tardando 3 s y el
    frame 65 empieza con 2 pasos (73 corridos), sin dibujos.
- [ ] **Paso 6 (PR B):** `TickCountMs`, `FaithfulPacer` y el modo `Faithful` de `GameLoop`; en 07,
  `FaithfulPacerTest.HasNoCap` al lado del test del PR A.
- [ ] **Paso 7:** a mano, en Windows y en Linux: `Main --pacing faithful --stats` da 21,33 FPS y 25
  pasos por segundo, y `Main --vsync off --stats` corre a cientos de FPS con 25 pasos por segundo.
  Anotarlo en el README.
- [ ] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "FakeClock|FixedStep|GameLoop|FaithfulPacer|CommandLine"
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60 --vsync off
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60 --pacing faithful
  ```

## Sub-ola CL3.3: input (teclado por frame, mouse)

**Concepto:** estado contra eventos: "está abajo" se consulta, "se apretó" es un flanco, y un
flanco que se lee por frame tiene que llegar a exactamente un paso de la lógica.

**Contrato** (02 § El WndProc; lo que diga la CL3.R, paso 5):

| Qué | Original | MuRet |
| --- | --- | --- |
| teclado | `GetAsyncKeyState` en cada frame, no `WM_KEYDOWN` | estado por frame armado con `SDL_EVENT_KEY_DOWN`/`_UP` por scancode; el auto-repeat (`event.key.repeat`) no es un apretón |
| apretar y soltar en un frame | se pierde, salvo que se use el bit bajo de `GetAsyncKeyState` (CL3.R) | se ve: `WasPressed` y no `IsDown` |
| un frame con 2 pasos | sin leer (CL3.R, paso 5) | el apretón lo ve solo el primer paso |
| un frame con 0 pasos | no pasa: cada frame mide 40 ms o más | el apretón espera al próximo paso |
| foco perdido | `WM_ACTIVATE`, y sin foco no corre el frame (CL3.R, paso 6) | se sueltan todas las teclas y botones |
| mouse | `WM_RBUTTON*`, `WM_MBUTTON*`, `WM_MOUSEWHEEL`: cámara y zoom | botones izquierdo, medio y derecho como las teclas; la rueda se suma hasta el paso; la posición en coordenadas de la ventana, que son píxeles mientras la ventana no pida `SDL_WINDOW_HIGH_PIXEL_DENSITY` (la de 640 × 480 es de la CL5, D-C11) |

**Archivos:** crear `Client/src/Input/Key.h` (los scancodes que usa el cliente, con nombre, sin
SDL), `InputState.h/.cpp` y `InputEvent.h`; `Client/src/Platform/InputEvents.h/.cpp` (de `SDL_Event`
a `Input::InputEvent`); `Client/tests/input/test_input_state.cpp`, con el `CMakeLists.txt` del
módulo nuevo (`muret_add_module_tests()`), y `Client/tests/platform/test_input_events.cpp` (la tabla
de scancodes, sin ventana, con su `muret_add_test` en `platform/CMakeLists.txt`). Modificar
`Client/tests/CMakeLists.txt` (el `add_subdirectory(input)`), `GameLoop` e `ILoopClient` (las firmas
de abajo), `Platform::PumpEvents`, `RecordingClient`, `main.cpp`, PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Input
{
enum class Key : std::uint16_t { /* los que use el cliente; Count al final */ };
enum class MouseButton : std::uint8_t { Left, Middle, Right };

struct InputEvent
{
    enum class Kind : std::uint8_t { KeyDown, KeyUp, ButtonDown, ButtonUp, Wheel, Motion, FocusLost };
    Kind kind;
    Key key{};
    MouseButton button{};
    float wheel = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
};

class InputState
{
public:
    void Apply(const InputEvent& event);
    bool IsDown(Key key) const;
    bool WasPressed(Key key) const;  // desde el último ConsumeEdges
    bool WasReleased(Key key) const;
    bool IsDown(MouseButton button) const;
    bool WasPressed(MouseButton button) const;
    bool WasReleased(MouseButton button) const;
    float Wheel() const;             // lo sumado desde el último ConsumeEdges
    float MouseX() const;            // coordenadas de la ventana
    float MouseY() const;
    // Lo llama GameLoop después de un paso: borra los flancos y la rueda, no lo que está abajo.
    void ConsumeEdges();
};
} // namespace Client::Input

namespace Client::App
{
// Cambian las firmas de la CL3.2.
class ILoopClient
{
public:
    virtual ~ILoopClient() = default;
    virtual void Step(const Input::InputState& input) = 0;
    virtual void Render(float alpha) = 0;
};
// GameLoop::RunFrame recibe el input y le llama ConsumeEdges después del primer paso del frame;
// en un frame sin pasos no lo toca.
int GameLoop::RunFrame(const WindowState& state, Input::InputState& input);
} // namespace Client::App

namespace Client::Platform
{
// Lo que usa el cliente; un scancode sin usar da std::nullopt.
std::optional<Input::Key> KeyFor(SDL_Scancode scancode);
// De SDL_Event a InputEvent; un evento que no es de input, o un auto-repeat, da std::nullopt.
std::optional<Input::InputEvent> InputEventFor(const SDL_Event& event);
// Reemplaza la de la CL3.1: los eventos de ventana van a `state` y los de input a `input`.
void PumpEvents(App::WindowState& state, Input::InputState& input);
} // namespace Client::Platform
```

`RecordingClient` pasa a `Step(const Input::InputState&)` y guarda, por paso, si vio el apretón
que el test le pregunta (`std::vector<bool> sawPress` y la tecla en `watchedKey`).

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests, la tabla de flancos: para cada
  secuencia (abajo; abajo y arriba; abajo, arriba y abajo; abajo con auto-repeat; foco perdido con
  una tecla abajo), qué dan `IsDown`, `WasPressed` y `WasReleased` antes y después de
  `ConsumeEdges`.
- [ ] **Paso 2:** los tests:
  - `InputStateTest.AHeldKeyIsPressedOnce`: abajo en el frame 1, sigue abajo en el 2 y el 3:
    `IsDown` en los tres, `WasPressed` solo antes del primer `ConsumeEdges`;
  - `InputStateTest.APressAndReleaseInOneFrameIsSeen`: abajo y arriba sin `ConsumeEdges` en el
    medio: `WasPressed` y `WasReleased`, no `IsDown`;
  - `InputStateTest.ASecondKeyDownIsNotAnEdge` (el auto-repeat ya lo filtra `Platform`; acá, un
    segundo `KeyDown` con la tecla abajo no es flanco);
  - `InputStateTest.FocusLostReleasesEverything`: con una tecla y un botón abajo, `FocusLost` los
    suelta y da `WasReleased` para los dos;
  - `InputStateTest.TheWheelAddsUntilConsumed`: +1 y +2 dan 3, y 0 después de `ConsumeEdges`;
  - `InputStateTest.MouseButtonsAreEdgesToo`: `WasPressed` y `WasReleased` de un botón;
  - `GameLoopTest.APressIsSeenByExactlyOneStep`: un frame de 80 ms (2 pasos) con un apretón: el
    primer paso lo ve, el segundo no;
  - `GameLoopTest.APressInAFrameWithoutStepsWaitsForTheNextStep`: a 240 FPS, un apretón en un
    frame de 0 pasos lo ve el primer paso que venga, aunque la tecla ya se haya soltado;
  - `InputEventsTest.MapsTheScancodes`: `SDL_SCANCODE_A` da `Key::A`, `SDL_SCANCODE_RETURN`
    `Key::Enter`, uno sin usar da `std::nullopt`; `InputEventsTest.AutoRepeatIsNotAnEvent`: un
    `SDL_EVENT_KEY_DOWN` con `repeat` da `std::nullopt` (el `SDL_Event` se arma a mano, sin
    ventana).
- [ ] **Paso 3:** `InputState`, la traducción en `Platform`, y las firmas nuevas de `GameLoop` e
  `ILoopClient`.
- [ ] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "InputState|InputEvents|GameLoop"
  ```

## Sub-ola CL3.4: config, línea de comandos y log

**Concepto:** de dónde sale lo que depende del lugar (la carpeta de datos, el servidor, el modo
de video), validado antes de abrir nada, y un log que se puede leer en un test. Va en dos PRs:
**PR A**, config y línea de comandos; **PR B**, el log.

**Contrato** (02 § Del launcher al juego, § Qué implica para MuRet; Q-C34 a Q-C37):

| Qué | Original | MuRet |
| --- | --- | --- |
| servidor | compilado (`0x6d5dc0` → host en `0x6c76e0`, puerto 44405 en `0x6d5dc4`) o el del launcher en el registro; FoxAnti pisa el host con `ConnectIp` | `[Server] Host` y `Port` en la config; el puerto por defecto es 44405; el host se exige recién cuando arranca la red (CL4) |
| argumentos | no se leen; el parser `0x4b9890` (`/u<ip> /p<puerto>`, letras en mayúscula o minúscula, en cualquier lugar, sin `connect`) no lo llama nadie (Q-C36) | `/u<ip> /p<puerto>` pisa la config, con o sin `connect` adelante; letras en mayúscula o minúscula; puerto de 1 a 65535; uno sin el otro es error (pregunta 6); `/y` y `/z` no |
| largos | copia la línea de comandos y el host sin mirar el largo (Q-C37) | el host va de 1 a 255 bytes y sin espacios, validado antes de guardarlo |
| su archivo | se abre a sí mismo desde la carpeta actual y sale sin avisar si no está (Q-C35) | no se abre a sí mismo; `[Data] Path` (o `--data <ruta>`): si se da y no existe, un error que la nombra (`Data::DataPath`, CL0.2) |
| config | registro `HKCU\SOFTWARE\Webzen\Mu\Config` y `config.ini` | `muret-client.ini` junto al ejecutable (la carpeta la da `SDL_GetBasePath` en `main.cpp`), o `--config <ruta>` (pregunta 5) |
| video | `Resolution` y `ColorDepth` del registro | `[Video] Width`, `Height` (640 × 480 por defecto), `VSync` (`on`, `adaptive`, `off`) y `Pacing` (`decoupled`, `faithful`) |
| log | archivo de texto con líneas como `Mu online %s (%s) executed.` | `Core::Log` con niveles, a `stderr` y a `muret-client.log` junto al ejecutable (o `[Log] Path`); si el archivo no abre, solo `stderr` y un aviso. Los mensajes de SDL entran por `SDL_SetLogOutputFunction` |

**`Resolve`, en este orden** (el primer error corta):
1. El archivo: con `--config <ruta>`, si no existe es error y lo nombra; sin `--config`, si
   `muret-client.ini` no está, se usan los valores por defecto (así `Main --frames 60` anda en CI).
2. Si el archivo está, `ParseConfig`: un error dice su línea.
3. La línea de comandos pisa la config (`/u /p`, `--data`, `--pacing`, `--vsync`).
4. Las combinaciones: en el modo fiel, un `VSync` dado en `on` o `adaptive` (en la config o con
   `--vsync`) es error y nombra las dos claves (la regla de `--pacing faithful --vsync on` de la
   CL3.2); si no se dio, queda en `off`. En el modo normal, sin dar, queda en `on`.
5. La carpeta de datos, solo si se dio: tiene que existir.

El host vacío no es error acá: lo pide la CL4 al conectarse.

**Archivos:**
- **PR A:** crear `Client/src/App/Config.h/.cpp` y `ConfigError.h`;
  `Client/tests/app/test_config.cpp`. Modificar `vcpkg.json` (`inih` con la feature `cpp`, si el
  paso 1 lo confirma), `Client/src/App/CommandLine.h/.cpp` y su test (pasa a `Core::Expected` con un
  error que se imprime, y suma `connect`, `/u`, `/p`, `--config` y `--data`), `main.cpp`,
  `Client/CMakeLists.txt` (los tests de `Main` sin ventana), `Client/README.md` (cómo se configura),
  07 (Q-C35 a Q-C37 con sus tests), PROGRESS y este plan.
- **PR B:** crear `Client/src/Core/Log.h/.cpp` y `Client/src/Platform/SdlLog.h/.cpp`;
  `Client/tests/core/test_log.cpp`. Modificar `main.cpp` (el aviso de `MaxFrameTime` y los errores
  del arranque van al log), PROGRESS y este plan; los fuentes y el test entran por los globs.

**Interfaces:**

```cpp
namespace Client::App
{
// El puerto compilado del ConnectServer, 0x6d5dc4 (02 § Del launcher al juego).
inline constexpr std::uint16_t DefaultConnectPort = 44405;
// Q-C37: el host del launcher llegaba a 255 bytes y se copiaba sin mirar el largo.
inline constexpr std::size_t MaxHostBytes = 255;

struct ServerAddress { std::string host; std::uint16_t port = DefaultConnectPort; };

struct CommandLine
{
    std::optional<std::uint64_t> maxFrames;
    std::optional<ServerAddress> connect;       // /u<ip> /p<puerto>, con o sin connect
    std::optional<std::filesystem::path> configPath;
    std::optional<std::filesystem::path> dataPath;
    std::optional<PacingMode> pacing;
    std::optional<VSyncMode> vsync;
    bool stats = false;
};
Core::Expected<CommandLine, std::string> ParseCommandLine(std::span<const std::string_view> arguments);

struct ClientConfig
{
    std::optional<std::filesystem::path> dataPath;
    std::optional<ServerAddress> server;
    int width = 640;
    int height = 480;
    std::optional<VSyncMode> vsync; // vacío: lo decide Resolve (paso 4)
    PacingMode pacing = PacingMode::Decoupled;
    std::optional<std::filesystem::path> logPath;
};
struct ConfigError { std::size_t line = 0; std::string message; };
Core::Expected<ClientConfig, ConfigError> ParseConfig(std::string_view text);
// Los pasos de arriba, en ese orden. `exeFolder` es donde se busca muret-client.ini.
Core::Expected<ClientConfig, std::string> Resolve(const std::filesystem::path& exeFolder,
                                                  const CommandLine& commandLine);
} // namespace Client::App

namespace Client::Core
{
enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };
class LogSink { public: virtual ~LogSink() = default; virtual void Write(LogLevel level, std::string_view line) = 0; };
// Escribe la misma línea en las dos salidas (stderr y el archivo), con un std::mutex: SDL puede
// loguear desde sus hilos (SDL_Log es seguro entre hilos).
class TeeSink final : public LogSink { /* LogSink& first, LogSink& second, std::mutex */ };
class Log
{
public:
    Log(LogSink& sink, LogLevel minimum);
    // La línea es "[info] " y el texto: Info("hola {}", 1) escribe "[info] hola 1". Los otros
    // niveles, "[debug] ", "[warning] " y "[error] ".
    template <typename... Args> void Info(std::format_string<Args...> format, Args&&... args);
    // Debug, Warning y Error igual.
};
} // namespace Client::Core

namespace Client::Platform
{
// RAII: el constructor pone la salida de SDL con SDL_SetLogOutputFunction(callback, &log), y el
// destructor vuelve a SDL_GetDefaultLogOutputFunction() antes de que muera el Log. Prioridades:
// TRACE, VERBOSE y DEBUG van a Debug; INFO a Info; WARN a Warning; ERROR y CRITICAL a Error.
class SdlLog
{
public:
    explicit SdlLog(Core::Log& log);
    ~SdlLog();
    SdlLog(const SdlLog&) = delete;
    SdlLog& operator=(const SdlLog&) = delete;
};
} // namespace Client::Platform
```

Sin globales (D-C1): el `Log` lo crea `main.cpp` y se pasa por referencia a quien lo use. `SdlLog`
se declara después del `Log` en `main`, así se destruye antes.

- [ ] **Paso 1 (PR A, §10):** con Context7, confirmar el puerto `inih` de vcpkg y su
  comportamiento: secciones, `clave = valor`, `;` al principio y al final de la línea, una clave
  repetida, una línea sin `=` (la línea del primer error), y el BOM. Si coincide con la tabla del
  paso 3, entra; si no, el parser propio, con la razón en el header. Confirmar también que CLI11 y
  cxxopts no leen `/u10.0.0.1` pegado (pregunta 6).
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** antes de los tests, la tabla de la línea de comandos:
  qué dan `connect /u10.0.0.1 /p44405`, `CONNECT /U10.0.0.1 /P44405`, `/p44405 /u10.0.0.1` (el
  original busca en cualquier orden), `/u10.0.0.1 /p44405` sin `connect`, `connect` solo,
  `connect /u10.0.0.1`, `/p44405` solo, `/p0`, `/p65536`, `/p12a`, `/u` vacío, un host de 255
  bytes y uno de 256, y `/y...`.
- [ ] **Paso 3 (PR A):** los tests:
  - `CommandLineTest.AcceptsConnectWithHostAndPort`, `IgnoresTheCase`, `AcceptsAnyOrder`,
    `ConnectIsOptional`, `ConnectAloneIsIgnored`; `RejectsAPortOutOfRange` (0 y 65536: "el puerto
    va de 1 a 65535"), `RejectsAPortWithLetters`, `RejectsHostWithoutPort` y
    `RejectsPortWithoutHost` (pregunta 6), `RejectsAnEmptyHost`, `RejectsTheLauncherOptions` (`/y`
    y `/z`), `AcceptsAHostOf255Bytes` y `RejectsAHostOf256Bytes` (Q-C37), `ParsesConfigPath`,
    `ParsesDataPath`, y los de `--frames`, `--pacing`, `--vsync` y `--stats` que ya están;
  - `ConfigTest.ReadsTheSections`, `DefaultsWhenAKeyIsMissing` (puerto 44405, 640 × 480,
    decoupled, y después de `Resolve` VSync on; con `Pacing = faithful` y sin `VSync`, off),
    `ReportsTheLineOfAnError` (una línea sin `=` en la 3 da `line == 3`),
    `RejectsAnUnknownVSyncValue`;
  - `ConfigTest.AMissingDefaultFileMeansDefaults`, `AMissingConfigPathIsAnError` (nombra la ruta),
    `TheCommandLineWins` (`/u... /p...` pisa `[Server]`, `--pacing faithful` pisa `Pacing`),
    `FaithfulWithVSyncOnIsAnError` (nombra `Pacing` y `VSync`), `AMissingDataFolderNamesIt` (el
    test del 02 y Q-C35), `NoDataFolderIsNotAnError` y `NoHostIsNotAnError` (lo pide la CL4).
- [ ] **Paso 4 (PR A):** `ParseCommandLine`, `ParseConfig` y `Resolve`. `main.cpp`: argumentos y
  config, y recién después SDL y la ventana.
- [ ] **Paso 5 (PR A):** CTest de `Main` sin ventana (fallan antes de `SDL_Init`):
  `Client.Main.RejectsBadPort` (`Main connect /u127.0.0.1 /p0`, la salida dice "el puerto va de 1 a
  65535") y `Client.Main.NamesTheMissingDataFolder` (`Main --data <carpeta que no existe>`, la
  salida trae la ruta). El paso de Xvfb del CI sigue sin config ni datos.
- [ ] **Paso 6 (PR B):** los tests: `LogTest.WritesTheLevelAndTheMessage` (con un `LogSink` de
  prueba que guarda las líneas: `Info("hola {}", 1)` da `[info] hola 1`),
  `BelowTheMinimumIsNotWritten`, `EachLevelHasItsPrefix`, `TeeSinkWritesBoth`.
- [ ] **Paso 7 (PR B):** `Log`, `TeeSink`, la salida a archivo y a `stderr`, y `SdlLog`.
- [ ] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "CommandLine|Config|Log|Client.Main"
  ```

---

## Qué no se prueba sin ventana, y cómo lo cubre CI

| Qué | Cómo |
| --- | --- |
| el bucle, los dos modos, el input, la config, el log | los tests de `app/`, `input/`, `core/` y `support/`, con el reloj falso: corren en los dos sistemas |
| el loader, la versión y el intervalo de swap como datos | los de `platform/`, sin ventana: los dos sistemas |
| abrir la ventana, el contexto 3.3, el swap | el paso de Xvfb con Mesa (llvmpipe) del CI, solo en Linux: `Main --frames 60`, y desde la CL3.2 también con `--vsync off` y con `--pacing faithful`. En Windows, a mano (pregunta 7) |
| que VSync ata los FPS al monitor y que el modo fiel da 21,33 FPS reales | a mano con `--stats` (CL3.2, paso 5): Xvfb no tiene retrazo, y el tiempo real en CI no es estable |
| minimizar, el foco, el teclado de verdad | a mano; la lógica de cada evento ya la prueban `WindowStateTest` e `InputStateTest` |

## Al cerrar la CL3

- [ ] Completar el cierre de la guía de la CL3.0.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Escribir el plan de la CL4 (red del cliente) en su propio PR, con lo aprendido: el socket no
  bloqueante entra en el `GameLoop` como una consulta por frame, y el keepalive de 20 s como un
  temporizador del bucle, probado con el `FakeClock`.
