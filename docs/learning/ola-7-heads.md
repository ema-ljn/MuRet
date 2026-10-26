# Guía de la ola 7: Resto de heads

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones (qué cambios de wire se aceptan, si hace falta un modo "gremios rotos", dónde va una
> transacción) son las "preguntas para decidir" del
> [plan de la ola 7](../superpowers/plans/2026-10-26-ola-7-resto-de-heads.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 7.1 | efectos después de responder, escalas de unidades, paridad | qué ve el GameServer si algo falla después de la respuesta, y dónde se pierde precisión al escalar |
| 7.2 | respuestas con varias formas, procedimientos, atomicidad | cómo se modela "la respuesta puede ser A o B", y qué es una operación atómica |
| 7.3 | arreglar algo que nunca funcionó | cómo se especifica un comportamiento sin oráculo, cómo se mantiene una caché coherente con la base, y qué es una transacción |

---

## 7.1 Efectos después de responder, escalas y paridad

### La idea

- **El orden importa para el que pregunta.** Si respondés "listo" y después hacés otra cosa que
  falla, el otro lado ya cree que salió bien. A veces es lo correcto (el efecto secundario no es
  parte de lo que se pidió) y a veces es un bug. Lo importante es que sea una decisión, y que el
  test la fije.
- **Escalas de unidades:** un número con decimales que viaja como entero multiplicado (×10, ×100)
  es aritmética de punto fijo. Dos lugares donde se pierde información:
  - **convertir de `double` a entero trunca**, y el `double` puede estar apenas por debajo del
    valor que esperabas;
  - **el rango:** un `u16` con escala ×10 solo representa hasta 6553,5.
- **Paridad byte a byte:** cuando el research tiene un *seam* sobre un layout, la verdad es una
  captura del original. El test compara tu respuesta con la capturada, y una diferencia se lee
  como un volcado hexadecimal lado a lado.

### Ejemplo mínimo (fuera del proyecto)

Un precio que viaja en centavos:

```cpp
#include <cmath>
#include <cstdint>
#include <cstdio>

int main() {
    double price = 1.15;
    auto truncated = static_cast<std::int32_t>(price * 100);          // 114: 1.15*100 = 114.99999999999999
    auto rounded   = static_cast<std::int32_t>(std::lround(price * 100)); // 115
    std::printf("%d %d\n", truncated, rounded);
}
```

### Errores típicos

- Aplicar la escala dos veces (al leer de la base y otra vez al armar el paquete), o ninguna.
- No pensar qué pasa con un valor que no entra en el campo del wire.
- Comparar respuestas "a ojo" en vez de byte a byte.
- Responder antes de un efecto que **sí** es parte de lo pedido.

### Cómo lo hacía el original

- `0x05` responde y **después** corre la cascada de gremio; result 0 solo si la cuenta o el nombre
  vienen vacíos.
- `0x07` guarda los stats con escala ×0,1 y los blobs solo si el `UPDATE` anduvo; que `0x06`
  multiplique Life/Mana ×10 es *inferido* (H-3).
- Todo en la [tabla de heads](../../Server/DataServer/docs/research/05-protocolo.md#tabla-de-heads)
  y en [10-pendientes](../../Server/DataServer/docs/research/10-pendientes.md).

### Para leer

- cppreference: `std::lround`, conversiones de punto flotante a entero ("Floating–integral
  conversions").
- "What Every Computer Scientist Should Know About Floating-Point Arithmetic" (Goldberg); con la
  primera parte alcanza.

### Preguntas para chequearte

1. Si Life viaja ×10 en un `u16`, ¿cuál es la vida más alta que se puede mandar sin romperse?
2. En `0x05`, ¿qué ve el GameServer si el borrado del personaje anda pero la cascada de gremio
   falla?
3. Tu respuesta de `0x06` difiere de la captura en 2 bytes. ¿Cómo averiguás si el error es tuyo o
   del research?

---

## 7.2 Respuestas con varias formas, procedimientos y atomicidad

### La idea

- **Una respuesta con dos formas** ("si la bodega es nueva respondo X, si existe respondo Y") se
  modela mejor como un tipo que dice "es una de estas dos", que con un `if` que arma bytes
  distintos en el medio del handler. En C++ eso es `std::variant` + `std::visit`. Cada forma tiene
  su serializador y su test.
- **Procedimientos almacenados:** lógica que vive en la base y se llama por nombre con argumentos.
  Desde ODBC es una sentencia más (`{CALL nombre(?, ?)}` o `EXEC nombre ?, ?`), con los mismos
  parámetros enlazados de la ola 6. El cuerpo no está en el binario: está en la base.
- **Atomicidad:** una operación es atómica si nadie puede ver ni meterse en un estado intermedio.
  "Leer el último número y guardar el siguiente" en dos pasos **no** es atómico: dos clientes
  pueden leer el mismo valor. Que MuRet tenga un solo hilo de lógica no ayuda si hay **otros
  procesos** usando la misma base.

### Ejemplo mínimo (fuera del proyecto)

Un resultado con dos formas:

```cpp
#include <cstdio>
#include <string>
#include <type_traits>
#include <variant>

struct Created  { int id; };
struct Existing { int id; std::string owner; };
using OpenResult = std::variant<Created, Existing>;

void describe(const OpenResult& r) {
    std::visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Created>) std::printf("nueva: %d\n", v.id);
        else std::printf("existente: %d de %s\n", v.id, v.owner.c_str());
    }, r);
}
```

Y una carrera entre dos procesos que comparten una base:

```text
proceso A: SELECT MAX(n) → 41          proceso B: SELECT MAX(n) → 41
proceso A: INSERT n = 42               proceso B: INSERT n = 42     ← número repetido
```

### Errores típicos

- Un `bool esNueva` al lado de campos que solo valen en uno de los dos casos.
- Asumir que un procedimiento hace lo que dice su nombre sin haber leído su cuerpo (H-6).
- Creer que "un solo hilo" resuelve la concurrencia contra una base compartida.

### Cómo lo hacía el original

- `0x08` responde con head `0x10` si la bodega no existía, y con `C2 04C8 08` si existía; el layout
  exacto es *seam* (H-2).
- `0x52` pide el serial a `WZ_GetItemSerial` y, si no hay fila, **no responde**.
- `0x20` actualiza el `ItemCount` en la base y, según la reconstrucción, no toca el valor en
  memoria: el login `0x00` sigue devolviendo el que se leyó al arrancar.
- Los procedimientos y sus argumentos están en
  [06 § bodega, OptionData, procedimientos](../../Server/DataServer/docs/research/06-sql-y-base.md#bodega-optiondata-procedimientos).

### Para leer

- cppreference: `std::variant`, `std::visit`, `if constexpr`.
- Documentación de SQL Server sobre `EXECUTE` y sobre llamar procedimientos desde ODBC
  ("Calling Stored Procedures").

### Preguntas para chequearte

1. ¿Qué te da `std::variant` que no te da un struct con un `bool` y todos los campos?
2. En el ejemplo de los dos procesos, ¿qué cambiarías para que sea atómico? ¿Dónde vive ese
   cambio, en el programa o en la base?
3. Si `0x20` cambia el `ItemCount`, ¿qué tiene que devolver el siguiente `0x00` en MuRet? ¿Cambia
   algo que el GameServer pueda ver?

---

## 7.3 Arreglar algo que nunca funcionó

### La idea

- **Sin oráculo:** los gremios del original nunca anduvieron (Q-01), así que no hay captura de
  "cómo responde cuando sale bien". La especificación sale de lo que el **GameServer espera**
  (los layouts de los paquetes, lo que hace con cada result) y queda escrita en un ADR antes del
  código.
- **Caché coherente con la base:** si tenés los gremios en RAM y en la base, cada cambio tiene que
  dejar a los dos de acuerdo. Lo usual: escribir primero en la base y, solo si anduvo, actualizar
  la caché. Y al arrancar, **cargar** la caché desde la base.
- **Transacción:** un grupo de sentencias que se aplica entero o no se aplica (`BEGIN TRANSACTION`
  … `COMMIT` o `ROLLBACK`). En ODBC se apaga el *autocommit* (`SQLSetConnectAttr` con
  `SQL_ATTR_AUTOCOMMIT`) y se cierra con `SQLEndTran`.
- **Orden de locks:** dos hilos que toman los mismos dos locks en orden opuesto se pueden bloquear
  para siempre (deadlock). Es C-1. En MuRet el estado lo toca un solo hilo, así que no hay locks
  que cruzar, pero conviene entender por qué.
- Un **flag de configuración** permite tener los dos comportamientos (el nuevo y el viejo) y
  elegir sin recompilar. Si hace falta acá, lo decidís vos.

### Ejemplo mínimo (fuera del proyecto)

El deadlock por orden de locks, y la forma de tomar dos locks sin él:

```cpp
#include <mutex>

std::mutex inventory, ledger;

void restock() { std::lock_guard a(inventory); std::lock_guard b(ledger); }   // inventory → ledger
void audit()   { std::lock_guard a(ledger); std::lock_guard b(inventory); }   // ledger → inventory: deadlock posible

void audit_ok() { std::scoped_lock both(ledger, inventory); }                  // toma los dos sin deadlock
```

Y una transacción, en SQL genérico:

```text
BEGIN TRANSACTION
  INSERT INTO Teams (Name, Captain) VALUES (?, ?)
  INSERT INTO TeamMembers (Name, Team, Role) VALUES (?, ?, 'captain')
COMMIT          ← o ROLLBACK si cualquiera falló: nunca queda un equipo sin capitán
```

### Errores típicos

- Actualizar la caché antes de saber si la base aceptó el cambio.
- Deshacer "a mano" (borrar lo que ya insertaste) en vez de usar una transacción: si el borrado
  también falla, quedás peor.
- Mandar paquetes al GameServer con un lock tomado.
- Probar gremios solo contra el repositorio en memoria: lo que nunca anduvo es justamente la parte
  de la base.

### Cómo lo hacía el original

- Las dos conexiones de gremios **nunca llaman a `Connect`**: todo el SQL de gremios falla, la caché
  queda vacía, y los heads devuelven result 3 o 4, o nada
  ([Q-01](../../Server/DataServer/docs/research/08-quirks-y-bugs.md#q-01-los-gremios-nunca-funcionan)).
- La caché es una lista de entradas de `0x25C` bytes sin loader al arrancar
  ([07 § caché de gremios](../../Server/DataServer/docs/research/07-datos-auxiliares.md#caché-de-gremios-en-ram)).
- El hilo de sync toma el lock de gremios y después el global; el handler `0x31` los toma al revés
  ([03 § C-1](../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#c-1-inversión-del-orden-de-locks-entre-gremios-y-global)).
- Las SQL de gremios están en
  [06 § gremios](../../Server/DataServer/docs/research/06-sql-y-base.md#gremios-todas-sobre-conexiones-sin-connect--q-01).

### Para leer

- cppreference: `std::scoped_lock`.
- Microsoft Learn: "Transactions (ODBC)", `SQLEndTran`, `SQL_ATTR_AUTOCOMMIT`.
- *C++ Concurrency in Action*, la sección sobre evitar deadlocks.

### Preguntas para chequearte

1. ¿Por qué C-1 nunca se dispara en el original? ¿Qué pasaría si alguien arreglara solo Q-01?
2. Crear un gremio son varias sentencias. ¿Cuál puede fallar, y en qué estado queda todo si falla
   la tercera sin transacción?
3. Arrancás MuRet con gremios en la base. ¿Qué tiene que devolver el primer `0x40`?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Lo que aprendí de las capturas** (y qué llevé al research):
- **Decisiones que tomé (y por qué):** cambios de wire aceptados · modo de compatibilidad de
  gremios · dónde van las transacciones
- **Preguntas que me quedaron para más adelante:**
