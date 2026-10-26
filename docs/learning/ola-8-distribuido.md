# Guía de la ola 8: Sistema distribuido

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones (dónde corre MuRet, contra qué se compara, qué es idempotente en cada head) son las
> "preguntas para decidir" del
> [plan de la ola 8](../superpowers/plans/2026-10-26-ola-8-distribuido.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 8.1 | el laboratorio: host, VM y red | cómo se diagnostica "no conecta", capa por capa |
| 8.2 | tests *golden* y paridad | qué se captura, qué se normaliza y cómo se declara una diferencia esperada |
| 8.3 | fallas parciales | qué es un timeout, un reintento y una operación idempotente, y por qué un reintento puede duplicar |
| 8.4 | apagado, métricas y health checks | qué se puede hacer dentro de un manejador de señal, y qué mide un servidor sano |

---

## 8.1 El laboratorio: host, VM y red

### La idea

- MuRet corre en el **host** y el GameServer en la **VM2003**: un binario de VS 2022 no corre en
  Windows Server 2003. Son dos máquinas en una red, aunque estén en la misma computadora.
- La red de la VM puede ser **NAT** (la VM sale, pero nadie le entra fácil), **solo host** (host y
  VM se ven entre ellos) o **puente** (la VM es una máquina más de tu red). Para que la VM llegue al
  host, la que elijas tiene que permitirlo.
- Diagnóstico **capa por capa**, de abajo hacia arriba:

  ```text
  1. ¿Se ven las IPs?            ping
  2. ¿Llego al puerto?           Test-NetConnection <ip> -Port <p> (Windows moderno) / nc -vz <ip> <p>
  3. ¿Alguien escucha ahí?       netstat -ano (Windows) / ss -ltnp (Linux)
  4. ¿Hablan el mismo protocolo? Wireshark con el filtro  tcp.port == <p>
  ```

  Si falla un paso, no tiene sentido mirar el siguiente.
- **Firewall:** el del host tiene que dejar entrar al puerto de MuRet; el de la VM, salir.

### Ejemplo mínimo (fuera del proyecto)

Probar la red con algo que no es tu programa. Por ejemplo, un servidor de archivos de Python en el
host y un navegador o `telnet` en la VM:

```text
host:  python -m http.server 8000 --bind 0.0.0.0
VM:    telnet <ip-del-host> 8000     → si conecta, la red y el firewall están bien
```

Si eso anda y MuRet no, el problema es MuRet (o el puerto que eligió).

### Errores típicos

- MuRet escuchando en `127.0.0.1`: solo acepta conexiones de la misma máquina.
- Usar la IP de la placa equivocada del host (la del adaptador virtual no es la de tu Wi-Fi).
- Cambiar tres cosas a la vez y no saber cuál lo arregló.

### Cómo lo hacía el original

En el laboratorio todo corre dentro de la VM, en loopback. El GameServer recibe la IP del
DataServer por línea de comandos; que acepte una IP remota es *inferido* y se confirma en esta
sub-ola (pregunta 1 del plan).

### Para leer

- La documentación de tu hipervisor sobre los modos de red.
- Wireshark User's Guide: "Capture Filters" y "Display Filters"; "Follow TCP Stream".

### Preguntas para chequearte

1. `ping` anda pero el GameServer no conecta. ¿Qué mirás después?
2. ¿Por qué MuRet tiene que escuchar en `0.0.0.0` (o en la IP del adaptador de la VM) y no en
   `127.0.0.1`?
3. ¿Desde dónde capturás con Wireshark para ver el tráfico GS↔DS: el host o la VM? ¿Por qué?

---

## 8.2 Tests *golden* y paridad

### La idea

- Un test **golden** guarda una salida de referencia (capturada una vez) y compara contra ella
  siempre. Acá la referencia es lo que respondió el DataServer original a una sesión real.
- Lo que hay que resolver:
  - **Reconstruir el flujo TCP:** una captura tiene segmentos, no frames. Hay que reensamblar y
    volver a cortar (tu `FrameParser` de la ola 1 sirve para eso).
  - **Normalizar** lo que cambia en cada corrida: seriales, índices de usuario, fechas. Esos campos
    se comparan aparte o se excluyen, con una razón escrita.
  - **El estado de la base**, no solo los bytes: volcar las tablas tocadas antes y después.
  - **Diferencias esperadas:** cada una se declara con el ADR que la justifica. Una diferencia sin
    ADR es un bug.
- El oráculo puede ser el binario original o la reconstrucción `mfc-vc6/`, y **no son iguales**.

### Ejemplo mínimo (fuera del proyecto)

Un test golden de la salida de un programa de línea de comandos:

```text
tests/golden/help.txt         ← la salida aprobada, versionada
test:  correr "tool --help" → comparar con help.txt → si difiere, mostrar el diff
actualizar: correr con --update-golden SOLO después de revisar el diff a mano
```

### Errores típicos

- Regenerar los golden sin mirar qué cambió ("el test falló, lo actualicé y pasó").
- Capturas sin anotar qué se hizo en el juego mientras se capturaba.
- Comparar contra la reconstrucción creyendo que es el binario.

### Cómo lo hacía el original

No tiene tests. Lo que sí hay es la lista de huecos que estas capturas cierran
([10-pendientes](../../Server/DataServer/docs/research/10-pendientes.md)) y las diferencias entre la
reconstrucción y el binario
([09-discrepancias](../../Server/DataServer/docs/research/09-discrepancias.md)).

### Para leer

- Wireshark: "Follow TCP Stream" y la exportación de bytes crudos; `tshark` para automatizar.
- Cualquier artículo sobre *snapshot testing* o *approval testing*.

### Preguntas para chequearte

1. La respuesta de MuRet a un `0x52` difiere de la captura solo en el serial. ¿Es un bug?
2. ¿Por qué comparar solo los bytes del wire no alcanza para saber si `0x07` anduvo?
3. Encontraste una diferencia que el research no explica. ¿Qué hacés primero?

---

## 8.3 Fallas parciales

### La idea

- En un sistema distribuido **una parte falla y las demás siguen**: el GameServer se cae a mitad
  de un guardado, la base deja de responder, la red pierde un paquete. No hay un "todo anduvo" o
  "todo falló".
- **Timeout:** después de cuánto tiempo dejás de esperar. Sin timeout, una espera puede ser para
  siempre. Hay de conexión, de consulta y de sesión inactiva.
- **Reintento:** volver a intentar algo que falló, con espera creciente (*backoff* exponencial) y
  un poco de azar (*jitter*) para que no reintenten todos a la vez.
- **El problema:** cuando hay timeout, **no sabés si se hizo o no**. Si reintentás, puede hacerse
  dos veces.
- **Idempotente:** hacerlo dos veces deja el mismo estado que hacerlo una. "Fijar el saldo en 500"
  es idempotente; "sumar 100 al saldo" no.
- **Inyectar fallas:** un envoltorio que implementa la misma interfaz que el repositorio real y
  falla o demora a pedido. Es el patrón *decorator*, y es lo que hace posibles los tests de esta
  sub-ola.

### Ejemplo mínimo (fuera del proyecto)

Un decorador que falla cada N llamadas:

```cpp
#include <string_view>

class Storage {
public:
    virtual ~Storage() = default;
    virtual bool put(std::string_view key, std::string_view value) = 0;
};

class FlakyStorage final : public Storage {
public:
    FlakyStorage(Storage& inner, int fail_every) : inner_(inner), fail_every_(fail_every) {}
    bool put(std::string_view key, std::string_view value) override {
        if (++calls_ % fail_every_ == 0) return false;    // falla inyectada
        return inner_.put(key, value);
    }
private:
    Storage& inner_;
    int fail_every_;
    int calls_ = 0;
};
```

### Errores típicos

- Reintentar algo que no es idempotente sin pensarlo.
- Reintentos sin tope ni espera (un servidor caído recibe una avalancha cuando vuelve).
- Timeouts tan cortos que fallan en un día normal, o tan largos que no sirven.
- Probar fallas solo "a mano", desenchufando cosas.

### Cómo lo hacía el original

Ante `08S01` reconecta con el lock global tomado: mientras tanto **todo** el servidor espera
([06 § `CQuery`](../../Server/DataServer/docs/research/06-sql-y-base.md#cquery-la-capa-odbc)). No
tiene timeouts propios de consulta ni de sesión.

### Para leer

- "Timeouts, retries, and backoff with jitter" (Amazon Builders' Library).
- Cualquier explicación de entrega *at-most-once*, *at-least-once* y *exactly-once*.

### Preguntas para chequearte

1. El GameServer manda `0x07` (guardar), no recibe nada y lo vuelve a mandar. ¿Es un problema?
2. ¿Y si manda dos veces `0x04` (crear)?
3. Una consulta tarda 5 s. ¿Qué les pasa a los otros GameServers en el original, y qué en MuRet?

---

## 8.4 Apagado, métricas y health checks

### La idea

- **Señales:** Ctrl+C manda `SIGINT`; un administrador de servicios manda `SIGTERM`. Dentro de un
  manejador de señal casi nada es seguro (ni `printf`, ni `malloc`, ni tomar un mutex): lo único
  que se hace es **marcar** (un `std::atomic<bool>` libre de locks) o despertar a alguien (escribir
  en un `eventfd`). El apagado de verdad lo hace el hilo principal. En Windows, la consola avisa con
  `SetConsoleCtrlHandler`, que además corre en un hilo aparte.
- **Apagado ordenado:** es la secuencia de la ola 3, más la cola de la ola 4: dejar de aceptar,
  **procesar lo que quedó en la cola**, esperar a que salgan las respuestas, cerrar.
- **Métricas:** *contadores* (cuántos errores de SQL hubo), *medidores* (cuántas conexiones hay
  ahora, largo de la cola) e *histogramas* (latencia por head, para ver p50 y p99).
- **Health check:** una forma barata de preguntar "¿estás vivo?" desde afuera. Se distingue
  "el proceso vive" (*liveness*) de "puede atender" (*readiness*: por ejemplo, la base responde).

### Ejemplo mínimo (fuera del proyecto)

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

std::atomic<bool> stop_requested{false};
extern "C" void on_signal(int) { stop_requested.store(true); }   // nada más: ni log, ni malloc

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (!stop_requested.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::puts("apagando en orden...");                          // acá sí: estamos fuera del manejador
}
```

### Errores típicos

- Loguear o liberar memoria adentro del manejador de señal.
- Apagar sin procesar la cola: es exactamente Q-09.
- Un health check que acepta conexiones y no las cierra: es exactamente Q-10.
- Métricas que nadie mira: volcalas al log con un formato que se pueda buscar.

### Cómo lo hacía el original

- Al cerrar la ventana libera los slots sin cerrar los sockets y termina; lo que estaba en la cola
  se pierde ([02 § apagado](../../Server/DataServer/docs/research/02-arranque.md#apagado),
  [Q-09](../../Server/DataServer/docs/research/08-quirks-y-bugs.md#q-09-el-cierre-desde-la-ui-no-cierra-los-sockets-ni-drena-la-cola)).
- El segundo listener (el "port check") acepta y **no cierra**: un socket perdido por cada sonda
  ([Q-10](../../Server/DataServer/docs/research/08-quirks-y-bugs.md#q-10-el-port-check-filtra-un-socket-por-sonda),
  [04 § dos sockets de escucha](../../Server/DataServer/docs/research/04-red.md#dos-sockets-de-escucha)).

### Para leer

- `man 7 signal-safety` (la lista de lo que se puede llamar en un manejador).
- cppreference: `std::signal`, `std::atomic<T>::is_always_lock_free`.
- Microsoft Learn: `SetConsoleCtrlHandler`.

### Preguntas para chequearte

1. ¿Por qué no se puede llamar a tu logger (que toma un mutex) desde el manejador de señal?
2. Con 30 mensajes en la cola, llega `SIGTERM`. ¿Qué pasa con cada uno?
3. ¿Qué diferencia hay entre "el puerto P+1 acepta conexiones" y "MuRet puede atender un login"?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Cómo quedó el laboratorio** (resumen; el detalle va en `ola-8.1-gs-real.md`):
- **Decisiones que tomé (y por qué):** dónde corre MuRet · contra qué se compara · qué heads son
  idempotentes
- **Preguntas que me quedaron para más adelante:**

## Retrospectiva del port (lo completás vos)

- **Qué sé hacer ahora que no sabía al empezar:**
- **Qué haría distinto si empezara de nuevo:**
- **Qué componente sigue** (ConnectServer, JoinServer…) **y por qué:**
