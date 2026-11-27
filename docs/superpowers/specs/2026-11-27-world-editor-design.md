# World editor: `muworldeditor`, a map editor on MuRet's engine (design)

> **Working document, 2026-11-27.** Emanuel's request, in his words translated: "in the future, a whole SDK to model
> scenarios and use objects and characters on the engine, like the Warcraft 3 map and character editor"; "the idea is
> to use our engine, because with the whole client rebuilt we could use ours"; "maybe create ours using the three you
> mentioned as examples, but for our engine ... another program called muworldeditor or so". Then: it must run on
> Windows and Linux; "the editor must be able to create maps, in a didactic way like the WoW editor does, place items
> of the game itself, create terrains the same way; in short, if not a copy of the war3 editor, very similar", "and if
> possible add or create new characters; I understand our engine can, and if not we'd see how to make it able". He
> named OpenMW (and its editor, OpenMW-CS) as the most interesting reference. Decided in the thread: start with the
> maps the 1.02 client and server already play (A), then new map numbers (B); he is the only user for now; the
> interface is Qt 6. Use the popular skills (clean code, patterns, architecture) and Context7 while building it.

## What and why

MuRet rebuilds MU Online 1.02's `main.exe`. Its engine already reads every file a world is made of, draws the terrain
and the objects exactly as the original does, and computes the same light. Nobody can change a map today without a
hex editor: there is no tool, and the engine writes none of those files.

`muworldeditor` is a desktop program, separate from the client and from `mutools`, that opens a MU world, lets
Emanuel sculpt its terrain, paint its ground, mark where one walks, place the world's objects, the server's monsters
and NPCs and the gates, and exports files that the client and the GameServer load. It draws with MuRet's own engine,
so what he sees while editing is what the client shows.

- **Why our engine and not a general one:** the only way to guarantee that the preview matches the game is to draw it
  with the code that draws the game. It also exercises the engine as a library, which is the point of the refactor's
  engine/game split (ADR 0028).
