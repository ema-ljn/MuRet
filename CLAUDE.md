# MuRet

Repo incremental de servidores de MU Online 1.02 en C++20. El primer componente es
`Server/DataServer/`, portado desde EmaNLJN/mu-server-repack-research-102i. El cliente (`main.exe`)
se reimplementa en `Client/` (roadmap: `docs/superpowers/specs/2026-10-30-cliente-design.md`,
research: `Client/docs/research/`).

**Es un proyecto de aprendizaje, con el modo tutor apagado por ahora** (Emanuel, 2026-11-10): Claude
escribe el código, incluidos los pasos `(Emanuel)` y `TODO(human)`, sin pedir el consentimiento del
tutor (ver `AGENTS.md`). El estilo de salida `Tutor MuRet` (`/output-style Tutor MuRet`), el subagente
`muret-reviewer` y `/tutor` (revisión corta de cambios sin commitear) se usan solo si Emanuel los pide.
Progreso en `docs/learning/PROGRESS.md`. Para C++, CMake, vcpkg y
GoogleTest está la skill `cpp-pro` (`.claude/skills/cpp-pro/`), por debajo de las reglas del repo.

- Entrada general para agentes y personas: `AGENTS.md`. Reglas de código: `docs/CODING_RULES.md`
  (adaptadas de MuMain) y formato con `.clang-format`.
- Roadmap (qué hace MuRet y en qué orden): `docs/superpowers/specs/2026-10-25-dataserver-port-design.md`
- Camino de aprendizaje (cómo se aprende: N.0, olas 9 a 14, carril R): `docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md`
- Research (qué hace el original, byte a byte): `Server/DataServer/docs/research/` (01..10)
- Planes por ola: `docs/superpowers/plans/`
- Trabajo por olas y sub-olas: una sub-ola = un concepto = un PR chico.
