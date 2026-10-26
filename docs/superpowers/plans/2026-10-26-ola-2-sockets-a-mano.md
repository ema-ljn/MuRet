# Plan de la ola 2: Sockets a mano

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** el servidor escucha en el puerto 55960, acepta conexiones y corta lo que llega en
frames con el `FrameParser` de la ola 1. Primero de la forma más simple (bloqueante), para ver
con los propios ojos por qué el original usa IOCP.

**Arquitectura:** una capa mínima de portabilidad (Winsock en Windows, sockets BSD en Linux)
envuelta en RAII; un servidor bloqueante; después un hilo por conexión; y una tabla de 100
sesiones con identificador y generación. Nada de lógica de juego todavía: cada frame se loguea.

**Stack:** C++20, Winsock 2 / sockets POSIX, `std::thread`, `std::jthread`, GoogleTest.

**Guía de estudio:** [`docs/learning/ola-2-sockets.md`](../../learning/ola-2-sockets.md): leé su sección antes de cada
sub-ola; la sección final la completás al cerrar la ola.

**Research que fija esta ola:** [02-arranque](../../../Server/DataServer/docs/research/02-arranque.md)
(línea de comandos, puerto), [04-red](../../../Server/DataServer/docs/research/04-red.md) (tabla de
conexiones, límites) y C-4/C-5 de
[03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#defectos-de-concurrencia).

**Terminada cuando:** un cliente de prueba manda paquetes y el log del servidor los muestra; la
conexión 101 se rechaza; un slot reusado no recibe nada de la conexión anterior; todo en CI.

## Preguntas para decidir al empezar

1. **Formato del archivo de config** (pregunta abierta del roadmap): TOML o JSON. Se decide acá y
   queda en el ADR 0003.
2. **Logger propio o externo.** El original loguea sin lock (C-5). ¿Escribís uno chico con un
   `std::mutex`, o usás spdlog por vcpkg?
3. **Qué hace la conexión 101.** El original la acepta y la cierra al toque (`Error : Max User
   Index`). ¿Igual, o no la aceptás?

---

## Sub-ola 2.1: portabilidad Winsock/BSD

**Concepto:** qué es un socket, `WSAStartup`/`WSACleanup`, `SOCKET` vs `int`, `closesocket` vs
`close`, `WSAGetLastError` vs `errno`, y por qué todo eso va detrás de una clase RAII que cierra
sola.

**Archivos:**
- Crear: `Server/DataServer/src/Network/Platform.h`, `Platform.cpp` (inicialización de la red, tipo de socket nativo, último error)
- Crear: `Server/DataServer/src/Network/Socket.h`, `Socket.cpp` (dueño único: se mueve, no se copia, cierra en el destructor)
- Crear: `Server/DataServer/src/Core/Log.h`, `Log.cpp` (logger seguro entre hilos)
- Crear: `Server/DataServer/tests/network/SocketTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests de `Socket`: un socket movido deja al original vacío; el destructor cierra; abrir, `bind` a `127.0.0.1:0` y leer el puerto que eligió el sistema.
- [ ] **Paso 2 (Emanuel):** `Platform` con `#ifdef _WIN32` en un solo lugar; el resto del código no ve `#ifdef`.
- [ ] **Paso 3 (Emanuel):** `Socket` RAII.
- [ ] **Paso 4 (Emanuel):** `Log` con niveles y un `std::mutex`; test que loguea desde 8 hilos y no mezcla líneas.
- [ ] **Paso 5:** verificar en CI (Windows y Linux).

## Sub-ola 2.2: servidor TCP bloqueante de un cliente

**Concepto:** `listen`/`accept`/`recv`, TCP como flujo (un `recv` puede traer medio paquete o tres),
argumentos de línea de comandos validados y el archivo de config.

**Archivos:**
- Crear: `Server/DataServer/src/Core/Config.h`, `Config.cpp`, `docs/adr/0003-formato-de-config.md`
- Crear: `Server/DataServer/src/Network/BlockingServer.h`, `BlockingServer.cpp`
- Crear: `tools/fake-gs/` (cliente de prueba que manda frames; andamiaje de Claude)
- Crear: `Server/DataServer/tests/integration/BlockingServerTests.cpp`
- Modificar: `Server/DataServer/src/main.cpp`

- [ ] **Paso 1 (Emanuel):** ADR 0003 con la decisión de formato de config.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** parseo de argumentos con tests: puerto válido, fuera de rango (`70000`), vacío (default 55960), basura. El original trunca a 16 bits sin avisar ([02 § línea de comandos](../../../Server/DataServer/docs/research/02-arranque.md#línea-de-comandos)); MuRet rechaza.
- [ ] **Paso 3 (Emanuel):** `BlockingServer`: acepta **un** cliente, lee en bucle, alimenta el `FrameParser`, loguea cada frame (head y tamaño) y cierra si el parser da error.
- [ ] **Paso 4 (Claude):** `tools/fake-gs` que se conecta y manda una lista de frames, con opción de partirlos en trozos.
- [ ] **Paso 5:** test de integración en loopback con puerto 0: el cliente manda 3 frames partidos y el servidor reporta 3.

## Sub-ola 2.3: un hilo por conexión, y por qué no escala

**Concepto:** `std::thread` por cliente, cuánto cuesta un hilo (pila, cambio de contexto), y
medirlo en vez de creerlo.

**Archivos:**
- Modificar: `Server/DataServer/src/Network/BlockingServer.cpp` (un hilo por conexión aceptada)
- Crear: `docs/learning/ola-2.3-medicion.md` (resultados de la medición)

- [ ] **Paso 1 (Emanuel):** cada `accept` lanza un hilo con su propio `FrameParser`.
- [ ] **Paso 2 (Emanuel):** apagar bien: al cerrar, el hilo de accept deja de aceptar y los hilos de cliente terminan (pista: cerrar el socket desbloquea el `recv`).
- [ ] **Paso 3 (Emanuel, ejercicio):** con `fake-gs` abrir 10, 100 y 1000 conexiones y anotar memoria y hilos del proceso. Escribir en la nota qué viste y qué tiene que ver con el IOCP del original ([03 § inventario de hilos](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#inventario-de-hilos)).

## Sub-ola 2.4: tabla de 100 sesiones

**Concepto:** identificadores con generación (un índice reusado no es la misma sesión), y por qué
el original cierra la conexión equivocada (C-4).

**Archivos:**
- Crear: `Server/DataServer/src/Network/SessionTable.h`, `SessionTable.cpp`
- Crear: `Server/DataServer/tests/network/SessionTableTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests: asignar 100 sesiones; la 101 falla; liberar la 7 y volver a asignar devuelve índice 7 con generación nueva; un id viejo (7, generación anterior) ya no encuentra nada.
- [ ] **Paso 2 (Emanuel):** `SessionTable` con `SessionId{index, generation}`; el estado de cada slot (libre, conectado, GameServer) como `enum class`, igual que el `active` 0/1/2 del original ([04 § tabla](../../../Server/DataServer/docs/research/04-red.md#la-tabla-de-conexiones)).
- [ ] **Paso 3 (Emanuel):** integrarla al servidor: la conexión 101 se trata según lo que decidiste.
- [ ] **Paso 4:** test de integración: conectar, desconectar, reconectar en el mismo slot y comprobar que no llega nada de la conexión anterior.

---

## Al cerrar la ola 2

- [ ] Completar la sección de cierre de la [guía de la ola 2](../../learning/ola-2-sockets.md).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la ola 3.
