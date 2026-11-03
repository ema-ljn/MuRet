# labs

El código de las N.0: lo que se construye desde cero para aprender un concepto, antes de usar el
estándar en el servidor o en el cliente. Lo escribe Emanuel; la 0.0, la CL2.0, la CL3.0 y la CL5.0
las resolvió Claude a su pedido. Cada carpeta es una N.0 y su guía está en
[`docs/learning/`](../docs/learning/README.md).

| Carpeta | N.0 | Guía |
| --- | --- | --- |
| [`0.0-enlace/`](0.0-enlace/) | del código al programa: unidades de traducción, símbolos, bibliotecas. Resuelta como ejemplo, con las respuestas en su [README](0.0-enlace/README.md) | [ola-0.0](../docs/learning/ola-0.0-del-codigo-al-programa.md) |
| [`cl2.0-flujos/`](cl2.0-flujos/) | flujos y bloques: el corte de un flujo TCP, bits MSB primero, el inverso modular y el jefe Q-C03. Resuelta como ejemplo, con las respuestas y la comparación con el `PacketReader` y `<bit>` en su [README](cl2.0-flujos/README.md) | [ola-cl2.0](../docs/learning/ola-cl2.0-flujos-y-bloques.md) |
| [`cl3.0-bucle/`](cl3.0-bucle/) | el bucle de juego: el paso fijo con acumulador, el reloj de `GetTickCount` y el jefe, el bucle de `0x637700` y la espiral que no tiene. Resuelta como ejemplo, con las respuestas en su [README](cl3.0-bucle/README.md) | [ola-cl3.0](../docs/learning/ola-cl3.0-el-bucle-de-juego.md) |
| [`cl5.0-pipeline/`](cl5.0-pipeline/) | el pipeline gráfico: el quad de `0x621b60` con la y dada vuelta y la escala del binario, la matriz de `gluOrtho2D` contra glm, de fan a triángulos, el jefe (el NDC del original y el viewport de `0x620db0`) y, en `gl/`, el quad dibujado en un framebuffer con Xvfb. Resuelta como ejemplo, con las respuestas en su [README](cl5.0-pipeline/README.md) | [ola-cl5.0](../docs/learning/ola-cl5.0-el-pipeline-grafico.md) |

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
  ticks suman 3.000 ms justos (46 una vez y 47 siete veces cada 8 frames, 46,875 de media). Lo que
  el original pierde es otra cosa: lo que pasa fuera de la medición (los pasos, la red, el bucle de
  mensajes).
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

## CL5.0: 95.000 vértices de a uno contra un VBO, y el orden con mezcla

La actividad 12 de la
[guía](../docs/learning/ola-cl5.0-el-pipeline-grafico.md#12-comparar-95000-vértices-de-a-uno-contra-un-vbo-y-el-orden-con-mezcla),
el paso 7 del plan.

- **Con la traza del 06, a mano.** 224,8 millones de `glVertex3fv` en 2.375 frames son unos 95.000
  vértices por frame, y las 285.000 llamadas por frame son tres por vértice: el color, la
  coordenada de textura y el vértice, más un `glBegin` y un `glEnd` por fan. Con un VBO que se
  llena por frame, como el lote de la CL5.2, son un `glBufferData` con todos los vértices y una
  llamada de dibujo por tramo de la misma textura y mezcla: decenas, no cientos de miles.
- **Medido**, en Xvfb con Mesa 25.2.8 (llvmpipe), con dos programas fuera del repo:
  - 95.000 vértices de a uno, en 23.750 fans de 4 con `glColor4f`, `glTexCoord2f` y `glVertex3fv`
    (332.500 llamadas, en un contexto "4.5 (Compatibility Profile)", porque en core no existen) y
    todos en el mismo punto, para que casi no haya nada que rasterizar: entre 5,8 y 6,2 ms por
    frame, con el `glFinish`. El primer frame, de 16 a 49 ms según la corrida, calienta el driver.
  - Los mismos 95.000 vértices como `QuadVertex` (16 bytes cada uno, 1.520.000 en total) en un
    `glBufferData` con `GL_STREAM_DRAW`: entre 0,11 y 0,13 ms.
  - 285.000 llamadas a `glGetError`, que no hace nada: entre 1,2 y 1,5 ms, unos 4 o 5 ns por
    llamada. Es lo que cuesta solo llamar a GL por un puntero; el resto de los 6 ms es lo que hace
    el driver con cada vértice.

  Con el VBO, unas 50 veces menos, sin contar el dibujo. En la VM XP del original el driver es
  otro y los números también; lo que no cambia es dónde está el costo.
- **Respuesta: el costo es de la CPU.** La GPU recibe los mismos vértices de las dos maneras; lo
  que cambia es cuántas veces el programa entra al driver y cuánto trabaja el driver por llamada
  (guardar el color actual, armar el vértice, ver si su buffer se llenó). Acá, 6 ms son casi el
  13 % de un frame del ritmo del original, 46,875 ms (CL3).
- **El orden con mezcla** (`LabFramebufferTest.TheOrderMattersOnlyWithNormalBlending`): un naranja
  (100, 50, 0) y un verde azulado (0, 50, 100), los dos con alfa 128 (0,502), sobre negro.
  - Con la normal, naranja y después verde azulado da (25, 37, 50), y al revés, (50, 37, 25). El
    primero deja (50, 25, 0), y el segundo pone la mitad de lo suyo y deja la mitad de eso. En el
    verde la cuenta da 37,55 y Mesa da 37: el redondeo a 8 bits mueve uno, y el test acepta ±1.
  - Con la aditiva, (100, 100, 100) en los dos órdenes.

  **Respuesta.** Con la normal, cada quad tapa en parte al anterior: lo que se ve depende de quién
  va arriba, y un lote que reordene para juntar las texturas cambia la pantalla. Por eso el
  `SpriteBatch` de la CL5.2 corta un comando cada vez que cambia la textura o la mezcla, y nunca
  reordena ("Lo que más puede morder", en el plan). Con la aditiva la suma conmuta, también con el
  recorte a 1, porque los colores no son negativos: un tramo todo aditivo se podría reordenar
  adentro, pero no con lo que tiene antes y después, y el original no lo hace.
- **Lo que va al producto:** un VBO que se llena por frame con todo el 2D (la CL5.2), y un comando
  por tramo de la misma textura y mezcla, en el orden en que la escena los pidió.
