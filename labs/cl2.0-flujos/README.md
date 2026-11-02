# CL2.0: flujos y bloques, resuelta

Ejemplo trabajado de la [guía de la CL2.0](../../docs/learning/ola-cl2.0-flujos-y-bloques.md). Lo
resolvió Claude porque Emanuel pidió, el 2026-11-02, que Claude escriba toda la CL2 hasta que él
diga que participa. Las salidas son reales, con GCC 13.3 en Linux; Clang 18 compila lo mismo, pero
este entorno no tiene sus bibliotecas de sanitizers. Windows y MSVC, sin probar acá (los corre el
CI).

Para cerrar la CL2.0 en el [cuaderno](../../docs/learning/PROGRESS.md) falta lo tuyo, cuando
entres: contar con tus palabras las "Preguntas para chequearte" de la guía y completar su sección
final.

| Archivo | Qué es |
| --- | --- |
| `FrameCutter.h` / `.cpp` | la misión de la actividad 4: el corte, de a un byte |
| `BitStream.h` / `.cpp` | la misión de la actividad 10: `BitWriter` y `BitReader`, MSB primero |
| `ModularInverse.h` / `.cpp` | la misión de la actividad 8: Euclides extendido |
| `*Test.cpp` | sus tests, de GoogleTest |
| `jefe/OldCutTest.cpp` | el jefe: un modelo del corte del original (Q-C03) y los tres casos que lo rompen |

