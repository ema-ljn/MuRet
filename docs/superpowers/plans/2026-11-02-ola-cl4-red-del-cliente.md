# Plan de la ola CL4: Red del cliente

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-02, con lo aprendido en la CL2 y la CL3. Cada sub-ola trae su contrato, sus
> nombres y sus tests; lo que se aprenda en una se escribe acá antes de arrancar la siguiente.
>
> **Quién escribe los pasos `TODO(human)`:** por pedido de Emanuel (2026-11-02, pregunta 1 de la
> CL2), Claude los resuelve también, hasta que él diga que participa. En
> [PROGRESS](../../learning/PROGRESS.md) quedan como resueltos por Claude.

**Objetivo:** que el cliente haga lo que hace el `main.exe` desde que abre la conexión hasta que
el GameServer acepta el login, y lo sostenga con el keepalive: conectarse al ConnectServer, pedir
la lista, elegir el servidor, leer la IP del GameServer, conectarse a él, leer el saludo, mandar el
login cifrado y verlo aceptado, y desde ahí mandar el keepalive cada 20 s. Todo con un socket TCP
no bloqueante que el bucle de la CL3 consulta una vez por frame, en un solo hilo (D-C5), y con las
piezas de la CL2 (`PacketReader`, `PacketWriter`, `EncryptPacket`, `IObfuscation`, los paquetes
del login) puestas en el orden del original. Se prueba con un ConnectServer y un GameServer
falsos que responden con los bytes del cable, sin red y con sockets reales en `127.0.0.1`, y a mano
contra el GS del laboratorio con la capa FoxAnti.

**Arquitectura:** tres piezas nuevas, una por capa, como dice el roadmap.
- `Client::Network` suma el socket: `ISocket` (la interfaz), `SystemSocket` (Winsock o BSD, con
  el único `#ifdef _WIN32` en `SocketApi.h`), la `SendQueue` (el envío que no entró, como el
  buffer de `FD_WRITE`) y la `Connection`, el camino de un paquete por una conexión: el socket, el
  `PacketReader` con su ofuscación, el descifrado y los seriales, y al mandar el cifrado, el
  `BeforeSend` y la cola. Network no ve a Protocol: lo que no se pudo descifrar sale como un
  evento, y el aviso `F1 03` lo manda quien la usa.
- `Client::Protocol` suma el `Dispatcher`: una tabla opcode → handler, con subcódigos, y el
  opcode desconocido al log una vez.
- `Client::App` suma la `ServerSession`, la máquina de estados de la entrada: ConnectServer,
  GameServer, login y keepalive. Tiene una `Connection`, dos `Dispatcher` (uno por servidor: el
  opcode `00` es el saludo en el CS y el chat en el GS) y el reloj del bucle. `main.cpp` la arma y
  la consulta desde el `ILoopClient`, que suma un `Poll()` por frame.

Todo va en `ClientLib` y se prueba sin SDL3: los sockets del sistema no son de la ventana
(pregunta 4). La CL4.0, como toda N.0, vive en `labs/`.

**Stack:** C++20 (`std::span`, `std::variant`, `std::function`, `std::chrono`), sockets del
sistema (Winsock 2 en Windows, BSD en Linux: `socket`, `connect`, `getaddrinfo`, `select` o
`poll`, `recv`, `send`, `fcntl` o `ioctlsocket`), GoogleTest. Ninguna biblioteca nueva: la
pregunta 3 compara asio, SDL3_net y los sockets del sistema (`CODING_RULES.md` §10).

**Guía de estudio:** `docs/learning/ola-cl4.0-sockets-no-bloqueantes.md`, que se escribe al
arrancar la CL4.0 (L-10). De la CL4.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [03-red-y-cifrado](../../../Client/docs/research/03-red-y-cifrado.md): los sockets, la cadena de
  recepción, el envío, C3/C4 y los seriales, y la capa de FoxAnti en su orden.
- [04-protocolo](../../../Client/docs/research/04-protocolo.md): el dispatcher `0x4e2b10` y sus
  tres niveles, el flujo de conexión del CS y del GS con los bytes del cable, el keepalive y "Cómo
  se verificó".
- [02-arranque-y-ciclo-de-vida](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md): el
  WndProc (`WM_USER` 0x400 y `WM_TIMER` 0x113), el keepalive "por reloj", la conexión "por frame",
  y que sin foco la red y el keepalive siguen.
- [07](../../../Client/docs/research/07-quirks-y-bugs.md): Q-C11 (el drenaje de `FD_WRITE`),
  Q-C16 (el `A9`), Q-C17 y Q-C18 (FoxAnti), Q-C04 y Q-C10 (los seriales y el rearmado, ya
  corregidos en la CL2), Q-C42 (los handlers de la conexión, ya corregidos en la CL2.5).
- [08](../../../Client/docs/research/08-pendientes.md): P-C04 (la lista, del lado de `main.exe`),
  P-C05 (el diferencial del login y del keepalive), P-C10 (contra qué servidor) y P-C11 (el `A9`).
- En `client-research/` del repo de research, el decompilado de `mainexe-vmxp-c32bbd5f`:
  `004e82e0`, `004e8570`, `004c1340`, `004e88a0`, `004e87b0`, `004e8380`, `004e8270`, `004e81b0`,
  `004e1d50`, `004ad100`, `004bee60`, la rama `0x400` y la `0x113` de `004af4e0` (el WndProc) y
  el `SetTimer` de `004b9b80`. Como referencia de estructura, `Core/Platform/NonBlockingSocket.h`
  y `LocalSocket.h` de MuMain: un socket no bloqueante que el bucle consulta por frame, con el
  "probá más tarde" igual en Windows y en POSIX.

**Lo que dice el binario de la red** (*estático*, leído en el decompilado para este plan; la
CL4.R lo confirma en el desensamblado y lo pasa al research). Lo que dice *verificado* ya se miró
en el desensamblado con objdump en la revisión del plan:

| Qué | Dónde | Qué hace |
| --- | --- | --- |
| iniciar | `0x4e81b0`, desde `0x4c1340` la primera vez (`DAT_057636bc`) | `WSAStartup`, con los errores en coreano (03) |
| crear | `0x4e82e0` | `socket(2, 1, 0)`: IPv4 y TCP. Con el segundo argumento distinto de 0, pone `DAT_05767918` en 0 |
| conectar | `0x4e8570` | `inet_addr` y, si no es una IP, `gethostbyname`; si el nombre no resuelve **devuelve 2**, y `0x4c1340` solo mira el 0, así que sigue como si hubiera conectado. **Rechaza `127.0.0.1`** (los cuatro bytes `7F 00 00 01`: devuelve 0). Llama a `connect` y después a `WSAAsyncSelect(sock, ventana, 0x400, 0x23)`: `FD_READ`, `FD_WRITE` y `FD_CLOSE` como el mensaje `0x400`. Como el socket es bloqueante hasta `WSAAsyncSelect` y no pide `FD_CONNECT`, el `connect` espera (*verificado*) |
| el que conecta | `0x4c1340` | loguea `Connect to Server`, crea el socket, conecta; si falla, loguea `Failed to connect` y va al objeto `0x70` (`0x65ee60`, `0x65fe50`); pone el serial de envío en 0 (03). Tiene un segundo llamador, `0x49df01`, con el 55980 (*verificado*; para qué, CL4.R) |
| los eventos | `0x4af4e0`, mensaje `0x400` | `lParam` 1 (`FD_READ`) → `0x4e88a0`; 2 (`FD_WRITE`) → `0x4e87b0`; `0x20` (`FD_CLOSE`) → un mensaje en pantalla (`0x555190`), el log, `0x4e8380` y el objeto de `0x65ee60` |
| leer | `0x4e88a0` | un `recv` al buffer de `0x2000` (`+0x2014`, lo que hay en `+0x4014`); con 0 o -1 vuelve sin cerrar (lo cierra `FD_CLOSE`); con menos de 3 bytes espera; si no, `FoxAnti.decode_inbound` y el corte |
| despachar | `0x4e1d50`, en cada vuelta del bucle de `WinMain`, con foco o sin él (02) | saca de la cola **todos** los paquetes (`do { pop } while`), descifra los C3/C4 y llama al dispatcher |
| mandar | en cada emisor (por ejemplo, el keepalive de la rama `0x113` de `0x4af4e0`) | `send` en un lazo; con `WSAEWOULDBLOCK` (`0x2733`) copia lo que falta al buffer de envío (`+0x10`, el largo en `+0x2010`) si entra en `0x2000` bytes, y si no entra o con otro error, cierra (`0x4e8380`). El envío en línea (`0x4b1664`) llama a `send` sin mirar `+0x2010`: un paquete nuevo puede adelantarse a lo encolado (*verificado*; quirk nuevo, CL4.R) |
| drenar | `0x4e87b0`, con `FD_WRITE` | manda desde `buf + enviado` el largo `pendiente - enviado` con `pendiente` ya descontado: resta dos veces lo que salió, y con `WSAEWOULDBLOCK` vuelve sin correr lo que quedó al principio (Q-C11). Con otro error loguea `FD_WRITE Send Error` y cierra |
| cerrar | `0x4e8380` | `setsockopt(SOL_SOCKET, SO_LINGER, {1, 0})`, que hace que `closesocket` corte con un RST; vacía la cola de paquetes, loguea `Socket Closed, Clear PacketQueue` y `closesocket`. Es el que corre al pasar del CS al GS (`0x4e32f2`, sub 3) |
| cerrar, la otra | `0x4e8270` | `shutdown(sock, 1)`, `recv` hasta 0 y `closesocket`: el cierre ordenado que dice el 03; no tiene llamadores (*verificado*) |
| el keepalive | `SetTimer(ventana, 1000, 20000)` en `0x4b9b80`; la rama `0x113` con el id 1000 (`0x3E8`) | cada 20 s, si `DAT_05767918` no es 0 (solo mira eso, *verificado*). Lo pone en 1 `0x412b30` y en 0 `0x4e8520` (*verificado*; quién las llama al conectar al GS y al cerrar, CL4.R): el timer **no mira el login**. La rama también llama a `thunk_FUN_00450376` |
| el primero | el sub 1 de `F1` con resultado 1 (`0x4e2b10`) | `DAT_05767928 = 2` y `0x4ad100`, que llama a `0x4bee60` (el keepalive) si `DAT_05763570` es 0; lo que sigue en `0x4ad100` (los `FindWindowA` de programas de trampas) es del anticheat, fuera de alcance |
| por frame | `0x638640`, en la escena 5 | si `0x4e8560` (el socket) da -1, avisa que se cerró (02) |

**Terminada cuando** (roadmap: el cliente pasa el login contra el GS del laboratorio). En concreto:
- con un CS y un GS falsos, primero sobre un `FakeSocket` y después con sockets reales en
  `127.0.0.1`, el cliente intercambia los bytes del cable del 04 (el `A9`, el saludo, la lista,
  la elección, la dirección, el saludo del GS, el login, el resultado) y llega a `LoggedIn`;
- tras el saludo `C1 04 00 01` del CS sale `C1 04 F4 02` exacto, y con FoxAnti la lista del cable
  llega al dispatcher como `F4 06` con el código 400 y la elección sale como `C1 06 F4 03 00 00`;
- el primer keepalive sale con el login aceptado, y después uno cada 20 s de reloj falso y no
  antes, también minimizada; un frame de 60 s manda uno solo;
- un envío partido en `{2, 2, -1}` no repite ni pierde bytes, y un cierre del otro lado es una
  desconexión con su motivo en el log;
- a mano, `Main connect /u<ip del CS> /p44405 --data <Data>` con la cuenta en las variables de la
  pregunta 7: el log dice que el login fue aceptado y muestra un keepalive cada 20 s durante dos
  minutos, sin que el GS corte; sin host, `Main --frames 60` sigue andando como en CI.

## Preguntas para decidir al empezar

1. **Quién escribe la CL4.** *Decidido (Emanuel, 2026-11-02, en la CL2): Claude escribe todo*, la
   CL4.0 en `labs/` y los pasos `TODO(human)` incluidos, hasta que digas que participás. Las
   preguntas 2 a 9 siguen con su recomendación mientras no digas otra cosa.
