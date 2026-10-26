# Guía de la ola 5: Handlers en memoria

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones (tipo de error, granularidad de los repositorios, capturas del wire) son las
> "preguntas para decidir" del
> [plan de la ola 5](../superpowers/plans/2026-10-26-ola-5-handlers-en-memoria.md), y las tomás
> vos. La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 5.1 | interfaces e inyección de dependencias | por qué un handler no tiene que saber si atrás hay SQL Server, y qué es un test de contrato |
| 5.2 | el handler como función | cómo se testea un handler sin red ni base |
| 5.3 | flujos de varios pasos y datos como datos | qué pasa cuando un paso falla a la mitad, y por qué las tablas van en archivos y no en un `switch` |

---

## 5.1 Interfaces e inyección de dependencias

### La idea

- Una **interfaz** en C++ es una clase con métodos `virtual` puros (`= 0`) y un destructor
  `virtual`. Dice **qué** se puede hacer, no **cómo**.
- **Inyección de dependencias:** el handler no crea su repositorio; lo **recibe** (por
  constructor). En producción le pasás el de ODBC; en los tests, uno en memoria. El handler no
  cambia.
- Un repositorio en memoria es un *fake*: una implementación de verdad, simple, que se comporta
  como la real. Es distinto de un *mock*, que verifica qué llamadas recibió. Para repositorios, el
  fake suele dar tests más claros.
- **Test de contrato:** los tests se escriben contra la **interfaz**, y corren contra **todas** las
  implementaciones. Si el de memoria y el de ODBC pasan los mismos tests, son intercambiables de
  verdad. (En la ola 6 los convertís en tests tipados.)
- **Distinguir "no existe", "falló" y "el valor".** Es el problema de fondo de esta ola. Un `int`
  que a veces es un dato y a veces un código de error (`-101`) es una trampa. Opciones: un tipo
  resultado (`std::expected` es C++23; en C++20 hay `tl::expected` o uno propio), o
  `std::optional` más un error aparte. Lo decidís vos.

### Ejemplo mínimo (fuera del proyecto)

Un reloj inyectado, para testear algo que depende de la hora sin esperar:

```cpp
#include <chrono>

class Clock {
public:
    virtual ~Clock() = default;
    virtual std::chrono::system_clock::time_point now() const = 0;
};

class SystemClock final : public Clock {
public:
    std::chrono::system_clock::time_point now() const override { return std::chrono::system_clock::now(); }
};

class FakeClock final : public Clock {                    // para tests
public:
    std::chrono::system_clock::time_point t{};
    std::chrono::system_clock::time_point now() const override { return t; }
};

class Coupon {                                            // no sabe qué reloj tiene
public:
    Coupon(const Clock& clock, std::chrono::system_clock::time_point expires)
        : clock_(clock), expires_(expires) {}
    bool valid() const { return clock_.now() < expires_; }
private:
    const Clock& clock_;
    std::chrono::system_clock::time_point expires_;
};
```

En el test, adelantás `FakeClock::t` y comprobás que el cupón vence, sin `sleep`.

### Errores típicos

- Olvidar el destructor `virtual`: borrar una implementación por puntero a la interfaz es
  comportamiento indefinido.
- Una interfaz calcada de las tablas SQL (`ExecuteQuery(string)`): el handler sigue sabiendo de SQL.
- Tests de contrato que en realidad prueban detalles del fake (el orden de un `unordered_map`, por
  ejemplo).
- Devolver referencias a datos internos del repositorio que después cambian.

### Cómo lo hacía el original

