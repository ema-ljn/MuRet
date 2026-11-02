# CL3.0: el bucle de juego, resuelta

Ejemplo trabajado de la [guía de la CL3.0](../../docs/learning/ola-cl3.0-el-bucle-de-juego.md). Lo
resolvió Claude porque Emanuel pidió, el 2026-11-02, que Claude escriba toda la CL3 hasta que él
diga que participa. Las salidas son reales, con GCC 13.3 y Clang 18 en Linux, y con GCC y
`-fsanitize=address,undefined`. Windows y MSVC, sin probar acá (los corre el CI).

Para cerrar la CL3.0 en el [cuaderno](../../docs/learning/PROGRESS.md) falta lo tuyo, cuando
entres: contar con tus palabras las "Preguntas para chequearte" de la guía y completar su sección
final.

| Archivo | Qué es |
| --- | --- |
| `FixedStep.h` / `.cpp` | la misión de la actividad 4: el paso fijo en ms enteros, y el `ChronoFixedStep` de la 9 |
| `TickGrid.h` / `.cpp` | la misión de la actividad 6: lo que lee `GetTickCount` con un tick de 15,625 o 10 ms |
| `test_*.cpp` | sus tests, de GoogleTest |
| `jefe/test_original_loop.cpp` | el jefe: un modelo del bucle de `0x637700`, su ritmo y la espiral que no tiene |

