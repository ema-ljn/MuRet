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
un modo fiel reproduce el ritmo del original: 21,33 frames por segundo y 7 pasos cada 6 frames.

**Arquitectura:** dos capas nuevas, como dice el roadmap. `Client::Platform`
(`Client/src/Platform/`) es lo único que ve SDL3: ventana, contexto GL, el loader, el reloj real,
la traducción de eventos y el log de SDL. Va en un target propio, `ClientPlatform`, porque
`ClientLib` no enlaza SDL3 (roadmap, "Estructura de `Client/`"). `Client::App`
(`Client/src/App/`) tiene el bucle, el estado de la ventana, la config y la línea de comandos, y
`Client::Core` suma el `Clock` (la interfaz) y el `Log`: todo eso va en `ClientLib` y se prueba
sin SDL. El input se parte igual: el estado por frame (`Client::Input`, en `ClientLib`) no sabe de
SDL, y `Platform` le pasa los eventos ya traducidos. `main.cpp` arma todo y no tiene lógica. Como
toda N.0, la CL3.0 vive en `labs/`.

**Stack:** C++20 (`std::chrono` con `std::chrono::nanoseconds` como unidad, `steady_clock` en el
lab, interfaces virtuales para inyectar el reloj, `std::bitset` para las teclas, `std::format`
para el log), SDL3 (ventana, `SDL_GL_*`, eventos, `SDL_GetTicksNS`, `SDL_DelayPrecise`,
`SDL_SetLogOutputFunction`), GoogleTest. No entra ninguna biblioteca nueva (`CODING_RULES.md`
§10): el paso fijo y el modo fiel son del MU, SDL3 ya está, y el log y la config se deciden en las
preguntas 4 y 5.

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
| el acumulador | `DAT_006d5ee4`, un `int` con signo | `CMP [0x6d5ee4],0x28` + `JL` en `0x637791`: corre pasos mientras valga 40 o más |
| un paso | `0x638162` | `SUB EAX,0x28`: cada paso resta 40 ms; no hay tope de pasos por frame |
| el inicio de la medición | `0x63852b`, `GetTickCount()` | se toma **después** de los pasos, justo antes de dibujar |
| la espera | `0x6385fd`..`0x638624` | `while (GetTickCount() - inicio < 0x28)`: activa, sin dormir |
| lo que suma | `0x63862c` | `DAT_006d5ee4 += GetTickCount() - inicio`: solo el dibujo, el swap y la espera |
| el otro delta | `0x459b60`, en `0x637700` antes de los pasos | QPC (o `timeGetTime`), con tope de 200 ms; va a `0x65ee60` y `0x65fc30`, no al acumulador |
| VSync | `0x69e830`, `0x69e860` | `wglSwapIntervalEXT` solo en dos stubs que nadie llama: el original no pide VSync |

Dos cosas salen de la tabla y no estaban escritas en el 02: **el tiempo de los pasos no se
acumula** (la medición empieza en `0x63852b`), y **el acumulador no tiene tope**: un frame de 10 s
corre 250 pasos seguidos. Lo que pasa fuera de la medición (los pasos, la red, el bucle de
mensajes, una ventana arrastrada) se pierde, salvo que cruce un tick de `GetTickCount`. La CL3.R
las pasa al 02 y la CL3.2 las prueba en el modo fiel.

**Terminada cuando** (roadmap): una ventana a la tasa del monitor con la lógica a tick fijo,
verificado con un reloj falso. En concreto:
- con el reloj falso, 25 pasos por segundo simulado con 1, 25, 60, 144 y 240 frames por segundo, y
  3 pasos seguidos ante un frame de 130 ms;
- en el modo fiel, con un reloj de 64 ticks por segundo, 64 frames cada 3 s y 7 pasos cada 6
  frames (0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2 en los primeros 13);
- `Main` abre la ventana en Linux (Xvfb, en CI) y en Windows (a mano), corre el bucle y sale con
  `--frames N` imprimiendo frames y pasos;
- `Main connect /u<ip> /p<puerto>` y la config eligen el servidor, y un puerto malo o una carpeta
  de datos que no existe terminan con un error que los nombra, sin abrir la ventana.

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
   `ClientLib` y `SDL3::SDL3`; `Main` enlaza los dos. `ClientTests` no enlaza SDL3, y lo poco de
   `Platform` que se prueba sin ventana (el chequeo de los punteros de GL, la versión, el
   intervalo de swap) va a un `ClientPlatformTests` aparte, que corre en los dos sistemas porque
   no abre ventana.
4. **El log: propio o `spdlog`.** `spdlog` está en vcpkg y resuelve el problema (niveles, salidas,
   archivo), pero trae `fmt` y su propio manejo de hilos, y el cliente tiene un hilo. Lo que se
   necesita son cuatro niveles, una salida a `stderr` y otra a un archivo, y una salida de prueba.
   *Recomendación:* propio, chico, con `std::format` (GCC 13 del runner y MSVC 2022 lo tienen), y
   la razón escrita en el header. Si al llegar a la CL4 hace falta más (rotación, varios hilos),
   se revisa.
