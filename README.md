# MuRet

Servidores y cliente de MU Online 1.02 reescritos en C++20 moderno, para Windows y Linux.
El repo crece de a un componente por vez: cada servidor en `Server/<Nombre>/` y el cliente en `Client/`.

Es también un proyecto de aprendizaje de C++ de sistemas y sistemas distribuidos:
el trabajo va por olas y sub-olas, y cada sub-ola es un concepto y un PR chico
(ver [el diseño](docs/superpowers/specs/2026-10-25-dataserver-port-design.md)).

## Componentes

| Componente | Qué hace | Estado |
| --- | --- | --- |
| [`Server/DataServer`](Server/DataServer/README.md) | traduce los pedidos de los GameServers a SQL (puerto 55960) | ola 0: andamiaje |
| [`Client`](Client/README.md) | el cliente del jugador (`main.exe`), con SDL3 y OpenGL 3.3 | ola CL0: andamiaje |

## Compilar

Requisitos: CMake 3.25+, y Ninja + GCC/Clang en Linux o Visual Studio 2022 en Windows.

```bash
# Linux
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug

# Windows (PowerShell o "Developer Command Prompt")
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
```

Los binarios quedan en `build/<preset>/`.

## Documentación

- Entrada para agentes y personas: [`AGENTS.md`](AGENTS.md); reglas de código: [`docs/CODING_RULES.md`](docs/CODING_RULES.md)
- Roadmap del port (objetivo, decisiones, las 9 olas): [`docs/superpowers/specs/2026-10-25-dataserver-port-design.md`](docs/superpowers/specs/2026-10-25-dataserver-port-design.md)
- Research del original (qué hace, byte a byte): [`Server/DataServer/docs/research/`](Server/DataServer/docs/research/)
- Cliente: [roadmap](docs/superpowers/specs/2026-10-30-cliente-design.md) (decisiones, olas CL0 a CL10) y [research](Client/docs/research/README.md)
- Plan de cada ola: [`docs/superpowers/plans/`](docs/superpowers/plans/)
- Guías de estudio por ola, notas y progreso: [`docs/learning/`](docs/learning/README.md)
