# Refactor de la arquitectura del cliente: plan por épicas

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** partir el cliente en un motor, un protocolo, un núcleo y módulos de juego chicos, con dependencias en un
solo sentido y sin la misma lógica escrita varias veces, sin cambiar un byte del comportamiento y sin perder el rastreo
de lo que falta portar.

**Architecture:** tres raíces de include con sus targets (`engine/`, `game/`, `app/`), una tabla de opcodes con un
handler por paquete, el estado del original partido por módulo y las escenas como guiones ordenados del frame. Se
llega por épicas (AR0 a AR7): primero la red de seguridad y el rastreo, después el motor, el protocolo y el estado, y al
final `Scenes/` partido en módulos, de a un grupo por hito.

**Tech Stack:** C++20, CMake 3.25 (`FILE_SET HEADERS`, `VERIFY_INTERFACE_HEADER_SETS`), GoogleTest, Python 3 con la
biblioteca estándar y `unittest` para las herramientas, GitHub Actions.

**Spec:** [`docs/superpowers/specs/2026-11-20-arquitectura-cliente-design.md`](../specs/2026-11-20-arquitectura-cliente-design.md)

**Anexo:** [`2026-11-20-refactor-arquitectura-cliente/`](2026-11-20-refactor-arquitectura-cliente/): el mapa de los
1.418 archivos de `Client/src` a su módulo de destino (`mapa-modulos.tsv`, semilla), los 26 módulos candidatos de
`Scenes/` con sus dependencias y el estado que tocan, los 100 pares que se incluyen mutuamente y las 132 interfaces y
bolsas de parámetros.

## Global Constraints

- Ningún PR del refactor cambia comportamiento: el golden de comportamiento (AR0.3) y todos los tests y goldens de hoy
  quedan idénticos en Linux y Windows, Debug y `math-release`. Una diferencia es un bug del refactor, no un golden
  para regenerar.
- Una mudanza (PR que solo mueve archivos) la genera `Client/tools/arquitectura/mover.py` desde `mapa-modulos.tsv` y
  la verifica `check_pure_move.py`. Nunca mezcla lógica: la lógica va en el PR de diseño que la sigue.
- Los commits y los títulos de PR del refactor llevan `AR<épica>.<hito>` (por ejemplo `AR1.2`) y nunca `CLn`, para que
  `git blame` no les asigne una ola.
- El orden de las llamadas y de los `rand()` del original no cambia; todo reparto entre módulos es una lista ordenada.
- Una carpeta que solo cambia de raíz no cambia sus includes (`"Core/Expected.h"` sigue igual: cambia la raíz de
  include del target); un archivo que cambia de carpeta (`Scenes/X.h` → `Guild/X.h`) sí, y `mover.py` reescribe esos
  includes y nada más.
- Los scripts nuevos usan solo la biblioteca estándar, se corren con `python3 -I` y sus tests con `unittest`.
- Máximo 3 agentes en paralelo (Emanuel, 2026-11-19), contando los del port y las revisiones.
- Cada paso usa la skill que le toca ("Las skills de cada paso"), y las tres skills nuevas del repo nacen con lo que
  describen.
- PRs grandes por hito (Emanuel, 2026-11-11), con la mudanza y el diseño en PRs separados (la mudanza se revisa por
  script; el diseño, a ojo).

## Review Focus

- Una línea movida por la mudanza que también cambió (un include que arrastró una definición, un `using` que se perdió):
  `check_pure_move.py` la tiene que marcar aunque el archivo compile.
- Un corte de `entradas/cortes-vivos.tsv` cuya línea se movió **y** se editó después: el ancla tiene que seguir
  encontrándolo si las direcciones y el nombre no cambiaron, y decir "sin resolver" (no inventar un archivo) si
  desapareció.
- Un commit de mudanza sin `AR` en el título, o un squash que no se anotó en `.git-blame-ignore-revs`: el generador de
  la matriz tiene que avisar en vez de asignar las citas a la ola del commit anterior.
- Una escena del golden de comportamiento que dependa del reloj real o del orden de un `unordered_map`: el arnés tiene
  que dar el mismo hash dos veces seguidas en la misma máquina y en las dos del CI.
- Un include nuevo que cruce capas en un PR del port mientras el refactor está a medias: `check_layers.py` lo corta
  aunque la base ya tenga violaciones viejas (las de la línea de base no cuentan; las nuevas sí).

---

## Las épicas de un vistazo

| épica | qué deja | hitos (PRs) | líneas que mueve o toca (≈) |
| --- | --- | ---: | ---: |
| **AR0** Red de seguridad, reglas y rastreo | las reglas nuevas, `modulos.tsv`, el chequeo de capas, el rastreo a prueba de mudanzas, el golden de comportamiento | 3 | 3 mil (herramientas, tests y docs) |
| **AR1** El motor aparte | las dependencias al revés dadas vuelta, el kit común, `Client/src/engine/` con sus targets | 2 (1 mudanza + 1 diseño) | 30 mil movidas, ~3 mil tocadas |
| **AR2** Protocolo y despacho | layouts con offsets en el tipo, la tabla de opcodes, un handler por paquete; adiós `ServerSession` gigante, `WorldEvent` y `SessionEventApplier` | 3 | ~20 mil |
| **AR3** El estado por módulo | `globales.tsv`, `GameContext`, el núcleo compartido y las primeras partes de `WorldRuntime` en sus módulos | 2 | ~10 mil |
| **AR4** `Scenes/` en módulos | 5 grupos de módulos verticales, cada uno con su mudanza y su diseño | 10 (5 + 5) | ~210 mil movidas, ~40 mil tocadas |
| **AR5** Escenas como guiones | `GameScene`, el login y la selección como listas ordenadas; `main.cpp` a `app/` | 2 | ~8 mil |
| **AR6** Deduplicación y tests | las funciones portadas dos veces, los clones propios, los falsos de tests compartidos | 2 | ~15 mil |
| **AR7** Cierre | el chequeo de capas estricto, el tamaño de funciones en CI, los docs | 1 | ~1 mil |
| **total** | | **25** (6 mudanzas por script + 19 de diseño) | |

Cada épica, salvo AR0, se detalla en su propio plan al arrancar (como las olas), con el estado de `main` de ese día.
Este plan fija el qué, el orden, cómo se verifica y cuándo está terminada.

## Los dos carriles: el refactor y el port

El port (huecos, trazas, cortes, VM) no se frena. Las reglas:

1. **Lo nuevo va a su módulo de destino.** `mapa-modulos.tsv` dice el destino de cada archivo; un archivo nuevo de un
   PR del port suma su fila en el mismo PR. Si el módulo todavía no existe, el archivo va donde iría hoy pero ya con la
   anatomía nueva (un handler por paquete, el estado que toca en la firma), y la mudanza de su grupo lo lleva.
2. **Un módulo en mudanza no recibe port ese día.** La mudanza es un script: si `main` cambió, se vuelve a correr sobre
   el `main` nuevo en vez de resolver conflictos a mano. El diseño de un módulo y el port del mismo módulo no corren a
   la vez.