5. **El formato de la config.** El original usa el registro y un `config.ini` (`[LOGIN] Version`,
   02). *Recomendación:* un INI chico (secciones, `clave = valor`, `;` de comentario) en
   `muret-client.ini`, leído a mano a un `Expected` con la línea del error. Antes de escribirlo,
   como pide §10, se miran `inih` y `tomlplusplus` en vcpkg con Context7 y se escribe en el header
   por qué no entran (la sospecha: devolver el error como valor con su línea es la mitad del
   trabajo, y las dos lo dan a su manera).
6. **`/u` sin `/p`** (Q-C36, el 02 lo deja para la CL3). El parser sin usar del original devuelve
   0 con el host ya copiado. *Recomendación:* error que lo dice ("falta /p<puerto>"), sin usar el
   puerto de la config: un `connect` a medias es un error de quien lanza el cliente.
7. **La ventana en CI de Windows.** El runner de Windows no tiene OpenGL 3.3 (solo el 1.1 de GDI),
   y por eso la ventana de la CL0.3 corre solo en Linux con Xvfb. *Recomendación:* igual: en
   Windows, CI compila todo y corre `ClientTests` y `ClientPlatformTests`; la ventana se prueba a
   mano y queda escrito en el README. Mesa para Windows (`opengl32.dll` de llvmpipe) se puede
   sumar después si un bug de Windows se escapa.

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
- **La ventana minimizada.** En muchos drivers, `SDL_GL_SwapWindow` vuelve enseguida con la
  ventana minimizada y el bucle se come un núcleo. La lógica sigue (el keepalive de la CL4 no se
  puede cortar), el dibujo no, y el bucle duerme hasta el próximo paso (CL3.2,
  `AMinimizedWindowStepsWithoutDrawing`).
- **Una tecla mantenida entre frames, o apretada y soltada en el mismo frame.** Mantenida, está
  abajo en todos los frames pero "se apretó" una sola vez. Apretada y soltada entre dos lecturas,
  `SDL_GetKeyboardState` no la ve nunca (lo dice su documentación): por eso el estado se arma con
  los eventos. Y un frame con 2 pasos no ve el mismo apretón dos veces, ni uno con 0 pasos lo
  pierde (CL3.3).
- **El modo fiel que mide de más.** Si la medición empieza antes de los pasos y no en `0x63852b`,
  un paso lento acelera el juego en vez de frenarlo (CL3.2,
  `TimeSpentInStepsIsNotAccumulated`).

---

## Sub-ola CL3.0: el bucle de juego (N.0, en `labs/`)

**Concepto:**
- el bucle de juego: leer la entrada, avanzar la lógica, dibujar; por qué un paso variable
  (`Update(dt)`) hace que la física y los tiempos dependan de los FPS, y el paso fijo con
  acumulador ("Fix your timestep"): se suma lo que tardó el frame y se corren pasos de 40 ms
  mientras alcance;
- la interpolación: el dibujo ve el estado entre dos pasos, con `alfa = acumulador / paso`, y por
  qué eso no cambia nada observable de la lógica;
- relojes: de pared contra monótono, resolución contra precisión, y por qué `GetTickCount` con un
  tick de 15,625 ms convierte una espera de 40 ms en una de 46,875;
- VSync: el swap que espera el retrazo, y por qué ata los FPS al monitor;
- la espiral de la muerte: si un paso tarda más que su paso, cada frame debe más pasos que el
  anterior, y por qué se pone un tope.

Del C++: `std::chrono::duration` y sus unidades, `duration_cast` (trunca hacia cero), duraciones
enteras contra de punto flotante, `steady_clock::is_steady`, y una interfaz virtual chica para
inyectar el reloj (el reloj falso de los tests).

**Archivos:**
- Crear: `docs/learning/ola-cl3.0-el-bucle-de-juego.md` (la guía; Claude)
- Crear: `labs/cl3.0-bucle/FixedStep.h/.cpp`, `TickGrid.h/.cpp`, sus tests y
  `jefe/OriginalLoopTest.cpp` (Emanuel)
- Modificar: `labs/CMakeLists.txt` (`MuRetLabsCl30` y `LabCl30Tests`, con el prefijo
  `LabCl30.`, como la CL2.0), `labs/README.md`

- [ ] **Paso 1 (Claude):** la guía, justo antes de empezar.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** `Labs::FixedStep` con milisegundos enteros:
  `int Advance(int frameMs)` devuelve cuántos pasos de 40 ms corren y deja el resto. Primero los
  tests: un frame de 40 da 1 paso y resto 0; uno de 39, 0 pasos y resto 39; uno de 130, 3 pasos y
  resto 10; 25 frames de 40 dan 25 pasos, y 1 de 1.000, también 25.
- [ ] **Paso 3 (Emanuel):** `Labs::TickGrid`, el reloj de `GetTickCount`: con `tick` en
  microsegundos (15.625 o 10.000), `Ms(t)` es el valor que leería `GetTickCount` en el
  microsegundo `t` (`(t / tick) * tick / 1000`, entero). Tests: con 15.625, `Ms` en los ticks 0 a
  4 da 0, 15, 31, 46 y 62; con 10.000, 0, 10, 20, 30 y 40.
