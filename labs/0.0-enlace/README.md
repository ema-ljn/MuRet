# 0.0: del código al programa, resuelta

Ejemplo trabajado de la [guía de la 0.0](../../docs/learning/ola-0.0-del-codigo-al-programa.md).
Lo resolvió Claude a pedido de Emanuel (2026-11-01). Las salidas son reales, con GCC 13.3 y GNU ld
en Linux; el jefe también se probó con Clang 18 y lld. Windows y MSVC, sin probar.

Para que la 0.0 quede cerrada en el [cuaderno](../../docs/learning/PROGRESS.md) falta lo tuyo:
contar con tus palabras las "Preguntas para chequearte" de la guía (nivel 2) y completar su
sección final.

| Archivo | Qué es |
| --- | --- |
| `HeadOffset.h` / `HeadOffset.cpp` | la misión: `Labs::HeadOffset`, en la biblioteca estática `MuRetLabs` |
| `HeadOffsetTest.cpp` | su test (`Lab00.HeadOffset`) |
| `jefe/` | la violación de la ODR y sus dos tests (`Lab00.OdrBoss.AB` y `Lab00.OdrBoss.BA`) |

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --test-dir build/linux-x64-debug --output-on-failure      # 3 tests, verdes en Debug y Release
```

## 1. Calentamiento

| Programa | Etapa que falla | Mensaje |
| --- | --- | --- |
| `a.cpp` (`#include "no_existe.h"`) | preprocesador | `fatal error: no_existe.h: No such file or directory` |
| `b.cpp` (`return Valor;`) | compilador | `error: 'Valor' was not declared in this scope` |
| `c.cpp` (`int Valor();` y la llamada) | linker | `undefined reference to 'Valor()'` |

**Respuesta.** El compilador ve una sola unidad de traducción por vez. La declaración `int Valor();`
le promete que la definición está en otro lado, y eso le alcanza para generar la llamada: deja un
hueco y anota `Valor()` como pedido. Con `g++ -c c.cpp` compila sin quejas y `nm -C c.o` muestra
`U Valor()`. Solo el linker, que junta todos los objetos y bibliotecas, puede saber que nadie la
define.

## 3. Los símbolos del DataServer

```text
                 U std::ios_base_library_init()
                 U std::cout
                 U std::basic_ostream<...>& std::operator<< <...>(std::basic_ostream<...>&, char const*)
0000000000000000 T main
0000000000000051 r std::__detail::__integer_to_chars_is_unsigned<unsigned int>
```

**Respuesta.** `std::cout` lo define `libstdc++.so.6`, una biblioteca dinámica (`ldd` la muestra).
Al enlazar, el linker solo comprueba que esa biblioteca lo exporta y anota la dependencia; el `U` se
resuelve del todo cuando el loader dinámico (`ld.so`) carga el proceso. La `r` minúscula es un
símbolo local: una constante de `<charconv>` con enlace interno.

Detalle fino: `nm -DC` sobre el ejecutable muestra `B std::cout@GLIBCXX_3.4`. En x86-64 el
ejecutable le reserva lugar en su propio `.bss` y el loader copia ahí el objeto de la biblioteca
(una *copy relocation*); la definición sigue siendo de `libstdc++`.

## 5. La misión

Cada paso falló donde decía la guía:

| Paso | Qué falló |
| --- | --- |
| 1, el test registrado, sin header | preprocesador: `fatal error: HeadOffset.h: No such file or directory` |
| 2, con la declaración, sin `.cpp` | linker: `undefined reference to 'Labs::HeadOffset(unsigned char)'` |
| 3 y 4, con un valor equivocado | el test en rojo: `FALLA: C2: el head va en la posicion 3` |
| 4, con el valor bueno | `100% tests passed` |

**Respuesta a la pregunta del paso 2.** Falla el linker, y el nombre del mensaje es
`Labs::HeadOffset(unsigned char)`: el nombre completo con su namespace y el tipo del parámetro,
porque así se decora en C++. Sin `-C`, el símbolo es `_ZN4Labs10HeadOffsetEh`.

**Las decisiones:**

- **Parámetro `std::uint8_t`:** es un byte del cable, de 0 a 255. Las llamadas con `0xC1` no dan
  warning porque la constante entra en el tipo.
