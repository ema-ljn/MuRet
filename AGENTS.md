# Instrucciones para agentes

Este archivo es la puerta de entrada para asistentes de código (Claude Code, Cursor, Codex, etc.)
que trabajen en este repositorio. Vale también para personas.

> Adaptado del [`AGENTS.md` de sven-n/MuMain](https://github.com/sven-n/MuMain/blob/main/AGENTS.md),
> con lo propio de MuRet: es un proyecto de aprendizaje.

## Es un proyecto de aprendizaje: leé esto primero

> **Por ahora el modo tutor está apagado** (Emanuel, 2026-11-10). Los agentes escriben el código del
> servidor y del cliente, incluidos los pasos marcados `(Emanuel)` o `TODO(human)`, sin pedir el
> consentimiento del tutor. El estilo de salida `Tutor MuRet`, el subagente `muret-reviewer` y `/tutor`
> se usan solo si Emanuel los pide. Lo que sigue en esta sección describe el modo tutor, para cuando
> vuelva; mientras tanto no aplica.

MuRet existe para que Emanuel aprenda C++ moderno, MU (incluido el binario original) y sistemas
distribuidos portando el DataServer de MU Online 1.02, después otros servidores, y reimplementando el cliente
(`Client/`). **Emanuel escribe el código del servidor y del cliente; el agente es tutor.**

- No escribas la lógica del servidor o del cliente ni los pasos marcados `(Emanuel)` o `TODO(human)` en los
  planes, salvo que Emanuel lo pida explícitamente.
- Lo que sí puede hacer un agente: explicar un concepto con un ejemplo **fuera del proyecto**,
  pasar una mini-spec (qué archivos, qué interfaz, qué test tiene que pasar), revisar con
  preguntas en vez de respuestas, y el andamiaje que los planes marcan como `(Claude)` (CMake, CI,
  herramientas de prueba).
- En Claude Code: estilo de salida `Tutor MuRet` y, para revisar, el subagente `muret-reviewer`
  (ver [`CLAUDE.md`](CLAUDE.md)). Para C++ está la skill
  [`cpp-pro`](.claude/skills/cpp-pro/SKILL.md), que dice dónde MuRet se aparta de su fuente.
- Clean Code es la guía del refactor del cliente (Emanuel, 2026-11-21). Las skills
  [`clean-code`](.claude/skills/clean-code/SKILL.md),
  [`clean-architecture`](.claude/skills/clean-architecture/SKILL.md),
  [`refactoring`](.claude/skills/refactoring/SKILL.md) y
  [`cpp-coding-standards`](.claude/skills/cpp-coding-standards/SKILL.md) traen sus reglas, cada una
  con una tabla de dónde MuRet manda distinto; van por debajo de CODING_RULES y de la fidelidad al
  original. Otros agentes las leen como docs.

## Reglas de código: leelas primero

**Las reglas de [`docs/CODING_RULES.md`](docs/CODING_RULES.md) valen para todo cambio de código
en este repositorio, lo haga una persona o un agente.**

Antes de escribir o modificar código:

1. Leé [`docs/CODING_RULES.md`](docs/CODING_RULES.md).
2. Aplicá las reglas al código nuevo o cambiado.
3. Al revisar un cambio, tomá `docs/CODING_RULES.md` como la base de estilo y calidad.

Las reglas no son exhaustivas: recogen las convenciones que más importan. Donde no dicen nada,
seguí el estilo del código que está alrededor.

## Dónde está cada cosa

| Qué | Dónde |
| --- | --- |
| qué hace MuRet y en qué orden (decisiones, olas 0 a 8) | [roadmap](docs/superpowers/specs/2026-10-25-dataserver-port-design.md) |
| cómo se aprende (sub-olas N.0, olas 9 a 14, carril R de ingeniería inversa) | [camino de aprendizaje](docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md) |
| qué hace el original, byte a byte (fuente de verdad del comportamiento) | [research](Server/DataServer/docs/research/README.md) |
| qué hace el cliente MuRet y en qué orden (decisiones D-C, olas CL0 a CL10) | [roadmap del cliente](docs/superpowers/specs/2026-10-30-cliente-design.md) |
| qué hace el `main.exe` original (red, formatos, escenas) | [research del cliente](Client/docs/research/README.md) |
| qué hay que hacer en cada ola, paso por paso | [planes](docs/superpowers/plans/) |
| el estado de la CL7 y lo que falta, en orden (traspaso del 2026-11-10) | [traspaso de la CL7](docs/superpowers/plans/2026-11-10-cl7-traspaso.md) |
| qué se estudia en cada ola, y en qué va Emanuel | [guías y progreso](docs/learning/README.md) |
| decisiones globales | `docs/adr/` |
| las reglas de Clean Code, Clean Architecture, refactoring y C++ Core Guidelines, adaptadas a MuRet | [skills del repo](.claude/skills/) |
| the client's architecture: layers, modules and where each file goes | [spec](docs/superpowers/specs/2026-11-20-client-architecture-design.md), [ADR 0023](docs/adr/0023-client-module-architecture.md), [`Client/modules.tsv`](Client/modules.tsv) and [`module-map.tsv`](Client/docs/research/coverage/module-map.tsv) |
| the client refactor, epic by epic (AR0 to AR7) | [plan](docs/superpowers/plans/2026-11-20-client-architecture-refactor.md) |
| where a client file goes and how a module is built | [`muret-module`](.claude/skills/muret-module/SKILL.md) skill |
| moving client files to their module, and the comments PR after a move | [`muret-move`](.claude/skills/muret-move/SKILL.md) skill |

El contrato con el GameServer 1.02 es el wire (puerto 55960, C1/C2, 22 heads) y el SQL que
describe el research. Toda diferencia con el original se documenta en un ADR y se cubre con un
test.

## Where a new port goes (client, 2026-11-21)

The client is moving to modules ([ADR 0023](docs/adr/0023-client-module-architecture.md),
[`CODING_RULES.md`](docs/CODING_RULES.md) §5). New code goes to its target module from the start:

1. Find its module in [`module-map.tsv`](Client/docs/research/coverage/module-map.tsv): the row of
   a file of the same feature, or the feature's module in [`Client/modules.tsv`](Client/modules.tsv).
2. If that module's folder does not exist yet, the file goes where it would go today (`Scenes/`,
   `UI/`, `World/`), already with the new anatomy (one handler per packet, the state it touches in
   its signature), and the same PR adds its row to the map. The move of its module carries it later.