- [ ] **Paso 4 (Emanuel, jefe):** el bucle de `0x637700` sobre una `TickGrid`: pasos mientras el
  acumulador valga 40 o más, `inicio = Ms(ahora)`, la espera activa de 40 ms avanzando el tiempo
  de a 1 µs, y el acumulador suma `Ms(ahora) - inicio`. Tests: con 15.625 µs, 64 frames ocupan
  3.000.000 µs (21,33 FPS) y los pasos de los primeros 13 frames son 0, 1, 1, 1, 1, 1, 2, 1, 1, 1,
  1, 1, 2; con 10.000 µs, 25 frames por segundo y 1 paso por frame desde el segundo.
- [ ] **Paso 5 (Emanuel, jefe, la espiral):** el mismo bucle con pasos que tardan 50 ms. Como el
  original no mide los pasos, el juego se frena en vez de espiralar: escribir el test que lo
  muestra (en 10 s simulados corren menos de 250 pasos) y, al lado, uno con `FixedStep` midiendo
  el frame entero, que sin tope debe cada vez más pasos por frame.
- [ ] **Paso 6 (Emanuel):** comparar, y anotarlo en `labs/README.md`: el `FixedStep` con
  milisegundos enteros contra uno con `std::chrono::nanoseconds` (qué se pierde al truncar, y por
  qué el original no pierde nada con `int`), y `steady_clock` contra `system_clock` (cuál puede
  ir para atrás y cuándo).
- [ ] **Paso 7 (carril R):** en Ghidra, el `JL` de `0x637798`, el `SUB EAX,0x28` de `0x638167` y
  el `ADD` de `0x638632`: confirmar que el acumulador es con signo y que nada lo acota.
- [ ] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "LabCl30"
  ```

## Sub-ola CL3.R: qué depende del paso de 40 ms (P-C03)

**Concepto:** carril R antes de desacoplar. El dibujo interpolado solo es fiel si todo lo que la
lógica hace por paso sigue por paso, y lo que el original hace por frame se decide uno por uno
(Q-C20 ya dice que la mascota por frame es un bug).

**Archivos:** modificar `Client/docs/research/02-arranque-y-ciclo-de-vida.md` (lo nuevo del bucle,
de la tabla de arriba, y la tabla de lo que corre por paso con lo que se lea), `08-pendientes.md`
(P-C03 y la parte de P-C08 que cierre), `07-quirks-y-bugs.md` (un quirk nuevo por el acumulador
sin tope, si la CL3.2 lo cambia, y lo que aparezca) y este plan.

- [ ] **Paso 1:** pasar al 02 lo que dice la tabla "Lo que dice el binario del bucle": acumulador
  con signo y sin tope, la medición desde `0x63852b` (los pasos no se miden), y el delta de
  `0x459b60` con tope de 200 ms que va a `0x65ee60`/`0x65fc30` y no al acumulador.
- [ ] **Paso 2:** leer `0x636290` (la lógica de la escena 5) y `0x623e70` (40 KB, solo en el
  juego): para cada cosa que avanza, si avanza por paso, por frame o por reloj (`timeGetTime`,
  `GetTickCount`). Una tabla en el 02: qué, dónde, por paso o por frame, y qué hace MuRet.
- [ ] **Paso 3:** quién usa `0x65ee60` y `0x65fc30` (el objeto del delta con tope): si algo de la
  lógica avanza por tiempo real y no por paso, el modo normal lo tiene que medir igual.
- [ ] **Paso 4:** los usos de `DAT_05767940` y `DAT_077f3b74` (las cuentas regresivas) y de
  `DAT_07d2d360` (el ciclo de 32 pasos): qué los lee, y si el que los lee dibuja.
- [ ] **Paso 5 (P-C08):** las llamadas a `GetAsyncKeyState`: si se leen en el frame o en el paso,
  si alguna usa el bit bajo ("se apretó desde la última llamada") o solo el alto ("está abajo"),
  y qué teclas lee cada escena. El resto del WndProc (`0x4af4e0`): qué hace con el mouse, y
  `WM_ACTIVATE` (`DAT_05763580`) con el foco perdido. Lo que cambie el diseño de la CL3.3 se
  escribe en su sección antes de arrancarla.
- [ ] **Paso 6:** cerrar P-C03 en el 08 (o dejar escrito qué queda para la CL6 y la CL7, que
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
| foco | `WM_ACTIVATE` guarda el foco en `DAT_05763580` | `SDL_EVENT_WINDOW_FOCUS_LOST`/`_GAINED` al `WindowState` |
| minimizar | el bucle sigue y la espera lo frena a 21,33 FPS | `SDL_EVENT_WINDOW_MINIMIZED`/`_RESTORED`: no se dibuja, la lógica sigue (CL3.2) |
| tamaño | resolución fija | `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`: el viewport sigue a la ventana |
| cursor | `WM_SETCURSOR` lo esconde: lo dibuja el juego | queda el del sistema hasta que el juego lo dibuje (CL5) |

**Archivos:** crear `Client/src/Platform/SdlContext.h/.cpp` (`SDL_Init`/`SDL_Quit` con RAII, lo
que hoy es `SdlVideo`), `Window.h/.cpp` (ventana y contexto, hoy `CreateGlWindow`),
`GlLoader.h/.cpp` (la tabla de punteros de la CL0.3, sin cambiar su forma, D-C2),
`SwapInterval.h/.cpp` y `WindowEvents.h/.cpp` (de `SDL_Event` a `App::WindowEvent`); crear
`Client/src/App/WindowState.h/.cpp` y `VSyncMode.h`; `Client/tests/app/WindowStateTest.cpp` y
`Client/tests/platform/GlLoaderTest.cpp` y `SwapIntervalTest.cpp`. Modificar
`Client/CMakeLists.txt` (`ClientPlatform`, `ClientPlatformTests`), `Client/src/main.cpp` (queda
el armado), `Client/README.md` y este plan.

**Interfaces:**

```cpp
namespace Client::App
{
enum class WindowEvent : std::uint8_t { CloseRequested, Minimized, Restored, FocusLost, FocusGained, PixelSizeChanged };

struct PixelSize { int width = 0; int height = 0; };

struct WindowState
{
    bool running = true;
    bool minimized = false;
    bool focused = true;
    PixelSize pixelSize;
};

// Pura: lo que cambia cada evento. `size` solo cuenta con PixelSizeChanged.
WindowState Apply(WindowState state, WindowEvent event, PixelSize size = {});
} // namespace Client::App

