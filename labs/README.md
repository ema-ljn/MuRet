# labs

El código de las N.0: lo que se construye desde cero para aprender un concepto, antes de usar el
estándar en el servidor o en el cliente. Lo escribe Emanuel; la 0.0, la CL2.0 y la CL3.0 las
resolvió Claude a su pedido. Cada carpeta es una N.0 y su guía está en
[`docs/learning/`](../docs/learning/README.md).

| Carpeta | N.0 | Guía |
| --- | --- | --- |
| [`0.0-enlace/`](0.0-enlace/) | del código al programa: unidades de traducción, símbolos, bibliotecas. Resuelta como ejemplo, con las respuestas en su [README](0.0-enlace/README.md) | [ola-0.0](../docs/learning/ola-0.0-del-codigo-al-programa.md) |
| [`cl2.0-flujos/`](cl2.0-flujos/) | flujos y bloques: el corte de un flujo TCP, bits MSB primero, el inverso modular y el jefe Q-C03. Resuelta como ejemplo, con las respuestas y la comparación con el `PacketReader` y `<bit>` en su [README](cl2.0-flujos/README.md) | [ola-cl2.0](../docs/learning/ola-cl2.0-flujos-y-bloques.md) |
| [`cl3.0-bucle/`](cl3.0-bucle/) | el bucle de juego: el paso fijo con acumulador, el reloj de `GetTickCount` y el jefe, el bucle de `0x637700` y la espiral que no tiene. Resuelta como ejemplo, con las respuestas en su [README](cl3.0-bucle/README.md) | [ola-cl3.0](../docs/learning/ola-cl3.0-el-bucle-de-juego.md) |

Los tests de la 0.0 son ejecutables que salen con 0 o con 1, registrados con `add_test()`; desde
la CL2.0, de GoogleTest. Se corren con CTest:

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --test-dir build/linux-x64-debug --output-on-failure
```

## CL3.0: ms enteros contra `std::chrono`, y los dos relojes

La actividad 9 de la [guía](../docs/learning/ola-cl3.0-el-bucle-de-juego.md#9-comparar-los-ms-enteros-stdchrono-y-los-dos-relojes).

- **`FixedStep` con `int` de ms contra `ChronoFixedStep` con `std::chrono::nanoseconds`.** Con 60
  frames que suman 1 s (16.666.666 o 16.666.667 ns cada uno), el de ms recibe cada frame truncado
  con `duration_cast<milliseconds>`, 16 ms: el segundo cuenta 960 ms y corre 24 pasos. El de
  nanosegundos corre 25 y queda en 0
  (`ChronoFixedStepTest.TruncatingEachFrameToMillisecondsLosesAStep`). Truncar cada frame pierde
  hasta 1 ms por frame, y a más FPS, más se pierde: a 144 FPS cada frame de 6,94 ms cuenta 6, y
  el juego pierde el 13,6 %, más de 3 pasos por segundo.
- **Por qué el original no pierde nada con `int`.** No trunca una duración: resta dos lecturas de
  `GetTickCount`, que ya son ms enteros. Una resta de enteros es exacta, y las lecturas se
  calculan desde el tiempo total, así que la truncación de cada tick no se acumula: medidas una
  detrás de la otra, las diferencias suman la última lectura menos la primera, y 64 frames de 3
  ticks suman 3.000 ms justos (46 y 47 alternados). Lo que el original pierde es otra cosa: lo que
  pasa fuera de la medición (los pasos, la red, el bucle de mensajes).
- **`steady_clock` contra `system_clock`.** `system_clock` es la hora de pared: va para atrás
  cuando alguien cambia la hora a mano o cuando NTP la corrige de golpe, y un frame medido con él
  puede dar negativo o de horas. `steady_clock` no vuelve nunca: el estándar lo garantiza con
  `is_steady`, que es `true` (el test lo comprueba con un `static_assert`). En `system_clock`, el
  estándar no lo fija; con GCC 13.3 y Clang 18, `system_clock::is_steady` es `false`. El reloj del
  cliente (la CL3.2) es `SDL_GetTicksNS`, monótono, y el falso de los tests puede ir para atrás a
  propósito, para probar que un frame negativo cuenta 0.
- **Lo que va al producto:** `std::chrono::nanoseconds` como unidad (`Core::Duration`, D-C4), con
  el paso de 40 ms y el tope en `milliseconds` y `seconds`, que se convierten solos sin truncar; el
  modo fiel sigue en ms enteros y en `DWORD`, como el original.