3. **Los agentes:** el refactor usa como mucho 2 de los 3 (uno escribe y uno revisa) y deja al menos 1 al port.
4. **Lo que falta portar sigue a la vista:** los cortes no cambian de texto; la referencia suma la columna `modulo` y
   `consultar.py modulo <Módulo>`; desde AR2, cada `case` del dispatcher sin portar es una fila `SinPortar{va}` de la
   tabla de opcodes. Antes de cada épica se toma la foto de `metricas.tsv`, y al terminarla se compara: los números del
   port tienen que ser los mismos (salvo lo que el port cerró en el medio).

## Las skills de cada paso

Cada paso del refactor tiene una skill que ya resuelve su procedimiento, y el plan las nombra para que quien ejecute no
lo reinvente (Emanuel, 2026-11-21). Las de `superpowers:` y `engineering:` y las incluidas (`code-review`, `simplify`,
`security-review`) son del entorno de Claude Code; `cpp-pro` y las tres nuevas viven en `.claude/skills/` del repo, y
`AGENTS.md` las apunta para que Codex las lea como docs.

**Las que ya hay:**

| paso | skill | para qué |
| --- | --- | --- |
| el plan detallado de cada épica, al arrancarla | `superpowers:writing-plans` | el formato de este plan (Global Constraints, Review Focus, tareas con sus tests) sobre el `main` de ese día |
| una decisión abierta: los ejecutables de test (AR1.2), `Hero` con `Player` (AR4.2), `Joints` con `Effects` (AR4.3) | `engineering:architecture` | un ADR con las opciones y lo que cuesta cada una; Emanuel elige y queda en `docs/adr/`, como el de AR0 |
| ejecutar un hito | `superpowers:subagent-driven-development`, o `superpowers:executing-plans` si corre en una sola sesión | un agente escribe y otro revisa cada tarea: entra justo en los 2 agentes del refactor (regla 3 de los carriles) |
| aislar el hito del port | `superpowers:using-git-worktrees` | un worktree por hito, así el port sigue sobre `main` sin pisarse (regla 2) |
| las herramientas de AR0 y cada PR de diseño | `superpowers:test-driven-development` | el test primero: en las herramientas, los de cada Task; en el diseño, la escena del golden de comportamiento o un test de caracterización antes de tocar la lógica |
| el golden de comportamiento (Task 7), los ejecutables de test (AR1.2), los falsos compartidos (AR6) | `engineering:testing-strategy` | qué escenas cubren qué módulos y cómo se agrupan los tests |
| C++, CMake y GoogleTest en todo PR de diseño | `cpp-pro` (del repo) | los targets con `FILE_SET HEADERS` (AR1.2), `Field<T, Offset>` y `Route<&Fn>` con concepts (AR2), el ownership del `GameContext` (AR3), la inyección de `MakeObfuscation()` (AR1.1); va por debajo de `CODING_RULES.md` |
| un golden o un test que cambió después de un PR del refactor | `superpowers:systematic-debugging` | la diferencia es un bug del refactor y no un golden para regenerar (Global Constraints): se busca la causa, no se cambia el esperado |
| antes de pedir la revisión | `simplify` | lo repetido y lo que sobra en el diff del diseño; en AR6 es la herramienta principal |
| la revisión de un PR de diseño | `code-review` con esfuerzo `high`, y `superpowers:requesting-code-review` / `superpowers:receiving-code-review` | el revisor busca bugs de corrección y quien escribe verifica cada pedido antes de aplicarlo; el nivel `max` solo si Emanuel lo pide |
| los layouts y el ruteo de los paquetes (AR2.1, AR2.2) | `security-review` | los layouts leen bytes que manda el servidor: un `Field<T, Offset>` más allá del largo del paquete es una lectura fuera de rango |
| cerrar un hito | `superpowers:verification-before-completion` y `superpowers:finishing-a-development-branch` | `check_layers.py`, `check_pure_move.py`, la foto de `metricas.tsv` igual a la de antes y el CI en verde en las cuatro configuraciones, con la salida a la vista; después, el squash y su línea en `.git-blame-ignore-revs` |
| el diseño de un hito mientras se genera la mudanza del siguiente (AR4) | `superpowers:dispatching-parallel-agents` | solo ahí: con el agente del port son 3, el máximo |
| la lista de lo repetido (AR6) | `engineering:tech-debt` | ordenar los clones por lo que cuestan y lo que arriesgan antes de factorizarlos |

`superpowers:brainstorming` no hace falta mientras la spec cubra la épica; vuelve si un diseño abre una pregunta que la
spec no contesta. `tutor` y el subagente `muret-reviewer` siguen afuera mientras el modo tutor esté apagado.

**Las tres nuevas, del repo.** Son los procedimientos del refactor que se repiten muchas veces y que también usa el
port. Van en `.claude/skills/<nombre>/SKILL.md`, en español y con el formato de `cpp-pro`: la `description` dice cuándo
se usa, y el cuerpo es la lista de pasos y apunta a los docs (CODING_RULES §5, el ADR, la spec) en vez de copiarlos.
Cada una se escribe en la tarea donde nace lo que describe, para no describir un script que todavía no existe, y suma
su línea en la tabla "Dónde está cada cosa" de `AGENTS.md`.

| skill | nace en | qué hace |
| --- | --- | --- |
| `muret-modulo` | AR0.1 (Task 2) | dónde va un archivo (`mapa-modulos.tsv`), la anatomía de un módulo (`Módulo.h`, `MóduloState.h`, `In/`, `Out/`, `<Módulo>Fwd.h`, `<Módulo>Defines.h`), cómo se crea un target con su capa en `modulos.tsv`, el contrato abajo y la implementación arriba, y qué hacer cuando `check_layers.py` corta un include. La usan los dos carriles (regla 1) |
| `muret-mudanza` | AR0.2 (Task 6) | una mudanza de punta a punta: `main` al día, `mover.py` con los módulos del hito, `check_pure_move.py`, las rutas escritas a mano que lista, el título `AR<épica>.<hito>`, nada de lógica y, después del squash, `mover.py --anotar`. Se usa en las 6 mudanzas |
| `muret-paquete` | AR2.2 | portar o tocar un paquete: el layout con `Field<T, Offset>` y su `static_assert` de tamaño, la fila de la tabla de opcodes que reemplaza a `SinPortar{va}`, el handler en el `In/` de su módulo, el golden del cable y la cita del `case` original |

---

## Épica AR0: red de seguridad, reglas y rastreo (detallada)

Tres PRs: **AR0.1** (Tasks 1 y 2), **AR0.2** (Tasks 3 a 6) y **AR0.3** (Task 7). AR0.3 puede ir en paralelo con
AR0.2.

### Task 1: el registro de módulos y el chequeo de capas

**Files:**
- Create: `Client/modulos.tsv`
- Create: `Client/tools/arquitectura/check_layers.py`
- Create: `Client/tools/arquitectura/test_check_layers.py`
- Create: `Client/tools/arquitectura/linea-de-base-capas.tsv`
- Modify: `.github/workflows/ci.yml` (job `includes`: correr el chequeo y sus tests)