namespace Client::App
{
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

class SdlContext; // SDL_Init(SDL_INIT_VIDEO) y SDL_Quit
class Window
{
public:
    static Core::Expected<Window, std::string> Create(const WindowOptions& options);
    void Swap();
    App::PixelSize PixelSize() const;
};
// Vacía la cola de SDL: cada evento de ventana va a `state`, y cada uno de teclado y mouse, a
// `input` (CL3.3).
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
  `MinimizedAndRestored`, `FocusLostKeepsRunning` (solo cambia `focused`),
  `PixelSizeChangedUpdatesTheSize`, `AZeroPixelSizeIsIgnored`;
  `GlLoaderTest.RejectsTheInvalidAddresses` (0, 1, 2, 3 y `UINTPTR_MAX` no; 4 sí),
  `GlLoaderTest.RequiresThreeThree` (3.3, 3.4, 4.5 sí; 3.2 y 2.1 no);
  `SwapIntervalTest.MapsEachMode` (`On` 1, `Adaptive` -1, `Off` 0).
- [ ] **Paso 3:** `WindowState`, y `Platform` con lo que ya hace `main.cpp`. `main.cpp` queda en el
  armado: `SdlContext`, `Window`, loader, `ApplySwapInterval` y el bucle de la CL0.3, que la
  CL3.2 reemplaza.
- [ ] **Paso 4:** CMake: `ClientPlatform` enlaza `ClientLib` y `SDL3::SDL3` y lleva
  `SDL_OPENGL_1_FUNCTION_TYPEDEFS` (sale de `Main`); `ClientPlatformTests` corre con
  `gtest_discover_tests` y sin ventana. El paso de Xvfb del CI sigue igual.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "WindowState|GlLoader|SwapInterval|Client.Main"
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60
  ```

## Sub-ola CL3.2: bucle con tick fijo y render desacoplado, con reloj falso

**Concepto:** el paso fijo con acumulador e interpolación, con el reloj inyectado: el mismo
`GameLoop` corre con el reloj de SDL en `Main` y con uno falso en los tests, donde un frame
"tarda" lo que el test dice.

**Contrato** (02 § El ritmo, y la tabla "Lo que dice el binario del bucle"):

| Qué | Original | MuRet, modo normal (`Decoupled`) | MuRet, modo fiel (`Faithful`) |
| --- | --- | --- | --- |
| paso | 40 ms (`0x28`, `0x637791` y `0x638162`) | 40 ms | 40 ms |
| qué se acumula | `GetTickCount` del dibujo, el swap y la espera (`0x63852b`..`0x63862c`) | el frame entero, de un `Now()` al siguiente | lo mismo que el original, con un reloj de 64 ticks por segundo |
| espera | activa hasta 40 ms de `GetTickCount` (`0x6385fd`..`0x638624`) | ninguna: VSync o sin tope | duerme hasta el tick en que se cumplen los 40 ms |
| tope | ninguno | `MaxFrameTime` (pregunta 2) | ninguno, como el original |
| dibujo | el estado después de los pasos | interpolado con `alfa = acumulador / paso` | `alfa = 0`, el estado después de los pasos |
| minimizado | sigue a 21,33 FPS | pasos sí, dibujo no; duerme hasta el próximo paso | igual que el normal |

**Archivos:** crear `Client/src/Core/Clock.h` (la interfaz), `Client/src/App/GameLoop.h/.cpp`,
`FixedStep.h/.cpp`, `FaithfulPacer.h/.cpp` y `LoopTiming.h` (las constantes);
`Client/src/Platform/SdlClock.h/.cpp`; `Client/tests/support/FakeClock.h/.cpp` con su
`FakeClockTest.cpp`, y `Client/tests/app/FixedStepTest.cpp`, `GameLoopTest.cpp` y
`FaithfulPacerTest.cpp`. Modificar `Client/CMakeLists.txt`, `main.cpp` (una escena de prueba: el
fondo cambia de color con el ciclo de 32 pasos, como `DAT_07d2d360`, interpolado), `App/CommandLine`
(`--pacing faithful` y `--vsync on|adaptive|off`; la CL3.4 los pasa a la config), 07 (el quirk
nuevo de la pregunta 2), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Core
{
using Duration = std::chrono::nanoseconds;

// El reloj del bucle. Now() es monótono en el real; el falso puede ir para atrás a propósito.
class Clock
{
public:
    virtual ~Clock() = default;
    virtual Duration Now() = 0;
    virtual void SleepFor(Duration duration) = 0;
};
} // namespace Client::Core

namespace Client::App
{
// 02 § El ritmo: CMP [0x6d5ee4],0x28 en 0x637791, SUB 0x28 en 0x638162.
inline constexpr Core::Duration LogicStep = std::chrono::milliseconds{40};
// Pregunta 2: un frame más largo cuenta como este; el resto se pierde con un aviso.
inline constexpr Core::Duration MaxFrameTime = std::chrono::seconds{1};
// El tick de GetTickCount en la VM XP: 15,625 ms, 64 por segundo (02 § El ritmo).
inline constexpr Core::Duration FaithfulTick = std::chrono::microseconds{15'625};
// La espera de 0x6385fd..0x638624, en los ms que mide GetTickCount.
inline constexpr std::int32_t FaithfulWaitMs = 40;

// Lo que el bucle llama: la lógica, un paso por vez, y el dibujo.
class LoopClient
{
public:
    virtual ~LoopClient() = default;
    virtual void Step() = 0;
    virtual void Render(float alpha) = 0; // alpha en [0, 1)
};

class FixedStep
{
public:
    // Suma el frame (negativo cuenta 0; más que MaxFrameTime cuenta MaxFrameTime) y devuelve
    // cuántos pasos corren.
    int Advance(Core::Duration frameTime);
    float Alpha() const;
    bool LastFrameWasClamped() const;
};

// El reloj de GetTickCount sobre el de Clock: los ms que leería en `now`, de a FaithfulTick.
std::int32_t TickCountMs(Core::Duration now);

enum class PacingMode : std::uint8_t { Decoupled, Faithful };

struct FrameStats { std::uint64_t frames = 0; std::uint64_t steps = 0; std::uint64_t clampedFrames = 0; };

class GameLoop
{
public:
    GameLoop(Core::Clock& clock, LoopClient& client, PacingMode mode);
    // Un frame: los pasos que tocan y el dibujo si `state.minimized` es falso. En Faithful, la
    // espera también. Devuelve cuántos pasos corrió.
    int RunFrame(const WindowState& state);
    const FrameStats& Stats() const;
};
} // namespace Client::App
```

El reloj falso (`Client/tests/support/FakeClock.h`):

```cpp
namespace Client::Tests
{
class FakeClock final : public Core::Clock
{
public:
    Core::Duration Now() override;            // el tiempo actual, sin moverlo
    void SleepFor(Core::Duration d) override; // lo mueve `d` y lo anota
    void Advance(Core::Duration d);           // lo mueve (d puede ser negativo)
    std::span<const Core::Duration> Sleeps() const;
};

// Un LoopClient que cuenta, y que hace "tardar" a cada paso y a cada dibujo lo que se le diga
// moviendo el FakeClock: así un test arma un frame de 130 ms o un breakpoint de 10 s.
class RecordingClient final : public App::LoopClient { /* steps, renders, alphas, stepCost, renderCost */ };
} // namespace Client::Tests
```

En `Decoupled`, el frame se mide de un `Now()` al siguiente, al empezar `RunFrame`. Para frames que
no dan nanosegundos enteros (240 por segundo), los tests avanzan `(i + 1) * 1 s / n - i * 1 s / n`
en el frame `i`, así que n frames suman 1 s exacto. En `Faithful`, el orden es el del original:
pasos mientras el acumulador valga 40 ms o más, `inicio = TickCountMs(Now())`, dibujo, y
`SleepFor` hasta el próximo tick mientras `TickCountMs(Now()) - inicio < 40`; después, el
acumulador suma `TickCountMs(Now()) - inicio`. `SdlClock` usa `SDL_GetTicksNS` y
`SDL_DelayPrecise`, que espera activamente el último tramo: con `SDL_DelayNS` o
`std::this_thread::sleep_for` un despertar tarde se saltea el tercer tick.

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests del paso 2, a mano: los pasos por
  frame del modo fiel en los primeros 7 frames, con el reloj de 64 ticks y los ms que mide cada
  uno (`TickCountMs` en los ticks 0, 3, 6 y 9 da 0, 46, 93 y 140). El paso 2 trae el resultado.
- [ ] **Paso 2:** los tests:
  - `FakeClockTest`: `Now()` no se mueve solo; `SleepFor(5 ms)` lo mueve 5 ms y lo anota;
    `Advance(-3 ms)` lo lleva para atrás;
  - `FixedStepTest.FortyIsOneStep` (40 ms: 1 paso, alfa 0), `ThirtyNineIsNone` (0 pasos, alfa
    0,975), `OneHundredThirtyIsThree` (3 pasos y alfa 0,25: el alcanzar del 02),
    `AlphaIsTheFractionLeft` (50 ms: 1 paso, alfa 0,25), `ANegativeFrameCountsZero`,
    `TheCapIsOneSecond` (10 s: 25 pasos, `LastFrameWasClamped()`);
  - `GameLoopTest.TwentyFiveStepsPerSecondAtAnyFrameRate`, parametrizado con 1, 25, 60, 144 y
    240 frames por segundo: en 4 s simulados, 100 pasos (el primer frame solo fija el inicio);
  - `GameLoopTest.ASlowFrameCatchesUpWithThreeSteps`: un dibujo de 130 ms, y el frame siguiente
    corre 3 pasos con alfa 0,25;
  - `GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap`: un dibujo de 10 s (el breakpoint) da 25
    pasos y `clampedFrames == 1`, y el siguiente frame normal vuelve a 1 paso cada 40 ms;
  - `GameLoopTest.AClockThatGoesBackRunsNoSteps`: el reloj vuelve 5 ms, el frame da 0 pasos y el
    acumulador no cambia; el siguiente, 40 ms después, da 1;
  - `GameLoopTest.AClockThatDoesNotMoveRunsNoSteps`: dos frames en el mismo instante (VSync
    apagado), 0 pasos, el mismo alfa y ningún NaN;
  - `GameLoopTest.AMinimizedWindowStepsWithoutDrawing`: minimizada durante 1 s, 25 pasos, 0
    dibujos, y el bucle durmió (los `Sleeps()` suman cerca de 1 s) en vez de girar;
  - `GameLoopTest.AlphaGrowsBetweenSteps`: con 240 FPS, el alfa sube de frame en frame dentro de un
    paso, siempre en [0, 1), y vuelve a bajar después de cada paso;
  - `FaithfulPacerTest.TickCountMsFollowsTheGrid`: 0, 15, 31, 46 y 62 en los ticks 0 a 4;
  - `FaithfulPacerTest.SixtyFourFramesEveryThreeSeconds`: 64 frames con dibujo de 0 ms terminan
    con el reloj en 3 s exactos (192 ticks), y el frame 65 empieza con 2 pasos: 73 corridos y 80
    ms en el acumulador, los 75 de 3 s a 25 por segundo;
  - `FaithfulPacerTest.SevenStepsEverySixFrames`: los pasos de los primeros 13 frames son 0, 1,
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, y desde el frame 2, cada 6 frames seguidos suman 7 (en los
    primeros 60);
  - `FaithfulPacerTest.EachFrameMeasuresThreeTicks`: cada frame suma 46 o 47 ms;
  - `FaithfulPacerTest.ADrawLongerThanTheWaitAddsItWhole`: con el reloj en el tick 0, un dibujo de
    130 ms suma 125 ms (8 ticks) sin esperar, y el frame siguiente corre 3 pasos;
  - `FaithfulPacerTest.TimeSpentInStepsIsNotAccumulated`: con pasos que tardan 10 ms, el
    acumulador suma solo lo que midió desde el inicio del dibujo (`0x63852b`): en 3 s corren 70
    pasos y no 75, como en el original, que se frena;
  - `FaithfulPacerTest.HasNoCap`: un dibujo de 10 s suma 10 s y corre 250 pasos.
- [ ] **Paso 3:** `FixedStep`, `TickCountMs`, `GameLoop` y `SdlClock`. `main.cpp` arma el bucle
  con la escena de prueba e imprime `frames` y `pasos` al salir.
- [ ] **Paso 4:** 07: el acumulador sin tope del original contra `MaxFrameTime`, con los dos
  tests (`FaithfulPacerTest.HasNoCap` y `GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap`).
- [ ] **Paso 5:** a mano, en Windows y en Linux: `Main --pacing faithful` da 21,33 FPS (que `Main`
  imprima los FPS medidos cada 5 s con `--stats`), y `Main --vsync off` corre a cientos de FPS con
  25 pasos por segundo. Anotarlo en el README.
- [ ] **Paso 6:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "FakeClock|FixedStep|GameLoop|FaithfulPacer"
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60 --vsync off
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
| foco perdido | `WM_ACTIVATE` | se sueltan todas las teclas y botones |
| mouse | `WM_RBUTTON*`, `WM_MBUTTON*`, `WM_MOUSEWHEEL`: cámara y zoom | botones izquierdo, medio y derecho como las teclas; la rueda se suma hasta el paso; la posición en píxeles de la ventana (la de 640 × 480 es de la CL5, D-C11) |

**Archivos:** crear `Client/src/Input/Key.h` (los scancodes que usa el cliente, con nombre, sin
SDL), `InputState.h/.cpp` y `InputEvent.h`; `Client/src/Platform/InputEvents.h/.cpp` (de
`SDL_Event` a `Input::InputEvent`, con `SDL_Scancode` → `Input::Key`); `Client/tests/input/
InputStateTest.cpp` y `Client/tests/platform/InputEventsTest.cpp` (la tabla de scancodes, sin
ventana). Modificar `Client/CMakeLists.txt`, `GameLoop` (consume los flancos después del primer
paso de cada frame), `Platform::PumpEvents`, PROGRESS y este plan.

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
    float Wheel() const;             // lo sumado desde el último ConsumeEdges
    float MouseX() const;
    float MouseY() const;
    // Lo llama GameLoop después de un paso: borra los flancos y la rueda, no lo que está abajo.
    void ConsumeEdges();
};
} // namespace Client::Input
```

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests, la tabla de flancos: para cada
  secuencia (abajo; abajo y arriba; abajo, arriba y abajo; abajo con auto-repeat; foco perdido con
  una tecla abajo), qué dan `IsDown`, `WasPressed` y `WasReleased` antes y después de
  `ConsumeEdges`.
- [ ] **Paso 2:** los tests:
  - `InputStateTest.AHeldKeyIsPressedOnce`: abajo en el frame 1, sigue abajo en el 2 y el 3:
    `IsDown` en los tres, `WasPressed` solo antes del primer `ConsumeEdges`;
  - `InputStateTest.APressAndReleaseInOneFrameIsSeen`: abajo y arriba sin `ConsumeEdges` en el
    medio: `WasPressed` y `WasReleased`, no `IsDown`;
  - `InputStateTest.AutoRepeatIsNotAPress` (el evento lo marca `Platform`; acá, un segundo
    `KeyDown` con la tecla abajo no es flanco);
  - `InputStateTest.FocusLostReleasesEverything`: con una tecla y un botón abajo, `FocusLost` los
    suelta y da `WasReleased`;
  - `InputStateTest.TheWheelAddsUntilConsumed`: +1 y +2 dan 3, y 0 después de `ConsumeEdges`;
  - `InputStateTest.MouseButtonsAreEdgesToo`;
  - `GameLoopTest.APressIsSeenByExactlyOneStep`: un frame de 80 ms (2 pasos) con un apretón: el
    primer paso lo ve, el segundo no;
  - `GameLoopTest.APressInAFrameWithoutStepsWaitsForTheNextStep`: a 240 FPS, un apretón en un
    frame de 0 pasos lo ve el primer paso que venga, aunque la tecla ya se haya soltado;
  - `InputEventsTest.MapsTheScancodes`: `SDL_SCANCODE_A` da `Key::A`, `SDL_SCANCODE_RETURN`
    `Key::Enter`, uno sin usar da `std::nullopt`.
- [ ] **Paso 3:** `InputState`, la traducción en `Platform` y el `ConsumeEdges` en `GameLoop`.
  El `LoopClient` recibe el `InputState` por referencia constante en `Step`.
- [ ] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "InputState|InputEvents|GameLoop"
  ```

