---
name: clean-architecture
description: Usala cuando en MuRet haya que decidir dónde va algo, qué módulo puede incluir a cuál, cómo se corta un ciclo entre módulos, dónde vive una interfaz o cómo se arma la raíz de composición de app/, sobre todo en el refactor del cliente por módulos. Son las reglas de Clean Architecture de Robert C. Martin, por debajo de la spec del refactor.
license: MIT
metadata:
  upstream: https://github.com/pproenca/dot-skills/tree/cf93c57cac89d6fc3e4194686000411567f5caf3/skills/.experimental/clean-architecture
  upstream-version: "1.0.6"
---

# Clean Architecture en MuRet

Es la skill `clean-architecture` de [pproenca/dot-skills](https://github.com/pproenca/dot-skills)
(MIT): 42 reglas del libro *Clean Architecture* de Robert C. Martin. Acompaña a `clean-code`
(Emanuel, 2026-11-21) en el refactor del cliente. Este archivo es la capa de MuRet; las reglas están
en `references/`, en inglés y con ejemplos de aplicaciones web (TypeScript, Java, Go, Python).

## Quién manda

1. Lo que Emanuel pida en la conversación.
2. **La fidelidad al `main.exe` original**: ninguna frontera cambia el orden de las llamadas ni el
   de los `rand()`. Todo reparto entre módulos es una lista ordenada.
3. La [spec del refactor](../../../docs/superpowers/specs/2026-11-20-arquitectura-cliente-design.md)
   (las tres raíces, las capas 0 a 12, los seis criterios de "Los módulos, uno por uno") y su
   [plan](../../../docs/superpowers/plans/2026-11-20-refactor-arquitectura-cliente.md).
4. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) §5 (capas separadas).
5. Esta skill y, al final, `references/`.

## Qué regla es qué cosa del cliente

La mayoría de las decisiones de la spec ya son estas reglas con otro nombre:

| Regla | En el cliente |
| --- | --- |
| `dep-inward-only`, `comp-stable-dependencies` | Las capas de la spec: un módulo incluye solo capas de abajo (motor 0 a 5, juego 6 a 11, `app` 12). Lo mide `check_layers.py`. |
| `dep-acyclic-dependencies` | Cero ciclos entre módulos al cerrar AR7. Un par que se incluye mutuamente se corta con un contrato abajo o juntando los dos (criterio 6 de la spec). |
| `dep-interface-ownership`, `dep-stable-abstractions` | "Contrato abajo, implementación arriba": los puertos de `Kernel` los implementan `Session`, `Sound` y `Ui`; `IObfuscation` vive en `Network` y FoxAnti arriba. |
| `dep-data-crossing-boundaries` | Los layouts de paquetes (`Field<T, Offset>`) y los `struct` de datos cruzan las capas; los objetos con lógica no. |
| `comp-common-closure`, `comp-screaming-architecture` | Un módulo por cosa del juego (`Guild`, `Party`, `Events/Crywolf`, `MapScripts/<Mapa>`), con su handler, su estado y su dibujo juntos, en vez de carpetas por tipo técnico. |
| `comp-common-reuse` | Un target por módulo con includes `PRIVATE`: quien usa `Tables` no arrastra `Render`. |
| `bound-main-component`, `frame-di-container-edge` | `app/` es la raíz de composición: arma todo a mano y en orden, y es el único que sabe de las piezas opcionales (`MakeObfuscation()` elige FoxAnti al compilar). |
| `bound-humble-object` | Lo que toca SDL, GL o el socket queda finito (`Platform`, `Render/Gl`, `Network`); la lógica que decide va en código que el golden de comportamiento (`FrameDigest`) prueba sin ventana. |
| `bound-partial-boundaries`, `bound-boundary-cost-awareness` | No toda carpeta merece un target hoy: los 58 módulos tienen el costo medido en la spec, y un módulo chico puede empezar como carpeta dentro de otro. |
| `test-boundary-verification` | `check_layers.py` con su línea de base que solo baja: es el ArchUnit del cliente. |
| `frame-logging-abstraction` | `Core::Log` y `ILogSink` ya separan el log de su destino. |
| `usecase-explicit-dependencies`, `test-testable-design` | Cada handler y cada paso del frame recibe en la firma las partes del `GameContext` que toca; nada de singletons ni de globales escondidos (`globales.tsv`, AR3). |