**Interfaces:**
- Produces: `modulos.tsv` con columnas `modulo` (`engine/Core`, `game/Guild`…), `raiz` (`engine`, `game`, `app`),
  `capa` (entero; más bajo, más abajo), `carpetas_hoy` (globs de `Client/src` que le pertenecen mientras dure la
  migración, separados por `;`), `puede_usar` (módulos o `*capa<N` separados por `;`) y `nota`.
- Produces: `check_layers.py --src Client/src --modulos Client/modulos.tsv --mapa <mapa-modulos.tsv> --base
  <linea-de-base-capas.tsv> [--escribir-base]`. Funciones: `include_graph(src: Path) -> dict[str, set[str]]`
  (archivo → headers del proyecto que incluye, rutas relativas a `src`), `module_of(path: str, mapa: dict) -> str`,
  `violations(graph, mapa, modulos) -> list[tuple[str, str, str, str]]` (archivo, include, módulo de, módulo a) y
  `cycles(graph, mapa) -> list[list[str]]` (componentes fuertemente conexas de más de un módulo). Sale con 1 si hay una
  violación que no está en la línea de base.

- [ ] **Step 1: los tests**, con un árbol de `src` inventado en un `tempfile.TemporaryDirectory`:
  - `test_engine_including_game_is_a_violation`: `engine/Core/A.h` incluye `Scenes/B.h` (módulo `game/Scenes`) → una
    violación `("Core/A.h", "Scenes/B.h", "engine/Core", "game/Scenes")`.
  - `test_allowed_dependency_is_not_reported`: `game/Guild` con `puede_usar` = `*capa<9` incluye `engine/Core` → nada.
  - `test_baseline_hides_old_violations_only`: la misma violación en la línea de base no hace fallar; una nueva sí
    (código de salida 1 y la lista con solo la nueva).
  - `test_cycle_between_two_modules`: `game/A` ↔ `game/B` → `cycles()` devuelve `[["game/A", "game/B"]]`.
  - `test_unmapped_file_fails`: un `.cpp` que no está en el mapa ni en `carpetas_hoy` → sale 1 y lo nombra.
- [ ] **Step 2:** `python3 -I -m unittest Client/tools/arquitectura/test_check_layers.py -v` → FAIL (no existe el
  módulo).
- [ ] **Step 3:** escribir `check_layers.py`. El grafo es el mismo que el de la medición del 2026-11-20: solo
  `#include "…"` que resuelven a un archivo de `src`; los `<…>` no cuentan. El módulo de un archivo sale primero del
  mapa (su fila por la ruta de hoy) y, si no tiene fila (un archivo nuevo), del glob más largo de `carpetas_hoy`.
- [ ] **Step 4:** los tests pasan.
- [ ] **Step 5:** `modulos.tsv` con los 58 módulos de destino de la spec (14 del motor, 43 del juego y `app`; los
  submódulos de `Events/` y `MapScripts/` son carpetas de su módulo) y, mientras dure la migración, `carpetas_hoy`
  apuntando a lo de hoy (`Core/**` → `engine/Core`; `Scenes/**` → según el mapa). Capas: `engine/Core` 0;
  `engine/Math`, `Crt` y `Crypto` 1; `engine/Diagnostics`, `Input`, `Audio`, `Network` y `Data` 2;
  `engine/Platform`, `Pathfinding` y `Obfuscation` 3; `engine/Render` 4; `engine/Models` 5; `game/Protocol`, `Tables` y `Maps` 6;
  `game/Kernel` 7; `game/Assets`, `Terrain`, `Sound`, `Camera` y `Ui` 8; los módulos del juego 9; `game/Session` 10;
  `game/Scenes` 11; `app` 12. Entre módulos de la misma capa, `puede_usar` lista los que hoy se usan en un solo
  sentido según el grafo medido con el mapa (`Effects` → `Particles`, `Movement` → `engine/Pathfinding`…).
- [ ] **Step 6:** `python3 -I Client/tools/arquitectura/check_layers.py … --escribir-base` sobre `main` escribe
  `linea-de-base-capas.tsv` y, sin `--escribir-base`, sale con 0. Con el mapa del anexo (revisión del 2026-11-21),
  la base de hoy tiene 365 includes que suben de capa (83 pares de módulos, 210 archivos), 11 de ellos desde el
  motor, y 80 pares mutuos entre módulos del juego de la misma capa; si da muy distinto, el chequeo o el mapa están
  mal.
- [ ] **Step 7:** en el job `includes` de `ci.yml`, un paso "Capas del cliente" que corre los tests y el chequeo
  (sin build: el chequeo lee los fuentes).
- [ ] **Step 8:** commit `AR0.1: el registro de módulos y el chequeo de capas`.

### Task 2: las reglas, el mapa y dónde va un port nuevo

**Files:**
- Modify: `docs/CODING_RULES.md` (§5 Capas separadas, §6 Una clase por archivo; una sección nueva sobre direcciones y
  offsets)
- Create: `docs/adr/NNNN-arquitectura-por-modulos-del-cliente.md` (el número siguiente libre al abrir el PR)
- Modify: `docs/adr/README.md` (la fila del ADR)
- Modify: `AGENTS.md` (tabla "Dónde está cada cosa" y una sección "Dónde va un port nuevo")
- Create: `.claude/skills/muret-modulo/SKILL.md` ("Las skills de cada paso")
- Create: `Client/docs/research/coverage/mapa-modulos.tsv` (la semilla del anexo, revisada)
- Modify: `docs/superpowers/specs/2026-10-30-cliente-design.md` (la sección "Estructura de `Client/`" apunta a la spec
  nueva)

**Interfaces:**
- Consumes: `modulos.tsv` y `check_layers.py` (Task 1).
- Produces: `mapa-modulos.tsv` con las columnas del anexo (`archivo`, `lineas`, `carpeta_actual`, `destino`,
  `confianza`, `por_que`), que leen `check_layers.py`, `mover.py` (Task 6) y la referencia (Task 5).

- [ ] **Step 1:** revisar a mano las 50 filas de confianza `baja` del mapa (los archivos de `Scenes/` que el grafo de
  includes tira hacia otro módulo) y corregir su `destino`; anotar en `por_que` la razón.
- [ ] **Step 2:** CODING_RULES §5 con la tabla de capas del cliente (las de la spec, con "hace / no hace"), la
  anatomía de un módulo (`Módulo.h` público, `MóduloState.h`, `In/`, `Out/`), los tamaños de referencia (handler ~80
  líneas, archivo ~400, función ~60; la excepción de las transcripciones de `switch` del original, marcadas) y la regla
  de interfaces (solo en los bordes del motor y donde un test la necesita). La sección nueva de direcciones: las citas
  en comentario se quedan; `Field<T, Offset, Endian>` para paquetes; `static_assert(offsetof/sizeof)` para los
  formatos de `Data\`; `// Contiguo en el original:` para los structs que no se parten.
- [ ] **Step 3:** el ADR con Contexto (las mediciones de la spec), Decisión (las tres raíces, las capas, la tabla de
  opcodes, el estado por módulo, lo que se toma y lo que no de OpenMU, TrinityCore y MuMain), Consecuencias (cómo se
  verifica: `check_layers.py`, el golden de comportamiento) y Estado (propuesto; lo acepta Emanuel).