## Sub-ola CL3.4: config, línea de comandos y log

**Concepto:** de dónde sale lo que depende del lugar (la carpeta de datos, el servidor, el modo
de video), validado antes de abrir nada, y un log que se puede leer en un test.

**Contrato** (02 § Del launcher al juego, § Qué implica para MuRet; Q-C34 a Q-C37):

| Qué | Original | MuRet |
| --- | --- | --- |
| servidor | compilado (`0x6d5dc0` → host en `0x6c76e0`, puerto 44405 en `0x6d5dc4`) o el del launcher en el registro; FoxAnti pisa el host con `ConnectIp` | `[Server] Host` y `Port` en la config; el puerto por defecto es 44405; sin host, error que lo dice |
| argumentos | no se leen; el parser `0x4b9890` (`/u<ip> /p<puerto>`, letras en mayúscula o minúscula, en cualquier lugar) no lo llama nadie (Q-C36) | `connect /u<ip> /p<puerto>` pisa la config; letras en mayúscula o minúscula; puerto de 1 a 65535; `/u` sin `/p` es error (pregunta 6); `/y` y `/z` no |
| largos | copia la línea de comandos y el host sin mirar el largo (Q-C37) | el host se valida (no vacío, hasta 255 bytes, sin espacios) antes de guardarlo |
| su archivo | se abre a sí mismo desde la carpeta actual y sale sin avisar si no está (Q-C35) | no se abre a sí mismo; `[Data] Path` (o `--data <ruta>`) y, si no existe, un error que la nombra (`Data::DataPath`, CL0.2) |
| config | registro `HKCU\SOFTWARE\Webzen\Mu\Config` y `config.ini` | `muret-client.ini` junto al ejecutable, o `--config <ruta>` (pregunta 5) |
| video | `Resolution` y `ColorDepth` del registro | `[Video] Width`, `Height` (640 × 480 por defecto), `VSync` (`on`, `adaptive`, `off`) y `Pacing` (`decoupled`, `faithful`) |
| log | archivo de texto con líneas como `Mu online %s (%s) executed.` | `Core::Log` con niveles, a `stderr` y a `muret-client.log`; los mensajes de SDL entran por `SDL_SetLogOutputFunction` |

