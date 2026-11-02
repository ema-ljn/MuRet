# CL4.0: sockets no bloqueantes, resuelta

Ejemplo trabajado de la [guía de la CL4.0](../../docs/learning/ola-cl4.0-sockets-no-bloqueantes.md).
Lo resolvió Claude porque Emanuel pidió, el 2026-11-02, que Claude escriba toda la CL4 hasta que
él diga que participa. Las salidas son reales, con GCC 13.3 en Linux y un build Debug. Windows y
MSVC, sin probar acá (los corre el CI): lo que este README dice de Windows sale de la
documentación de Microsoft o del plan, y lo dice.

Para cerrar la CL4.0 en el [cuaderno](../../docs/learning/PROGRESS.md) falta lo tuyo, cuando
entres: contar con tus palabras las "Preguntas para chequearte" de la guía y completar su sección
final.

| Archivo | Qué es |
| --- | --- |
| `LabSocket.h` / `.cpp` | la misión de la actividad 3, con el `connect` y los datos de la 5: el socket con RAII, y todos los `#ifdef _WIN32` del lab |
| `LoopbackPair.h` / `.cpp` | la misión de la actividad 5: un listener y un cliente en `127.0.0.1`, no bloqueantes y en un hilo |
| `DrainQueue.h` / `.cpp` | la misión de la actividad 8: la cola con cursor, de `0x2000` bytes como el buffer del original |
| `TestSupport.h` | lo que comparten los tests: el `PrintTo` de `IoStatus`, la espera con tope y el `send` que sigue un guion |
| `test_*.cpp` | sus tests, de GoogleTest |
| `jefe/test_original_drain.cpp` | el jefe: un modelo de `0x4e87b0` contra la cola, sobre el mismo guion (Q-C11) |

