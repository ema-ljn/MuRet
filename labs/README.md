# labs

El código de las N.0: lo que se construye desde cero para aprender un concepto, antes de usar el
estándar en el servidor o en el cliente. Lo escribe Emanuel; la 0.0, la CL2.0, la CL3.0, la
CL4.0, la CL5.0 y la CL6.0 las resolvió Claude a su pedido. Cada carpeta es una N.0 y su guía está
en [`docs/learning/`](../docs/learning/README.md).

| Carpeta | N.0 | Guía |
| --- | --- | --- |
| [`0.0-enlace/`](0.0-enlace/) | del código al programa: unidades de traducción, símbolos, bibliotecas. Resuelta como ejemplo, con las respuestas en su [README](0.0-enlace/README.md) | [ola-0.0](../docs/learning/ola-0.0-del-codigo-al-programa.md) |
| [`cl2.0-flujos/`](cl2.0-flujos/) | flujos y bloques: el corte de un flujo TCP, bits MSB primero, el inverso modular y el jefe Q-C03. Resuelta como ejemplo, con las respuestas y la comparación con el `PacketReader` y `<bit>` en su [README](cl2.0-flujos/README.md) | [ola-cl2.0](../docs/learning/ola-cl2.0-flujos-y-bloques.md) |
| [`cl3.0-bucle/`](cl3.0-bucle/) | el bucle de juego: el paso fijo con acumulador, el reloj de `GetTickCount` y el jefe, el bucle de `0x637700` y la espiral que no tiene. Resuelta como ejemplo, con las respuestas en su [README](cl3.0-bucle/README.md) | [ola-cl3.0](../docs/learning/ola-cl3.0-el-bucle-de-juego.md) |
| [`cl4.0-sockets/`](cl4.0-sockets/) | sockets no bloqueantes: el socket con RAII, el `connect` como máquina de estados y `SO_ERROR`, el `send` que toma menos y la cola con cursor, y el jefe Q-C11, el drenaje de `0x4e87b0`. Resuelta como ejemplo, con las respuestas en su [README](cl4.0-sockets/README.md) | [ola-cl4.0](../docs/learning/ola-cl4.0-sockets-no-bloqueantes.md) |
| [`cl5.0-pipeline/`](cl5.0-pipeline/) | el pipeline gráfico: el quad de `0x621b60` con la y dada vuelta y la escala del binario, la matriz de `gluOrtho2D` contra glm, de fan a triángulos, el jefe (el NDC del original y el viewport de `0x620db0`) y, en `gl/`, el quad dibujado en un framebuffer con Xvfb. Resuelta como ejemplo, con las respuestas en su [README](cl5.0-pipeline/README.md) | [ola-cl5.0](../docs/learning/ola-cl5.0-el-pipeline-grafico.md) |
| [`cl6.0-huesos/`](cl6.0-huesos/) | huesos y cuaterniones: `AngleQuaternion` y `QuaternionMatrix` contra glm, el slerp de `0x5fce90` con sus dos comparaciones y la rama que no corre, la cadena de huesos con el skinning rígido, y el jefe (el slerp contra los ángulos y el lerp normalizado, y en qué key está el original). Resuelta como ejemplo, con las respuestas en su [README](cl6.0-huesos/README.md) | [ola-cl6.0](../docs/learning/ola-cl6.0-huesos-y-cuaterniones.md) |

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

## CL4.0: un socket por frame, hilos o IOCP; `select` o `poll`

