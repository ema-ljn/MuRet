---
name: clean-architecture
description: Use when deciding in MuRet where something goes, which module may include which, how to cut a cycle between modules, where an interface lives or how the app/ composition root is wired, above all in the client refactor by modules. Robert C. Martin's Clean Architecture rules, ranked below the refactor spec.
license: MIT
metadata:
  upstream: https://github.com/pproenca/dot-skills/tree/cf93c57cac89d6fc3e4194686000411567f5caf3/skills/.experimental/clean-architecture
  upstream-version: "1.0.6"
---

# Clean Architecture in MuRet

This is the `clean-architecture` skill from [pproenca/dot-skills](https://github.com/pproenca/dot-skills) (MIT): 42
rules from Robert C. Martin's *Clean Architecture*. It goes with `clean-code` (Emanuel, 2026-11-21) in the client
refactor. This file is MuRet's layer; the rules are in `references/`, with web application examples (TypeScript, Java,
Go, Python).

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Fidelity to the original `main.exe`**: no boundary changes the order of calls or of `rand()` calls. Every fan-out
   between modules is an ordered list.
3. The [refactor spec](../../../docs/superpowers/specs/2026-11-20-client-architecture-design.md) (the three roots, layers
   0 to 12, the six criteria of "The modules, one by one") and its
   [plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md).
4. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) §5 (separate layers).
5. This skill and, last, `references/`.

## Which rule is which part of the client

Most of the spec's decisions already are these rules under another name:

| Rule | In the client |
| --- | --- |
| `dep-inward-only`, `comp-stable-dependencies` | The spec's layers: a module includes only lower layers (engine 0 to 5, game 6 to 11, `app` 12). `check_layers.py` measures it. |
| `dep-acyclic-dependencies` | Zero cycles between modules when AR7 closes. A pair that includes each other is cut with a contract below or by merging the two (criterion 6 of the spec). |
| `dep-interface-ownership`, `dep-stable-abstractions` | "Contract below, implementation above": the `Kernel` ports are implemented by `Session`, `Sound` and `Ui`; `IObfuscation` lives in `Network` and FoxAnti above. |
| `dep-data-crossing-boundaries` | Packet layouts (`Field<T, Offset>`) and data `struct`s cross layers; objects with logic do not. |
| `comp-common-closure`, `comp-screaming-architecture` | One module per game feature (`Guild`, `Party`, `Events/Crywolf`, `MapScripts/<Map>`), with its handler, state and drawing together, instead of folders by technical type. |
| `comp-common-reuse` | One target per module with `PRIVATE` includes: whoever uses `Tables` does not drag in `Render`. |
| `bound-main-component`, `frame-di-container-edge` | `app/` is the composition root: it wires everything by hand and in order, and it is the only one that knows the optional pieces (`MakeObfuscation()` picks FoxAnti at compile time). |
| `bound-humble-object` | What touches SDL, GL or the socket stays thin (`Platform`, `Render/Gl`, `Network`); the deciding logic goes in code the behavior golden (`FrameDigest`) tests without a window. |
| `bound-partial-boundaries`, `bound-boundary-cost-awareness` | Not every folder deserves a target today: the 58 modules have their cost measured in the spec, and a small module can start as a folder inside another one. |
| `test-boundary-verification` | `check_layers.py` with its baseline that only goes down: it is the client's ArchUnit. |
| `frame-logging-abstraction` | `Core::Log` and `ILogSink` already separate the log from its destination. |
| `usecase-explicit-dependencies`, `test-testable-design` | Each handler and each frame step receives in its signature the parts of the `GameContext` it touches; no singletons or hidden globals (`globals.tsv`, AR3). |

## Where `references/` does not apply to MuRet

| The rule says | In MuRet |
| --- | --- |
| `entity-*`: rich entities with business rules | The original's state is data (`struct`s that mimic the `.bss`) and the logic is in the module's functions; see the `obj-data-object-asymmetry` row in the `clean-code` skill. What does apply: invariants are guarded in a single place. |
| `usecase-*`: use cases, commands, input and output ports | The closest thing is one handler per packet (AR2) or one step of the frame script (AR5). The idea applies (orchestrate without implementing the rules, one reason to change); the vocabulary does not. |
| `usecase-transaction-boundary`, `frame-orm-in-infrastructure`, `frame-web-in-infrastructure`, `adapt-controller-thin`, `adapt-presenter-formats` | They do not apply: the client has no database, ORM or HTTP. |
| `comp-reuse-release-equivalence`: versioned packages | It is a single binary in a single repo: what ships together is a target, with no version of its own. |
| `bound-defer-decisions` | Fidelity already decided almost everything (GL 3.3 as reference, SDL3). What is deferred has its place in the spec ("Left to the builder"). |
| Exceptions in the examples | MuRet returns errors as values (`Core::Expected`). |

## The rules

| Priority | Category | Prefix | Files |
| --- | --- | --- | --- |
| 1 | Dependency Direction | `dep-` | `inward-only`, `interface-ownership`, `no-framework-imports`, `data-crossing-boundaries`, `acyclic-dependencies`, `stable-abstractions` |
| 2 | Entity Design | `entity-` | `pure-business-rules`, `no-persistence-awareness`, `encapsulate-invariants`, `value-objects`, `rich-not-anemic` |
| 3 | Use Case Isolation | `usecase-` | `single-responsibility`, `input-output-ports`, `orchestrates-not-implements`, `no-presentation-logic`, `explicit-dependencies`, `transaction-boundary` |
| 4 | Component Cohesion | `comp-` | `screaming-architecture`, `common-closure`, `common-reuse`, `reuse-release-equivalence`, `stable-dependencies` |
| 5 | Boundary Definition | `bound-` | `humble-object`, `partial-boundaries`, `boundary-cost-awareness`, `main-component`, `defer-decisions`, `service-internal-architecture` |
| 6 | Interface Adapters | `adapt-` | `controller-thin`, `presenter-formats`, `gateway-abstraction`, `mapper-translation`, `anti-corruption-layer` |
| 7 | Framework Isolation | `frame-` | `domain-purity`, `orm-in-infrastructure`, `web-in-infrastructure`, `di-container-edge`, `logging-abstraction` |
| 8 | Testing Architecture | `test-` | `tests-are-architecture`, `testable-design`, `layer-isolation`, `boundary-verification` |

Each rule is in `references/<prefix><file>.md` (for example `references/dep-acyclic-dependencies.md`), and the
categories in `references/_sections.md`.

## Origin and license

`references/` is a verbatim copy of `skills/.experimental/clean-architecture/references/` at commit `cf93c57c` of
pproenca/dot-skills (skill version 1.0.6), under the MIT license in [`LICENSE`](LICENSE). This `SKILL.md` is an
adaptation. To update: bring `references/` from a new commit, change the commit and version in the frontmatter and in
this section, and review both tables.
