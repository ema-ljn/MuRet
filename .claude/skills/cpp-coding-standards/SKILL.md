---
name: cpp-coding-standards
description: Usala cuando en MuRet haya que escribir o revisar C++ contra las C++ Core Guidelines (RAII, inmutabilidad, tipos fuertes, paso de parámetros, clases, concepts, enum class) y querés citar la regla por su número (F.16, C.21, ES.46). Complementa a cpp-pro; manda CODING_RULES.
license: MIT
metadata:
  upstream: https://github.com/affaan-m/everything-claude-code/tree/4adeee6732a12d1540f49a4d50a38ce8c97f9a53/skills/cpp-coding-standards
---

# C++ Core Guidelines en MuRet

Es la skill `cpp-coding-standards` de
[affaan-m/everything-claude-code](https://github.com/affaan-m/everything-claude-code) (MIT): un
resumen de las [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
con el número de cada regla. Sirve para revisar y para justificar un cambio citando la regla.
`cpp-pro` explica el lenguaje y el build; esta dice qué regla de las Core Guidelines se rompe. El
resumen está en [`references/core-guidelines.md`](references/core-guidelines.md).

## Quién manda

1. Lo que Emanuel pida en la conversación.
2. La fidelidad al `main.exe` original (ver la skill `refactoring`, "Los cuidados de fidelidad").
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) y `.clang-format`.
4. La skill `cpp-pro` (C++20, CMake, vcpkg, GoogleTest en MuRet).
5. Esta skill y, al final, `references/`.

## Donde `references/` no vale para MuRet

| `references/` dice | En MuRet |
| --- | --- |
| C++17/20/23 | Solo C++20, sin extensiones: nada de C++23 (`std::expected`, `std::print`). |
| E.2, E.14, E.15: excepciones para los errores | MuRet devuelve los errores como valores (`Core::Expected`, `std::optional`, `std::error_code`), como dicen `cpp-pro` y CODING_RULES. Lo que sí vale de E.*: RAII (E.6), `noexcept` donde no se puede fallar (E.12) y destructores que no fallan (E.16). |
| NL.10 `underscore_style`, miembros con `_` al final, constantes en mayúsculas en los ejemplos | CODING_RULES §7: tipos, funciones y constantes en `PascalCase`, variables en `camelCase`, `m_` en los miembros privados, `I` en las interfaces, `s_` en los estáticos. |
| SF.8 guardas de include | `#pragma once`, como todo el repo. |
| I.2 nada de globales que no sean `const` | Es la meta de AR3: los globales del original pasan a partes del estado por módulo (`globales.tsv`, `GameContext`). Hasta entonces, el global de hoy queda donde está. |
| ES.45 nada de constantes mágicas | CODING_RULES §3: las constantes del wire van con nombre en `Protocol/` y con la cita de la sección del research que las fija. |
| CP.* concurrencia | CODING_RULES §8: cada dato tiene un hilo dueño. El cliente es casi todo de un hilo. |
| La lista de control del final (excepciones propias, guardas) | Vale sin esas dos líneas. |

## Cuándo usarla y cuándo no

- **Sí**: al escribir o revisar una clase, un tipo de paquete (`Field<T, Offset>`), el
  ownership del `GameContext`, el paso de parámetros de un handler (F.16, F.20, I.23), o al
  cambiar un `enum` por `enum class` (Enum.3).
- **No** para cambiar la forma de una transcripción del original "porque la guía lo pide": la
  fidelidad gana, y el cambio de forma va a un PR de diseño con el golden en verde.

## Origen y licencia

[`references/core-guidelines.md`](references/core-guidelines.md) es una copia textual de
`skills/cpp-coding-standards/SKILL.md` en el commit `4adeee67` de affaan-m/everything-claude-code,
bajo la licencia MIT de [`LICENSE`](LICENSE). Este `SKILL.md` es una adaptación. Para actualizar:
traer el archivo de un commit nuevo, cambiar el commit en el frontmatter y en esta sección, y
revisar la tabla "Donde `references/` no vale para MuRet".