2. **El orden de las sub-olas.** El roadmap dice CL4.1 socket, CL4.2 CS, CL4.3 GS, CL4.4
   keepalive, desconexión y cola de envío, CL4.5 `Dispatcher`. Dos cosas no cierran: un socket no
   bloqueante no puede mandar sin la cola (un `send` puede tomar menos de lo pedido), y el CS ya
   despacha tres opcodes. *Recomendación:* CL4.1 socket **y cola de envío** (lo dice el 03: "el
   envío que no entra se encola como en `FD_WRITE`"), CL4.2 `Dispatcher` (puro, sin red), CL4.3 CS,
   CL4.4 GS y login, CL4.5 keepalive y desconexión. Cada una sigue siendo un concepto, y la fila de
   la CL4 del roadmap se corrige con este plan.
3. **La biblioteca de sockets (§10).** Se miraron con Context7 (2026-11-02):
   - **asio standalone** (`/chriskohlhoff/asio`, puerto `asio` de vcpkg): solo headers sin Boost
     (`ASIO_STANDALONE`), y `io_context::poll()` corre los handlers listos sin esperar, así que
     entra en un frame. Pero el modelo es otro: operaciones asíncronas con handlers, buffers que
     tienen que vivir hasta el handler, y el roadmap del servidor la deja como comparación
     opcional (D-3, sub-ola 3.4).
   - **SDL3_net** (`/libsdl-org/sdlwiki`, `SDL3_net/`): todo no bloqueante, `NET_CreateClient`
     conecta en el fondo, la resolución de nombres va en hilos propios (es lo único que los
     sockets a mano no dan), y `NET_ReadFromStreamSocket` da 0 sin datos.
     `NET_WriteToStreamSocket` encola adentro lo que no sale, pero
     `NET_GetStreamSocketPendingWrites` dice cuánto quedó, así que el tope de `0x2000` del
     original se puede armar encima. El problema es otro: **suma SDL3 a `ClientLib`**, que hoy
     no lo ve (solo `ClientPlatform`), y con eso `Network` y sus tests dejan de correr sin SDL3
     (pregunta 4). Si su versión 3 está estable y en vcpkg se confirma en el paso 1 de la CL4.1.
   - **los sockets del sistema**: seis llamadas detrás de un `SocketApi` de pocas líneas, como el
     `NonBlockingSocket.h` de MuMain; `UniqueSocket` es justo la excepción que nombra L-3 (lo que
     el estándar no trae y se construye), y D-3 los pide a mano en el servidor.

   *Recomendación:* **los sockets del sistema**. Resuelven este problema (un socket TCP de
   cliente que se consulta por frame) sin sumar dependencias y sin traer SDL3 a `ClientLib`. Lo
   que la ola enseña no es la razón: ese ejercicio va en `labs/` (CL4.0) con cualquier biblioteca.
   Lo único que se pierde es la resolución en otro hilo (pregunta 5). La razón queda en el
   header de `SocketApi.h`, como pide §10.
4. **Dónde vive el socket.** *Recomendación:* en `ClientLib`, en `Client/src/Network/`: no es de
   la ventana, y así `Connection` y `ServerSession` se prueban sin SDL3. En Windows `ClientLib`
   enlaza `ws2_32`. Los tests con sockets reales usan `127.0.0.1` y un puerto que elige el sistema
   (`bind` al 0): corren en CI en los dos sistemas, sin red de afuera.
5. **La resolución de nombres y el `connect`.** El original resuelve con `gethostbyname`, que
   espera, y conecta con el socket todavía bloqueante (*inferido*): el juego se congela mientras
   tanto. *Recomendación:* `getaddrinfo` con `AF_INET`, sincrónico como el original (el
   laboratorio usa IPs, que resuelven al instante), con lo que tardó en el log si pasa de 100 ms;
   el `connect` sí, no bloqueante, con un `ConnectTimeout` de 10 s medido con el `IClock` del
   bucle. MuRet no rechaza `127.0.0.1` (los tests lo usan) y un nombre que no resuelve es un error,
   no un "conectado" (los dos van a 07 como quirks nuevos en la CL4.R).
6. **Desde cuándo cuenta el keepalive.** El original tiene un timer de 20 s desde el arranque,
   que manda si hay conexión al GS aunque no haya login, y además uno al aceptar el login: en el
   cable, el segundo llegó a los 7,5 s del primero. *Recomendación:* el primero con el login
   aceptado, como el original, y después cada 20 s **contados desde ese**, solo con el login
   aceptado; si un frame tarda más (un breakpoint de 60 s), sale uno solo y el siguiente a los
   20 s. Es lo que pide el test del 02 ("cada 20 s de reloj simulado y no antes"), y la diferencia
   con el timer del original va a 07 (CL4.5).
7. **De dónde salen la cuenta y la clave antes de la CL5.** La escena de login llega en la CL5.5.
   *Recomendación:* dos variables de entorno, `MURET_CLIENT_ACCOUNT` y `MURET_CLIENT_PASSWORD`,
   solo para la prueba a mano: ni en la config (un archivo con una clave) ni en la línea de
   comandos (el historial y `ps`). Sin ellas y con host, `Main` termina con un error que nombra
   las dos. Una vacía o de más de 10 bytes (`WriteLoginRequest` daría `FieldTooLong`) es un error
   que la nombra sin mostrar su valor. En MSVC se leen con `_dupenv_s`, como
   `Client/tests/support/GoldenDir.cpp` (`std::getenv` da C4996 con `/WX`). El README las carga
   con `read -rs` en bash o `Read-Host -MaskInput` en PowerShell, para que la clave no quede en
   el historial. La cuenta y la clave no van al log ni tienen `operator<<` (como `LoginRequest`
   de la CL2.5), y los tests tienen un `PrintTo` que no las imprime: sin él, GoogleTest vuelca los
   bytes del struct, y con SSO la clave va ahí.
8. **Qué ofuscación por defecto.** D-C6 la elige por config según el servidor, y el único
   servidor contra el que se prueba es el del laboratorio (pregunta 4 del roadmap).
   *Recomendación:* `[Server] Obfuscation` con `foxanti` (por defecto) o `none`; otro valor es
   un error que dice la línea, como el resto de la config de la CL3.4. Cuando exista un servidor
   de MuRet, se revisa.
9. **Qué servidor elige sin la UI.** *Recomendación:* el primero de la lista, en el orden en que
   llega; una lista vacía es un error que lo dice. La CL5.5 lo cambia por la elección del jugador;
   cómo agrupa y ubica `main.exe` cada servidor (P-C04) queda para esa sub-ola.

## Lo que más puede morder

Cada uno tiene su test en la sub-ola que lo dice. En la review se miran primero:

- **`SIGPIPE` en Linux.** Un `send` a un socket que el otro lado cerró manda la señal y, por
  defecto, mata el proceso. En Linux se manda con `MSG_NOSIGNAL`; en Windows no hay señal (CL4.1,
  `SystemSocketTest.SendingAfterThePeerClosedDoesNotKillTheProcess`).
- **El `connect` no bloqueante.** Vuelve con `EINPROGRESS` en Linux y con `WSAEWOULDBLOCK` en
  Windows, y termina cuando el socket queda escribible: el resultado está en `SO_ERROR`. En
  Windows un `connect` que falla no se ve como escribible sino en el conjunto de excepciones de
  `select` (por eso, en Windows, `select` con los dos conjuntos: `WSAPoll` no lo avisaba en
  versiones viejas). En POSIX va `poll`, porque `select` no puede con un descriptor desde
  `FD_SETSIZE`. Contra un puerto cerrado de `127.0.0.1`, en Linux el `connect` da `EINPROGRESS`
  y el rechazo llega en el primer `PollConnect`; en Windows puede tardar unos 2 s por los
  reintentos. El contrato deja que el `Refused` salga del mismo `StartConnect` si el sistema ya lo
  sabe, pero en loopback no se vio. `SO_ERROR` se borra al leerlo: el resultado final se guarda, o
  un segundo `PollConnect` después del rechazo daría `Connected`. El paso 2 de la CL4.1 lo midió
  y `AClosedPortIsRefused` lo prueba, con un tope de 5 s reales.
- **`recv` que da 0, que da -1 o que no tiene nada.** 0 es que el otro lado cerró; -1 con
  `EWOULDBLOCK`, `EAGAIN`, `WSAEWOULDBLOCK` o `EINTR` es "probá más tarde"; con `ECONNRESET`,
  `ECONNABORTED`, `EPIPE` o sus `WSA*`, en `recv` o en `send`, también cerró el otro lado (un kick
  con RST, como `0x4e8380`, da `ECONNRESET`), y con otro error, una falla. El original ignora el
  0 y espera el `FD_CLOSE` (CL4.1).
- **Un envío partido.** `send` puede tomar 2 bytes de 10; el original, con Q-C11, repite lo que ya
  salió y pierde la cola. La `SendQueue` lleva un cursor sobre un buffer fijo de `0x2000` y lo
  prueba con `{2, 2, -1}` (CL4.1). Una cola que pasa de `0x2000` bytes cierra la conexión, como el
  original (`SendQueueFull`). Con algo pendiente, nunca se manda directo: el original sí, en
  `0x4b1664`, y desordena (CL4.R).
- **Pasar del CS al GS.** El socket se cierra antes de abrir el otro, los seriales vuelven a 0
  (`0x4c1340`) y lo que quedó a medio cortar del CS no se mezcla con el GS: el `PacketReader`
  vuelve a empezar (CL4.3, `ReopeningStartsClean`). Lo que llegó en el mismo lote que el `F4 03`
  se tira: si no, lo que sigue del CS iría al dispatcher del GS, donde el `00` es el chat (CL4.3,
  `WhatFollowsTheAddressIsDropped`).
- **Un frame largo.** Un breakpoint de 60 s no manda tres keepalives seguidos (CL4.5), y un
  `connect` que tarda no congela el frame más que la resolución del nombre (pregunta 5).
- **La red en el modo fiel.** El `Poll()` va antes de `BeginFrame`, fuera de la medición, como el
  bucle de mensajes del original: con polls de 20 ms el juego se frena (48 frames y 56 pasos en 3 s,
  no 64 y 74). Si quedara adentro, el modo fiel mediría de más (CL4.1,
  `FaithfulDoesNotMeasureThePoll`). Un `Poll` después de los pasos también da 48 y 56, así que el
  test del orden corre en los dos modos.
- **La cuenta y la clave.** Nunca al log ni a la salida de un test que falla. Los tests usan solo
  `"user0001"` y `"pass0002"`, las de `MuClientNetFixtures.h`, y comparan lo que viene del cable
  con `SameBytesWithoutValues` (`SameBytes` muestra el byte distinto con su valor). El `PrintTo`
  de `Credentials` no imprime la clave, CTest limpia las variables de la pregunta 7
  (`ENVIRONMENT_MODIFICATION` con `unset:`), el log dice "login mandado" sin la cuenta, y la
  captura a mano queda fuera de Git (CL4.4 y CL4.5).
- **Un paquete malo no siempre corta.** Un error del corte (`PacketReader`) cierra la conexión,
  porque se perdió dónde empieza el siguiente; un `InvalidPacket` de FoxAnti o un C3 que no
  descifra se descartan y la conexión sigue (CL4.3 y CL4.4).

---

## Sub-ola CL4.0: sockets no bloqueantes (N.0, en `labs/`)

**Concepto:**
- un socket TCP de cliente: `socket`, `connect`, `send`, `recv`, `close`; qué hace cada uno cuando
  es bloqueante y cuando no, y por qué un cliente con una sola conexión no necesita hilos ni IOCP
  (D-C5): le alcanza con preguntar una vez por frame;
- el `connect` no bloqueante como una máquina de estados: en curso, conectado o rechazado, y
  `SO_ERROR` para saber cuál;
- "probá más tarde" (`EWOULDBLOCK`, `EAGAIN`, `WSAEWOULDBLOCK`) contra un error de verdad, y
  `recv` que da 0 (el otro lado mandó su FIN);
- un `send` que toma menos de lo pedido, y la cola con cursor que manda el resto después;
- los estados de una conexión: cerrada, conectando, abierta, y los motivos para volver a cerrada.

Del C++: RAII sobre un descriptor del sistema (el destructor cierra, se mueve y no se copia), un
`#ifdef _WIN32` en un solo archivo, `std::span` sobre el buffer de `send` y `recv`.

**Archivos:**
- Crear: `docs/learning/ola-cl4.0-sockets-no-bloqueantes.md` (la guía; Claude)
- Crear: `labs/cl4.0-sockets/LabSocket.h/.cpp` (el socket con RAII y el `#ifdef`),
  `LoopbackPair.h/.cpp` (un listener y un cliente en `127.0.0.1`, los dos no bloqueantes),
  `DrainQueue.h/.cpp`, sus tests (`test_loopback_pair.cpp`, `test_drain_queue.cpp`), `README.md`
  y `jefe/test_original_drain.cpp` (Emanuel)
- Modificar: `labs/CMakeLists.txt` (la biblioteca `MuRetLabsCl40`, que en Windows enlaza `ws2_32`,
  y el ejecutable `LabCl40Tests` con `gtest_discover_tests(... TEST_PREFIX "LabCl40.")`, como
  `LabCl30Tests`), `labs/README.md`

- [ ] **Paso 1 (Claude):** la guía, justo antes de empezar.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** `Labs::LabSocket`, RAII: uno movido queda vacío, el
  destructor cierra, y `SetNonBlocking`. Primero los tests: un socket movido no cierra el
  descriptor que se llevó el otro; `bind` a `127.0.0.1:0` y `getsockname` dan un puerto distinto
  de 0.
- [ ] **Paso 3 (Emanuel):** `Labs::LoopbackPair`: un listener en `127.0.0.1:0` y un cliente que
  conecta sin bloquear. Tests: el `connect` vuelve enseguida (en curso o ya conectado); después del
  `accept` el cliente queda escribible y `SO_ERROR` da 0; un `recv` sin nada da "probá más tarde";
  5 bytes mandados de un lado llegan los 5 del otro; cerrar un lado hace que el otro lea 0.
- [ ] **Paso 4 (Emanuel):** un puerto cerrado (un listener que se abre, se mira su puerto y se
  cierra): el `connect` termina con `ECONNREFUSED` o `WSAECONNREFUSED` en `SO_ERROR`. En Windows
  puede tardar unos 2 s: el test espera hasta 5 s reales.
- [ ] **Paso 5 (Emanuel, jefe, Q-C11):** el drenaje de `0x4e87b0` como está en el decompilado,
  contra una cola con cursor, los dos sobre un `send` falso que acepta lo que dice un guion. Con un
  buffer `00 01 02 … 09` y el guion `{2, 2, -1}` (el -1 es `WSAEWOULDBLOCK`) y después "todo", el
  original manda `00 01 02 03` en la primera vuelta y `00 01 02 03 04 05` en la segunda: al cable
  van `00 01 02 03 00 01 02 03 04 05`, con `00..03` repetidos y `06..09` perdidos (*análisis* del
  decompilado: la segunda llamada pide `8 - 2 = 6` en vez de 8, y la tercera vuelve sin correr el
  buffer). La cola con cursor manda `00..09` una vez. El test dice los dos.
- [ ] **Paso 6 (Emanuel; lo anota en `labs/README.md`):** comparar un socket por frame con un hilo
  por conexión y con IOCP o epoll (qué gana cada uno y con cuántas conexiones), y `select` contra
  `poll` y `WSAPoll` para saber si un `connect` terminó.
- [ ] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "LabCl40"
  ```

## Sub-ola CL4.R: la conexión del original, en el binario (P-C11, Q-C11)

**Concepto:** carril R antes de copiar el comportamiento. La tabla "Lo que dice el binario de la
red" salió del decompilado; esta sub-ola la confirma en el desensamblado, cierra lo que la CL4
necesita y lo pasa al research. Lo que no hace falta para el login (P-C04) queda para la CL5.5.

**Archivos:** modificar `Client/docs/research/03-red-y-cifrado.md` (los sockets: el `connect`,
los eventos, el envío y los dos cierres), `04-protocolo.md` (el `A9` y el keepalive),
`02-arranque-y-ciclo-de-vida.md` (el timer de 20 s), `07-quirks-y-bugs.md` (Q-C11 con los bytes,
Q-C16 y los quirks nuevos), `08-pendientes.md` (P-C11 y P-C05, y P-C04 pasa a la CL5.5), PROGRESS
y este plan.

- [ ] **Paso 1 (P-C11, Q-C16):** el `A9`. Leer `0x415be0`, `0x422990` y `0x424230` y sus
  llamadores: cuál corre al conectar al CS (el `A9 00 00 01` que sale antes del saludo en las 64
  conexiones del `muproxy.log`) y cuál en el GS (`A9 01 00 08`, diez veces en la sesión real), y
  qué son sus tres bytes. Si no se sabe qué son, se reproducen tal cual (07): el del CS en la
  CL4.3, y el del GS en la CL4.4 si sale antes del login aceptado; si sale después, en su ola. El
  `A9` mira `FUN_00659290(0x2000000)` (*verificado*): si eso resulta del anticheat, no se
  reproduce.
- [ ] **Paso 2 (el `connect`):** en `0x4e8570`, el orden de `connect` y `WSAAsyncSelect` (si el
  `connect` espera), el rechazo de `7F 00 00 01` y el 2 de un nombre que no resuelve, que
  `0x4c1340` no trata como error (los tres, *verificados*), y el segundo llamador de `0x4c1340`,
  `0x49df01`, con el 55980. Dos quirks nuevos en 07, con lo que hace MuRet (pregunta 5).
- [ ] **Paso 3 (los cierres):** `0x4e8380` con `SO_LINGER {1, 0}` (corta con un RST) y `0x4e8270`
  con `shutdown` y el drenaje, que no tiene llamadores (*verificado*). Corregir el 03, que los da
  a los dos como "cierre ordenado".
- [ ] **Paso 4 (Q-C11):** en el desensamblado de `0x4e87b0`, la resta doble y la vuelta con
  `WSAEWOULDBLOCK` sin correr el buffer: pasar a 07 los bytes del paso 5 de la CL4.0 en vez de
  "aritmética anómala". En el envío en línea del keepalive (`0x4af4e0`, alrededor de
  `0x4b1664`), confirmar qué copia al buffer de envío con `WSAEWOULDBLOCK` después de un envío
  parcial: el decompilado muestra el principio del paquete con el largo de lo que falta, y si es
  así, es el mismo defecto. Sumar a 07 el quirk nuevo: ese envío llama a `send` sin mirar
  `+0x2010` (*verificado*), así que un paquete nuevo sale antes que lo encolado; MuRet nunca
  manda directo con algo pendiente: lo va a probar `ConnectionTest.NewPacketsWaitBehindThePending`
  (CL4.3), y `SendQueueTest.NewBytesWaitBehindThePending` prueba solo la cola.
- [ ] **Paso 5 (la lectura y el cierre del otro lado):** en `0x4e88a0`, que un `recv` de 0 o de -1
  vuelve sin cerrar, y la rama `FD_CLOSE` del mensaje `0x400`: qué muestra (`0x555190`, el objeto
  de `0x65ee60`).
- [ ] **Paso 6 (el keepalive):** el `SetTimer(ventana, 1000, 20000)` de `0x4b9b80` (si es
  `WinMain` y cuándo corre), que la rama `0x113` solo mira `DAT_05767918` (puesto en 1 al conectar
  al GS) y no el login, quién llama a `0x412b30` (lo pone en 1) y a `0x4e8520` (en 0), qué hace
  `thunk_FUN_00450376` en esa rama, qué es `DAT_05763570` en `0x4ad100`, y qué velocidades manda
  `0x4bee60` antes de que haya un personaje: los tests de la CL4.5 las usan. Pasarlo al 04 y al
  02, y el quirk de la pregunta 6 a 07.
- [ ] **Paso 7:** el research queda con *estático* o *inferido* en cada cosa, y la tabla de este
  plan se corrige con lo que haya cambiado.

## Sub-ola CL4.1: socket no bloqueante portable consultado por el bucle

**Concepto:** E/S no bloqueante detrás de una interfaz chica, con una implementación del sistema y
una falsa para los tests; la cola de lo que no salió; y la consulta por frame en el `GameLoop`. Va
en tres PRs: **PR A**, el socket (`ISocket`, `IResolver`, `SystemSocket`, `FakeSocket`,
`LoopbackServer`); **PR B**, la `SendQueue`; **PR C**, el `Poll()` del bucle.

**Contrato** (03 § Sockets, la tabla de arriba y Q-C11):

| Qué | Original | MuRet |
| --- | --- | --- |
| iniciar | `WSAStartup` la primera vez que conecta (`0x4e81b0`) | `SocketApi::NetworkInit` con RAII (`WSAStartup(MAKEWORD(2, 2))` y `WSACleanup` en Windows, nada en Linux); lo crea `main.cpp`, y cada test con sockets el suyo (`WSAStartup` cuenta las llamadas) |
| crear | `socket(2, 1, 0)` (`0x4e82e0`) | lo mismo, IPv4 y TCP, y no bloqueante desde el principio |
| resolver | `inet_addr`, o `gethostbyname` que espera (`0x4e8570`) | un `IResolver`: el del sistema prueba `inet_pton` y, si no es una IP, `getaddrinfo` con `AF_INET`, que espera (pregunta 5); `ResolveFailed` si no da nada. Los tests usan uno falso |
| conectar | bloqueante (*verificado*), después `WSAAsyncSelect(…, 0x400, 0x23)` | `StartConnect` vuelve enseguida: `InProgress`, o `Connected` o `Refused` si el sistema ya lo sabe (pasa en loopback); `PollConnect` dice si terminó, una vez por frame, y guarda el resultado final (`SO_ERROR` se borra al leerlo). `EINTR` es "probá más tarde" |
| `127.0.0.1` | se rechaza (`0x4e8570`) | se acepta |
| leer | un `recv` de hasta `0x2000` por `FD_READ` (`0x4e88a0`) | `Receive` da lo que haya, 0 si no hay nada (con un buffer vacío, sin llamar a `recv`); `Closed` si el otro lado cerró |
| el otro lado cerró | `FD_CLOSE` | `Closed` con `recv` en 0, y con `ECONNRESET`, `ECONNABORTED`, `EPIPE` y sus `WSA*` en `recv` o en `send`; la `Connection` lo da como `PeerClosed` |
| mandar | `send` en línea, sin mirar lo pendiente (`0x4b1664`); lo que no sale va al buffer de `0x2000` o cierra | `Send` da cuánto tomó, 0 si no tomó nada; lo que falta va a la `SendQueue` (PR B). Con algo pendiente no se manda directo: lo nuevo va detrás |
| drenar | `FD_WRITE` con Q-C11 | `SendQueue::Flush` con un cursor, en cada `Poll` |
| tope de la cola | `0x2000` bytes (`< 0x2001`); si no entra, cierra | un buffer fijo de `MaxPendingSend = 0x2000`, que no crece; si no entra, `SendQueueError::Full`, y la `Connection` cierra con `SendQueueFull` (CL4.3) |
| `SIGPIPE` | no aplica (Windows) | `MSG_NOSIGNAL` en Linux |
| cerrar | `SO_LINGER {1, 0}` y `closesocket` (`0x4e8380`) | `close` o `closesocket` normal, sin el RST (CL4.R, paso 3) |
| por frame | la cola se despacha en cada vuelta del bucle de `WinMain`, con foco o sin él (`0x4e1d50`) | `ILoopClient::Poll()` una vez por `RunFrame`, antes de los pasos, también minimizada; en el modo fiel, antes de `BeginFrame` y fuera de la medición (PR C) |

**Archivos:**
- **PR A:** crear `Client/src/Network/SocketApi.h/.cpp` (el único `#ifdef _WIN32`, y la razón de
  §10 en el header), `SocketError.h`, `ISocket.h`, `IResolver.h`, `SystemResolver.h/.cpp`,
  `SystemSocket.h/.cpp`; `Client/tests/support/FakeSocket.h/.cpp`, `FakeResolver.h` y
  `LoopbackServer.h/.cpp`, con `Client/tests/support/test_fake_socket.cpp`, y
  `Client/tests/network/test_system_socket.cpp`. Modificar `Client/CMakeLists.txt` (`ws2_32` en
  Windows), PROGRESS y este plan. No hay `Endpoint`: ya hay dos structs de host y puerto
  (`App::ServerAddress` de la CL3.4 y `Protocol::Connection::ServerAddress` del `F4 03`), y
  Network no ve a App, así que `ISocket` y `Connection` reciben `host` y `port` sueltos.
