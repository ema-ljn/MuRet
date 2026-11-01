# Ola CL1.0: bytes y archivos

La N.0 de la CL1 del cliente. Antes de leer el primer formato de `Data\` hace falta saber qué es un
byte en la memoria, cómo se arma un entero con varios, y qué pasa cuando un cargador no puede
hacer su trabajo. Es la 1.0 del [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md#10-memoria-y-representación-de-datos)
con material del cliente, más el `Expected` que pide la CL1 (decidido el 2026-11-01). Plan:
[CL1](../superpowers/plans/2026-11-01-ola-cl1-formatos-sin-ventana.md#sub-ola-cl10-bytes-y-archivos-n0-en-labs).

Qué cubre:

- **Teoría:**
  - la memoria como un arreglo de bytes con dirección, y la representación de los objetos;
  - enteros sin signo y en complemento a dos, y la aritmética que da la vuelta;
  - endianness, alineación y relleno;
  - tres formas de reportar un error: códigos, excepciones y tipos resultado.
- **C++:**
  - `<cstdint>`, `std::byte`, arrays de C contra `std::array`, `sizeof` y `alignof`;
  - los casts (`static_cast`, `std::bit_cast`) y por qué `reinterpret_cast` es peligroso;
  - comportamiento indefinido;
  - cadenas de largo fijo, `std::string` y `std::string_view`;
  - una plantilla de clase chica y `std::variant`.
- **Lectura:** *CS:APP* (Bryant y O'Hallaron, 3.ª edición), capítulo 2 y sección 3.9.

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | predecí los primeros bytes de `Text.bmd` |
| 2 | idea | la memoria como un arreglo de bytes; `sizeof` |
| 3 | mirá la memoria | los bytes de un `std::uint32_t` con `std::bit_cast` |
| 4 | idea | endianness |
| 5 | idea | sin signo, complemento a dos y la vuelta de los 32 bits |
| 6 | cazá el bug | armar un entero con `std::byte` |
| 7 | misión | `Labs::ByteView`, desde un test que todavía no compila |
| 8 | comparar | tu `ByteView` contra `std::span`, `std::bit_cast` y `std::endian` |
| 9 | rompelo | castear el buffer a `std::uint32_t*` y leer con UBSan |
| 10 | idea y misión | errores como valor: `Client::Core::Expected` |
| 11 | jefe | Q-C07: el registro de `Text.bmd` que no termina |
| 12 | detective de bytes | el checksum de las tablas, a mano |
| 13 | detective (carril R) | el bucle que copia `Text.bmd` en el `main.exe` original |

Cada actividad termina en una pregunta. Las respuestas no están acá: salen de lo que corrés.

## 1. Calentamiento: los primeros bytes de `Text.bmd`

`Data\Local\Text.bmd` tiene los textos de la interfaz: 2048 registros de 300 bytes, sin cabecera,
pasados por XOR con la clave de tres bytes `FC CF AB` (el byte `i` con `clave[i % 3]`). El
registro 0 está vacío: sus 300 bytes son cero.

**Predecí, antes de mirar:**
- ¿cuánto mide el archivo?
- ¿cuáles son sus primeros seis bytes?

Después comprobalo sobre la carpeta golden con `ls -l` y `xxd -l 16` (en Windows, con
`Format-Hex` en PowerShell).

**Pregunta:** si en vez de cero el registro 0 empezara con la letra `A` (`0x41`), ¿qué byte
verías primero en el archivo?

## 2. Idea: la memoria es un arreglo de bytes

Para el programa, la memoria es una secuencia de bytes, cada uno con su dirección. Un objeto
ocupa `sizeof(T)` bytes seguidos, y su **representación** es lo que hay en esos bytes. Un
`std::uint32_t` ocupa 4; un `char[300]`, 300. El tipo dice cómo leer esos bytes: los mismos 4
bytes son un entero, un `float` o cuatro letras según con qué tipo los mires.

Un archivo es lo mismo pero en disco: bytes sin tipo. Leer un formato es decidir qué tipo tiene
cada tramo, y en qué orden van sus bytes.

## 3. Mirá la memoria: los bytes de un `std::uint32_t`

```cpp
const std::uint32_t itemKey = 0xE2F1;
const auto bytes = std::bit_cast<std::array<std::byte, 4>>(itemKey);
```

Imprimí los cuatro bytes en hexadecimal (`std::to_integer<int>`). Antes de correrlo, predecí el
orden. Después miralos también en el debugger, con la vista de memoria sobre `&itemKey`.

**Pregunta:** `0xE2F1` es la clave del checksum de `Item.bmd`. Si un archivo la guarda como
`F1 E2 00 00`, ¿en qué orden la escribió?

## 4. Idea: endianness

Un entero de varios bytes se puede guardar con el byte menos significativo primero
(**little-endian**, como x86) o con el más significativo primero (**big-endian**). C++20 dice
cuál es el de la máquina en `std::endian::native` (`<bit>`).

Lo que importa es que **el formato fija el orden, no la máquina**. Los archivos de MU son
little-endian: los dwords del checksum y los `int16` de los modelos `.bmd`. El cable usa los dos
órdenes: el tamaño de un C2 va en big-endian (CL2). Un lector que copia los bytes del archivo
tal cual a un entero solo anda en una máquina con el mismo orden que el archivo.

## 5. Idea: sin signo, complemento a dos y la vuelta de los 32 bits

- Un entero **sin signo** de N bits va de 0 a 2^N − 1, y su aritmética es módulo 2^N: sumarle 1
  al máximo da 0. En C++ eso está definido.
- Un entero **con signo** se guarda en complemento a dos (garantizado desde C++20): el mismo
  patrón `FF FF FF FF` es 4.294.967.295 sin signo y −1 con signo. Pero si una suma con signo se
  pasa del rango, es **comportamiento indefinido**: el compilador puede suponer que nunca pasa.
- Las **promociones**: un `std::uint8_t` o un `char` que entra en una cuenta se convierte primero
  a `int`. `b << 24`, con `b` un `std::uint8_t`, se calcula en `int`. Con `b >= 0x80`, el
  resultado no entra en un `int`: antes de C++20 era comportamiento indefinido, y desde C++20 da
  la vuelta (cppreference, "Bitwise shift operators").

El checksum de las tablas suma dwords y espera que den la vuelta: por eso trabaja con
`std::uint32_t`.

**Pregunta:** ¿por qué la aritmética sin signo puede dar la vuelta sin problema y la con signo no?
Pensalo desde lo que el compilador puede suponer al optimizar un `for`.

## 6. Cazá el bug: armar un entero con `std::byte`

Este código quiere leer un `u16` little-endian de dos `std::byte`. Con GCC compila sin warnings,
incluso con los de MuRet:

```cpp
const std::byte low{0xF1};
const std::byte high{0xE2};
const auto value = std::to_integer<std::uint32_t>(low | high << 8);
```

**Predecí qué vale `value`, y después corrélo.** La pista está en qué tipo devuelve el `<<` de
un `std::byte` (cppreference, `std::byte`).

**Pregunta:** ¿en qué momento hay que pasar de `std::byte` a un entero para que el
desplazamiento no pierda bits?

## 7. Misión: `Labs::ByteView`

**El contrato:** una vista de solo lectura sobre bytes que no son suyos (un puntero y un largo),
con lecturas que avanzan un cursor:

| Lectura | Lee | Ejemplo |
| --- | --- | --- |
| `ReadU8` | 1 byte | `E2` → `0xE2` |
| `ReadU16Le` | 2 bytes, el menos significativo primero | `F1 E2` → `0xE2F1` |
| `ReadU32Le` | 4 bytes, el menos significativo primero | `F1 E2 00 00` → `0xE2F1` |
| `ReadU16Be` | 2 bytes, el más significativo primero | `00 05` → `5` (como el tamaño de un C2) |

Una lectura que se pasaría del final **falla sin leer nada**. Cómo lo informa lo decidís vos. Va
en `labs/cl1.0-bytes/`, como toda N.0 (L-4 del camino), y desde la CL0.2 los tests son de
GoogleTest.

**Te toca**, en este orden:

1. Los tests primero (`ByteViewTest.cpp`), con los bytes de la tabla y una lectura que se pasa
   del final. Antes de compilar, decí qué etapa va a fallar.
2. `ByteView.h` y `ByteView.cpp`, con desplazamientos de bits y sin `reinterpret_cast`. Primero
   en rojo, después en verde.
3. Un test con `FF FF FF FF` como `u32le`, el caso de la actividad 5.

**Decisiones que pesás vos:**
- ¿la vista guarda un `const std::byte*` o un `const unsigned char*`?
- ¿qué devuelve una lectura que falla (`std::optional`, un `bool` y un parámetro de salida, o el
  `Expected` de la actividad 10)?
- ¿la vista puede quedar apuntando a un buffer que ya se liberó? ¿De quién es el problema?

## 8. Comparar: lo que ya trae el estándar

L-3 del camino: se construye desde cero, se compara con el estándar y en el producto se usa el
estándar. Para tu `ByteView`, el estándar trae:

- `std::span<const std::byte>`, la misma vista, con `first`, `subspan` y `size`, pero sin
  cursor ni lecturas;
- `std::bit_cast` (`<bit>`), que copia la representación de un objeto a otro tipo del mismo
  tamaño;
- `std::endian`, para saber el orden de la máquina.

`std::byteswap`, que da vuelta los bytes de un entero, es de C++23 y MuRet compila en C++20.

**Pregunta:** con `std::bit_cast` y `std::endian`, ¿cómo leerías un `u32le` en una máquina
big-endian? ¿Te ahorra algo frente a los desplazamientos?

## 9. Rompelo: castear el buffer

```cpp
std::vector<unsigned char> file(16, 0xAB);
const auto* value = reinterpret_cast<const std::uint32_t*>(file.data() + 1);
std::printf("%x\n", *value);
```

En x86 suele imprimir `abababab` y parece que anda. Compilalo en un directorio de build aparte
con `-fsanitize=address,undefined -fno-omit-frame-pointer` (la skill
[`cpp-pro`](../../.claude/skills/cpp-pro/SKILL.md) dice cómo) y corrélo.

**Pregunta:** el reporte habla de alineación. ¿Qué otra regla rompe esa línea aunque la dirección
estuviera alineada? (Buscá "type aliasing" en cppreference.)

## 10. Idea y misión: errores como valor

Un cargador puede no poder hacer su trabajo: el archivo es corto, o el checksum no da. El
original muestra un `MessageBox` y sigue con la tabla en cero (Q-C12). Hay tres formas de
avisarle al que llama:

- **un código de error**, que el que llama puede ignorar sin que nadie se entere;
- **una excepción**, que corta el camino normal y obliga a alguien a atraparla;
- **un tipo resultado**: la función devuelve un valor que tiene adentro el resultado *o* el
  error, y para llegar al resultado hay que mirar primero cuál de los dos es.

C++23 trae `std::expected<T, E>`. MuRet compila en C++20, así que lo escribís vos, chico, como
pide L-3: `Client::Core::Expected<T, E>` en `Client/src/Core/Expected.h`. Es una **plantilla de
clase**: `T` y `E` son tipos que elige el que la usa (`Expected<TextTable, DataError>`). Adentro
alcanza con un `std::variant<T, E>`, que guarda exactamente uno de los dos.

> **Ya está escrito** (2026-11-01, a pedido tuyo, como ejemplo trabajado):
> [`Client/src/Core/Expected.h`](../../Client/src/Core/Expected.h) y sus tests en
> [`Client/tests/core/ExpectedTest.cpp`](../../Client/tests/core/ExpectedTest.cpp). Leelos y
> respondé las dos decisiones de abajo con tus palabras: el comentario del header dice qué se
> eligió, no por qué te convence o no.

**Te tocaba**, tests primero (`Client/tests/core/ExpectedTest.cpp`):
- uno con valor y uno con error, y qué devuelve cada consulta;
- qué pasa si pedís el valor de uno que tiene un error.

**Decisiones que pesás vos:**
- ¿Le copiás la interfaz a `std::expected` (`has_value`, `value`, `error`) o seguís los nombres de
  MuRet (`HasValue`, `Value`, `Error`, regla 7)? Pensá qué pasa el día que el repo pase a C++23.
- ¿Cómo se construye uno con error si `T` y `E` fueran el mismo tipo?

## 11. Jefe: Q-C07, el registro que no termina

En el `Text.bmd` real, el registro 7 ocupa sus 300 bytes sin ningún `\0`
([07](../../Client/docs/research/07-quirks-y-bugs.md), Q-C07). Es el único de los 2048. El
original lo copia a `char g_GlobalText[2048][300]` y después lo usa como una cadena de C.

**Te toca** (`labs/cl1.0-bytes/FixedTextTest.cpp`):

1. Armá un buffer sintético de 8 registros de 300 bytes, con el 7 lleno y sin `\0`.
2. Predecí qué devuelve `std::strlen` sobre el registro 7, y corrélo con ASan. Con el buffer
   terminando justo ahí, ASan lo marca. ¿Qué pasaría en la tabla del original, donde después del
   registro 7 viene el 8?
3. Escribí la lectura de largo fijo que devuelve un `std::string_view` de hasta 300 bytes, cortado
   en el primer `\0` si hay, y el test que muestra que con el registro 7 da 300.

**Pregunta:** el original además pasa algunos textos como formato de `sprintf`. ¿Qué pasa si un
texto tiene un `%s`? ¿Por qué MuRet no lo hace nunca?

## 12. Detective de bytes: el checksum a mano

El checksum de `Item.bmd` y `Skill.bmd`
([05](../../Client/docs/research/05-formatos-de-datos.md#tablas-bmd)) arranca con
`acc = key << 9` y recorre el archivo de a dwords little-endian:

- según la paridad del dword, `acc ^= dword` o `acc += dword` (en `Skill` van con XOR los pares,
  en `Item` los impares);
- en los dwords múltiplos de 4, además, `acc ^= (acc + key) >> ((i & 7) + 1)`.

Calculá a mano, en hexadecimal, el checksum de `Skill` (`key = 0x5A18`) de estos 8 bytes:
`01 00 00 00 FF FF FF FF`. Anotá el resultado: es un test de la CL1.1.

**Pregunta:** el segundo dword se suma. ¿Qué número se le está sumando en realidad, mirado como
entero con signo, y por qué el resultado es el mismo?

## 13. Detective (carril R): el bucle que copia `Text.bmd`

La evidencia del cargador original está en el repo de research,
`client-research/analysis/mainexe/evidence/s31-assets/agentes/005495e0-text-bmd-loader.md`, con
su asm (§5). Las direcciones son del `main.exe` de SHA-256 `2f16a9c6…`
(`analysis/mainexe/originals/main.exe`), por si querés abrirlo en Ghidra. El bucle de la copia:

```text
549680 mov eax,0x76bd1dc
54968c lea esi,[edx+eax] / mov edi,eax
       add eax,0x12c / mov ecx,0x4b / cmp eax,0x77531dc / rep movsd