- **Resultado `std::optional<std::size_t>`:** `size_t` porque es una posición dentro de un buffer;
  `optional` porque con un primer byte que no es C1 ni C2 no hay respuesta. El parser del original
  descarta todo lo acumulado con ese byte (`04-red.md`, "Máquina de estados"), pero eso le toca al
  parser, no a esta función. Un valor centinela (0 o -1) es peor: quien llama lo puede usar como
  índice sin mirarlo, y `optional` obliga a preguntar.
- **No es `constexpr`:** la misión pide la definición en un `.cpp` adentro de `MuRetLabs`. Una
  función `constexpr` es `inline` sin decirlo y tiene que estar en el header, así que la biblioteca
  no tendría su `T` (pregunta 4 de la guía).
- **Las constantes con nombre** (`HeaderC1`, `HeadOffsetC1`…) son `constexpr` en un namespace
  anónimo: enlace interno.

**`nm -C`:** `libMuRetLabs.a` tiene `T Labs::HeadOffset(unsigned char)` y el objeto del test
`U Labs::HeadOffset(unsigned char)`. Las constantes salen como `r (anonymous namespace)::HeaderC1`
(locales, minúscula). Y aparecen muchas `W` de `std::optional<unsigned long>`: son instancias de
templates, que el compilador emite en cada objeto que las usa y el linker deja una sola. Es el
mismo mecanismo de `inline` que usa el jefe.

## 6. Rompelo

Con la función definida en el header **sin** `inline` e incluida desde dos `.cpp`:

```text
dos.cpp:(.text+0x0): multiple definition of `Labs::HeadOffset(unsigned char)'; uno.cpp:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

Con `inline` enlaza, y `nm -C uno.o` muestra `W Labs::HeadOffset(unsigned char)` en vez de `T`. Con
`static` también enlaza, pero sale `t` (minúscula): cada unidad tiene su copia privada.

**Respuesta.** `inline` no evita que haya dos definiciones: las vuelve legales. El compilador emite
la función en una sección aparte (un grupo COMDAT; en `nm`, la `W` de *weak*) en cada unidad que
la usa, y el linker se queda con una y descarta las demás. Confía en la ODR: supone que son
idénticas y no las compara.

## 8. El `Dataserver.exe` (carril R)

`objdump -h` y `objdump -p` sobre `dataserver/bin/Dataserver.exe` del repo de research (SHA-256
`58daf2c8…`, el mismo del 01):

| Sección | VA | Tamaño virtual | En el archivo | Offset |
| --- | --- | --- | --- | --- |
| `.text` | `0x401000` | `0x2fe77` | `0x30000` | `0x1000` |
| `.rdata` | `0x431000` | `0x8186` | `0x9000` | `0x31000` |
| `.data` | `0x43a000` | `0x6019548` | `0x6000` | `0x3a000` |
| `.rsrc` | `0x6454000` | `0x50da` | `0x6000` | `0x40000` |

ImageBase `0x400000`, entry point `0xFC3E` (relativo a la ImageBase), `SizeOfImage` `0x605a000`
(unos 101 MB) para un archivo de 286.720 bytes.

1. **Los dos datos del 01, corregidos en este PR:**
   - **Imports:** la tabla trae `WS2_32`, `KERNEL32`, `USER32`, `GDI32`, `WINSPOOL.DRV`,
     `ADVAPI32`, `SHELL32`, `COMCTL32` y `ODBC32`. El 01 sumaba `comdlg32` y `WZ_UM.DLL`, que solo
     están como strings en `.rdata` (`WZ_UM.DLL` en el offset `0x31b14` del archivo, al lado de
     `Copy File 'WZ_UM.DLL' failed`; `comdlg32.dll` en `0x38fe6`), y no tenía
     `WINSPOOL.DRV`. `WZ_UM.DLL` la carga en ejecución el auto-update `0x40d900`
     ([02](../../Server/DataServer/docs/research/02-arranque.md)); `KERNEL32` trae `LoadLibraryA` y
     `GetProcAddress`.
   - **`0x43a3b4` no está en `.rdata`:** `.rdata` termina en `0x439186`, y la string de
     `giocp.cpp` está en `.data`, en el offset `0x3a3b4` del archivo (`strings -a -t x`).
2. **El parser `0x403010`:** RVA `0x403010 - 0x400000 = 0x3010`, dentro de `.text` (`0x1000` a
   `0x30e77`). Offset en el archivo: `0x1000 + (0x3010 - 0x1000) = 0x3010`.