- [ ] **Step 4:** AGENTS.md: "Dónde va un port nuevo": mirar `mapa-modulos.tsv`; si el módulo no existe todavía, el
  lugar de hoy con la anatomía nueva y su fila en el mapa; un paquete nuevo = layout en `Protocol/<Módulo>/` + handler
  en `In/` + fila en la tabla (desde AR2).
- [ ] **Step 5:** la skill `muret-modulo`: los pasos de Step 4 y la anatomía de Step 2, apuntando a CODING_RULES §5 y
  al ADR; su línea en `AGENTS.md`.
- [ ] **Step 6:** `check_layers.py` sigue en 0 y los docs pasan el job de formato. Commit `AR0.1: las reglas de la
  arquitectura por módulos y el mapa de archivos`.

### Task 3: el ancla estable de las clasificaciones a mano

**Files:**
- Modify: `Client/docs/research/scripts/tools/build_client_reference.py` (la lectura de `entradas/` y los `V_REACH`)
- Create: `Client/docs/research/scripts/tools/reference_anchors.py`
- Create: `Client/docs/research/scripts/tools/test_reference_anchors.py`
- Modify: `Client/docs/research/coverage/referencia/entradas/cortes-vivos.tsv`, `sin-cablear.tsv`,
  `sustitutos.tsv`, `chat-funciones.tsv` (columna `ancla` nueva; las demás quedan)
- Modify: `Client/docs/research/coverage/referencia/LEEME.md` (cómo se reconoce una fila a mano)

**Interfaces:**
- Produces: `anchor_of(line_text: str, vas: list[int], name: str, symbol: str) -> str`: las direcciones ordenadas, el
  nombre del corte (`Unexpected`, `cut`…), el símbolo y un hash corto (`sha1`, 12 hex) del texto de la línea
  normalizado (sin espacios repetidos, sin el comentario final, sin números de línea), unidos por `|`.
- Produces: `resolve(anchor: str, current_cuts: list[dict]) -> dict | None`: el corte actual con la misma ancla; si no
  hay, el que tenga las mismas direcciones, nombre y símbolo y un texto con distancia mínima (se marca
  `resuelto_por` = `parecido`); si tampoco, `None`.

- [ ] **Step 1: los tests:**
  - `test_anchor_ignores_whitespace_and_trailing_comment`: dos líneas que difieren solo en espacios y en el
    comentario final dan la misma ancla.
  - `test_resolve_finds_moved_cut`: un corte con ancla `A` en `Scenes/X.cpp:10`; el corte actual con la misma ancla
    está en `game/Guild/X.cpp:12` → `resolve` lo devuelve con el archivo nuevo.
  - `test_resolve_edited_line_falls_back_to_similar`: mismo nombre, direcciones y símbolo, texto con una palabra
    distinta → lo devuelve con `resuelto_por` = `parecido`.
  - `test_resolve_missing_returns_none`: ninguna coincidencia → `None`, y el generador cuenta la fila en
    `metricas.tsv` como `entradas-sin-resolver`.
- [ ] **Step 2:** `python3 -I -m unittest Client/docs/research/scripts/tools/test_reference_anchors.py -v` → FAIL.
- [ ] **Step 3:** `reference_anchors.py` con las dos funciones; `build_client_reference.py` calcula el ancla de cada
  corte actual (de las líneas que ya lee con `git cat-file --batch`), lee `ancla` de cada entrada a mano, resuelve y
  escribe el `archivo` y la `línea` de hoy en las tablas de salida. Los cuatro `V_REACH` pasan a ser filas de
  `cortes-vivos.tsv` (columna `cierra_el_cliente`), no un diccionario en el código.
- [ ] **Step 4:** una corrida de migración (`--anclar-entradas`) escribe la columna `ancla` en las cuatro entradas
  desde los `archivo:línea` de hoy. Antes y después, con la misma ref, `metricas.tsv` es idéntica.
- [ ] **Step 5:** tests en verde; commit `AR0.2: las entradas a mano se reconocen por un ancla estable`.

### Task 4: las mudanzas no le cambian la ola a nadie

**Files:**
- Modify: `Client/docs/research/scripts/tools/build_roadmap_matrix.py` (`_commit_waves`, `_blame`)
- Create: `.git-blame-ignore-revs` (vacío, con su comentario)
- Create: `Client/docs/research/coverage/referencia/entradas/mudanzas.tsv` (encabezado: `commit`, `desde`, `hasta`)
- Create: `Client/docs/research/scripts/tools/test_commit_waves.py`

**Interfaces:**
- Produces: en `_commit_waves`, un commit cuyo asunto empieza con `AR\d` toma la ola `AR` (no hereda la anterior);
  `_blame` corre `git blame --line-porcelain -M -C --ignore-revs-file .git-blame-ignore-revs` cuando el archivo existe
  en la ref, y una cita cuyo commit tiene ola `AR` suma un aviso `cita-atribuida-a-AR` en el resumen (quiere decir que
  falta anotar un squash).

- [ ] **Step 1: los tests**, con un repo de git inventado en una carpeta temporal:
  - `test_ar_commit_does_not_inherit_wave`: commits `CL9 hito 2: …`, `AR1.2: mudanza …` y `arreglo`, en ese orden →
    olas `CL9`, `AR` y `CL9`: el tercero hereda la del último commit del port, no la del refactor.
  - `test_blame_ignores_listed_move`: un archivo con una cita de un commit `CL7 …`, movido de carpeta en un commit
    `AR1.2` anotado en `.git-blame-ignore-revs` → la cita sigue con ola `CL7`.
  - `test_unlisted_move_warns`: el mismo movimiento sin anotar → aviso `cita-atribuida-a-AR`.
- [ ] **Step 2:** los tests fallan; Step 3: el cambio en `build_roadmap_matrix.py`; Step 4: pasan.
- [ ] **Step 5:** en `coverage/referencia/LEEME.md`, la receta de cada mudanza: después del squash, sumar su hash a
  `.git-blame-ignore-revs` y las rutas a `mudanzas.tsv` (lo hace `mover.py --anotar <hash>`, Task 6).
- [ ] **Step 6:** commit `AR0.2: el blame de la matriz sigue las mudanzas`.

### Task 5: lo que falta, por módulo

**Files:**
- Modify: `Client/docs/research/scripts/tools/build_client_reference.py` (columna `modulo` y métricas por módulo)
- Modify: `Client/docs/research/coverage/referencia/consultar.py` (pregunta `modulo`)
- Modify: `Client/docs/research/scripts/tools/reference_summary.py` (tabla por módulo en el resumen de la action)
- Modify: `Client/docs/research/scripts/tools/test_reference_summary.py`
- Modify: `Client/docs/research/coverage/referencia/LEEME.md`

**Interfaces:**
- Consumes: `mapa-modulos.tsv` (Task 2); `resolve()` (Task 3).
- Produces: columna `modulo` en `funciones.tsv` (el módulo del archivo que más la cita; para una sin citar, el de sus
  llamadores citados), `cortes.tsv`, `huecos.tsv` y `archivos.tsv`; ámbito `modulo` en `metricas.tsv` con las métricas
  `lineas`, `funciones_citadas`, `kb_citados`, `cortes_vivos`, `cierran`, `huecos`, `kb_huecos`, `pasos_vm` y
  `ids_08_abiertas`.