- **PR B:** crear `Client/src/Network/SendQueue.h/.cpp` y
  `Client/tests/network/test_send_queue.cpp`. Modificar 07 (Q-C11 con su test), PROGRESS y este
  plan.
- **PR C:** modificar `Client/src/App/ILoopClient.h` (`Poll()`), `Client/src/App/GameLoop.cpp`
  (`RunFrame` lo llama), `Client/tests/support/RecordingClient.h/.cpp` (anota el orden de las
  llamadas y puede hacer tardar al poll), `Client/tests/app/test_game_loop.cpp`, `main.cpp` (un
  `Poll()` vacío), 07 (mientras se arrastra la ventana en Windows, el original sigue con
  `WM_TIMER` y MuRet no consulta la red: quirk nuevo), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Network::SocketApi
{
// SOCKET (UINT_PTR) en Windows, int en POSIX: los dos entran en un uintptr_t.
using Handle = std::uintptr_t;
inline constexpr Handle InvalidHandle = ~Handle{0};
// El errno o el WSAGetLastError de la llamada, ya traducido. TryLater junta EWOULDBLOCK, EAGAIN,
// WSAEWOULDBLOCK y EINTR; PeerClosed, recv en 0 y ECONNRESET, ECONNABORTED, EPIPE y sus WSA*.
enum class Status : std::uint8_t { Done, TryLater, InProgress, Refused, PeerClosed, Failed };
struct Result
{
    Status status = Status::Done;
    std::size_t bytes = 0;
    int systemError = 0; // para el log
};
class NetworkInit;  // RAII: WSAStartup(MAKEWORD(2, 2)) y WSACleanup; nada en POSIX
class UniqueSocket; // RAII sobre un Handle: se mueve, no se copia, y el destructor cierra
[[nodiscard]] UniqueSocket OpenTcp(); // socket(AF_INET, SOCK_STREAM, 0), ya no bloqueante
[[nodiscard]] Result Connect(Handle socket, std::array<std::uint8_t, 4> ip, std::uint16_t port);
// poll en POSIX (select no puede desde FD_SETSIZE), select con escritura y excepciones en Windows;
// tiempo 0, y SO_ERROR para el resultado.
[[nodiscard]] Result CheckConnect(Handle socket);
[[nodiscard]] Result Receive(Handle socket, std::span<std::byte> buffer);
// MSG_NOSIGNAL en Linux. En Winsock el largo es un int: se manda de a INT_MAX como mucho.
[[nodiscard]] Result Send(Handle socket, std::span<const std::byte> bytes);
} // namespace Client::Network::SocketApi

namespace Client::Network
{
enum class SocketError : std::uint8_t
{
    ResolveFailed, // el IResolver no dio ninguna IPv4
    Refused,       // el otro lado rechazó la conexión
    Closed,        // el otro lado cerró: recv en 0, o ECONNRESET, ECONNABORTED, EPIPE o sus WSA*
    Failed,        // otro error del sistema: LastSystemError() lo dice, para el log
};

// De un nombre o una IPv4 en texto a una IPv4. El del sistema espera (pregunta 5); el de los
// tests contesta lo que le digan, sin DNS.
class IResolver
{
public:
    virtual ~IResolver() = default;
    [[nodiscard]] virtual Core::Expected<std::array<std::uint8_t, 4>, SocketError> Resolve(std::string_view host) = 0;
};

enum class ConnectProgress : std::uint8_t
{
    InProgress,
    Connected,
};

// Un socket TCP de cliente que nunca espera, salvo al resolver el nombre (pregunta 5). Se puede
// volver a conectar después de Close().
class ISocket
{
public:
    virtual ~ISocket() = default;
    // Resuelve, crea el socket, lo pone no bloqueante y empieza a conectar. InProgress casi
    // siempre; Connected o Refused si el sistema ya lo sabe (en loopback pasa).
    [[nodiscard]] virtual Core::Expected<ConnectProgress, SocketError> StartConnect(std::string_view host,
                                                                                    std::uint16_t port) = 0;
    // Si el connect terminó (SocketApi::CheckConnect). SO_ERROR se borra al leerlo, así que el
    // resultado final se guarda: después de Connected o de un error, da lo mismo sin volver a
    // mirar el socket.
    [[nodiscard]] virtual Core::Expected<ConnectProgress, SocketError> PollConnect() = 0;
    // Lo que haya, hasta buffer.size(); 0 si no llegó nada. Con un buffer vacío da 0 sin llamar a
    // recv, que daría 0 y se leería como un cierre.
    [[nodiscard]] virtual Core::Expected<std::size_t, SocketError> Receive(std::span<std::byte> buffer) = 0;
    // Cuánto tomó el sistema, de 0 a bytes.size().
    [[nodiscard]] virtual Core::Expected<std::size_t, SocketError> Send(std::span<const std::byte> bytes) = 0;
    virtual void Close() = 0;
    [[nodiscard]] virtual bool IsOpen() const = 0;
    // El errno o el WSAGetLastError del último Failed, para el log.
    [[nodiscard]] virtual int LastSystemError() const = 0;
};

class SystemResolver final : public IResolver { /* inet_pton, y si no, getaddrinfo con AF_INET */ };
// El constructor recibe el IResolver: explicit SystemSocket(IResolver& resolver). Guarda el
// SocketApi::UniqueSocket, el resultado final del connect y el último error del sistema.
class SystemSocket final : public ISocket { /* … */ };

// El envío que no entró en el socket, con un cursor: lo que salió no se vuelve a mandar (Q-C11).
// El buffer de envío del original mide 0x2000 (+0x10, el largo en +0x2010, en 0x4e87b0). El de
// MuRet es un std::array fijo de ese tamaño: Push corre lo pendiente al principio antes de copiar,
// así que el cursor no lo hace crecer.
inline constexpr std::size_t MaxPendingSend = 0x2000;

enum class SendQueueError : std::uint8_t
{
    Full,   // lo pendiente más lo nuevo pasa de MaxPendingSend: el original cierra
    Socket, // el socket falló: SocketError en LastSocketError()
};

class SendQueue
{
public:
    // Agrega `bytes` detrás de lo pendiente, o Full sin agregar nada.
    [[nodiscard]] std::optional<SendQueueError> Push(std::span<const std::byte> bytes);
    // Manda lo que el socket tome, desde el cursor, hasta que no tome más.
    [[nodiscard]] std::optional<SendQueueError> Flush(ISocket& socket);
    [[nodiscard]] std::size_t Pending() const;
    [[nodiscard]] std::optional<SocketError> LastSocketError() const; // el del último Socket
    void Clear();
};
} // namespace Client::Network

namespace Client::App
{
// App/ILoopClient.h de la CL3.3, con un llamado más: Poll, una vez por frame, antes de los pasos.
// GameLoop::RunFrame(const WindowState& state, Input::InputState& input) no cambia de firma.
class ILoopClient
{
public:
    virtual ~ILoopClient() = default;
    virtual void Poll() = 0; // la red: corre también minimizada
    virtual void Step(const Input::InputState& input) = 0;
    virtual void Render(float alpha) = 0;
};
} // namespace Client::App
```

El falso y el servidor de prueba (`Client/tests/support/`):

```cpp
namespace Client::Tests
{
// Un ISocket que sigue un guion: cuántos PollConnect quedan en curso y cómo terminan, qué trae
// cada Receive (una lectura por entrada, como llegan del cable) y cuánto toma cada Send.
class FakeSocket final : public Network::ISocket
{
public:
    int pollsUntilConnected = 0;
    std::optional<Network::SocketError> connectResult; // vacío: Connected
    std::deque<std::vector<std::byte>> reads;           // vacío: Receive da 0
    std::deque<std::optional<std::size_t>> sendLimits;  // vacío: toma todo; nullopt: toma 0
    std::optional<Network::SocketError> sendError;      // el próximo Send falla con esto
    bool peerClosed = false;                            // Receive da Closed cuando no hay más reads
    std::vector<std::byte> sent;                        // todo lo que tomó Send, en orden
    std::vector<App::ServerAddress> connects;           // el host y el puerto de cada StartConnect
    int closes = 0;
    // … los override
};

// Un listener de verdad en 127.0.0.1, puerto elegido por el sistema, no bloqueante, que el test
// consulta en el mismo hilo que el cliente: sin hilos y sin esperas.
class LoopbackServer
{
public:
    LoopbackServer();
    [[nodiscard]] std::uint16_t Port() const;
    [[nodiscard]] bool Accept();                // true si hay un cliente nuevo
    [[nodiscard]] std::vector<std::byte> Read(); // lo que haya del cliente
    void Write(std::span<const std::byte> bytes);
    void CloseClient();
    void ResetClient(); // SO_LINGER {1, 0} y close: un RST, como el kick de 0x4e8380
};

// Un IResolver que da `address`, o ResolveFailed si está vacío.
class FakeResolver final : public Network::IResolver { /* std::optional<std::array<std::uint8_t, 4>> address; */ };
} // namespace Client::Tests
```

- [x] **Paso 1 (PR A, §10):** con Context7, confirmar la pregunta 3: `SDL3_net` (si su versión 3
  está estable y en vcpkg, que `NET_WriteToStreamSocket` encola adentro y que
  `NET_GetStreamSocketPendingWrites` dice cuánto) y asio standalone (el puerto `asio` y
  `io_context::poll`). Si la recomendación sigue, la razón va al header de
  `SocketApi.h`; si no, se vuelve a la pregunta antes de escribir. Se confirmó el 2026-11-02:
  en la `builtin-baseline` de `vcpkg.json` están `sdl3-net` 3.2.0 (estable; sus funciones son
  "desde SDL_net 3.0.0") y `asio` 1.38.2. `NET_WriteToStreamSocket` nunca espera y encola lo que
  no sale, sin tope, y `NET_GetStreamSocketPendingWrites` dice cuánto quedó (-1 si la conexión
  falló); `io_context::poll()` corre los handlers listos sin esperar. La recomendación sigue,
  por lo mismo: `sdl3-net` depende de `sdl3` y lo traería a `ClientLib`, y asio cambia el modelo
  a handlers. La razón quedó en el header de `SocketApi.h`.
- [x] **Paso 2 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de los tests, la tabla de lo
  que da cada llamada del sistema en Linux y en Windows: `connect` no bloqueante (`EINPROGRESS` y
  `WSAEWOULDBLOCK`), "probá más tarde" en `recv` y `send` (`EWOULDBLOCK`, `EAGAIN`,
  `WSAEWOULDBLOCK`, `EINTR`), un rechazo (`ECONNREFUSED`, `WSAECONNREFUSED`), el otro lado que
  cerró (`recv` 0, y `ECONNRESET`, `ECONNABORTED`, `EPIPE` y sus `WSA*`), y cómo se ve un
  `connect` que falló (en `poll`, `POLLOUT` o `POLLERR` en Linux; en `select`, en las excepciones
  en Windows). La columna de Linux se miró con un programa de prueba en `127.0.0.1` (salvo la
  fila del `connect` que termina enseguida, que POSIX permite y en loopback no se vio); la de
  Windows sale de la documentación de Winsock, sin probar a mano: la prueban los
  `SystemSocketTest` en el CI de Windows.

  | Qué | Linux | Windows | `SocketApi` |
  | --- | --- | --- | --- |
  | `connect` no bloqueante | -1 con `EINPROGRESS`, también en loopback y contra un puerto cerrado; con `EINTR` el connect sigue en el fondo | `SOCKET_ERROR` con `WSAEWOULDBLOCK` (`WSAEINPROGRESS` es de otra cosa: una llamada bloqueante en curso) | `InProgress` |
  | `connect` que ya terminó | 0 | 0 | `Done` |
  | un rechazo | el `connect` da `EINPROGRESS`, y `ECONNREFUSED` llega en `SO_ERROR` en el primer `poll` (POSIX deja que lo dé el mismo `connect`; en loopback no se vio) | `WSAECONNREFUSED` en `SO_ERROR`, unos 2 s después: reintenta el SYN | `Refused` |
  | cómo se ve que terminó | `poll` con `POLLOUT`: conectado da `POLLOUT`; un rechazo, `POLLOUT`, `POLLERR` y `POLLHUP`. `SO_ERROR` dice cuál y se borra al leerlo: el segundo da 0, y `poll` sigue con `POLLOUT` | `select`: conectado, en las de escritura; uno que falló, en las de excepciones y no en las de escritura. `SO_ERROR` dice cuál | `CheckConnect`, y el resultado final lo guarda `SystemSocket` |
  | probá más tarde, en `recv` y `send` | -1 con `EAGAIN` o `EWOULDBLOCK` (en Linux valen lo mismo), o `EINTR` | `SOCKET_ERROR` con `WSAEWOULDBLOCK` (`WSAEINTR` solo lo da una llamada bloqueante cancelada) | `TryLater` |
  | un `recv` de 0 bytes | -1 con `EAGAIN` si no llegó nada; 0 si hay bytes esperando o si el otro lado cerró | no se llama | `Done` con 0, sin llamar a `recv` |
  | el otro lado mandó su FIN | `recv` da 0 | `recv` da 0 | `PeerClosed` |
  | el otro lado mandó un RST (un kick, `0x4e8380`) | `recv` da `ECONNRESET`, y el siguiente 0 | `WSAECONNRESET` | `PeerClosed` |
  | `send` después del FIN | el primero sale y el otro lado contesta con un RST; el siguiente da `EPIPE` y, sin `MSG_NOSIGNAL`, la señal `SIGPIPE` | el primero sale; cuando llega el RST, `WSAECONNRESET` o `WSAECONNABORTED`. No hay señal | `PeerClosed` |
  | la conexión la cortó la pila local | `ECONNABORTED` | `WSAECONNABORTED` | `PeerClosed` |
  | otro error | el `errno` | el `WSAGetLastError` | `Failed`, con `systemError` |

- [x] **Paso 3 (PR A):** los tests:
  - `SystemSocketTest.ConnectsToALoopbackListener`: `StartConnect` a `127.0.0.1` y el puerto del
    `LoopbackServer`, con el `SystemResolver` (una IP no pasa por el DNS); consultando
    `PollConnect` y `Accept` en el mismo lazo, `Connected` y `Accept` en `true` antes de 100
    vueltas (sin mirar `Accept` pasaría igual: el sistema termina el connect antes del `accept`);
  - `SystemSocketTest.ReceiveWithNothingIsZero` (0 bytes, sin error),
    `AnEmptyReceiveIsNotAClose` (un buffer vacío da 0, no `Closed`),
    `SendsAndReceivesBytes` (`C1 04 F4 02` del cliente llega igual al servidor, y `C1 04 00 01`
    del servidor llega igual al cliente), `APeerThatClosesIsClosed` (`CloseClient`: `Receive` da
    `Closed`), `AResetIsClosed` (`ResetClient`: `Receive` da `Closed`, no `Failed`),
    `AClosedPortIsRefused` (`Refused` desde `PollConnect`, con un tope de 5 s reales: en Linux en
    el primero, en Windows unos 2 s después; desde `StartConnect` también vale, si el sistema ya
    lo sabe; y un `PollConnect` más después da `Refused` otra vez),
    `AnUnknownHostFailsToResolve` (con un `FakeResolver` vacío: `ResolveFailed` y `IsOpen` en
    `false`; el `SystemResolver` con `nombre.invalid` queda para la prueba a mano, porque depende
    del DNS), `SendingAfterThePeerClosedDoesNotKillTheProcess` (después de `CloseClient`, primero
    `Receive` hasta que dé `Closed`, y recién ahí diez `Send` de 1.024 bytes: alguno da `Closed`,
    ninguno `Failed`, y el test sigue vivo), `AMovedSocketIsClosed` y `CanConnectAgainAfterClose`;
  - `FakeSocketTest.FollowsTheScript`: dos `PollConnect` en curso y después conectado; dos
    `reads` salen en dos `Receive`; un límite de 2 toma 2.

  Hechos, con lo que cambió al escribirlos:
  - Todo lo que el sistema hace en el fondo (el connect, el `accept`, que lleguen los bytes, el
    FIN o el RST) se espera con `RetryUntil` de `LoopbackServer.h`: el mismo lazo, sin dormir,
    con el tope de 5 s reales de `AClosedPortIsRefused` (`LoopbackTimeout`). Las 100 vueltas de
    `ConnectsToALoopbackListener` pasan en Linux, donde el handshake en loopback termina en el
    mismo `connect`, pero en Windows lo procesa la pila en el fondo y 100 vueltas sin esperar
    pueden no alcanzar.
  - `AnEmptyReceiveIsNotAClose` deja bytes esperando antes del `Receive` vacío: en Linux un `recv`
    de 0 bytes sin nada que leer da `EAGAIN` (sería un "probá más tarde", y el test pasaría sin la
    guarda), y con bytes esperando da 0, que es lo que se leería como un cierre.
  - El puerto cerrado de `AClosedPortIsRefused` es el de un socket atado a `127.0.0.1` sin
    `listen` (`SocketApi::BindOnLoopback`), vivo todo el test, y no el de un listener que ya
    cerró: así ningún otro proceso ni otro test en paralelo puede tomar ese puerto entre que se
    mira y el `connect`. Sin listener, el SYN vuelve con un RST: es el caso de `WSAECONNREFUSED`
    en la documentación de Winsock (un servicio sin aplicación que escuche). En Linux se midió;
    en Windows lo prueba el CI.
  - `SendingAfterThePeerClosedDoesNotKillTheProcess` manda de a 1.024 bytes hasta el primer error,
    con el tope de 5 s, y no diez seguidos: en Windows el RST se procesa en el fondo y puede llegar
    después del décimo, y lo que da un `send` después del primer error no lo fija el contrato. El
    primero sale siempre (el test lo mira), así que el que da `EPIPE` es el que mandaría `SIGPIPE`.
  - Se sumaron `SystemSocketTest.AMoveAssignmentClosesTheOldOne` (el que recibe cierra el suyo, y
    el servidor lo ve: `LoopbackServer::ClientClosed`); `FakeSocketTest`, con seis más (cada
    connect y cada close anotados, un connect que falla y se repite, una lectura que no entra en
    el buffer, el cierre después de los `reads`, el límite `nullopt` y el `sendError` de una vez,
    y sin conexión `Closed`); `SystemResolverTest` (`test_system_resolver.cpp`: una IPv4 en el
    orden en que se escribe, un host vacío y uno con un byte 0, todos sin DNS); y `SocketApiTest`
    (`test_socket_api.cpp`: el tope de `INT_MAX` por llamada, `NetworkInit` anidado, el RAII de
    `UniqueSocket` y un buffer vacío que no llama al sistema).
  - Diez mutantes a mano (sin `MSG_NOSIGNAL`, sin la guarda del buffer vacío, sin el resultado
    guardado, sin `ECONNRESET` o `EPIPE` en `IsPeerClosed`, el resolver sin mirar el byte 0, `Send`
    sin mirar el estado, la asignación de `UniqueSocket` sin cerrar, y dos del `FakeSocket`): los
    mata algún test. Sin `MSG_NOSIGNAL`, la señal mata el ejecutable del test.
- [x] **Paso 4 (PR A):** `SocketApi`, `SystemResolver`, `SystemSocket`, `FakeSocket`,
  `FakeResolver` y `LoopbackServer`. Lo que se sumó o cambió respecto de la interfaz de arriba:
  - El `#ifdef _WIN32` está en `SocketApi.cpp` y no en el header: `SocketApi.h` no incluye ningún
    header del sistema, así `<winsock2.h>` y las macros de `<windows.h>` (`min` y `max`) no
    llegan a quien lo incluye. `SocketApi.cpp` lo usa en los includes y en un bloque del
    principio de su namespace anónimo; el resto del archivo es igual en los dos sistemas.
  - `SocketApi` suma `Opened` (el socket, o vacío con su `Result`), que es lo que dan `OpenTcp`,
    `ListenOnLoopback` y `Accept`; `ParseIpv4` (`inet_pton`) y `LookUpIpv4` (`getaddrinfo` con
    `AF_INET`), que usa el `SystemResolver`; `BytesPerCall` y `MaxBytesPerCall`, el tope de
    `INT_MAX` de Winsock, usado también en Linux para que los dos se porten igual; y, para los
    tests, `ListenOnLoopback`, `LocalPort`, `Accept` y `CloseWithReset` (los del `LoopbackServer`)
    y `BindOnLoopback` (el puerto cerrado de `AClosedPortIsRefused`): el servidor de prueba usa el
    mismo `#ifdef` y no uno propio. `NetworkInit` tiene `IsReady` y `SystemError` (lo que dio
    `WSAStartup`).
  - `Ipv4Address` (`std::array<std::uint8_t, 4>`, en el orden en que se escribe) va en
    `IResolver.h`. `IResolver` e `ISocket` tienen la copia y el movimiento protegidos, como
    `IObfuscation`, para que `SystemSocket` se pueda mover.
  - Sin conexión (antes de `StartConnect`, después de `Close`, o en uno movido) `PollConnect`,
    `Receive` y `Send` dan `Closed`, y el comentario de `SocketError::Closed` lo dice. Lo mismo
    `Receive` y `Send` mientras el connect sigue o después de que falló; `PollConnect` ahí repite
    el resultado final, `ResolveFailed` incluido. `IsOpen` es "hay un socket del sistema": un
    `ResolveFailed` no lo crea, y un `Refused` lo deja hasta `Close`. Un `Closed` de `Receive` no
    cierra el socket: así `Send` llega a la llamada del sistema, y `MSG_NOSIGNAL` se prueba de
    verdad.
  - `FakeSocket` suma `systemError` (lo que da `LastSystemError`); `pollsUntilConnected` es
    cuántos `PollConnect` dan `InProgress` (con 0, el `StartConnect` ya termina); `sendError` vale
    para un solo `Send`. Como el `SystemSocket`, guarda el resultado final del connect (un
    `PollConnect` después de un fallo lo repite aunque cambie `connectResult`), y un
    `ResolveFailed` termina en el `StartConnect` y deja `IsOpen` en `false` (en la revisión:
    `FakeSocketTest.AFailedConnectStaysFailed` y `AnUnresolvedHostIsNotOpen`). `FakeResolver`
    anota en `hosts` lo que le preguntaron.
  - `LoopbackServer` suma `ClientClosed` (un `Read` vio el FIN o el RST del cliente), y su header
    trae `RetryUntil` y `LoopbackTimeout`, que van a usar también los tests en loopback de la CL4.3
    y la CL4.5.
  - Quedan para cuando la red arranque en `Main` (CL4.3): que `main.cpp` cree el `NetworkInit`, y
    el log de lo que tardó la resolución si pasa de 100 ms (pregunta 5), que mide quien tiene el
    log; `SystemResolver` no lo tiene.
