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
| [`Client`](Client/README.md) | el cliente del jugador (`main.exe`), con SDL3 y OpenGL 3.3 | ola CL1: lee sin ventana las tablas `.bmd` (`Text`, `Gate`, `Item`, `Skill`), el terreno de un mundo y las texturas OZT y OZJ |

## Compilar

Requisitos: CMake 3.25+, Ninja + GCC/Clang en Linux o Visual Studio 2022 en Windows, y
[vcpkg](https://github.com/microsoft/vcpkg) con la variable `VCPKG_ROOT` apuntando a su clon. Las
dependencias (SDL3, libjpeg-turbo, GoogleTest) están en [`vcpkg.json`](vcpkg.json) y vcpkg las baja y compila al
configurar, en las versiones de su `builtin-baseline`. libjpeg-turbo además va fija en `overrides`,
porque otra versión puede decodificar distinto (`Client/tests/vectors/jpeg/README.md`). CLI11 is pinned too, because
the text of its help and of its errors reaches the tests (AR1.3).

```bash
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg          # Windows: bootstrap-vcpkg.bat y $env:VCPKG_ROOT = "C:\vcpkg"
```

Si al configurar vcpkg dice que no encuentra la baseline, el clon es más viejo que `vcpkg.json`:
actualizalo con `git -C "$VCPKG_ROOT" pull`.

En Linux, SDL3 necesita los headers de X11, Wayland y OpenGL. El puerto de ALSA requiere Autotools.
libjpeg-turbo usa NASM para SIMD y en Windows vcpkg lo descarga.

```bash
sudo apt install ninja-build pkg-config autoconf autoconf-archive automake libtool \
  libx11-dev libxext-dev libwayland-dev libxkbcommon-dev libgl-dev libegl-dev nasm
```

```bash
# Linux
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug

# Windows (PowerShell o "Developer Command Prompt")
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

Los binarios quedan en `build/<preset>/`. El CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml))
hace lo mismo en Linux y Windows, con warnings como errores, en cada PR.

## Documentación

- Entrada para agentes y personas: [`AGENTS.md`](AGENTS.md); reglas de código: [`docs/CODING_RULES.md`](docs/CODING_RULES.md)
- Roadmap del port (objetivo, decisiones, las 9 olas): [`docs/superpowers/specs/2026-10-25-dataserver-port-design.md`](docs/superpowers/specs/2026-10-25-dataserver-port-design.md)
- Research del original (qué hace, byte a byte): [`Server/DataServer/docs/research/`](Server/DataServer/docs/research/)
- Cliente: [roadmap](docs/superpowers/specs/2026-10-30-cliente-design.md) (decisiones, olas CL0 a CL10) y [research](Client/docs/research/README.md)
- Plan de cada ola: [`docs/superpowers/plans/`](docs/superpowers/plans/)
- Guías de estudio por ola, notas y progreso: [`docs/learning/`](docs/learning/README.md)
