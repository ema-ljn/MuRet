---
name: clean-architecture
description: Use when deciding in MuRet where something goes, which module may include which, how to cut a cycle between modules, where an interface (a port) lives or how the app/ composition root is wired, above all in the client refactor by modules. Clean Architecture and ports and adapters, ranked below the refactor spec.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/42203085cfc5736204bf0f6d436dff7e1da0bf3f/plugins/backend-development/skills/architecture-patterns
---

# Clean Architecture in MuRet

This is the `architecture-patterns` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT), one of the
most-starred Claude Code plugin repositories, renamed `clean-architecture` here. It covers Robert C. Martin's Clean
Architecture, Hexagonal Architecture (ports and adapters) and Domain-Driven Design, with Python backend examples. MuRet
uses the first two; DDD's tactical patterns are for services with a database. It goes with `clean-code` (Emanuel,
2026-11-21) in the client refactor. This file is MuRet's layer; the upstream skill is in
[`references/upstream-skill.md`](references/upstream-skill.md), with [`references/details.md`](references/details.md)
and [`references/advanced-patterns.md`](references/advanced-patterns.md).

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Fidelity to the original `main.exe`**: no boundary changes the order of calls or of `rand()` calls. Every fan-out
   between modules is an ordered list.
3. The [refactor spec](../../../docs/superpowers/specs/2026-11-20-client-architecture-design.md) (the three roots, layers
   0 to 12, the six criteria of "The modules, one by one") and its
   [plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md).
4. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) §5 (separate layers).
5. This skill and, last, `references/`.

## Which idea is which part of the client

Most of the spec's decisions already are these ideas under another name. "Inward" in the skill is "downward" in the
spec's layers.

| The skill says | In the client |
| --- | --- |
| Dependencies point inward only | The spec's layers: a module includes only lower layers (engine 0 to 5, game 6 to 11, `app` 12). `check_layers.py` measures it. |
| Every boundary is crossed through an abstract interface owned by the core | "Contract below, implementation above": the `Kernel` ports are implemented by `Session`, `Sound` and `Ui`; `IObfuscation` lives in `Network` and FoxAnti above. `Actors`, `Player` and `Items` do not include `Protocol` or `Session`: they send through the `Kernel` port. |
| Ports and adapters at the edges | The engine's edges: `ISocket`, `IPlayback` and `IObfuscation` are ports; what touches SDL, GL or Winsock stays thin in `Platform`, `Render/Gl` and `Network`. |
| In-memory adapters in tests | Tests put fakes behind the same ports, and the behavior golden (`FrameDigest`) runs the frame's steps without a window. |
| Dependency injection wiring at the outermost layer | `app/` is the composition root: it wires everything by hand and in order, and it is the only one that knows the optional pieces (`MakeObfuscation()` picks FoxAnti at compile time). No container. |
| Circular imports between layers; detecting and breaking dependency cycles | Zero cycles between modules when AR7 closes. A pair that includes each other is cut with a contract below or by merging the two (criterion 6 of the spec). `check_layers.py`, with its baseline that only goes down, is the cycle detector. |
| A controller only parses, calls a use case and maps the response | The packet handler (AR2) decodes the layout and calls the module that owns the state; the logic does not pile up in the dispatch table. |
| No framework code in the domain | Game modules (layers 6 to 11) do not include SDL or GL headers. |
| Bounded contexts, and no context bleed | One module per game feature (`Guild`, `Party`, `Events/Crywolf`, `MapScripts/<Map>`), with its handler, state and drawing together, and one target per module with `PRIVATE` includes, so whoever uses `Tables` does not drag in `Render`. |

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| DDD tactical patterns: entities with identity, value objects, aggregates, repositories, domain events, outbox | The client has no database or persistence. The original's state is data (`struct`s that mimic the `.bss`) and the logic is in the module's functions; see the §5 row in the `clean-code` skill. What does apply: invariants are guarded in a single place. |
| Use cases as classes with request and response objects | The closest thing is one handler per packet (AR2) or one step of the frame script (AR5). The idea applies (orchestrate without implementing the rules, one reason to change); the classes do not. |
| Microservices, multi-service trees, HTTP clients, ORM, `async` | They do not apply: one binary, an almost entirely single-threaded client, no database, ORM or HTTP. |
| Swap implementations without touching the core | Not a goal: fidelity already decided the platform (SDL3, GL 3.3 as reference). Ports go where the engine meets the platform or where a test needs a seam. |
| Exceptions and Python examples | MuRet returns errors as values (`Core::Expected`), in C++20 under CODING_RULES. |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/backend-development/skills/architecture-patterns/SKILL.md` at commit `42203085` of wshobson/agents, and
`references/details.md` and `references/advanced-patterns.md` are verbatim copies of that skill's `references/`, under
the MIT license in [`LICENSE`](LICENSE). The upstream file's links are relative to the skill's root. This `SKILL.md` is
an adaptation. To update: bring the files from a new commit, change the commit in the frontmatter and in this section,
and review both tables.