- [ ] **Paso 5 (PR B y PR C):** los tests:
  - `SendQueueTest.SendsEverythingWhenTheSocketTakesIt` (10 bytes, un `Flush`, 0 pendientes);
  - `SendQueueTest.PartialSendsKeepTheOrder` (Q-C11): `00..09` con `{2, 2, nullopt}`: el primer
    `Flush` deja 6 pendientes y `sent` es `00 01 02 03`; el segundo, sin límites, deja 0 y `sent`
    es `00..09` (el original daría `00 01 02 03 00 01 02 03 04 05`, CL4.0);
  - `SendQueueTest.FillsUpToTheOriginalBuffer`: `0x2000` bytes entran; uno más da `Full` y no se
    agrega;
  - `SendQueueTest.ReusesTheBufferAfterAPartialSend`: `0x2000` bytes, un `Flush` que toma
    `0x1000`, y otros `0x1000` entran (el cursor no ocupa lugar);
  - `SendQueueTest.NewBytesWaitBehindThePending`: con `06..09` pendientes, `Push` de `0A 0B` y un
    `Flush` sin límites: `sent` termina en `06 07 08 09 0A 0B`;
  - `SendQueueTest.ASocketErrorIsReported` (`sendError = Closed`: `Socket`, `LastSocketError()`
    da `Closed`, y lo pendiente queda);
  - `SendQueueTest.AnErrorMidFlushKeepsWhatDidNotLeave` (`{2, Closed}`: `Socket`, 8 pendientes, y
    un segundo `Flush` manda `02..09` una vez: lo que ya salió no vuelve);

    Los de `SendQueue`, hechos en el PR B, con tres más: `MoreThanTheBufferInOnePushIsFull`
    (`0x2001` bytes de una vez, con la cola vacía), `FlushingNothingDoesNotCallTheSocket` y
    `ClearDropsThePending`. En la revisión se sumaron `AnErrorMidFlushKeepsWhatDidNotLeave` (para
    eso `sendLimits` acepta un `SocketError` en el guion, que da ese error en ese `Send`:
    `FakeSocketTest.AScriptedSendErrorComesInItsTurn`), `ASocketThatClaimsMoreDoesNotRunOff` (un
    `ISocket` del test que dice haber tomado de más: el cursor no pasa del final) y
    `NoSocketErrorBeforeTheFirst`. Los bytes de los de `0x2000` no se repiten cada 256, así un
    byte corrido o repetido se ve. Ocho mutantes a mano (el cursor que corre de más, el tope medido
    desde el final y no desde lo pendiente, el tope con `>=`, un error que tira lo pendiente o que
    pierde su `SocketError`, un `Flush` sin nada que llama al socket, y la compactación sin
    descontar el cursor o sin hacerse): los mata algún test. Los de la revisión matan otros siete:
    el cursor que vuelve atrás con un error, el `Flush` sin el tope, `LastSocketError` con
    `Closed` de entrada, el error del guion ignorado, el `FakeSocket` sin el resultado guardado o
    que abre con un `ResolveFailed`, y el `SystemSocket` que vuelve a mirar `SO_ERROR` después
    del rechazo.
  - `GameLoopTest.PollsOncePerFrameBeforeTheSteps`, en `Decoupled` y en `Faithful`: el segundo
    frame, 80 ms después del primero (que solo fija el inicio), anota `Poll`, `Step`, `Step`,
    `Render`;
  - `GameLoopTest.PollsWhileMinimized`: minimizada, 26 `RunFrame` dan 26 `Poll`, 25 pasos y 0
    dibujos;
  - `GameLoopTest.FaithfulDoesNotMeasureThePoll`: en el modo fiel, con polls de 20 ms y llamando a
    `RunFrame` mientras `Now()` sea menor que 3 s, 48 frames y 56 pasos (midiendo el poll serían
    64 y 74, como sin poll); con polls de 10 ms, que no cruzan un tick, 64 y 74.

  *PR C, hecho así:* los tres de `GameLoopTest`, en rojo antes de llamar a `Poll` desde `RunFrame`.
  En el modo fiel el primer frame no solo fija el inicio (corre el paso del valor inicial), así que
  en `PollsOncePerFrameBeforeTheSteps` el primer dibujo tarda 100 ms: mide 6 ticks (93 ms) y el
  segundo frame corre dos pasos, como el de Decoupled 80 ms después. `PollsWhileMinimized` mira
  también el modo fiel (26 polls y ningún dibujo). Con el `Poll` dentro de la medición falla
  `FaithfulDoesNotMeasureThePoll`, y con el `Poll` después de los pasos, solo el del orden: los dos
  se probaron así, cambiando `GameLoop.cpp` a mano. Uno más, `TestSceneTest.PollDoesNothing`.
- [ ] **Paso 6 (PR B y PR C):** `SendQueue`, y 07, Q-C11 con
  `SendQueueTest.PartialSendsKeepTheOrder` y el jefe de la CL4.0 (PR B); el `Poll()` en
  `ILoopClient`, `GameLoop` y `RecordingClient`, y 07 con lo de arrastrar la ventana (PR C).

  El PR B está: `SendQueue` como la interfaz de arriba, y en 07 la columna "MuRet" de Q-C11, con
  los bytes de los dos drenajes y sus tests (lo que hace el original, en "Qué pasa", lo pasa al 07
  la CL4.R, paso 4). Lo que se decidió al escribirla:
  - `Push` compacta solo cuando lo nuevo no entra detrás de lo que hay, y un `Flush` que deja la
    cola vacía vuelve el cursor a 0: en el caso de siempre (todo sale en el mismo `Flush`) no se
    corre nada.
  - `Push` no manda: quien la usa hace `Push` y `Flush`, así lo nuevo nunca se adelanta a lo
    pendiente. `NewBytesWaitBehindThePending` lo prueba en la cola; que MuRet nunca mande directo
    con algo pendiente lo va a probar `ConnectionTest.NewPacketsWaitBehindThePending` (CL4.3).
  - Un socket que dice haber tomado más de lo que se le dio no corre el cursor afuera del buffer:
    se toma como que tomó todo.
  - `LastSocketError()` es un `std::optional<SocketError>`, vacío antes del primer error (en la
    revisión: antes daba `Closed`, que no se distinguía de uno real), y `SendQueueError` tiene su
    `PrintTo` en `support/NetworkPrinters.h`.
  - Con un error en la mitad de un `Flush`, lo que ya salió no vuelve y lo demás queda: el cursor
    se corre con cada `Send` que tomó algo, antes del que falla.

  *PR C, hecho así:* `RunFrame` llama a `Poll` lo primero, en los dos modos: en Decoupled, antes
  del `Now()` del frame, así lo que tarda entra en el frame que lo pagó; en Faithful, antes de
  `BeginFrame`. El cliente de `Main` es la `TestScene`, así que el `Poll()` vacío va ahí y
  `main.cpp` no cambia. `RecordingClient` anota cada llamada en `calls` (un `LoopCall`, con su
  `PrintTo`) y hace tardar al poll con `pollCost`. Lo de arrastrar la ventana es Q-C49.