- **Why separate from the client:** Emanuel asked for "another program". The references that are separate programs on
  the same engine (OpenMW-CS, VCMI's editor) keep the game binary free of editor code.
- **Why separate from `mutools`:** `mutools` is a Python tool for `.bmd` files with a web studio. A world editor needs
  the C++ renderer in its window. The two meet at the files: `mutools` edits models, tables and actions, and the world
  editor places them.

## Goals, non-goals and anti-goals

**Goals (stage A, this design):**

- Open any world in a `Data` folder (`Client/assets/Data` or another edition) together with the GameServer's data
  folder, and edit a copy of it.
- Terrain: raise, lower, smooth, flatten and set heights with a brush; paint the two tile layers and their blend;
  paint the attributes (walkable, safe zone and the other bits); paint the light map.
- Objects: place, move, rotate, scale, copy and delete the world's objects, picked from the world's model table.
- Characters: place the server's monsters and NPCs (their spawn rows), and move and delete them.
- Gates: place and edit the gates that take a player from one map to another.
- Check the map before exporting, with a list of problems that points at each one.
- Export the client's files and the server's files byte-exact (see "Byte-exact"), so the 1.02 GameServer Emanuel uses
  and the original `main.exe` load them.
- Undo and redo every change.
- Easy to learn: the interface explains itself (tool tips, hints in the status bar, overlays that show what the tool
  will change), in the manner of the Warcraft 3 World Editor and VCMI's editor.
- Windows and Linux, from the same CMake and vcpkg as the client.
- An agent-friendly command line for the operations that need no window (check, export, round trip, information).

**Goals of stage B (later, not designed here):** new map numbers, new monster and NPC types with their own models,
models from other folders (items, monsters) as decoration, and content kept as a layer over the original data (as
OpenMW's add-ons do). See "Stage B".

**Non-goals:** new playable classes (they touch the protocol and the server); editing `.bmd` models, textures or
actions (that is `mutools`); a scripting or trigger language like Warcraft 3's; multi-user editing; an installer and
translations (he is the only user for now).

**Anti-goals (failure even if it works):**

- A preview that differs from what the client draws.
- Writing into `Client/assets/Data` or into the server's folder in place.
- An export that the original `main.exe` or the GameServer 1.02 rejects or reads differently from the editor.
- Code copied from the GPL or AGPL references, or vendored sources (ideas only; libraries through vcpkg).
- Qt types leaking into the editing core, so that the core cannot be tested or driven without a window.

## What the references teach

Measured on 2026-11-27 with GitHub's search API (stars that day).

| Reference | ★ | Its editor | What we take |
| --- | --- | --- | --- |
| [OpenMW](https://github.com/OpenMW/openmw) (Morrowind) | 6,788 | OpenMW-CS, a separate program on the engine, Qt | Tables of the game's records beside the 3D view, drag from a table into the scene, a "Verification Results" table, edits kept apart from the base game |
| [VCMI](https://github.com/vcmi/vcmi) (Heroes 3) | 6,048 | `vcmieditor`, a separate program | Brushes of 1, 2 and 4 cells and rectangle selection, a passability view, an object browser to drag from, an inspector, "Fill" for automatic obstacles; opens the original maps, saves its own |
| [OpenRA](https://github.com/OpenRA/OpenRA) (C&C) | 17,846 | inside the game | Imports the original maps into its format; a check command (`--check-yaml`) |
| [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2), [OpenTTD](https://github.com/OpenTTD/OpenTTD) | 16,614, 8,534 | scenario editors inside the game | That a rebuilt engine can host its own editor |
| [fheroes2](https://github.com/ihhub/fheroes2) (Heroes 2) | 3,738 | inside the game since 1.1.0 | Its own license-free map format |
| [HiveWE](https://github.com/stijnherfst/HiveWE) (Warcraft 3) | 491 | separate, C++20, vcpkg, Qt | Our stack; terrain, pathing and object tools of the WC3 editor |
| [Noggit](https://github.com/wowdev/noggit3) (WoW 3.3.5a) | 206 | separate, C++, Qt 5, OpenGL | Sculpt and paint brushes with radius and strength |
| [Tiled](https://github.com/mapeditor/tiled), [TrenchBroom](https://github.com/TrenchBroom/TrenchBroom) | 13,098, 2,927 | general level editors, Qt | Tool palettes, docks, undo history, how a didactic editor feels |
| [VDraven/MuOnline-WorldEditor](https://github.com/VDraven/MuOnline-WorldEditor) | 57 | a DLL inside the Season 16 `main.exe` | The only MU world editor on GitHub; nothing popular exists for MU |

Two families exist: an editor inside the game (OpenRA, OpenRCT2, OpenTTD, fheroes2, VDraven's, and the one Webzen left
in the 1.02 `main.exe`, see below) and a separate program on the same engine (OpenMW-CS, VCMI, HiveWE, Noggit).
`muworldeditor` belongs to the second. Every reference reads the original maps and keeps its own project; most write
their own format. Stage A must write the original's formats instead, because the original client and server have to
load them.

## What a 1.02 world is

A world is a folder `Data\World<N>\` (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo") on a grid
of 256 × 256 cells, cell `(x, y)` at `y * 256 + x`, plus rows in the server's files.

| File | Holds | Original reader | MuRet reader | Original writer (in `main.exe`) |
| --- | --- | --- | --- | --- |
| `EncTerrain<N>.map` | tile layer 1, tile layer 2, blend alpha (`0x30002` bytes, K16) | `0x5f87b0` | `engine/Data/Terrain/TerrainMapping` | `0x5f8960` (writes `0x30002`, K16) |
| `EncTerrain<N>.att` | attributes per cell, 1 or 2 bytes (`0x10004` or `0x20004`, XOR3 + K16) | `0x5f8390` | `TerrainAttributes` | `0x5f8690` (writes `0x10004`) |
| `EncTerrain<N>.obj` | objects: type (int16), position, angle (3 floats each), scale; 30 bytes each (K16) | `0x607370` | `TerrainObjects` | not located yet |
| `TerrainHeight.OZB` | heights, a 256 × 256 grayscale BMP after 4 bytes | `0x5f9020` | `TerrainHeights` | `0x5f92b0` (a development tool) |
| `TerrainLight.OZJ` | base light, a JPEG after 24 bytes | `0x63b490` | `TerrainLight` | the OZJ/OZT exporter `0x63b280` |
| `Tile*.OZJ/OZT` | the tile textures | the texture loaders | `Data/Textures` | `0x63b280` |
| `Object<N>\Object<k>.bmd` | the world's 160 object models (`k` from 1 to 160; Lorencia names its 112) | `0x616ec0` | `game/Assets/WorldModels` | — |

The folder of each map comes from `0x61a560` (maps 11 to 17 share `World12`, 18 to 23 `World19`, 24 to 29 and 36
`World25`; two `.att` names change with the game's state). `0x61ada0` exports loose `Terrain.map/.obj/.att`
(a development tool); nothing loads them.

**Webzen's own editor is still in the binary.** `main.exe` keeps an editor mode that nothing turns on: the flags
`0x77f3b18` (terrain picking and clicks change with it, 06 "picking del modo editor") and `0x7d366bc` (with it,
`0x517570` writes `Data2\MonsterSetBase2.txt`), the function `0x5acbe0` with no callers (it reads the keys A, D, E, F,
Q, R, S and W, 02 "teclas"), and the savers above. They are the reference for every writer: what the original wrote
is what the original reads.

**The server's side.** The target is the server Emanuel runs, the repack's GameServer 1.02 with MyDll ("1.02I MX",
`C:\RE\MuServer\data` on the VM2003). The research repo has its reconstruction
(`mydll-gameserver/decompilacion-completa/reconstruction`, `gameserver/src/reconstructed/win32-vc6`) and a sample of
its files (`client-research/mu-golden/servidor-en-uso-vm2003/`: `Terrain1.att`, `MonsterSetBase.txt`, `gate.txt`).

- `Terrain<N>.att`: the server's copy of the attributes, 65,539 bytes, unencrypted: the header `00 FF FF` and one
  byte per cell (the sample's `Terrain1.att`). The stock GameServer 1.02 loads a fixed table of 17 maps
  (`GameMain.cpp`, `0x48a309`); MyDll's hook loads maps 0 to 34 (`mydll_gshook.cpp`: `Terrain1.att` to
  `Terrain11.att` for maps 0 to 10, `Terrain12.att` for 11 to 17, `Terrain19.att` for 18 to 23, and `Terrain25`, `31`,
  `32`, `34` and `35` for maps 24, 30, 31, 33 and 34). Those are the maps stage A edits on both sides.
- `MonsterSetBase.txt`: one file for every map, in numbered sections; the fixed NPCs' rows are
  `monster map radius x y direction`, and the monsters' sections spread a count over an area. The original client's
  editor mode wrote a `MonsterSetBase2.txt` (`0x517570`).
- `gate.txt` on the server and `Gate.bmd` in the client: the gates.
- `Monster.txt`: the monster types (read only, for names and to check spawns). The research repo does not have it,
  nor the server's `.att` of maps 1 to 34; they come from the server's folder.

**The client and the server already disagree, on purpose.** The research measured it
(`client-research/analysis/mainexe/evidence/s31-assets/divergencias-cliente-servidor.md`): the server's
`Terrain1.att` differs from the client's `EncTerrain1.att` in 70 cells of Lorencia's square, 51 gates exist only on
the server and 5 only in the client, 36 shared gates ask for another level, and rows 108 to 128 are reassigned. The
research calls them the server's customizations. The editor keeps both copies as they are (see "Two copies").

**What this means for stage A:**

- The grid stays 256 × 256 and the map numbers stay those the client and the server in use know (0 to 34). A "new
  map" in stage A takes the place of an existing one.
- An object's type is a slot of that world's 160 models. A free slot (no `Object<k>.bmd` on disk; the original skips a
  missing model silently, Q-C23) can take a new model, which is how a model from elsewhere (an item, say) becomes
  decoration in stage A. Whether a slot is free also depends on the family's special models and on the map's scripts
  that key on types (05, "Los modelos de cada mundo"); the editor checks both.
- Monsters and NPCs are server rows of types the server's `Monster.txt` and the client's monster model table already
  know. New types are stage B.
- In worlds 1 to 5 the original client compares one fixed `.att` cell with an expected value and stops with "Data
  error" if it differs (Q-C26, `0x5f859e`). The editor never changes that cell without saying so.

## Approaches

Decided with Emanuel on 2026-11-27: C++20 (it links the engine directly; Rust would need a C layer in front of every
class and a second build) and **Qt 6** for the interface.

1. **Qt 6 Widgets** (chosen). Menus, dockable panels, model/view tables (what OpenMW-CS's tables are), dialogs and
   shortcuts come built. Our renderer draws inside a `QOpenGLWidget`. Cost: a large dependency that vcpkg builds from
   source (long the first time, then cached), its own event loop, LGPL v3 (linked dynamically). Most popular editors
   that are separate programs use it (OpenMW-CS, VCMI, HiveWE, Noggit, TrenchBroom, Tiled, O3DE).
2. **Dear ImGui** with docking, in an SDL3 window of our `Platform` (as Hazel, Lumix and Overload do). Reuses the
   window and the loop as they are, MIT, small and fast to build. Cost: it looks like a programmer's tool, and tables,
   undo, dialogs and on-screen help would be ours to build. Not chosen: "didactic, like WC3" is what Qt gives built.
3. **An editor mode inside our client**, as Webzen had and OpenRA has. Not chosen: Emanuel wants a separate program,
   and it would put editor code in the game binary.
4. **A web page** like the `mutools` studio. Not chosen: it would not draw with our engine.

## The design

### Where it lives

A fourth include root, `Client/src/editor/`, beside `engine/`, `game/` and `app/`, at the same layer as `app/` (12):
the editor and the client's `app/` never include each other. Its modules get rows in `Client/modules.tsv`, so
`check_layers.py` checks them like any other. AR4's closing criterion ("`Client/src` holds `engine/`, `game/` and
`app/` and nothing else") gains `editor/` in the PR that creates it. Tests go in `Client/tests/editor/`.

```text
Client/src/editor/
  Project/    the working copy: open, create from a Data folder and a server folder, save, export
  ServerData/ the server's files: Terrain<N>.att, MonsterSetBase.txt, gate.txt, Monster.txt (read only)
  Document/   the world being edited: layers, objects, spawns, gates; commands and the undo stack
  Tools/      brushes and placement: what a stroke or a click changes, as commands, with no input or drawing
  Checks/     the rules that find problems before export
  Cli/        `muworld`, the command line (CLI11), JSON for agents
  Ui/         Qt: the main window, docks, tables, the viewport widget, the tools' input
  App/        the two composition roots: muworldeditor's main and muworld's main
```

Only `Ui/` and `App/` see Qt: the other modules' targets do not link it, so a Qt include there does not compile. The
writers of the client's formats are engine code, beside their readers (`engine/Data/Terrain`, plus an `EncryptK16` in
`engine/Crypto` and the `Gate.bmd` writer beside `game/Tables/GateTable`), because they encode the same knowledge.
`ServerData` stays in the editor until another program needs it (a future GameServer port would take it then).

### Layers (ports and adapters)

```text
App (composition roots)
 ├─ Ui (Qt adapter) ──────┐
 └─ Cli (CLI11 adapter) ──┤
                          ▼
     Tools · Checks · Project
                          ▼
                     Document ──── ServerData
                          ▼
     engine/Data (readers and writers) · game/Tables · game/Assets · game/Terrain
```

- `Document` owns the state and the only way to change it: a `Command` that applies and reverts itself (Command
  pattern). A terrain stroke stores the rectangle of cells it touched, before and after, so undo is cheap and exact.
  The undo stack is ours, in `Document`; the UI shows it (Qt's `QUndoStack` would put Qt in the core).
- `Tools` turn an intent (a stroke from here to there with this brush, a click that places this model) into commands.
  They take positions in world units and cells, never mouse events, so tests drive them directly.
- `Checks` read a `Document` and return findings with a stable code, a severity, the place (cell, object, spawn) and
  a message; the UI lists them and jumps to each one, the CLI prints them as JSON.
- `Project` reads and writes the working copy and exports; it is the only module that touches the file system.
- `Ui` is thin: it maps Qt events to tools and the document's changes to widgets (observer: the document publishes
  what changed; views redraw what they show).
- The composition roots wire everything by hand, as `app/` does (no container, no singletons).

### The project: a working copy, never the originals

As in `mutools` (`mutools workspace init`), the originals are read-only. A project is a working copy of a whole
install, not of one map, because three of its files hold every map (`MonsterSetBase.txt`, `gate.txt`, `Gate.bmd`),
world folders are shared (`World12` serves maps 11 to 17) and a gate joins two maps. Two projects exported into one
install would undo each other's edits in those files. A project is a folder:

```text
<name>.muworld/
  project.json        schema version; the client Data and the server data it started from (paths and SHA-256 of
                      every file it copied)
  client/             the client files it changed, at their Data paths (World<N>/..., Object<N>/..., Gate.bmd)
  server/             the server files it changed (Terrain<N>.att, MonsterSetBase.txt, gate.txt)
```

A file enters the project the first time an edit touches it (copy on write); everything else is read from the
originals. Any map of the install opens from the same project, and the gate tool edits both ends of a gate. The
files stay in their original formats because the writers are byte-exact: there is no second format to keep in step,
and any tool that reads MU files reads a project. Saving writes only what changed. **Export** runs the checks,
refuses on an error, and writes every changed file whole into a folder laid out as `Data\` and as the server's data
folder, which Emanuel then copies over a test install. If an original changed since the project copied it (its
SHA-256), export stops and says which. The editor never writes into a `Data` or a server folder it did not create.
(My call; the builder check asked whether a project is one map, one world folder or a whole install.)

### Two copies: the client's and the server's

The attributes and the gates live twice, and the copies differ today on purpose (see "The server's side"). The
`Document` holds both:

- A cell nobody painted keeps each copy's own value, so the server's customizations survive an export.
- A stroke of the attribute brush writes the client's and the server's copies by default; the brush can be set to
  one side only.
- The attribute overlay can show the client's copy, the server's, or where they differ.
- Gates are listed by number with both rows side by side. Editing a gate changes the rows it has; a gate that exists
  on one side only is marked so, and a new gate goes to both.

(My calls, from the builder check.)

### Byte-exact

Two promises, each with its tests:

1. **Round trip.** Reading and writing a file with no edit gives the same bytes, for every world in
   `Client/assets/Data` (all of `EncTerrain*.map/.att/.obj`, `TerrainHeight.OZB`, `TerrainLight.OZJ`) and for the
   server sample. A file whose bytes carry something the reader drops (the 4 leading bytes of the OZB, the BMP header
   the original copies and ignores, the 24 bytes before the OZJ's JPEG) keeps it in the document.
2. **Edits follow the original's writer.** A changed file is written as the original's saver writes it (`0x5f8960`,
   `0x5f8690`, `0x5f92b0`, `0x63b280`): sizes, headers, the world byte, the encryption. Where the original's writer is
   not located (`.obj`), the layout is the reader's, and the plan says so. The original's `.att` saver writes one
   byte per cell (`0x10004`); a world whose file has two (`0x20004`: in `Client/assets/Data`, World1 and World31
   on) keeps its own layout.

An unchanged layer is never re-encoded. This matters for `TerrainLight.OZJ`: a JPEG re-encoded is not the same file,
so the light is written only when it was painted, with the original exporter's JPEG settings (`engine/Data/Textures/
JpegWriter`).

### What the editor edits

| Tool | Changes | Notes |
| --- | --- | --- |
| Sculpt: raise, lower, smooth, flatten, set | `TerrainHeight.OZB` | Brush radius and strength, a falloff; heights are bytes, so a stroke rounds the way the file stores them |
| Paint tiles | `.map` layers 1 and 2 and the alpha | The world's tile textures as a palette with thumbnails |
| Paint attributes | `.att` and the server's `Terrain<N>.att` | One layer per bit, shown as a colored overlay; both copies by default (see "Two copies") |
| Paint light | `TerrainLight.OZJ` | A color and a strength |
| Objects | `.obj` | Place, move, rotate, scale, copy, delete; snap to the ground and to the grid |
| Monsters and NPCs | the map's rows of `MonsterSetBase.txt` | Type from `Monster.txt`, position, radius, direction; the other maps' rows stay byte for byte |
| Gates | `gate.txt` and `Gate.bmd` | Source area and target map and cell; the target must be walkable; both ends editable |

### The views

```text
┌ Tools ──┬──────────────── 3D view ────────────────┬ Inspector ────┐
│ sculpt  │                                         │ brush: 4      │
│ paint   │         (the world as the client        │ strength: 30% │
│ attrib. │          draws it, with the brush       │ falloff: soft │
│ objects │          circle and the overlays)       ├ Minimap ──────┤
│ spawns  │                                         │ (top view)    │
│ gates   ├ Browser: objects · tiles · monsters ────┤               │
│ light   │ Problems · History                      │               │
└─────────┴─────────────────────────────────────────┴───────────────┘
 status bar: "Drag to raise; Shift lowers; Ctrl smooths"        x 128 y 97
```

- **3D view:** the main view, drawn by the engine (below). A free camera (orbit, pan, zoom, like WC3's) and a toggle
  to the client's own game camera. Overlays: the brush circle on the ground, the grid, the attributes, spawn radii,
  gate areas.
- **Minimap:** a top view of the whole 256 × 256 grid (heights shaded, tiles, attributes), where a click moves the
  camera; also useful before the 3D view loads.
- **Browser:** tables of the world's object models, its tiles and the server's monster types, with thumbnails the
  engine renders off screen, filter and search; drag a row into the 3D view to place it (OpenMW-CS, VCMI).
- **Inspector:** the selected thing's fields, editable, and the active tool's settings.
- **Problems:** the checks' findings; a double click goes to the place (OpenMW-CS's verification table).
- **History:** the undo stack, named commands, click to go back to a point.

The didactic part is in these: every tool has a tool tip with its shortcut and a status-bar hint while it is active,
the overlay shows what a stroke will change before the click, and the problems explain what is wrong and what to do.

### Drawing with the engine

- The 3D view is a `QOpenGLWidget` with a 3.3 core `QSurfaceFormat`. Our GL function table is filled by
  `LoadGlFunctions(lookup)` (`engine/Platform/Sdl/GlLoader.h`), which already takes the lookup as a parameter, here
  `QOpenGLContext::getProcAddress`. The table and the loader name SDL types today, so the editor either links
  `Platform` (and SDL) only for them, or they move to a place without SDL; that is an engine change for its own PR,
  with Emanuel's yes (it touches ADR 0028's cut).
- `QOpenGLWidget` draws into its own framebuffer object, not into framebuffer 0: the renderer draws into whatever
  framebuffer is bound when the frame starts, and the editor's tests check it.
- The view draws on demand (after a change, a camera move or while an animation plays), like the `mutools` studio,
  not in a loop. Animated objects step at the client's rate only while "play" is on.
- What draws a world today is `Scenes/GameScene` (`DrawTheWorldPass`), which also expects a session. The editor needs
  "draw this world from this camera" without one. AR4.1 already put `Terrain`, `Assets` and `Camera` in their modules;
  AR4.3 moves `WorldObjects` and `MapScripts` (each map's object behavior); Task 41 builds the `Game` library. The
  editor's 3D view therefore comes in two steps: first the terrain and the objects in their rest pose, drawn by
  `game/Terrain` and `engine/Models` linked through `ClientLib`; then the per-map behavior (water, fire, animated
  objects) once AR4.3 has landed, with `Game` replacing `ClientLib` after Task 41. The function that draws a world
  without a session is extracted for the editor and the client to share, in a PR the refactor plan agrees with, never
  as a copy.
- Monsters and NPCs are drawn as their models at their spawn cells, facing their direction, in the rest pose of
  their first action (looping it while "play" is on), from the client's monster model table (`game/Assets`) through
  `engine/Models`; an area spawn shows its area and count as an overlay. This does not wait for the character drawing
  to leave `GameScene`: a monster's per-type extras (its glows and effects) join with the world's behavior in stage 6.
  (My call, from the builder check: models, not markers, because Emanuel asked for an editor like Warcraft 3's.)
- Picking reuses `game/Terrain/TerrainPicking` and `engine/Models/RayPicking`; the walkable test reuses
  `engine/Pathfinding`, so the editor can show the path a character would take between two cells.

### The checks

Each with a stable code, in the manner of `mutools`' findings. A first list:

- An object whose type is not a loaded model of the world, or outside the grid, or more objects than the file's
  int16 count allows.
- A spawn or a gate target on a cell that is not walkable, or outside the grid; a spawn type the server's
  `Monster.txt` does not have; more spawns than the server's table holds.
- A cell the editor changed where the client's and the server's attributes now differ (a warning, never an error:
  untouched cells differ on purpose).
- Worlds 1 to 5: the cell the original client checks (Q-C26) changed.
- A model put in a free slot that a map script or a family's special models also use.
- A file that would not be written as the original's writer writes it.

### The command line for agents

`muworld`, a second small executable without Qt (CLI11, already in vcpkg), following the `ai-native-cli` skill: JSON
by default with the envelope and the stable error codes `mutools` uses, `--dry-run` for a batch, self-description.

```text
muworld project create --data <Data> --server <server data> <name>.muworld
muworld info <project>
muworld check <project>
muworld export <project> <out>
muworld roundtrip <Data> [--server <server data>]
muworld apply <project> <patch.json>      objects, spawns and gates as JSON Patch, as `mutools apply`
```

Terrain painting stays in the window; an agent edits the tables (objects, spawns, gates) and checks. A project skill
like `muret-bmd` teaches an agent to use it, with its recipes run as tests.

### Build, dependencies and CI

- **vcpkg:** a manifest feature `editor` adds `qtbase` (6.11.2 at the current baseline) with
  `default-features: false` and only what the editor needs (`gui`, `widgets`, `opengl`, `png`, `freetype`; on Linux
  `xcb`, `xkb` and `wayland`). The client's build never asks for it, so its CI does not build Qt.
- **CMake:** `MU_BUILD_EDITOR` (off by default) and presets that turn it on with `VCPKG_MANIFEST_FEATURES=editor`.
  Targets named as the repo names them (`mu_` functions, no `MuRet` prefix); executables `muworldeditor` and
  `muworld` in `build/<preset>/bin/` beside the engine's libraries.
- **CI:** a separate workflow for the editor, Linux (GCC) and Windows (MSVC), that runs when the editor, the engine or
  the game change, with vcpkg's binary cache; never a required check and never `paths-ignore` on the client's
  workflow (a skipped required check blocks merges).
- **Linking Qt dynamically** (LGPL v3): the Linux triplet (`cmake/triplets/x64-linux-shared-sdl.cmake`) builds
  `qtbase` shared, as it does SDL3 (per-port linkage); Windows' triplet is dynamic already.
- **Windows release:** `windeployqt` puts Qt's libraries beside the executables; Linux takes vcpkg's shared Qt beside
  them as SDL3 is today.

### Tests

- Round trip of every world file in `Client/assets/Data` and of the server sample (byte-exact, "Byte-exact" above).
- Each writer against the original's saver: a small edit, then the bytes the original's writer would produce at the
  edited offsets.
- `Document`: every command applies and reverts to the same bytes; a stroke's undo restores exactly its rectangle.
- `Tools`: a brush on known heights gives known heights (no window).
- `Checks`: one test per rule, with a project that breaks only it.
- `muworld`: the JSON envelope, error codes and exit codes, as `mutools` tests them.
- `Ui`: a few Qt Test tests (`QTest`) on widgets and the viewport's framebuffer; drawing comparisons reuse the
  engine's golden machinery (`GlFramebuffer::ReadPixels`).
- A check in the VM (the original client and the server in use load an exported map) closes stage A, as the client's
  VM checks do: last.

### Stage B (later)

Not designed here; what stage A leaves ready:

- **New map numbers:** our client's map table (folder, name, music, scripts) read from data instead of `0x61a560`'s
  comparisons, and a server that knows the new number (the server in use loads maps 0 to 34 only).
- **New monsters and NPCs:** a `Monster.txt` row, a client entry from monster type to model, and the model from
  `mutools`.
- **Any model as decoration,** loaded per world from a list instead of the 160 fixed slots.
- **Content as a layer** over the original `Data` (OpenMW's add-ons): our client reads an overlay folder before
  `Data`.

## Stages

One PR or a milestone block each, each with its plan in `docs/superpowers/plans/` and Emanuel's yes before code:

1. **Write the world's client files:** writers beside the readers, `EncryptK16`, and the round trip of every world in
   `Client/assets/Data`. No editor yet.
2. **The server's files:** `editor/ServerData` (`Terrain<N>.att`, `MonsterSetBase.txt`, `gate.txt`, `Monster.txt`),
   the `Gate.bmd` writer, and their round trip on the server sample.
3. **The editing core and `muworld`:** `Project`, `Document`, commands and undo, `Tools`, `Checks`, the CLI and its
   skill.
4. **The Qt window:** the vcpkg feature, `MU_BUILD_EDITOR`, the CI workflow, the main window with its docks, the
   minimap, the browser, the inspector, problems and history, and editing end to end on the minimap.
5. **The 3D view:** the GL table fed by Qt, the terrain and the objects drawn by the engine, the camera, picking, the
   brush on the ground and the overlays; every tool in 3D.
6. **The world alive and the details:** the per-map behavior (after AR4.3), the light brush, thumbnails, the path
   preview, and the VM check that closes stage A.

## Left to the builder

- Locating the original's `.obj` writer (or confirming there is none) and the JPEG settings of `0x63b280`, in the
  research repo.
- Where exactly the GL table and its loader go if they leave `Platform/Sdl` (its own PR, with Emanuel's yes).
- The extraction of "draw a world without a session" from `GameScene`, agreed with the AR4/AR5 plans.
- The server's files the research repo lacks (`Monster.txt`, the server's `.att` of maps 1 to 34): asked to Emanuel
  with the spec; until they arrive, stage 2 tests what the sample has and the monster browser and spawn checks read
  `Monster.txt` only when the project's server folder has it.
- `MonsterSetBase.txt`'s sections and their row shapes, keeping every comment and blank line of the file.
- The exact Qt features per platform, once vcpkg builds them on both CI runners.
- The checks' codes and messages, the brushes' falloff curves, the keyboard shortcuts.

## Sources

- OpenMW-CS manual: <https://openmw.readthedocs.io/en/latest/manuals/openmw-cs/index.html>
- VCMI map editor: <https://vcmi.eu/modders/Map_Editor>
- OpenRA mapping: <https://github.com/OpenRA/OpenRA/wiki/Mapping>
- fheroes2 1.1.0: <https://www.gamingonlinux.com/2024/05/heroes-of-might-and-magic-ii-game-engine-fheroes2-v110-released/>
- Qt 6 `QOpenGLWidget` and `QSurfaceFormat` (through Context7, `/websites/doc_qt_io_qt-6`):
  <https://doc.qt.io/qt-6/qopenglwidget.html>
- Dear ImGui docking (through Context7, `/ocornut/imgui`): <https://github.com/ocornut/imgui/wiki/Docking>
- vcpkg ports at the repo's baseline `9a5dfef5`: `qtbase` 6.11.2, `imgui` 1.93.0.