**Archivos:** crear `Client/src/App/Config.h/.cpp` y `ConfigError.h`, `Client/src/Core/Log.h/.cpp`
y `Client/src/Platform/SdlLog.h/.cpp`; `Client/tests/app/ConfigTest.cpp` y
`Client/tests/core/LogTest.cpp`. Modificar `Client/src/App/CommandLine.h/.cpp` y su test (pasa a
`Core::Expected` con un error que se imprime, y suma `connect`, `/u`, `/p`, `--config`, `--data`,
`--pacing`, `--vsync`), `main.cpp`, `Client/CMakeLists.txt` (los tests de `Main` sin ventana),
`Client/README.md` (cómo se configura), 07 (Q-C35 a Q-C37 con sus tests), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::App
{
// El puerto compilado del ConnectServer, 0x6d5dc4 (02 § Del launcher al juego).
inline constexpr std::uint16_t DefaultConnectPort = 44405;

struct ServerAddress { std::string host; std::uint16_t port = DefaultConnectPort; };

struct CommandLine
{
    std::optional<std::uint64_t> maxFrames;
    std::optional<ServerAddress> connect;       // connect /u<ip> /p<puerto>
    std::optional<std::filesystem::path> configPath;
    std::optional<std::filesystem::path> dataPath;
    std::optional<PacingMode> pacing;
    std::optional<VSyncMode> vsync;
};
Core::Expected<CommandLine, std::string> ParseCommandLine(std::span<const std::string_view> arguments);

struct ClientConfig
{
    std::filesystem::path dataPath;
    std::optional<ServerAddress> server;
    int width = 640;
    int height = 480;
    VSyncMode vsync = VSyncMode::On;
    PacingMode pacing = PacingMode::Decoupled;
};
struct ConfigError { std::size_t line = 0; std::string message; };
Core::Expected<ClientConfig, ConfigError> ParseConfig(std::string_view text);
// La config con la línea de comandos encima; valida la carpeta de datos y el servidor.
Core::Expected<ClientConfig, std::string> Resolve(ClientConfig config, const CommandLine& commandLine);
} // namespace Client::App

