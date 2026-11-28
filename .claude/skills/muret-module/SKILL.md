---
name: muret-module
description: Use when adding a file to the MuRet client, deciding which module a file belongs to, building a game module (its public header, state, In/ and Out/), or when check_layers.py rejects an include or a file without a module.
---

# Client modules in MuRet

The client is split into 59 modules in three roots (`engine/`, `game/`, `app/`), each with a layer. This skill is the
procedure; the rules live in [CODING_RULES §5](../../../docs/CODING_RULES.md#5-capas-separadas), the decision in
[ADR 0023](../../../docs/adr/0023-client-module-architecture.md) and the target shape in the
[spec](../../../docs/superpowers/specs/2026-11-20-client-architecture-design.md). Fidelity to `main.exe` and CODING_RULES
rank above this skill.

## Which module a file belongs to

1. Look up the file, or a file of the same feature, in
   [`Client/docs/research/coverage/module-map.tsv`](../../../Client/docs/research/coverage/module-map.tsv). Its `target`
   is the module (`game/Guild`); a deeper target (`game/Events/Kalima`) is a folder of its module.
2. A new file without a row: pick the module by what the file does, using the notes in
   [`Client/modules.tsv`](../../../Client/modules.tsv), and prefer the one that adds no upward include and no mutual
   pair. Add its row to the map in the same PR (`file`, `lines`, `current_folder`, `target`, `confidence`, `why`; no
   addresses in `why`, write `(addr.)`).
   A file that names an event or a map goes by the key the original dispatches on (spec, criterion 5): a case of the
   effect, particle or joint switch, or a monster keyed by its model, stays with those modules; what a packet, a
   window, an NPC or the event's stage reaches is the event's (`game/Events/<Event>`); what a per-map chain reaches
   with the map as its gate is the map script's (`game/MapScripts/<Map>`).
3. Files in `Core/`, `Input/`, `Audio/`, `Network/`, `Data/`, `Platform/`, `Render/`, `Protocol/` and `App/` fall back
   to their folder's module (`folders_today`) when they have no row. Files in `Scenes/`, `UI/`, `World/` and
   `GameLogic/` always need a row.
4. If the module's folder does not exist yet, the file goes in today's folder with the new anatomy (below); the move of
   its module carries it later.

## An engine module is a folder of its library

The engine builds as four shared libraries
([CODING_RULES §5, "The engine's libraries"](../../../docs/CODING_RULES.md#the-engines-libraries-2026-11-25)):
`engine/Platform/Sdl` is `Platform`, `engine/Render/Gl` is `OpenGL`, `engine/Audio/Sdl` is `Audio`, and every other
folder of `engine/` is `Engine`. A new file joins its library by its folder, with no CMake change.

- What another binary uses (the game, the app, another library, a test) and is defined in a `.cpp` carries the
  library's macro, from the generated `<Library>Api.h`: `class ENGINE_API ByteReader`,
  `[[nodiscard]] ENGINE_API float RoundedSin(float x);`. Header-only code carries none.
- A link error that names an engine symbol (`undefined reference` on Linux, `LNK2019` on Windows) is a missing macro
  on its class or its function.
- No mutable state defined in a header, and an outside library goes in the library that owns it.

## Anatomy of a game module

- `<Module>.h`: what other modules may use. `<Module>Fwd.h`: forward declarations. `<Module>Defines.h`: enums and ids,
  no logic. These small headers are how a module shares declarations: there is no `Common.h` or umbrella header
  (`docs/CODING_RULES.md` §7).
- `<Module>State.h`: the part of the original's globals the module owns, as a plain struct.
- `In/`: one handler per incoming packet, a free function of up to ~80 lines that receives the packet already read and
  the state it touches, in its signature. It does not know the session.
- `Out/`: one action per player intent, which validates against the state and sends its packet.
- Windows and the module's own logic in files named after what they do.
- Something several modules need from outside (send a packet, request a sound, an on-screen message) comes through a
  `Kernel` port, not a bag of `std::function`.
- A value of a shared space (an opcode, an item code, a map, a sound, a model, a text line other modules print) is not
  defined again here: the module names its role with the home's name as the initializer, and a space the module owns
  has its home here ([CODING_RULES §7, "Named values"](../../../docs/CODING_RULES.md#named-values-2026-11-23)).

## When check_layers.py fails

Run it like CI does:

```bash
python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv \
  --map Client/docs/research/coverage/module-map.tsv --baseline Client/tools/architecture/layers-baseline.tsv \
  --clean-root engine
```

- **A file has no module:** add its row to the map (above).
- **A new include goes up a layer:** the code is in the wrong module, or what it needs belongs lower. Move the shared
  part down (a type to `<Module>Defines.h` of the lower module, a contract to `Kernel`) or invert the call with a port.
  Never edit the baseline by hand to hide a new violation.
- **A new include reaches a module of the same layer:** if the use is one-way and makes sense, add the module to the
  includer's `may_use` in `modules.tsv` and say why in the PR. If the other module already includes this one, that is a
  cycle: move what both share down to `Kernel` or merge them (spec, criterion 6).
- **A violation from the engine:** CI runs with `--clean-root engine`, so an include from `engine/` that breaks a
  layer fails even if the baseline lists it, and `--write-baseline` refuses to record it. Invert it with a port in the
  engine (AR1.1 did it for the sound catalog, the frame contracts and the model library).
- **Baseline rows that no longer happen:** rerun with `--write-baseline` and commit the smaller baseline.
- After changing the map or `modules.tsv`, rerun the check and its tests:
  `python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_layers.py'`.

## Creating a module

A new module is a row in `modules.tsv` (`module`, `root`, `layer`, `folders_today`, `may_use` starting with
`*layer<N` for its own layer, `note`) and its rows in the map. It needs a reason that fits the spec's criteria
(dependency direction decides, not size); propose it to Emanuel before adding it. An engine module has no target of its
own: it is a folder of its library. A game module gets its target in AR4.
