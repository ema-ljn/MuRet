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

### Windows in one command

[`Client\tools\windows\build.cmd`](Client/tools/windows/build.ps1) does every step: it finds Visual Studio 2022 or
newer, enters its developer shell, keeps its own vcpkg clone at the `builtin-baseline` of `vcpkg.json` in
`%LOCALAPPDATA%\MuRet\vcpkg` (as CI does), and builds `main.exe` in Release with Ninja. It needs only Git and Visual
Studio with "Desktop development with C++" and "C++ CMake tools for Windows"; `VCPKG_ROOT` is not used.

```bat
Client\tools\windows\build.cmd
Client\tools\windows\build.cmd -Package
Client\tools\windows\build.cmd -Config Debug -Tests
```

| Option | What it does |
| --- | --- |
| `-Config Debug` | Debug instead of Release |
| `-Tests` | builds every target and runs CTest; CI runs them in Debug |
| `-Package` | `main.exe`, its DLLs, the Visual C++ runtime (Release), a `muret-client.ini` and `Client\assets\Data` in `build\package\<name>\`, and its zip |
| `-NoData` | with `-Package`, no `Data`: `main.exe` then reads a `Data` folder beside it |
| `-Clean` | deletes the build folder first |
| `-Jobs <N>` | N compilations at once, if `cl` runs out of memory (C1060) |
| `-Define <NAME=VALUE>` | a CMake cache variable, for example `MU_XOR_REMAP=OFF` |
| `-VcpkgRoot <path>` | your own vcpkg clone, used as it is |

The first run is the long one: it clones vcpkg and builds its ports, which vcpkg then keeps in its binary cache. At
the end the script prints how to run `main.exe` with the repo's `Data`, through a `muret-client.ini` it leaves in
`build\<preset>\` with the data path and a font (`[Video] FontFile`, Arial). From PowerShell,
`.\Client\tools\windows\build.ps1` takes the same options when the execution policy lets scripts run.

### Releases

A tag `vX.Y.Z` publishes the Windows client as a GitHub Release, built by
[`release.yml`](.github/workflows/release.yml) with the script above. The version is the one in
`project(MuRet VERSION ...)` of `CMakeLists.txt`, which `main.exe --version` and the login show, and the workflow
fails on any other tag. The zip holds `main.exe`, its DLLs, the Visual C++ runtime and a `muret-client.ini`, without
`Data`: the original client's `Data` folder goes beside `main.exe`. To publish one, raise the version in a PR, merge
it, then:

```bash
git fetch origin main
git tag v0.2.0 origin/main
git push origin v0.2.0
```

A PR that changes `Client/tools/windows/` or `release.yml` builds the package without publishing it, and Actions >
Release > Run workflow builds it from any branch. Both leave the zip as the run's artifact.

## Documentación

- Entrada para agentes y personas: [`AGENTS.md`](AGENTS.md); reglas de código: [`docs/CODING_RULES.md`](docs/CODING_RULES.md)
- Roadmap del port (objetivo, decisiones, las 9 olas): [`docs/superpowers/specs/2026-10-25-dataserver-port-design.md`](docs/superpowers/specs/2026-10-25-dataserver-port-design.md)
- Research del original (qué hace, byte a byte): [`Server/DataServer/docs/research/`](Server/DataServer/docs/research/)
- Cliente: [roadmap](docs/superpowers/specs/2026-10-30-cliente-design.md) (decisiones, olas CL0 a CL10) y [research](Client/docs/research/README.md)
- Plan de cada ola: [`docs/superpowers/plans/`](docs/superpowers/plans/)
- Guías de estudio por ola, notas y progreso: [`docs/learning/`](docs/learning/README.md)
