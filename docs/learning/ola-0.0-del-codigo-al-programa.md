# Ola 0.0: del código al programa

La primera N.0. Va antes de cerrar la 0.1 y antes de la 0.2 (GoogleTest), y es la base de las dos
mitades de MuRet: la 0.2 le suma un target de tests al DataServer, y la CL0.2 del cliente parte
`Client/` en `ClientLib`, `Main` y los tests. En los dos casos son unidades de traducción,
bibliotecas y enlace, que es lo que se ve acá. Acá también nace `labs/`.

**Resuelta como ejemplo trabajado** (a pedido de Emanuel, 2026-11-01): el código está en `labs/` y
las salidas y respuestas de cada actividad en [`labs/0.0-enlace/README.md`](../../labs/0.0-enlace/README.md).
Leé primero la actividad acá, predecí, y recién después mirá la respuesta.

Qué cubre (del [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md#00-del-código-al-programa)):

- **Teoría:** unidades de traducción, símbolos, enlace interno y externo, la regla de una
  definición (ODR), archivos objeto, bibliotecas estáticas y dinámicas.
- **C++:** declaración y definición, headers, `namespace`, `static` e `inline`, `const` y
  `constexpr`, paso por valor y por referencia.
- **Lectura:** *CS:APP* (Bryant y O'Hallaron, 3.ª edición), capítulo 7, "Linking".

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | predecí qué etapa falla en tres programas rotos |
| 2 | idea | unidad de traducción, archivo objeto y tabla de símbolos |
| 3 | mirá la memoria | `nm` sobre el objeto del `main.cpp` del DataServer |
| 4 | idea | enlace externo e interno; la ODR |
| 5 | misión | `Labs::HeadOffset` en `labs/`, desde un test que todavía no compila |
| 6 | rompelo | la misma función definida dos veces |
| 7 | idea | bibliotecas estáticas y dinámicas |
| 8 | detective (carril R) | el formato PE del `Dataserver.exe` original contra el [01](../../Server/DataServer/docs/research/01-binario-y-metodo.md) |
| 9 | cazá el bug del original | el linker de VC6 pliega funciones idénticas |
| 10 | jefe | una violación de la ODR que enlaza sin error y cambia con el orden de enlace |
| 11 | comparar (opcional) | headers contra módulos de C++20 |

Cada actividad termina en una pregunta. Las respuestas no están acá: salen de lo que corrés.

## 1. Calentamiento: ¿qué etapa falla?

Tres programas de un solo archivo. Antes de compilar cada uno, decí qué etapa falla
(preprocesador, compilador o linker) y qué va a decir el mensaje. Después compilalo con
`g++ -std=c++20 a.cpp` (o `cl /std:c++20 a.cpp` en Windows) y compará.

```cpp
// a.cpp
#include "no_existe.h"
int main() { return 0; }

// b.cpp
int main() { return Valor; }

// c.cpp
int Valor();
int main() { return Valor(); }
```

**Pregunta:** `c.cpp` pasa el compilador sin quejas y falla después. ¿Por qué el compilador lo
acepta, si `Valor` no está definida en ningún lado?

## 2. Idea: unidad de traducción, archivo objeto y símbolos

Una **unidad de traducción** es lo que sale del preprocesador para un `.cpp`: el archivo con cada
`#include` pegado adentro y las macros resueltas. El compilador la traduce a un **archivo objeto
reubicable** (`.o`, `.obj`): código máquina con huecos donde van direcciones que todavía no se
conocen, y una **tabla de símbolos**. Cada símbolo es un nombre marcado como *definido acá* o como
*pedido* (se usa, pero lo define otro). El compilador ve una sola unidad por vez; por eso le alcanza
una **declaración** para dejar usar algo. El **linker** junta los objetos, empareja cada símbolo
pedido con una definición (**resolución**) y escribe las direcciones finales en los huecos
(**reubicación**). *CS:APP* 7.1 a 7.5 y 7.7.

## 3. Mirá la memoria: los símbolos del DataServer

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
nm -C build/linux-x64-debug/Server/DataServer/CMakeFiles/DataServer.dir/src/main.cpp.o
```

`-C` traduce los nombres decorados de C++ (*name mangling*) a algo legible. `T` es "definido en la
sección de código", `U` es "pedido y sin definir", y una letra minúscula marca un símbolo local.
En Windows, `dumpbin /symbols` sobre el `.obj` muestra lo mismo con otro formato.

**Pregunta:** `main` sale con `T` y `std::cout` con `U`. ¿Quién define `std::cout`, y en qué momento
se resuelve ese `U`?

## 4. Idea: enlace externo e interno, y la ODR

Cada nombre tiene un **enlace** (*linkage*). Con **enlace externo**, el mismo nombre en dos unidades
es la misma entidad y el linker las une. Con **enlace interno** (`static` en una función o variable
de namespace, o un namespace anónimo), el nombre es privado de su unidad: otra puede usar el mismo
sin chocar. La **regla de una definición** (ODR) dice que una función o variable con enlace externo
tiene exactamente una definición en todo el programa. La excepción es lo `inline` (y los templates):
puede definirse en varias unidades, con la condición de que todas las definiciones sean **idénticas**.
Esa condición no la verifica nadie: si se rompe, el programa está mal formado y el estándar no exige
diagnóstico (*ill-formed, no diagnostic required*). cppreference: "Definitions and ODR", "Storage
class specifiers", "inline specifier".

## 5. Misión: `Labs::HeadOffset`

**El contrato** ([04, "El framing C1/C2"](../../Server/DataServer/docs/research/04-red.md#el-framing-c1c2)):
en un C1 el `head` está en la posición 2; en un C2, en la 3. `Labs::HeadOffset(primerByte)`
devuelve esa posición.

**Lo que ya existe:** [`labs/0.0-enlace/HeadOffsetTest.cpp`](../../labs/0.0-enlace/HeadOffsetTest.cpp),
un test sin GoogleTest: un ejecutable que sale con 0 si todo pasa y con 1 si algo falla, que es lo
que CTest lee. Todavía no compila, a propósito. `labs/CMakeLists.txt` tiene el `TODO(human)` de los
targets.

**Te toca**, en este orden, y antes de cada paso decí qué etapa va a fallar y con qué mensaje:

1. Registrá el test en `labs/CMakeLists.txt` (`add_executable` y `add_test`) y compilá.
2. Escribí `labs/0.0-enlace/HeadOffset.h` con la declaración. Compilá.
3. Escribí `HeadOffset.cpp` con la definición, armá con él la biblioteca estática `MuRetLabs`
   (`add_library(MuRetLabs STATIC ...)`) y enlazá el test contra ella (`target_link_libraries`).
4. Primero hacé que devuelva un valor equivocado y miralo en rojo; después, en verde.
5. Completá el `TODO(human)` del test: un primer byte que no es C1 ni C2.
6. Corré `nm -C` sobre `libMuRetLabs.a` y sobre el objeto del test: ¿dónde está la `T` y dónde
   la `U` de `HeadOffset`?

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --test-dir build/linux-x64-debug --output-on-failure
```

(Los *test presets* llegan en la 0.2; hasta entonces, `--test-dir`.)

**Decisiones que pesás vos:** el tipo del parámetro y el del resultado; si va `constexpr`; y qué
devuelve con un byte inválido, sabiendo qué hace el parser del original con ese byte
([04, "Máquina de estados"](../../Server/DataServer/docs/research/04-red.md#máquina-de-estados)) y
que una función como esta no puede cortar la conexión.

**Pregunta (entre el paso 2 y el 3):** con el header y sin el `.cpp`, ¿qué etapa falla, y qué nombre
aparece en el mensaje?

## 6. Rompelo: dos definiciones

Mové la definición de `HeadOffset` al header, sin `inline`, e incluilo desde dos `.cpp` del mismo
ejecutable (por ejemplo, un segundo archivo de test). Antes de compilar, predecí la etapa y el
mensaje. Después agregale `inline` y mirá `nm -C` otra vez: la letra del símbolo cambia.

**Pregunta:** ¿por qué `inline` lo arregla, si la función sigue definida en dos unidades?

## 7. Idea: bibliotecas estáticas y dinámicas

Una **biblioteca estática** (`.a`, `.lib`) es un archivo de objetos. El linker la recorre y copia al
ejecutable solo los objetos que resuelven algún símbolo pedido hasta ese momento; el resto queda
afuera. Una **biblioteca dinámica** (`.so`, `.dll`) no se copia: el ejecutable guarda su nombre y
los símbolos que necesita de ella, y el **loader** del sistema operativo la carga y los resuelve al
arrancar el proceso. Un programa también puede cargar una a mano, ya corriendo, con `dlopen` o
`LoadLibrary`: esa no figura en ninguna tabla del ejecutable. En Windows, la lista de lo que se
resuelve al cargar es la **tabla de imports** del PE. *CS:APP* 7.6.2, 7.6.3, 7.10 y 7.11.

## 8. Detective (carril R): el `Dataserver.exe` original

El binario está en el repo de research, en `dataserver/bin/Dataserver.exe`. En Linux, `objdump -h`
muestra las secciones y `objdump -p` la cabecera y los imports (binutils lee PE); en Windows,
`dumpbin /headers /imports`.

1. Compará la ImageBase, el entry point, las secciones y los imports con la tabla de
   [identidad del 01](../../Server/DataServer/docs/research/01-binario-y-metodo.md#identidad-del-binario).
   Dos datos de esa tabla no coinciden con el binario: uno en los imports y otro en la columna de
   evidencia. Encontralos (`strings -a -t x` da el offset de cada string en el archivo) y
   corregí el 01 en tu PR.
2. Ubicá el parser (`0x403010`): en qué sección cae y en qué offset del archivo empieza.
3. Confirmá con los imports que MFC 4.2 está enlazada estáticamente, como dice el 01.

**Pregunta:** la sección `.data` ocupa `0x6000` bytes en el archivo, pero la siguiente sección
empieza en `0x06454000`. ¿Qué hay en ese hueco, y qué tiene que ver con las globales del
[mapa de memoria del 01](../../Server/DataServer/docs/research/01-binario-y-metodo.md#mapa-de-memoria-globales-relevantes)?

## 9. Cazá el bug del original: el plegado de funciones

Leé "Una trampa del método" en el [01](../../Server/DataServer/docs/research/01-binario-y-metodo.md#una-trampa-del-método-funciones-plegadas-por-el-linker):
el linker de VC6 deja una sola copia de dos funciones con el mismo código máquina, y Ghidra le pone
el nombre de una a las dos. Es una optimización del linker (`/OPT:ICF` en MSVC, `--icf` en `lld` y
`gold`), no del compilador.

**Pregunta:** ¿qué propiedad de las funciones, que un programa en C++ puede observar, deja de valer
cuando el linker pliega dos en una?

## 10. Jefe: una violación de la ODR que no avisa

Dos unidades del mismo ejecutable definen `inline` una función con el mismo nombre y la misma firma,
pero con cuerpos distintos: por ejemplo, una versión de `HeadOffset` que cuenta las posiciones desde
1. Cada unidad expone una función propia que la llama. Escribí el test que muestra qué devuelve cada
unidad, y armá dos ejecutables con los mismos objetos en distinto orden en `add_executable`.
Compilá en Debug y anotá qué da cada uno; después, en Release.

El test que lo demuestra queda en `labs/` como el registro del jefe, con un comentario que diga qué
viste y por qué.

**Pregunta:** ¿por qué ni el compilador ni el linker avisan, y por qué en Release puede cambiar el
resultado?

## 11. Comparar (opcional): headers y módulos

C++20 trae **módulos** (`export module`, `import`), la respuesta del estándar a los headers. Para
probarlos con CMake hace falta la 3.28 o más nueva, con Ninja 1.11 o Visual Studio 2022; el repo
pide 3.25, así que va como experimento aparte, fuera del build. Anotá qué cambia y qué no.

**Pregunta:** ¿qué problema de los headers resuelve un módulo, y cuál no resuelve (la ODR, por
ejemplo, ¿sigue valiendo)?

## El C++ que entra

- **Declaración y definición:** `int Valor();` declara; `int Valor() { return 1; }` define. Con las
  variables pasa lo mismo: `extern int contador;` declara y `int contador = 0;` define.
- **Headers:** llevan declaraciones (y lo `inline`), con `#pragma once` o una guarda de `#define`.
  Nada de `using namespace` en un header ([CODING_RULES](../CODING_RULES.md), "Namespaces").
- **`namespace`:** la misión va en `Labs::`, con el componente adelante como pide CODING_RULES.
- **`static` e `inline`:** las dos palabras tienen más de un significado. En esta sub-ola importa
  el que tienen sobre el enlace.
- **`const` y `constexpr`:** una variable `const` de namespace tiene enlace interno salvo que sea
  `extern` o `inline`; una función `constexpr` es `inline` sin decirlo.
- **Paso por valor y por referencia:** un byte se pasa por valor. ¿Cuándo conviene `const T&`?
  Pensalo cuando aparezca el primer parámetro grande (la ola 1 trae buffers).

## Ejemplo mínimo (fuera del proyecto)

La forma de una biblioteca chica con su test, en otro dominio (archivos `.wav`). No resuelve la
misión: mirá qué va en cada archivo, no qué hace.

```cpp
// Wav.h: solo declaraciones
#pragma once
namespace Wav
{
unsigned int ChunkSize(const unsigned char* header);
} // namespace Wav

// Wav.cpp: la definición, en la biblioteca
#include "Wav.h"
unsigned int Wav::ChunkSize(const unsigned char* header)
{
    return header[4] | header[5] << 8 | header[6] << 16 | static_cast<unsigned int>(header[7]) << 24;
}
```

`nm -C` sobre el objeto de `Wav.cpp` muestra `T Wav::ChunkSize(unsigned char const*)`; sin `-C`,
el mismo símbolo es `_ZN3Wav9ChunkSizeEPKh`.

## Errores típicos

- Definir una función no `inline` en un header: "multiple definition" en cuanto dos `.cpp` lo
  incluyen.
- "Arreglarlo" con `static`: compila, pero cada unidad tiene su propia copia, y si es una variable,
  su propio estado.
- Escribir el header y olvidar `target_link_libraries`: el compilador está contento y el linker
  dice "undefined reference".
- Con el linker de GNU, poner una biblioteca estática antes del objeto que la usa en la línea de
  comandos: la recorre cuando todavía nadie pidió nada. Con targets de CMake no pasa, pero conviene
  saber por qué.
- Leer `nm` sin `-C` y no reconocer el nombre.

## Cómo lo hacía el original

- Un proyecto de Visual C++ 6 (`.dsp`) con todos los `.cpp` en un solo ejecutable, MFC 4.2
  enlazada estática y las DLL del sistema por la tabla de imports
  ([01](../../Server/DataServer/docs/research/01-binario-y-metodo.md#identidad-del-binario)).
- El linker plegaba funciones idénticas (actividad 9), y eso todavía confunde al leer el binario.

## Para leer

- *CS:APP*, capítulo 7: 7.1 a 7.7 para la misión, 7.6.1 para el jefe, 7.10 y 7.11 para las
  dinámicas.
- cppreference: "Translation phases", "Definitions and ODR", "Storage class specifiers",
  "inline specifier".
- Microsoft Learn, "PE Format", para la actividad 8.

## Preguntas para chequearte

1. ¿Qué tiene una unidad de traducción que no tiene el `.cpp`?
2. Un `U` en la tabla de símbolos de un objeto, ¿es un error? ¿Cuándo pasa a serlo?
3. Para el linker, ¿qué diferencia hay entre `static int x;` e `int x;` en un `.cpp`?
4. Si `HeadOffset` fuera `constexpr` en el header, ¿qué quedaría adentro de `libMuRetLabs.a`?
5. ¿Por qué una violación de la ODR puede no tener diagnóstico?
6. Falta una DLL de la tabla de imports, o falta una que el programa pide con `LoadLibrary`: ¿qué
   pasa en cada caso, y en qué momento?

## Al cerrar la 0.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Los dos datos del 01 que corregí y cómo lo comprobé:**
- **Qué devuelve `HeadOffset` con un byte inválido, y por qué:**
- **Qué vi en el jefe (Debug y Release):**
- **Preguntas que me quedaron para más adelante:**
