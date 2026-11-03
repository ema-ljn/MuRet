# 0005. El ritmo: lógica a 25 pasos por segundo y dibujo desacoplado

- **Estado:** aceptado (D-C4 del roadmap y la pregunta 2 del plan de la CL3, 2026-11-02).
- **Fila:** Q-C44 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
- **Código:** `Client::App::GameLoop`, `FixedStep`, `FaithfulPacer` y las constantes de
  `Client/src/App/LoopTiming.h`; `[Video] Pacing` en la config.

## Contexto

El original corre la lógica en pasos de 40 ms, 25 por segundo, y después de dibujar espera de forma
activa 40 ms medidos con `GetTickCount`. Con el tick de 15,625 ms, esa espera termina a los
46,875 ms: dibuja a 21,33 FPS, medido en la selección y en Lorencia. El acumulador de pasos
(`DAT_006d5ee4`, en `0x637791`..`0x638638`) arranca en 40 y no tiene tope: un frame de 10 s corre
250 pasos seguidos. Pero solo mide desde `0x63852b`, el dibujo, el swap y la espera: lo que pasa
antes se pierde de a ticks enteros (Q-C44; la sección "El ritmo" del
[02](../../Client/docs/research/02-arranque-y-ciclo-de-vida.md)).

El [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) pide dos cosas que tiran para lados
distintos. Más fluido: dibujar a los FPS que dé la máquina sin cambiar la velocidad del juego
(D-C4). Fiel: todo lo observable al tick, la velocidad de la lógica incluida. Cuántos frames se
dibujan entre dos pasos no es observable en ese sentido: no cambia lo que se manda ni a qué ritmo
avanza el juego.

## Decisión

- **La lógica va siempre a 25 pasos por segundo**, con el mismo bucle de alcanzar del original
  (`LogicStep` = 40 ms). Eso no es opcional.
- **Por defecto, el dibujo va desacoplado** (`Pacing = decoupled`): VSync, sin tope o con
  `[Video] MaxFps`, y el dibujo interpola entre el último paso y el próximo. MuRet mide el frame
  entero, no solo desde el dibujo, y lo recorta a 1 s (`MaxFrameTime`): un frame más largo corre
  25 pasos como mucho, y el resto se pierde con un aviso en el log. Sin tope, 10 s en un debugger
  serían 250 pasos, y una espiral si cada paso tarda más de 40 ms. Con uno menor que 1 s, el test
  de 1 frame por segundo y 25 pasos que pide el 02 no pasa.
- **El modo fiel es la opción** (`Pacing = faithful`): reproduce la espera del original con un
  reloj de 15,625 ms, sin tope, y mide desde el mismo lugar que el original. Sirve para comparar
  lado a lado con el `main.exe`.

No es como el [ADR 0001 de MuMain][mm]: allá, debajo de 25 FPS, el frame avanza un paso como mucho
y el juego va en cámara lenta. MuRet se pone al día, con hasta 25 pasos por frame.

## Consecuencias

- **Se gana:** con una máquina que dibuja a 60 o 144 FPS el juego se ve más fluido y va a la misma
  velocidad que el original. Un corte largo (un breakpoint, el driver) no hace que el juego corra
  segundos de golpe ni que espirale.
- **Se pierde:** en el modo por defecto, después de un frame de más de 1 s el juego queda atrás del
  original por lo que pasó del tope. Y como el frame se mide entero, una ventana arrastrada suma
  tiempo que el original pierde. Las dos diferencias desaparecen con `Pacing = faithful`.
- **Lo fijan:**
  - `ConfigTest.DefaultsWhenAKeyIsMissing`: sin la clave, el ritmo es `Decoupled`.
  - `GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap` y `FixedStepTest.TheCapIsOneSecond`: el
    tope de 1 s, y que un frame de 10 s corre 25 pasos.
  - `FaithfulPacerTest.HasNoCap`: en el modo fiel, el mismo frame corre 250 pasos.
  - `FaithfulPacerTest.TimeSpentInStepsIsNotAccumulated`: el modo fiel no mide los pasos, como el
    original.

El contrato y los pasos están en las sub-olas CL3.0 y CL3.2 del
[plan de la CL3](../superpowers/plans/2026-11-02-ola-cl3-plataforma-y-bucle.md).

[mm]: https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md
