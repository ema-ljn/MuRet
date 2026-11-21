# Annex to the client architecture plan

The tables that came out of measuring `Client/src` on `main` `bb56680` (2026-11-20), for the
[plan](../2026-11-20-client-architecture-refactor.md) and the
[spec](../../specs/2026-11-20-client-architecture-design.md). They are a snapshot: the live map moves to
`Client/docs/research/coverage/module-map.tsv` in AR0.1, and from then on PRs maintain it. Addresses of the original
that appeared in the reasons were replaced with `(addr.)`, so the reference does not count them as doc citations.

| file | rows | what it is |
| --- | ---: | --- |
| `module-map.tsv` | 1,418 | **every file of `Client/src` and its target module** (`engine/Core`, `game/Guild`…), with its confidence and the reason. The `Scenes/` ones come from analyzing the include graph, the names and what each file does; the `UI/`, `World/`, `GameLogic/` and `App/` ones, from naming rules (medium confidence). 50 have low confidence: the graph pulls them toward another module and they must be checked by hand (AR0.1, Task 2). **Revised on 2026-11-21**: 486 rows changed target when going from 31 to 58 modules (`engine/Crt`, `Crypto`, `Diagnostics`, `Pathfinding`, `Obfuscation`; `game/Tables`, `Maps`, `Sound`, `Assets`, `Terrain`, `Camera`, `Movement`, `Skills`, `Tooltips`, `Messenger`, `PersonalShop`, `Party`, `Quests`, `ChaosMix`, `Warehouse`, `PetTrainer`, `Events/<Event>`, `MapScripts/<Map>`, `Fauna`, `Weather`, `Particles`, `Joints`, `FrontEnd`); the reason of each says "(2026-11-21 review)" |
| `scenes-candidate-modules.tsv` | 26 | (from the first version, with the names of the time) the 26 candidate modules of `Scenes/`: proposed layer, lines, files, waves, what it depends on and what depends on it, and which `WorldRuntime` fields (with `*` the ones it writes, approximate) and which `WorldState` accessors it uses |
| `scenes-cycles.tsv` | 100 | (from the first version) the pairs of `Scenes/` modules that include each other, and whether they stay mutual after moving down the state headers and the UI kit (48 do) |
| `interfaces-and-callback-bags.tsv` | 132 | virtual interfaces, callback bags and parameter bags: methods, callbacks, references and pointers, implementations in `src` and in tests, and how many files use them |

How it was measured (scripts in the session's scratchpad, not in the repo): the graph of `#include "…"` that resolve to
a file in `src`; lizard for the size and complexity of the 8,800 functions; jscpd 4.3 and a token-based detector of our
own (windows of 50, with literals normalized) for repetition. The spec's numbers come from there.

The 2026-11-21 revision (more modules, requested by Emanuel) was done with naming and folder rules over the first
version's map; with the new map, 365 includes in 83 module pairs (210 files) go up a layer and there are 80 mutual pairs
between game modules of the same layer.
