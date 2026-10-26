# Guía de la ola 2: Sockets a mano

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones (formato de config, logger propio o externo, qué hacer con la conexión 101) son las
> "preguntas para decidir" del
> [plan de la ola 2](../superpowers/plans/2026-10-26-ola-2-sockets-a-mano.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 2.1 | sockets, portabilidad y RAII | por qué un recurso del sistema va adentro de una clase que se mueve y no se copia |
| 2.2 | servidor bloqueante | qué devuelve `recv` en cada caso, y por qué un argumento se valida en vez de convertirse "como sea" |
| 2.3 | un hilo por conexión | cuánto cuesta un hilo de verdad, medido, y por qué el original no lo hace así |
| 2.4 | identificadores con generación | por qué "slot 7" no alcanza para nombrar una sesión |

---

## 2.1 Sockets, portabilidad y RAII

### La idea

- Un **socket** es un recurso del sistema operativo, como un archivo abierto: el kernel te da un
  número para referirte a él y vos tenés que devolverlo cuando terminás.
- Windows y Linux usan casi la misma API (vienen de los sockets BSD), con diferencias chicas que
  muerden:

  | | Windows (Winsock 2) | Linux (POSIX) |
  | --- | --- | --- |
  | tipo | `SOCKET` (entero **sin signo**) | `int` |
  | inválido | `INVALID_SOCKET` | `-1` |
  | antes de usar la red | `WSAStartup`, y `WSACleanup` al final | nada |
  | cerrar | `closesocket` | `close` |
  | último error | `WSAGetLastError()` | `errno` |

- **RAII** (*Resource Acquisition Is Initialization*): el constructor adquiere el recurso y el
  destructor lo libera. Así no hay camino (un `return` temprano, una excepción) que se olvide de
  cerrar.
- Un dueño único **se mueve, no se copia.** Si copiaras la clase, dos objetos cerrarían el mismo
  socket. Por eso se borra el constructor de copia y el de movimiento deja al original **vacío**.
  Es la "regla de cinco": si escribís un destructor, pensá también en copia y movimiento.
- Todo lo que depende del sistema va en **un solo lugar** con `#ifdef _WIN32`. El resto del código
  no sabe en qué sistema corre.
- Un **logger** que llaman varios hilos tiene que escribir cada línea entera bajo un lock. Si no,
  dos hilos pueden intercalar pedazos de línea.

### Ejemplo mínimo (fuera del proyecto)

Un dueño único de un `FILE*` de C. El patrón es el mismo para cualquier handle:

```cpp
#include <cstdio>
#include <utility>

class File {
public:
    explicit File(const char* path) : f_(std::fopen(path, "rb")) {}
    ~File() { if (f_) std::fclose(f_); }

    File(const File&) = delete;                       // no se copia
    File& operator=(const File&) = delete;

    File(File&& other) noexcept : f_(std::exchange(other.f_, nullptr)) {}
    File& operator=(File&& other) noexcept {
        if (this != &other) {
            if (f_) std::fclose(f_);
            f_ = std::exchange(other.f_, nullptr);    // el otro queda vacío
        }
        return *this;
    }

    explicit operator bool() const { return f_ != nullptr; }

private:
    std::FILE* f_ = nullptr;
};
```

### Errores típicos

- El movimiento que no deja al original vacío: los dos destructores cierran el mismo handle.
- Comparar un `SOCKET` con `< 0`: en Windows es sin signo y nunca es negativo.
- Leer `WSAGetLastError()` / `errno` tarde: cualquier otra llamada del sistema en el medio lo pisa.
- `#ifdef _WIN32` desparramado en diez archivos.
- Un logger que toma el lock por cada pedazo (`<< a << b`) en vez de por línea completa.

### Cómo lo hacía el original

El log no tiene lock (C-5): formatea en la pila y escribe en un ring buffer de 30 líneas con un
índice que se incrementa sin atomicidad, y lo llaman todos los hilos
([03 § locks](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#locks)). Funciona
casi siempre, que es la peor forma de fallar.

### Para leer

- cppreference: "RAII", "The rule of three/five/zero", `std::exchange`.
- *Beej's Guide to Network Programming* (la referencia clásica de sockets BSD, gratis).
- Microsoft Learn: "Getting Started with Winsock" y "Porting Socket Applications to Winsock".

### Preguntas para chequearte

1. ¿Qué pasa si una función tiene tres `return` y una excepción posible, y el socket se cierra a
   mano antes de cada `return`?
2. Después de `File b = std::move(a);`, ¿qué tiene `a`? ¿Qué pasa cuando se destruye?
3. ¿Por qué `WSAStartup` no puede ir en el constructor de cada socket?

---

## 2.2 Un servidor bloqueante

### La idea

- La secuencia de un servidor TCP:

  ```text
  socket() → bind(ip, puerto) → listen(backlog) → accept()  ← bloquea hasta que alguien conecta
                                                     │
                                                     └→ socket nuevo para ESE cliente → recv() en bucle
  ```

- **Bloqueante** quiere decir que `accept` y `recv` duermen al hilo hasta que hay algo. Es lo más
  fácil de entender y lo que menos escala.
- `recv` devuelve tres cosas distintas:
  - `> 0`: llegaron esos bytes. Pueden ser medio paquete o tres (la lección de la ola 1).
  - `0`: el otro lado cerró **ordenadamente**. No es un error, y no hay que volver a llamar.
  - `< 0`: error (incluido el corte abrupto con RST).
- `send` también puede mandar **menos** de lo que le pediste: devuelve cuántos bytes salieron.
- El **puerto** en `sockaddr_in` va en orden de red (big-endian): `htons(55960)`. Es la endianness
  de la ola 1, en otro lugar.
- En los tests se usa el **puerto 0**: el sistema elige uno libre y `getsockname` te dice cuál.
  Un puerto fijo en un test choca en CI cuando dos jobs corren a la vez.
- **Validar un argumento** es aceptar lo válido y rechazar todo lo demás con un mensaje claro.
  `atoi` y `sscanf("%d")` no te dicen si había basura ni si el número entraba en el tipo.

### Ejemplo mínimo (fuera del proyecto)

`std::from_chars` (C++17, `<charconv>`) te dice las dos cosas que `atoi` esconde: si el número no
entraba en el tipo y dónde terminó de leer.

```cpp
#include <charconv>
#include <optional>
#include <string_view>

// Volumen de un reproductor: 0..100, nada más.
std::optional<int> parse_volume(std::string_view s) {
    int v = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;  // basura o desborde
    if (v < 0 || v > 100) return std::nullopt;                                 // fuera de rango
    return v;
}
```

### Errores típicos

- Tratar `recv == 0` como "no llegó nada" y seguir llamando: bucle infinito.
- Asumir que `send` mandó todo.
- Olvidar `htons` y escuchar en otro puerto (55960 sin `htons` en x86 es el 39130).
- `SO_REUSEADDR` copiado de un tutorial de Linux: en Windows significa otra cosa (permite que otro
  proceso robe el puerto). Allá el equivalente sano es no usarlo, o `SO_EXCLUSIVEADDRUSE`.
- Un test de integración sin timeout: si el servidor se cuelga, CI espera para siempre.

### Cómo lo hacía el original

`sscanf("%d %d")` y se queda con los 16 bits bajos sin avisar: `"70000"` termina escuchando en el
4464 ([02 § línea de comandos](../../Server/DataServer/docs/research/02-arranque.md#línea-de-comandos)).
Escucha con backlog 5 en el principal y 8 en el *port check*
([04 § dos sockets de escucha](../../Server/DataServer/docs/research/04-red.md#dos-sockets-de-escucha)).

### Para leer

- *Beej's Guide*, capítulos de `bind`, `listen`, `accept`, `recv`/`send`.
- cppreference: `std::from_chars`, `std::optional`.
- Microsoft Learn: "Using SO_REUSEADDR and SO_EXCLUSIVEADDRUSE".

### Preguntas para chequearte

1. El cliente manda 3 frames y cierra. ¿Qué secuencia de valores puede devolver `recv`?
2. ¿Por qué el backlog de `listen` no es "cuántos clientes pueden estar conectados"?
3. ¿Qué tiene que imprimir el servidor si arrancás con `abc` como puerto? ¿Y con `0`?

---

## 2.3 Un hilo por conexión, y por qué no escala

### La idea

- Con un servidor bloqueante, la forma directa de atender a varios es **un hilo por cliente**:
  cada hilo se bloquea en su propio `recv`.
- Cada hilo cuesta:
  - **pila**: en Windows se **reserva** 1 MB por defecto (pero se usa de a poco); en Linux con
    glibc el default es el de `ulimit -s`, típicamente 8 MB de memoria virtual. Reservado no es lo
    mismo que usado: mirá las dos columnas cuando midas.
  - **cambio de contexto**: con miles de hilos, el sistema pasa más tiempo cambiando de hilo que
    trabajando.
  - **creación y destrucción**: no es gratis, sobre todo si las conexiones son cortas.
- `std::thread` **tiene** que terminar en `join()` o `detach()` antes de destruirse; si no, el
  programa llama a `std::terminate`. `std::jthread` (C++20) hace `join` solo en el destructor y
  trae un `std::stop_token` para pedirle que pare.
- Un `stop_token` **no** despierta a un hilo bloqueado en `recv`. Para eso hay que actuar sobre el
  socket: `shutdown` hace que el `recv` vuelva. En Linux, cerrar el descriptor desde otro hilo no
  garantiza despertarlo, y además el número puede reusarse para otro archivo mientras el hilo
  todavía lo usa: `shutdown` primero, `close` después del `join`.
- **Medir, no creer.** El ejercicio de esta sub-ola es abrir 10, 100 y 1000 conexiones y anotar
  qué pasa con la memoria y los hilos del proceso.

### Ejemplo mínimo (fuera del proyecto)

Un programa que solo crea hilos dormidos, para mirar su costo con las herramientas del sistema:

```cpp
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    std::vector<std::jthread> threads;
    for (int i = 0; i < 1000; ++i)
        threads.emplace_back([](std::stop_token st) {
            while (!st.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    std::puts("1000 hilos vivos. Mirá el proceso y apretá Enter.");
    std::getchar();
}   // cada jthread pide stop y hace join al destruirse
```

Dónde mirar: en Windows, Process Explorer o el Administrador de tareas (columnas *Threads*,
*Private bytes*, *Working set*); en Linux, `grep -E 'Threads|VmSize|VmRSS' /proc/<pid>/status`.

### Errores típicos

- Una lambda que captura por referencia una variable del bucle de `accept`: cuando el hilo la lee,
  ya cambió.
- `detach` para "no preocuparse": después no hay forma de esperar a que terminen al apagar.
- Medir en Debug y sacar conclusiones de rendimiento.
- Confundir memoria virtual (`VmSize`) con memoria usada (`VmRSS`).

### Cómo lo hacía el original

No usa un hilo por conexión: tiene un hilo de accept, `CPUs × 2` workers de IOCP y un consumidor,
así que la cantidad de hilos **no depende** de cuántos GameServers estén conectados
([03 § inventario de hilos](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#inventario-de-hilos)).
La excepción es el sync de gremios (C-7), que crea un hilo nuevo por cada `0x40`.

### Para leer

- cppreference: `std::thread`, `std::jthread`, `std::stop_token`.
- Microsoft Learn: "Thread Stack Size".
- `man 3 pthread_create` (sección sobre el tamaño de pila por defecto) y `man 2 shutdown`.

### Preguntas para chequearte

1. Un proceso de 32 bits en Windows tiene 2 GB de espacio de direcciones para el usuario. Con 1 MB
   reservado por hilo, ¿cuántos hilos entran como mucho? ¿Qué te dice eso de los servidores de
   2003?
2. ¿Por qué `VmSize` sube mucho con 1000 hilos y `VmRSS` casi nada?
3. Con 100 GameServers conectados, ¿cuántos hilos tiene el original en una VM de 2 CPUs? ¿Y tu
   servidor de esta sub-ola?

---

## 2.4 Identificadores con generación

### La idea

- La tabla de sesiones tiene 100 slots que se **reusan**. Si a una sesión la nombrás solo por su
  índice, "la sesión 7" puede ser la que se desconectó hace un segundo o la que acaba de entrar en
  su lugar.
- La solución es un **identificador con generación**: cada slot guarda un contador que sube cada
  vez que se libera, y el id de una sesión es el par `(índice, generación)`. Buscar con un id viejo
  falla en vez de encontrar al nuevo ocupante.
- Es el mismo patrón que usan los motores de juego para entidades (*generational indices*, *slot
  maps*) y una defensa contra el problema ABA: "volví a ver A, pero no es el mismo A".
- El estado de cada slot es un `enum class` (no un `int` suelto): el compilador no te deja
  compararlo con cualquier número ni mezclarlo con otro enum.

### Ejemplo mínimo (fuera del proyecto)

Un estacionamiento con 3 lugares que da tickets `(lugar, vuelta)`:

```text
t0  entra auto A → lugar 2, vuelta 0   ticket A = (2, 0)
t1  sale A       → lugar 2 libre, la vuelta pasa a 1
t2  entra auto B → lugar 2, vuelta 1   ticket B = (2, 1)
t3  alguien presenta el ticket (2, 0) para retirar → el lugar 2 está en vuelta 1 → rechazado
```

Sin la vuelta, el ticket viejo de A le entregaría el auto de B.

### Errores típicos

- Que la búsqueda devuelva una referencia al slot y alguien la guarde: el slot cambia de dueño y
  la referencia sigue apuntando ahí.
- No validar el índice antes de usarlo (fuera de rango).
- Un contador de generación muy chico (8 bits) que da la vuelta rápido y vuelve a coincidir.
- Liberar el slot en un lugar y subir la generación en otro: tiene que ser la misma operación.

### Cómo lo hacía el original

La tabla tiene 100 entradas de `0x5C` bytes con un `active` 0/1/2 y asigna el **primer** slot
libre ([04 § la tabla de conexiones](../../Server/DataServer/docs/research/04-red.md#la-tabla-de-conexiones)).
El cierre va **por índice**, así que una completion vieja de una conexión que ya se cerró puede
cerrar a la que ocupa ese slot ahora, y un cierre con error deja el contexto sin liberar (C-4 en
[03 § defectos](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#defectos-de-concurrencia)).

### Para leer

- Cualquier artículo sobre *generational indices* o *slot map* (el `slot_map` de la propuesta
  P0661 para C++ es un buen punto de partida).
- cppreference: "Enumeration declaration" (`enum class`).

### Preguntas para chequearte

1. Con generación de 32 bits y una reconexión por segundo en el mismo slot, ¿cuánto tarda en dar la
   vuelta?
2. ¿Por qué el original "funciona" casi siempre a pesar de C-4? ¿Qué tiene que pasar para que se
   note?
3. ¿Qué devuelve tu tabla cuando la buscás con `(7, generación vieja)`: un error, un `nullptr`, un
   `std::optional` vacío? ¿Por qué elegiste eso?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Lo que medí en la 2.3** (resumen; el detalle va en `ola-2.3-medicion.md`):
- **Decisiones que tomé (y por qué):** formato de config · logger propio o externo · qué hace la
  conexión 101
- **Preguntas que me quedaron para más adelante:**