Las tres misiones van en la biblioteca `MuRetLabsCl20`, y los tests en `LabCl20Tests`, con el
prefijo `LabCl20.` en CTest. Son 33:

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug -R "FrameCutter|BitWriter|BitReader|ModularInverse|OldCut"
```

## 1. Calentamiento

| Después de | Paquetes enteros | Qué queda esperando |
| --- | --- | --- |
| `C1 04 F3` | 0 | `C1 04 F3`: el tamaño es 4, falta 1 byte |
| `00 C2 00` | 1, `C1 04 F3 00` | `C2 00`: falta el segundo byte del tamaño |
| `06 42 0B 30` | 2 | nada |

El tamaño del segundo se sabe con el `06`, en la tercera lectura. **Respuesta.** Con `00 C2` solo
sabrías que el primero terminó y que el segundo es un C2: su cabecera mide 3 y el tamaño va en
dos bytes. Del tamaño, nada.

## 2. El estado después de cada lectura

En la tabla de arriba: después de la primera, el corte espera el cuerpo (`Body`) con 3 bytes;
después de la segunda espera el tamaño (`Size`) con 2; después de la tercera espera un tipo
(`Type`) con 0.

## 3. El caso 1 de `red_reference.py`

Sale `C1 05 10 01 02` con la primera lectura y `C2 00 06 20 03 04` con la segunda. **Respuesta.**
El original guarda los 3 bytes de la cabecera del C2, `C2 00 06`, y los corre al principio del
buffer (`0x4e89ec..0x4e8a05`, un `rep movsd` y un `rep movsb`), donde la lectura siguiente los
completa.

## 4. La misión: `FrameCutter`

Antes de las implementaciones, con los tres `.cpp` vacíos, compilan los tests y falla el linker:
`undefined reference to 'Labs::FrameCutter::Feed(std::byte)'`, y lo mismo para cada función.

**Las decisiones:**

- **Qué devuelve `Feed`:** `std::optional<Frame>`. Casi siempre no hay paquete, y `optional` hace
  que quien llama pregunte antes de usarlo. El error no viaja en el retorno: lo dice `Error()`.
- **Después de un error, el corte se para.** TCP no tiene separadores: después de una cabecera
  mala no se sabe dónde empieza el próximo paquete, y seguir es adivinar (el original descarta el
  buffer y adivina). `Feed` no hace nada desde ahí; quien lo usa cierra la conexión. El test
  `StaysStoppedAfterAnError` lo deja escrito.
- **Dónde van los bytes:** en un `std::vector<std::byte>` del corte, que crece mientras llega el
  paquete. Cuando el último byte llega, sale por movimiento (`std::exchange`) y el corte queda con
  uno vacío: el dueño pasa a ser quien lo recibe, sin copiar.
- **Mínimos:** la cabecera más el opcode, 3 en C1/C3 y 4 en C2/C4. El 0 entra ahí.

**El de propiedad** (`RandomPacketsComeOutInOrder`). 200 paquetes válidos al azar (tipo, tamaño y
cuerpo), uno detrás del otro, salen iguales y en orden. **Respuesta a "Pensá".** Con un corte que
recibe de a un byte, partir el flujo no cambia nada: las llamadas a `Feed` son las mismas, así que
el test no lo parte. Lo que prueba es la variedad de tipos y tamaños (con C2/C4 de más de 255,
para que el byte alto del tamaño no sea siempre 0). Partir sí importa en el `PacketReader` de la
CL2.1, que recibe lecturas enteras.

## 5 y 6. El XOR32

**Respuesta de la 5.** Para descifrar el byte `i` se usan el `i` y el `i - 1` cifrados:
`p[i] = c[i] ^ key[i & 31] ^ c[i - 1]`. Un byte corrompido en el cable rompe 2 bytes al
descifrar, el suyo y el siguiente (1 si era el último). Si es el opcode, sale mal el opcode y el
primer byte cifrado.

**La 6.** `00 ^ 89 ^ F3 = 7A`: `C1 04 F3 00` viaja como `C1 04 F3 7A`, y así está en el cable
([04](../../Client/docs/research/04-protocolo.md)). **Respuesta.** El XOR32 empieza en el primer
byte después del opcode, y el opcode está en el byte 2 de un C1 (cabecera de 2) y en el 3 de un
C2 (cabecera de 3). El opcode queda en claro en los dos.

## 7 y 8. El inverso modular

**Respuesta de la 7.** Multiplicar por `k` y por `d` módulo m devuelve el resto del número
módulo m, no el número: uno igual o mayor que m vuelve achicado. Un carril en claro tiene 16 bits,
hasta `0xFFFF`, y los módulos tienen 17 o 18, desde `0x10000`: todo carril es menor que su módulo
y vuelve igual.

**La misión.** `ModularInverse(3, 7)` da 5; `(6, 9)` y `(0, 7)` no tienen; con m = 0 o 1 tampoco.

**Las decisiones:**

- **Las cuentas en `std::int64_t`.** Euclides extendido deja los coeficientes negativos en la
  mitad de los pasos, así que un tipo sin signo no sirve. El que se devuelve no pasa de m/2 en
  valor absoluto, y cabría en un `int32_t`; lo que no cabe es el resto, que empieza en m (hasta
  32 bits), ni el último coeficiente, que llega a m y se descarta, ni los productos `q × t`. Con
  todo en 64 con signo no hay que pensar cuál entra. Al final, uno negativo se lleva a `0..m-1`
  sumándole m. `WorksWithTheLargestModulus` usa m = `0xFFFFFFFF`, el resto más grande que hay.
- **El producto del test en `std::uint64_t`:** dos números de 18 bits dan hasta 36.

## 9 y 10. El bloque y los bits

**Respuesta de la 9.** 19 bytes útiles son 3 bloques (8 + 8 + 3, el último con relleno): 33
bytes cifrados más los 2 de la cabecera del C3, 35.

**La misión.** Cuatro campos de 18 bits y uno de 16 dan 11 bytes; con `0x3FFFF 0x00001 0x2AAAA
0x15555` y `0xBEEF` son `FF FF C0 00 1A AA A9 55 55 BE EF`. Un carril como lo escribe el
original da `00 00 40` con `0x10000` y `00 00 80` con `0x20000`, lo mismo que `encrypt_block` de
`c3c4_reference.py`, que pasa los 16 bits de abajo y después 2 desde la posición 22 del dword.

**Respuesta.** No es un campo de 18 bits MSB primero: como campo, `0x10000` daría `40 00 00`. Un
round-trip no lo ve, porque un lector con el mismo error recupera el valor igual
(`ALaneIsNotAnEighteenBitField` lo muestra). Lo ve un vector fijo con los bits 16 y 17 prendidos,
y la clave identidad de `mu-client` nunca los prende: por eso el plan pone uno en la CL2.3.

Además, `Write` con más de 32 bits completa con ceros arriba, y `Read` con más de 32 falla:
no entran en el resultado.

## 11. Rompelo

Con `lane` y `key` en variables, GCC 13.3 y `-fsanitize=address,undefined`:

```text
mul.cpp:8:33: runtime error: signed integer overflow: 65535 * 65535 cannot be represented in type 'int'
4294836225
```

**Respuestas:**

- `product` es `int`: los dos `std::uint16_t` se promueven a `int` antes de multiplicar, y
  `0xFFFF * 0xFFFF` se pasa de `INT_MAX`. El número que imprime es el que dejó el hardware, pero
  el estándar no promete ninguno.
- Con `std::uint32_t{lane} * key`, `key` se promueve a `int` y la conversión usual lo lleva a
  `unsigned int`: la cuenta es sin signo, y da `0xFFFE0001`.
- Se pasa cuando el producto supera `2^31 - 1`. Con `lane = 0xFFFF`, desde `key = 0x8001`
  (`0xFFFF * 0x8000` todavía entra). Que una clave tenga 16 bits no alcanza: el producto tiene
  que pasar de `INT_MAX`, y para eso los dos factores tienen que pasar de `0x8000` (con la clave
  en `0xFFFF`, cualquier carril desde `0x8001`). Las del servidor llegan a 16 bits, así que
  puede pasar, y en el descifrado más todavía, porque el carril cifrado tiene hasta 18 bits. Por
  eso los productos van en `std::uint32_t`: no porque entren (18 por 16 bits pueden ser 34), sino
  porque dan la vuelta en 32 bits igual que el `IMUL` del original. Las claves reales cumplen
  `clave de descifrado × (módulo - 1) < 2^32` (`synthetic_key` de `red_reference.py`), así que
  con ellas y un bloque bien cifrado no da la vuelta; con uno roto, cuyo carril pasa del módulo,
  sí. Las dos cosas las prueba la CL2.3.

Con `const` y valores literales, el compilador hace la cuenta al compilar: avisa con `-Woverflow`
y UBSan no ve nada, porque no queda una multiplicación que vigilar.

## 12. Comparar

- **El corte.** El `FrameCutter` se prueba más fácil: cada byte cambia un estado, y no hay bordes
  de lectura que cubrir. El `PacketReader` de la CL2.1 recibe lecturas enteras, así que sus
  tests tienen que partir el flujo en todas partes. Pero hace menos trabajo por byte: lee la
  cabecera una vez y copia el cuerpo entero, en vez de una llamada y un `push_back` por byte.
- **Los bits.** `std::bitset<N>` tiene un largo fijo al compilar, numera los bits desde el menos
  significativo (`b[0]` es el de abajo, y `to_string` empieza por el `N - 1`) y no se pasa a
  bytes. Sirve para mirar bits, no para escribir un flujo MSB primero: le faltan agregar al final
  y sacar los bytes. De `<bit>`, `std::bit_width` dice cuántos bits ocupa un número
  (`bit_width(0x3FFFB)` es 18, el ancho de un carril cifrado) y `std::rotl` y `std::popcount`
  no hacen falta acá. Ninguno escribe un flujo: el `BitWriter` sigue siendo propio, y en el
  producto el empaquetado va dentro del cifrado de la CL2.3.

## 13. El jefe

El modelo (`OldCut`) es un arreglo de `0x2000` con 2 bytes de más: un paquete empieza como mucho
en `0x1FFF`, y el corte lee hasta `offset + 2`. En el original esos dos bytes son el contador de
`+0x4014`, que está justo después del buffer.

| Test | Qué pasa en el original |
| --- | --- |
| `Case1bShiftsTheStreamWhenTheOldByteIsZero` | sale `C1 05 10 01 02` y después `C1 05 06 20 03`, que nadie mandó; el `04` que sobra descarta el buffer. El C2 no sale nunca |
| `AnOldByteOfTwoCutsAPacketShorterThanItsHeader` | con un `02` viejo en la posición 7, sale un "C2 00" de 2 bytes, y lo que sigue se descarta |
| `SizeZeroFillsTheBufferUntilNothingMoreIsRead` | un `C1 00 00` al principio no avanza: el buffer llega a `0x2000`, y ahí lo anota en el log y deja de leer |
| `AnOldByteOfThreeOrMoreWaitsAndCutsRight` | con un `03` viejo, el tamaño no entra: corre `C2 00` al principio y la lectura siguiente saca el C2 bien |
| `WaitsWithLessThanThreeBytesAndDoesNotReadAnEmptyRead` | el caso 2: con menos de 3 bytes no corta; una lectura vacía no lee |

`FrameCutterWaitsForTheMissingBytes` muestra que el `FrameCutter` espera el tercer byte de la
cabecera y saca el C2 entero.

**Respuesta.** El tamaño de un C2 es `(00 << 8) | viejo`, así que da 0 si el byte viejo es 0.
Después de un tamaño 0 el original sale sin correr al principio lo que quedaba: `n` ya está
descontado de los paquetes cortados, pero los bytes siguen en su lugar. La lectura siguiente se
escribe en `buf + n`, y el corte vuelve a empezar en el 0, sobre bytes viejos. Con un viejo de 1
o 2, el tamaño entra en lo que hay y sale un paquete más corto que su cabecera; desde 3, el
tamaño no entra, los dos bytes se corren y la lectura siguiente los completa bien.

## 14. Carril R

En `004e893d_FUN_004e893d.asm` y `004e88a0_FUN_004e88a0.asm` (build `c32bbd5f`):

- **Qué mira antes de leer `[edi+0x1]` y `[edi+0x2]`:** solo el tipo, `[edi]`, contra `C1` a
  `C4` (`0x4e894b..0x4e8959`). No mira cuántos bytes quedan.
- **`[ebx+0x4014]`** es `n`, los bytes del buffer que todavía no se cortaron: `0x4e89b2` le resta
  cada paquete. Con un tamaño que no es mayor que 0, `jle 0x4e8a69` salta a la salida de la
  función: no corre el resto al principio (eso está en `0x4e89dd`) y deja `n` como estaba.
- **Con `n` en `0x2000`,** `0x4e88a0` loguea en `MuError.log` (`0x41fa40` con `0x57630f0`) y
  vuelve con 1 sin llamar a `recv`. El que llama, el WndProc (`0x4af4e0`), ignora ese valor
  (`0x4b3207`: después del `call` salta sin mirar `eax`): no hay cierre, y no se lee más.
- **Por qué un `C1 00` al principio lleva hasta ahí:** cada lectura agrega bytes detrás, el corte
  vuelve a empezar en el 0 y lee el mismo tamaño 0: `n` solo crece. El tamaño 0 del jefe está más
  adelante en el buffer y ya se descontaron los paquetes de antes: la lectura siguiente se escribe
  en otra posición, el corte lee otra cabecera y el flujo se corre en vez de trabarse.
