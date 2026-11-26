---
name: muret-move
description: Use when moving MuRet client files to their module folder (a refactor move PR), recording a merged move in .git-blame-ignore-revs, or writing the comments PR that follows a move. Covers move.py, check_pure_move.py, check_comments_only.py and the PR titles CI reads.
---

# Moving client files to their modules

A move only moves files: `move.py` generates it from
[`module-map.tsv`](../../../Client/docs/research/coverage/module-map.tsv) and `check_pure_move.py` proves it. The comments
of the moved code are trimmed in a separate PR after the move's squash, and logic changes go in the design PR after that
([plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md), Global Constraints). Which module a
file belongs to is the [`muret-module`](../muret-module/SKILL.md) skill.

## The move PR

1. Start from the base up to date, on a branch named after the milestone (`refactor/ar1.2-engine-move`). Nobody ports
   into the modules being moved that day.
2. Run the script with the milestone's modules, or a whole root:

   ```bash
   python3 -I Client/tools/architecture/move.py --modules game/Guild,game/Items
   python3 -I Client/tools/architecture/move.py --root engine
   ```

   It runs `git mv` to `Client/src/<target>/<name>`, rewrites the `#include` lines whose path inside its root changed
   (`"Scenes/GuildState.h"` becomes `"Guild/GuildState.h"`; `"Core/Expected.h"` stays when only its root changes), the
   moved paths in `CMakeLists.txt`, `*.cmake` and the workflows, the `file` column of the map, of
   `layers-baseline.tsv` and of `named-values-baseline.tsv`, a `home` of `named-values.tsv` that names a moved header,
   and adds each move to `entradas/moves.tsv` without its commit.
3. Read what it printed and do by hand only that:
   - `names src/X/, which the move emptied`: a folder in a build file (a `list(FILTER … REGEX "/src/Platform/")`) that
     now has to name the new folder.
   - `Client/src/<root> is new`: the first move into a root needs that folder as an include directory (the milestone's
     targets, or `target_include_directories(ClientLib PUBLIC src/<root>)` until then).
4. Check before pushing, and let CI build (the cloud container cannot build the client):

   ```bash
   python3 -I Client/tools/architecture/check_pure_move.py --base origin/refactor/client --head HEAD
   python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv \
     --map Client/docs/research/coverage/module-map.tsv --baseline Client/tools/architecture/layers-baseline.tsv
   ```

   `check_pure_move.py` compares each renamed file's lines without its includes, blank lines and export macros (a file
   that leaves the engine drops its `ENGINE_API`), and lists any line that changed or went missing. It does not look at
   the files that were only edited: in a move those are include lines and build paths, so read
   `git diff -M50% --diff-filter=M` before pushing.
5. Commit and title the PR `refactor(AR<epic>.<milestone>): move <what> to Client/src/<root>`. CI's "Refactor moves and
   comments" step runs `check_pure_move.py` on every PR titled `type(ARn)` or `type(ARn.m)`.
6. If the base moves before the merge, merge it in and rerun `move.py` with the same modules: it moves the files the
   base added to those modules and skips the ones already moved. An include the base added to an old path fails the
   build; give it the new path.
7. After the squash, record it so `git blame` and the coverage waves skip it (LEEME of the reference, "When a refactor
   moves code"), in the next PR:

   ```bash
   python3 -I Client/tools/architecture/move.py --record <squash hash>
   ```

   It adds the hash with its subject to `.git-blame-ignore-revs` and fills the commit of the new rows of `moves.tsv`.

## The comments PR

After the move's squash, on its own branch, titled `docs(AR<epic>.<milestone>): trim and translate comments`:

- Each comment of the moved code becomes the one-line citation (`// 0x4d3200`) plus a short why the code cannot say, in
  English (CODING_RULES §9). Longer explanations go to a note in `Client/docs/research/`.
- Check it, as CI does for every `docs(ARn.m)` title:

  ```bash
  python3 -I Client/tools/architecture/check_comments_only.py --base origin/refactor/client --head HEAD
  ```

  It fails if a code token changed (text inside a string or a raw string is code: the GLSL `//` lines of
  `Render/Gl/Shaders.h` stay), if a file other than a `.h`, `.cpp`, `.hpp`, `.inc` or a research `.md` changed, or if an
  address a comment cited in the base is no longer cited in that file.
- After its squash, `move.py --record <hash>` adds it to `.git-blame-ignore-revs` too.

## The tools' tests

```bash
python3 -I -m unittest discover -s Client/tools/architecture -p 'test_*.py' -v
```
