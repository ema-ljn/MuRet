# Ola CL4.0: sockets no bloqueantes

La N.0 de la CL4 del cliente. Antes de armar el socket del cliente, la cola de envío y la sesión
con el ConnectServer y el GameServer, hace falta saber qué hace un socket TCP cuando no bloquea:
cómo se conecta sin esperar, cómo distingue "probá más tarde" de un error, qué quiere decir un
`recv` que da 0, qué pasa cuando `send` toma menos de lo pedido, y por qué el `main.exe` repite y
pierde bytes al mandar lo que le quedó (Q-C11). Lo bloqueante (`socket`, `bind`, `listen`,
`accept`, `connect`, y el RAII sobre el descriptor) ya está en la
[ola 2 del servidor](ola-2-sockets.md#21-sockets-portabilidad-y-raii): esta guía lo repasa y sigue
desde ahí. Plan (tiene las respuestas: abrilo después):
[CL4](../superpowers/plans/2026-11-02-ola-cl4-red-del-cliente.md#sub-ola-cl40-sockets-no-bloqueantes-n0-en-labs).

**Resuelta por Claude** el 2026-11-02, a tu pedido: el código está en
[`labs/cl4.0-sockets/`](../../labs/cl4.0-sockets/), y las respuestas de las 12 actividades, con
las salidas reales, en su [README](../../labs/cl4.0-sockets/README.md). Si la hacés vos, abrilo
después de cada actividad, no antes. La sección final de esta guía sigue siendo tuya.

Qué cubre:

- **Teoría:**
  - bloqueante contra no bloqueante, y por qué un cliente con una sola conexión no necesita hilos
    ni IOCP: le alcanza con preguntar una vez por frame (D-C5);
  - el `connect` no bloqueante como una máquina de estados (en curso, conectado, rechazado), y
    `SO_ERROR` para saber cuál;
  - "probá más tarde" (`EWOULDBLOCK`, `EAGAIN`, `WSAEWOULDBLOCK`) contra un error de verdad, y el
    `recv` que da 0 (el otro lado mandó su FIN);
  - el `send` que toma menos de lo pedido, y la cola con cursor que manda el resto después;
  - los estados de una conexión (cerrada, conectando, abierta) y los motivos para volver a
    cerrada.
- **C++:**
  - RAII sobre un descriptor del sistema: el destructor cierra, se mueve con `std::exchange` y no
    se copia;
  - un `#ifdef _WIN32` en un solo archivo, con un handle y un enum iguales en los dos sistemas;
  - `std::span` sobre el buffer de `send` y `recv`, y `std::function` para inyectar el `send`;
  - un test con sockets de verdad en un solo hilo, que espera al sistema con un tope.
- **Lectura:** *Beej's Guide to Network Programming*, "Blocking" y `poll()`; las páginas de
  `connect(2)` y `socket(7)` de Linux; Microsoft Learn, `connect`, `select` y `WSAPoll`.

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | un `recv` bloqueante adentro del bucle de la CL3 |
| 2 | idea | bloqueante y no bloqueante, y una conexión consultada una vez por frame |
| 3 | misión | `Labs::LabSocket`: RAII sobre el descriptor |
| 4 | idea | el `connect` como máquina de estados, "probá más tarde" y el `recv` en 0 |
| 5 | misión | `Labs::LoopbackPair`: dos sockets en `127.0.0.1`, en un hilo |
| 6 | misión | un puerto cerrado, y `SO_ERROR`, que se borra al leerlo |
| 7 | idea | el `send` que toma menos, la cola con cursor y los estados de una conexión |
| 8 | misión | `Labs::DrainQueue` |
| 9 | jefe | el drenaje de `FD_WRITE` de `0x4e87b0` (Q-C11) |
| 10 | comparar | un socket por frame, un hilo por conexión, IOCP o epoll; `select`, `poll` y `WSAPoll` |
| 11 | rompelo | `MSG_NOSIGNAL`, un movimiento sin `exchange`, un `recv` vacío y un `connect` sin `SO_ERROR` |
| 12 | detective (carril R) | las dos restas de `0x4e87b0` en el desensamblado |

Cada actividad termina en una pregunta. Las respuestas no están en la guía: salen de lo que
corrés. El plan y el [03](../../Client/docs/research/03-red-y-cifrado.md) tienen algunas, así que
abrilos después de predecir.

## 1. Calentamiento: un `recv` que espera

El bucle de la CL3 corre, en cada frame, el `Poll`, los pasos y el dibujo. Suponé que el `Poll` lee
el socket con un `recv` bloqueante, como el servidor de la ola 2:

```cpp
void Poll()
{
    std::array<char, 256> buffer{};
    const int received = recv(m_socket, buffer.data(), static_cast<int>(buffer.size()), 0);
    // ... lo que llegó va al corte de paquetes
}
```

El ConnectServer manda el saludo apenas acepta, contesta la lista cuando el cliente la pide, y
después no manda nada hasta que el cliente elija un servidor. El cliente pide la lista y elige en
un paso, cuando ve lo que llegó.

**Predecí, antes de seguir:**
- ¿qué hace el primer frame? ¿Y el segundo, y el tercero?
- ¿qué ve el jugador, y qué dice Windows de la ventana?

**Pregunta:** sin cambiar el protocolo, ¿qué dos salidas hay?

## 2. Idea: no bloquear, y preguntar una vez por frame

Un socket nace **bloqueante**: `connect` espera el handshake, `recv` espera que llegue algo y
`send` espera que haya lugar en el buffer del sistema. El servidor de la ola 2 lo resolvía con un
hilo por conexión. En un juego, el hilo que espera es el del bucle.

Con el socket **no bloqueante** (`O_NONBLOCK` con `fcntl`, o `FIONBIO` con `ioctlsocket`), ninguna
de esas llamadas espera: hace lo que puede ahora, y si no puede nada, falla con un código que
quiere decir "probá más tarde". El sistema sigue trabajando en el fondo: el handshake avanza, los
bytes que llegan se juntan en el buffer de recepción y los del buffer de envío salen. El programa
pregunta cuando quiere.

Para un cliente con una conexión, "cuando quiere" es **una vez por frame**: el `Poll` de la CL4.1,
en el mismo hilo que la lógica (D-C5). Sin hilos no hay locks, ni colas entre hilos, ni carreras,
y un test puede correr los dos lados de la conexión en un hilo. Lo que se paga: lo que llega espera
hasta el próximo frame para leerse, y cada frame hace al menos un `recv`, aunque no haya nada.

El original hacía otra cosa: `WSAAsyncSelect` le pide a Winsock que avise con un mensaje de
ventana (`0x400`) cuando hay algo para leer, lugar para mandar o un cierre, y los avisos llegan por
el bucle de mensajes, también en un solo hilo (02).

**Pregunta:** a 60 FPS, ¿cuántos `recv` por segundo hace un `Poll` con un socket, y cuánto tiempo
se lleva si cada uno cuesta unos cientos de nanosegundos? ¿Y un servidor que preguntara así a
10.000 conexiones?

## 3. Misión: `Labs::LabSocket`

**El contrato:** un socket TCP de IPv4 dueño de su descriptor: `OpenTcp()`, `Close()`,
`SetNonBlocking()`, `BindLoopback(port)`, `LocalPort()`, `Listen()` y `Accept()`. Lo del `connect`
y los datos llega en la actividad 5. Se mueve y no se copia, y el movido queda vacío. Todo lo de
Winsock y lo de POSIX va en `LabSocket.cpp`; afuera hay un handle entero y un `IoStatus`, iguales
en los dos. Va en `labs/cl4.0-sockets/`. Antes, repasá la
[2.1 del servidor](ola-2-sockets.md#21-sockets-portabilidad-y-raii).

**Te toca**, tests primero (`test_lab_socket.cpp`):

1. Uno vacío no tiene descriptor, y todo da `false` o 0.
2. `bind` a `127.0.0.1:0` y `getsockname` dan un puerto distinto de 0.
3. Un socket movido no cierra el descriptor que se llevó el otro. ¿Cómo lo ves desde afuera, sin
   mirar el número del descriptor? (Pista: el puerto.)
4. El destructor cierra. ¿Cómo lo ves? (Pista: otro socket puede tomar el mismo puerto.)
5. Las implementaciones, primero en rojo y después en verde.

**Decisiones que pesás vos:**
- `SOCKET` es un entero sin signo del ancho de un puntero en Windows, e `int` en Linux. ¿Qué tipo
  usa la interfaz, y qué valor es "vacío" en los dos?
- si el sistema no da un socket, ¿`OpenTcp` tira una excepción, devuelve un `Expected` o uno
  vacío?
- `Close` en uno vacío, o dos veces: ¿qué pasa?

## 4. Idea: el `connect`, "probá más tarde" y el `recv` en 0

**El `connect` que no bloquea** vuelve enseguida, casi siempre con un error que no es un error:
`EINPROGRESS` en Linux, `WSAEWOULDBLOCK` en Windows. El handshake sigue en el fondo, y el
`connect` pasa a ser una máquina de estados:

```text
           connect               SO_ERROR en 0
cerrado ------------> en curso ----------------> conectado
                         |
                         +----------------------> rechazado (ECONNREFUSED), u otro error
```

Para saber en qué estado está sin esperar, se pregunta si el socket quedó **escribible** (`poll`
con `POLLOUT`, o `select` con tiempo 0), y si sí, se lee `SO_ERROR` con `getsockopt`: 0 es
conectado, y otro valor es el error del `connect`. Escribible no quiere decir conectado: en Linux,
un `connect` rechazado también queda escribible. En Windows, uno rechazado no queda escribible:
aparece en las **excepciones** de `select`. Y `SO_ERROR` **se borra al leerlo**.

**"Probá más tarde" contra un error.** En un socket no bloqueante, un `recv` sin nada para leer y
un `send` sin lugar fallan con `EWOULDBLOCK` o `EAGAIN` (en Linux son el mismo número, pero POSIX
no lo garantiza) o con `WSAEWOULDBLOCK`. No es un error: es "ahora no". `EINTR`, una señal que
interrumpió la llamada, tampoco. Lo demás sí: `ECONNRESET` (el otro lado cortó con un RST),
`EPIPE`, `ENOTCONN`, y sus `WSA*`.

**El `recv` en 0** no es "no hay nada" (eso es "probá más tarde"): es que el otro lado mandó su
FIN, cerró su mitad de la conexión, y no va a llegar nada más. Un cliente que lo trata como "no hay
nada" lee 0 en cada frame para siempre.

**Pregunta:** ¿qué resultados puede dar un `recv` no bloqueante, y qué hace el `Poll` con cada
uno? ¿Por qué un `recv` con un buffer de 0 bytes es una trampa?

## 5. Misión: `Labs::LoopbackPair`

**El contrato:** `LoopbackPair::Open()` abre un listener no bloqueante en `127.0.0.1:0` y un
cliente que le conecta sin bloquear; `Accept()` acepta al cliente si ya llegó. Los dos lados son no
bloqueantes y están en el mismo hilo: el test pregunta, y el sistema avanza en el fondo. Es el "una
vez por frame" de D-C5 sin el frame. `LabSocket` suma `ConnectLoopback`, `CheckConnect`, `Send` y
`Receive`, que devuelven un `IoResult`: el `IoStatus`, los bytes y el error del sistema.

**Te toca**, tests primero (`test_loopback_pair.cpp`):

1. El `connect` vuelve enseguida: en curso, o ya conectado.
2. Después del `accept`, el cliente está conectado: `CheckConnect` da `Done`.
3. Un `recv` sin nada da "probá más tarde", en los dos lados.
4. 5 bytes mandados de un lado llegan como 5 del otro.
5. Cerrar un lado hace que el otro lea 0.

**Predecí, antes de correrlo:** ¿el cliente queda conectado antes o después de que el servidor
llame a `accept`?

**Decisiones que pesás vos:**
- el test espera que el sistema haga algo (un `accept`, un rechazo). ¿Cuánto espera, y cómo, sin
  volverse lento ni frágil?
- TCP es un flujo: ¿5 bytes mandados juntos llegan en una sola lectura? ¿Cómo escribís el test
  para que no dependa de eso?

## 6. Misión: un puerto cerrado

Un listener que se abre, dice su puerto y se cierra: ahí no escucha nadie. **Te toca** el test: el
`connect` a ese puerto termina en `ECONNREFUSED` o `WSAECONNREFUSED`, desde la misma llamada (en
Linux puede pasar) o en `SO_ERROR`. En Windows tarda unos 2 s, porque reintenta: el test espera
hasta 5 s reales.

**Predecí:** después de un `CheckConnect` que dio `Refused`, ¿qué da el siguiente?

**Pregunta:** si el `Poll` de la CL4.1 llama a `CheckConnect` en cada frame mientras conecta,
¿dónde tiene que guardar lo que dio, y por qué?

## 7. Idea: el `send` que toma menos, y los estados de una conexión

Un `send` copia los bytes al **buffer de envío** del sistema y vuelve; el sistema los manda
después. Si el otro lado no lee, ese buffer se llena: un `send` bloqueante espera, y uno no
bloqueante toma **lo que entra**, que puede ser menos de lo pedido, o falla con "probá más tarde"
si no entra nada. Lo que no entró es problema del programa: si lo tira, el otro lado ve un paquete
cortado y desde ahí todo corrido; si lo manda otra vez desde el principio, ve bytes repetidos.

La **cola con cursor** guarda lo que falta y un índice a lo próximo que sale:

```text
| ya salió | pendiente | libre |
           ^           ^
         m_begin     m_end
```

Cada `Flush` manda desde el cursor y lo corre lo que tomó el `send`. Lo nuevo va detrás de lo
pendiente, para que el orden en el cable sea el orden en que se mandó. Con "probá más tarde", el
cursor queda donde estaba hasta el próximo frame (en el original, hasta el próximo `FD_WRITE`).

**Los estados de una conexión**, juntando todo:

```text
cerrada --connect--> conectando --SO_ERROR en 0--> abierta
   ^                      |                           |
   +---- rechazada, ------+                           |
   |     u otro error                                 |
   +---- recv en 0 (FIN), un RST, otro error, --------+
         o una cola que no entra
```

Cada vuelta a cerrada tiene su motivo, y el log lo tiene que decir (la CL4.5).

**Pregunta:** si el otro lado no lee nunca, ¿cuántos bytes entran en loopback antes del primer
"probá más tarde"? ¿Qué hace la cola cuando se llena, y qué hacía el original?

## 8. Misión: `Labs::DrainQueue`

**El contrato:** `Push(bytes)` agrega detrás de lo pendiente, o da `false` si no entra;
`Flush(send)` manda desde el cursor mientras el `send` tome algo, y devuelve `Done`, `TryLater` o
el estado del error; `Pending()` dice lo que falta. El `send` es un `std::function` que recibe un
`std::span` y devuelve un `IoResult`: el de `LabSocket` en un test, y uno que sigue un guion en
los demás. Va en `labs/cl4.0-sockets/`.

**Te toca**, tests primero (`test_drain_queue.cpp`): todo de una vez; un parcial que deja el resto
en orden; un `send` que no toma nada; llenar hasta el tope; que lo que salió libere lugar; lo nuevo
detrás de lo pendiente; un error que corta y deja lo pendiente; y una cola vacía que no llama al
`send`. Después, una cola sobre el `LoopbackPair`.

**Decisiones que pesás vos:**
- ¿de qué tamaño es el buffer, y crece? (El del original tiene `0x2000` bytes, fijos.)
- ¿cuándo corrés lo pendiente al principio: en cada `Flush`, en cada `Push`, nunca?
- un `send` que dice `Done` con 0 bytes: ¿seguís?

## 9. Jefe: el drenaje de `0x4e87b0` (Q-C11)

Cuando un `send` del original da `WSAEWOULDBLOCK`, el emisor copia lo que faltó al buffer de envío
del objeto de red (`+0x10`, con lo pendiente en `+0x2010`). Cuando Winsock avisa `FD_WRITE` (hay
lugar), `0x4e87b0` lo drena. En el decompilado, con nombres:

```c
sent = 0;
while (pending > 0) {
    result = send(socket, buffer + sent, pending - sent, 0);
    if (result == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) return 1;
        /* el log "FD_WRITE Send Error" */
        close(); return 0;
    }
    if (result < 1) { close(); return 0; }
    sent += result;
    pending -= result;
}
return 1;
```

**Te toca** (`labs/cl4.0-sockets/jefe/test_original_drain.cpp`):

1. Un modelo chico de `0x4e87b0`, tal como está, sobre un buffer como el del original y un `send`
   falso que sigue un guion.
2. Con el buffer `00 01 02 … 09`, el guion `{2, 2, WSAEWOULDBLOCK}` y después "todo": **predecí**
   cuánto pide cada llamada, qué sale al cable en la primera vuelta y qué en la segunda. Escribilo
   como test.
3. Tu `DrainQueue` con el mismo guion: al cable, `00..09` una vez.
4. ¿Hace falta el segundo parcial para que falle? Probá `{2, WSAEWOULDBLOCK}`.
5. Y dos envíos seguidos sin `WSAEWOULDBLOCK`, `{2, 6}`: ¿qué largo pide la tercera llamada?

**Pregunta:** ¿por qué casi nadie veía este bug? (Pista: ¿cuándo corre `0x4e87b0`, y cuánto
pendiente tiene un `FD_WRITE` contra el lugar que avisa?)

## 10. Comparar: hilos, IOCP, `select` y `poll`

L-3 del camino: se construye desde cero, se compara y en el producto se usa el estándar, que acá
son los sockets del sistema (la pregunta 3 del plan). **Te toca:** anotarlo en `labs/README.md`.

**Preguntas:**
- un socket consultado una vez por frame, un hilo por conexión (la 2.3 del servidor), e IOCP o
  epoll (la ola 3): ¿qué gana cada uno, qué cuesta, y con cuántas conexiones conviene? ¿Cuál era
  el del original?
- para saber si un `connect` terminó, `select`, `poll` y `WSAPoll`: ¿cuál anda en los dos sistemas,
  cuál tiene un límite en el número del descriptor, y cuál no avisaba un `connect` rechazado?

## 11. Rompelo

Cuatro cambios de una línea, uno por vez, con los 29 tests del lab:

1. en Linux, sacá `MSG_NOSIGNAL` de los flags del `send`;
2. en el constructor de movimiento, copiá el handle en vez de usar `std::exchange`;
3. en `Receive`, sacá el `if (buffer.empty())`;
4. en `CheckConnect`, devolvé `Done` cuando el socket quedó escribible, sin leer `SO_ERROR`.

**Predecí** qué test falla con cada uno, y cómo, antes de correrlos.

**Preguntas:**
- ¿qué hace `SIGPIPE`, y por qué ese test no falla en un `EXPECT`?
- con el movimiento roto fallan tests que no mueven nada a mano: ¿quién mueve los sockets?
  (Pista: ¿qué devuelve `LoopbackPair::Open`?)

## 12. Detective (carril R): las dos restas de `0x4e87b0`

El drenaje está en el repo de research,
`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/004e87b0_FUN_004e87b0.asm`, y en
Ghidra, abriendo `mainexe-vmxp-c32bbd5f/original/main.exe`. Las instrucciones que importan:

```text
4e87b4 xor  ebx,ebx
4e87b7 mov  eax,[esi+0x2010]
4e87bd test eax,eax
4e87bf jle  0x4e8812
4e87c1 mov  eax,[esi+0x2010]
4e87ca sub  eax,ebx
4e87ce lea  ecx,[esi+ebx+0x10]
4e87d5 call [0x6b8588]
4e87db mov  edi,eax
4e87dd cmp  edi,-0x1
4e87e0 jz   0x4e881b
4e87e2 test edi,edi
4e87e4 jle  0x4e883f
4e87fc mov  ecx,[esi+0x2010]
4e8802 add  ebx,edi
4e8804 sub  ecx,edi
4e8808 mov  [esi+0x2010],ecx
4e8810 jg   0x4e87c1
4e8814 mov  eax,0x1
4e881a ret
4e881b mov  edi,[0x6b858c]
4e8821 call edi
4e8823 cmp  eax,0x2733
4e8828 jz   0x4e8812
```

**Preguntas:**
- ¿qué registro lleva lo mandado, y en cuánto arranca en cada `FD_WRITE`?
- ¿de dónde sale el largo de la segunda llamada? Seguí `[esi+0x2010]` desde `0x4e8808` hasta
  `0x4e87ca`. ¿El puntero de `0x4e87ce` avanza bien?
- ¿qué hay en `0x6b8588` y en `0x6b858c`? (Pista: el decompilado los llama por ordinal.)
- `JLE` y `JG`: ¿con signo o sin signo? ¿Algo mira el largo que se le pide a `send`?

## El C++ que entra

- **RAII sobre un descriptor:** el destructor llama a `Close()`, que con uno vacío no hace nada.
  `std::exchange(other.m_handle, InvalidSocket)` toma el handle y deja al otro vacío en una línea.
  La copia está borrada: dos dueños cerrarían dos veces el mismo número, y el segundo `close`
  podría cerrar otro descriptor que el sistema ya repartió con ese número.
- **Un handle portable:** en un `std::uintptr_t` entran un `SOCKET` (un `UINT_PTR`) y un `int`.
  El `-1` de POSIX convertido a `std::uintptr_t` da `~0`, que es `INVALID_SOCKET`: "vacío" es el
  mismo valor en los dos, con un solo `static_cast`.
- **El `#ifdef _WIN32` en un archivo:** `winsock2.h` con `WIN32_LEAN_AND_MEAN` y `NOMINMAX`, y lo
  que cambia (`closesocket` o `close`, `WSAGetLastError` o `errno`, qué código es cada caso)
  detrás de funciones con el mismo nombre en los dos lados.
- **`std::span<const std::byte>`** para `send` y `std::span<std::byte>` para `recv`: el largo viaja
  con el puntero, y el `reinterpret_cast<const char*>` queda solo en la llamada del sistema.
  Winsock toma el largo como `int`: se recorta a `INT_MAX` en vez de pasar un `size_t`, que en
  MSVC daría C4267.
- **`std::function` para inyectar el `send`:** la cola no sabe si manda a un socket o a un guion.
  Un `std::function` copia lo que recibe: para que el test vea lo que anotó su `send` falso, le
  pasa una lambda que llama al original, no una copia.
- **Un enum para el resultado:** `IoStatus` sobre `std::uint8_t`, con un `PrintTo` en su namespace
  para que GoogleTest lo muestre con nombre y no como un byte.
- **Un test que espera al sistema:** una función que pregunta de a 1 ms hasta que pasa o hasta un
  tope de 5 s de reloj real (`steady_clock`), en vez de dormir un tiempo fijo.

## Ejemplo mínimo (fuera del proyecto)

Un pipe no bloqueante entre un programa y un compresor que corre aparte, en POSIX: el mismo "probá
más tarde" y el mismo 0 al cerrar, sin red.

```cpp
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <unistd.h>

enum class ReadStatus { Data, TryLater, Closed, Failed };

bool SetNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Lee lo que el compresor escribió de su progreso, sin esperar.
ReadStatus ReadProgress(int pipe, char* buffer, std::size_t size, std::size_t& got)
{
    const ssize_t n = read(pipe, buffer, size);
    if (n > 0)
    {
        got = static_cast<std::size_t>(n);
        return ReadStatus::Data;
    }
    if (n == 0)
    {
        return ReadStatus::Closed; // el compresor terminó y cerró su punta
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    {
        return ReadStatus::TryLater;
    }
    return ReadStatus::Failed;
}
```

**Pregunta:** una barra de progreso que llama a `ReadProgress` una vez por frame, ¿qué hace con
cada resultado? ¿Qué le pasa al compresor si escribe más rápido de lo que el programa lee?

## Errores típicos

- Tratar "probá más tarde" como un error y cerrar, o un error como "probá más tarde" y seguir.
- Tratar el `recv` en 0 como "no hay nada" (actividad 4).
- Leer `SO_ERROR` dos veces y creerle a la segunda (actividad 6).
- Creer que escribible quiere decir conectado: sin `SO_ERROR`, un rechazo pasa por conectado
  (actividad 11).
- Suponer que un `send` toma todo, o mandar otra vez desde el principio lo que tomó a medias
  (actividad 9).
- Olvidarse de `MSG_NOSIGNAL` en Linux: el primer `send` a uno que cerró mata el proceso.
- Comparar un `SOCKET` con `< 0`, o leer `errno` después de otra llamada que lo pisa (la 2.1).
- Un test con sockets de verdad que duerme un tiempo fijo: lento en una máquina y frágil en otra.

## Cómo lo hacía el original

- Un socket IPv4 y TCP (`0x4e82e0`), con un `connect` todavía bloqueante y después
  `WSAAsyncSelect` con `FD_READ`, `FD_WRITE` y `FD_CLOSE` como el mensaje `0x400` (`0x4e8570`):
  desde ahí, no bloqueante, con los avisos por el bucle de mensajes.
- Leía con un `recv` por cada `FD_READ` (`0x4e88a0`), y con 0 o -1 volvía sin cerrar: el cierre lo
  hacía `FD_CLOSE`.
- Mandaba con `send` en cada emisor; con `WSAEWOULDBLOCK` copiaba lo que faltaba a un buffer fijo
  de `0x2000` bytes, y si no entraba, cerraba.
- Lo drenaba con `FD_WRITE` en `0x4e87b0`, restando dos veces lo mandado y sin correr el buffer:
  con un parcial, repite los primeros bytes y pierde los últimos (Q-C11, el jefe).
- Cerraba con `SO_LINGER` en `{1, 0}` (`0x4e8380`): `closesocket` corta con un RST en vez de un
  FIN.

## Para leer

- *Beej's Guide to Network Programming*: "Blocking", y `poll()` y `select()`.
- Linux: `connect(2)` (`EINPROGRESS`, y leer `SO_ERROR` después de `poll`), `send(2)`
  (`MSG_NOSIGNAL` y `EPIPE`), `socket(7)` (`SO_ERROR`) y `poll(2)`.
- Microsoft Learn: `connect` (la parte de los sockets no bloqueantes), `select` (qué conjunto dice
  qué), `WSAPoll`, `WSAAsyncSelect` y "Windows Sockets Error Codes".
- MuMain, `Core/Platform/NonBlockingSocket.h`: un socket no bloqueante que el bucle consulta por
  frame (la referencia de estructura del plan).

## Preguntas para chequearte

1. ¿Qué cambia en `connect`, `recv` y `send` cuando el socket no bloquea? ¿Qué sigue haciendo el
   sistema en el fondo?
2. ¿Por qué un cliente con una conexión no necesita hilos ni IOCP? ¿Qué paga a cambio?
3. ¿Cómo sabés, sin esperar, si un `connect` terminó y cómo terminó? ¿Por qué hay que guardar el
   resultado?
4. ¿Qué diferencia hay entre "probá más tarde", un error y un `recv` en 0?
5. ¿Qué hace un `send` no bloqueante con el buffer del sistema casi lleno, y qué tiene que hacer el
   programa con lo que no entró?
6. ¿Qué bytes salen al cable con el drenaje del original y el guion `{2, 2, WSAEWOULDBLOCK}`, y por
   qué?
7. ¿Qué pasa si un socket con RAII se copia? ¿Y si el movimiento no deja vacío al otro?

## Hacia el cliente

Según el plan, la CL4.1 lleva el lab a `Client/src/Network/`: `LabSocket` pasa a `ISocket` y
`SystemSocket`, con el único `#ifdef _WIN32` en `SocketApi.h`, y la `DrainQueue`, a la
`SendQueue`, el buffer de `FD_WRITE` sin el bug. El `Poll()` que `GameLoop` llama una vez por frame
(el PR C de la CL4.1) es el "una vez por frame" de la actividad 2. Lo que el lab encontró y el
cliente tiene que respetar:

- `SO_ERROR` se lee una vez, y lo que da se guarda en el estado de la conexión (actividad 6);
- un `recv` con un buffer vacío no se llama (actividad 11);
- un `send` que toma 0 sin error es "probá más tarde", no un lazo (actividad 8);
- en Linux, cada `send` lleva `MSG_NOSIGNAL` (actividad 11);
- en Windows, un `connect` rechazado tarda unos 2 s y aparece en las excepciones de `select`
  (actividades 6 y 10).

## Al cerrar la CL4.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Qué estados tiene mi `connect`, y qué hago con `SO_ERROR`:**
- **Qué vi en el jefe: qué bytes repite y pierde el original, y con qué guion:**
- **Lo que anoté al comparar un socket por frame con hilos e IOCP, y `select` con `poll`:**
- **Qué rompió cada cambio de la actividad 11:**
- **Preguntas que me quedaron para más adelante:**
