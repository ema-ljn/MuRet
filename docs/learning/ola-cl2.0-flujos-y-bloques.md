# Ola CL2.0: flujos y bloques

La N.0 de la CL2 del cliente. Antes de cortar, cifrar y descifrar los paquetes de `main.exe` hace
falta saber por qué TCP no entrega mensajes, cómo se escribe un corte que no lee bytes que no
llegaron, y la matemática y los bits del cifrado C3/C4. La parte del flujo TCP es la 1.2 de la
[guía de la ola 1 del servidor](ola-1-protocolo.md#12-el-flujo-tcp-y-la-máquina-de-estados), acá
con material del cliente. Plan (tiene las respuestas: abrilo después):
[CL2](../superpowers/plans/2026-11-02-ola-cl2-protocolo-sin-red.md#sub-ola-cl20-flujos-y-bloques-n0-en-labs).

Qué cubre:

- **Teoría:**
  - TCP como flujo de bytes, y el corte como máquina de estados con sus invariantes;
  - un cifrado de flujo encadenado (el XOR32) y uno por bloques (`SimpleModulus`), y por qué
    los dos son ofuscación y no seguridad;
  - aritmética modular: multiplicar módulo m, el inverso modular y Euclides extendido;
  - campos de bits empaquetados MSB primero.
- **C++:**
  - operadores de bits sobre `std::byte` y `std::to_integer`;
  - la aritmética sin signo que da la vuelta, el desborde con signo que es comportamiento
    indefinido, y la promoción de `std::uint16_t` a `int`;
  - `enum class` para los estados y `std::optional` para "no hay resultado";
  - `std::span` de largo fijo;
  - `std::mt19937` con semilla fija para un test de propiedad.
- **Lectura:** *An Introduction to Mathematical Cryptography* (Hoffstein, Pipher y Silverman,
  2.ª edición), secciones 1.2 y 1.3; *CS:APP*, sección 2.3, de repaso.

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | predecí cuántos paquetes hay después de cada lectura |
| 2 | idea | TCP es un flujo; el corte es una máquina de estados |
| 3 | predecí | el caso 1 de `red_reference.py`, partido en dos lecturas |
| 4 | misión | `Labs::FrameCutter`, desde un test que todavía no compila |
| 5 | idea | el XOR32: un cifrado encadenado, y por qué es ofuscación |
| 6 | detective de bytes | un byte del XOR32, a mano |
| 7 | idea | aritmética modular y el inverso |
| 8 | misión | `Labs::ModularInverse` con Euclides extendido |
| 9 | idea | el bloque de `SimpleModulus`: 8 bytes dan 11 |
| 10 | misión | `Labs::BitWriter` y `Labs::BitReader`, MSB primero |
| 11 | rompelo | `std::uint16_t * std::uint16_t` con UBSan |
| 12 | comparar | tu corte contra el `PacketReader`, y tus bits contra `<bit>` |
| 13 | jefe | Q-C03: el corte del original sobre un buffer que se reusa |
| 14 | detective (carril R) | las lecturas de la cabecera en `0x4e893d` |

Cada actividad termina en una pregunta. Las respuestas no están en la guía: salen de lo que
corrés. El plan, 03 y 04 tienen algunas, así que abrilos después de predecir.

## 1. Calentamiento: dos lecturas

Por una conexión llegan dos paquetes, `C1 04 F3 00` y `C2 00 06 42 0B 30`. Del lado del que
recibe, el primer `recv` devuelve `C1 04 F3`, el segundo `00 C2 00` y el tercero `06 42 0B 30`.

**Predecí, antes de seguir:**
- ¿cuántos paquetes enteros hay después de cada lectura?
- ¿en qué momento sabés cuánto mide el segundo paquete?

**Pregunta:** si el segundo `recv` hubiera devuelto solo `00 C2`, ¿qué sabrías del segundo
paquete, y qué no?

## 2. Idea: TCP es un flujo, y el corte es una máquina de estados

TCP entrega bytes en orden, sin perder ninguno, pero no respeta dónde terminaba cada `send`: dos
paquetes pueden llegar en un `recv`, o uno partido en tres. Cortarlos es trabajo del que recibe.

En MU el corte se lee de la cabecera: `C1` y `C3` llevan el tamaño en el byte 1; `C2` y `C4`, en
los bytes 1 y 2, big-endian. El tamaño es el del paquete entero. Un **corte** es una máquina de
estados (un autómata finito): según lo que ya juntó, le falta el tipo, el tamaño o el cuerpo, o
tiene un paquete para entregar. Sus invariantes son los de la 1.2 del servidor: solo mira bytes
que llegaron, nunca escribe fuera de su buffer, siempre avanza o se detiene, y el mismo flujo
partido de cualquier forma da los mismos paquetes.

**Pregunta:** en la actividad 1, ¿en qué estado queda el corte después de cada `recv`, y qué bytes
tiene guardados?

## 3. Predecí: el caso 1 de `red_reference.py`

`client-research/analysis/mainexe/scripts/red_reference.py` es un modelo en Python de cómo
recibe el original. Su caso 1 manda `C1 05 10 01 02` y los tres primeros bytes de
`C2 00 06 20 03 04` en una lectura, y los otros tres en la siguiente.

**Predecí:** ¿qué paquetes salen después de la primera lectura, y cuáles después de la segunda?
Después corré `python3 red_reference.py --selftest` y leé el caso 1 en `selftest()`. Solo el 1:
el 1b, que está abajo, es el jefe (actividad 13), y conviene predecirlo antes de abrirlo.

**Pregunta:** ¿qué bytes guarda el corte entre una lectura y la otra, y dónde los pone?

## 4. Misión: `Labs::FrameCutter`

**El contrato:** se alimenta de a un byte (`Feed(std::byte)`) y devuelve un paquete entero
cuando lo tiene. Los estados van explícitos en un `enum class`: esperando el tipo, el tamaño o
el cuerpo. Un tipo que no es `C1` a `C4`, o un tamaño menor que la cabecera y el opcode, es un
error. Va en `labs/cl2.0-flujos/`.

**Te toca**, en este orden:

1. GoogleTest en `labs/`: es andamiaje, como el paso 1 de la CL1.0, y lo agrega Claude si
   todavía no está.
2. Los tests primero (`FrameCutterTest.cpp`), con los casos de `red_reference.py --selftest`
   que no son del cifrado: corte, resto parcial, cabecera incompleta, cabecera inválida y tamaño
   0. Antes de compilar, decí qué etapa va a fallar.
3. Uno de propiedad: paquetes válidos al azar (tipo, tamaño y cuerpo), uno detrás del otro,
   salen iguales y en orden. Usá `std::mt19937` con una semilla fija y tomá su salida
   directamente (`engine() % n`): el estándar fija los números del motor, pero no los de
   `std::uniform_int_distribution`, que cambian entre MSVC y libstdc++. **Pensá:** ¿sirve partir
   el flujo al azar en lecturas, si el corte recibe de a un byte?
4. `FrameCutter.h` y `FrameCutter.cpp`, primero en rojo y después en verde.

**Decisiones que pesás vos:**
- ¿qué devuelve `Feed` cuando todavía no hay paquete, y cuando hay un error?
- después de un error, ¿el corte sigue o se queda en ese estado?
- ¿dónde guardás los bytes del paquete que se está armando, y quién es su dueño cuando sale?

## 5. Idea: el XOR32, un cifrado encadenado

Casi todo C1/C2 que manda el cliente (los `F4` al ConnectServer no) pasa por el XOR32
([03](../../Client/docs/research/03-red-y-cifrado.md#envío-el-motor-c1c2-y-el-xor32), después
de la actividad 6: su ejemplo es la respuesta): cada byte
que va después del opcode se cifra con `valor ^= key[i & 31] ^ buf[i - 1]`, con una tabla de 32
bytes y el byte anterior **ya cifrado**. Es un cifrado de flujo **encadenado**: cada byte depende
de todos los anteriores.

Pero la tabla está en el `main.exe`, y es la misma para todos los clientes: cualquiera que tenga
el binario descifra lo que manda. Eso es **ofuscación**, no seguridad. Lo mismo pasa con las
claves de `SimpleModulus`, que viajan con el cliente en `Data\Enc1.dat` y `Data\Dec2.dat`.

**Pregunta:** si en el cable se corrompe un byte de un paquete con XOR32, ¿cuántos bytes salen
mal al descifrarlo? Pensalo desde qué usa el descifrado de cada byte.

## 6. Detective de bytes: un byte del XOR32

La tabla empieza con `e7 6d 3a 89 bc b2 9f 73`. El cliente manda `C1 04 F3 00`: el opcode `F3`
va en claro y el byte 3 se cifra con `key[3]` y con el opcode.

**Calculá a mano**, en hexadecimal, qué byte sale en el lugar del `00`. Después buscá ese paquete
en el research ([04](../../Client/docs/research/04-protocolo.md)): está en el cable.

**Pregunta:** en un C1 el XOR32 empieza en el byte 3, y en un C2, en el 4. ¿Por qué no empiezan
en el mismo índice?

## 7. Idea: aritmética modular y el inverso

`a mod m` es el resto de dividir `a` por `m`, y en aritmética módulo m todo da la vuelta en m:
con m = 11, `2 * 6 = 12`, y `12 mod 11 = 1`. Cuando `(k * d) mod m = 1`, `d` es el **inverso** de
`k` módulo m: multiplicar por `k` y después por `d` devuelve el número, con una condición.

No todo número tiene inverso: `k` lo tiene si y solo si `k` y `m` no tienen otro divisor en común
que el 1 (su máximo común divisor es 1). **Euclides extendido** calcula ese divisor y, de paso, dos
enteros `x` e `y` con `k * x + m * y = mcd(k, m)`. Si el mcd es 1, `x` (llevado a `0..m-1`) es el
inverso.

En `SimpleModulus`, cada carril se cifra multiplicando por una clave módulo m, y se descifra
multiplicando por su inversa. Por eso la clave de cifrado del cliente (`Enc1.dat`) es la inversa
de la de descifrado del servidor (`Dec1.dat`), con el mismo módulo.

**Pregunta:** ¿qué número no vuelve igual después de multiplicar por `k` y por `d` módulo m? Un
carril en claro tiene 16 bits y los módulos tienen 17 o 18: ¿le puede pasar a un carril?

## 8. Misión: `Labs::ModularInverse`

**El contrato:** `ModularInverse(a, m)` devuelve el inverso de `a` módulo `m`, o `std::nullopt`
si no hay. Va en `labs/cl2.0-flujos/`.

**Te toca**, tests primero:

1. Uno a mano: calculá con papel el inverso de 3 módulo 7 y escribilo como test.
2. Uno sin inverso: elegí un `a` y un `m` que compartan un divisor.
3. Uno de propiedad: para un módulo de 18 bits, varios `a` al azar (con semilla fija) cumplen
   `(a * inverso) mod m = 1`, o no tienen inverso y su mcd con `m` no es 1.
4. La implementación, primero en rojo y después en verde.

**Decisiones que pesás vos:**
- Euclides extendido va dejando coeficientes negativos. ¿Con qué tipo los guardás, y cómo
  llevás el resultado a `0..m-1`?
- con un módulo de 18 bits, ¿en qué tipo hacés el producto del test para que no se pase?

## 9. Idea: el bloque de `SimpleModulus`

El cifrado C3/C4 ([03](../../Client/docs/research/03-red-y-cifrado.md#c3c4-cifrado-por-bloques-con-enc1dat-y-dec2dat))
toma 8 bytes en claro y da 11 cifrados:

- los 8 bytes son cuatro **carriles** de 16 bits little-endian;
- cada carril pasa por un XOR (con su clave XOR y el carril anterior ya cifrado), se multiplica
  por su clave módulo su módulo y queda con hasta 18 bits; una segunda pasada, del carril 2 al 0,
  le aplica a cada uno otro XOR (su clave XOR y el carril siguiente), y el 3 queda igual;
- los cuatro carriles ocupan 4 × 18 = 72 bits, y dos bytes de cola (el largo útil y un
  checksum) suman 16: 88 bits, que son 11 bytes;
- si el último bloque no tiene 8 bytes, se completa con relleno, y la cola dice cuántos eran
  útiles.

**Pregunta:** un C1 de 20 bytes se cifra desde su byte 1 (el serial en lugar del tamaño), así que
lleva 19 bytes útiles. ¿Cuántos bloques son, y cuánto mide el C3 que sale?

## 10. Misión: `Labs::BitWriter` y `Labs::BitReader`

**El contrato:** `Write(value, bits)` agrega los `bits` de abajo de `value` al flujo, el más
alto primero; `Read(bits)` los lee en el mismo orden. Van en `labs/cl2.0-flujos/BitStream.h` y
`BitStream.cpp`. Los bits se escriben **MSB primero**: el primer bit del flujo es el `0x80` del
byte 0, el segundo el `0x40`, y un campo puede empezar en la mitad de un byte y terminar en el
siguiente.

**Te toca**, tests primero:

1. El primer bit escrito es el `0x80` del byte 0.
2. Cuatro campos de 18 bits y uno de 16 ocupan justo 11 bytes y se leen iguales.
3. Un carril como lo escribe el original: sus dos bytes de abajo, de a 8 bits empezando por el
   de abajo, y después los 2 bits que quedan arriba, como un campo de 2 bits. **Predecí** qué 3
   bytes dan `0x10000` y `0x20000` antes de correrlo. Después compará tu predicción con
   `encrypt_block` de `c3c4_reference.py` (al lado de `red_reference.py`), que es el oráculo.
4. La implementación.

**Pregunta:** en el punto 3, ¿un carril queda escrito como un campo de 18 bits MSB primero? Si
no, ¿qué test de round-trip no se daría cuenta de que los dos bits de arriba están al revés?

## 11. Rompelo: `std::uint16_t * std::uint16_t`

```cpp
std::uint16_t lane = 0xFFFF;
std::uint16_t key = 0xFFFF;
const auto product = lane * key;
std::printf("%u\n", static_cast<unsigned>(product));
```

Parece una multiplicación sin signo. Compilalo en un directorio de build aparte con
`-fsanitize=address,undefined -fno-omit-frame-pointer` (la skill
[`cpp-pro`](../../.claude/skills/cpp-pro/SKILL.md) dice cómo) y corrélo.

**Preguntas:**
- ¿de qué tipo es `product`? (Buscá "integral promotion" en cppreference.)
- ¿qué cambia con `std::uint32_t{lane} * key`?
- ¿con qué valores de `lane` y `key` la cuenta se pasa de un `int`? Las claves del servidor
  llegan a 16 bits: ¿alcanza con eso, o depende de cuánto vale la clave?

## 12. Comparar: lo que ya trae el estándar, y lo que va al producto

L-3 del camino: se construye desde cero, se compara y en el producto se usa el estándar. Para el
corte, el estándar no trae nada: la comparación es con el `PacketReader` de la CL2.1, que es el
que va a `ClientLib` (cuando esté la CL2.1). Para los bits, `<bit>` trae `std::bit_width`,
`std::rotl` y `std::popcount`, y `std::bitset` guarda bits de largo fijo.

**Te toca:** anotarlo en `labs/README.md`.

**Preguntas:**
- tu `FrameCutter` recibe de a un byte y el `PacketReader` recibe lecturas enteras. ¿Cuál es más
  fácil de probar, y cuál hace menos trabajo por byte?
- ¿te sirve `std::bitset` para escribir un flujo de bits MSB primero? ¿Qué le falta?

## 13. Jefe: Q-C03, el corte que lee bytes viejos

El original corta sobre un buffer de `0x2000` bytes que **se reusa** entre lecturas
([07](../../Client/docs/research/07-quirks-y-bugs.md), Q-C03). Cuando un paquete queda partido,
mueve lo que sobra al principio del buffer, pero lee la cabecera sin mirar cuántos bytes quedan:
si solo llegaron los dos primeros bytes de un C2, el tercero es lo que había en el buffer de
antes.

**Te toca** (`labs/cl2.0-flujos/jefe/OldCutTest.cpp`):

1. Un modelo chico del corte del original, con el buffer que se reusa, que lea `buf[off + 1]` y
   `buf[off + 2]` sin mirar el largo (como `_split` de `red_reference.py`). Que sea un arreglo
   fijo con un par de bytes de más al final: leer fuera de un `std::vector` del largo justo es
   comportamiento indefinido, y el modelo lo haría.
2. El caso 1b de `red_reference.py`: `C1 05 10 01 02` y `C2 00 06 20 03 04`, partidos después de
   `C2 00`. **Predecí** qué paquetes saca el original, y escribilo como test.
3. El mismo flujo en tu `FrameCutter`, y el test que muestra que espera los bytes que faltan.

**Pregunta:** ¿qué tiene que pasar con el byte viejo para que el tamaño dé 0, y qué hace el
original después de un tamaño 0 que no está al principio de la lectura?

## 14. Detective (carril R): la cabecera en `0x4e893d`

El corte del `main.exe` (`c32bbd5f`) está en el repo de research,
`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/004e893d_FUN_004e893d.asm`, y en
Ghidra, abriendo `mainexe-vmxp-c32bbd5f/original/main.exe`. Su análisis, en
`analysis/mainexe/evidence/s35-red/agentes/004e893d-corte-paquetes.md`, es del build histórico
`2f16a9c6`, con las mismas direcciones que este listado. El tamaño de un C2/C4 y el de un C1/C3:

```text
4e895f mov cl,[edi+0x1]
4e8962 mov al,[edi+0x2]
4e8965 mov esi,ecx / shl esi,0x8 / add esi,eax
4e8970 mov cl,[edx+ebx+0x2015]
4e8977 mov esi,ecx
4e8979 test esi,esi
4e897b jle 0x4e8a69
4e8981 cmp esi,[ebx+0x4014]
```

Y al principio de la lectura, en `004e88a0_FUN_004e88a0.asm`:

```text
4e88a9 mov eax,[ebx+0x4014]
4e88af cmp eax,0x2000
4e88b4 jl 0x4e88d4
```

**Preguntas:**
- `edi` apunta al paquete. ¿Qué mira el código antes de leer `[edi+0x1]` y `[edi+0x2]`?
- ¿Qué es `[ebx+0x4014]`, y qué pasa con un tamaño que no es mayor que 0?
- ¿Qué pasa cuando `[ebx+0x4014]` llega a `0x2000`? ¿Por qué un `C1 00` al principio de una
  lectura lleva hasta ahí, y el tamaño 0 del jefe no?

## El C++ que entra

- **`std::byte` y sus operadores:** `&`, `|`, `^`, `~`, `<<` y `>>` funcionan; `+` y `*` no. El
  desplazamiento de un `std::byte` sigue siendo un `std::byte` y pierde los bits que se salen
  (actividad 6 de la CL1.0): para armar un número, primero `std::to_integer`.
- **Sin signo y con signo:** un `std::uint32_t` da la vuelta módulo 2³², y eso está definido; un
  `int` que se pasa de rango es comportamiento indefinido.
- **Promociones:** un entero más chico que `int` que entra en una cuenta no se queda con su
  tipo (actividad 11, y la CL1.0).
- **`enum class`:** los estados del corte, sin conversión implícita a entero. Con un `switch` sin
  `default`, GCC y Clang avisan si falta un estado (`-Wswitch`); MSVC con `/W4`, no.
- **`std::optional<T>`:** un valor o nada, para "no hay inverso" o "todavía no hay paquete".
- **`std::span<const std::byte, 8>`:** una vista de largo fijo; desde un `std::span` dinámico se
  pasa con `first<8>()`. Si el dinámico tiene menos de 8, es comportamiento indefinido: es la
  trampa del último bloque, que puede venir incompleto.
- **`std::mt19937` con semilla fija:** la misma secuencia con cualquier biblioteca estándar; las
  distribuciones, no (actividad 4).

## Ejemplo mínimo (fuera del proyecto)

Un color RGB565 empaqueta tres campos en 16 bits: 5 de rojo, 6 de verde y 5 de azul, el rojo
arriba.

```cpp
std::uint16_t PackRgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
```

**Pregunta:** si escribieras estos tres campos con tu `BitWriter` (5, 6 y 5 bits), ¿los dos bytes
que salen son los de `PackRgb565` en big-endian o en little-endian?

## Errores típicos

- Suponer que un `recv` es un paquete (actividad 1).
- Leer el tamaño de un C2 antes de que lleguen sus tres bytes de cabecera (el jefe).
- Descifrar el XOR32 encadenando con el byte equivocado.
- Multiplicar dos `std::uint16_t` y creer que la cuenta es sin signo (actividad 11).
- Elegir el tipo de los coeficientes de Euclides extendido sin mirar qué valores toman.
- Probar los bits solo con round-trips (actividad 10).
- Un test al azar con `std::uniform_int_distribution`, que da otros números con otra biblioteca
  estándar.

## Cómo lo hacía el original

- Recibía en un buffer de `0x2000` bytes por conexión y cortaba sin mirar cuántos bytes
  quedaban para la cabecera (Q-C03). Con un tipo inválido, descartaba todo lo acumulado, también
  los paquetes buenos que venían detrás.
- Con un tamaño 0 no avanzaba ni descartaba, y no corría al principio lo que sobraba: según
  dónde estuviera ese tamaño en el buffer, el flujo se trababa o se corría (actividades 13 y 14).
- Tenía el XOR32 copiado en cada función que arma un paquete, no en un solo lugar.
- Guardaba las claves de `SimpleModulus` en archivos, con un XOR de una tabla que está en el
  binario, y descifraba en un buffer de la pila más chico que el C4 más grande que acepta
  (Q-C05).

## Para leer

- Hoffstein, Pipher y Silverman, *An Introduction to Mathematical Cryptography*, 1.2
  (divisibilidad y Euclides extendido) y 1.3 (aritmética modular e inversos).
- *CS:APP*, 2.3 (aritmética de enteros), de repaso.
- La 1.2 de la [guía de la ola 1 del servidor](ola-1-protocolo.md#12-el-flujo-tcp-y-la-máquina-de-estados).
- cppreference: `std::byte`, "Implicit conversions" (promociones), "Arithmetic operators",
  `std::optional`, `std::span`, `std::mersenne_twister_engine` y `<bit>`.

## Preguntas para chequearte

1. ¿Por qué un `recv` no es un paquete, y qué invariantes cumple un corte bien hecho?
2. ¿Qué diferencia hay entre un cifrado de flujo encadenado y uno por bloques?
3. ¿Por qué el XOR32 y `SimpleModulus` no protegen el login de nadie que tenga el binario?
4. ¿Cuándo tiene inverso un número módulo m, y cómo lo encuentra Euclides extendido?
5. ¿Por qué la clave de cifrado del cliente es la inversa de la de descifrado del servidor?
6. ¿Qué es escribir bits MSB primero? ¿Un carril del original es un campo de 18 bits MSB primero,
   y qué vector lo prueba?
7. ¿Qué pasa con `std::uint16_t * std::uint16_t` cuando los dos valen `0xFFFF`?

## Al cerrar la CL2.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Qué devuelve mi `FrameCutter` sin paquete y con un error, y por qué:**
- **Con qué tipos escribí Euclides extendido, y por qué:**
- **Qué vi en el jefe, y en qué cambia mi corte:**
- **Lo que anoté al comparar con el `PacketReader` y con `<bit>`:**
- **Preguntas que me quedaron para más adelante:**
