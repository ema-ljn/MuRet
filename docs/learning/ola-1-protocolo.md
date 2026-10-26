# Guía de la ola 1: Protocolo sin red

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito: muestran el
> concepto, no la solución de MuRet. Las decisiones de diseño (tamaño máximo de frame, qué hacer
> ante un error) no están acá: son las "preguntas para decidir" del
> [plan de la ola 1](../superpowers/plans/2026-10-26-ola-1-protocolo-sin-red.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 1.1 | bytes, vistas y endianness | por qué `std::span` no copia, y cómo se lee un `u16` little-endian sin castear |
| 1.2 | flujo TCP y máquina de estados | por qué un `recv` no es un paquete, y qué invariantes cumple un parser |
| 1.3 | layout en memoria y serialización | qué es el padding, qué asegura un `static_assert(sizeof)` y por qué no se castea un buffer a un struct |

---

## 1.1 Bytes, vistas y endianness

### La idea

- **`std::byte`** (C++17, `<cstddef>`) es un byte que **no es un número**: no podés sumarlo por
  error, solo hacer operaciones de bits. Para leerlo como número lo convertís explícitamente con
  `std::to_integer<uint8_t>(b)`.
- **`std::span<const std::byte>`** (C++20, `<span>`) es una **vista**: un puntero y un largo. No es
  dueña de la memoria y copiarla es gratis. Sirve para pasar "estos bytes" a una función sin que
  importe si vienen de un `std::vector`, un `std::array` o un buffer de red. Ojo: si la memoria
  de atrás muere, la vista queda colgando.
- **Endianness:** el orden de los bytes de un número de varios bytes. MU manda los campos en
  **little-endian** (el byte menos significativo primero: `0x1234` viaja como `34 12`), pero el
  tamaño de un paquete C2 va en **big-endian** (`0x01F4` viaja como `01 F4`). En el mismo paquete
  conviven los dos órdenes.
- **No se castea un buffer a `uint16_t*`.** Hacer `*reinterpret_cast<const uint16_t*>(p)` sobre
  bytes que no son un `uint16_t` rompe la regla de *strict aliasing* (comportamiento indefinido)
  y además puede leer desalineado. Se arma el número a mano con shifts, o se copia con
  `std::memcpy` / `std::bit_cast` (C++20).

### Ejemplo mínimo (fuera del proyecto)

```cpp
#include <cstddef>
#include <cstdint>
#include <span>

// Lee un u32 big-endian (como el largo de muchos formatos de archivo).
std::uint32_t read_u32_be(std::span<const std::byte, 4> b) {
    return (std::to_integer<std::uint32_t>(b[0]) << 24) |
           (std::to_integer<std::uint32_t>(b[1]) << 16) |
           (std::to_integer<std::uint32_t>(b[2]) << 8)  |
            std::to_integer<std::uint32_t>(b[3]);
}
```

Fijate que no depende del endianness de la máquina: funciona igual en x86 y en ARM.

### Errores típicos

- Olvidar convertir a un tipo más ancho antes de hacer shift (`uint8_t << 8` se promueve a `int`;
  con `<< 24` y el bit alto en 1 te metés en problemas de signo).
- Un `span` que apunta a un `std::vector` que después crece (`push_back` puede mover la memoria).
- Leer más allá del final "porque el buffer es grande": el largo de lo que llegó es el del span,
  no el de la memoria que hay atrás.

### Cómo lo hacía el original

Leía los campos con punteros y offsets fijos sobre el buffer, sin saber el largo del paquete
([05 § convenciones](../../Server/DataServer/docs/research/05-protocolo.md#convenciones-de-las-tablas)).
Un paquete más corto de lo esperado hacía que el handler leyera basura de mensajes anteriores.

### Para leer

- cppreference: `std::byte`, `std::span`, `std::endian` (`<bit>`, C++20), `std::bit_cast`.
  `std::byteswap` recién llega en C++23.
- Sección de strict aliasing en cppreference ("reinterpret_cast", *type aliasing*).

### Preguntas para chequearte

1. ¿Qué imprime `0x1234` guardado como `u16` little-endian, byte por byte? ¿Y big-endian?
2. Tenés una función que recibe `std::span<const std::byte>`. ¿Quién es dueño de esos bytes? ¿Qué
   pasa si la guardás en un miembro y la usás después?
3. ¿Por qué el resultado de `read_u32_be` es el mismo en una máquina big-endian?

---

## 1.2 El flujo TCP y la máquina de estados

### La idea

- **TCP entrega un flujo de bytes, no mensajes.** Si el otro lado manda dos paquetes de 10 bytes,
  vos podés recibir 20 de una vez, o 3 y después 17, o 1, 1 y 18. El "frame" (el paquete del
  protocolo) lo tenés que reconstruir vos acumulando bytes.
- Un **parser de framing** es una **máquina de estados**: según lo que ya acumuló, sabe si le
  falta la cabecera, si le falta el cuerpo, o si tiene un frame completo para entregar.
- Un buen parser tiene **invariantes** que podés escribir y testear:
  - solo mira bytes que efectivamente llegaron;
  - siempre termina (cada vuelta consume bytes o se detiene);
  - nunca escribe fuera de su buffer;
  - entregar el flujo partido de cualquier forma da los mismos frames.
- **Primero los tests.** Para un parser la tabla de casos *es* la especificación: completo,
  partido en todos los lugares posibles, varios juntos, inválido.

### Ejemplo mínimo (fuera del proyecto)

Un protocolo inventado de líneas con prefijo de largo: `'#' len payload`, con `len` de un byte.

```text
estado "espero #"      → si el byte no es '#': error
estado "espero len"    → guardo len (0 es inválido)
estado "espero cuerpo" → cuando tengo len bytes, entrego el frame y vuelvo a "espero #"
```

Pensá cómo sería con un `len` de **dos** bytes que puede llegar partido entre dos recepciones:
¿en qué estado quedás si llegó solo el primero?

### Errores típicos

- Asumir que un `recv` = un paquete (funciona en loopback con mensajes chicos y falla en la red
  real).
- Leer la cabecera antes de tener todos sus bytes (es exactamente el bug P-1 del original).
- Compactar el buffer moviendo bytes con `memcpy` cuando origen y destino se solapan (hace falta
  `memmove`).
- No tener un tope: un largo enorme hace que el buffer crezca sin límite.

### Cómo lo hacía el original

Leé [04 § el parser](../../Server/DataServer/docs/research/04-red.md#el-parser-0x403010) completo:
la máquina de estados, las propiedades P-1 a P-5 y el
[dominio efectivo del tamaño](../../Server/DataServer/docs/research/04-red.md#dominio-efectivo-del-tamaño).
Es el mejor ejemplo de "qué puede salir mal" que vas a tener.

### Para leer

- Cualquier explicación de "TCP message framing" (prefijo de largo vs delimitador).
- GoogleTest: tests parametrizados (`TEST_P`) para recorrer una tabla de vectores.

### Preguntas para chequearte

1. Te llegan `C1 05` y después nada. ¿Qué sabés y qué no sabés del paquete?
2. ¿Por qué "cortar el flujo en trozos aleatorios y comparar" es un buen test? ¿Qué bug atrapa
   que la tabla fija no?
3. En el original, con `size == 0` el parser retorna sin tocar el acumulador. ¿Qué le pasa a esa
   conexión de ahí en adelante?

---

## 1.3 Layout en memoria y serialización

### La idea

- El compilador puede meter **padding** entre campos de un struct para alinearlos. Un struct con
  un `uint8_t` y un `uint32_t` suele medir 8 bytes, no 5.
- `#pragma pack(1)` (o `__attribute__((packed))`) saca el padding, y
  **`static_assert(sizeof(T) == N)`** hace que el layout sea una propiedad que el compilador
  verifica: si alguien agrega un campo, no compila.
- Aun empaquetado, **no se castea el buffer al struct**: es el mismo problema de aliasing de 1.1,
  más el de endianness. Se **serializa campo por campo**: `Read` toma un lector de bytes y llena
  el struct; `Write` hace lo inverso. El struct empaquetado queda como documentación del layout.
- **Round-trip:** bytes de referencia → `Read` → `Write` → los mismos bytes. Es el test que
  demuestra que entendiste el layout.

### Ejemplo mínimo (fuera del proyecto)

```cpp
#include <cstdint>
#pragma pack(push, 1)
struct WavChunkHeader {        // cabecera de un chunk de un .wav
    char          id[4];       // "fmt "
    std::uint32_t size;        // little-endian
};
#pragma pack(pop)
static_assert(sizeof(WavChunkHeader) == 8);
```

### Errores típicos

- Confiar en el `size` que manda el cliente: un request más corto que su layout tiene que
  rechazarse, no leerse igual.
- Strings de largo fijo sin `\0` garantizado (cuenta 10 bytes, personaje 10, gremio 8): copiar con
  `strcpy` lee de más. Se copian exactamente N bytes y se termina a mano.
- Bytes de relleno sin inicializar en la respuesta: lo que había antes en esa memoria viaja por la
  red.

### Cómo lo hacía el original

Los layouts están en la
[tabla de heads](../../Server/DataServer/docs/research/05-protocolo.md#tabla-de-heads) como
`campo@+offset`: el login `0x00`, por ejemplo, trae la clase en `+3`, el puerto (u16) en `+4`, el
nombre en `+6` (0x30 bytes) y el serverId (u16) en `+0x38`. El nombre del GameServer se guarda en
un campo fijo de 0x30 bytes de la tabla de conexiones
([04 § tabla](../../Server/DataServer/docs/research/04-red.md#la-tabla-de-conexiones)): si ocupa
los 0x30, no queda lugar para el `\0`.

### Para leer

- cppreference: "Objects and alignment", `alignof`, `offsetof`.
- GoogleTest: `EXPECT_EQ` sobre `std::vector<std::byte>` y cómo leer la diferencia.

### Preguntas para chequearte

1. ¿Cuánto mide `struct { uint8_t a; uint32_t b; uint8_t c; }` sin `pack`, en x64? ¿Por qué?
2. Si el struct empaquetado no se usa para leer el buffer, ¿para qué lo querés?
3. ¿Qué tiene que hacer `Read` si el request trae 10 bytes y el layout pide 0x3A?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Decisiones que tomé (y por qué):** tamaño máximo de frame · qué hace el parser ante un error ·
  structs y serialización
- **Preguntas que me quedaron para más adelante:**