3. Includes respect the layers. In CI, `check_layers.py` rejects a new include that goes up a layer
   or reaches a module of the same layer that `may_use` does not name, and a file without a module.
4. A new packet, from AR2 on: its layout in `Protocol/<Module>/`, its handler in `<Module>/In/` and
   its row in the opcode table, in place of its `Unported{va}` row.
5. A module that is being moved gets no port work that day; its design and port work on it do not
   run at the same time.
6. A sine, a cosine or any function the C library rounds comes from `Math/RoundedMath.h`,
   and a rotation or a projection from `RoundedTransforms.h`
   ([ADR 0027](docs/adr/0027-correctly-rounded-math.md), `CODING_RULES.md` §10). In CI,
   `check_rounded_math.py` rejects `std::sin`, `sinf`, `glm::rotate` and their kind.

The [`muret-module`](.claude/skills/muret-module/SKILL.md) skill has these steps.

## Compilar y testear

El proyecto usa CMake con presets y vcpkg en modo manifiesto (`VCPKG_ROOT` tiene que apuntar a un
clon de vcpkg). Requisitos y comandos en el [`README.md`](README.md):

```bash
# Linux
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug

# Windows
cmake --preset windows-x64 && cmake --build --preset windows-x64-debug && ctest --preset windows-x64-debug
```

