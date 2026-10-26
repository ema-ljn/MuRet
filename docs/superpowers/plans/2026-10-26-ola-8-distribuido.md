# Plan de la ola 8: Sistema distribuido

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** que un GameServer 1.02 real no pueda distinguir a MuRet del original, y que MuRet
aguante lo que pasa en un sistema distribuido de verdad: desconexiones, base caída, mensajes
duplicados, apagados.

**Arquitectura:** MuRet corre en el host (Linux o un Windows moderno) y el GameServer en la
VM2003 se conecta por la red. Una herramienta `packet-replay` reproduce capturas del original
contra MuRet y compara las respuestas byte a byte. Un repositorio "con fallas" envuelve al real
para inyectar errores y demoras en los tests.

**Stack:** C++20, VM2003 con GameServer 1.02, Wireshark, `tools/packet-replay`.

**Research que fija esta ola:** [02-arranque](../../../Server/DataServer/docs/research/02-arranque.md)
(apagado), [08-quirks-y-bugs](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md) (Q-09,
Q-10), [09-discrepancias](../../../Server/DataServer/docs/research/09-discrepancias.md) (el oráculo
no es el binario) y [10-pendientes](../../../Server/DataServer/docs/research/10-pendientes.md).

**Terminada cuando:** un GS 1.02 entra, crea un personaje y juega contra MuRet; la paridad
byte a byte pasa en todas las capturas; los tests de fallas pasan.

## Preguntas para decidir al empezar

1. **Dónde corre MuRet.** Un binario C++20 compilado con VS 2022 **no corre en Windows Server
   2003** (el último toolset para XP/2003 fue el de VS 2017). La idea es correr MuRet en el host y
   apuntar el GS al IP del host: el GS recibe el IP del DataServer por línea de comandos
   (`gameserver.exe 127.0.0.1 55970 <ip-del-ds> 55960 55901`). *Inferido* que acepta un IP remoto;
   se confirma en la 8.1.
2. **Contra qué se compara.** ¿Contra el binario original, contra la reconstrucción `mfc-vc6/`, o
   contra los dos? La reconstrucción "arregla" algunas cosas (D-2, D-3 de
   [09](../../../Server/DataServer/docs/research/09-discrepancias.md)).
3. **Qué es "idempotente" para cada head.** ¿Qué pasa si el GS manda dos veces el mismo `0x07`
   (guardar)? ¿Y dos `0x04` (crear)?

---

## Sub-ola 8.1: el GameServer real contra MuRet

**Concepto:** red entre host y VM, firewall, y la primera prueba de punta a punta con el cliente
real.

**Archivos:**
- Crear: `docs/learning/ola-8.1-gs-real.md` (cómo se arma el laboratorio)

- [ ] **Paso 1 (Emanuel):** levantar MuRet en el host con la base de la VM (ola 6) y apuntar el GS al host.
- [ ] **Paso 2 (Emanuel):** login del GS, entrar al juego con un cliente, crear un personaje, moverlo, guardar, salir y volver a entrar.
- [ ] **Paso 3 (Emanuel):** anotar cada diferencia que veas respecto del original y llevarla al research o a un issue.

## Sub-ola 8.2: `packet-replay` y paridad byte a byte

**Concepto:** tests *golden*: capturar una vez, reproducir siempre; comparar estado de la base
antes y después.

**Archivos:**
- Crear: `tools/packet-replay/` (lee una captura, manda los requests, compara las respuestas)
- Crear: `Server/DataServer/tests/data/golden/` (capturas versionadas)
- Crear: `Server/DataServer/tests/integration/ParityTests.cpp`

- [ ] **Paso 1 (Emanuel):** capturar con Wireshark el tráfico GS↔DS **original** (con el GS apuntando al DS por la red se ve desde el host) durante una sesión de juego típica; guardar también un volcado de las tablas tocadas.
- [ ] **Paso 2 (Emanuel):** `packet-replay`: extraer los frames del GS, mandarlos a un DataServer, guardar las respuestas.
- [ ] **Paso 3 (Emanuel, `TODO(human)`):** `ParityTests`: misma captura contra MuRet, respuestas iguales byte a byte, salvo los cambios documentados en ADR (cada diferencia esperada se declara con su ADR).
- [ ] **Paso 4:** cerrar los *seam* que queden en [10](../../../Server/DataServer/docs/research/10-pendientes.md).

## Sub-ola 8.3: timeouts, reconexión, idempotencia y fallas parciales

**Concepto:** qué falla en un sistema distribuido (red, base, el otro proceso), timeouts,
reintentos y por qué un reintento puede duplicar efectos.

**Archivos:**
- Crear: `Server/DataServer/src/Database/Faulty/` (envoltorio que falla o demora a pedido, solo para tests)
- Crear: `Server/DataServer/tests/integration/FailureTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests: el GS se desconecta a mitad de un guardado; la base se cae y vuelve; una consulta tarda 5 s (los otros GS siguen atendidos, a diferencia del original); llega dos veces el mismo `0x07`.
- [ ] **Paso 2 (Emanuel):** timeouts de consulta y de sesión; reconexión a la base con espera creciente.
- [ ] **Paso 3 (Emanuel):** documentar qué heads son idempotentes y cuáles no.

## Sub-ola 8.4: apagado limpio, métricas y health checks

**Concepto:** apagado ordenado (el original pierde los mensajes encolados, Q-09); observabilidad;
reemplazar el "port check" que filtra sockets (Q-10).

**Archivos:**
- Modificar: `Server/DataServer/src/main.cpp` (señales: `SIGINT`/`SIGTERM`, Ctrl+C en Windows)
- Crear: `Server/DataServer/src/Core/Metrics.h/.cpp`, `Network/HealthCheck.h/.cpp`
- Crear: `Server/DataServer/tests/integration/ShutdownTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** test: con mensajes en la cola, pedir apagado; todos se procesan y sus respuestas salen antes de cerrar.
- [ ] **Paso 2 (Emanuel):** métricas: conexiones, largo de la cola, latencia por head, errores de SQL; volcarlas al log cada N segundos.
- [ ] **Paso 3 (Emanuel):** health check en el puerto `P+1` (como el original) que responde y cierra, sin fugas.

---

## Al cerrar la ola 8

- [ ] Nota de aprendizaje `docs/learning/ola-8-distribuido.md`.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Retrospectiva: qué se aprendió en todo el port y qué componente sigue (ConnectServer, JoinServer…).