namespace Client::Core
{
enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };
class LogSink { public: virtual ~LogSink() = default; virtual void Write(LogLevel level, std::string_view line) = 0; };
class Log
{
public:
    Log(LogSink& sink, LogLevel minimum);
    template <typename... Args> void Info(std::format_string<Args...> format, Args&&... args);
    // Debug, Warning y Error igual.
};
} // namespace Client::Core
```

Sin globales (D-C1): el `Log` lo crea `main.cpp` y se pasa por referencia a quien lo use.

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests, la tabla de la línea de comandos:
  qué dan `connect /u10.0.0.1 /p44405`, `CONNECT /U10.0.0.1 /P44405`, `/p44405 /u10.0.0.1` (el
  original busca en cualquier orden), `connect /u10.0.0.1`, `/p0`, `/p65536`, `/p12a`, `/u` vacío
  y `/y...`.
- [ ] **Paso 2:** los tests:
  - `CommandLineTest.AcceptsConnectWithHostAndPort`, `IgnoresTheCase`, `AcceptsAnyOrder`;
    `RejectsAPortOutOfRange` (0 y 65536: "el puerto va de 1 a 65535"), `RejectsAPortWithLetters`,
    `RejectsHostWithoutPort` (pregunta 6), `RejectsAnEmptyHost`, `RejectsTheLauncherOptions` (`/y`
    y `/z`), `RejectsAHostLongerThan255Bytes` (Q-C37), y los de `--frames` que ya están;
  - `ConfigTest.ReadsTheSections`, `DefaultsWhenAKeyIsMissing` (puerto 44405, 640 × 480, VSync
    on, decoupled), `ReportsTheLineOfAnError` (una línea sin `=` en la 3 da `line == 3`),
    `RejectsAnUnknownVSyncValue`;
  - `ConfigTest.TheCommandLineWins`: `connect /u... /p...` pisa `[Server]`, `--pacing faithful`
    pisa `Pacing`;
  - `ConfigTest.AMissingDataFolderNamesIt`: `Resolve` con una carpeta que no existe da un error
    que contiene su ruta (el test del 02 y Q-C35);
  - `LogTest.WritesTheLevelAndTheMessage` (con un `LogSink` de prueba que guarda las líneas),
    `BelowTheMinimumIsNotWritten`, `FormatsTheArguments`.
- [ ] **Paso 3:** `ParseCommandLine`, `ParseConfig`, `Resolve`, `Log` y `SdlLog`. `main.cpp`:
  argumentos, config, log, y recién después SDL y la ventana; el frame recortado por
  `MaxFrameTime` sale en el log como aviso.
- [ ] **Paso 4:** CTest de `Main` sin ventana (corren antes de `SDL_Init`):
  `Client.Main.RejectsBadPort` (`Main connect /u127.0.0.1 /p0`, la salida dice "el puerto va de 1 a
  65535") y `Client.Main.NamesTheMissingDataFolder` (`Main --data <carpeta que no existe>`, la
  salida trae la ruta).
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "CommandLine|Config|Log|Client.Main"
  ```