3. **MFC estática:** no hay `MFC42.DLL` en los imports; el código de MFC está adentro del `.text`
   (sus nombres de RTTI, como `.?AVCObject@@`, están en las strings).

**Respuesta.** El hueco son las variables globales sin inicializar: unos 96 MiB de duración
estática que arrancan en cero. El archivo no los guarda; el loader reserva esas páginas y las llena
con ceros. Ahí viven las tablas grandes del
[mapa de memoria del 01](../../Server/DataServer/docs/research/01-binario-y-metodo.md#mapa-de-memoria-globales-relevantes)
(`0x063xxxxx` a `0x0644xxxx`, adentro de `.data`, que termina en `0x6453548`), y por eso
`SizeOfImage` mide 101 MB.

Dato extra: `.text` termina en `0x430e77`, pero hay código en el relleno de la página, en
`0x430e90`, `0x430ed0` y `0x430ef0`. Es un *code cave*: `0x409b77` salta a `0x430ef0` (lo documenta
el repo de research, `dataserver/src/reconstructed/docs/reviews/`). Por eso el 01 dice que las
funciones llegan hasta `0x430EF0`.

## 9. El plegado de funciones

**Respuesta.** La identidad de la dirección: dos funciones distintas tienen direcciones distintas,
y un programa lo puede comparar (`&f != &g`, o usar punteros a función como claves). Si el linker
pliega dos funciones en una, esa comparación da igual. Por eso `lld` tiene `--icf=safe`, que solo
pliega las funciones cuya dirección nadie toma. En el `Dataserver.exe` el efecto lo sufre quien lo
lee: un solo cuerpo con dos nombres en Ghidra.

## 10. El jefe

`jefe/OdrA.cpp` y `jefe/OdrB.cpp` definen `inline int Labs::Odr::HeadPosition(std::uint8_t)` con
cuerpos distintos (la B cuenta desde 1). Cada una expone `FromA` o `FromB`. Para un C1:

| Build | Objetos A, B | Objetos B, A |
| --- | --- | --- |
| `-O0` | `FromA=2 FromB=2` | `FromA=3 FromB=3` |
| `-O2` | `FromA=2 FromB=3` | `FromA=2 FromB=3` |

Ni un warning en ningún caso. Los tests `Lab00.OdrBoss.AB` y `.BA` compilan esos objetos con `-O0`
(`/Ob0` en MSVC, sin probar) para que el resultado no dependa de Debug o Release, y comparan la
salida con lo que corresponde a cada orden.

**Respuesta.** El compilador ve una unidad por vez, así que nunca tiene las dos definiciones
juntas. El linker sí ve las dos copias de `HeadPosition`, pero la ODR le dice que son iguales:
se queda con la primera que encuentra y descarta la otra sin compararlas (además, el código
máquina de dos definiciones iguales puede diferir legítimamente con otros flags). Sin optimizar,
las dos unidades llaman al símbolo, y gana la copia del primer objeto. Optimizando, el compilador
copia el cuerpo dentro de `FromA` y de `FromB` (*inlining*), cada uno el de su propia unidad, y la
elección del linker deja de importar para esas llamadas. El mismo programa da tres respuestas
distintas según el orden y los flags: eso es "mal formado, sin diagnóstico".

## 11. Módulos

Opcional, sin hacer.

## Las preguntas para chequearte, resueltas

1. Los headers pegados adentro y las macros resueltas: es el `.cpp` tal como lo ve el compilador.
2. No: un `U` es una promesa. Es un error recién cuando el linker termina y nadie lo definió.
3. `static int x;` tiene enlace interno: es local a su unidad (minúscula en `nm`) y cada `.cpp`
   tiene la suya. `int x;` es global: una sola en todo el programa, y si dos `.cpp` la definen, el
   linker dice "multiple definition".
4. Nada de `HeadOffset`: siendo `constexpr`, es `inline` y vive en el header; se emite como `W` en
   cada objeto que la usa, y la biblioteca se queda sin su `T`.
5. Para detectarla hay que comparar definiciones de unidades distintas. El compilador nunca las ve
   juntas, y el linker ve código máquina que puede diferir sin que haya violación.
6. Si falta una DLL de la tabla de imports, el loader no arranca el proceso: el error sale antes
   de `main`. Si falta una que se pide con `LoadLibrary`, la llamada devuelve `NULL` en ejecución y
   el programa decide qué hacer.
