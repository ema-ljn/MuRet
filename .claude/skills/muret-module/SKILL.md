---
name: muret-module
description: Use when adding a file to the MuRet client, deciding which module a file belongs to, building a game module (its public header, state, In/ and Out/), or when check_layers.py rejects an include or a file without a module.
---

# Client modules in MuRet

The client is split into 58 modules in three roots (`engine/`, `game/`, `app/`), each with a layer. This skill is the
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
3. Files in `Core/`, `Input/`, `Audio/`, `Network/`, `Data/`, `Platform/`, `Render/`, `Protocol/` and `App/` fall back
   to their folder's module (`folders_today`) when they have no row. Files in `Scenes/`, `UI/`, `World/` and
   `GameLogic/` always need a row.
4. If the module's folder does not exist yet, the file goes in today's folder with the new anatomy (below); the move of
   its module carries it later.

## Anatomy of a game module

- `<Module>.h`: what other modules may use. `<Module>Fwd.h`: forward declarations. `<Module>Defines.h`: enums and ids,
  no logic.
- `<Module>State.h`: the part of the original's globals the module owns, as a plain struct.
- `In/`: one handler per incoming packet, a free function of up to ~80 lines that receives the packet already read and
  the state it touches, in its signature. It does not know the session.
- `Out/`: one action per player intent, which validates against the state and sends its packet.
- Windows and the module's own logic in files named after what they do.
- Something several modules need from outside (send a packet, request a sound, an on-screen message) comes through a
  `Kernel` port, not a bag of `std::function`.

## When check_layers.py fails

Run it like CI does:

```bash
python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv \
  --map Client/docs/research/coverage/module-map.tsv --baseline Client/tools/architecture/layers-baseline.tsv
```

- **A file has no module:** add its row to the map (above).
- **A new include goes up a layer:** the code is in the wrong module, or what it needs belongs lower. Move the shared
  part down (a type to `<Module>Defines.h` of the lower module, a contract to `Kernel`) or invert the call with a port.
  Never edit the baseline by hand to hide a new violation.
- **A new include reaches a module of the same layer:** if the use is one-way and makes sense, add the module to the
  includer's `may_use` in `modules.tsv` and say why in the PR. If the other module already includes this one, that is a
  cycle: move what both share down to `Kernel` or merge them (spec, criterion 6).
- **Baseline rows that no longer happen:** rerun with `--write-baseline` and commit the smaller baseline.
- After changing the map or `modules.tsv`, rerun the check and its tests:
  `python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_layers.py'`.

## Creating a module

A new module is a row in `modules.tsv` (`module`, `root`, `layer`, `folders_today`, `may_use` starting with
`*layer<N` for its own layer, `note`) and its rows in the map. It needs a reason that fits the spec's criteria
(dependency direction decides, not size); propose it to Emanuel before adding it. Its CMake target comes with the move of
its root (AR1.2 for the engine, AR4 for the game modules).
