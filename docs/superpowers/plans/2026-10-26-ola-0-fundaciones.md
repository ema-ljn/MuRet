# Plan de la ola 0: Fundaciones

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Diseño: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).

**Objetivo:** que el repo compile y testee igual en Windows y Linux, con la
documentación base en su lugar, antes de escribir una sola línea del servidor.

**Arquitectura:** CMake raíz que agrega un subdirectorio por componente
(`Server/DataServer`); presets por plataforma; dependencias por vcpkg en modo manifiesto;
GoogleTest + CTest; CI en GitHub Actions con Windows y Linux.

**Stack:** C++20, CMake 3.25+, Ninja (Linux), Visual Studio 2022 (Windows), vcpkg,
GoogleTest, GitHub Actions.

**Guía de estudio:** [`docs/learning/ola-0-fundaciones.md`](../../learning/ola-0-fundaciones.md): leé su
sección antes de cada sub-ola; la sección final la completás al cerrar la ola.

**Terminada cuando:** `cmake --build` y `ctest` pasan en los dos sistemas, en CI.

---

## Sub-ola 0.1: CMake raíz y esqueleto de `Server/DataServer` ✅

**Concepto:** cómo se arma un programa C++ (preprocesador → compilador → linker) y cómo
CMake lo describe con *targets*. Ver [`docs/learning/ola-0.1-cmake.md`](../../learning/ola-0.1-cmake.md).

**Archivos:**
- Crear: `CMakeLists.txt`, `CMakePresets.json`, `cmake/CompilerWarnings.cmake`, `.gitignore`
- Crear: `Server/DataServer/CMakeLists.txt`, `Server/DataServer/src/main.cpp`, `Server/DataServer/README.md`
- Crear: `README.md`, esta spec y este plan

- [x] **Paso 1:** CMake raíz con C++20 estricto (`CMAKE_CXX_EXTENSIONS OFF`) y `add_subdirectory(Server/DataServer)`.
- [x] **Paso 2:** `muret_set_warnings(target)` con `/W4 /permissive-` en MSVC y `-Wall -Wextra -Wpedantic -Wconversion …` en GCC/Clang; opción `MURET_WARNINGS_AS_ERRORS`.
- [x] **Paso 3:** presets `linux-x64-debug`, `linux-x64-release` (Ninja) y `windows-x64` (VS 2022, multi-config) con build presets `windows-x64-debug/release`.
- [x] **Paso 4:** target `DataServer` con un `main.cpp` que imprime una línea y sale con 0.
- [x] **Paso 5:** verificar en Linux:
  ```bash
  cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
  ./build/linux-x64-debug/Server/DataServer/DataServer   # imprime la línea, exit 0
  ```
  Verificado con GCC 13 y Clang con `-DMURET_WARNINGS_AS_ERRORS=ON`.
- [ ] **Paso 6 (Emanuel):** compilar con el preset `windows-x64` en tu máquina y contestar las preguntas de la nota de aprendizaje.

## Sub-ola 0.2: vcpkg, GoogleTest, CTest y CI

**Concepto:** gestión de dependencias (vcpkg en modo manifiesto), tests unitarios y
qué es un *test runner*; integración continua en dos sistemas. Ver la
[guía, § 0.2](../../learning/ola-0-fundaciones.md#02-dependencias-tests-y-ci).

**Archivos:**
- Crear: `vcpkg.json` (dependencia `gtest`), `Server/DataServer/tests/CMakeLists.txt`, `Server/DataServer/tests/SmokeTest.cpp`
- Crear: `.github/workflows/ci.yml`
- Modificar: `CMakePresets.json` (toolchain de vcpkg en el preset base), `CMakeLists.txt` (`enable_testing()`, opción `MURET_BUILD_TESTS`), `Server/DataServer/CMakeLists.txt` (`add_subdirectory(tests)`)

- [ ] **Paso 1:** `vcpkg.json` con `gtest`; preset base con `"toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"`. Verificar: `cmake --preset linux-x64-debug` descarga y compila gtest.
- [ ] **Paso 2:** `find_package(GTest CONFIG REQUIRED)`, target `DataServerTests` linkeado a `GTest::gtest_main`, `gtest_discover_tests(DataServerTests)`.
- [ ] **Paso 3 (Emanuel, `TODO(human)`):** escribir el primer test en `SmokeTest.cpp`. Primero uno que **falle** a propósito, verlo rojo con `ctest --preset …`, después hacerlo pasar. Objetivo: entender el ciclo rojo → verde y leer la salida de GoogleTest.
- [ ] **Paso 4:** test presets en `CMakePresets.json` (`ctest --preset linux-x64-debug`).
- [ ] **Paso 5:** `.github/workflows/ci.yml` con matriz `ubuntu-latest` + `windows-latest`: checkout, vcpkg (cache), configure, build, test. Verificar: CI verde en el PR.

## Sub-ola 0.3: documentación base

**Concepto:** por qué se documentan las decisiones (ADR) y las reglas de código antes
de que haya mucho código; cómo se lee un repo organizado como MuMain. Ver la
[guía, § 0.3](../../learning/ola-0-fundaciones.md#03-documentación-base).

**Archivos:**
- Crear: `AGENTS.md`, `docs/CODING_RULES.md`, `docs/adr/0001-contrato-wire-y-sql.md`, `docs/adr/0002-iocp-epoll-a-mano.md`, `.clang-format`

- [x] **Paso 1:** `docs/CODING_RULES.md` adaptado de MuMain: namespaces `DataServer::<Capa>`, RAII obligatorio, sin `new`/`delete` crudos, nada hardcodeado, regla de capas (Network no sabe de SQL, Handlers no tocan sockets, Database no conoce paquetes).
- [ ] **Paso 2 (Emanuel):** escribir el ADR 0001 (el contrato wire + SQL se respeta al byte) con la plantilla Contexto / Decisión / Consecuencias. Yo reviso.
- [ ] **Paso 3:** ADR 0002 (IOCP y epoll a mano, Asio como comparación opcional).
- [x] **Paso 4:** `AGENTS.md` como entrada para humanos y agentes, apuntando a `CODING_RULES.md`, la spec y `docs/learning/`.
- [x] **Paso 5:** `.clang-format` (el de MuMain) y verificar que `main.cpp` lo respeta: `clang-format --dry-run --Werror` limpio.

---

## Al cerrar la ola 0

- [ ] Completar la sección de cierre de la [guía de la ola 0](../../learning/ola-0-fundaciones.md).
- [ ] Actualizar `docs/learning/PROGRESS.md` (conceptos dominados y flojos).
- [ ] Revisar el [plan de la ola 1](2026-10-26-ola-1-protocolo-sin-red.md) (protocolo sin red) con lo aprendido.
