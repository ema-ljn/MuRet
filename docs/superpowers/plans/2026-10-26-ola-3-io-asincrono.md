# Plan de la ola 3: I/O asíncrono como el original

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** reemplazar "un hilo por conexión" por el modelo del original: pocos hilos que
atienden muchas conexiones con I/O asíncrono. IOCP en Windows, epoll en Linux, los dos detrás de
la misma interfaz. Y esta vez sin los bugs de vida de los contextos que tiene el original.

**Arquitectura:** interfaz `Network::IoBackend` con dos implementaciones (`IocpBackend`,
`EpollBackend`). La interfaz entrega eventos con estilo de completion ("llegaron estos bytes de
esta sesión", "se cerró esta sesión"), así el resto del servidor no sabe qué backend corre. Cada
sesión tiene una cola de envío propia y un solo envío en vuelo.

**Stack:** C++20, Win32 IOCP (`CreateIoCompletionPort`, `WSARecv`/`WSASend`,
`GetQueuedCompletionStatus`), Linux epoll (`epoll_ctl`, `epoll_wait`, `eventfd`), sockets no
bloqueantes, AddressSanitizer.

**Guía de estudio:** [`docs/learning/ola-3-io-asincrono.md`](../../learning/ola-3-io-asincrono.md): leé su sección antes de cada
sub-ola; la sección final la completás al cerrar la ola.

**Research que fija esta ola:** [04-red](../../../Server/DataServer/docs/research/04-red.md)
(contexto por conexión, recepción, envío) y C-2, C-3, C-4 de
[03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#defectos-de-concurrencia).

**Terminada cuando:** 100 clientes simultáneos mandan y reciben sin crash ni fugas (ASan/LSan
limpios) en Windows y Linux, y pasan los tests de cierre con operaciones pendientes.

## Preguntas para decidir al empezar

1. **Accept.** El original acepta en un hilo bloqueante y asocia cada socket al IOCP. ¿Copiás eso
   o usás `AcceptEx`? Sugerencia: primero como el original, `AcceptEx` como ejercicio extra.
2. **Cuántos workers.** El original usa CPUs × 2. ¿Por qué ese número y no CPUs?
3. **Level-triggered o edge-triggered** en epoll. Cuál es más fácil de hacer bien y cuál cuesta menos
   llamadas al sistema.
4. **Cómo se libera una sesión.** El original cuenta operaciones a mano y se equivoca (C-2).
   ¿Contador por operación posteada, o `shared_ptr` que cada operación en vuelo sostiene?

---

## Sub-ola 3.1: IOCP en Windows

**Concepto:** modelo *proactor*: pedís la operación y el kernel te avisa cuando terminó.
`OVERLAPPED` como identificador de una operación en curso, completion key, por qué el buffer y el
`OVERLAPPED` tienen que vivir hasta la completion.

**Archivos:**
- Crear: `Server/DataServer/src/Network/IoBackend.h` (interfaz común)
- Crear: `Server/DataServer/src/Network/Iocp/IocpBackend.h`, `IocpBackend.cpp`, `IocpSession.h`, `IocpSession.cpp`
- Crear: `Server/DataServer/tests/network/IoBackendTests.cpp` (los mismos tests para los dos backends)

- [ ] **Paso 1 (Emanuel):** diseñar `IoBackend`: arrancar en un puerto, apagar, `Send(SessionId, bytes)`, y callbacks o una cola de eventos para "bytes recibidos" y "sesión cerrada".
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** tests contra la interfaz (sirven para los dos backends): eco de un frame; 100 clientes a la vez; un cliente que corta con RST; un cliente que corta limpio.
- [ ] **Paso 3 (Emanuel):** `IocpBackend`: hilo de accept, N workers con `GetQueuedCompletionStatus`, un `WSARecv` en vuelo por sesión, re-postear después de entregar los bytes.
- [ ] **Paso 4 (Emanuel):** la vida de la sesión: se libera **solo** cuando no queda ninguna operación en vuelo, contando **cada** operación posteada (arregla C-2). Atención al caso `GetQueuedCompletionStatus` falla con `lpOverlapped == NULL`: no se desencoló nada y la key no vale.
- [ ] **Paso 5:** verificar en Windows con el preset de debug y en CI.

## Sub-ola 3.2: epoll en Linux

**Concepto:** modelo *reactor*: el kernel te avisa que **podés** leer o escribir, y la operación la
hacés vos. Cómo se adapta un reactor a una interfaz de estilo completion.

**Archivos:**
- Crear: `Server/DataServer/src/Network/Epoll/EpollBackend.h`, `EpollBackend.cpp`, `EpollSession.h`, `EpollSession.cpp`
- Modificar: `Server/DataServer/CMakeLists.txt` (cada backend compila solo en su sistema)

- [ ] **Paso 1 (Emanuel):** `EpollBackend` con sockets no bloqueantes, un hilo de accept y N hilos con `epoll_wait`.
- [ ] **Paso 2 (Emanuel):** `eventfd` para despertar a los hilos al apagar.
- [ ] **Paso 3:** los tests de `IoBackendTests.cpp` pasan **sin cambios** en Linux.

## Sub-ola 3.3: cola de envío por sesión y cierre ordenado

**Concepto:** por qué el original manda desde la pila del handler y reusa el `OVERLAPPED` (C-3);
cola de envío con buffers propios; envíos parciales; apagado ordenado.

**Archivos:**
- Crear: `Server/DataServer/src/Network/SendQueue.h`, `SendQueue.cpp`
- Crear: `Server/DataServer/tests/network/SendQueueTests.cpp`, `tests/network/ShutdownTests.cpp`
- Crear: `cmake/Sanitizers.cmake` y presets `linux-x64-asan` (andamiaje de Claude)

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests de `SendQueue`: encolar 3 mensajes y que salgan en orden; un envío parcial continúa donde quedó; nunca hay dos envíos en vuelo para la misma sesión.
- [ ] **Paso 2 (Emanuel):** `SendQueue` dueña de sus buffers (copia o mueve el mensaje al encolar).
- [ ] **Paso 3 (Emanuel):** apagado: dejar de aceptar, cerrar las sesiones, esperar a que no queden operaciones en vuelo, despertar y terminar los workers (`PostQueuedCompletionStatus` con key nula en Windows, `eventfd` en Linux).
- [ ] **Paso 4 (Emanuel):** tests de C-4: una completion vieja de una sesión cerrada no toca al slot reusado (el `SessionId` con generación de la ola 2 lo resuelve).
- [ ] **Paso 5 (Claude):** preset con ASan/LSan y job en CI.
- [ ] **Paso 6:** 100 clientes simultáneos, envío y recepción, cierre limpio y con RST: ASan y LSan limpios.

## Sub-ola 3.4 (opcional): el mismo servidor con Asio

**Concepto:** qué hace una biblioteca de red por dentro, comparando con lo que escribiste.

- [ ] **Paso 1 (Emanuel):** un `AsioBackend` que implemente `IoBackend` con `asio::io_context` (vcpkg `asio`, sin Boost).
- [ ] **Paso 2:** los mismos tests de `IoBackendTests.cpp`.
- [ ] **Paso 3:** agregar a la sección de cierre de la [guía de la ola 3](../../learning/ola-3-io-asincrono.md) la comparación: líneas de código, bugs que tuviste y rendimiento.

---

## Al cerrar la ola 3

- [ ] Completar la sección de cierre de la [guía de la ola 3](../../learning/ola-3-io-asincrono.md) (proactor vs reactor, vida de los buffers).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la ola 4.