Las dos misiones van en la biblioteca `MuRetLabsCl30`, y los tests en `LabCl30Tests`, con el
prefijo `LabCl30.` en CTest. Son 30:

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug -R "LabCl30"
```

## 1. Calentamiento

Con el `Update(dt)` de la guía (posición primero, velocidad después), la altura máxima es 2,667 a
30 FPS y 2,535 a 144; la de verdad, `v² / 2g`, es 2,5. Con `dt = 0,5` el salto sube 5, el doble, y
cae en el tercer frame. **Respuesta.** Un paquete cada 25 pasos sería uno cada 25 frames: a 144
FPS, casi 6 por segundo, y a 30, uno y pico. El servidor vería otro cliente según la máquina.

## 2. El bucle

**Respuesta.** A 60 FPS un frame dura 16,67 ms: dos seguidos suman 33,3 y no llegan a un paso,
tres suman 50 y sí. Así que hay como mucho 2 frames seguidos sin pasos. Y como el resto es menor
que 40, un frame corre como mucho 1 paso: 39,99 + 16,67 no llega a 80.

## 3. El acumulador a mano

| Frame | Pasos | Resto |
| --- | --- | --- |
| 40 | 1 | 0 |
| 39 | 0 | 39 |
| 130 | 3 | 10 |
| 25 de 40 | 25 | 0 |
| 1 de 1.000 | 25 | 0 |

**Respuesta.** Después del de 39, uno de 1 ms corre 1 paso (`TheRestCarriesOver`). Tirando el
resto, un juego a 25,6 FPS (frames de 39 ms) no correría nunca un paso.

## 4. La misión: `FixedStep`

Antes de las implementaciones, con los dos `.cpp` vacíos, compilan los tests y falla el linker:
`undefined reference to 'Labs::FixedStep::Advance(int)'`, y lo mismo para cada función de
`FixedStep`, `ChronoFixedStep` y `TickGrid`.

**Las decisiones:**

- **Un frame negativo cuenta 0** (`ANegativeFrameCountsZero`): el resto queda como estaba. Es lo
  que hace la CL3.2 con un reloj que vuelve para atrás; el original no lo necesita, porque su
  medición es una resta de `GetTickCount`, que no vuelve.
- **Una división y no un `while`.** Dan lo mismo, pero el `while` con un frame enorme da 53 millones
  de vueltas, y la división, una.
- **La suma en 64 bits.** `restMs` es menor que 40, así que `restMs + frameMs` se pasa de un `int`
  solo con `frameMs` mayor que `INT_MAX - 39`. Con `std::int64_t{restMs} + frameMs` no se pasa, y
  la cantidad de pasos, que es esa suma dividida por 40, vuelve a entrar en un `int`
  (`TheLargestFrameDoesNotOverflow`: 39 y después `INT_MAX` dan 53.687.092 pasos y resto 6).
  `-1` y `0` desde el resto en 0 no corren nada y lo dejan en 0 (`MinusOneAndZeroFromRestZero`).
- **El mismo cuidado en `ChronoFixedStep`.** `nanoseconds` llega a unos 292 años, pero
  `rest + frame` se pasa con `nanoseconds::max()` y 1 ns de resto (UBSan lo marca en `<chrono>`),
  y la cantidad de pasos no entra en un `int` pasados los `INT_MAX` pasos, unos 994 días. El frame
  se recorta a `INT_MAX` pasos antes de sumar: una hora son 90.000 pasos
  (`OneHourIsNinetyThousandSteps`), y `nanoseconds::max()` cuenta `INT_MAX` y deja el resto como
  estaba (`TheLargestFrameCountsIntMaxSteps`).

## 5. Qué mide un reloj

**Respuesta.** No: los nanosegundos son la unidad, no la resolución. La resolución se averigua
leyendo el reloj en un bucle y mirando la diferencia más chica entre dos lecturas distintas. En este
entorno, con `steady_clock` y sin sanitizers, 43 ns: el costo de leerlo. `GetTickCount`, en cambio,
da 0 hasta que pasa un tick entero.

## 6. La misión: `TickGrid`

`Ms` en los ticks 0 a 4 da 0, 15, 31, 46 y 62 con 15.625 µs, y 0, 10, 20, 30 y 40 con 10.000.

**Las decisiones:**

- **El tiempo en microsegundos enteros** (`std::int64_t`): 15,625 ms son 15.625 µs, exactos.
- **`Ms` devuelve `std::uint32_t`, el `DWORD`:** da la vuelta a los 2³² ms (49,7 días), y el cast
  está definido, módulo 2³². La resta de dos lecturas, también en `std::uint32_t`, sigue dando la
  diferencia correcta después de la vuelta; por eso el jefe resta en `DWORD` y recién después
  compara con signo, como el original. El tick 274.877.906 lee 4.294.967.281, el siguiente lee 0,
  y del primero al tercero después la resta da 47 (`TheReadingWrapsAroundLikeADword`); 64 frames
  que cruzan la vuelta siguen tardando 3 s (`TheGetTickCountWrapChangesNothing`).

**Respuesta.** Cada lectura se calcula desde el tiempo total (`(t / tick) * tick / 1000`), no
sumando 15 por tick: el error de truncar es siempre menor que 1 ms y no crece. El tick 64 lee
1.000 (`SixtyFourXpTicksAreOneSecond`).

## 7. 40 ms que duran 46,875

Desde el tick 0, que lee 0: el tick 2 lee 31 (no llega) y el 3 lee 46, así que la espera termina
en el tick 3 y mide 46. El frame siguiente empieza en 46: el tick 5 lee 78 (32) y el 6, 93 (47).
Termina en el tick 6 y mide 47. **Respuesta.** Tres ticks por frame: 46,875 ms, 64 / 3 = 21,33
FPS, y la medición da 46 una vez y 47 siete veces cada 8 frames (8 frames son 375 ms, 46,875 de
media; `EachFrameWaitsThreeTicksAndMeasures46Or47`). Con un tick de 10 ms, la espera termina en el
cuarto tick, a los 40 ms justos: 25 FPS.

## 8. Interpolar, y VSync

**Respuesta.** A 60 Hz, con frames de 16,67 ms, los alfas van 0,417, 0,833, (paso) 0,25, 0,667,
(paso) 0,083, 0,5, 0,917, (paso) 0,333..., y se repiten cada 12 frames, que son 5 pasos: 200
ms. Con la cuenta exacta no llega a 1, porque después de los pasos el
acumulador es menor que el paso; en `float` sí puede: 39.999.999 / 40.000.000 redondea a `1.0f`, y
por eso la CL3.2 lo recorta a `std::nextafter(1.0f, 0.0f)`.

## 9. Comparar

Está en [`labs/README.md`](../README.md#cl30-ms-enteros-contra-stdchrono-y-los-dos-relojes), y
el test es `ChronoFixedStepTest.TruncatingEachFrameToMillisecondsLosesAStep`: 60 frames que suman 1
s dan 24 pasos truncando cada frame a ms, y 25 en nanosegundos.

## 10. Rompelo

`duration_cast` da `a = 1` y `b = -1`: trunca hacia cero. `std::chrono::floor` da -2 con el
negativo, y `std::chrono::round` lleva 1,5 ms a 2 (redondea al par).

Con la suma en `int`, `Advance(39)` y `Advance(INT_MAX)`, GCC 13.3 y
`-fsanitize=address,undefined`:

```text
a.cpp:8:49: runtime error: signed integer overflow: 39 + 2147483647 cannot be represented in type 'int'
steps=0 -53687090 rest=-10
```

**Respuestas:**

- Sin sanitizers sale lo mismo, `-53687090` pasos y resto `-10`: la suma dio la vuelta a un
  negativo y la división truncó hacia cero. El estándar no promete ese número, porque el desborde
  con signo es comportamiento indefinido; es el que dejó el hardware.
- `duration<double, std::milli>` tiene `Rep` de punto flotante
  (`std::chrono::treat_as_floating_point`), y la conversión a una así es implícita porque se
  supone que no trunca. A `milliseconds` entero, desde `nanoseconds`, truncaría: hace falta
  `duration_cast`, y es a propósito.

## 11. La espiral

**Respuesta.** Con el tope de 1 s el juego deja de espiralar, pero se atrasa: cada frame mide más
de 1 s, cuenta 1 s y corre 25 pasos, que tardan 1,25 s. Son 20 pasos por segundo, el 80 % de la
velocidad, y la pantalla cambia una vez cada 1,25 s. Lo que pasa del tope se pierde y el log lo
avisa (la pregunta 2 del plan).

## 12. El jefe

El modelo (`OriginalLoop`) es el bucle de `0x637700` sobre una `TickGrid`, con el tiempo en µs y la
espera activa de a 1 µs. El acumulador arranca en 40, como en el `.data` del binario (actividad
13); los pasos y el dibujo tardan lo que diga el test, y el reloj puede empezar en cualquier hora.
Deja afuera la salida de `0x6381aa`, que vuelve antes de medir cuando `WM_DESTROY` ya marcó el
cierre.

| Test | Qué da |
| --- | --- |
| `SixtyFourFramesTakeThreeSeconds` | con 15.625 µs, 64 frames terminan en 3.000.000 µs (21,33 FPS), con 74 pasos corridos y 80 ms en el acumulador: los 75 de 3 s más el del valor inicial |
| `TheFirstThirteenFrames` | 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2 |
| `EachFrameWaitsThreeTicksAndMeasures46Or47` | cada frame termina en su tercer tick y mide 46 una vez y 47 siete veces cada 8 |
| `SeventyFiveStepsEverySixtyFourFrames` | en 300 frames, desde el frame 2, cada 64 seguidos suman 75, con 11 de 2 pasos; cada 6 suman 7 u 8; los frames 31 a 36 dan 2, 1, 1, 1, 1, 2 |
| `WithTenMsTicksTwentyFiveFramesPerSecond` | con 10.000 µs, 25 frames en 1 s y 1 paso por frame desde el primero |
| `WithOneMsTicksEachFrameMeasuresFortyExactly` | con un reloj de 1 ms, cada frame mide 40 exactos: los dos relojes de arriba no distinguen una espera de 40 de una de 33 |
| `ATenSecondDrawRunsTwoHundredFiftyStepsNextFrame` | un dibujo de 10 s suma 10.000 ms y el frame siguiente corre 250 pasos: no hay tope |
| `TimeSpentInStepsIsNotAccumulated` | pasos de 10 ms, frames mientras el reloj no llegue a 3 s: 61 frames y 71 pasos, y el reloj en 3,015625 s (midiendo desde antes de los pasos serían 64 y 74) |
| `SlowStepsSlowTheGameDownInsteadOfSpiralling` | pasos de 50 ms, frames mientras el reloj no llegue a 10 s: 99 frames y 115 pasos, con el reloj en 10,03125 s; arrancando en 0, 100 y 116, con el reloj en 10,125 s |
| `AFixedStepThatMeasuresTheStepsSpirals` | un `FixedStep` que mide el frame entero, sin tope: 1, 1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 8, 10, 12, 15 |
| `StartingAtZeroOnlyChangesTheFirstFrame` | arrancando en 0, como el modelo del plan al principio: el primer frame corre 0 pasos y 64 frames, 73; después del primero los dos modelos quedan igual |
| `TheGetTickCountWrapChangesNothing` | empezando 67 ticks antes de la vuelta del `DWORD`, 64 frames tardan 3 s y corren los mismos pasos que desde 0 |

Con el acumulador en 0, todos los números del plan salieron como estaban escritos; antes de los
tests, un modelo en Python del mismo bucle dio lo mismo. Con el modelo en 40, cada número lo
rehicieron los dos simuladores de las revisiones, escritos aparte.

**Respuesta.** El original no espirala porque no mide los pasos: la medición empieza en `0x63852b`,
después de ellos. Con pasos lentos, cada frame sigue sumando 46 o 47 ms, corre 1 o 2 pasos y tarda
más: en 10 s corren 115 pasos y no 250, el juego va al 46 % de su velocidad. Lo que paga es eso:
la lógica se atrasa respecto del reloj, y con ella todo lo que va por paso (la red de la CL4, los
tiempos de las animaciones). Un breakpoint de 10 s **en el dibujo**, en cambio, sí se mide (está
entre `0x63852b` y `0x638632`), y como no hay tope, el frame siguiente corre 250 pasos seguidos
(`ATenSecondDrawRunsTwoHundredFiftyStepsNextFrame`).

## 13. Carril R

En `00637700_FUN_00637700.asm` (build `c32bbd5f`) y en `main.exe` con `objdump -d -M intel`:

```text
637791 cmp dword ptr [0x6d5ee4],0x28
637798 jl  0x6381aa
638162 mov eax,[0x6d5ee4]
638167 sub eax,0x28
63816a mov [0x6d5ee4],eax
6381a5 jmp 0x637791
63852b call [0x6b8144]                 GetTickCount (el import de 0x6b8144)
638531 mov [ebp-0x160],eax             el inicio
6385fd call [0x6b8144]
638603 sub eax,[ebp-0x160]
638609 mov [ebp-0x168],eax
63860f cmp dword ptr [ebp-0x168],0x28
638616 jge 0x63862c
63862c mov edx,[0x6d5ee4]
638632 add edx,[ebp-0x168]
638638 mov [0x6d5ee4],edx
```

**Respuestas:**

- **Con signo.** `JL` salta con `SF != OF`, la comparación con signo; la sin signo sería `JB`. Un
  acumulador negativo no corre pasos, y uno de `0x80000000` o más tampoco: para el procesador es
  negativo.
- **Sin tope.** Después del `SUB` y la lógica del paso, `0x6381a5` vuelve al `CMP` de `0x637791`
  sin otra comparación en el medio, y el `ADD` de `0x638632` suma la diferencia entera, sin
  recortarla. La espera también compara con signo (`JGE` en `0x638616`) una resta de `DWORD`.
- **Quién más lo toca.** Buscando los bytes de la dirección (`e4 5e 6d 00`) en todo `.text`
  aparecen 5, y son los operandos de las 5 instrucciones de arriba (`0x637793`, `0x638163`,
  `0x63816b`, `0x63862e` y `0x63863a`): nadie más lo nombra. *Estático*: una escritura por
  puntero no se descarta así.
- **Lo nuevo: arranca en 40.** En el `.data` del archivo (desplazamiento `0x2d5ee4`),
  `0x6d5ee4` vale `0x28`. El primer frame de `0x637700` corre 1 paso y no 0, y como el acumulador
  es global y nadie lo pone en 0 por su dirección, sigue de una escena a la otra. El jefe arranca
  en 40, como el original; `StartingAtZeroOnlyChangesTheFirstFrame` muestra que arrancando en 0
  solo cambia el primer frame. Lo que cambia en 40, para la CL3.R y el `FaithfulPacer` de la CL3.2:
  - el primer frame corre 1 paso: los primeros 13 empiezan en 1, en el plan y en sus tests;
  - 64 frames corren 74 pasos y no 73, con los mismos 80 ms en el acumulador
    (`SixtyFourFramesEveryThreeSeconds` y los "73 corridos" de `MinimizedKeepsTheWait`);
  - con pasos de 10 ms, 61 frames y 71 pasos, no 70, y midiendo desde antes de los pasos, 64 y 74,
    no 73 (`TimeSpentInStepsIsNotAccumulated`);
  - la espiral, 99 frames y 115 pasos, no 100 y 116;
  - con el tick de 10 ms, 1 paso por frame desde el primero y no desde el segundo;
  - en `ADrawLongerThanTheWaitAddsItWhole` y `HasNoCap`, el primer frame corre 1 paso antes de
    dibujar; los 3 y los 250 del frame siguiente no cambian.

  No cambian los 3 s de 64 frames, las ventanas desde el frame 2 ni lo que mide cada frame.
