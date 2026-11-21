---
name: clean-code
description: Usala cuando en MuRet haya que escribir, revisar o refactorizar código pensando en que se lea y se mantenga (nombres, funciones, comentarios, errores, clases, tests), y en todo PR de diseño del refactor del cliente. Son las reglas de Clean Code de Robert C. Martin, por debajo de CODING_RULES y de la fidelidad al original.
license: MIT
metadata:
  upstream: https://github.com/pproenca/dot-skills/tree/cf93c57cac89d6fc3e4194686000411567f5caf3/skills/.experimental/clean-code
  upstream-version: "1.0.7"
---

# Clean Code en MuRet

Es la skill `clean-code` de [pproenca/dot-skills](https://github.com/pproenca/dot-skills) (MIT):
las 48 reglas del libro *Clean Code* de Robert C. Martin, con las correcciones de hoy y un "cuándo
no" en cada una. Emanuel eligió Clean Code como guía del refactor del cliente (2026-11-21). Este
archivo es la capa de MuRet; las reglas están en `references/`, en inglés y con ejemplos en Java.

## Quién manda

1. Lo que Emanuel pida en la conversación.
2. **La fidelidad al `main.exe` original**: el orden de las llamadas, el de los `rand()` y la
   aritmética de x87 no cambian por una regla de estilo. Un golden que cambia es un bug, no una
   limpieza ([spec del refactor](../../../docs/superpowers/specs/2026-11-20-arquitectura-cliente-design.md),
   "Lo que no se negocia").
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) y `.clang-format`. Varias de sus reglas
   ya son de Clean Code (1 funciones cortas, 2 salir temprano, 3 nada hardcodeado, 4 no duplicar,
   9 legible antes que ingenioso).
4. Los planes de `docs/superpowers/` y las skills `refactoring` (cómo se refactoriza sin cambiar el
   comportamiento) y `clean-architecture` (los módulos y sus dependencias).
5. Esta skill y, al final, `references/`.

## Donde `references/` no vale para MuRet

| La regla dice | En MuRet |
| --- | --- |
| `name-avoid-encodings`: sin `m_` ni `I` | Se quedan: `m_` en los miembros privados, `I` en las interfaces y `s_` en los estáticos (CODING_RULES §7, decidido el 2026-11-02). Lo que sí vale: nada de notación húngara de tipos (`strName`, `iCount`). |
| `name-method-verb`: `getName()`, `setName()` | Los nombres siguen CODING_RULES §7 (`PascalCase` en funciones y métodos). El principio (verbos para lo que hace, sustantivos para lo que es) vale igual. |
| `func-small`: casi nunca 20 líneas | CODING_RULES §1 (unas 40 líneas) y los tamaños de la spec (handler ~80, función ~60, archivo ~400). Una transcripción de un `switch` del original no se parte para llegar a un número: se marca como transcripción (o pasa a tabla en AR2). |
| `err-use-exceptions`, `err-write-try-catch-first`, `err-define-by-caller-needs` | MuRet no tira excepciones: los errores vuelven como valores (`Core::Expected`, `std::optional`, `std::error_code`). El principio se aplica igual: el camino feliz a la vista, el error con contexto y un tipo de error por lo que necesita quien llama. |
| `err-avoid-null` | `std::optional` para lo que puede faltar; un puntero crudo es un observador que no es dueño y que puede ser `nullptr` solo si su contrato lo dice. |
| `cmt-*`: el código se explica solo | Vale, y más estricto (Emanuel, 2026-11-21; CODING_RULES §9): un comentario va solo si es estrictamente necesario, en inglés y en una línea. Lo que se queda: la cita al original (`// 0x4e2b10`, a lo sumo con una etiqueta corta), que lee el rastreo; la sección del research de una constante (`// 04-protocolo § ...`); `// Contiguous in the original:`; y un porqué que el código no puede decir. Nada en castellano, ni prosa, ni código comentado. |
| `obj-data-object-asymmetry`, `entity`/modelos ricos | El estado que imita la `.bss` del original queda como datos (`struct` sin invariantes, campos en `camelCase`) y la lógica en las funciones de su módulo. Es la asimetría datos/objetos de la regla, no un modelo anémico por descuido. |
| `emerge-simple-design`: con una sola implementación, dudá de la interfaz | Las interfaces van en los bordes del motor (`ISocket`, `IPlayback`, `IObfuscation`) y donde un test las necesita, como dice la regla misma ("tests need seams"). |
| `class-separate-concerns`: un framework de inyección | Sin framework: la raíz de composición es `app/`, armada a mano en orden (`MakeObfuscation()` es el ejemplo). `GameContext` pasa las partes del estado por la firma; no es un service locator. |
| `fmt-*` | Lo decide `.clang-format`; no se alinea a mano. |
| `test-*` | GoogleTest y CTest, un ejecutable por archivo de test hasta que AR1.2 decida otra cosa, y la regla 13 de CODING_RULES (cada comportamiento, un test). |