- Produces: `python3 -I consultar.py modulo game/Guild` imprime esas métricas y las listas (cortes vivos con
  `archivo:línea`, huecos con sus llamadores, pasos de VM y filas de 08).

- [ ] **Step 1: los tests** en `test_reference_summary.py`: `test_module_table_counts_cuts_and_gaps` (dos módulos
  inventados con cortes y huecos dan sus totales) y `test_gap_takes_module_of_its_callers` (un hueco cuyos dos
  llamadores citados están en `game/Items` toma `game/Items`; con llamadores en dos módulos, el de más llamadores, y
  empate → el primero en orden alfabético).
- [ ] **Step 2:** fallan; Step 3: el código; Step 4: pasan (el paso de `ci.yml` que ya corre
  `test_reference_summary.py` los toma).
- [ ] **Step 5:** regenerar la referencia sobre `main` y commitearla: la suma por módulo de cada métrica es igual al
  total de hoy. Commit `AR0.2: la referencia cuenta lo que falta por módulo`.

### Task 6: el script de mudanza y su verificación

**Files:**
- Create: `Client/tools/arquitectura/mover.py`
- Create: `Client/tools/arquitectura/check_pure_move.py`
- Create: `Client/tools/arquitectura/test_mover.py`, `Client/tools/arquitectura/test_check_pure_move.py`
- Modify: `.github/workflows/ci.yml` (en PRs con `AR` en el título, correr `check_pure_move.py` contra la base)
- Create: `.claude/skills/muret-mudanza/SKILL.md` ("Las skills de cada paso")

**Interfaces:**
- Produces: `mover.py --mapa <mapa-modulos.tsv> --modulos game/Guild[,…] [--raiz engine]` hace `git mv` de cada
  archivo cuyo `destino` empieza con alguno de los módulos pedidos, a `Client/src/<destino>/<nombre>`, y actualiza los
  `#include "…"` **solo** si la ruta relativa a su raíz cambió (dentro de una misma raíz, `Scenes/X.h` →
  `Guild/X.h`); escribe las filas de `mudanzas.tsv` y pone la ruta nueva en la columna `archivo` del mapa.
  `mover.py --anotar <hash>` suma el squash a `.git-blame-ignore-revs`.
- Produces: `mover.py` lista además las rutas escritas a mano que apuntan a un archivo movido, en `CMakeLists.txt`,
  `cmake/*.cmake` y `.github/workflows/*.yml` (por ejemplo, el job `math-release` mira
  `Client/src/Render/Models/OriginalRetainedMath.cpp` y `Client/CMakeLists.txt` nombra `JpegImage.cpp` y
  `JpegWriter.cpp`), y las reescribe.
- Produces: `check_pure_move.py --base <ref> --head <ref>`: para cada archivo renombrado (`git diff -M50% --name-status`)
  compara el multiconjunto de líneas sin las de `#include` ni las vacías; sale con 1 y lista las diferencias si alguna
  línea cambió. Los `CMakeLists.txt`, `*.cmake`, `*.md` y `*.tsv` se permiten.

- [ ] **Step 1: los tests:**
  - `test_pure_move_passes`: un archivo movido con un include reescrito → 0.
  - `test_moved_and_edited_fails`: movido y con una línea de lógica cambiada → 1 y la línea.
  - `test_dropped_line_fails`: movido sin una línea (un `using` que desapareció) → 1.
  - `test_mover_rewrites_includes_across_folders`: `Scenes/GuildState.h` a `game/Guild/GuildState.h`; un
    `#include "Scenes/GuildState.h"` en otro archivo pasa a `#include "Guild/GuildState.h"`.
  - `test_mover_keeps_includes_when_only_root_changes`: `Core/Expected.h` a `engine/Core/Expected.h` → ningún include
    cambia.
- [ ] **Step 2:** fallan; Step 3: los dos scripts; Step 4: pasan.
- [ ] **Step 5:** la skill `muret-mudanza` con los comandos de verdad de los dos scripts, y su línea en `AGENTS.md`.
- [ ] **Step 6:** commit `AR0.2: el script de mudanza y su verificación`.

### Task 7: el golden de comportamiento

**Files:**
- Create: `Client/tests/characterization/CMakeLists.txt` (se suma a `muret_add_module_tests`)
- Create: `Client/tests/characterization/test_characterization.cpp`
- Create: `Client/tests/support/FrameDigest.h`, `Client/tests/support/FrameDigest.cpp`
- Create: `Client/tests/characterization/escenas/*.tsv` (el guion de cada escena: frame, paquete que llega en hex o
  entrada)
- Create: `Client/tests/characterization/golden/*.tsv` (frame → hash)

**Interfaces:**
- Consumes: `support/RecordingFrameTarget.h` (`frames`, `RecordedFrame::events`), `support/FakeClock.h`,
  `support/FakeServers.h` o `support/SessionHarness.h`, `support/Fnv1a.h`, `Render::Models::CrtRandom::State()`.
- Produces: `struct FrameDigest { std::uint64_t draw; std::uint64_t sent; std::uint32_t randState; std::uint64_t state; };`
  y `FrameDigest DigestOf(const RecordedFrame& frame, std::span<const std::byte> sentThisFrame, std::uint32_t randState,
  const Scenes::WorldState& world)`. `state` es el FNV-1a de un resumen fijo y chico: posición y acción de los actores
  vivos, vida, maná y zen propios, el mapa, los paneles abiertos y las líneas del chat. No hashea punteros ni
  direcciones de memoria.

- [ ] **Step 1: el arnés** con una escena: login → lista de servidores → lista de personajes → entrada a Lorencia y
  100 frames quietos (`CharacterizationTest.LoginToLorencia`). Cada frame: los paquetes del guion de ese frame, la
  entrada del guion, un paso del juego con `FakeClock` a 40 ms y el dibujo en `RecordingFrameTarget`; se agrega el
  `FrameDigest` a la lista.
- [ ] **Step 2:** `CharacterizationTest.IsDeterministic`: la misma escena dos veces en el mismo proceso da la misma
  lista. Si falla, se arregla la fuente de no determinismo (reloj real, orden de un contenedor sin orden), no el test.
- [ ] **Step 3:** el golden: con `MURET_CHARACTERIZATION_WRITE=1` el test escribe `golden/<escena>.tsv`; sin ella,
  compara frame por frame y el error dice el primer frame distinto y cuál de los cuatro campos cambió.
- [ ] **Step 4:** tres escenas más: `WalkAndFight` (caminar, atacar a un monstruo con una skill, que muera, levantar
  el drop), `InventoryShopAndChat` (abrir el inventario, mover un ítem, comprar en una tienda, mandar un susurro) y
  `EventEntry` (la entrada a Devil Square o Blood Castle con el aviso del evento). Los paquetes salen de
  `support/WirePackets.h` y de las capturas que ya usan los tests de cada tema.