Los tests son de GoogleTest y los corre CTest. Los golden del cliente leen `MU_CLIENT_GOLDEN_DIR`
y se saltean sin ella ([`Client/README.md`](Client/README.md#tests)). El CI
(`.github/workflows/ci.yml`) compila con warnings como errores y corre los tests en Linux y Windows.

## Cierre de las olas del cliente (pedido de Emanuel, 2026-11-05)

Antes de dar una CL por terminada, hacer un barrido profundo de **todas sus funcionalidades** en
pasadas complementarias: inventariar el comportamiento del original, contrastar ASM y pseudocódigo
con la implementación, y revisar de forma independiente qué observan realmente los tests. El ASM
prevalece sobre el pseudocódigo. Cubrir ramas, estado persistente, efectos secundarios, orden de
ejecución y transiciones, incluidos los comportamientos raros del original. Si una pasada encuentra
un hueco, corregirlo con evidencia y volver a revisar el área y sus dependencias. Registrar lo
cubierto, las discrepancias y lo que sigue sin verificarse; una sub-ola o una suite en verde no
cierran por sí solas toda la CL. Esto responde a huecos que aparecieron recién en pasadas posteriores.

## Ramas y PRs

- Los PRs van a `main`.
- The client refactor's PRs (scope `AR…`) go to `refactor/client`, the refactor's foundation branch, which merges into
  `main` when Emanuel decides (Emanuel, 2026-11-21). `main` is merged into it, never rebased or force-pushed, and the
  merge adds the map rows of the files the port added.
- **Una sub-ola = un concepto = un PR chico** (regla 10). Un concepto por commit.
- **Ramas, commits y títulos y descripciones de PR en inglés** (Emanuel, 2026-11-21), igual que el
  código, sus comentarios y los docs nuevos ([`CODING_RULES.md`](docs/CODING_RULES.md) §7 y §9; los
  docs de antes se traducen con el [plan de traducción](docs/superpowers/plans/2026-11-21-docs-to-english.md)).
- **Commits, títulos de PR y ramas en notación Angular** (Emanuel, 2026-11-21):
  - Commit y título de PR: `tipo(alcance): descripción`, con la descripción en imperativo, en
    minúscula y sin punto final, y un cuerpo que explique el porqué. El título del PR es el commit
    del squash, así que sigue la misma forma.
  - Tipos: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build` y `ci` (los de Angular).
  - Alcance: la ola o el hito si lo hay, porque la matriz lee la ola del título (`feat(CL11): …`,
    `refactor(AR1.2): …`); si no, el módulo o la carpeta (`docs(learning): …`, `fix(network): …`).
    Puede faltar (`fix: …`).
  - Rama: `tipo/alcance-descripcion-corta`, en minúscula y con guiones (`refactor/ar1.2-engine-move`,
    `feat/cl11-kalima-effects`, `fix/guild-mark-overflow`, `docs/translate-rules`). Las ramas que
    nombra el entorno de Claude Code en la nube (`claude/…`) no se pueden elegir; ahí el título del PR
    sigue la regla igual.
- En la descripción del PR, citá la sub-ola y la sección del research que fija el cambio.
- En el código, un comentario solo si es estrictamente necesario, corto y en inglés (CODING_RULES §9).

## Fuera de alcance para cambios de IA

- Resolver los ejercicios de Emanuel o escribir lógica del servidor o del cliente sin que lo pida.
- Versionar assets del juego o volcados de memoria: solo hashes y scripts. La excepción es
  `Client/assets/Data/`, la `Data\` entera del cliente original, tal cual: está en el repo porque
  es el original que MuRet reimagina y la base de sus tests (decidido por Emanuel el 2026-11-04).
  Los volcados de memoria y el `muproxy.log`, que lleva el login del jugador, siguen afuera.
- Limpiezas grandes del código existente para que cumpla las reglas, salvo que se pida. Las reglas
  se aplican hacia adelante.
- Portar lo que el roadmap deja afuera: el auto-update y la UI MFC del original.
- Romper protecciones del binario original (VM de `MyDll`, Themida, licencias, FoxAnti): el
  research se limita a protocolo y comportamiento. Del anticheat del cliente solo se porta la capa
  de ofuscación de paquetes que el servidor del laboratorio espera.
- Cambiar la configuración del repositorio en GitHub.
