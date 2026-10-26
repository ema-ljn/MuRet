# Guía de la ola 3: I/O asíncrono

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones (cómo aceptar, cuántos workers, level o edge-triggered, cómo se libera una sesión)
> son las "preguntas para decidir" del
> [plan de la ola 3](../superpowers/plans/2026-10-26-ola-3-io-asincrono.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 3.1 | IOCP, modelo *proactor* | qué es un `OVERLAPPED`, qué es la completion key, y por qué el buffer tiene que vivir hasta la completion |
| 3.2 | epoll, modelo *reactor* | la diferencia entre "terminó" y "podés", y qué cambia entre level y edge-triggered |
| 3.3 | cola de envío y cierre ordenado | por qué un envío necesita un buffer propio, y en qué orden se apaga un servidor asíncrono |
| 3.4 | Asio (opcional) | qué te resuelve una biblioteca y qué no |

---

## 3.1 IOCP: el modelo *proactor*

### La idea

- En la ola 2 cada hilo esperaba **su** conexión. Con I/O asíncrono, **pocos hilos atienden
  muchas conexiones**: le pedís al sistema una operación y seguís; el sistema avisa cuando terminó.
- En Windows eso es un **I/O Completion Port** (IOCP). El ciclo:
  1. Creás el puerto (`CreateIoCompletionPort`) y le **asociás** cada socket con una *completion
     key* (un número tuyo; en la práctica, un puntero a tu sesión).
  2. **Posteás** la operación: `WSARecv(socket, buffer, …, &overlapped)`. Vuelve enseguida.
  3. El kernel llena tu buffer cuando llegan datos y deja un aviso en el puerto.
  4. Un worker bloqueado en `GetQueuedCompletionStatus` (GQCS) recibe `(bytes, key, OVERLAPPED*)`.
- **Key = qué conexión. `OVERLAPPED*` = qué operación.** Una sesión puede tener un recv y un send
  en vuelo a la vez; la key es la misma y el `OVERLAPPED` las distingue. El truco habitual es poner
  el `OVERLAPPED` dentro de un struct tuyo y recuperar el struct con `CONTAINING_RECORD`.
- **Regla de oro:** el buffer y el `OVERLAPPED` **le pertenecen al kernel** desde que posteás hasta
  que sale la completion. Si los liberás o los reusás antes, el kernel escribe en memoria que ya no
  es tuya.
- Los cuatro casos de GQCS:

  | Retorno | `lpOverlapped` | Qué pasó |
  | --- | --- | --- |
  | `TRUE` | no nulo, `bytes > 0` | la operación terminó bien |
  | `TRUE` | no nulo, `bytes == 0` en un recv | el otro lado cerró ordenadamente |
  | `FALSE` | **no nulo** | se desencoló una operación que **falló** (RST, cancelada): hay que manejarla |
  | `FALSE` | **nulo** | no se desencoló nada (timeout o error del puerto): la key no vale |

- El puerto tiene un límite de **concurrencia** (cuántos workers corren a la vez), que no es lo
  mismo que cuántos workers creás. Tenelo en cuenta cuando decidas cuántos workers crear.

### Ejemplo mínimo (fuera del proyecto)

Un IOCP sin sockets: se usa como cola de trabajos entre hilos con `PostQueuedCompletionStatus`.
Muestra la key, la recuperación del struct desde el `OVERLAPPED` y quién es dueño de qué.

```cpp
#include <windows.h>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

struct Job {
    OVERLAPPED ov{};     // el puerto devuelve este puntero
    int number = 0;
};

int main() {
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    std::vector<std::jthread> workers;
    for (int w = 0; w < 2; ++w)
        workers.emplace_back([port, w] {
            for (;;) {
                DWORD bytes = 0; ULONG_PTR key = 0; OVERLAPPED* ov = nullptr;
                BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &ov, INFINITE);
                if (!ok && ov == nullptr) return;   // no se desencoló nada
                if (key == 0) return;               // señal de "terminar" que mandamos nosotros
                std::unique_ptr<Job> job(CONTAINING_RECORD(ov, Job, ov));  // vuelve a tener dueño
                std::printf("worker %d: trabajo %d\n", w, job->number);
            }
        });
    for (int i = 1; i <= 5; ++i) {
        auto job = std::make_unique<Job>();
        job->number = i;
        PostQueuedCompletionStatus(port, 0, 42, &job.release()->ov);  // el puerto lo tiene ahora
    }
    for (int w = 0; w < 2; ++w) PostQueuedCompletionStatus(port, 0, 0, nullptr);
    workers.clear();     // join
    CloseHandle(port);
}
```

Fijate el `release()`: mientras el trabajo está en el puerto, nadie en tu código es dueño de él.
Con un `WSARecv` pasa lo mismo, solo que el que lo tiene es el kernel.

### Errores típicos

- El buffer del recv en la pila de una función que ya retornó.
- Tratar `FALSE` como "no pasó nada" y perder una operación que falló (y con ella, la cuenta de
  operaciones en vuelo).
- Usar la key cuando `lpOverlapped` es nulo.
- Liberar la sesión cuando "se cerró", sin esperar a que salgan las completions que faltan.

### Cómo lo hacía el original

Un hilo de accept bloqueante, `CPUs × 2` workers y **un solo `WSARecv` en vuelo por conexión**,
re-posteado después de parsear
([04 § recepción](../../Server/DataServer/docs/research/04-red.md#recepción)). El contexto cuenta
sus operaciones en `pendingOps`, y lo hace mal en dos lugares: los envíos pendientes no suman (C-2)
y un GQCS fallido cierra sin restar (C-4)
([03 § C-2](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#c-2-pendingops-no-cuenta-los-envíos-que-quedan-pendientes)).

### Para leer

- Microsoft Learn: "I/O Completion Ports", `GetQueuedCompletionStatus` (la sección de valores de
  retorno, entera), `WSARecv`.
- El layout del contexto del original:
  [04 § el contexto por conexión](../../Server/DataServer/docs/research/04-red.md#el-contexto-por-conexión-ds_conn_ctx-0x4078-bytes).

### Preguntas para chequearte

1. Tenés un recv y un send en vuelo para la misma sesión. Sale una completion: ¿cómo sabés cuál de
   los dos fue?
2. ¿Por qué el original no puede tener dos `WSARecv` en vuelo para la misma conexión sin romper el
   orden de los paquetes?
3. Un cliente corta con RST mientras tenés un recv posteado. ¿Qué te devuelve GQCS?

---

## 3.2 epoll: el modelo *reactor*

### La idea

- En Linux, `epoll` no te avisa que una operación **terminó**: te avisa que **podés** hacerla sin
  bloquear. El `recv` lo hacés vos, sobre un socket **no bloqueante**, hasta que devuelve
  `EAGAIN` ("por ahora no hay más").
- Lo que hay que saber:
  - **Level-triggered** (el default): `epoll_wait` te sigue avisando mientras haya datos sin leer.
  - **Edge-triggered** (`EPOLLET`): te avisa solo cuando el estado **cambia**. Si no leés todo
    hasta `EAGAIN`, no te vuelve a avisar y la conexión queda colgada.
  - **`EPOLLONESHOT`**: después de un aviso, el socket queda desarmado hasta que lo rearmás con
    `EPOLL_CTL_MOD`. Con varios hilos en `epoll_wait` sobre el mismo epoll, sin esto **dos hilos
    pueden recibir eventos del mismo socket a la vez**, y adiós al orden por conexión.
  - **`EPOLLOUT`** se pide solo cuando tenés algo pendiente para mandar; si lo dejás siempre
    activo en level-triggered, te avisa sin parar.
- **`eventfd`** es un contador que se puede meter en el epoll: escribirle despierta a quien está en
  `epoll_wait`. Sirve para apagar.
- Para que el resto del servidor no sepa qué backend corre, el reactor se adapta al estilo
  completion: cuando el socket "se puede leer", leés vos y entregás "llegaron estos bytes".

### Ejemplo mínimo (fuera del proyecto)

Un epoll que mira la terminal y un `eventfd`. Sin manejo de errores, para que se vea la forma.
Corrélo en una terminal (epoll no acepta archivos comunes).

```cpp
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

int main() {
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int stop = eventfd(0, EFD_CLOEXEC);

    epoll_event ev{};
    ev.events = EPOLLIN; ev.data.fd = STDIN_FILENO;      // level-triggered
    epoll_ctl(ep, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
    ev.events = EPOLLIN; ev.data.fd = stop;
    epoll_ctl(ep, EPOLL_CTL_ADD, stop, &ev);

    std::jthread timer([stop] {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::uint64_t one = 1;
        (void)write(stop, &one, sizeof one);             // despierta a epoll_wait
    });

    for (;;) {
        epoll_event events[8];
        int n = epoll_wait(ep, events, 8, -1);
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == stop) { std::puts("fin"); return 0; }
            char buf[256];
            ssize_t r = read(STDIN_FILENO, buf, sizeof buf);  // "podés leer": leés vos
            if (r <= 0) return 0;  // Ctrl-D: el destructor del jthread espera al timer
            std::printf("leí %zd bytes\n", r);
        }
    }
}
```

### Errores típicos

- Edge-triggered sin leer hasta `EAGAIN`.
- Olvidar poner el socket en no bloqueante (`SOCK_NONBLOCK`, `accept4`, o `fcntl` con
  `O_NONBLOCK`): el "podés leer" se vuelve un `recv` que bloquea al worker.
- `send` a un socket que el otro cerró: en Linux eso manda la señal `SIGPIPE`, que **mata el
  proceso** por defecto. Se usa `MSG_NOSIGNAL` o se ignora la señal.
- Varios hilos sobre el mismo epoll sin `EPOLLONESHOT` (u otra forma de que una sesión la atienda
  un hilo a la vez).
- Cerrar el descriptor antes de sacarlo del epoll, o mientras otro hilo lo está usando.

### Cómo lo hacía el original

No hay: el original es solo Windows. Lo que tenés que conservar es su **comportamiento**: un recv
a la vez por conexión, paquetes en orden (I-1 en
[03 § el recorrido de un mensaje](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#el-recorrido-de-un-mensaje)).

### Para leer

- `man 7 epoll` (la sección de preguntas frecuentes al final vale oro), `man 2 eventfd`,
  `man 2 accept4`.
- `man 7 socket` sobre `SIGPIPE` y `MSG_NOSIGNAL`.

### Preguntas para chequearte

1. Llegan 3000 bytes y vos leés de a 1024. ¿Cuántos avisos te da epoll en level-triggered? ¿Y en
   edge-triggered?
2. ¿Por qué con IOCP el orden por conexión "sale solo" si tenés un recv en vuelo, y con epoll y
   varios hilos hay que cuidarlo?
3. ¿Qué pasa si un worker de epoll se bloquea en un `recv` porque el socket quedó bloqueante?

---

## 3.3 Cola de envío y cierre ordenado

### La idea

- Mandar es asíncrono igual que recibir: el envío puede quedar **pendiente** si el buffer del
  socket está lleno (el otro lado no lee). Mientras tanto, los bytes tienen que seguir vivos.
- Por eso cada sesión tiene una **cola de envío dueña de sus buffers**: el handler copia o mueve
  el mensaje a la cola y se olvida. La cola manda **de a uno**: un solo envío en vuelo por sesión.
- **Envío parcial:** la completion (o el `send` no bloqueante) puede informar menos bytes que los
  pedidos. Se continúa desde donde quedó, antes de pasar al siguiente mensaje.
- **Cierre ordenado:** un servidor asíncrono se apaga en orden, y el orden importa.

  ```text
  1. dejar de aceptar conexiones nuevas
  2. cerrar las sesiones (las operaciones en vuelo terminan con error de "cancelada")
  3. esperar a que salga la completion de CADA operación en vuelo
  4. despertar a los workers y esperarlos (join)
  5. recién ahora liberar las sesiones y el puerto/epoll
  ```

  Si liberás en el paso 2, una completion del paso 3 toca memoria liberada.
- **Sanitizers:** AddressSanitizer encuentra uso después de liberar y escrituras fuera de rango;
  LeakSanitizer, memoria que nunca se liberó. Son los bugs exactos del original. (MSVC tiene ASan
  pero no LSan; por eso el job de fugas corre en Linux.)

### Ejemplo mínimo (fuera del proyecto)

La diferencia entre mandar desde la pila y mandar desde una cola, como línea de tiempo:

```text
Desde la pila:
  función arma el mensaje en su pila → envío pendiente → la función retorna
  → otra función usa esa pila → el sistema manda bytes que ya son de otra cosa

Desde una cola dueña de sus buffers:
  función → encola (copia o move) → retorna
  cola: ¿hay un envío en vuelo? no → manda el primero
  aviso de "salieron N bytes": ¿quedó resto? → manda el resto; si no, saca el siguiente
```

### Errores típicos

- Dos envíos en vuelo para la misma sesión: los bytes pueden salir intercalados.
- Un `std::vector` de la cola que se mueve de lugar (reasignación) mientras su buffer está en
  vuelo.
- Apagar con `exit()` o dejar que `main` termine con workers vivos.
- Un test de cierre que pasa "porque no se notó": sin ASan, un uso después de liberar puede no
  romper nada en tu máquina.

### Cómo lo hacía el original

Manda desde la pila del handler, con **un único `OVERLAPPED` de envío reusado** y sin mirar si hay
otro envío en curso; el buffer de envío propio del contexto nunca se usa (C-3 en
[03](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#c-3-un-solo-overlapped-de-envío-reusado-apuntando-a-la-pila),
[04 § envío](../../Server/DataServer/docs/research/04-red.md#envío-0x403140-envoltorio-ds_sendpacket-0x405980)).
Y no tiene apagado ordenado: los mensajes encolados se pierden (Q-09).

### Para leer

- Microsoft Learn: `WSASend` (lo que dice sobre la vida del buffer), `CancelIoEx`.
- Clang/GCC: documentación de AddressSanitizer y LeakSanitizer; MSVC: `/fsanitize=address`.

### Preguntas para chequearte

1. ¿Por qué el original casi nunca muestra el bug de C-3 en el laboratorio? ¿Qué tendría que hacer
   un GameServer para dispararlo?
2. En el apagado, ¿por qué el paso 3 va antes del 4?
3. ¿Qué tiene que pasar con la cola de envío de una sesión que se cierra con mensajes pendientes?

---

## 3.4 (opcional) El mismo servidor con Asio

### La idea

- Asio (la de Boost, o la independiente por vcpkg) envuelve IOCP y epoll detrás de un
  `io_context`. Por dentro hace lo que escribiste en 3.1–3.3.
- Lo que te da: operaciones asíncronas con un *completion handler* (`async_read_some`,
  `async_write`), *strands* para que los handlers de una sesión no corran a la vez, y timers.
- Lo que **no** te da: la vida de tus objetos. El patrón típico es que cada operación en vuelo
  sostenga un `shared_ptr` a la sesión (`shared_from_this`): el mismo problema de 3.1, con otra
  herramienta.

### Para leer

- La documentación de Asio: "Basic Asio Anatomy" y los ejemplos de *echo server* asíncrono.

### Preguntas para chequearte

1. ¿Qué bug de los que tuviste en 3.1–3.3 te habría evitado Asio, y cuál no?
2. ¿Qué es un *strand*, en términos de lo que hiciste con epoll?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:** (proactor vs reactor, vida de los buffers)
- **Qué me costó y cómo lo destrabé:**
- **Bugs que me encontraron ASan/LSan:**
- **Decisiones que tomé (y por qué):** cómo acepto · cuántos workers · level o edge-triggered ·
  cómo se libera una sesión
- **Preguntas que me quedaron para más adelante:**