- [ ] **Step 5:** el CI de Linux y el de Windows dan el mismo golden (la matemática de MuRet no depende de la CPU;
  `client-test-machines-cpu`). Si Windows difiere, el campo que difiere dice dónde.
- [ ] **Step 6:** commit `AR0.3: el golden de comportamiento de cuatro escenas`.

**AR0 está terminada cuando:** el chequeo de capas corre en CI con la línea de base de hoy; la referencia regenerada
sobre `main` da los mismos números que la commiteada y además los da por módulo; una mudanza de prueba (en una rama
que no se mergea: `mover.py --modulos game/Guild`) pasa `check_pure_move.py`, compila en el CI y deja la referencia y el
golden de comportamiento iguales.

---

## Épica AR1: el motor aparte

### AR1.1 (diseño): dar vuelta las dependencias y el kit común

- **Al motor:** `App/WindowState`, `VSyncMode` y `Resolution` a `Platform/` (`PixelSize` a `Core/`); `CrtRandom`
  de `Render/Models/Cloth.h` a su propio `CrtRandom.h` (219 archivos incluyen `Cloth.h`, casi todos por él);
  `World/Ftol.h` y `World/OriginalFtol.h` unidos en un `Ftol.h`; los dos, con la matemática x87 de `Render/Models`
  (`OriginalRotation`, `OriginalAngles`, `OriginalRetainedMath`, `HalfLifeMath`), van a `engine/Math` en AR1.2;
  `ITextDrawer`, `SceneCanvas`, `SceneText`, `IFrameTarget`, `IWorldView` y el contrato de `Screenshots` a
  `Render/Frame/`; `IModelLibrary` de `Scenes/CharacterDrawing.h` a `Render/Models/IModelLibrary.h` (va a
  `engine/Models` en AR1.2); el include de `CharacterDrawing.h` en `Render/ModelBatchParticleBackend.cpp` se invierte
  con una interfaz chica en `Render`; `UI/IClipboard` a `Platform/`.
- **Los módulos nuevos del motor** (revisión del 2026-11-21): `CrtRandom` sale de `Cloth.h` a su propio header, que
  va a `engine/Crt` con `Msvc6Sort` y los dos formatos de `sprintf`; `Xor32` recibe un `std::span<std::byte>` en vez
  de un `Network::Packet`, así `engine/Crypto` (SimpleModulus, Xor32, Xor3, K16) no ve la red; `IPlayback` gana un
  `Register(SoundDefinition)` y `SdlAudio` deja de incluir el catálogo de sonidos, que el juego le pasa al arrancar
  (el catálogo va a `game/Sound`). El orden de las llamadas al motor de audio no cambia: el golden lo compara.
  `FoxAntiObfuscation` sale de `Network` a `engine/Obfuscation`, una capa que se saca al compilar y que el resto del
  cliente recibe inyectada (Emanuel, 2026-11-21: la decide quien programa, no quien juega). `Network` se queda con
  `IObfuscation`, `ObfuscationError` y la identidad; `Connection`, `PacketReader`, `EncryptPacket` y `ServerSession`
  ya reciben la capa como `IObfuscation` sin saber cuál es, y eso no cambia.
  - **Un solo interruptor, al compilar**: la opción de CMake `MURET_FOXANTI`, prendida por defecto mientras el
    laboratorio la use. Apagada, `engine/Obfuscation` no se agrega (`if(MURET_FOXANTI) add_subdirectory(...)`),
    `app/` no la enlaza y el binario no lleva nada de FoxAnti.
  - **La inyección, sin `#if` en el código**: la raíz de composición de `app/` pide la capa a
    `std::unique_ptr<const Network::IObfuscation> MakeObfuscation()`, declarada en `app/MakeObfuscation.h`, y la
    guarda junto a la conexión (hoy `Online` en `main.cpp`). CMake agrega a `MuRetApp` una de dos definiciones:
    `app/MakeObfuscationFoxAnti.cpp`, que arma una `FoxAntiObfuscation` y enlaza `MuRetObfuscation`, o
    `app/MakeObfuscationNone.cpp`, que arma una `IdentityObfuscation`. Ningún otro archivo sabe que la capa existe:
    sacarla del repo es borrar `engine/Obfuscation`, `MakeObfuscationFoxAnti.cpp` y el `if` del CMake.
  - **La clave `[Server] Obfuscation` de `muret-client.ini` se va** (existe desde la CL4, pregunta 8 de su plan).
    Salen `App/ObfuscationMode.h`, el campo `obfuscation` de `ClientConfig`, `ParseObfuscation`, `ChooseObfuscation`
    de `main.cpp` y sus casos de `test_config`; `Online` recibe la capa en vez del modo. Un .ini que todavía la
    tenga da el error de clave que no existe, con su línea y un texto que apunta a `MURET_FOXANTI`. `Client/README.md`
    deja de nombrarla y dice cómo compilar sin la capa.
  - **Los tests**: los que necesitan FoxAnti (los 2 de FoxAnti y los 10 que arman la conexión con ella, entre ellos
    los goldens del cable del laboratorio) van en una lista aparte del CMake de los tests, que solo se compila con la
    opción prendida; si un archivo la usa solo en algunos casos, esos casos pasan a un archivo propio. El resto
    inyecta la identidad o un doble.
  - **El CI**: el job de Windows Release, que ya compila solo `Main` y los tests de la matemática, configura con
    `MURET_FOXANTI=OFF`. Así el cliente sin la capa compila y enlaza en cada PR sin un job nuevo.
- **El kit común** (se crea acá para que las mudanzas no arrastren copias): `X87.h` junto a `Ftol.h` (`F32`, `D`,
  `Truncate`, `Bits`: las 115 copias de `F32` se reemplazan solo donde el cuerpo es idéntico, verificado por script),
  `Core/WrappingInt.h`, `Core::Expected` con `AndThen`, `Transform` y `OrElse` (los nombres de `std::expected`, para
  pasar a él cuando el repo use C++23), `Data::Text::TextOr`, `UI::Rect::Contains`, y en `game/Maps/MapId.h` el
  enum de mapas con sus predicados (`IsChaosCastle`, `IsKalima`…; hoy 25 `FirstChaosCastleMap` y 8 `InChaosCastle`,
  y unas 189 comparaciones por mapa con constantes sueltas en 62 archivos, que pasan a los nombres sin moverse).
- **Verificación:** el golden de comportamiento idéntico; `check_layers.py` baja las violaciones del motor a 0 y se
  regenera la línea de base.

### AR1.2 (mudanza + targets): `Client/src/engine/`

- `mover.py --raiz engine` lleva `Core`, `Platform`, `Input`, `Audio`, `Network`, `Data` y `Render` a
  `Client/src/engine/`, `Render/Models` a `engine/Models`, la matemática x87 a `engine/Math`, y cada archivo que el
  mapa manda a un módulo nuevo del motor (`Crt`, `Crypto`, `Diagnostics`, `Pathfinding`, `Obfuscation`, con lo de `UI/`, `World/` y
  `GameLogic/` que les toca). Lo que hoy está en `Data/` o `Audio/` pero es del juego va directo a su módulo: las
  tablas a `game/Tables` (1.772 líneas), las carpetas de los mapas a `game/Maps` y el catálogo de sonidos a
  `game/Sound`. Los includes de las carpetas que solo cambian de raíz no cambian de texto; los demás los reescribe el
  script.