## Donde `references/` no vale para MuRet

| La regla dice | En MuRet |
| --- | --- |
| `entity-*`: entidades ricas con reglas de negocio | El estado del original es datos (`struct` que imitan la `.bss`) y la lógica está en las funciones del módulo; ver la fila de `obj-data-object-asymmetry` en la skill `clean-code`. Lo que sí vale: los invariantes se cuidan en un solo lugar. |
| `usecase-*`: casos de uso, comandos, puertos de entrada y salida | Lo más parecido es un handler por paquete (AR2) o un paso del guion del frame (AR5). La idea vale (orquestar sin implementar las reglas, una razón para cambiar); el vocabulario no. |
| `usecase-transaction-boundary`, `frame-orm-in-infrastructure`, `frame-web-in-infrastructure`, `adapt-controller-thin`, `adapt-presenter-formats` | No aplican: el cliente no tiene base de datos, ORM ni HTTP. |
| `comp-reuse-release-equivalence`: paquetes con versión | Es un solo binario en un solo repo: lo que se entrega junto es un target, sin versión propia. |
| `bound-defer-decisions` | La fidelidad ya decidió casi todo (GL 3.3 de referencia, SDL3). Lo que sí se difiere tiene su lugar en la spec ("Lo que queda"). |
| Excepciones en los ejemplos | MuRet devuelve los errores como valores (`Core::Expected`). |

## Las reglas

| Prioridad | Categoría | Prefijo | Archivos |
| --- | --- | --- | --- |
| 1 | Dependency Direction | `dep-` | `inward-only`, `interface-ownership`, `no-framework-imports`, `data-crossing-boundaries`, `acyclic-dependencies`, `stable-abstractions` |
| 2 | Entity Design | `entity-` | `pure-business-rules`, `no-persistence-awareness`, `encapsulate-invariants`, `value-objects`, `rich-not-anemic` |
| 3 | Use Case Isolation | `usecase-` | `single-responsibility`, `input-output-ports`, `orchestrates-not-implements`, `no-presentation-logic`, `explicit-dependencies`, `transaction-boundary` |
| 4 | Component Cohesion | `comp-` | `screaming-architecture`, `common-closure`, `common-reuse`, `reuse-release-equivalence`, `stable-dependencies` |
| 5 | Boundary Definition | `bound-` | `humble-object`, `partial-boundaries`, `boundary-cost-awareness`, `main-component`, `defer-decisions`, `service-internal-architecture` |
| 6 | Interface Adapters | `adapt-` | `controller-thin`, `presenter-formats`, `gateway-abstraction`, `mapper-translation`, `anti-corruption-layer` |
| 7 | Framework Isolation | `frame-` | `domain-purity`, `orm-in-infrastructure`, `web-in-infrastructure`, `di-container-edge`, `logging-abstraction` |
| 8 | Testing Architecture | `test-` | `tests-are-architecture`, `testable-design`, `layer-isolation`, `boundary-verification` |

Cada regla está en `references/<prefijo><archivo>.md` (por ejemplo
`references/dep-acyclic-dependencies.md`), y las categorías en `references/_sections.md`.

## Origen y licencia

`references/` es una copia textual de `skills/.experimental/clean-architecture/references/` en el
commit `cf93c57c` de pproenca/dot-skills (versión 1.0.6 de la skill), bajo la licencia MIT de
[`LICENSE`](LICENSE). Este `SKILL.md` es una adaptación. Para actualizar: traer `references/` de un
commit nuevo, cambiar el commit y la versión en el frontmatter y en esta sección, y revisar las dos
tablas.