## Cómo se usa en el refactor

- **Al diseñar un módulo** (los PR de diseño de AR1 a AR6): nombres que dicen el porqué
  (`name-intention-revealing`), una cosa por función a un solo nivel de abstracción
  (`func-one-thing`, `func-abstraction-level`), una razón para cambiar por clase (`class-small`,
  `class-cohesion`) y la construcción separada del uso (`class-separate-concerns`).
- **Al deduplicar** (AR6): `func-dry` con su "cuándo no". Dos funciones del original que se parecen
  se juntan solo si el cuerpo es idéntico y el golden lo confirma; la duplicación casual se queda.
- **Al revisar**: citá la regla por su nombre (`func-minimize-arguments`) y el archivo de
  `references/` que la explica.
- **En las mudanzas no**: un PR que solo mueve archivos no limpia nada (Global Constraints del plan).

## Las reglas

| Prioridad | Categoría | Prefijo | Archivos |
| --- | --- | --- | --- |
| 1 | Meaningful Names | `name-` | `intention-revealing`, `avoid-disinformation`, `meaningful-distinctions`, `pronounceable`, `searchable`, `avoid-encodings`, `class-noun`, `method-verb` |
| 2 | Functions | `func-` | `small`, `one-thing`, `abstraction-level`, `minimize-arguments`, `no-side-effects`, `command-query-separation`, `dry` |
| 3 | Comments | `cmt-` | `express-in-code`, `explain-intent`, `avoid-redundant`, `avoid-commented-out-code`, `warning-consequences` |
| 4 | Formatting | `fmt-` | `vertical-formatting`, `horizontal-alignment`, `team-rules`, `indentation` |
| 5 | Error Handling | `err-` | `use-exceptions`, `write-try-catch-first`, `provide-context`, `define-by-caller-needs`, `avoid-null` |
| 6 | Objects and Data Structures | `obj-` | `data-abstraction`, `data-object-asymmetry`, `law-of-demeter`, `avoid-hybrids`, `dto` |
| 7 | Boundaries | `bound-` | `wrap-third-party`, `learning-tests` |
| 8 | Classes and Systems | `class-` | `small`, `cohesion`, `organize-for-change`, `isolate-from-change`, `separate-concerns` |
| 9 | Unit Tests | `test-` | `first-law`, `keep-clean`, `one-assert`, `first-principles`, `build-operate-check` |
| 10 | Emergence and Simple Design | `emerge-` | `simple-design`, `expressiveness` |

Cada regla está en `references/<prefijo><archivo>.md` (por ejemplo `references/func-small.md`), y
las categorías en `references/_sections.md`.

## Origen y licencia

`references/` es una copia textual de `skills/.experimental/clean-code/references/` en el commit
`cf93c57c` de pproenca/dot-skills (versión 1.0.7 de la skill), bajo la licencia MIT de
[`LICENSE`](LICENSE). Este `SKILL.md` es una adaptación. Para actualizar: traer `references/` de un
commit nuevo, cambiar el commit y la versión en el frontmatter y en esta sección, y revisar la tabla
"Donde `references/` no vale para MuRet".
