# Instrucciones para agentes

Este archivo es la puerta de entrada para asistentes de código (Claude Code, Cursor, Codex, etc.)
que trabajen en este repositorio. Vale también para personas.

> Adaptado del [`AGENTS.md` de sven-n/MuMain](https://github.com/sven-n/MuMain/blob/main/AGENTS.md),
> con lo propio de MuRet: es un proyecto de aprendizaje.

## Es un proyecto de aprendizaje: leé esto primero

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
| qué se estudia en cada ola, y en qué va Emanuel | [guías y progreso](docs/learning/README.md) |
| decisiones globales | `docs/adr/` |

El contrato con el GameServer 1.02 es el wire (puerto 55960, C1/C2, 22 heads) y el SQL que
describe el research. Toda diferencia con el original se documenta en un ADR y se cubre con un
test.

## Compilar y testear

El proyecto usa CMake con presets y vcpkg en modo manifiesto (`VCPKG_ROOT` tiene que apuntar a un
clon de vcpkg). Requisitos y comandos en el [`README.md`](README.md):

```bash
# Linux
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug

# Windows
cmake --preset windows-x64 && cmake --build --preset windows-x64-debug && ctest --preset windows-x64-debug
```

Los tests son de GoogleTest y los corre CTest. Los golden del cliente leen `MURET_CLIENT_GOLDEN_DIR`
y se saltean sin ella ([`Client/README.md`](Client/README.md#tests)). El CI
(`.github/workflows/ci.yml`) compila con warnings como errores y corre los tests en Linux y Windows.

## Ramas y PRs

- Los PRs van a `main`.
- **Una sub-ola = un concepto = un PR chico** (regla 10). Un concepto por commit.
- Mensajes de commit en notación Angular: `tipo(alcance): descripción`, en castellano, con el
  alcance opcional (por ejemplo `docs(learning): …`, `feat(tutor): …`, `fix: …`) y un cuerpo que
  explique el porqué.
- En la descripción del PR, citá la sub-ola y la sección del research que fija el cambio.

## Fuera de alcance para cambios de IA

- Resolver los ejercicios de Emanuel o escribir lógica del servidor o del cliente sin que lo pida.
- Versionar assets del juego (`Data\` del cliente) o volcados de memoria: solo hashes y scripts.
- Limpiezas grandes del código existente para que cumpla las reglas, salvo que se pida. Las reglas
  se aplican hacia adelante.
- Portar lo que el roadmap deja afuera: el auto-update y la UI MFC del original.
- Romper protecciones del binario original (VM de `MyDll`, Themida, licencias, FoxAnti): el
  research se limita a protocolo y comportamiento. Del anticheat del cliente solo se porta la capa
  de ofuscación de paquetes que el servidor del laboratorio espera.
- Cambiar la configuración del repositorio en GitHub.