- [ ] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "Socket|SendQueue|GameLoop"
  ```

## Sub-ola CL4.2: `Dispatcher` y el opcode desconocido

**Concepto:** una tabla de despacho en vez de un `switch` de 10 KB: opcode → handler, con un
segundo nivel por subcódigo, y lo que no tiene handler al log una vez. Puro, sin red: se prueba con
paquetes armados en el test.

**Contrato** (04 § El dispatcher `0x4e2b10`):

| Qué | Original | MuRet |
| --- | --- | --- |
| forma | un `switch` de tres niveles en una función: el opcode (`0x4E5D90[245]` → `0x4E5B58`), `pkt[3]` para `0xF1` y `pkt[4]` debajo | `Protocol::Dispatcher`: un handler por opcode, o por opcode y subcódigo; el tercer nivel lo lee el handler (como el resultado del login, `ReadLoginResult`) |
| el opcode | `pkt[2]` en un C1, `pkt[3]` en un C2 | `Packet::Opcode()` (CL2.1) |
| el subcódigo | `F3` y `F4` lo toman de `pkt[3]` en un C1 y de `pkt[4]` en un C2 | el byte que sigue al opcode, en cualquier opcode registrado con subcódigos |
| sin handler | 104 opcodes caen al default `0x4E5B4A` sin avisar | `Unknown`, y una línea de aviso la primera vez de cada opcode (o de cada opcode y subcódigo), sin los bytes |
| sin subcódigo | lee `pkt[3]` o `pkt[4]` haya llegado o no (Q-C42) | `Malformed` si el paquete termina en el opcode |
| el cuarto argumento | lo pasa FoxAnti; algunos handlers solo responden si vale 0 (*inferido*) | no está: Protocol no sabe de la ofuscación. Si un handler de la CL9 lo necesita, se mira ahí |
| un solo dispatcher | el opcode `00` es el saludo con la escena en 2 y el chat en el juego (`0x4e33e4`) | un `Dispatcher` por servidor (CL4.3): el del CS y el del GS registran cada uno lo suyo |

**Archivos:** crear `Client/src/Protocol/Dispatcher.h/.cpp` y
`Client/tests/protocol/test_dispatcher.cpp`. Modificar PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Protocol
{
// Lo que recibe un handler: el C1/C2 entero, con su cabecera, ya descifrado y traducido.
using Handler = std::function<void(const Network::Packet& packet)>;

enum class DispatchResult : std::uint8_t
{
    Handled,
    Unknown,   // sin handler: el aviso va al log la primera vez
    Malformed, // un C3/C4, o un opcode con subcódigos en un paquete que termina en el opcode
};

class Dispatcher
{
public:
    explicit Dispatcher(Core::Log& log);
    // Un handler para todo el opcode. false si ya había uno (para el opcode o sus subcódigos): no
    // se pisa, porque registrar dos veces es un error de quien arma la tabla.
    [[nodiscard]] bool On(std::uint8_t opcode, Handler handler);
    // Un handler para el opcode y el byte que lo sigue.
    [[nodiscard]] bool On(std::uint8_t opcode, std::uint8_t subcode, Handler handler);
    DispatchResult Dispatch(const Network::Packet& packet);
};
} // namespace Client::Protocol
```

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de los tests, la tabla: para
  `C1 04 F4 02`, `C2 00 0B F4 06 00 01 90 01 00 00`, `C1 05 F1 01 01`, `C1 0C F1 00 …`,
  `C1 04 0E 00`, `C1 03 F1` y un C3, qué opcode y qué subcódigo lee el original y qué lee el
  `Dispatcher`.

  | Paquete | El original (`0x4e2b10`) | El `Dispatcher` |
  | --- | --- | --- |
  | `C1 04 F4 02` | el opcode en `pkt[2]`, `F4`; `F4` toma el subcódigo de `pkt[3]` en un C1: `02` | `F4 02`, el byte que sigue al opcode |
  | `C2 00 0B F4 06 00 01 90 01 00 00` | el opcode en `pkt[3]`, `F4`; en un C2, el subcódigo de `pkt[4]`: `06` | `F4 06`: en un C2 el byte que sigue al opcode también es `pkt[4]` |
  | `C1 05 F1 01 01` | `F1`; el nivel 2, `pkt[3]`: `01`; el nivel 3, `pkt[4]`: `01` | `F1 01`; el `01` de `pkt[4]` (el resultado) lo lee el handler, `ReadLoginResult` |
  | `C1 0C F1 00 …` | `F1` y `pkt[3]`: `00`, el saludo | `F1 00` |
  | `C1 04 0E 00` | `0E`, sin nivel 2: `0E` no tiene subcódigos | `0E`, con un handler para todo el opcode: el `00` no se mira |
  | `C1 03 F1` | `F1`, y lee `pkt[3]` aunque no llegó: el byte que haya quedado en el buffer (Q-C42) | `Malformed`, sin llamar a ningún handler |
  | un C3 | no llega cifrado: `0x4e1d50` lo descifra y lo rearma como C1 (Q-C10), y el dispatcher ve el opcode de adentro | `Malformed`: el opcode de un C3 viaja cifrado (`Packet::Opcode()` da `std::nullopt`); la `Connection` lo descifra antes (CL4.3) |

  La única diferencia en el contrato es el nivel 2 de `F1` en un C2: el original lo lee siempre en
  `pkt[3]`, y el `Dispatcher` en `pkt[4]`. Los `F1` de la conexión llegan como C1 (04, "El flujo
  de conexión"), así que no cambia nada; si un handler de otra ola recibe un `F1` en C2, se mira
  ahí.
- [x] **Paso 2:** los tests:
  - `DispatcherTest.CallsTheHandlerOfTheOpcode` (`C1 04 0E 00` con un handler para `0E`: lo
    llama una vez con el paquete entero);
  - `DispatcherTest.RoutesBySubcode` (`C1 05 F1 01 01` va al de `F1 01` y no al de `F1 00`);
  - `DispatcherTest.TheSubcodeOfAC2IsAfterItsOpcode` (`C2 00 0B F4 06 …` va al de `F4 06`);
  - `DispatcherTest.AnUnknownOpcodeIsLoggedOnce` (dos `C1 04 99 00`: los dos `Unknown` y una sola
    línea de aviso, que nombra el `99` y no trae los bytes);
  - `DispatcherTest.AnUnknownSubcodeIsLoggedOnce` (`C1 05 F1 7F 00` dos veces: una línea);
  - `DispatcherTest.AnOpcodeWithoutItsSubcodeIsMalformed` (`C1 03 F1`, con `F1` registrado con
    subcódigos);
  - `DispatcherTest.AnEncryptedPacketIsMalformed` (un C3);
  - `DispatcherTest.RegisteringTwiceKeepsTheFirst` (el segundo `On` da `false` y despacha el
    primero; también un `On(F1)` después de un `On(F1, 01)`).

  Hecho así, en rojo contra un `Dispatcher` que no registraba nada, con dos tests más:
  `AnOpcodeWithoutSubcodesNeedsNoMoreBytes` (`C1 03 0E` con un handler para todo `0E` es
  `Handled`: solo un opcode con subcódigos necesita el byte que sigue) y
  `AnEmptyHandlerIsNotRegistered` (`On` con un `Handler` vacío da `false` y no deja nada a medias;
  si no, `Dispatch` lo llamaría y `std::function` tiraría `bad_function_call`). Los de los avisos
  también miran que un opcode, o un opcode y subcódigo, distinto tenga su propia línea. Después,
  por la revisión del PR, `AC2WithoutItsSubcodeIsMalformed` (`C2 00 04 F4`, con `F4` registrado
  con subcódigos: en un C2 el subcódigo es `pkt[4]`, que no llegó).
- [x] **Paso 3:** `Dispatcher`.

  Hecho así: un `std::map` de opcode a lo registrado (un handler para todo el opcode, o un
  `std::map` de subcódigo a handler, nunca los dos), y dos `std::bitset<256>` con lo ya avisado,
  uno del `Dispatcher` para los opcodes y uno por opcode para sus subcódigos. Buscar no reserva
  memoria, así que despachar un paquete tampoco (regla 12). Un `Malformed` no va al log: lo
  decide quien llama, la `ServerSession` de la CL4.3, que tiene los dos `Dispatcher`. El aviso
  dice `un paquete con el opcode 99 no tiene handler: se descarta, y este aviso no se repite`.
- [x] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "Dispatcher"
  ```

## Sub-ola CL4.3: ConnectServer: saludo, lista e IP del GS

**Concepto:** los estados de una conexión y de un protocolo con turnos: conectar, mandar, esperar
la respuesta que corresponde, y pasar a otro servidor. Va en dos PRs: **PR A**, la `Connection`
(el camino de un paquete en claro: el socket, el corte, `AfterCut`, `BeforeDispatch`,
`BeforeSend` y la cola); **PR B**, la `ServerSession` hasta la dirección del GS, y la config.

**Contrato** (04 § El flujo de conexión, los bytes del cable; 03 § La capa de FoxAnti):

| Paso | Original | MuRet |
| --- | --- | --- |
| conectar | `0x4c1340` con el host y el puerto del CS (el compilado, el del launcher o el de FoxAnti) | `Connection::Open` con el `App::ServerAddress` de `[Server] Host` y `Port`, o de `/u /p` (CL3.4), campo por campo; el `ConnectTimeout` de la pregunta 5 |
| el `A9` | `C1 06 A9 00 00 01` con XOR32 antes del saludo (`C1 06 A9 20 9C 2F` en el cable): el pedido de la información de una mascota (Q-C16, CL4.R) | igual, con el evento `Opened`: `WritePetInfoRequest(LoginScenePetInfoRequest)`, que sale antes que el `F4 02` aunque el saludo llegue en el mismo `Poll` |
| el saludo | `C1 04 00 01`; con la escena en 2, loguea `Send Request Server List` (`0x4e33e4`) | el handler del `00` en el dispatcher del CS |
| pedir la lista | `C1 04 F4 02` en claro (`0x4c6070`); en 8 de 64 conexiones sale dos veces | `WriteServerListRequest()` (CL2.5), una vez por saludo |
| la lista | `C2 00 0A F4 02 01 00 00 00 CC` en el cable; FoxAnti la pasa a `C2 00 0B F4 06 00 01 90 01 00 00` | `AfterCut` de la `Connection`, y `ReadServerList` da `{400, carga 0}` |
| elegir | `C1 06 F4 03 90 01`; FoxAnti resta 400 y sale `C1 06 F4 03 00 00` | el primero de la lista (pregunta 9): `WriteServerSelect(400)` y `BeforeSend` |
| la dirección | `C1 16 F4 03 <IP:16> <puerto:2>`: `"172.16.198.1"` y `0xDA5D` = 55901; loguea, cierra el CS (`0x4e8380`) y conecta al GS (`0x4c1340`) | `ReadServerAddress`; la `ServerSession` lo pasa a `App::ServerAddress`, cierra, tira lo que queda del lote y pasa a `ConnectingGameServer` (CL4.4) |
| un paquete que no se traduce | FoxAnti no valida los largos (Q-C09) | `InvalidPacket`: se descarta con un aviso y se sigue leyendo |
| el corte | Q-C03, Q-C38 a Q-C40 | un error del `PacketReader` cierra con `BadStream` |

**Archivos:**
- **PR A:** crear `Client/src/Network/Connection.h/.cpp`, `CloseReason.h` y `Delivery.h` (el
  `Delivery` de `Protocol/Outgoing.h` baja a Network, porque la `Connection` lo necesita y Network
  no ve a Protocol; `Outgoing` lo usa desde ahí); `Client/tests/network/test_connection.cpp`.
  Modificar `Client/src/Protocol/Outgoing.h/.cpp` y quien use `Protocol::Delivery`, PROGRESS y
  este plan.
- **PR B:** crear `Client/src/App/ServerSession.h/.cpp`, `Client/src/Protocol/Pet/PetInfo.h/.cpp`
  (`WritePetInfoRequest`: la CL4.R dijo qué es el `A9`, y el plan lo llamaba `A9.h` y `WriteA9`
  hasta saberlo), `Client/tests/app/test_server_session.cpp`,
  `Client/tests/protocol/test_pet_info.cpp`, `Client/src/App/Credentials.h` (el struct, sacado de
  la sesión para que la CL4.4 le sume la lectura), `Client/src/App/ObfuscationMode.h`, y
  `Client/tests/support/AppPrinters.h` (el `PrintTo` de `Credentials`, que no imprime la clave)
  con su test.
  Modificar `Protocol/Connection/Opcodes.h` (el `00` del saludo del CS; el `A9` va en
  `PetInfo.h`), `Protocol/Outgoing.h/.cpp` (`Xor32C1`), `App/Config.h/.cpp` y su test
  (`[Server] Obfuscation`, pregunta 8), `Client/README.md`, PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Network
{
// Cómo sale un paquete: venía en Protocol/Outgoing.h (CL2.5).
enum class Delivery : std::uint8_t { Clear, Encrypted };

// Por qué se cerró una conexión, para el log y para la sesión.
enum class CloseReason : std::uint8_t
{
    ByClient,        // Close()
    ResolveFailed,
    Refused,
    ConnectTimedOut, // ConnectTimeout sin terminar de conectar
    PeerClosed,      // el otro lado cerró
    SocketFailed,    // otro error del socket
    BadStream,       // el PacketReader perdió el corte
    SendQueueFull,   // lo pendiente pasó de MaxPendingSend
};

// Pregunta 5: lo que se espera a que termine un connect. El original no tiene uno propio.
inline constexpr Core::Duration ConnectTimeout = std::chrono::seconds{10};
// Lo que se lee por vuelta de Poll: lecturas de 0x2000 (el buffer de recv del original, 0x4e88af)
// hasta que no haya más o hasta 8, para acotar el trabajo de un frame. Decisión de MuRet.
inline constexpr std::size_t ReceiveChunkSize = 0x2000;
inline constexpr int MaxReadsPerPoll = 8;

enum class LinkState : std::uint8_t { Closed, Connecting, Open };

// Las claves de los C3/C4 con el GameServer: Enc1.dat para lo que sale, Dec2.dat para lo que llega.
struct ConnectionKeys
{
    Crypto::EncryptionKey encryption;
    Crypto::DecryptionKey decryption;
};

// Lo que pasa en un Poll, en el orden en que pasó.
struct Opened // el connect terminó: va antes de lo que llegó en el mismo Poll
{
};
// Un C3/C4 que llegó y no se despacha (CL4.4): quien usa la conexión decide si manda el F1 03.
struct Undecryptable
{
    Crypto::CryptoError error;
};
struct Closed // siempre lo último del lote; el motivo también queda en LastCloseReason()
{
    CloseReason reason;
};
using Event = std::variant<Opened, Packet, Undecryptable, Closed>;

// El camino de un paquete por una conexión, en el orden del original (03, "Cadena de recepción" e
// IObfuscation.h). El socket, la ofuscación, el reloj y el log son de quien la arma y viven más.
class Connection
{
public:
    // `paddingSeed` siembra el std::mt19937 del relleno de los C3/C4 (CL4.4): main.cpp le da uno
    // de std::random_device, y los tests uno fijo.
    Connection(ISocket& socket, const IObfuscation& obfuscation, Core::IClock& clock, Core::Log& log,
               std::uint32_t paddingSeed);
    // Cierra lo que haya, vacía el corte, la cola y los seriales, y empieza a conectar. Con claves,
    // descifra los C3/C4 y cifra lo que va Encrypted (CL4.4); sin ellas, un C3/C4 es Undecryptable.
    // Network no ve a App: el host y el puerto llegan sueltos.
    void Open(std::string_view host, std::uint16_t port, std::optional<ConnectionKeys> keys);
    // Una vez por frame: primero PollConnect y después el ConnectTimeout (un frame largo no corta
    // un connect que ya terminó), manda lo encolado, lee, corta, traduce y descifra. Lo que pasó,
    // en orden. Quien recorre el lote y llama a Open o Close a mitad tira lo que queda.
    [[nodiscard]] std::vector<Event> Poll();
    // XOR32 ya aplicado si lo lleva (Protocol). Cifra si es Encrypted, pasa por BeforeSend,
    // encola y hace Flush: con algo pendiente, lo nuevo va detrás, nunca directo. Si
    // EncryptPacket falla, no manda nada y deja un aviso con el opcode y el CryptoError; la
    // conexión sigue. Antes de Open, o cerrada, se descarta con un aviso.
    void Send(Packet packet, Delivery delivery);
    void Close();
    [[nodiscard]] LinkState State() const;
    [[nodiscard]] std::optional<CloseReason> LastCloseReason() const;
};
} // namespace Client::Network

namespace Client::App
{
// En App/Credentials.h. Sin operator<<: no se imprimen nunca (pregunta 7). El PrintTo de los
// tests, en tests/support/AppPrinters.h, escribe el largo de cada campo y no la clave.
struct Credentials
{
    std::string account;
    std::string password;
};

enum class SessionState : std::uint8_t
{
    Idle,
    ConnectingConnectServer,
    WaitingConnectHello,  // el A9 salió; se espera C1 04 00 01
    WaitingServerList,    // F4 02 salió
    WaitingServerAddress, // F4 03 salió
    ConnectingGameServer, // CL4.4
    WaitingGameHello,     // CL4.4
    WaitingLoginResult,   // CL4.4
    LoggedIn,             // CL4.5: el keepalive corre
    Failed,
};

enum class SessionFailure : std::uint8_t
{
    ConnectServerLost,  // con el CloseReason de la conexión
    EmptyServerList,
    GameServerLost,     // CL4.4
    HelloRejected,      // CL4.4
    VersionMismatch,    // CL4.4
    LoginRejected,      // CL4.4, con el resultado
};

struct SessionSettings
{
    ServerAddress connectServer; // App/ServerAddress.h, de la CL3.4
    Credentials credentials;
};

class ServerSession
{
public:
    ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log,
                  Network::ConnectionKeys keys, SessionSettings settings);
    void Start(); // conecta al CS
    // Una vez por frame, desde ILoopClient::Poll: recorre el lote de Connection::Poll en orden, y
    // si un handler llamó a Open (el F4 03) o hubo un Closed, deja de recorrerlo.
    void Poll();
    [[nodiscard]] SessionState State() const;
    [[nodiscard]] std::optional<SessionFailure> Failure() const;
    // La dirección del F4 03, pasada de Protocol::Connection::ServerAddress a la de App.
    [[nodiscard]] std::optional<ServerAddress> GameServer() const;
};
} // namespace Client::App
```

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de los tests, el diagrama de
  estados de la `ServerSession` del CS: qué paquete espera cada estado, qué manda al recibirlo, a
  qué estado pasa, y qué hace con un paquete que no espera (se ignora con un aviso) y con un
  cierre (`Failed`).

  | Estado | Espera | Al recibirlo manda | Pasa a |
  | --- | --- | --- | --- |
  | `Idle` | `Start()` | abre el CS (`Connection::Open`) | `ConnectingConnectServer` |
  | `ConnectingConnectServer` | el `Opened` | el `A9`, `C1 06 A9 00 00 01` con XOR32 y en claro | `WaitingConnectHello` |
  | `WaitingConnectHello` | `00`, el saludo | `C1 04 F4 02` | `WaitingServerList` |
  | `WaitingServerList` | `F4 06`, la lista | `F4 03` con el código del primero | `WaitingServerAddress` |
  | `WaitingServerList` | `F4 06` sin servidores | nada: cierra el CS | `Failed` (`EmptyServerList`) |
  | `WaitingServerAddress` | `F4 03`, la dirección | nada: cierra el CS y abre el GS, y deja el resto del lote | `ConnectingGameServer` |
  | `ConnectingGameServer` | el `Opened` del GS | nada: el GS saluda primero (CL4.4) | sigue en `ConnectingGameServer` |

  Un paquete del CS que su estado no espera (un segundo saludo, una lista o una dirección fuera de
  turno) se ignora con un aviso que dice cuál es y no manda nada; uno que no se lee (una lista
  que dice más servidores de los que trae, una dirección sin puerto), también, y el estado espera
  uno que sirva. Uno que el `Dispatcher` da como `Malformed` se descarta con un aviso de la
  sesión, y uno sin handler, con el del `Dispatcher`. Un `Closed` con el CS, en cualquiera de sus
  estados, es `Failed` con `ConnectServerLost`, y con el GS, `GameServerLost`, con el
  `CloseReasonText` en el log. `Start()` vuelve a empezar desde cualquier estado.
- [x] **Paso 2 (PR A):** los tests de `Connection`, con `FakeSocket`, `FakeClock` y el
  `RecordingSink` de `Client/tests/support/` (la CL3.4 lo deja ahí), detrás de un `ILogSink`:
  - `ConnectionTest.OpensWhenTheSocketConnects` (`pollsUntilConnected = 2`: `Connecting`,
    `Connecting`, `Open`, y el tercer `Poll` da `Opened`);
  - `ConnectionTest.OpenedComesBeforeWhatArrivesWithIt` (el connect termina y `C1 04 00 01`
    llega en el mismo `Poll`: `Opened` y después el paquete);
  - `ConnectionTest.AConnectThatTakesTooLongTimesOut` (en curso para siempre: a los 9,999 s sigue
    `Connecting`, a los 10 s `Closed` con `ConnectTimedOut`);
  - `ConnectionTest.ALongFrameDoesNotTimeOutAConnectThatFinished` (el socket conecta en el
    `Poll` de los 60 s: `Opened`, no `ConnectTimedOut`);
  - `ConnectionTest.WhatIsSentWhileConnectingLeavesWhenItOpens` (`C1 04 F4 02` mandado en
    `Connecting`: `sent` vacío hasta el `Opened`, y `C1 04 F4 02` en ese `Poll`);
  - `ConnectionTest.NewPacketsWaitBehindThePending` (el socket toma 2 bytes de un `C1 04 F4 02`
    y después todo; un `C1 04 00 01` mandado en el medio sale detrás: `sent` es
    `C1 04 F4 02 C1 04 00 01`; el original, en `0x4b1664`, lo adelantaría);
  - `ConnectionTest.CutsWhatArrivesInPieces` (`C1 04` en una lectura y `00 01` en la siguiente:
    un solo `Packet`, `C1 04 00 01`);
  - `ConnectionTest.WithFoxAntiTheListArrivesAsF406` (el cable `C2 00 0A F4 02 01 00 00 00 CC` da
    `C2 00 0B F4 06 00 01 90 01 00 00`);
  - `ConnectionTest.WithFoxAntiTheChoiceLeavesAsF40300` (`WriteServerSelect(400)` sale como
    `C1 06 F4 03 00 00`);
  - `ConnectionTest.AListThatDoesNotTranslateIsDropped` (una lista con más entradas de las que
    trae, seguida de `C1 04 00 01`: sale solo el saludo, con un aviso, y sigue `Open`);
  - `ConnectionTest.ABadHeaderCloses` (`FF 00`: `Closed` con `BadStream`);
  - `ConnectionTest.APeerThatClosesCloses` (`peerClosed` sin `reads`: el `Poll` da
    `Closed{PeerClosed}`, el estado es `Closed` y `LastCloseReason()` es `PeerClosed`; lo mismo
    con `sendError = Closed` en un `Send`);
  - `ConnectionTest.ReopeningStartsClean` (medio paquete del CS, `Open` a otro host y puerto: lo
    que llega después se corta desde cero, y `FakeSocket::closes` es 1);
  - `ConnectionTest.AFullSendQueueCloses` (el socket no toma nada y se mandan paquetes que suman
    `0x2001` bytes: `Closed` con `SendQueueFull`).

  Hechos en el PR A, con lo que cambió al escribirlos:
  - `NewPacketsWaitBehindThePending` (el plan lo llamaba `ASendWaitsBehindThePending`) es más
    fuerte: con el socket tomando de a 2, lo que sale después de mandar un paquete nuevo es el
    resto del pendiente, no el principio del nuevo. La parte de `APeerThatClosesCloses` con
    `sendError` es `ASendToAPeerThatClosedCloses`, y la de `peerClosed` trae un saludo antes del
    FIN: sale el saludo y después el `Closed`.
  - Se sumaron, en `ConnectionTest`: `AConnectThatFinishesAtOnceOpensInTheFirstPoll`,
    `AConnectThatFailsCloses` (`ResolveFailed`, `Refused` y `Failed`, en el `StartConnect` o en
    un `PollConnect`), `TheConnectTimeoutStartsAfterTheResolve`, `ASlowResolveIsLogged`,
    `NothingIsReadOrSentBeforeTheConnect` (mientras conecta, la conexión no llama a `Receive`,
    `Send` ni `Flush`: un `Closed` ahí es siempre el otro lado),
    `WhatDidNotLeaveLeavesInTheNextPoll`, `ReadsAtMostEightTimesPerPoll`,
    `ReadsInChunksOfTheOriginalBuffer` (nueve paquetes de `0x2000` en una entrada del guion: ocho
    en un `Poll`), `WithFoxAntiTheSkillListGetsItsByteBeforeTheDispatch` (el `BeforeDispatch`),
    `WithFoxAntiWhatArrivesFromTheGameServerIsUnmasked` (el de la lista de la CL4.4: la máscara es
    del corte, que ya está acá; también después de volver a abrir),
    `AnEncryptedPacketIsNotDelivered`, `AFlushThatFailsInThePollClosesWithoutReading` (con el
    error del guion a mitad del `Flush`), `ASocketFailureLogsTheSystemError`,
    `ReopeningForgetsACloseThatWasNotReported`, `CloseIsNotAnEvent`,
    `AClosedConnectionDoesNotUseTheSocket` y `AnEncryptedSendDoesNotLeave`; además
    `CloseReasonTest.EveryReasonHasItsOwnText` y `FakeSocketTest.RunsItsHookInsideStartConnect`.
  - Treinta y dos mutantes a mano de `Connection.cpp` (el tope antes del `PollConnect`, el tope
    con `>`, el tope contado desde antes del resolver, el aviso lento con `>=`, una lectura de más
    o una sola, el `Flush` mientras conecta o sin `Flush` en el `Poll`, un cierre que no sale como
    evento o un `Close` que sí, el corte o la cola sin vaciar, el corte sin la ofuscación, sin
    `AfterCut`, `BeforeDispatch` o `BeforeSend`, un `BadStream` que no cierra o un `InvalidPacket`
    que sí, un C3 que pasa, un `Encrypted` que sale, leer después de un `Flush` que falló, entre
    otros): los mata algún test.
  - De la revisión del PR: `WithFoxAntiASkillListThatDoesNotTranslateIsDropped` (un `C1 04 F3 11`
    sin `pkt[4]`, Q-C09: `BeforeDispatch` no lo traduce, se descarta con un aviso y la conexión
    sigue) y `ASocketThatClaimsMoreThanTheBufferDoesNotReadPastIt` (el `std::min` de la lectura);
    los avisos de descarte y de cierre se comparan enteros, con su nivel, así un aviso que sumara
    los bytes del paquete (el login va a pasar por el de `Encrypted` hasta la CL4.4) falla, y
    `CloseIsNotAnEvent` mira que el `ByClient` va en `info`. Ocho mutantes más (`BeforeDispatch`
    que cierra o que descarta sin aviso, los niveles del log de cierre, un aviso con el tamaño del
    paquete, la lectura sin el `std::min`): los mata algún test. Unos `static_assert` fijan que una
    ofuscación temporal no compila y que la `Connection` no se copia ni se mueve.
  - De la segunda vuelta de la revisión: los niveles del log de cierre estaban fijados solo en
    parte (pasar `ConnectTimedOut`, `BadStream`, `SendQueueFull`, `Refused` o `ResolveFailed` a
    `info` sobrevivía). `EveryCloseIsLoggedWithItsLevel` es una tabla con una fila por motivo de
    `CloseReason`, cada una con su conexión, que mira la línea entera y su nivel: esos cinco
    mutantes y el de `PeerClosed` mueren. El `ClaimsMoreSocket`, que estaba copiado en los tests de
    la `Connection` y de la `SendQueue`, pasó a `tests/support/ClaimsMoreSocket.h`, con las dos
    mentiras: la de `Receive` y la de `Send`.
  - `ServerSessionTest.WhatFollowsTheAddressIsDropped` es del PR B: la `Connection` vacía el corte
    en `Open` (`ReopeningStartsClean`), pero lo que ya salió en el lote lo tiene que dejar de
    recorrer la sesión.
- [x] **Paso 3 (PR A):** `Connection` (sin el cifrado, que llega en la CL4.4) y `Delivery` en
  Network. Lo que se decidió al escribirla, y lo que cambió respecto de la interfaz de arriba:
  - Sin el cifrado no están todavía `paddingSeed` en el constructor, `keys` en `Open`,
    `ConnectionKeys`, el evento `Undecryptable` ni los seriales: los suma la CL4.4, PR A. Hasta
    entonces un C3/C4 que llega se descarta con un aviso (la CL4.4 lo descifra o lo da como
    `Undecryptable`), y lo que va `Encrypted` no sale, con un aviso que dice el opcode, que es lo
    que va a seguir pasando en una conexión sin claves.
  - El `Opened` sale siempre en un `Poll`, aunque el socket haya conectado en el `StartConnect`
    (en loopback pasa): `PollConnect` repite el resultado (`ISocket.h`). Hasta ese `Poll` el
    estado es `Connecting`, y `Send` solo encola. `SocketError::Closed` es también "no conectado"
    (`ISocket.h`): la `Connection` llama a `Receive`, `Send` y `SendQueue::Flush` solo en `Open`,
    así un `Closed` ahí es siempre `PeerClosed`. Lo prueban `APeerThatClosesCloses`,
    `ASendToAPeerThatClosedCloses` y `NothingIsReadOrSentBeforeTheConnect`.
  - Un cierre que pasa fuera de un `Poll` (en `Open`, si el socket ya sabe que no se puede; en
    `Send`, con la cola llena o un error del socket) cierra ahí, `State` y `LastCloseReason` ya lo
    dicen, y el `Closed` sale al final del `Poll` siguiente. `Close` y el cierre que hace `Open`
    sobre una conexión abierta no son eventos, porque los pidió quien la usa. `Open` olvida el
    `Closed` que nadie vio y el `LastCloseReason`.
  - Cada cierre va al log con `CloseReasonText`: en `info` el de `ByClient`, en `warning` el
    resto, y el de `SocketFailed` con el error del sistema, que solo la conexión ve. Por eso
    `CloseReason.h` tiene un `CloseReason.cpp`, que el plan no tenía; la sesión del PR B lo puede
    usar para su propio log. Los avisos de un paquete descartado dicen el tipo y el opcode
    (`C2 F4`, `C3`), nunca sus bytes.
  - En `Poll` va primero el `Flush` y después la lectura, como dice la interfaz; si el `Flush`
    falla, ese `Poll` no lee: el socket ya dio el cierre. Con un error de `Flush`,
    `SendQueue::LastSocketError()` siempre lo tiene; si viniera vacío, sería un bug de la cola, y
    la conexión lo toma como `SocketFailed`, sin decir que cerró el otro lado.
  - De lo que la CL4.1 dejó para esta sub-ola: el log de lo que tardó la resolución está acá.
    `Open` mide el `StartConnect` con el `IClock` y lo anota si pasa de `SlowConnectStart`
    (100 ms); el `ConnectTimeout` cuenta desde que vuelve. Para probarlo, el `FakeSocket` suma
    `onStartConnect`, que corre al principio de cada `StartConnect` y mueve el `FakeClock`. Que
    `main.cpp` cree el `NetworkInit`, no: en este PR `Main` no abre ninguna conexión, y va con
    quien arme la red en `main.cpp`.
  - `Delivery` bajó a `Network/Delivery.h`; `Outgoing` lo usa desde ahí, y su `PrintTo` pasó de
    `support/ProtocolPrinters.h` a `support/NetworkPrinters.h`, con los de `CloseReason` y
    `LinkState`. `LinkState` va en su `Network/LinkState.h`, como `CloseReason.h`, para que los
    printers no incluyan la `Connection` entera.
- [x] **Paso 4 (PR B):** los tests de la sesión, con `FakeSocket` haciendo de CS:
  - `A9Test.WritesTheBytesOfTheWire` (`C1 06 A9 00 00 01` con XOR32 es `C1 06 A9 20 9C 2F`,
    en claro);
  - `ServerSessionTest.SendsTheA9WhenTheConnectServerOpens` (lo primero que sale es
    `C1 06 A9 20 9C 2F`);
  - `ServerSessionTest.AsksForTheListAfterTheHello` (después de `C1 04 00 01` sale `C1 04 F4 02`
    exacto: el test del 04);
  - `ServerSessionTest.TheA9LeavesBeforeAHelloThatArrivesWithTheConnect` (el connect termina y
    `C1 04 00 01` llega en el mismo `Poll`: `sent` es `C1 06 A9 20 9C 2F C1 04 F4 02`);
  - `ServerSessionTest.ChoosesTheFirstServer` (la lista del cable, con FoxAnti: sale
    `C1 06 F4 03 00 00`);
  - `ServerSessionTest.ReadsTheGameServerAddress` (`C1 16 F4 03`, `"172.16.198.1"` en 16 bytes y
    `5D DA`: `GameServer()` es `{172.16.198.1, 55901}`, el CS se cerró y el estado es
    `ConnectingGameServer`);
  - `ServerSessionTest.WhatFollowsTheAddressIsDropped` (el `F4 03` y un `C1 04 00 01` en la misma
    lectura: sigue `ConnectingGameServer`, y el log no tiene ningún aviso de opcode desconocido,
    porque el `00` no llegó al dispatcher del GS);
  - `ServerSessionTest.AnEmptyListFails` (sin ofuscación, `C2 00 07 F4 06 00 00`:
    `EmptyServerList`);
  - `ServerSessionTest.AnUnexpectedPacketIsIgnored` (un `F4 03` antes de la lista: aviso y sigue
    en `WaitingServerList`);
  - `ServerSessionTest.LosingTheConnectServerFails` (`PeerClosed` en `WaitingServerList`:
    `ConnectServerLost`);
  - `ConfigTest.ReadsTheObfuscation` (`foxanti`, `none`, por defecto `foxanti`; `xor` es un error
    con su línea).

  Hechos en el PR B, con lo que cambió al escribirlos:
  - El `A9` se llama por lo que es: `A9Test` es `PetInfoTest`,
    `SendsTheA9WhenTheConnectServerOpens` es `SendsThePetInfoRequestWhenTheConnectServerOpens` y
    `TheA9LeavesBeforeAHelloThatArrivesWithTheConnect` es
    `ThePetInfoRequestLeavesBeforeAHelloThatArrivesWithTheConnect`. `PetInfoTest` suma
    `PutsEachFieldAtItsOffset`.
  - `ReadsTheGameServerAddress` usa una dirección cualquiera (`10.0.0.2`, 55901) y no la IP del
    laboratorio: el `FakeSocket` no conecta, y la del cable no tiene por qué estar en un test.
    Mira además que no haya avisos. `WhatFollowsTheAddressIsDropped` sigue hasta el `Opened` del
    GS, que no manda nada.
  - Se sumaron, en `ServerSessionTest`: `StartsIdle`, `ChoosesTheFirstOfSeveral` (el primero en el
    orden en que llega, no el de código más bajo), `WithTheGameServerThe00IsNotTheConnectHello`
    (con el GS, el `00` va a su dispatcher, que no tiene handlers: el aviso es el del
    `Dispatcher`), `AnEmptyListBeforeTheCloseKeepsItsFailure`, `ASecondHelloIsIgnored` (el
    original pide la lista dos veces en 8 de 64 conexiones), `AListOutOfTurnIsIgnored`,
    `AListThatDoesNotReadIsIgnored`, `AnAddressThatDoesNotReadIsIgnored`,
    `AMalformedPacketIsLogged`, `AConnectServerThatRefusesFails`, `LosingTheGameServerFails`,
    `WhatFollowsACloseInAHandlerIsDropped`, `WhatArrivesBeforeTheServerClosesIsHandled` y
    `StartingAgainStartsClean`; en `ConfigTest`, `ResolveKeepsTheObfuscation` (`/u /p` no la
    cambia); y `AppPrintersTest.CredentialsShowTheLengthsAndNotTheText`, con la cuenta y la clave
    sintéticas de `MuClientNetFixtures.h`.
  - Treinta y tres mutantes a mano de `ServerSession.cpp` (sin cortar el lote por el `Open` o por
    un cierre en un handler, cortarlo también con un cierre del `Poll`, sin el `A9`, el `Opened`
    del GS como el del CS, el saludo sin mirar el turno o sin pedir la lista, el último de la
    lista, una lista vacía que no falla o que sigue el lote, la lista o la dirección sin mirar el
    turno o sin aviso si no se leen, la dirección sin guardar o con otro puerto, `Fail` sin cerrar,
    un cierre del GS como del CS, los avisos en otro nivel, siempre el dispatcher del CS, `Start`
    sin olvidar la falla o el GS, entre otros): los mata algún test.
  - De la revisión del PR: `Poll` recorre el lote con un `std::visit` que tiene un caso por tipo de
    `Network::Event`, así el `Undecryptable` de la CL4.4 no compila hasta que la sesión diga qué
    hace con él (con el `std::get` de antes, compilaba y tiraba `bad_variant_access`).
    `TheLogDoesNotSayTheAddresses`, y una línea más en `StartingAgainStartsClean`, miran que
    ninguna línea del log diga el host ni el puerto del CS o del GS: tres mutantes más (la
    dirección del GS en el aviso del `F4 03`, el host o el puerto del CS en `Start`) mueren.
    `SessionState` y `SessionFailure` pasaron a `App/SessionState.h`, así `AppPrinters.h` no trae
    la `Connection`. Los `Reach*` de los tests van dentro de `ASSERT_NO_FATAL_FAILURE`.
- [x] **Paso 5 (PR B):** `ServerSession` hasta `ConnectingGameServer`, el `A9` y la config. Lo que
  se decidió al escribirla, y lo que cambió respecto de la interfaz de arriba:
  - El constructor es `ServerSession(Connection&, Log&, SessionSettings)`: sin el `IClock` ni las
    `ConnectionKeys`, que suman la CL4.4 (el login lleva el tick y el GS cifra) y la CL4.5 (el
    keepalive). `SessionState` y `SessionFailure` llegan hasta lo que usa este PR; la CL4.4 suma
    `WaitingGameHello`, `WaitingLoginResult`, `LoggedIn`, `HelloRejected`, `VersionMismatch` y
    `LoginRejected`. `SessionSettings` ya trae las `Credentials`, que no se usan hasta el login.
  - El pedido de la mascota es `Protocol::Pet::WritePetInfoRequest`, en `Protocol/Pet/PetInfo.h`
    con su opcode (`PetInfoOpcode`) y las constantes de la mascota, el lugar y la casilla;
    `LoginScenePetInfoRequest` es el del CS, el cuervo oscuro de la mano izquierda, igual en
    cualquier mapa y con cualquier personaje. Va con el XOR32 y en claro, que no daban ni
    `ClearC1` ni `EncryptedC1`: `Outgoing` suma `Xor32C1`. No se reproduce nada del anticheat (la
    bandera `0x2000000` es de la UI, y el `A9` del juego es de la CL8).
  - Hay dos `Dispatcher`, el del CS con el `00`, el `F4 06` y el `F4 03`, y el del GS, vacío hasta
    la CL4.4; el estado elige cuál. Para dejar el resto del lote, la sesión lleva un contador que
    cambia con cada `Open` y cada `Fail`, y mira también si un `Send` de un handler cerró la
    conexión (la revisión del PR A sumó eso al comentario de `Connection::Poll`). Si la cerró el
    `Poll` mismo, el `Closed` es lo último del lote y lo de antes se despacha: si no, la sesión no
    vería nunca ese `Closed`.
  - Los avisos dicen qué paquete se ignora y por qué, sin sus bytes; el log no dice la dirección
    del GS (el original la loguea), como no dice el host de la config. Perder la conexión y la
    lista vacía van en `error`.
  - `[Server] Obfuscation` es `ClientConfig::obfuscation`, un `ObfuscationMode` en su propio header
    como `PacingMode`, y no arma `config.server`. Elegir la `IObfuscation` según el modo es de quien
    arme la red en `main.cpp`, igual que el `NetworkInit`: en este PR `Main` no conecta.
- [x] **Paso 6:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug &&
    ctest --preset linux-x64-debug -R "Connection|PetInfo|ServerSession|Config|AppPrinters"
  ```

## Sub-ola CL4.4: GameServer: saludo, versión y login

**Concepto:** el paso a una conexión cifrada: claves por conexión, seriales que vuelven a 0, y el
primer paquete que solo se puede comparar descifrándolo. Va en tres PRs: **PR A**, el cifrado en
la `Connection`; **PR B**, la sesión hasta `LoggedIn`, y las variables y las claves en `main.cpp`;
**PR C**, los `FakeServers` y la integración con sockets reales en `127.0.0.1`.

**Contrato** (04 § El flujo de conexión; 03 § C3/C4):

| Paso | Original | MuRet |
| --- | --- | --- |
| conectar | `0x4c1340` con la dirección del `F4 03`; los seriales a 0 | `Connection::Open(gs.host, gs.port, keys)`: `PacketSerials::Reset` |
| las claves | `Enc1.dat` y `Dec2.dat`, cargadas en `WinMain` (`0x4ba094`, `0x4ba0a3`; Q-C41) | `LoadEncryptionKey` y `LoadDecryptionKey` (CL2.3) desde la carpeta de datos, en `main.cpp`, antes de abrir la ventana. Con host, primero las variables de la pregunta 7 y después las claves: sin carpeta o sin archivos, un error que nombra los dos. Los reales no se versionan (D-C7), y tampoco hay `.dat` sintéticos en Git: los tests arman las `ConnectionKeys` en memoria con `SimpleModulusKeys.h`, y el de `LoadConnectionKeys` escribe los dos archivos al correr, en una carpeta temporal |
| el saludo | `C1 0C F1 00 01 <idx:2> "10209"` (`0x4c1e20`): con otro resultado, "Connectting error"; con otra versión, "Version dismatch" (`0x65fe50(0x6f)`), aunque el resultado no sea 1 | `ReadServerHello`: `HelloRejected` si el resultado no es 1, y si no, `VersionMismatch` con las dos versiones en el log |
| el login | `C1 31 F1 01` con XOR32, como C3 de 68 bytes, serial 0 (`0x641bf0`) | `WriteLoginRequest` con las credenciales y `App::TickCountMs(now)` (CL3.2, un `DWORD` como `GetTickCount`), `Delivery::Encrypted` |
| el relleno | un byte de `rand()` detrás de lo útil | un `std::mt19937` de la `Connection` con la semilla que le da `main.cpp` (`std::random_device`); los tests descifran lo que salió en vez de comparar el cifrado |
| el resultado | `C1 05 F1 01 01`: `DAT_05767928 = 2` y el primer keepalive (CL4.5) | `ReadLoginResult`: 1 es `LoggedIn`; otro, `LoginRejected` con el resultado (qué dice cada uno es de la CL5.5) |
| un C3/C4 que no descifra | no despacha, suma 1 al serial y manda `C1 06 F1 03 06 00` con XOR32 como C3 (`C1 06 F1 7B C1 73`) | `Undecryptable` con `ChecksumMismatch` o `NotWholeBlocks`: la sesión manda `WriteDecryptErrorReport()`; con los otros, solo el aviso |
| el serial desfasado | se queda desfasado (Q-C04) | `SerialCheck::Resynced`, un aviso en el log (CL2.3) |
| FoxAnti | `BeforeEncrypt` remapea `0x10`, `0x19` y `0x1E`; el `0xAA` en todo lo que manda el GS | lo mismo, por la `IObfuscation` de la conexión |

**Archivos:**
- **PR A:** modificar `Client/src/Network/Connection.h/.cpp` (las claves, `EncryptPacket` y
  `DecryptPacket`, los seriales) y `Client/tests/network/test_connection.cpp`; PROGRESS y este
  plan.
- **PR B:** crear `Client/src/App/Credentials.cpp` (leer las variables, con `_dupenv_s` en MSVC
  como `GoldenDir.cpp`) y `Client/tests/app/test_credentials.cpp`. Modificar
  `Client/src/App/Credentials.h` (`ReadCredentials`), `Client/src/App/ServerSession.h/.cpp` y su
  test, `main.cpp` (las variables y después las claves), `Client/CMakeLists.txt` (los CTest de
  `Main`: todos con
  `ENVIRONMENT_MODIFICATION "MURET_CLIENT_ACCOUNT=unset:;MURET_CLIENT_PASSWORD=unset:"`, y el de
  las claves con `set:user0001` y `set:pass0002` y una carpeta vacía de `CMAKE_CURRENT_BINARY_DIR`
  como `--data`), `Client/README.md` (la cuenta con `read -rs` o `Read-Host -MaskInput`),
  PROGRESS y este plan.
- **PR C:** crear `Client/tests/support/FakeServers.h/.cpp` (el CS y el GS falsos: los bytes del
  cable sobre dos `LoopbackServer`, con las claves sintéticas de `SimpleModulusKeys.h` del lado
  del servidor) y `Client/tests/app/test_server_session_loopback.cpp`. Modificar PROGRESS y este
  plan.

```cpp
namespace Client::App
{
// Lee una variable: std::nullopt si no está. La de main.cpp usa _dupenv_s en MSVC y std::getenv
// en el resto; la de los tests, un mapa.
using EnvironmentLookup = std::function<std::optional<std::string>(const char* name)>;
// La cuenta y la clave de la pregunta 7. El error nombra la variable que falta, que está vacía o
// que pasa de 10 bytes (FieldTooLong en WriteLoginRequest), y nunca trae su valor.
[[nodiscard]] Core::Expected<Credentials, std::string> ReadCredentials(const EnvironmentLookup& lookup);
} // namespace Client::App

namespace Client::Tests
{
// El CS y el GS falsos, consultados en el mismo hilo que el cliente. El F4 03 da 127.0.0.1 y el
// puerto del GS falso, no la dirección del cable.
class FakeServers
{
public:
    // Las claves del lado del servidor: con ellas descifra el login y cifra el saludo y el
    // resultado.
    explicit FakeServers(Network::ConnectionKeys serverKeys);
    [[nodiscard]] std::uint16_t ConnectServerPort() const;
    // Una vuelta: acepta, lee lo que mandó el cliente y contesta lo que toca en el guion del 04.
    void Poll();
    [[nodiscard]] bool SawLogin() const;
};
} // namespace Client::Tests
```

- [x] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests, a mano: el serial de cada C3 que
  manda el cliente desde que abre el GS hasta el tercer keepalive (el login, el primer keepalive y
  dos más), y qué pasa con el serial de recepción si llega un C3 con el checksum malo y después
  uno bueno. Lo resolvió Claude:

  | Qué | El serial de envío | El de recepción |
  | --- | --- | --- |
  | `Open` al GS | 0 (`PacketSerials::Reset`) | espera el 0 |
  | el login, el primer C3 | 0 | |
  | el primer keepalive | 1 | |
  | el segundo y el tercero | 2 y 3 | |
  | un C3 del GS con el checksum malo (el GS le puso el 0) | | no se despacha: `SkipReceive`, espera el 1 sin mirar el que trajo |
  | el C3 bueno que sigue (con el 1) | | `InOrder`; espera el 2 |

  Lo que sale en claro no gasta serial. El `F1 03` que manda la sesión con un checksum malo es un
  C3 y toma el siguiente: si llega entre el login y el primer keepalive, el keepalive sale con el
  2. Si el GS no hubiera gastado el 0 en el malo y mandara el bueno con el 0, ese sería
  `Resynced`, con un aviso, y desde ahí se esperaría el 1.
- [x] **Paso 2 (PR A):** los tests, con claves sintéticas:
  - `ConnectionTest.EncryptsWhatGoesEncrypted` (un C1 `Encrypted` sale como C3 y, descifrado con
    la clave del servidor, es el mismo C1 con serial 0; el siguiente, serial 1);
  - `ConnectionTest.DecryptsWhatArrivesEncrypted` (un C3 del servidor sale como el C1 rearmado);
  - `ConnectionTest.OpeningAgainResetsTheSerials` (dos C3, `Open` otra vez, el siguiente sale con
    serial 0);
  - `ConnectionTest.ABadChecksumIsUndecryptable` (`Undecryptable{ChecksumMismatch}`, y el C3
    bueno que sigue se despacha);
  - `ConnectionTest.AnEncryptedPacketWithoutKeysIsUndecryptable` (un C3 en la conexión del CS: un
    `Undecryptable`, y la conexión sigue `Open`);
  - `ConnectionTest.WithFoxAntiWhatArrivesFromTheGameServerIsUnmasked` (un C1 con el `0xAA`).

  Hechos en el PR A, con lo que cambió al escribirlos:
  - `WithFoxAntiWhatArrivesFromTheGameServerIsUnmasked` ya estaba, de la CL4.3: la máscara es del
    corte. Lo de los C3 es `WithFoxAntiWhatArrivesEncryptedIsUnmaskedDecryptedAndTranslated`: el
    `0xAA` se saca en el corte, después se descifra, y lo descifrado pasa por `BeforeDispatch` (un
    `F3 11` recibe su byte).
  - `AnEncryptedPacketIsNotDelivered` y `AnEncryptedSendDoesNotLeave`, de la CL4.3, son
    `AnEncryptedPacketWithoutKeysIsUndecryptable` y `AnEncryptedSendWithoutKeysDoesNotLeave`.
  - Los tests no comparan lo cifrado: lo que salió se corta y se descifra con la clave sintética
    del servidor (`SimpleModulusKeys.h`), y se compara con `SameBytesWithoutValues`; lo que llega
    lo cifra el test con la otra.
  - Se sumaron, en `ConnectionTest`: `WhatIsEncryptedWhileConnectingLeavesWhenItOpens`,
    `OpeningWithoutKeysForgetsTheOldOnes`, `AnEncryptedPacketThatIsNotWholeBlocksIsUndecryptable`,
    `ASerialOutOfOrderIsResyncedOnce` (un aviso, una vez), `WithFoxAntiWhatIsEncryptedIsRemapped`
    (el `BeforeEncrypt`: el `0x10` llega al GS como `0x1D`), `ThePaddingComesFromTheSeededGenerator`
    y `APacketThatDoesNotEncryptDoesNotLeave` (un C2 que como C4 pasaría de `MaxPacketSize`: no
    sale y no gasta serial); `CryptoErrorTest.EveryErrorHasItsOwnText`;
    `ServerSessionTest.AnUndecryptablePacketIsIgnored`; y, contra el cable del laboratorio con las
    claves reales, `GoldenConnection.DeliversWhatTheGameServerAnswered` (los 25 C3/C4 del GS, en 7
    conexiones, salen como los dan `DecryptPacket` y el `BeforeDispatch` de FoxAnti) y
    `GoldenConnection.EncryptsLikeTheClient` (los 183 C3 del cliente, descifrados y mandados otra
    vez, salen con la cabecera y los bloques del cable menos el último, que lleva el relleno). Los
    dos usan una sola `Connection` y la abren de nuevo en cada conexión del log, y cuentan sin
    imprimir bytes.
  - Diecisiete mutantes a mano de `Connection.cpp` (`Open` sin guardar las claves, o guardándolas
    solo si hay, o sin volver los seriales a 0; un cifrado que falla y sale igual; cifrar sin la
    ofuscación; el relleno en 0 o con el byte de arriba; lo descifrado sin `BeforeDispatch`; el
    `Undecryptable` que no sale o sin su error; el `Resynced` sin aviso o el aviso también en
    orden; sin claves, sin aviso al recibir o el `Encrypted` que sale en claro; un C3 que pasa sin
    descifrar; lo `Clear` cifrado también): los mata algún test.
- [x] **Paso 3 (PR A):** el cifrado en `Connection`. Lo que se decidió al escribirlo, y lo que
  cambió respecto de la interfaz de la CL4.3:
  - `Undecryptable` lleva un `std::optional<Crypto::CryptoError>`, y no un `CryptoError` a secas:
    `std::nullopt` es un C3/C4 en una conexión sin claves (la del CS). Ningún `CryptoError` dice
    eso, y sumarle uno al enum del cifrado lo mezclaría con los de la clave. La PR B manda el
    `F1 03` solo con `ChecksumMismatch` o `NotWholeBlocks`, como dice el contrato.
  - El aviso de un `Undecryptable` lo deja la conexión, uno por paquete, y la sesión no lo repite.
    Dice el tipo del cable (de un C3/C4, nunca el opcode, que va cifrado) y el error en palabras:
    `Crypto::CryptoErrorText`, en un `CryptoError.cpp` nuevo. Un `Resynced` deja un aviso cada vez
    que pasa: como `PacketSerials` se resincroniza, un desfase da uno solo.
  - El relleno es el byte de abajo de cada número del `std::mt19937`: el motor lo fija el
    estándar, así la serie es la misma con cualquier biblioteca (`std::uniform_int_distribution`
    no). Se saca uno por paquete que se intenta cifrar.
  - Lo que va `Encrypted` se cifra en `Send`, con el serial de ese momento, y espera cifrado en la
    cola si la conexión todavía conecta. Si `EncryptPacket` falla, no sale, con un aviso que dice
    el tipo, el opcode y el error, y la conexión sigue. Sin claves, el aviso de la CL4.3 dice
    "esta conexión no tiene claves".
  - `Open` toma las claves por valor y se las queda, y volver a abrir sin claves se olvida de las
    de antes. La `ServerSession` abre las dos conexiones con `std::nullopt` hasta que la PR B
    cargue las del GS, y no hace nada con un `Undecryptable`: su `std::visit` (de la revisión de
    la CL4.3, PR B) no compilaba sin ese caso.
  - De la revisión: `WhatArrivesIsDecryptedBetweenAfterCutAndBeforeDispatch` y
    `WhatIsEncryptedIsEncryptedBeforeBeforeSend`, con una ofuscación espía que anota cada etapa y
    el tipo del paquete que ve (`AfterCut` ve el C3 y `BeforeDispatch` el C1; `BeforeSend` ve el
    C3), y `WithFoxAntiAnEncryptedD3IsNotRemapped` (un D3 que va cifrado llega como D3, porque el
    `BeforeSend` de FoxAnti solo toca los C1 en claro). Los matan los mutantes que descifran antes
    del `AfterCut` y que llaman a `BeforeSend` antes de cifrar. Los dos tests golden pasaron a
    usar `ExpectDecryptedLikeDecryptPacket`, `ResendEncrypted` y `ExpectLikeTheWire`. El 03 suma
    la fila de las claves por conexión: un C3/C4 en el CS no manda el `F1 03`.
- [x] **Paso 4 (PR B y PR C):** los tests de la sesión:
  - `ServerSessionTest.ConnectsToTheGameServerItWasGiven` (después del `F4 03`, `connects` tiene el
    endpoint del GS);
  - `ServerSessionTest.SendsTheLoginAfterTheHello` (con `C1 0C F1 00 01 00 05 "10209"`, el C3 que
    sale descifra a los 49 bytes de `WriteLoginRequest` con `"user0001"`, `"pass0002"` y el tick
    del `FakeClock`, comparados con `SameBytesWithoutValues`);
  - `ServerSessionTest.ARejectedHelloFails` (resultado 0: `HelloRejected`, sin login);
  - `ServerSessionTest.AnotherVersionFails` (`"09908"`: `VersionMismatch`, sin login);
  - `ServerSessionTest.AnAcceptedLoginIsLoggedIn` (`C1 05 F1 01 01`: `LoggedIn`);
  - `ServerSessionTest.ARejectedLoginFails` (`C1 05 F1 01 00`: `LoginRejected` con 0);
  - `ServerSessionTest.ABadChecksumSendsTheReport` (el C3 que sale descifra a `C1 06 F1 7B C1 73`);
  - `ServerSessionTest.TheLogDoesNotHaveTheCredentials` (ninguna línea del log tiene
    `"user0001"` ni `"pass0002"`);
  - `CredentialsTest.MissingNamesBoth` (sin las dos: el error nombra `MURET_CLIENT_ACCOUNT` y
    `MURET_CLIENT_PASSWORD`);
  - `CredentialsTest.EmptyOrTooLongNamesItWithoutItsValue` (la clave vacía, y la cuenta
    `"user0001xyz"` de 11 bytes: el error nombra la variable y no trae el valor);
  - `Client.Main.ConnectWithoutCredentialsNamesThem` (`Main connect /u127.0.0.1 /p44405` con las
    variables en `unset:`: la salida nombra las dos, sin leer `--data`, sin red y sin ventana);
  - `Client.Main.ConnectWithoutKeysNamesThem` (lo mismo con las variables en `set:user0001` y
    `set:pass0002` y `--data` a la carpeta vacía: la salida nombra `Enc1.dat` y `Dec2.dat`, sin
    abrir la ventana);
  - **PR C:** `ServerSessionLoopbackTest.LogsInAgainstFakeServers`: con `SystemSocket` y los
    `FakeServers` en `127.0.0.1`, el CS manda los bytes del cable del 04 (con FoxAnti), el GS el
    saludo y el resultado cifrados con la clave sintética del servidor, y la sesión llega a
    `LoggedIn` antes de 200 vueltas del lazo (la integración que pide el 04).

  Hechos en el PR B todos menos el del PR C, con lo que cambió al escribirlos:
  - `ConnectsToTheGameServerItWasGiven` mira también que al abrir el GS no sale nada y la sesión
    pasa a `WaitingGameHello`; `ARejectedHelloFails` prueba también resultado 0 con `"09908"`,
    que es `HelloRejected` (el resultado se mira primero); `ABadChecksumSendsTheReport` mira que
    el `F1 03` sale con el serial 1 y que el resultado que llega después se lee.
  - Se sumaron, en `ServerSessionTest`: `AHelloThatArrivesEncryptedAlsoSendsTheLogin` (el GS del
    laboratorio lo manda como C3), `AVersionThatDoesNotPrintIsLoggedWithQuestionMarks`,
    `APacketThatIsNotWholeBlocksSendsTheReport`, `AnotherUndecryptablePacketSendsNoReport` (un
    `TooShort`), `AGameHelloOutOfTurnIsIgnored`, `ALoginResultOutOfTurnIsIgnored`,
    `WhatDoesNotReadFromTheGameServerIsIgnored`, `CredentialsThatDoNotFitFail`,
    `LosingTheGameServerAfterTheLoginFails` y `StartingAgainForgetsTheLogin`. En
    `CredentialsTest`: `ReadsBoth`, `MissingOneNamesOnlyIt`, `TenBytesFit` y
    `SystemEnvironmentReadsTheProcess`. Y `ConnectionKeyFilesTest`, con los errores sobre una
    carpeta temporal y, con la carpeta golden, las claves que cargan y que se encuentran sin mirar
    mayúsculas.
  - Lo que hace el GS de los tests (cifrar lo que contesta, descifrar lo que recibe) pasó de
    `test_connection.cpp` a `tests/support/TestGameServer.h`, para la sesión también.
  - Cuarenta y dos mutantes a mano: veintiocho de `ServerSession.cpp` (el GS sin claves o el CS
    con ellas, sin mirar el resultado o la versión, el login en claro o con el tick en 0, cada
    transición que falta, el `F1 03` que no sale, sale en claro, sale solo con el checksum o con
    cualquier error, la versión sin limpiar, cada chequeo de turno), y siete de `Credentials.cpp`
    y otros siete de `ConnectionKeyFiles.cpp`: los mata algún test, aunque a dos de los de
    `ConnectionKeyFiles.cpp`, hasta la revisión, solo con la carpeta golden.
- [x] **Paso 5 (PR B y PR C):** la sesión del GS hasta `LoggedIn`, `ReadCredentials`, y las
  variables y las claves en `main.cpp` (PR B); los `FakeServers` y el test en loopback (PR C).

  Lo que se decidió en el PR B, y lo que cambió respecto de la interfaz:
  - El constructor es el de la interfaz, con el `IClock` y las `ConnectionKeys`: el GS abre con
    ellas y el CS sin claves. `SessionFailure` no lleva datos, así que el resultado del login
    queda en `LoginResult()`, aceptado o no, hasta el próximo `Start`.
  - Una falla que no estaba: `CredentialsTooLong`, si `WriteLoginRequest` da `FieldTooLong`.
    `ReadCredentials` ya no deja pasar más de 10 bytes, pero la sesión no depende de quién la
    armó; el error no trae la cuenta.
  - El resultado del saludo se mira antes que la versión. El original muestra "Version dismatch"
    aunque el resultado no sea 1, pero un GS que rechaza no tiene por qué mandar su versión. La
    versión va al log con `?` en cada byte que no se ve, porque viene del servidor.
  - El `F1 03` sale con `ChecksumMismatch` y `NotWholeBlocks`, con una línea de info; con los
    otros errores, y sin claves, solo queda el aviso de la conexión.
  - `EnvironmentLookup` recibe un `std::string_view` y no un `const char*`: el mapa de los tests
    busca con `std::less<>`, y `SystemEnvironment` copia el nombre para ponerle el `\0`.
    `MaxCredentialBytes` es `CredentialSize` de `Protocol/Connection/Login.h`.
  - Las claves las lee `App/ConnectionKeyFiles.h` (`LoadConnectionKeys`), y no Data, porque los
    cargadores reciben bytes: busca con `ResolveDataPath`, lee 4096 bytes como mucho, y el error
    nombra los dos archivos y qué pasó con cada uno.
  - `main.cpp` lee las variables y después las claves en la fase 1, solo con un host que no está
    vacío (un `[Server]` con solo el puerto no conecta), y el error va a stderr como los de la
    config. El `NetworkInit` va después del log y antes de SDL. La sesión en el bucle es de la
    CL4.5, igual que la semilla de `std::random_device`: con un host, `Main` lo dice en el log y
    sigue como sin host, y sin host no cambia nada, así `Main --frames 60` corre sin un servidor.

  De la revisión del PR B:
  - CI no corría el camino que carga de `LoadConnectionKeys`: los dos tests que cargan claves son
    golden, y sin la carpeta sobrevivían `Enc1.dat` y `Dec2.dat` cambiados de lugar y un tope de
    lectura de 20 bytes. `ConnectionKeyFilesTest.LoadsEachKeyFromItsFile` escribe al correr, en
    una carpeta temporal, un archivo de 54 bytes con la de cifrar de `Pair17` y otro con la de
    descifrar de `Pair18`, y mira los bloques de cada una: los dos mueren.
  - `HasHost` de `main.cpp` pasó a `App::HasServerHost` (`Config.h`), con tres tests: solo el
    puerto, un host vacío y un host. `return config.server.has_value();` ya no sobrevive.
  - `SystemEnvironment` y los tests golden leen el entorno con `Core::ReadEnvironmentVariable`
    (`Core/Environment.h`), el único `#ifdef _MSC_VER` de `ClientLib` fuera de los sockets. La
    copia de `_dupenv_s` la libera un `std::unique_ptr` con un deleter propio y no con
    `&std::free`, porque la dirección de una función de la biblioteca estándar no está
    garantizada.
  - El `F1 03` con `NotWholeBlocks` es *probable* en el original, no verificado (03, "C3/C4"), y
    `main()` saca el `NetworkInit` a `StartNetwork`. El README carga con `IFS= read -rs` y dice
    cómo borrar la clave del entorno al terminar.
  - 07: Q-C41 dice lo que hace `Main` sin clave, y Q-C55, que MuRet cierra el GS con un saludo o
    un login rechazados y mira el resultado antes que la versión.

  Lo que se decidió en el PR C, y lo que cambió respecto de la interfaz:
  - `FakeServers` sigue el guion del 04 de a un paquete: cada servidor contesta solo lo que toca
    después de lo que le llegó. El CS espera el A9 antes de saludar (el orden del cable), y el GS
    saluda apenas acepta. Lo que manda el cliente se compara con los bytes del cable: el A9 con
    su XOR32, el `F4 02` y el `F4 03` con el código que FoxAnti pasó a 0. El GS manda el saludo
    y el resultado cifrados con la clave sintética del servidor y con el `0xAA` en cada byte,
    como el del laboratorio (398 de 398 segmentos del GS en el 04).
  - Suma a la interfaz `GameServerPort()`, `Login()` (el login descifrado, para compararlo con
    `WriteLoginRequest` con `SameBytesWithoutValues`), `ClientLeftTheConnectServer()` (el CS ve
    el corte) y `Complaints()`: con algo fuera del guion, el servidor lo anota en palabras, sin un
    byte, corta al cliente y deja de contestar, así la sesión falla enseguida y el test dice qué
    vio. `TestGameServer.h` suma `GameServerKeys()`, las dos claves del lado del servidor.
  - El tope no son 200 vueltas del lazo sino los 5 s reales de `RetryUntil`, como los tests de
    `SystemSocket`: una vuelta no es tiempo, y lo que hace el sistema en el fondo (el connect, el
    accept, los bytes) puede pedir muchas en una máquina cargada. En loopback pasa en menos de un
    milisegundo; 4800 corridas, 16 a la vez, pasaron todas.
  - El reloj de la sesión es un `FakeClock`: da el tick del login, que el test compara, y el
    connect no vence por el reloj.
  - `FakeServersTest.TheConnectServerComplainsAboutWhatIsNotInTheScript`: un `F4 02` antes del A9
    deja la queja, y el cliente ve el cierre.
  - Cinco mutantes a mano mueren en el test en loopback: el login en claro, el GS sin claves, la
    sesión que no pide la lista (este, por el tope de 5 s), el `F4 03` con otro código, y FoxAnti
    sin quitar el `0xAA` (los otros, enseguida, por una queja o un cierre).
- [x] **Paso 6:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug &&
    ctest --preset linux-x64-debug -R "Connection|ServerSession|Credentials|Client.Main"
  ```

## Sub-ola CL4.5: keepalive y desconexión

**Concepto:** un temporizador del bucle, por reloj y no por pasos, probado con el `FakeClock`; y
qué hace el cliente cuando la conexión se corta. Va en un PR.

**Contrato** (04 § El flujo de conexión, el keepalive; 02 § El WndProc y "Qué avanza por paso,
por frame y por reloj"):

| Qué | Original | MuRet |
| --- | --- | --- |
| el paquete | `C1 0C 0E 00 <tick:4> <ataque:2> <magia:2>` con XOR32, como C3 de 24 bytes (`0x4bee60`) | `WriteKeepalive` (CL2.5), `Delivery::Encrypted` |
| el primero | con el login aceptado (`0x4ad100`), 48 ms después en el cable | en el mismo `Poll` que lee el `F1 01` aceptado |
| los siguientes | `WM_TIMER` 1000 cada 20 s, un timer desde el arranque que manda si hay conexión al GS, sin mirar el login ni el foco | cada 20 s desde el primero, con un `IntervalTimer` sobre el `IClock` del bucle, solo en `LoggedIn` (pregunta 6); minimizada también |
| un frame largo | un `WM_TIMER` pendiente (*inferido*) | uno solo, y el siguiente a los 20 s del que salió |
| las velocidades | los WORD de `personaje+0x4C` y `+0x58` con su ajuste (CL2.5) | `SetKeepaliveSpeeds(attack, magic, flags)`; hasta que haya personaje (CL6), lo que diga la CL4.R, paso 6 |
| el tick | `GetTickCount` | `App::TickCountMs(now)` (CL3.2), que da la vuelta como un `DWORD` |
| el cierre del otro lado | `FD_CLOSE`: un mensaje, el log, `0x4e8380` y el objeto de `0x65ee60`; en la escena 5, el socket en -1 cada frame | `Failed` con `GameServerLost` y el `CloseReason` en el log; el keepalive se detiene; no se reconecta solo (la CL5.4 muestra el aviso) |

Nada le pone tope todavía a los estados `Waiting*` de la sesión (de la revisión de la CL4.3, PR
B): un CS que acepta y nunca saluda deja la sesión en `WaitingConnectHello` para siempre, sin
`Failed`, porque el `ConnectTimeout` es solo del connect. Esta sub-ola decide si la sesión suma un
tope por reloj, con el `IClock` que ya trae para el keepalive.

**Archivos:** crear `Client/src/Core/IntervalTimer.h/.cpp` y
`Client/tests/core/test_interval_timer.cpp`. Modificar `Client/src/App/ServerSession.h/.cpp` y
sus tests, `main.cpp` (la sesión en el `Poll()` del cliente del bucle, solo con host; cada cambio
de estado al log), 07 (el keepalive de la pregunta 6, Q-C nuevo), `Client/README.md` (la prueba
contra el laboratorio), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Core
{
// Algo que pasa cada `period` de reloj, no de pasos. Sin deriva: el siguiente vence a un periodo
// del anterior. Si se miró un periodo o más tarde, vence una sola vez y el siguiente queda a un
// periodo del instante en que se miró.
class IntervalTimer
{
public:
    explicit IntervalTimer(Duration period);
    void Start(Duration now);  // el primero vence en now + period
    void Stop();
    // true si venció (el siguiente, como dice arriba). Un reloj que va para atrás no vence.
    [[nodiscard]] bool Due(Duration now);
    [[nodiscard]] bool Running() const;
};
} // namespace Client::Core

namespace Client::App
{
// 04 § El flujo de conexión: el timer 0x3E8 de 20.000 ms (SetTimer en 0x4b9b80), y 156
// keepalives, uno cada 20 s, en el cable (02 § Una discrepancia entre documentos).
inline constexpr Core::Duration KeepalivePeriod = std::chrono::seconds{20};

// En ServerSession: las velocidades del personaje antes de su ajuste, y los bits de
// personaje+0x3C (KeepaliveSpeed, CL2.5). Hasta que se llame, las de la CL4.R, paso 6.
void SetKeepaliveSpeeds(std::uint16_t attackSpeed, std::uint16_t magicSpeed, std::uint8_t characterFlags);
} // namespace Client::App
```

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de los tests, a mano: con el login aceptado en
  t = 3 s, en qué instantes salen keepalives hasta t = 100 s, y qué cambia si entre t = 30 s y
  t = 90 s no corre ningún frame.
- [ ] **Paso 2:** los tests:
  - `IntervalTimerTest.FiresEveryPeriod` (empezado en 0 con 20 s: a los 19,999 s no, a los
    20,016 s sí, otra vez no, a los 39,999 s no y a los 40 s sí: sin deriva);
  - `IntervalTimerTest.ALongPauseFiresOnce` (a los 65 s vence una vez, y la siguiente a los 85 s);
  - `IntervalTimerTest.AClockThatGoesBackDoesNotFire` (empezado en 10 s: `Due(5 s)` no, y
    después a los 30 s sí);
  - `ServerSessionTest.SendsTheFirstKeepaliveWithTheLogin` (después de `C1 05 F1 01 01` sale un C3
    con serial 1 que descifra a lo mismo que `WriteKeepalive` con el `TickCountMs` del
    `FakeClock` y las velocidades de la CL4.R, paso 6);
  - `ServerSessionTest.KeepaliveEveryTwentySecondsAndNotBefore` (el test del 02: con el login en
    t = 3 s, ninguno nuevo a los 22,999 s; a los 23 s, uno; hasta t = 103 s, 6 en total, con
    seriales 1 a 6);
  - `ServerSessionTest.NoKeepaliveBeforeTheLogin` (abierto el GS y sin resultado del login, 60 s:
    ninguno; el original mandaría hasta tres);
  - `ServerSessionTest.ASixtySecondFrameSendsOneKeepalive` (con el login en t = 3 s, un frame en
    t = 63 s: un keepalive, con serial 2, y el siguiente a los 83 s);
  - `ServerSessionTest.KeepsTheKeepaliveWhileMinimized` (con el `GameLoop` y la ventana minimizada
    100 s: 1 + 5 keepalives);
  - `ServerSessionTest.UsesTheSpeedsItWasGiven` (`SetKeepaliveSpeeds(0x0328 + 20, 0x0268 + 20,
    KeepaliveAdjust20)`: el C3 descifrado y pasado por `Xor32Decode` tiene `28 03 68 02` en las
    velocidades; el `Xor32Decode` hace falta porque el XOR32 sigue puesto después de descifrar);
  - `ServerSessionTest.LosingTheGameServerStopsTheKeepalive` (`PeerClosed` en `LoggedIn`:
    `GameServerLost` con `PeerClosed` en el log, y en los 40 s siguientes no sale nada).
- [ ] **Paso 3:** `IntervalTimer`, el keepalive y la desconexión en la sesión, y `main.cpp`; 07.
- [ ] **Paso 4 (Emanuel, a mano, contra el laboratorio):** con la VM del laboratorio levantada,
  `Main connect /u<ip del CS> /p44405 --data <Data del laboratorio>` y las variables de la
  pregunta 7: el log dice cada estado hasta el login aceptado y un keepalive cada 20 s durante dos
  minutos, y el GS no corta. La clave se carga con `read -rs` o `Read-Host -MaskInput`. Si se
  puede capturar el cable, el login y el keepalive descifrados se comparan con los de la sesión
  real del 04 (P-C05), sin imprimir la cuenta. La captura queda fuera de Git (se descifra con las
  claves reales), y el README anota solo el resultado.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "IntervalTimer|ServerSession|Client.Main"
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60
  ```

---

## Qué no se prueba en CI, y cómo se cubre

| Qué | Cómo |
| --- | --- |
| el socket, la cola, el bucle con la red, el dispatcher, la conexión y la sesión | los tests de `network/`, `protocol/`, `app/`, `core/` y `support/`, con `FakeSocket` y `FakeClock`: corren en los dos sistemas |
| sockets de verdad | `SystemSocketTest` y `ServerSessionLoopbackTest` en `127.0.0.1`, en los dos sistemas (CI no tiene red de afuera, y no le hace falta) |
| la resolución de un nombre real | a mano, `nombre.invalid` incluido; en CI, un `FakeResolver`, porque el DNS puede tardar o reescribir un NXDOMAIN |
| el login contra el GS del laboratorio, con FoxAnti | a mano (CL4.5, paso 4): CI no tiene la VM. Es el "terminada cuando" del roadmap |
| el diferencial campo por campo del login y del keepalive (P-C05) | con una captura nueva del cable, si se hace en el paso 4 de la CL4.5, fuera de Git; si no, sigue en 08 |
| arrastrar la ventana en Windows | sin probar: el original sigue con `WM_TIMER` y MuRet no consulta la red mientras tanto (07, Q-C49) |
| qué pasa con el GS si el cliente se congela más de lo que él espera | sin probar: el timeout del GS no está en el research del cliente |

## Al cerrar la CL4

- [ ] Completar el cierre de la guía de la CL4.0.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Escribir el plan de la CL5 (render 2D y la escena de login) en su propio PR, con lo
  aprendido: la `ServerSession` es lo que la `LoginScene` de la CL5.4 y la UI de la CL5.5 manejan
  (la lista con P-C04, la cuenta y la clave del campo de texto en vez de las variables de la
  pregunta 7, y el aviso de una desconexión).
