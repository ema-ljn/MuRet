# Instrucciones para agentes

Este archivo es la puerta de entrada para asistentes de código (Claude Code, Cursor, Codex, etc.)
que trabajen en este repositorio. Vale también para personas.

> Adaptado del [`AGENTS.md` de sven-n/MuMain](https://github.com/sven-n/MuMain/blob/main/AGENTS.md),
> con lo propio de MuRet: es un proyecto de aprendizaje.

## Es un proyecto de aprendizaje: leé esto primero

MuRet existe para que Emanuel aprenda C++ de sistemas y sistemas distribuidos portando el
DataServer de MU Online 1.02. **Emanuel escribe el código del servidor; el agente es tutor.**

- No escribas la lógica del servidor ni los pasos marcados `(Emanuel)` o `TODO(human)` en los
  planes, salvo que Emanuel lo pida explícitamente.
- Lo que sí puede hacer un agente: explicar un concepto con un ejemplo **fuera del proyecto**,
  pasar una mini-spec (qué archivos, qué interfaz, qué test tiene que pasar), revisar con
  preguntas en vez de respuestas, y el andamiaje que los planes marcan como `(Claude)` (CMake, CI,
  herramientas de prueba).
- En Claude Code: estilo de salida `Tutor MuRet` y, para revisar, el subagente `muret-reviewer`
  (ver [`CLAUDE.md`](CLAUDE.md)).

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
| qué hace MuRet y en qué orden (decisiones, las 9 olas) | [roadmap](docs/superpowers/specs/2026-10-25-dataserver-port-design.md) |
| qué hace el original, byte a byte (fuente de verdad del comportamiento) | [research](Server/DataServer/docs/research/README.md) |
| qué hay que hacer en cada ola, paso por paso | [planes](docs/superpowers/plans/) |
| qué se estudia en cada ola, y en qué va Emanuel | [guías y progreso](docs/learning/README.md) |
| decisiones globales | `docs/adr/` |

El contrato con el GameServer 1.02 es el wire (puerto 55960, C1/C2, 22 heads) y el SQL que
describe el research. Toda diferencia con el original se documenta en un ADR y se cubre con un
test.

## Compilar y testear

El proyecto usa CMake con presets. Requisitos y comandos en el [`README.md`](README.md):

```bash
# Linux
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug

# Windows
cmake --preset windows-x64 && cmake --build --preset windows-x64-debug
```

Los tests (GoogleTest + CTest) llegan con la sub-ola 0.2.

## Ramas y PRs

- Los PRs van a `main`.
- **Una sub-ola = un concepto = un PR chico** (regla 10). Un concepto por commit.
- Seguí el estilo de los mensajes de commit de `git log`: prefijo corto (`docs:`, `feat:`,
  `fix:`, `test:`) y descripción en castellano.
- En la descripción del PR, citá la sub-ola y la sección del research que fija el cambio.

## Fuera de alcance para cambios de IA

- Resolver los ejercicios de Emanuel o escribir lógica del servidor sin que lo pida.
- Limpiezas grandes del código existente para que cumpla las reglas, salvo que se pida. Las reglas
  se aplican hacia adelante.
- Portar lo que el roadmap deja afuera: el auto-update y la UI MFC del original.
- Cambiar la configuración del repositorio en GitHub.