- **CMake:** un target por carpeta del motor (`MuRetCore`, `MuRetMath`, `MuRetCrt`, `MuRetCrypto`, `MuRetDiagnostics`,
  `MuRetInput`, `MuRetAudio`, `MuRetNetwork`, `MuRetData`, `MuRetPlatform`, `MuRetPathfinding`, `MuRetObfuscation`, `MuRetRender`,
  `MuRetModels`) y los tres primeros del juego (`MuRetTables`, `MuRetMaps`, `MuRetSound`) con raíz de include
  `Client/src/engine` (o `Client/src/game`), sus headers en
  `FILE_SET HEADERS` y `VERIFY_INTERFACE_HEADER_SETS ON`; `ClientLib` queda con lo demás y los enlaza.
  `muret_set_fidelity_flags(target)` (en `cmake/`) junta `-ffp-contract=off` y lo que pida MSVC, y se aplica a todos
  los targets. Los tests de `tests/{core,platform,input,audio,network,data,render,render_gl}` enlazan solo su target.
- **Verificación:** `check_pure_move.py` en 0; CI verde; el golden idéntico; la referencia igual salvo rutas;
  `.git-blame-ignore-revs` y `mudanzas.tsv` anotados.
- **Decisión pendiente para Emanuel (se pregunta al abrir AR1.2):** agrupar los tests en un ejecutable por módulo
  (15 a 40 en vez de 728: ≈90 % menos enlaces) revierte la decisión del 2026-11-02 de un ejecutable por archivo, como
  MuMain. Sin respuesta, se queda como está.

**AR1 está terminada cuando** el motor compila sin ver `game/` y `app/`, y `check_layers.py` no tiene ninguna
violación de la raíz `engine`.

## Épica AR2: protocolo y despacho

### AR2.1 (diseño): los layouts de los paquetes

- `Protocol/Field.h`: `Field<T, Offset, Endian>` y un `PacketLayout` con tipo (C1 a C4), opcode, subcódigo, largo
  mínimo y la dirección del lector original; un lector que acumula el primer error (sin los 83 `if (!fields.HasValue())`
  ni los 172 `TooShort` a mano). `Core::ByteReader` queda para las listas de largo variable.
- Las cuatro maneras de leer pasan a una: se borran las 11 copias de `FieldsOf` y las dos clases `Fields` privadas de
  `Protocol/Events.cpp` y `Protocol/CastleSiege.cpp`; las tres lecturas de lista de `CastleSiege.cpp` pasan a un
  `ReadList<Record, Size>`.
- `Protocol/` se ordena por módulo (`Protocol/Guild/`, `Protocol/Items/`…), como `WorldPackets::<Función>` en
  TrinityCore.
- **Sin generador de código** por ahora (OpenMU genera desde XML): los descriptores en C++ ya dejan los offsets en un
  solo lugar. Se revisa al cerrar AR2 si la repetición que queda lo justifica (CODING_RULES §10).

### AR2.2 (diseño): la tabla de opcodes y el ruteo

- `game/Session/GameServerOpcodes.cpp` y `ConnectServerOpcodes.cpp`: tablas `constexpr` con una fila por `case` del
  dispatcher original (`0x4e2b10` y sus sub-dispatchers): tipo, opcode, subcódigo, dirección, handler, si el original
  copia el paquete al anillo antes del handler y si espera cifrado. `static_assert` sin filas repetidas.
- `SinPortar{va}` para cada `case` sin handler; un test compara la tabla con la lista de `case` del research
  (04-protocolo y el switch de `0x4e2b10`), así que un paquete que falta es una fila que se ve.
- `Route<&Fn>` deduce de la firma del handler el layout y las partes del `GameContext` que hay que pasarle.
- `ServerSession` (2.878 líneas) se parte: `Session/Connection` (keepalive, serial, claves, el flujo del login y del
  cambio de servidor) y el ruteo por la tabla. Los primeros módulos que pasan a handlers por paquete son los del login,
  la selección, el movimiento y el viewport.
- La skill `muret-paquete` ("Las skills de cada paso"), con un paquete del login como ejemplo, y su línea en
  `AGENTS.md`.

### AR2.3 (diseño): todos los handlers en sus módulos

- El resto de los 125 registros pasa a handlers de 40 a 80 líneas en `game/<Módulo>/In/` (en `Scenes/` si el módulo
  todavía no se mudó, en su carpeta de destino del mapa), y las ~50 funciones de envío de la sesión pasan a acciones en
  `Out/`.
- Se borran `App/WorldEvent.h`, `App/CharacterEvent.h`, `ISessionEventSink`, `SessionEventApplier` y `/bigobj` de
  `ClientLib`.
- **Verificación de las dos:** el golden de comportamiento idéntico; `tests/app/test_server_session.cpp` (5.575
  líneas) se reparte en tests por handler con los mismos bytes de entrada y las mismas aserciones.

**AR2 está terminada cuando** agregar un paquete nuevo toca su layout, su handler y una fila de la tabla, y nada más.

## Épica AR3: el estado por módulo

### AR3.1 (diseño): el registro de globales y el `GameContext`

- La referencia genera `globales.tsv` desde las citas de datos de `trazas.tsv` (sección `datos`, desde `0x6b8000`):
  dirección → struct y campo de MuRet → módulo (del mapa). Un global citado en dos campos se marca: o es contiguo en
  el original o es una copia que sobra.
- `game/Kernel/GameContext.h`: la raíz de composición del estado (`ActorPool`, `PlayerState`, `MapState`, el scratch
  del frame, la fauna y, por ahora, `WorldRuntime` entero); `WorldState` queda como fachada mientras dure la migración.
  `GameContext::Hero()` reemplaza los 210 `Actors()[…hero]`.

### AR3.2 (diseño): las primeras partes de `WorldRuntime` a sus módulos

- Los 17 campos que usa un solo módulo, y los estados que ya son de un módulo (`GuildState`, `SocialState`,
  `NpcWindowState`, `QuestState`, `CrywolfGlobals` y un `CastleSiegeState` que se queda con los 9 campos sueltos del
  castillo) salen de `WorldRuntime` al `GameContext`, con su dueño anotado en `globales.tsv`.
- El núcleo compartido (`hero`, el pedido de sonido, `ownRecordDepth`, el mapa) queda en `Kernel/`; el arbitraje del
  mouse (`movement.pendingClick`, `ui.mouseOnUi`, `ui.field`) pasa a su propio estado.
- Lo contiguo en el original no se parte (`TooltipLines`, Q-C591; `CharacterBalloons`, Q-C1503; los bloques que vacía
  junto una función de reset).
- El resto de `WorldRuntime` se reparte dentro de cada hito de AR4, con su módulo.

**AR3 está terminada cuando** `globales.tsv` cubre todas las citas de datos y `WorldRuntime.h` ya no incluye headers
de `UI/` de los módulos que salieron.