---

## Qué no se prueba sin ventana, y cómo lo cubre CI

| Qué | Cómo |
| --- | --- |
| el bucle, los dos modos, el input, la config, el log | `ClientTests` con el reloj falso: corren en los dos sistemas |
| el loader, la versión y el intervalo de swap como datos | `ClientPlatformTests`, sin ventana: los dos sistemas |
| abrir la ventana, el contexto 3.3, el swap | el paso de Xvfb con Mesa (llvmpipe) del CI, solo en Linux: `Main --frames 60` con cada modo. En Windows, a mano (pregunta 7) |
| que VSync ata los FPS al monitor y que el modo fiel da 21,33 FPS reales | a mano con `--stats` (CL3.2, paso 5): Xvfb no tiene retrazo, y el tiempo real en CI no es estable |
| minimizar, el foco, el teclado de verdad | a mano; la lógica de cada evento ya la prueban `WindowStateTest` e `InputStateTest` |

## Al cerrar la CL3

- [ ] Completar el cierre de la guía de la CL3.0.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Escribir el plan de la CL4 (red del cliente) en su propio PR, con lo aprendido: el socket no
  bloqueante entra en el `GameLoop` como una consulta por frame, y el keepalive de 20 s como un
  temporizador del bucle, probado con el `FakeClock`.
