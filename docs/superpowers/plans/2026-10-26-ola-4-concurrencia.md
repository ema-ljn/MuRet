# Plan de la ola 4: Concurrencia

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** separar la red de la lógica como hace el original (muchos hilos reciben, uno solo
ejecuta), pero sin su lock global, sin `Sleep(1)` y sin perder mensajes cuando la cola se llena.

**Arquitectura:** los hilos de I/O de la ola 3 cortan frames y los meten en una `MessageQueue`
acotada. Un único **hilo de lógica** los saca y los pasa a un `Dispatcher` que llama al handler
según el head. El hilo de lógica es dueño de todo el estado del servidor, así que los handlers no
necesitan locks.

**Stack:** C++20 (`std::mutex`, `std::condition_variable`, `std::jthread`, `std::stop_token`),
ThreadSanitizer.

**Guía de estudio:** [`docs/learning/ola-4-concurrencia.md`](../../learning/ola-4-concurrencia.md): leé su sección antes de cada
sub-ola; la sección final la completás al cerrar la ola.

**Research que fija esta ola:** [03-hilos-y-concurrencia](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md):
el recorrido de un mensaje, la cola (tope 5000, descarte silencioso y fuga), throughput y
latencia, y los invariantes I-1, I-2 e I-3.

**Terminada cuando:** el test de estrés pasa con ThreadSanitizer limpio y verifica los tres
invariantes; la política de cola llena está testeada.

## Preguntas para decidir al empezar

1. **Cola llena.** El original descarta en silencio y pierde memoria. Opciones: dejar de leer de
   esa conexión hasta que haya lugar (*backpressure*), cerrar la conexión que inunda, o rechazar
   con log. ¿Cuál, y qué ve el GameServer en cada caso?
2. **Tope de la cola.** ¿5000 como el original, o un número que salga de una cuenta? Pista: la
   sección de throughput del research tiene la fórmula.
3. **¿Orden total o solo por conexión?** El original da orden total (I-2) porque tiene un solo
   consumidor. ¿Algún head lo necesita? (Ejemplo: dos GameServers guardando el mismo personaje.)

---

## Sub-ola 4.1: `MessageQueue` acotada

**Concepto:** productor/consumidor, `std::mutex` + `std::condition_variable`, despertar falso
(*spurious wakeup*), y cómo se apaga una cola sin dejar hilos colgados.

**Archivos:**
- Crear: `Server/DataServer/src/Dispatch/MessageQueue.h`, `MessageQueue.cpp`
- Crear: `Server/DataServer/tests/dispatch/MessageQueueTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests: orden FIFO; con la cola llena se aplica la política elegida; `Pop` bloquea hasta que llega algo; `Stop` despierta a quien espera y `Pop` devuelve "terminado"; sin fugas cuando se llena (lo opuesto del original).
- [ ] **Paso 2 (Emanuel):** implementar con `std::deque` + `mutex` + `condition_variable`. El mensaje lleva `SessionId`, head y los bytes del frame.
- [ ] **Paso 3:** verificar con `ctest -R MessageQueue`.

## Sub-ola 4.2: hilo de lógica y `Dispatcher`

**Concepto:** un hilo dueño del estado (no hace falta lock si un solo hilo toca los datos), tabla
`head → handler`, y drenar todo lo disponible en cada vuelta en vez de un mensaje por `Sleep(1)`.

**Archivos:**
- Crear: `Server/DataServer/src/Dispatch/Dispatcher.h`, `Dispatcher.cpp`, `LogicThread.h`, `LogicThread.cpp`
- Crear: `Server/DataServer/tests/dispatch/DispatcherTests.cpp`
- Modificar: `Server/DataServer/src/main.cpp` (red → cola → hilo de lógica)

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests del `Dispatcher`: un head registrado llama a su handler con el `SessionId` y el payload; un head sin handler se ignora y se loguea (el original lo ignora en silencio).
- [ ] **Paso 2 (Emanuel):** `LogicThread` con `std::jthread`: `Pop` bloqueante y despacho. Los handlers responden por `IoBackend::Send`, que encola en la `SendQueue` de la sesión (ola 3).
- [ ] **Paso 3 (Emanuel):** handler de prueba que hace eco, para ver el camino completo.
- [ ] **Paso 4:** test de integración: un cliente manda 1000 frames y recibe 1000 ecos en orden.

## Sub-ola 4.3: estrés, backpressure y TSan

**Concepto:** *data race* vs condición de carrera; cómo ThreadSanitizer las encuentra; medir
latencia y throughput y compararlos con la cota del original.

**Archivos:**
- Crear: `Server/DataServer/tests/integration/StressTests.cpp`
- Crear: preset `linux-x64-tsan` y job de CI (andamiaje de Claude)
- Crear: `docs/learning/ola-4.3-throughput.md`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** test de estrés: 50 clientes × 1000 frames numerados. Verificar I-1 (cada cliente recibe sus respuestas en orden), I-2 (un contador global en el hilo de lógica ve exactamente 50 000 mensajes) e I-3 (un handler que marca "estoy corriendo" nunca se encuentra con otro).
- [ ] **Paso 2 (Emanuel):** test de backpressure: un cliente que inunda y no lee; el servidor se comporta según la política y los otros clientes siguen atendidos.
- [ ] **Paso 3 (Claude):** preset y job de CI con TSan.
- [ ] **Paso 4 (Emanuel, ejercicio):** medir mensajes por segundo y anotarlo al lado de la cota del original (~64 msg/s con el reloj por defecto, ver [03 § throughput](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#throughput-y-latencia)).

---

## Al cerrar la ola 4

- [ ] Completar la sección de cierre de la [guía de la ola 4](../../learning/ola-4-concurrencia.md).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la ola 5.