5496a2 jl 0x54968c
```

**Preguntas:**
- `rep movsd` copia `ecx` dwords. ¿Qué son `0x12c` y `0x4b`, y qué tienen que ver?
- ¿Cuántas vueltas da el bucle hasta `0x77531dc`?
- ¿Por qué la evidencia dice que equivale a un solo `memcpy`?

## El C++ que entra

- **`<cstdint>`:** `std::uint8_t`, `std::uint16_t` y `std::uint32_t` tienen el tamaño exacto que
  dice el nombre; `int` no.
- **`std::byte`:** un byte que no es un número. No se suma; para hacer cuentas, se pasa a entero
  con `std::to_integer` (actividad 6).
- **Arrays:** `std::array<std::byte, 4>` sabe su tamaño y se copia como un valor; un
  `std::byte[4]` se convierte en un puntero apenas lo pasás a una función.
- **Casts:** `static_cast` convierte valores; `std::bit_cast` copia la representación;
  `reinterpret_cast` cambia el tipo de un puntero y casi siempre lleva a comportamiento
  indefinido (actividad 9).
- **`std::string_view`:** una vista a caracteres que no son suyos, con largo. No necesita un `\0`
  al final, y por eso sirve para los textos de largo fijo.
- **Plantillas de clase y `std::variant`:** lo justo para el `Expected` (actividad 10).

## Ejemplo mínimo (fuera del proyecto)

Otra forma de leer un entero de un archivo: copiar sus bytes con `std::memcpy`, que sí está
permitido, a diferencia del cast. El campo es la frecuencia de muestreo de un `.wav`, un `u32`
little-endian en el byte 24 de la cabecera.

```cpp
std::uint32_t SampleRate(const unsigned char* header)
{
    std::uint32_t value = 0;
    std::memcpy(&value, header + 24, sizeof value);
    return value;
}
```

**Pregunta:** esta función devuelve el valor correcto en tu máquina. ¿En cuál no, y por qué la
misión usa desplazamientos en vez de esto?

## Errores típicos

- Castear el buffer a `std::uint32_t*` o a un `struct` (actividad 9).
- Desplazar un `std::byte` y perder los bits (actividad 6).
- Suponer que un texto de largo fijo termina en `\0` (actividad 11).
- Hacer cuentas que tienen que dar la vuelta con un tipo con signo.
- Leer el tamaño de una tabla con `sizeof` de un puntero en vez del array.
- Devolver una vista (`std::span`, `std::string_view`, tu `ByteView`) a un buffer local que muere
  al salir de la función.

## Cómo lo hacía el original

- Leía `Text.bmd` con un solo `fread` de tamaño fijo y no miraba cuánto leyó: un archivo corto
  deja en la tabla lo que había en el heap.
- Pasaba la tabla por XOR3 y la copiaba entera a un array global (`g_GlobalText` en
  `0x076BD1DC`).
- Si faltaba el archivo, mostraba un `MessageBox`, mandaba `WM_DESTROY` y seguía cargando las
  demás tablas (Q-C12).

## Para leer

- *CS:APP*, capítulo 2 (2.1 representación, 2.2 enteros, 2.3 aritmética) y sección 3.9
  (alineación).
- cppreference: "Fixed width integer types", `std::byte`, `std::bit_cast`, `std::endian`,
  "Bitwise shift operators", "Implicit conversions" (promociones), `reinterpret_cast` (la sección
  "Type aliasing") y `std::expected`.

## Preguntas para chequearte

1. ¿Qué cambia entre leer `F1 E2 00 00` como `u32le` y como `u32be`?
2. ¿Por qué un `std::uint32_t` puede dar la vuelta y un `int` no debería?
3. ¿Qué tiene de malo `reinterpret_cast<const std::uint32_t*>(buffer + 1)`, aunque en tu máquina
   ande?
4. ¿Por qué `Text(7)` tiene que devolver 300 caracteres y no "hasta el primer `\0`"?
5. ¿Qué gana el que llama con un `Expected` frente a un `bool` de "salió bien"?
6. Tu `ByteView` y un `std::span`: ¿qué tienen igual y qué agregaste vos?

## Al cerrar la CL1.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Qué devuelve una lectura de `ByteView` que falla, y por qué lo elegí:**
- **La interfaz de mi `Expected`, y por qué:**
- **Qué vi en el jefe (con ASan y sin ASan):**
- **El checksum de la actividad 12:**
- **Preguntas que me quedaron para más adelante:**
