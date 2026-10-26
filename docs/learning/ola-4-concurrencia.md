# Guía de la ola 4: Concurrencia

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones (qué hacer con la cola llena, de qué tamaño, qué orden garantizar) son las
> "preguntas para decidir" del
> [plan de la ola 4](../superpowers/plans/2026-10-26-ola-4-concurrencia.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 4.1 | productor/consumidor con `mutex` y `condition_variable` | por qué se espera con un predicado, y cómo se apaga una cola sin dejar a nadie colgado |
| 4.2 | un hilo dueño del estado | por qué los handlers de MuRet no necesitan locks, y qué se pierde a cambio |
| 4.3 | carreras, TSan y medición | la diferencia entre *data race* y condición de carrera, y cómo se mide throughput y latencia sin engañarse |

---

## 4.1 Productor/consumidor

### La idea

- Varios hilos **producen** (los de I/O cortan frames) y uno **consume** (el de lógica). En el
  medio, una cola compartida protegida por un **`std::mutex`**.
- El consumidor no debe girar preguntando "¿hay algo?". Se duerme en una
  **`std::condition_variable`** y el productor lo despierta con `notify_one`.
- **Siempre se espera con un predicado:** `cv.wait(lock, [&]{ return !q.empty() || stopped; })`.
  Una condition variable puede despertarse sin que nadie la haya notificado (*spurious wakeup*), y
  además otro hilo puede haberse llevado el elemento antes que vos. El predicado se vuelve a
  evaluar con el lock tomado.
- `wait` suelta el mutex mientras duerme y lo vuelve a tomar al despertar. Por eso necesita un
  `std::unique_lock` (que se puede soltar), no un `std::lock_guard`.
- Una cola **acotada** tiene un tope: cuando se llena, algo tiene que pasar (esperar, rechazar,
  dejar de leer). Una cola sin tope solo mueve el problema a la memoria.
- **Apagar:** una bandera `stopped` dentro del mismo mutex, `notify_all`, y que `Pop` sepa
  devolver "terminado" en vez de un elemento.

### Ejemplo mínimo (fuera del proyecto)

Una "puerta" que bloquea a los hilos hasta que alguien la abre. Tiene las mismas piezas que una
cola, sin ser una cola:

```cpp
#include <condition_variable>
#include <mutex>

class Gate {
public:
    void open() {
        {
            std::lock_guard lock(m_);
            open_ = true;
        }
        cv_.notify_all();                            // despierta a todos los que esperan
    }
    void wait() {
        std::unique_lock lock(m_);
        cv_.wait(lock, [this] { return open_; });    // el predicado cubre los despertares falsos
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    bool open_ = false;
};
```

### Errores típicos

- `wait` sin predicado (o con un `if` en vez del predicado).
- Modificar la condición **sin** el mutex y después notificar: el consumidor puede perderse el
  aviso y dormir para siempre.
- Hacer trabajo pesado (o llamar a un callback) con el lock de la cola tomado.
- `Stop` que pone la bandera pero no notifica, o notifica con `notify_one` habiendo varios
  esperando.

### Cómo lo hacía el original

Lista enlazada con tope fijo de 5000. Si está llena, **descarta el mensaje en silencio y pierde
unos 8 KB** (nadie mira el retorno). El consumidor no espera en nada: pregunta el tamaño, saca
**uno** y hace `Sleep(1)`, en bucle
([03 § la cola](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#la-cola)).

### Para leer

- cppreference: `std::condition_variable` (el ejemplo de la página), `std::unique_lock`,
  `std::condition_variable_any` con `std::stop_token` (C++20).
- *C++ Concurrency in Action*, de Anthony Williams, capítulos 3 y 4 (el libro de referencia).

### Preguntas para chequearte

1. ¿Qué pasa si el productor hace `q.push(x)` sin tomar el mutex, y después `notify_one`?
2. Con la cola vacía y `Stop()` llamado, ¿qué tiene que devolver `Pop` y cómo se entera?
3. ¿Por qué `notify_one` puede ir **afuera** del bloque que tiene el lock?

---

## 4.2 Un hilo dueño del estado

### La idea

- Si **un solo hilo** toca el estado del servidor (tabla de personajes, caché de gremios), ese
  estado **no necesita locks**: no hay con quién competir. Los demás hilos no tocan el estado,
  le **mandan mensajes** al dueño. Es la idea del modelo de actores y de los *event loops*.
- La red y la lógica quedan separadas: los hilos de I/O no esperan a la lógica (solo encolan), y
  la lógica no sabe de sockets (responde por una interfaz de envío).
- El precio: todo lo que hace el hilo de lógica es **secuencial**. Si un handler tarda (por ejemplo,
  una consulta SQL lenta), los demás esperan. Es el mismo precio que paga el original, pero sin
  frenar la red.
- El **dispatcher** es una tabla `head → handler`. Un head sin handler no es un crash: se loguea y
  se sigue.
- **Drenar:** en cada vuelta, procesar todo lo que hay en la cola (o un lote), en vez de uno y a
  dormir.

### Ejemplo mínimo (fuera del proyecto)

Un bot de chat con una tabla de comandos. El patrón "clave → función, y qué hacer con una clave
desconocida" es el mismo:

```cpp
#include <cstdio>
#include <functional>
#include <string_view>
#include <unordered_map>

using Command = std::function<void(std::string_view args)>;

int main() {
    std::unordered_map<std::string_view, Command> commands{
        {"/help", [](std::string_view) { std::puts("comandos: /help /echo"); }},
        {"/echo", [](std::string_view a) { std::printf("%.*s\n", int(a.size()), a.data()); }},
    };

    auto run = [&](std::string_view name, std::string_view args) {
        if (auto it = commands.find(name); it != commands.end()) it->second(args);
        else std::printf("comando desconocido: %.*s\n", int(name.size()), name.data());
    };
    run("/echo", "hola");
    run("/dance", "");
}
```

### Errores típicos

- "Por las dudas" agregar un mutex al estado que solo toca el hilo de lógica: no protege nada y
  esconde que alguien más lo está tocando.
- Un handler que guarda una referencia a los bytes del mensaje después de retornar.
- Llamar desde un hilo de I/O a algo del estado "solo para leer": eso también es una carrera.
- Un handler que bloquea esperando una respuesta de la red: el hilo de lógica se queda esperándose
  a sí mismo.

### Cómo lo hacía el original

Tiene un solo consumidor, así que también hay un "dueño" del estado, pero lo protege con un **lock
global** que también usan los workers, el accept y cada envío. Mientras un handler corre (con su
SQL), **ninguna** conexión puede procesar nada
([03 § el recorrido de un mensaje](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#el-recorrido-de-un-mensaje)).
De ahí salen tres invariantes que MuRet conserva a propósito: **I-1** (orden por conexión),
**I-2** (orden total) e **I-3** (un handler nunca corre en paralelo con otro).

### Para leer

- Cualquier introducción al modelo de actores o a *event loops* (el de Node.js o el de un motor de
  juego sirven).
- cppreference: `std::function`, `std::unordered_map`.

### Preguntas para chequearte

1. ¿Por qué I-3 hace innecesarios los locks en los handlers?
2. Si mañana quisieras dos hilos de lógica, ¿qué invariante perdés primero?
3. ¿Qué hace tu hilo de lógica mientras espera una consulta SQL de 200 ms? ¿Y los hilos de I/O?

---

## 4.3 Carreras, TSan y medición

### La idea

- **Data race:** dos hilos acceden a la misma memoria, al menos uno escribe, y no hay
  sincronización entre ellos. En C++ es **comportamiento indefinido**, aunque "parezca funcionar".
- **Condición de carrera:** el resultado depende del orden en que corren los hilos. Puede pasar
  **sin** data race (todo con atómicos o con locks, pero con la lógica mal partida).
- **ThreadSanitizer** (TSan) instrumenta cada acceso a memoria y detecta data races cuando ocurren
  en una corrida. No detecta condiciones de carrera lógicas. No se puede combinar con ASan y no
  existe en MSVC: corre en Linux con Clang o GCC.
- **Backpressure:** cuando el consumidor no da abasto, la presión se propaga hacia atrás (dejar de
  leer de una conexión hace que TCP frene al que manda) en vez de acumular sin límite.
- **Medir:**
  - *throughput* (mensajes por segundo) y *latencia* (cuánto tarda **cada** mensaje) son cosas
    distintas; la latencia se reporta con percentiles (p50, p99), no con el promedio.
  - **Ley de Little:** en régimen, elementos en el sistema = tasa de llegada × tiempo en el
    sistema (`L = λ·W`). Sirve para razonar el tamaño de una cola.
  - En Release, con calentamiento, varias corridas, y con `std::chrono::steady_clock`.

### Ejemplo mínimo (fuera del proyecto)

Un data race que TSan encuentra, y una condición de carrera que no:

```cpp
#include <atomic>
#include <cstdio>
#include <thread>

int hits = 0;                                   // sin protección
void count() { for (int i = 0; i < 100000; ++i) ++hits; }

std::atomic<int> seats{1};
bool book() {                                   // sin data race, pero mal: "chequear y después actuar"
    if (seats.load() > 0) { seats.fetch_sub(1); return true; }
    return false;
}

int main() {
    { std::jthread a(count), b(count); }        // compilá con -fsanitize=thread
    std::printf("hits = %d (esperabas 200000)\n", hits);
    { std::jthread a(book), b(book); }          // a veces quedan -1 asientos; TSan no dice nada
    std::printf("seats = %d\n", seats.load());
}
```

### Errores típicos

- "Corrió mil veces y no falló" como prueba de que no hay carrera.
- Un test de estrés que no verifica nada (solo que no crashea).
- Medir con el log en nivel debug escribiendo a disco por cada mensaje.
- Reportar el promedio de latencia y esconder que el p99 es diez veces peor.

### Cómo lo hacía el original

El techo de throughput sale de `Sleep(1)` más el handler: `μ = 1 / (t_s + t_h)`, unos **64
mensajes por segundo** para todo el servidor con el reloj por defecto de Windows (*inferido*,
[03 § throughput y latencia](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#throughput-y-latencia)).
Es el número contra el que vas a comparar tu medición.

### Para leer

- Documentación de ThreadSanitizer (Clang): cómo leer un reporte.
- cppreference: "Memory model" (sección *Threads and data races*).
- Cualquier explicación de la Ley de Little (es de teoría de colas, una página alcanza).

### Preguntas para chequearte

1. En el ejemplo, ¿por qué `hits` puede terminar en menos de 200000? ¿Qué instrucción se parte?
2. ¿Cómo arreglarías `book` sin usar un mutex? (Pista: `compare_exchange`.)
3. Si llegan 300 mensajes por segundo y cada uno pasa 20 ms en el sistema, ¿cuántos hay en
   promedio en la cola?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Lo que medí en la 4.3** (resumen; el detalle va en `ola-4.3-throughput.md`), al lado de los
  ~64 msg/s del original:
- **Decisiones que tomé (y por qué):** cola llena · tope de la cola · orden total o por conexión
- **Preguntas que me quedaron para más adelante:**