## Épica AR4: `Scenes/` en módulos verticales

Cinco hitos, de abajo hacia arriba en el grafo. Cada hito es **una mudanza** (`mover.py --modulos …`, con lo que hoy
está en `Scenes/`, `UI/`, `World/` y `GameLogic/` de esos módulos, según el mapa) y **un diseño** (el namespace
`Client::<Módulo>`, el estado del módulo fuera de `WorldRuntime`, sus handlers y acciones, las clases chicas, la
repetición propia del módulo y los nudos de la spec que le tocan). Antes de cada hito se compara el mapa con
`consultar.py modulo` para no mudar un módulo con port abierto.

| hito | módulos | líneas (≈) | nudos que se desatan en el diseño |
| --- | --- | ---: | --- |
| **AR4.1** | `Kernel`, `Assets`, `Terrain`, `Camera`, `Sound` (lo que queda), `Player`, `Actors` (+ dibujo de personajes) | 34 mil | el paso del personaje (`0x508f00`) pasa a un registro de pasos por clase de actor; los aplicadores del viewport dejan de llamar a monstruos, chat y tiendas |
| **AR4.2** | `Monsters`, `Pets`, `Movement`, `Skills`, `Combat`, `Hero` | 33 mil | combate↔héroe; si `Hero` (788 líneas) se junta con `Player`; el clic de `GameMovement` deja de incluir a `Events` y `Chat` (punto de extensión ordenado); el cuervo de `WorldTransitionSupport` a `Pets`; `0x57ce80`, `0x4eac00` y `0x4ce000` portadas una vez |
| **AR4.3** | `Particles`, `Joints`, `Effects`, `WorldObjects`, `Fauna`, `Weather`, `MapScripts` (una carpeta por mapa) | 43 mil | los adaptadores de `0x53fe70`, `0x530860` y `0x518cf0` (cuatro cada uno) pasan a un `EffectSpawner`; `ParticleSpawn` reemplaza las firmas de 15 parámetros (109 llamadas); `Joints`↔`Effects` (13 y 6 includes) a un solo sentido o juntos; la tabla `constexpr` id de mapa → funciones de `MapScripts` |
| **AR4.4** | `Items`, `Tooltips`, `Npc`, `Quests`, `ChaosMix`, `Warehouse`, `PetTrainer`, `Party`, `Social`, `Messenger`, `PersonalShop`, `Chat`, `Guild` | 48 mil | `LetterPortrait` arma el actor por `Actors.h`, no por sus tripas; `InventoryServices` (10 callbacks, 32 archivos) pasa a puertos; `SocialState` se parte en comercio, party y duelo, salvo lo contiguo |
| **AR4.5** | `Ui` (el kit), `Hud` (+ diálogos), `Events` (un submódulo por evento), `CastleSiege`, `FrontEnd` | 48 mil | `PanelRegistry` con un `IPanel` por módulo y las reglas de exclusión como tabla; el HUD deja de dibujar a mano las ventanas de cada módulo; se borra `GamePanels`; `EventPackets.cpp` (1.740 líneas) se reparte entre los submódulos de cada evento |

**Cada hito está terminado cuando** sus módulos no tienen violaciones en `check_layers.py` (se sacan de la línea de
base), el golden de comportamiento es idéntico, la referencia da los mismos números del port y `scenes-ciclos.tsv`
regenerado no tiene pares de esos módulos.

## Épica AR5: las escenas como guiones del frame

- `GameScene` (3.430 líneas, nueve trabajos) queda como la lista ordenada de pasos y de dibujos del frame, cada uno con
  su dirección, que llama a los módulos; salen `GameFrameProjection`, `GameAssets` y los armados de contextos
  (`ChatContext`, `InventoryContext`, `ArrowContext`). Lo mismo para el login, la selección y las cargas.
- `SceneManager` pierde sus 7 `std::function` del constructor; `main.cpp` (1.010 líneas, `RunScenes` de 283) pasa a
  `app/App/Composition.cpp` y `main.cpp` queda en lo que dice CODING_RULES: arma las capas y arranca.
- **Verificación:** el golden idéntico; un test de cada guion afirma el orden de sus pasos contra la lista del research
  (02-arranque y 06-render, el orden del frame).

## Épica AR6: deduplicación y tests

- Las 22 funciones del original portadas dos veces (88 funciones) quedan en una, si las dos dan lo mismo (si no, la
  diferencia es un bug del port: se abre aparte).
- Los clones que copian clones del original se factorizan donde el comportamiento no cambia (`Skill55cbc0` ≡
  `Skill55faa0`, los kernels de partículas de Kalima, la selección y las pruebas, los dos bloques de `Cloth.cpp`).
- `tests/support/` suma lo que hoy está copiado: `LetterMeasure` (40 archivos), dos `IModelLibrary` falsos (nulo y
  triángulo; hoy 60 archivos), un constructor de `GameHudContext` (armado a mano 42 veces) y los ayudantes numéricos de
  los tests (`F32` en 41 archivos).

## Épica AR7: cierre

- `check_layers.py` sin línea de base (cero violaciones, cero ciclos entre módulos).
- `readability-function-size` de clang-tidy con los tamaños de la spec, en modo trinquete: solo el código nuevo o
  cambiado, y las transcripciones de `switch` del original en una lista de excepciones.
- `Client/README.md` con el mapa de módulos; el ADR de AR0 pasa a aceptado.

## Orden y paralelismo

```text
AR0.1 ─► AR0.2 ─┬─► AR1.1 ─► AR1.2 ─► AR2.1 ─► AR2.2 ─► AR2.3 ─┐
AR0.3 ──────────┘                      AR3.1 ─► AR3.2 ──────────┴─► AR4.1 ─► AR4.2 ─► AR4.3 ─► AR4.4 ─► AR4.5 ─► AR5 ─► AR6 ─► AR7
```

- AR0.3 va en paralelo con AR0.1 y AR0.2. AR3.1 puede empezar con AR2.1.
- Dentro de AR4, el diseño de un hito puede correr mientras se genera la mudanza del siguiente.
- El port usa el tercer agente todo el tiempo; si el port se pone urgente, el refactor para en un borde de hito (cada
  hito deja `main` en un estado completo).

## Self-review (hecho al escribirlo)

- **Cobertura de la spec:** las tres raíces (AR1, AR4), la tabla de opcodes y los handlers (AR2), el estado por
  módulo y `globales.tsv` (AR3), los guiones (AR5), la repetición (AR1.1, AR2.1, AR4, AR6), el golden de
  comportamiento (Task 7), el rastreo a prueba de mudanzas (Tasks 3 a 6), las reglas y el ADR (Task 2), el chequeo de
  capas (Task 1 y AR7). La decisión de los ejecutables de test queda como pregunta en AR1.2.
- **Nombres consistentes:** `modulos.tsv`, `mapa-modulos.tsv`, `check_layers.py`, `check_pure_move.py`, `mover.py`,
  `mudanzas.tsv`, `.git-blame-ignore-revs`, `FrameDigest`, `GameContext`, `Route<&Fn>`, `SinPortar{va}` se usan igual
  en todo el plan.