La actividad 10 de la
[guía](../docs/learning/ola-cl4.0-sockets-no-bloqueantes.md#10-comparar-hilos-iocp-select-y-poll).

- **Un socket consultado una vez por frame** (MuRet, D-C5). Cuesta una llamada al sistema por
  socket y por frame: con el lab, unos 270 ns por `recv` sin nada, 16 µs por segundo a 60 FPS. No
  hay hilos ni locks, y un test corre los dos lados en un hilo (el `LoopbackPair`). Lo que llega
  espera hasta el próximo frame, como en el original, que despacha la cola de paquetes una vez por
  vuelta del bucle (`0x4e1d50`). Sirve con uno o dos sockets; con miles, cada vuelta le pregunta a
  todos, casi siempre para nada.
- **Un hilo por conexión** (la 2.3 del servidor). El código de cada conexión es bloqueante y
  simple, pero lo que lee tiene que pasar al hilo de la lógica por una cola con lock, cada hilo
  reserva su pila (1 MiB por defecto en Windows, 8 MiB de espacio virtual en Linux) y los cambios de
  contexto crecen con los hilos: sirve con decenas o cientos de conexiones. A un cliente con una
  le suma una carrera entre la red y la lógica, y no le da nada.
- **IOCP o epoll** (la ola 3). El sistema dice cuáles de muchos sockets están listos (epoll, por
  disponibilidad) o qué operación terminó (IOCP, por finalización, con el buffer prestado al
  sistema hasta que termine). Una llamada atiende a todos los que tienen algo, y el costo depende
  de cuántos están listos, no de cuántos hay: es lo del servidor, con miles. Para un socket, es más
  código, y en IOCP la vida de los buffers, a cambio de nada.
- **El del original**, `WSAAsyncSelect`: el sistema avisa con un mensaje de ventana (`0x400`). Es
  un solo hilo, como MuRet, pero los avisos llegan por el bucle de mensajes de Windows, que sigue
  andando aunque el del juego no corra, por ejemplo mientras se arrastra la ventana (CL4.1).
- **`select`, `poll` y `WSAPoll`, para saber si un `connect` terminó:**
  - `select` anda en los dos, pero no igual. En POSIX, `fd_set` es un mapa de bits de
    `FD_SETSIZE` bits (1024 en glibc): un descriptor de 1024 o más no entra, y `FD_SET` con él
    escribe fuera del arreglo. En Windows, `fd_set` es una lista de hasta 64 `SOCKET` (su
    `FD_SETSIZE`), el primer argumento no se usa, y un `connect` rechazado aparece en `exceptfds`
    y no en `writefds`: hay que mirar los dos.
  - `poll` es de POSIX: no tiene límite en el número del descriptor, y da `POLLOUT` cuando el
    `connect` terminó, con `POLLERR` o `POLLHUP` si falló, que llegan aunque no se pidan. Es el que
    usa el lab en Linux.
  - `WSAPoll` es el `poll` de Winsock, desde Vista. Según su documentación, hasta Windows 10 2004
    no avisaba un `connect` que fallaba, y esperaba hasta el tiempo límite. Por eso el lab usa
    `select` en Windows: con un socket, el tope de 64 no importa, y anda en cualquier versión.
  - **Lo que va al producto:** lo mismo, según el plan (el `CheckConnect` de la CL4.1): `poll` en
    POSIX y `select` con escritura y excepciones en Windows, con tiempo 0, y `SO_ERROR` leído una
    vez.

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

## CL6.0: slerp, lerp normalizado y ángulos interpolados

La actividad 12 de la
[guía](../docs/learning/ola-cl6.0-huesos-y-cuaterniones.md#12-comparar-slerp-lerp-normalizado-y-ángulos),
el paso 6 del plan. Los números salen del jefe
(`OriginalPoseTest.NlerpHidesBelowTheTraceErrorAndEulerDoesNot`).

- **Medido.** Un vértice a 50 de su hueso, lo más lejos que se aparta del slerp del original entre
  t = 0 y 1, cada 0,01:

  | Keys | Lerp normalizado | Ángulos interpolados |
  | --- | --- | --- |
  | (10°, 20°, 30°) a (12°, 24°, 36°), 6,9° | 0,00035 | 0,039 |
  | (30°, 40°, 50°) a (50°, 20°, 80°), 33° | 0,037 | 1,66 |

- **Respuesta: por qué el 05 distingue a uno y no al otro.** El slerp y el lerp normalizado
  recorren el mismo arco, a velocidad distinta: la diferencia crece con el cubo del ángulo entre
  las keys (de 6,9° a 33°, 4,8 veces más ángulo, 106 veces más diferencia), y entre keys de unos
  grados es de milésimas, debajo del 0,015 del ajuste de la selección en el 06. Los ángulos
  interpolados recorren otro camino, y el error crece con lo que cambian los ejes a la vez: 0,039
  con 6,9°, más del doble del ajuste.
- **No vale para cualquier par.** Con keys a 33°, el lerp normalizado ya se aparta 0,037, y en la
  traza se vería. Los ángulos siguen 44 veces peor.
- **Lo que cuesta cada uno, por hueso.** El slerp del original, un `acos`, tres senos y dos
  divisiones. El lerp normalizado, una raíz y cuatro divisiones (o una multiplicación por la
  inversa). Los ángulos interpolados, seis senos y cosenos, los de `AngleQuaternion`. Con los 60
  huesos de `Player.bmd` (05) y los cinco personajes de la selección, no hay diferencia que medir.
- **Respuesta a la pregunta.** El test contra la traza de la CL6.2 (0,02 de tolerancia) no
  encontraría el lerp normalizado entre keys cercanas. Igual conviene el del original: es lo que
  se espera de esa función (`docs/CODING_RULES.md` § 10), y si alguna acción tiene keys lejanas, la
  diferencia aparece donde nadie la busca.
- **Lo que va al producto:** el slerp de `0x5fce90` tal cual, con sus dos comparaciones y la
  vuelta de `q` (la CL6.2), no `glm::slerp`: los dos dan lo mismo a 1,8e-7 en el lab, pero el de
  glm pasa a lineal en otro lugar.
