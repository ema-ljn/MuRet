# Ola CL3.0: el bucle de juego

La N.0 de la CL3 del cliente. Antes de sacar el hola mundo de `main.cpp` y armar el bucle con la
lógica en pasos fijos y el dibujo interpolado, hace falta saber por qué un paso variable hace que
el juego dependa de los FPS, cómo un acumulador corre pasos de 40 ms con cualquier frame, qué mide
de verdad un reloj, y por qué el `main.exe` dibuja a 21,33 FPS con una espera de 40 ms. Plan
(tiene las respuestas: abrilo después):
[CL3](../superpowers/plans/2026-11-02-ola-cl3-plataforma-y-bucle.md#sub-ola-cl30-el-bucle-de-juego-n0-en-labs).

**Resuelta por Claude** el 2026-11-02, a tu pedido: el código está en
[`labs/cl3.0-bucle/`](../../labs/cl3.0-bucle/), y las respuestas de las 13 actividades, con las
salidas reales, en su [README](../../labs/cl3.0-bucle/README.md). Si la hacés vos, abrilo después
de cada actividad, no antes. Lo que pasó del lab al cliente, en la CL3.2 y la CL3.3, está en
[Del lab al cliente](#del-lab-al-cliente). La sección final de esta guía sigue siendo tuya.

Qué cubre:

- **Teoría:**
  - el bucle de juego (leer la entrada, avanzar la lógica, dibujar), el paso variable y el paso
    fijo con acumulador ("Fix your timestep");
  - la interpolación con `alfa = acumulador / paso`, y por qué no cambia nada de la lógica;
  - relojes: de pared contra monótono, unidad contra resolución, y el tick de 15,625 ms de
    `GetTickCount`;
  - VSync, y por qué ata los FPS al monitor;
  - la espiral de la muerte, y el tope que la corta.
- **C++:**
  - `std::chrono::duration` y sus unidades, `duration_cast` (trunca hacia cero), y duraciones
    enteras contra de punto flotante;
  - `steady_clock::is_steady`, y por qué `system_clock` no sirve para medir un frame;
  - una interfaz virtual chica para inyectar el reloj, y un reloj falso para los tests;
  - la suma en 64 bits para que un `int` no se pase de rango.
- **Lectura:** Glenn Fiedler, "Fix Your Timestep!"; Robert Nystrom, *Game Programming Patterns*,
  el capítulo "Game Loop"; cppreference, `<chrono>`.

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | un salto con `Update(dt)`, a 30 y a 144 FPS |
| 2 | idea | el bucle de juego, el paso variable y el paso fijo con acumulador |
| 3 | predecí | los pasos de un frame de 130 ms, y de 25 frames de 40 |
| 4 | misión | `Labs::FixedStep`, en milisegundos enteros |
| 5 | idea | relojes: de pared y monótono, unidad y resolución |
| 6 | misión | `Labs::TickGrid`, el reloj de `GetTickCount` |
| 7 | detective de números | por qué una espera de 40 ms dura 46,875 |
| 8 | idea | la interpolación y VSync |
| 9 | comparar | ms enteros contra `std::chrono::nanoseconds`; `steady_clock` contra `system_clock` |
| 10 | rompelo | `duration_cast` con un negativo, y un frame que no entra en un `int` |
| 11 | idea | la espiral de la muerte y el tope |
| 12 | jefe | el bucle de `0x637700` sobre una `TickGrid`, y la espiral que no tiene |
| 13 | detective (carril R) | el acumulador en `0x637791`, `0x638167` y `0x638632` |

Cada actividad termina en una pregunta. Las respuestas no están en la guía: salen de lo que
corrés. El plan y el [02](../../Client/docs/research/02-arranque-y-ciclo-de-vida.md) tienen
algunas, así que abrilos después de predecir.

## 1. Calentamiento: un salto a dos FPS

Un personaje salta con velocidad inicial de 10 unidades por segundo, y la gravedad le resta 20 por
segundo. El juego lo mueve en cada frame con `Update(dt)`:

```cpp
void Update(float dt)
{
    y += velocity * dt;
    velocity -= Gravity * dt;
}
```

**Predecí, antes de seguir:**
- ¿llega a la misma altura máxima a 30 FPS (`dt = 1/30`) que a 144 (`dt = 1/144`)?
- si el frame se traba medio segundo (`dt = 0,5`), ¿qué hace el salto?

**Pregunta:** si el servidor espera que el cliente mande un paquete cada 25 pasos de lógica, ¿qué
pasa con un `Update(dt)` que corre una vez por frame?

## 2. Idea: el bucle de juego y el paso fijo

Un juego es un bucle: leer la entrada, avanzar la lógica, dibujar, y otra vez. La pregunta es
**cuánto avanza la lógica en cada vuelta**.

Con un **paso variable**, la lógica avanza lo que tardó el frame (`Update(dt)`). Es simple, pero
las cuentas dependen del `dt`: la integración del salto de la actividad 1 da otra altura con otro
`dt`, una colisión puede saltearse con un `dt` grande, y dos máquinas con otros FPS ven otro
juego.

Con un **paso fijo**, la lógica avanza siempre lo mismo, 40 ms en el MU (25 pasos por segundo). El
bucle **acumula** lo que tardó cada frame y corre pasos mientras el acumulador alcance:

```text
acumulador += lo que tardó el frame
mientras acumulador >= paso:
    Step()                      la lógica, siempre con el mismo paso
    acumulador -= paso
Render()
```

Lo que no llega a un paso queda para el frame siguiente: a 144 FPS, la mayoría de los frames corren
0 pasos y uno de cada cinco o seis corre 1; a 1 FPS, cada frame corre 25. En un segundo, siempre
25. Es el "Fix your timestep" de Fiedler. El original también corre pasos fijos con un acumulador
(02, "El ritmo"), pero mide otra cosa, no tiene tope y no interpola (actividad 12).

**Pregunta:** a 60 FPS, ¿cuántos frames seguidos pueden correr 0 pasos? ¿Y cuántos pasos puede
correr un frame como mucho?

## 3. Predecí: el acumulador a mano

Con pasos de 40 ms y el acumulador en 0:

- un frame de 40 ms; uno de 39; uno de 130;
- 25 frames de 40, y 1 frame de 1.000.

**Predecí** cuántos pasos corre cada uno y qué resto queda.

**Pregunta:** después del frame de 39, viene uno de 1 ms. ¿Cuántos pasos corre? ¿Qué cambiaría si
el resto se tirara al final de cada frame?

## 4. Misión: `Labs::FixedStep`

**El contrato:** `int Advance(int frameMs)` suma un frame en milisegundos enteros y devuelve
cuántos pasos de 40 ms corren; `RestMs()` dice lo que quedó. Va en `labs/cl3.0-bucle/`.

**Te toca**, en este orden:

1. Los tests primero (`test_fixed_step.cpp`): los cinco casos de la actividad 3 y el resto que
   pasa al frame siguiente. Antes de compilar, decí qué etapa va a fallar.
2. `FixedStep.h` y `FixedStep.cpp`, primero en rojo y después en verde.

**Decisiones que pesás vos:**
- ¿qué hacés con un frame negativo? (Un reloj real no vuelve para atrás, pero uno falso sí.)
- ¿corrés los pasos con un `while` o con una división? ¿Dan lo mismo?
- ¿se puede pasar de rango la suma del resto y el frame? ¿Con qué `frameMs`?

## 5. Idea: qué mide un reloj

Hay dos clases de relojes:

- **De pared** (`std::chrono::system_clock`, `GetSystemTimeAsFileTime`): la hora del calendario.
  Puede saltar para adelante o para atrás, porque alguien la cambia a mano o porque NTP la corrige.
  Sirve para poner una fecha en el log, no para medir cuánto tardó algo.
- **Monótono** (`std::chrono::steady_clock`, `QueryPerformanceCounter`, `SDL_GetTicksNS`): no
  vuelve para atrás nunca. No dice qué hora es, pero la diferencia entre dos lecturas es lo que
  pasó. Es el que mide un frame.

Y dos números que se confunden:

- la **unidad** (el período de la representación) es en qué cuenta el reloj su valor:
  `GetTickCount` da milisegundos, y `steady_clock` en libstdc++, nanosegundos. En `<chrono>` es el
  `period` del reloj;
- la **resolución** (la granularidad) es cada cuánto cambia ese valor de verdad. `GetTickCount` da
  ms, pero se actualiza con el tick del reloj del sistema: 15,625 ms (64 por segundo) en la VM XP y
  en un Windows con el tick por defecto. Entre dos ticks lee siempre lo mismo. Así la usan
  Microsoft, que dice que la resolución de `GetTickCount` es la del reloj del sistema, y POSIX, con
  `clock_getres`.

La **precisión** es otra cosa: cuánto se parecen dos medidas de lo mismo. Acá no hace falta.

**Pregunta:** un reloj que da nanosegundos, ¿cambia de a un nanosegundo? ¿Cómo averiguarías su
resolución?

## 6. Misión: `Labs::TickGrid`

**El contrato:** con `tick` en microsegundos (15.625 o 10.000), `Ms(t)` es lo que leería
`GetTickCount` en el microsegundo `t`: el último tick entero, pasado a ms truncados,
`(t / tick) * tick / 1000`, en enteros. Va en `labs/cl3.0-bucle/`.

**Te toca**, tests primero:

1. Con 15.625, `Ms` en los ticks 0 a 4. **Predecí** los cinco valores antes de correrlo.
2. Con 10.000, lo mismo.
3. Que entre dos ticks no se mueve: un microsegundo antes del tick 1, y uno antes del 2.
4. La implementación.

**Decisiones que pesás vos:**
- ¿en qué unidad guardás el tiempo del modelo, para que 15,625 ms sea exacto?
- `GetTickCount` devuelve un `DWORD`. ¿De qué tipo es tu `Ms`, y qué pasa a los 2³² ms?

**Pregunta:** el tick 1 lee 15 y el 64 lee 1.000. ¿Por qué la truncación de cada tick no se
acumula?

## 7. Detective de números: 40 ms que duran 46,875

La espera del original (02, "El ritmo") es activa: lee `GetTickCount` al empezar el dibujo y gira
mientras la diferencia sea menor que 40.

**Calculá a mano**, con la `TickGrid` de 15,625 ms y empezando en el tick 0:
- ¿en qué tick termina la espera, y cuántos ms midió?
- el frame siguiente empieza ahí. ¿En qué tick termina, y cuántos ms midió esta vez?

**Pregunta:** ¿cuántos frames por segundo da eso? ¿Y con un tick de 10 ms?

## 8. Idea: interpolar, y VSync

Con el paso fijo, el dibujo casi nunca cae justo en un paso: a 60 FPS, entre dos pasos hay frames
con 0 pasos que dibujarían lo mismo, y el movimiento se ve a saltos de 40 ms. La **interpolación**
guarda el estado de los dos últimos pasos y dibuja uno intermedio, con

```text
alfa = acumulador / paso            en [0, 1)
dibujado = anterior * (1 - alfa) + actual * alfa
```

La lógica no ve el alfa: los pasos son los mismos, en el mismo orden, con o sin interpolación. Lo
que cambia es solo lo que se dibuja, que va un paso atrás de la lógica.

**VSync** hace que el swap espere el retrazo vertical del monitor: con 60 Hz, el swap vuelve cada
16,67 ms y el bucle no puede ir más rápido. Ata los FPS al monitor y evita el *tearing*; sin
VSync el bucle gira lo más rápido que puede. El original no lo pide (`0x69e830` y `0x69e860` no
se llaman): sus 21,33 FPS salen de la espera.

**Pregunta:** a 60 Hz con VSync y pasos de 40 ms, ¿qué alfas ve el dibujo en los frames que hay
entre dos pasos? ¿Puede valer 1 el alfa?

## 9. Comparar: los ms enteros, `std::chrono`, y los dos relojes

L-3 del camino: se construye desde cero, se compara y en el producto se usa el estándar. El
`FixedStep` de la CL3.2 usa `std::chrono::nanoseconds` (D-C4 del roadmap): escribí uno igual al de
la actividad 4, con `int Advance(std::chrono::nanoseconds frame)`, y pasale a los dos los mismos
60 frames de un segundo a 60 FPS (16.666.666 o 16.666.667 ns cada uno), al de ms truncando cada
frame con `duration_cast<milliseconds>`.

**Te toca:** anotarlo en `labs/README.md`.

**Preguntas:**
- ¿cuántos pasos da cada uno en ese segundo, y por qué? El original también suma `int` de ms: ¿por
  qué él no pierde nada?
- `steady_clock` contra `system_clock`: ¿cuál puede ir para atrás, y cuándo? ¿Qué dice
  `is_steady` de cada uno?

## 10. Rompelo: `duration_cast` y un `int` que se pasa

```cpp
using namespace std::chrono;
const auto a = duration_cast<milliseconds>(nanoseconds{1'999'999});
const auto b = duration_cast<milliseconds>(nanoseconds{-1'999'999});
```

**Predecí** `a.count()` y `b.count()` antes de imprimirlos. Después, en tu `FixedStep` de la
actividad 4, cambiá la suma a `int` y llamá `Advance(39)` y `Advance(INT_MAX)` con
`-fsanitize=address,undefined`.

**Preguntas:**
- ¿`duration_cast` redondea, trunca hacia abajo o trunca hacia cero? ¿Qué funciones de `<chrono>`
  hacen lo otro?
- ¿qué dice UBSan del `int`, y qué devuelve `Advance` sin sanitizers?
- `std::chrono::duration<double, std::milli>` recibe un `nanoseconds` sin cast, y un
  `milliseconds` entero no. ¿Por qué?

## 11. Idea: la espiral de la muerte

Un paso fijo tiene una trampa. Si un paso tarda en correr **más que el paso que simula** (40 ms de
lógica que tardan 50 ms de CPU), el frame que corre 2 pasos tarda 100 ms, así que el siguiente
debe 2,5 pasos: corre 2 y guarda 20 ms. El de después debe 3 y tarda 150, y el siguiente debe más.
Cada frame debe más que el anterior: la **espiral de la muerte**. El juego se congela corriendo
pasos atrasados.

Lo mismo pasa una vez sola después de un breakpoint de 10 s: el frame siguiente debe 250 pasos.
Por eso los bucles modernos ponen un **tope** al frame (la pregunta 2 del plan: `MaxFrameTime =
1 s`, 25 pasos como mucho): lo que pasa del tope se pierde, y el juego se atrasa en vez de
espiralar.

**Pregunta:** con el tope de 1 s y pasos de 50 ms, ¿el juego espirala? ¿Qué se ve en la pantalla?

## 12. Jefe: el bucle de `0x637700`, y la espiral que no tiene

El original tiene el acumulador sin tope (la tabla "Lo que dice el binario del bucle" del plan),
pero mide distinto: empieza a medir **después** de los pasos y del `glClear` y el viewport
(`0x638516` y `0x63851c`), justo antes de la escena (`0x63852b`), así que el acumulador suma solo
el dibujo, el swap y la espera. Y no arranca en 0: en el `.data` del binario vale 40 (lo vas a
leer en la actividad 13).

**Te toca** (`labs/cl3.0-bucle/jefe/test_original_loop.cpp`):

1. Un modelo del bucle sobre tu `TickGrid`: el acumulador en 40 al empezar, pasos mientras valga
   40 o más (cada uno resta 40), `inicio = Ms(ahora)`, el dibujo (que tarda lo que diga el test),
   la espera activa de 40 ms avanzando el tiempo de a 1 µs, y el acumulador suma
   `Ms(ahora) - inicio`. La resta es de `DWORD`, como en el original, y la comparación con signo.
   Que el reloj pueda empezar cerca de los 2³² ms, para probar la vuelta.
2. Con 15.625 µs: **predecí** cuánto tardan 64 frames y los pasos de los primeros 13, y escribilo
   como test. Con 10.000 µs, los FPS y los pasos por frame.
3. "7 pasos cada 6 frames", dice el 02. Comprobalo en los primeros 300 frames: ¿cuánto suman 64
   frames seguidos, y 6?
4. La espiral: pasos que tardan 50 ms, frames mientras el reloj no llegue a 10 s. **Predecí**
   cuántos frames y cuántos pasos corren. Al lado, tu `FixedStep` midiendo el frame entero y sin
   tope: el primer frame mide 40 ms y cada uno de los siguientes, lo que tardaron los pasos del
   anterior. Anotá los pasos de los primeros 15 frames.

**Pregunta:** ¿por qué el original no espirala con los pasos lentos, y qué paga a cambio? ¿Qué
hace con un breakpoint de 10 s en el dibujo?

## 13. Detective (carril R): el acumulador en el binario

El bucle está en el repo de research,
`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/00637700_FUN_00637700.asm`, y en
Ghidra, abriendo `mainexe-vmxp-c32bbd5f/original/main.exe` (o con `objdump -d -M intel
--start-address=...`). Los tres lugares que tocan el acumulador:

```text
637791 cmp dword ptr [0x6d5ee4],0x28
637798 jl  0x6381aa
638162 mov eax,[0x6d5ee4]
638167 sub eax,0x28
63816a mov [0x6d5ee4],eax
63862c mov edx,[0x6d5ee4]
638632 add edx,[ebp-0x168]
638638 mov [0x6d5ee4],edx
```

**Preguntas:**
- `JL` contra `JB`: ¿qué dice de cómo trata el procesador al acumulador?
- ¿adónde salta el bucle después del `SUB`, y hay alguna comparación con un tope entre el `SUB` y
  el `CMP`?
- ¿qué otra instrucción de `.text` nombra `0x6d5ee4`? ¿Y cuánto vale en el `.data` del archivo,
  antes del primer frame?

## El C++ que entra

- **`std::chrono::duration<Rep, Period>`:** un número (`Rep`) y su unidad (`Period`, una
  `std::ratio`). `milliseconds` y `nanoseconds` son enteros de al menos 45 y 64 bits; una
  conversión que no pierde nada (de ms a ns) es implícita, y una que trunca (de ns a ms) pide
  `duration_cast`.
- **`duration_cast` trunca hacia cero:** -1,999999 ms da -1 ms. `std::chrono::floor`, `ceil` y
  `round` (C++17) hacen lo otro.
- **Enteras contra de punto flotante:** con `Rep = double`, la conversión desde cualquier unidad
  es implícita, porque se supone que no trunca; pero un `double` pierde nanosegundos cuando el
  número crece, y sumar `0.1` diez veces no da `1.0`. Por eso el bucle de la CL3.2 acumula en
  `nanoseconds` enteros y pasa a `float` solo el alfa.
- **Duración entre duración:** `rest / Step` da un número sin unidad, y `rest % Step`, una
  duración.
- **`steady_clock::is_steady`:** un `static constexpr bool`. Es `true` en `steady_clock` por el
  estándar; en `system_clock`, el estándar no lo fija, y en la práctica es `false`.
- **Una interfaz para inyectar el reloj:** una clase con funciones virtuales puras y destructor
  virtual. El bucle recibe una referencia a la interfaz; `main` le pasa el reloj real, y los tests,
  uno falso que avanza lo que el test dice (ejemplo abajo).
- **La suma en 64 bits:** `std::int64_t{a} + b` convierte antes de sumar; `a + b` en `int` puede
  pasarse, y eso es comportamiento indefinido.

## Ejemplo mínimo (fuera del proyecto)

Un reloj inyectado y uno falso, como los de la CL3.2:

```cpp
class IClock
{
public:
    virtual ~IClock() = default;
    virtual std::chrono::nanoseconds Now() = 0;
};

class FakeClock final : public IClock
{
public:
    std::chrono::nanoseconds Now() override { return m_now; }
    void Advance(std::chrono::nanoseconds d) { m_now += d; }

private:
    std::chrono::nanoseconds m_now{0};
};

int StepsOfTheNextFrame(IClock& clock, std::chrono::nanoseconds& last, ChronoFixedStep& step)
{
    const auto now = clock.Now();
    const int steps = step.Advance(now - last);
    last = now;
    return steps;
}
```

**Pregunta:** con este `FakeClock`, ¿cómo escribís un test de "un frame de 130 ms corre 3 pasos"
sin esperar 130 ms de verdad? ¿Y uno de un reloj que vuelve 5 ms para atrás?

## Errores típicos

- Mover la lógica con `Update(dt)` y creer que da lo mismo a cualquier FPS (actividad 1).
- Tirar el resto del acumulador al final de cada frame (actividad 3).
- Medir un frame con `system_clock`.
- Confundir la unidad de un reloj con su resolución (actividad 5).
- Truncar cada frame a ms antes de sumarlo (actividad 9).
- Esperar que `duration_cast` redondee (actividad 10).
- Medir los pasos con el frame y no ponerle un tope (actividad 11).
- Dibujar con un alfa que puede valer 1: el estado se dibujaría dos veces igual, al final de un
  paso y al principio del siguiente.

## Cómo lo hacía el original

- Corría la lógica en pasos fijos de 40 ms con un acumulador `int` de ms, sin tope
  (`0x637791`..`0x638167`), que arrancaba en 40 (`.data`): el primer frame ya corría un paso.
- Medía con `GetTickCount` desde después de los pasos (`0x63852b`): los pasos no se acumulan, así
  que unos pasos lentos frenan el juego en vez de espiralar.
- Esperaba activamente hasta que `GetTickCount` avanzara 40, y con el tick de 15,625 ms eso era el
  tercer tick: 46,875 ms por frame, 21,33 FPS, y 75 pasos cada 64 frames.
- No pedía VSync, y no interpolaba: dibujaba el estado después de los pasos.

## Para leer

- Glenn Fiedler, "Fix Your Timestep!" (gafferongames.com): el acumulador, el alfa y la espiral.
- Robert Nystrom, *Game Programming Patterns*, "Game Loop" (gameprogrammingpatterns.com).
- La documentación de `GetTickCount` (Microsoft Learn): su resolución es la del reloj del sistema,
  de 10 a 16 ms (la resolución de la actividad 5).
- cppreference: `std::chrono::duration`, `duration_cast`, `floor`/`ceil`/`round`, `steady_clock`,
  `system_clock` e `is_steady`.
- El [ADR 0001 de MuMain](https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md):
  un bucle moderno que conserva el paso del original.

## Preguntas para chequearte

1. ¿Por qué un paso variable hace que el juego dependa de los FPS, y qué arregla el acumulador?
2. ¿Qué es el alfa de la interpolación, y por qué no cambia nada de la lógica?
3. ¿Qué diferencia hay entre un reloj de pared y uno monótono? ¿Y entre su unidad y su resolución?
4. ¿Por qué la espera de 40 ms del original dura 46,875 ms, y cuántos pasos corren cada 64 frames?
5. ¿Qué es la espiral de la muerte? ¿Por qué el original no la tiene, y por qué MuRet sí la tendría
   sin tope?
6. ¿Qué hace `duration_cast` con -1,5 ms pasados a ms? ¿Por qué el bucle acumula en
   `nanoseconds` enteros y no en `double`?
7. ¿Cómo se prueba un bucle con tiempo sin esperar el tiempo?

## Del lab al cliente

El lab modela el bucle en `labs/`, con ms enteros y un reloj de a microsegundos. La CL3.2 lo
llevó a `Client/src/App/` en dos piezas que conviven: `FixedStep` y `GameLoop` en el modo normal
(`PacingMode::Decoupled`), que es el "Fix your timestep" de la actividad 2 con un tope, y
`FaithfulPacer`, que es tu jefe de la actividad 12 sobre el reloj del cliente
(`PacingMode::Faithful`, `--pacing faithful`). Las constantes de los dos están en
`App/LoopTiming.h`: `LogicStep` (40 ms), `MaxFrameTime` (1 s), `FaithfulTick` (15.625 µs),
`FaithfulWaitMs` y `FaithfulInitialAccumulatorMs` (40 y 40). Las dos corren con el mismo
`Core::IClock`: el de SDL en `Main` (`Platform::SdlClock`) y el `FakeClock` de
`Client/tests/support/` en los tests, el del ejemplo mínimo con una cosa más, que anota cada
`SleepFor` (`Sleeps()`) y hace fallar el test si el bucle pide dormir 0 o menos.

| En el lab | En el cliente | Qué cambió |
| --- | --- | --- |
| `Labs::FixedStep`, `int` de ms (actividad 4) | `App::FixedStep`, `Core::Duration` (nanosegundos) | el frame no se trunca a ms: es el `ChronoFixedStep` de la actividad 9, que daba 25 pasos donde el de ms daba 24. Un frame negativo sigue contando 0, y uno de más de `MaxFrameTime` (1 s) cuenta 1 s (`LastFrameWasClamped`); uno de 1 s justo no se recorta |
| el alfa de la actividad 8 | `FixedStep::Alpha()` | `Accumulated() / LogicStep` en `double`, pasado a `float` y recortado a `std::nextafter(1.0f, 0.0f)`: con 39.999.999 ns el `float` redondearía a 1 (el último error típico de esta guía) |
| el pseudocódigo de la actividad 2 | `GameLoop::RunFrame` en `Decoupled` | mide de un `Now()` al siguiente (el primer frame solo fija el inicio), corre los pasos que tocan, y dibuja con el alfa si la ventana no está minimizada; minimizada, duerme lo que falta para el próximo paso en vez de girar |
| `Labs::TickGrid`, `Ms(t)` en µs (actividad 6) | `App::TickCountMs(now)` | el mismo `(t / tick) * tick` truncado a ms, sobre `Core::Duration` y de a `FaithfulTick`, en un `std::uint32_t` que da la vuelta como el `DWORD`. Con un `now` negativo (solo el `FakeClock`) redondea hacia abajo |
| el jefe, `jefe/test_original_loop.cpp` (actividad 12) | `App::FaithfulPacer`, y `GameLoop` en `Faithful` | el mismo bucle partido en tres: `BeginFrame` (los pasos, mientras el acumulador valga 40 o más), `StartMeasure` (el `inicio` de `0x63852b`) y `EndFrame` (la espera y la suma de `0x638632`). El orden lo pone `GameLoop::RunFaithful`: los pasos, `StartMeasure`, el dibujo y `EndFrame`. Arranca en 40, como el `.data` (actividad 13), no tiene tope y no interpola: el alfa es 0. El acumulador es un `std::int32_t` que da la vuelta como el `ADD` |
| la espera activa, de a 1 µs | `EndFrame` con `IClock::SleepFor` | duerme de tick en tick (hasta el comienzo del próximo) mientras la resta, con signo como el `JGE`, dé menos de 40: en vez de girar, se despierta cuando `GetTickCount` cambiaría. `SdlClock` duerme con `SDL_DelayPrecise`, que espera activamente el último tramo: un despertar tarde con `sleep_for` podría caer en el tick siguiente |
| la actividad 8, VSync | `Platform::ApplySwapInterval` y `--vsync` | `on` por defecto en el modo normal (si `adaptive` falla, prueba con `on`); en el fiel, solo `off`, como el original: con VSync, la espera de 46,875 ms se engancharía a la grilla del monitor |

Los números que sacaste en el lab son los tests del cliente:

| Actividad | Test del cliente |
| --- | --- |
| 3: 40, 39 y 130 ms | `FixedStepTest.FortyIsOneStep`, `ThirtyNineIsNone` y `OneHundredThirtyIsThree` |
| 2: el paso fijo, a cualquier FPS | `GameLoopTest.TwentyFiveStepsPerSecondAtAnyFrameRate`, a 1, 25, 60, 144 y 240 FPS |
| 6: la grilla de `GetTickCount` | `FaithfulPacerTest.TickCountMsFollowsTheGrid` |
| 8: el alfa | `FixedStepTest.AlphaIsTheFractionLeft`, `AlphaStaysBelowOne` y `GameLoopTest.AlphaGrowsBetweenSteps` |
| 10: un frame negativo | `FixedStepTest.ANegativeFrameCountsZero` y `GameLoopTest.AClockThatGoesBackRunsNoSteps` |
| 11: el tope | `FixedStepTest.TheCapIsOneSecond` y `GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap`: 10 s de breakpoint dan 25 pasos, no 250 (Q-C44) |
| 12: 64 frames en 3 s | `FaithfulPacerTest.SixtyFourFramesEveryThreeSeconds`: 74 pasos corridos, los 75 de 3 s más el del valor inicial, menos los 80 ms que quedan en el acumulador |
| 12: los primeros 13 frames, y 75 cada 64 | `FaithfulPacerTest.SeventyFiveStepsEverySixtyFourFrames`: `1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2` |
| 12: la espiral que no tiene | `FaithfulPacerTest.TimeSpentInStepsIsNotAccumulated`: con pasos de 10 ms, 61 frames y 71 pasos en 3 s, y no 64 y 74 |
| 12: un dibujo de 10 s | `FaithfulPacerTest.HasNoCap`: 250 pasos en el frame siguiente |
| el ejemplo mínimo: 130 ms sin esperar | `GameLoopTest.ASlowFrameCatchesUpWithThreeSteps`, con el alfa en 0,25 |

Lo que el lab no tenía, y llegó con el cliente:

- **La ventana minimizada.** El original sin foco no corre el frame (Q-C46). MuRet sigue con sus
  pasos, porque el keepalive de la CL4 no puede parar, y deja de dibujar. En el modo normal
  duerme lo que falta para el próximo paso (`GameLoopTest.AMinimizedWindowStepsWithoutDrawing` y
  `AMinimizedWindowSleepsWhatIsLeftOfTheStep`); en el fiel, la espera sigue, porque sin ella el
  acumulador no suma y no corre nunca más un paso (`FaithfulPacerTest.MinimizedKeepsTheWait`).
- **Los relojes raros.** La resta de la espera es con signo, como el `JGE`: con un reloj que
  vuelve para atrás da negativa y la espera sigue hasta 40 ms después del inicio, en vez de salir
  enseguida (`FaithfulPacerTest.AClockThatGoesBackKeepsWaiting`). Por ese signo, un dibujo de más
  de 24,8 días cuelga el original, y a MuRet también (el comentario de `EndFrame`). La vuelta de
  `GetTickCount` a los 49,7 días no cambia nada (`TheGetTickCountWrapChangesNothing`), y el
  acumulador da la vuelta como el `ADD` (`TheAccumulatorWrapsLikeTheAdd`).
- **La entrada, en un frame con 0 o 2 pasos** (CL3.3). La actividad 2 lee la entrada una vez
  por frame, pero los pasos son 0, 1 o más. `InputState` separa "está abajo" (`IsDown`) de los
  flancos (`WasPressed`, `WasReleased`), que valen hasta el próximo `ConsumeEdges`, y `GameLoop`
  lo va a llamar después del primer paso del frame: con 2 pasos, el apretón lo ve uno solo; con
  0, espera al próximo paso, aunque la tecla ya esté arriba.
  Así quedó: `RunFrame(state, input)` le pasa `const Input::InputState&` a cada `Step` y llama a
  `ConsumeEdges` después del primero (`GameLoopTest.APressIsSeenByExactlyOneStep` y
  `APressInAFrameWithoutStepsWaitsForTheNextStep`).
- **La lógica que lee la hora.** La CL3.R encontró que las demoras de las habilidades y un freno
  de 300 ms avanzan por reloj aunque corran en el paso. Por eso el `IClock` del bucle es el mismo
  que le llega a la lógica desde `Main`: en la CL3 no lo usa nada, pero la CL7 lo va a necesitar,
  y en el modo fiel tendrá que leerlo sobre la misma grilla.

**Para comprobarlo:**

1. Corré los dos juntos y compará los nombres de la tabla de arriba con lo que ves:
   `ctest --preset linux-x64-debug -R "LabCl30|FixedStep|GameLoop|FaithfulPacer"`.
2. Con `Main --pacing faithful --stats` y `Main --vsync off --stats` (el README del cliente dice
   cómo). **Predecí** antes los FPS y los pasos por segundo de cada uno. ¿Cuál de los dos números
   depende de tu monitor, y cuál de tu máquina?
3. En `GameLoop::RunFaithful` (`Client/src/App/GameLoop.cpp`), mové `m_pacer.StartMeasure` antes
   de `m_pacer.BeginFrame()` y corré `FaithfulPacerTest.TimeSpentInStepsIsNotAccumulated`. ¿Qué
   número da, y qué actividad del lab lo predecía?

## Al cerrar la CL3.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Qué hace mi `FixedStep` con un frame negativo y con uno enorme, y por qué:**
- **Qué vi en el jefe: el ritmo del original, y la espiral que no tiene:**
- **Lo que anoté al comparar los ms enteros con `std::chrono`:**
- **Qué cambió del `FixedStep` del lab al de `App` (el tope, el alfa y los ns), y por qué:**
- **Qué vi con `Main --pacing faithful --stats` y `--vsync off --stats` en mi máquina:**
- **Preguntas que me quedaron para más adelante:**
