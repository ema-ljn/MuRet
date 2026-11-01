---
name: cpp-pro
description: Usala cuando en MuRet haya que escribir, revisar o explicar C++20 (RAII, ownership, plantillas y concepts, ranges, concurrencia, rendimiento), tocar CMake, vcpkg o GoogleTest, o entender un warning, un comportamiento indefinido o un error de compilación o de enlace.
license: MIT
metadata:
  upstream: https://github.com/Jeffallan/claude-skills/tree/078a25cc5bfd8011459be7b91d004e900c1d02d3/skills/cpp-pro
  upstream-version: "1.1.0"
  upstream-author: https://github.com/Jeffallan
---

# C++ en MuRet

Es la skill `cpp-pro` de [Jeffallan/claude-skills](https://github.com/Jeffallan/claude-skills)
(MIT) con una capa de MuRet arriba. Lo que dice este archivo gana sobre `references/`, y las
reglas del repo ganan sobre los dos.

## Quién manda

1. Lo que Emanuel pida en la conversación.
2. El rol de tutor ([`AGENTS.md`](../../../AGENTS.md), estilo `Tutor MuRet`): Emanuel escribe el
   código del servidor y del cliente. Esta skill no habilita a escribir lógica que no pidió. Sirve
   para explicar con un ejemplo fuera del proyecto, revisar con preguntas, hacer el andamiaje que
   los planes marcan `(Claude)` y los ejemplos trabajados que él pida.
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) y `.clang-format`.
4. Los roadmaps y planes de `docs/superpowers/`.
5. Esta skill y, al final, `references/`.

## Antes de afirmar cómo se comporta una API

Consultá Context7 y decí que lo consultaste. Para la versión exacta de una dependencia, el header
que instaló vcpkg gana sobre la documentación.

| Qué | Id en Context7 |
| --- | --- |
| C++ y su biblioteca estándar | `/websites/cppreference_cpp` |
| GoogleTest | `/google/googletest` |
| CMake | `/websites/cmake_cmake_help` |
| vcpkg | `/microsoft/vcpkg` |
| SDL3 | `/libsdl-org/sdlwiki` |

## Donde `references/` no vale para MuRet

| `references/` dice | En MuRet |
| --- | --- |
| C++20/23 | Solo C++20 (`CMAKE_CXX_STANDARD 20`, sin extensiones). Nada de C++23: ni `std::expected`, ni `std::print`, ni deducing this. Los errores van con `std::optional` y `std::error_code`, como en `Client/src/Data/DataPath.h`, o con el `Expected` propio del camino de aprendizaje. |
| Dependencias con `FetchContent` o Conan | Solo `vcpkg.json` en modo manifiesto, con la `builtin-baseline` fija. Una dependencia nueva se justifica (regla 10) y, si pesa, lleva un ADR. |
| Tests con Catch2 | GoogleTest y CTest (`gtest_discover_tests`). Primero el test en rojo (regla 13). |
| Nombres en `snake_case`, miembros con `_` al final | Tipos, funciones y constantes en `PascalCase`; variables y miembros en `camelCase` (regla 7). |
| `-Wall -Wextra -Wpedantic` | Ya están, con más, en `cmake/CompilerWarnings.cmake` (`muret_set_warnings`). CI compila con `MURET_WARNINGS_AS_ERRORS=ON`. |
| Correr ASan y UBSan siempre | Todavía no hay un preset con sanitizers. Para probar a mano en Linux, configurá otro directorio de build con `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`. Un preset nuevo es andamiaje: se propone en su PR, no se mete de paso. |
| SIMD, allocators propios, lock-free, módulos, corrutinas | Solo cuando una ola lo pida. Regla 12: medir antes de optimizar, y solo en los caminos calientes. La red va con IOCP y epoll a mano (D-3 del roadmap), no con corrutinas. |
| Pools de hilos y estado compartido | Regla 8: cada dato tiene un hilo dueño; el estado del servidor lo toca solo el hilo de lógica. |
| `shared_ptr` como algo normal | `std::unique_ptr` primero; `std::shared_ptr` solo si la propiedad es compartida de verdad (regla 8). |
| Un constructor que tira una excepción si algo falla | El código de MuRet devuelve los errores como valores (`std::optional`, `std::error_code`). Seguí el patrón del archivo. |
| Usar `auto` siempre | `auto` cuando el tipo se ve en la misma línea o es muy largo; si no, el tipo explícito (regla 9). |

## Lo que sigue valiendo de cpp-pro

- C++ Core Guidelines, RAII siempre y const-correctness.
- `static_cast` y compañía, nunca un cast de C (`-Wold-style-cast` ya lo corta en GCC y Clang).
- Nada de `new`/`delete` crudos ni de `using namespace` en un header.
- Concepts para restringir una plantilla, cuando haya plantillas.
- No ignorar el comportamiento indefinido: `std::tolower` con un `char` negativo, overflow con
  signo, referencias y punteros colgados.
- Un ejemplo de RAII del repo: `SdlVideo` y los `unique_ptr` con deleter de `Client/src/main.cpp`.

## Las referencias

Son de upstream y no se tocaron. Sus ejemplos usan otro estilo de nombres y a veces excepciones:
explican un concepto, pero no se copian tal cual al repo.

| Tema | Archivo | Para |
| --- | --- | --- |
| C++20 | `references/modern-cpp.md` | concepts, ranges, `<=>`, `constexpr`, `std::format` (salteá lo de C++23) |
| Plantillas | `references/templates.md` | variadic, `if constexpr`, type traits, CRTP |
| Memoria y rendimiento | `references/memory-performance.md` | smart pointers, move, RVO, layout y alineación |
| Concurrencia | `references/concurrency.md` | atomics, memory ordering, mutex, `condition_variable` |
| Build | `references/build-tooling.md` | CMake moderno, sanitizers, clang-tidy, GoogleTest (salteá Conan, `FetchContent` y Catch2) |

## Origen y licencia

`references/` es una copia textual de `skills/cpp-pro/references/` en el commit `078a25cc` de
Jeffallan/claude-skills, bajo la licencia MIT de [`LICENSE`](LICENSE). Este `SKILL.md` es una
adaptación. Para actualizar: traer `references/` de un commit nuevo, cambiar el commit en el
frontmatter y en esta sección, y revisar la tabla de "Donde `references/` no vale para MuRet".
