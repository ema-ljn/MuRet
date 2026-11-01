# Guía de la ola 0: Fundaciones

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito: muestran el
> concepto, no la solución de MuRet. Lo que hay que hacer, paso por paso, está en el
> [plan de la ola 0](../superpowers/plans/2026-10-26-ola-0-fundaciones.md). La sección final la
> completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 0.0 | del código al programa: unidades de traducción, símbolos, enlace, ODR | qué hay en un archivo objeto, por qué un símbolo queda sin definir o definido dos veces, y qué cambia entre una biblioteca estática y una dinámica (guía aparte: [ola-0.0-del-codigo-al-programa.md](ola-0.0-del-codigo-al-programa.md)) |
| 0.1 ✅ | cómo se arma un programa C++ y qué hace CMake | preprocesador → compilador → linker, y qué es un *target* (nota aparte: [ola-0.1-cmake.md](ola-0.1-cmake.md)) |
| 0.2 | dependencias, tests y CI | qué resuelve vcpkg, qué es un *test runner*, y qué significa que CI esté verde |
| 0.3 | documentación base | por qué una decisión se escribe antes de que haya mucho código, y para qué sirve un formateador |

---

## 0.1 CMake ✅

Ya está hecha. El concepto y las preguntas pendientes están en [ola-0.1-cmake.md](ola-0.1-cmake.md#te-toca):
compilar en Windows con el preset `windows-x64` y contestar las tres preguntas.

---

## 0.2 Dependencias, tests y CI

### La idea

- **Gestor de dependencias.** En C++ no hay un gestor "oficial" como `npm` o `pip`. **vcpkg** baja,
  compila e instala bibliotecas para tu compilador y tu sistema. En **modo manifiesto** las
  dependencias se declaran en un `vcpkg.json` en la raíz del repo, y CMake las instala solo al
  configurar si le pasás el *toolchain file* de vcpkg.
  - El *toolchain file* se lee **en el primer configure** y queda en la caché de CMake. Si lo
    agregás después, hay que configurar de cero.
  - Un `builtin-baseline` (un commit de vcpkg) fija las versiones: el mismo repo compila igual hoy
    y dentro de un año.
- **Test unitario.** Un programa chico que llama a tu código con entradas conocidas y verifica las
  salidas. **GoogleTest** te da las macros (`TEST`, `EXPECT_EQ`, `ASSERT_TRUE`) y un `main` que
  corre todos los tests y reporta (`GTest::gtest_main`).
  - `EXPECT_*` anota el fallo y **sigue**; `ASSERT_*` anota el fallo y **corta** ese test. Se usa
    `ASSERT` cuando seguir no tiene sentido (por ejemplo, un puntero nulo que el resto del test
    usaría).
  - **Rojo → verde:** un test que nunca viste fallar puede no estar probando nada. Primero lo ves
    rojo, después escribís lo que lo pone verde.
- **CTest** es el *test runner* de CMake: sabe qué ejecutables de test hay y los corre todos con un
  comando (`ctest`). `gtest_discover_tests` le cuenta a CTest cada `TEST` por separado, así podés
  filtrar con `-R` y ver cuál falló.
- **Integración continua (CI).** Cada push o PR dispara una compilación y los tests en máquinas
  limpias. En GitHub Actions eso es un archivo YAML en `.github/workflows/`, con una **matriz**
  (Windows y Linux) que corre el mismo trabajo en cada sistema. "Verde" quiere decir que compiló y
  pasaron los tests **en máquinas que no son la tuya**.

### Ejemplo mínimo (fuera del proyecto)

Un test de GoogleTest para una función de años bisiestos, escrita mal a propósito:

```cpp
#include <gtest/gtest.h>

bool is_leap(int year) {
    return year % 4 == 0;                       // le falta la regla de los siglos
}

TEST(LeapYear, DivisibleByFour) { EXPECT_TRUE(is_leap(2024)); }
TEST(LeapYear, CenturyIsNot)    { EXPECT_FALSE(is_leap(1900)); }   // rojo
TEST(LeapYear, Every400Is)      { EXPECT_TRUE(is_leap(2000)); }
```

Corrélo, leé la salida del test que falla (qué esperaba, qué obtuvo, en qué línea), arreglá
`is_leap` y miralo pasar a verde.

Un `vcpkg.json` de otra herramienta, con una biblioteca de formateo:

```json
{
  "name": "mi-herramienta",
  "version-string": "0.1.0",
  "builtin-baseline": "<commit de vcpkg>",
  "dependencies": [ "fmt" ]
}
```

Y el esqueleto de un workflow de GitHub Actions con matriz:

```yaml
name: ci
on: [push, pull_request]
jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - run: echo "acá van configure, build y test"
```

### Errores típicos

- `VCPKG_ROOT` sin definir: el preset no encuentra el toolchain y `find_package` falla.
- Agregar el toolchain a un build ya configurado y no entender por qué no lo usa (borrá el
  directorio de build o configurá con `--fresh`).
- `enable_testing()` que no está en el `CMakeLists.txt` raíz: `ctest` dice "No tests were found".
- En Windows (multi-config), correr `ctest` sin `-C Debug` ni test preset: no encuentra los
  ejecutables.
- Un test que pasa sin haberlo visto en rojo nunca.
- "En mi máquina anda": rutas absolutas, una biblioteca instalada a mano, o un archivo que no está
  en git. CI está justamente para eso.

### Cómo lo hacía el original (y cómo lo hace MuMain)

El original se compilaba con Visual C++ 6 y MFC 4.2 enlazada estática, contra bibliotecas del
sistema (`ODBC32`, `WS2_32`)
([01 § identidad del binario](../../Server/DataServer/docs/research/01-binario-y-metodo.md#identidad-del-binario)).
No tiene tests ni CI: cada cambio se probaba a mano contra un GameServer.

MuMain, la referencia de organización, usa vcpkg en modo manifiesto con `builtin-baseline`, tests
con doctest corridos por `ctest`, y CI con Windows, Linux y macOS. Es el mismo esquema que vas a
armar, con GoogleTest en lugar de doctest.

### Para leer

- GoogleTest: "GoogleTest Primer" (`TEST`, aserciones, cómo leer la salida).
- vcpkg: "Manifest mode" y "Versioning" (`builtin-baseline`).
- CMake: módulo `GoogleTest` (`gtest_discover_tests`), `ctest(1)` y la sección `testPresets` de
  `cmake-presets(7)`.
- GitHub Actions: "Using a matrix for your jobs" y la integración de caché binaria de vcpkg.

### Preguntas para chequearte

1. ¿Qué diferencia hay entre `EXPECT_EQ` y `ASSERT_EQ`? Dame un caso para cada uno.
2. ¿Por qué el primer test tiene que fallar a propósito?
3. ¿Qué pasa con tu build dentro de un año si `vcpkg.json` no fija un `builtin-baseline`?
4. CI está rojo solo en Windows y en tu Linux pasa. ¿Qué mirás primero?

---

## 0.3 Documentación base

### La idea

- **ADR** (*Architecture Decision Record*): un archivo corto por decisión importante, con tres
  partes: **Contexto** (qué problema había y qué restricciones), **Decisión** (qué se eligió) y
  **Consecuencias** (qué se gana, qué se pierde, qué queda más difícil). Un ADR no se edita cuando
  la decisión cambia: se escribe uno nuevo que lo **reemplaza**. Así queda el porqué, no solo el
  qué.
- **Reglas de código** (`CODING_RULES.md`): lo que todo cambio tiene que respetar, escrito antes de
  que haya mucho código para que nadie tenga que adivinarlo. En MuRet se adaptan de MuMain: capas
  separadas (la red no sabe de SQL, los handlers no tocan sockets), RAII, nada hardcodeado.
- **`AGENTS.md`:** la puerta de entrada al repo para personas y agentes de IA: qué leer primero,
  cómo se compila, qué convenciones hay.
- **Formateador** (`.clang-format`): el estilo del código pasa a ser una configuración que aplica
  una herramienta, no una discusión en cada PR. CI puede verificarlo con
  `clang-format --dry-run --Werror`.

### Ejemplo mínimo (fuera del proyecto)

Un ADR de otro proyecto, para ver la forma:

```markdown
# ADR 0003: SQLite para el catálogo de la biblioteca

## Contexto
El catálogo tiene ~20 000 libros y lo usa una sola computadora del mostrador.
Hoy está en una planilla que se corrompe si dos personas la abren a la vez.

## Decisión
Guardar el catálogo en un archivo SQLite al lado del programa.

## Consecuencias
- Sin servidor que instalar ni mantener.
- Un solo escritor a la vez: si mañana hay tres mostradores, hay que revisar esta decisión.
- El respaldo es copiar un archivo.
```

### Errores típicos

- Un ADR que solo dice la decisión, sin contexto ni consecuencias: dentro de seis meses nadie
  sabe por qué.
- Editar un ADR viejo en vez de escribir uno nuevo que lo reemplace.
- Reglas de código que nadie aplica (ni la revisión ni una herramienta).
- Reformatear todo el repo en el mismo PR que cambia lógica: el diff se vuelve ilegible.

### Cómo lo hacía el original (y cómo lo hace MuMain)

El original no tiene decisiones escritas: el porqué de cada cosa hubo que deducirlo del binario, y
la reconstrucción y los documentos no siempre coinciden con él
([09-discrepancias](../../Server/DataServer/docs/research/09-discrepancias.md)). Es lo que pasa
cuando la documentación no acompaña al código.

MuMain tiene `AGENTS.md` en la raíz, `docs/CODING_RULES.md` (capas separadas, una clase por
archivo, sin valores hardcodeados, cuidado con fugas) y un `.clang-format` que su CI verifica.

### Para leer

- Michael Nygard, "Documenting Architecture Decisions" (el artículo que propuso los ADR).
- El `docs/CODING_RULES.md` y el `AGENTS.md` de [sven-n/MuMain](https://github.com/sven-n/MuMain).
- Documentación de ClangFormat: "Configuring Style with clang-format".

### Preguntas para chequearte

1. ¿Qué tiene que pasar con el ADR 0001 si en la ola 6 decidís que el SQL no se respeta al byte?
2. ¿Qué regla de capas rompe un handler que llama a `send` directamente?
3. ¿Por qué conviene que el formato lo verifique CI y no la revisión?

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Mis respuestas a las preguntas de la 0.1** (o el link a donde las contesté):
- **Decisiones que tomé (y por qué):** lo que quedó en los ADR 0001 y 0002
- **Preguntas que me quedaron para más adelante:**