Las tres misiones van en la biblioteca `MuRetLabsCl40`, que en Windows enlaza `ws2_32`, y los
tests en `LabCl40Tests`, con el prefijo `LabCl40.` en CTest. Son 28: 6 del socket, 10 del par, 8
de la cola y 4 del jefe.

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug -R "LabCl40"
```

Los números de las actividades 2, 5, 6 y 7 salen de un programa chico que usa la biblioteca del
lab y no está en el repo: 200 pares, 3 puertos cerrados, un millón de `recv` sin nada, 3 llenados
del buffer y 5 `send` después de un cierre.

## 1. Calentamiento

El primer frame espera el saludo, unos milisegundos, y lo lee: el paso pide la lista. El segundo
espera la lista y la lee. El tercero se queda en el `recv` para siempre: el servidor espera que el
cliente elija, y el cliente elegiría en un paso, que corre después del `Poll`. Es un interbloqueo,
cada uno espera al otro. El jugador ve la ventana congelada, y Windows, que no la ve sacar mensajes
de su cola, la marca como "No responde". Con el GameServer pasa lo mismo apenas se queda callado: el
frame dura lo que tarde en llegar el próximo paquete, y el keepalive no sale.

**Respuesta.** Un hilo que espera por el bucle, con una cola hacia la lógica (la 2.3 del
servidor), o un socket que no espera. La CL4 elige el segundo (D-C5).

## 2. No bloquear

**Respuesta.** Uno por frame: 60 por segundo. Acá, un `recv` sin nada cuesta unos 270 ns (un
millón seguidos, todos "probá más tarde"): 60 son 16 µs por segundo, el 0,002 % del tiempo. Un
servidor que preguntara así a 10.000 conexiones haría 10.000 llamadas por vuelta, casi todas para
nada: unos 2,7 ms por vuelta solo en preguntar, y la cuenta crece con cada conexión. Para eso están
epoll e IOCP (actividad 10).

## 3. La misión: `LabSocket`

Antes de las implementaciones, con los `.cpp` vacíos, los tests compilan y falla el linker: un
`undefined reference` para cada función de `NetworkInit`, `LabSocket`, `LoopbackPair` y
`DrainQueue`.

**Las decisiones:**

- **`std::uintptr_t` como handle.** Entran un `SOCKET` de 64 bits y un `int`. Vacío es `~0`:
  `INVALID_SOCKET` en Windows, y el `-1` de POSIX pasado a `std::uintptr_t` da lo mismo, así que
  `OpenTcp` guarda lo que dio `socket` sin preguntar en qué sistema corre (`InvalidSocket`).
- **Vacío, no una excepción.** `OpenTcp` da uno vacío si el sistema no da un socket, y con uno
  vacío todo da `false`, 0 o `Failed` (`AnEmptySocketHasNothingToDo`). El cliente usará
  `Core::Expected`; en el lab alcanza con `IsOpen`.
- **`Close` idempotente.** Con uno vacío no hace nada, y el destructor lo llama siempre.
- **El RAII, visto desde afuera.** El movido queda vacío y el otro sigue con el mismo puerto
  (`AMovedSocketLeavesTheDescriptorToTheOther`). Al destruirse, el puerto queda libre y otro
  socket lo toma (`TheDestructorFreesThePort`), sin `SO_REUSEADDR`: un listener que no aceptó a
  nadie no deja nada en `TIME_WAIT`. Asignar encima de uno abierto cierra el viejo
  (`MovingOntoASocketClosesTheOldOne`).

## 4. El `connect`, "probá más tarde" y el `recv` en 0

**Respuesta.** Datos (`Done` con los bytes), "probá más tarde" (`TryLater`: el `Poll` sigue con
el frame), 0 (`PeerClosed` sin error: el otro lado mandó su FIN, y la conexión pasa a cerrada con
ese motivo) o un error: `PeerClosed` si es un RST (`ECONNRESET`), y `Failed` si es otro. Con un
buffer de 0 bytes, `recv` da 0 si hay datos esperando, y 0 se lee como un cierre; sin datos, en
Linux da `EAGAIN`. Por eso `Receive` con un buffer vacío no llama a `recv`
(`AnEmptyReceiveIsNotAClose` deja 4 bytes esperando antes de probarlo).

## 5. La misión: `LoopbackPair`

**Predicción.** Antes. En los 200 pares del programa, el `connect` dio siempre `InProgress` con
`EINPROGRESS` (115), y un `CheckConnect` justo después de abrir el par, sin `accept`, dio `Done`
las 200 veces: el sistema termina el handshake y deja la conexión en la cola del listener, y
`accept` solo la saca de ahí. Por eso `AfterTheAcceptTheClientIsConnected` no depende del orden, y
`TheConnectReturnsAtOnce` acepta `Done` también: el plan lo pide, y en otro sistema puede pasar.

**Las decisiones:**

- **Esperar al sistema con un tope.** `WaitUntil` pregunta de a 1 ms hasta que la condición dé
  `true` o pasen 5 s de reloj real. En Linux el `accept` y el rechazo llegan en microsegundos, así
  que los 28 tests tardan 0,2 s; el tope es para un CI lento y para el rechazo de Windows.
- **Leer hasta tener 5.** TCP no garantiza que 5 bytes lleguen en una lectura: `ReadExactly` junta
  hasta tener los que espera (`FiveBytesArriveAsFive`).
- **El aceptado, no bloqueante a mano.** En Linux, `accept` no hereda `O_NONBLOCK` del listener, y
  `LoopbackPair::Accept` lo pone. En Windows el aceptado hereda las propiedades del listener,
  según la documentación de `accept`; ponerlo igual no cambia nada.
- **Un cierre es `PeerClosed` con el error en 0** (`ClosingOneSideMakesTheOtherReadZero`).

## 6. Un puerto cerrado

**Predicción.** `Done`. En los 3 puertos cerrados, el `connect` dio `InProgress` (115), y el
primer `CheckConnect`, entre 7 y 16 µs después, `Refused` con `ECONNREFUSED` (111). El segundo dio
`Done` con el error en 0: leer `SO_ERROR` lo borró, y el socket escribible ya no dice nada del
rechazo. Un `CheckConnect` que se llama dos veces miente (`AClosedPortIsRefused` lo llama hasta
que deja de dar `InProgress`, y no más).

**Respuesta.** En el estado de la conexión, la primera vez que da algo que no sea `InProgress`:
pasa de "conectando" a "abierta" o a "cerrada, rechazada", y no vuelve a preguntar. En Windows,
el rechazo tarda unos 2 s (lo dice el plan; no se midió acá) y `select` lo da en las excepciones
(actividad 10).

## 7. El `send` que toma menos

**Respuesta.** Con un cliente que manda de a 64 KiB y un servidor que no lee, los 3 llenados dieron
lo mismo: 59 `send` enteros, uno de 44.032 bytes (el parcial) y después `TryLater` con `EAGAIN`
(11). En total, 3.910.656 bytes, unos 3,7 MiB, entre el buffer de envío del cliente y el de
recepción del servidor (`ASendThatDoesNotFitTakesLessThanAsked`). La cola del lab tiene `0x2000`
bytes fijos, como el buffer del original: lo que no entra da `false` (`FillsUpToTheOriginalBuffer`),
y el original, en ese caso, cierra la conexión.

Mandar a uno que cerró es otro motivo para volver a cerrada. El primer `send` dio `Done` con sus
1.024 bytes, porque el sistema todavía no sabía; el otro lado contestó con un RST, y los 4
siguientes dieron `PeerClosed` con `EPIPE` (32) (`SendingToAClosedPeerDoesNotKillTheProcess`).

## 8. La misión: `DrainQueue`

**Las decisiones:**

- **`0x2000` bytes fijos**, en un `std::array`: es el buffer del original, y una cola que crece sin
  tope esconde un servidor que no lee.
- **Lo pendiente se corre al principio en `Push`**, y solo si el cursor no está en 0: lo que salió
  libera lugar (`ReusesTheRoomOfWhatWasSent`) y `Flush` no copia nada. Con el cursor en 0 el
  origen y el destino serían el mismo rango, y `std::copy` no lo permite.
- **Un `Done` con 0 bytes es "probá más tarde"** (`ASendThatTakesNothingStopsTheFlush`): seguir
  ahí sería un lazo sin fin adentro del frame.
- **Un error corta y deja lo pendiente** (`AnErrorStopsTheFlushAndKeepsThePending`): cierra quien
  llama, con su motivo. Una cola vacía no llama al `send` (`AnEmptyQueueDoesNotCallTheSend`).
- **Sobre un socket de verdad**, `LabSocket::Send` cumple con lo que pide `Flush`
  (`ADrainQueueFlushesIntoTheClient`).

## 9. El jefe: el drenaje de `0x4e87b0`

| Guion | Lo que pide cada `send` | Al cable | Test |
| --- | --- | --- | --- |
| `{2, 2, WSAEWOULDBLOCK}`, y después todo | 10, 6, 2, y 6 | `00 01 02 03`, y `00 01 02 03 04 05` | `RepeatsTheFirstBytesAndLosesTheLast` |
| el mismo, con la `DrainQueue` | 10, 8, 6, y 6 | `00..09`, una vez | `TheCursorQueueSendsEachByteOnce` |
| `{2, WSAEWOULDBLOCK}`, y después todo | 10, 6, y 8 | `00 01`, y `00..07` | `OnePartialSendIsEnough` |
| `{2, 6}` | 10, 6, -6 | `00..07`, y la tercera llamada cierra | `TwoPartialSendsInARowAskForANegativeLength` |

Con el guion del plan sale lo que dice el plan: `00 01 02 03 00 01 02 03 04 05`, con `00..03`
repetidos y `06..09` perdidos. La segunda llamada pide `8 - 2 = 6` y no 8, y la tercera,
`6 - 4 = 2` y no 6; con `WSAEWOULDBLOCK`, la función vuelve sin correr el buffer. El `FD_WRITE`
siguiente arranca con lo mandado en 0 y manda los 6 pendientes desde el principio.

**Lo que el plan no decía** (*análisis* del modelo; no se probó contra Winsock):

- **Basta un parcial.** Con `{2, WSAEWOULDBLOCK}`, al cable van `00 01 00 01 02 03 04 05 06 07`:
  `00 01` repetidos y `08 09` perdidos. El 07 dice "varios envíos parciales seguidos"; alcanza con
  uno.
- **Después de un parcial, el drenaje no termina bien nunca.** Lo pendiente llega a 0 solo si el
  último `send` toma `L - S` (lo que falta del largo `L` con `S` mandado antes), y esa llamada
  pidió `L - 2S`: con `S > 0`, tendría que tomar más de lo que pidió. Así que, si el primer `send`
  de un `FD_WRITE` no toma todo, el drenaje termina de una de dos formas: con `WSAEWOULDBLOCK`, y
  el `FD_WRITE` siguiente repite y pierde, o pidiendo un largo de 0 o negativo. Con `{2, 6}`, la
  tercera pide -6. Qué hace Winsock con eso no se probó: con cualquier error que no sea
  `WSAEWOULDBLOCK`, o con 0, `0x4e87b0` cierra la conexión. El modelo cierra (`false`), y hasta
  ahí salieron `00..07` sin repetir nada.

**Respuesta.** `0x4e87b0` corre solo después de que un `send` dio `WSAEWOULDBLOCK`, o sea con el
buffer del sistema lleno, y en loopback eso pasa con unos 3,7 MiB adentro (actividad 7), no con
paquetes de decenas de bytes y un servidor que lee. Y cuando corre, `FD_WRITE` avisa que hay lugar,
y lo pendiente es de `0x2000` bytes como mucho: el primer `send` casi siempre toma todo, y el bug
necesita que no.

## 10. Comparar

Está en [`labs/README.md`](../README.md#cl40-un-socket-por-frame-hilos-o-iocp-select-o-poll).

## 11. Rompelo

| Cambio | Qué falla | Cómo |
| --- | --- | --- |
| en Linux, `send` sin `MSG_NOSIGNAL` | `SendingToAClosedPeerDoesNotKillTheProcess` | el proceso muere: CTest lo marca `(SIGPIPE)` y el test no llega a ningún `EXPECT` |
| el constructor de movimiento copia el handle | 11 de 28: `AMovedSocketLeavesTheDescriptorToTheOther` y los 10 de `LoopbackPairTest` | el movido sigue abierto, y el otro da el puerto 0 en vez del suyo: el destructor del movido cerró el descriptor |
| `Receive` llama a `recv` con un buffer vacío | `AnEmptyReceiveIsNotAClose` | da `PeerClosed` en vez de `Done`, con 4 bytes esperando |
| `CheckConnect` da `Done` sin leer `SO_ERROR` | `AClosedPortIsRefused` | da `Done` en vez de `Refused`: en Linux, el rechazado también queda escribible |

**Respuestas.**

- `SIGPIPE` llega cuando se escribe en una conexión que el otro lado cerró, y su acción por
  defecto termina el proceso. No hay `EXPECT` que lo vea: el ejecutable de los tests muere.
  `MSG_NOSIGNAL` le pide a `send` que no la mande y que dé `EPIPE`. Winsock no tiene señales.
- `LoopbackPair::Open` devuelve un `std::optional<LoopbackPair>`, y meter el par en el
  `std::optional` mueve sus sockets. El par de adentro se queda con los números, y el de afuera,
  al destruirse, los cierra: los 10 tests del par fallan en el `SetUp` o en el primer uso.

## 12. Carril R

En `004e87b0_FUN_004e87b0.asm` (build `c32bbd5f`) y en su decompilado, `004e87b0_FUN_004e87b0.c`.

**Respuestas:**

- **`EBX` lleva lo mandado**, y el `XOR` de `0x4e87b4` lo pone en 0 en cada llamada: lo que salió
  en un `FD_WRITE` anterior no cuenta, y el buffer no se corrió.
- **Las dos restas.** `0x4e8804` descuenta lo que salió de lo pendiente y `0x4e8808` lo guarda en
  `+0x2010`; la vuelta siguiente lo relee en `0x4e87c1` y le resta `EBX` otra vez en `0x4e87ca`.
  El puntero de `0x4e87ce`, `+0x10 + EBX`, sí avanza bien: lo que está mal es el largo, y que con
  `WSAEWOULDBLOCK` (`0x4e8828` salta a `0x4e8812`, que devuelve 1) lo pendiente se queda donde
  estaba, sin correrse al principio.
- **Los imports.** `0x6b8588` es `send` y `0x6b858c`, `WSAGetLastError`: el decompilado los llama
  `Ordinal_19` y `Ordinal_111`, sus ordinales en Winsock. `0x2733` es 10035, `WSAEWOULDBLOCK`.
- **Con signo.** `JLE` y `JG` saltan por `SF` y `OF`, la comparación con signo: un pendiente
  negativo no entra al lazo (`0x4e87bf`). Nada mira el largo que se le pide a `send`: con 2
  pendientes y 8 mandados, `0x4e87ca` da -6 y va como el `int` de `send`. Entre `0x4e87e6` y
  `0x4e87f7` hay una llamada más, a `0x4e8880` si `+0x4018` no es 0, que es un `RET` vacío: el
  modelo del jefe la deja afuera.