No hay capas: el handler arma el SQL con `sprintf`, lo ejecuta y lee las columnas en el mismo
lugar. Los getters de `CQuery` devuelven `-100..-103` como si fueran datos
([06 § `CQuery`](../../Server/DataServer/docs/research/06-sql-y-base.md#cquery-la-capa-odbc)), y
por eso existen Q-05 (el error de arranque viaja al GS como `ItemCount`) y Q-08 (una consulta que
falla devuelve "slot 0")
([08](../../Server/DataServer/docs/research/08-quirks-y-bugs.md#q-08-ds_findfreegameidslot-devuelve-el-slot-0-cuando-la-query-falla)).

### Para leer

- cppreference: "Abstract class", "virtual function specifier", `override`, `final`.
- Martin Fowler, "Mocks Aren't Stubs" (la diferencia entre fakes, stubs y mocks).
- La documentación de `tl::expected` (por si lo elegís) y la de `std::expected` en cppreference.

### Preguntas para chequearte

1. ¿Por qué `Coupon` guarda `const Clock&` y no un `Clock` por valor?
2. Si el repositorio de memoria pasa todos los tests de contrato y el de ODBC no, ¿qué te dice eso?
3. En tu diseño, ¿cómo se ve "la cuenta no existe" distinto de "la base no respondió"?

---

## 5.2 El handler como función

### La idea

- Un handler recibe un request y el estado, y produce **respuestas** (qué mandar y a quién) y
  **cambios** (qué guardar). Si no toca sockets ni hilos, se testea como una función: entrada →
  salida.
- Para que eso sea posible, el handler no manda por la red: llama a una interfaz de envío. En los
  tests, esa interfaz es un fake que **anota** lo que le mandaron, y el test compara esos bytes con
  los esperados.
- Estructura de un test: **Arrange** (estado inicial), **Act** (llamar al handler), **Assert**
  (respuestas y estado final). Los *fixtures* de GoogleTest (`TEST_F`) arman el Arrange común.
- Un *broadcast* es "una respuesta para cada sesión activa": el test lo verifica mirando el fake de
  envío de cada una.

### Ejemplo mínimo (fuera del proyecto)

Una máquina expendedora como función pura. No hay hardware: el test le pasa un estado y una
moneda, y mira el estado nuevo y lo que "salió".

```cpp
#include <string>
#include <vector>

struct Machine { int credit = 0; int stock = 3; };
struct Step { Machine next; std::vector<std::string> outputs; };

Step insert_coin(Machine m, int coin) {
    Step s{m, {}};
    s.next.credit += coin;
    if (s.next.credit >= 100 && s.next.stock > 0) {
        s.next.credit -= 100;
        s.next.stock -= 1;
        s.outputs.push_back("producto");
    }
    return s;
}
// Test: insert_coin({50, 3}, 50) → next = {0, 2}, outputs = {"producto"}
```

### Errores típicos

- Un handler que llama directo a `IoBackend` y obliga a levantar red para testearlo.
- Tests que solo miran "respondió algo" en vez de los bytes exactos.
- Olvidar el caso del request corto (menos bytes que el layout): en la ola 1 decidiste qué pasa.
- Estado compartido entre tests (un repositorio global): un test rompe a otro según el orden.

### Cómo lo hacía el original

- `0x00` no hace SQL: devuelve el `ItemCount` leído **una vez al arrancar**
  ([05 § 0x00](../../Server/DataServer/docs/research/05-protocolo.md#0x00--login-del-gameserver)).
- `0x40` crea un hilo por pedido para mandar la caché de gremios (C-7), que está siempre vacía
  (Q-01) ([05 § 0x40](../../Server/DataServer/docs/research/05-protocolo.md#0x40--el-que-destapa-el-bug-de-gremios)).
- `0x02` reenvía el paquete a todos los GS; con un C2 usa `byte[1]` como largo y lo corta mal.

### Para leer

- GoogleTest Primer: `TEST_F`, `SetUp`, y "Test Fixtures: Using the Same Data Configuration for
  Multiple Tests".
- La [tabla de heads](../../Server/DataServer/docs/research/05-protocolo.md#tabla-de-heads) del
  research, para cada head que implementes.

### Preguntas para chequearte

1. ¿Qué necesita saber el test de `0x00` sobre la red? ¿Y sobre SQL?
2. ¿Por qué `insert_coin` recibe `Machine` por valor y devuelve uno nuevo?
3. En el test de broadcast con dos sesiones, ¿qué tiene que haber en cada fake de envío?

---

## 5.3 Flujos de varios pasos y datos como datos

### La idea

- Varios heads son **flujos**: hacen varios pasos contra la base. Si uno falla a la mitad, los
  anteriores **ya pasaron**. Hay que decidir, para cada flujo, qué estado queda y qué se responde.
- El orden de los pasos importa: en un alta, lo que es caro de deshacer va al final, después de
  todo lo que puede fallar.
- "Saltear sin dejar hueco" y "dejar el lugar vacío" son dos contratos distintos con el cliente. El
  test fija cuál es el del original.
- **Datos como datos:** stats por clase, tabla de items y filtros de nombres son **tablas**. Van en
  archivos que se cargan al arrancar, con validación y error claro si una línea está mal. El código
  solo las consulta. Cambiar un valor no debería requerir recompilar.
- **Round-trip** otra vez: para el cifrado del nombre, cifrar y descifrar tiene que devolver lo
  mismo. Es la misma idea que el test de serialización de la ola 1.

### Ejemplo mínimo (fuera del proyecto)

Un flujo de dos pasos que falla a la mitad:

```text
transferir(A → B, 100):
  1. restar 100 de A     ✔
  2. sumar 100 a B       ✘ (la base no respondió)
  → A perdió 100 y B no los recibió. ¿Qué respondés? ¿Qué arreglás?
```

Y una tabla que deja de estar en el código:

```cpp
// Antes: los datos escondidos en un switch
int shipping_cost(int zone) {
    switch (zone) { case 1: return 500; case 2: return 800; default: return 1200; }
}

// Después: "zona costo" por línea en un archivo; el código solo busca
struct ShippingTable {
    static std::optional<ShippingTable> load(std::istream& in);  // nullopt + log con la línea mala
    std::optional<int> cost(int zone) const;
private:
    std::unordered_map<int, int> cost_by_zone_;
};
```

### Errores típicos

- Escribir los blobs iniciales de un personaje antes de saber si se creó.
- Correr un chequeo, ver que falla, y cortar: el original corre los tres chequeos de nombre igual
  (sin corto-circuito); si eso cambia algo observable (logs), el test lo fija.
- Cargar una tabla sin validar: una línea mal formada se convierte en un cero silencioso.
- Rutas relativas al directorio de trabajo para los archivos de datos.

### Cómo lo hacía el original

- `0x01` crea la cuenta si no existe, trae los personajes y **saltea sin hueco** los previews que
  fallan ([05 § 0x01](../../Server/DataServer/docs/research/05-protocolo.md#0x01--lista-de-personajes)).
- `0x04` corre tres chequeos de nombre sin corto-circuito, llama al SP y **solo si anduvo** escribe
  los blobs iniciales ([05 § 0x04](../../Server/DataServer/docs/research/05-protocolo.md#0x04--crear-personaje)).
- `0x06` descifra el nombre con un XOR de 3 bytes y responde un paquete fijo de `0x3BC` bytes más
  uno de opciones ([05 § 0x06](../../Server/DataServer/docs/research/05-protocolo.md#0x06--cargar-personaje)).
- Las tablas: [07 § stats](../../Server/DataServer/docs/research/07-datos-auxiliares.md#stats-de-inicio-por-clase),
  [items](../../Server/DataServer/docs/research/07-datos-auxiliares.md#tabla-de-items),
  [filtros](../../Server/DataServer/docs/research/07-datos-auxiliares.md#filtros-de-nombres) y
  [cifrado](../../Server/DataServer/docs/research/07-datos-auxiliares.md#cifrado-del-nombre-en-0x06).

### Para leer

- cppreference: `std::istream` y `std::getline`, `std::from_chars` (de la ola 2).
- GoogleTest: cómo apuntar a archivos de datos de test (una variable de CMake con la ruta, pasada
  como definición al compilar).

### Preguntas para chequearte

1. En `transferir`, ¿qué orden de pasos hace que una falla sea más fácil de arreglar?
2. Si un preview falla y la lista sale sin hueco, ¿qué ve el GameServer? ¿Y si saliera con hueco?
3. ¿Qué tiene que pasar si el archivo de stats tiene una clase de más o una línea con letras?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:** (interfaces, inyección de dependencias)
- **Qué me costó y cómo lo destrabé:**
- **Decisiones que tomé (y por qué):** tipo de error · granularidad de los repositorios ·
  capturas del wire
- **Seams que cerré o dejé marcados:**
- **Preguntas que me quedaron para más adelante:**
