# Plan de la ola CL0: Fundaciones del cliente

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-10-30.

**Objetivo:** que `Client/` exista, compile y testee igual en Windows y Linux, con su research y
su roadmap, antes de escribir una línea del cliente.

**Arquitectura:** `Client/` es un componente más del CMake raíz, al lado de `Server/DataServer`.
Desde la CL0.2, una biblioteca `ClientLib` con todo el código, el ejecutable `Main` y los tests
`ClientTests` linkeando contra ella.

**Stack:** C++20, CMake 3.25+, vcpkg (SDL3, GoogleTest), un loader de GL propio.

**Research que fija esta ola:** [01-binario-y-metodo](../../../Client/docs/research/01-binario-y-metodo.md)
(qué se reimplementa y con qué evidencia).

**Terminada cuando:** `Main` compila y corre en Windows y Linux en CI, `ClientTests` corre (con
los tests golden salteados si no hay `MURET_CLIENT_GOLDEN_DIR`) y una ventana SDL3 vacía abre y
cierra.

## Preguntas para decidir al empezar

1. **¿Cuándo arranca el cliente respecto del servidor?** La CL0.2 necesita GoogleTest (sub-ola 0.2
   del servidor). Recomendación del roadmap: CL1 después de la ola 1 del servidor.
   **Decidido (Emanuel, 2026-11-01): el cliente va directo.** La CL0.2 trae ella misma vcpkg,
   GoogleTest y los test presets, y la CL0.3 el CI; la 0.2 del servidor los reusa.
2. **¿Cómo se saltea un test golden?** GoogleTest tiene `GTEST_SKIP()`. ¿El test se saltea en
   silencio si falta la carpeta, o falla si la carpeta está pero le falta un archivo? (El repo de
   research hace lo segundo: "si no está, se saltea; si está incompleto, falla".)
   **Decidido (2026-11-01): la regla del repo de research.** Sin `MURET_CLIENT_GOLDEN_DIR` (o
   vacía), el test se saltea y CTest lo muestra como `Skipped`, no como pasado. Si la variable
   apunta a una carpeta que no existe, o a una a la que le falta el archivo que el test pide, el
   test falla: una carpeta golden incompleta es un error, no un motivo para saltear.

---

## Sub-ola CL0.1: esqueleto de `Client/`, research y roadmap ✅ (Claude)

**Concepto:** ninguno nuevo; es la 0.1 del servidor aplicada a un segundo componente. Sirve de
repaso: ¿qué cambia en el CMake raíz cuando hay dos `add_subdirectory`?

**Archivos:**
- Crear: `Client/CMakeLists.txt`, `Client/src/main.cpp`, `Client/README.md`
- Crear: `Client/docs/research/README.md` y `01` a `08`
- Crear: `docs/superpowers/specs/2026-10-30-cliente-design.md` y este plan
- Modificar: `CMakeLists.txt` raíz, `README.md`, `AGENTS.md`, `CLAUDE.md`, `docs/learning/README.md`, `docs/learning/PROGRESS.md`

- [x] **Paso 1:** `add_subdirectory(Client)` en el CMake raíz.
- [x] **Paso 2:** target `Main` con un `main.cpp` que imprime una línea y sale con 0, con `muret_set_warnings`.
- [x] **Paso 3:** research del cliente desde `client-research/` del repo de research (01..08).
- [x] **Paso 4:** roadmap del cliente con las decisiones D-C1 a D-C14 y las olas CL0 a CL10.
- [x] **Paso 5:** verificar en Linux:
  ```bash
  cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
  ./build/linux-x64-debug/Client/Main
  ```
- [ ] **Paso 6 (Emanuel):** compilar y correr en Windows (`build\windows-x64\Client\Debug\Main.exe`) y leer el research en el orden del README.

## Sub-ola CL0.2: `ClientLib`, `ClientTests` y la carpeta golden ✅ (Claude, ejemplo trabajado)

**Concepto:** biblioteca + ejecutable + tests como tres targets; tests que dependen de datos
externos y cómo se saltean sin mentir.

**Archivos:**
- Crear: `vcpkg.json` (`gtest`, con la `builtin-baseline` fija), `Client/src/Data/DataPath.h/.cpp`
- Crear: `Client/tests/support/GoldenDir.h/.cpp`, `Client/tests/data/DataPathTest.cpp`, `Client/tests/data/GoldenDataFilesTest.cpp`
- Modificar: `Client/CMakeLists.txt` (targets `ClientLib`, `ClientTests`; opción `MURET_CLIENT_GOLDEN_DIR`)
- Modificar: `CMakePresets.json` (toolchain de vcpkg y test presets), `CMakeLists.txt` (opción `MURET_BUILD_TESTS`)

- [x] **Paso 1 (Claude, andamiaje):** `ClientLib` estática, `Main` y `ClientTests` linkeando contra ella; registrar en CTest con `gtest_discover_tests`. Lo primero de `ClientLib` es `Client::Data::ResolveDataPath`: el original pide rutas como `Data\Player\Player.bmd` y Windows no distingue mayúsculas, pero el `Data\` del laboratorio tiene `player\`; en Linux hay que buscar sin distinguirlas.
- [x] **Paso 2 (Claude, andamiaje):** `MURET_CLIENT_GOLDEN_DIR` llega a los tests como variable de entorno al correr `ctest`, o como variable de caché de CMake que se la pasa a CTest.
- [x] **Paso 3 (Claude, a pedido de Emanuel):** `Client::Tests::GoldenDataTest`, la base de los tests golden con la regla de la pregunta 2, y dos tests de ejemplo (las claves `Enc1.dat`/`Dec2.dat` de 54 bytes y `Player\Player.bmd`). La regla misma tiene tests (`GoldenDirTest`), separada de la lectura del entorno.
- [x] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug
  ```
  Sin la variable, los 2 golden salen `Skipped` y el resto pasa (en Windows también se saltean
  los que necesitan un sistema de archivos que distinga mayúsculas). Con
  `MURET_CLIENT_GOLDEN_DIR=<clon de mu-online-reverse-lab>/analysis/decompilacion-maxima/assets/original-data`
  pasan también los golden; con una carpeta vacía, fallan con "falta … en la carpeta golden".

## Sub-ola CL0.3: SDL3, loader de GL y CI ✅ (Claude, ejemplo trabajado)

**Concepto:** dependencias de terceros con vcpkg en modo manifiesto; qué es un loader de OpenGL y
por qué hace falta (las funciones de GL 3.3 no se enlazan, se piden en runtime).

**Archivos:**
- Modificar: `vcpkg.json` (`sdl3`), `Client/CMakeLists.txt`, `Client/src/main.cpp`
- Crear: `.github/workflows/ci.yml` (no existía: la 0.2 del servidor todavía no se había hecho)

- [x] **Paso 1 (Claude, andamiaje):** `sdl3` en `vcpkg.json`, sin sus features por defecto (en Linux, `x11` y `wayland`; afuera `ibus`, que arrastra D-Bus). `find_package(SDL3)` y link en `Main`, **no** en `ClientLib`: la biblioteca queda sin ventana, así sus tests corren en cualquier máquina. El loader es propio (D-C2): una tabla de punteros tipados con los `typedef` `PFNGL…PROC` (`PFNGLCLEARPROC`, `PFNGLCREATESHADERPROC`), que se llena con `SDL_GL_GetProcAddress`. Los de GL 1.x los declara `SDL_opengl.h` si se define `SDL_OPENGL_1_FUNCTION_TYPEDEFS`; los de la 3.3 vienen siempre en `SDL_opengl_glext.h`. (`decltype(&::glClear)` no sirve: las funciones de la 3.3 no tienen prototipo.) Después de cargarla, `main` comprueba con `glGetIntegerv(GL_MAJOR_VERSION)` que el contexto sea 3.3 o más, porque en GLX `SDL_GL_GetProcAddress` devuelve un puntero hasta para una función que no existe. El puerto `glad` de vcpkg trae el perfil de compatibilidad y necesita Python para compilar.
- [x] **Paso 2 (Claude, a pedido de Emanuel):** en `main.cpp`, una ventana SDL3 de 640×480 con contexto OpenGL 3.3 core que se limpia de un azul oscuro y cierra con la X; `--frames N` dibuja N frames y sale, para probarla sin nadie que la cierre. Los argumentos se leen en `ClientLib` (`App/CommandLine`), con sus tests, y CTest corre `Main --frames 0` para comprobar que arranca (en Windows, que encuentra `SDL3.dll`) sin abrir la ventana.
- [x] **Paso 3:** `GL_VERSION` y `GL_RENDERER` al arrancar. Linux con Xvfb y Mesa: `4.5 (Core Profile) Mesa 25.2.8` y `llvmpipe (LLVM 20.1.2, 256 bits)` (Mesa da un contexto core más nuevo que el pedido, y está bien). Sin VSync en Xvfb: avisa y sigue. **Falta Windows** (paso 5).
- [x] **Paso 4:** el CI (`.github/workflows/ci.yml`) compila y corre los tests en Windows (MSVC) y Linux (GCC) con warnings como errores. En Linux además abre la ventana en Xvfb y dibuja 60 frames; en Windows no, porque el runner no tiene un driver de OpenGL 3.3.
- [ ] **Paso 5 (Emanuel):** en tu máquina, con `VCPKG_ROOT` apuntando a un clon de vcpkg, compilar con `windows-x64`, correr `Main.exe` y anotar acá qué `GL_VERSION` y `GL_RENDERER` dio.

## Para cerrar la CL0 (Emanuel)

Lo resolvió Claude como ejemplo; para que quede tuyo, contestá con tus palabras:

1. ¿Por qué `SDL3::SDL3` se linkea a `Main` y no a `ClientLib`? ¿Qué pasaría con `ClientTests` en el CI si fuera al revés?
2. `glClear` está declarada en `SDL_opengl.h`, pero `main.cpp` nunca la llama por su nombre. ¿Por qué no alcanza con linkear `opengl32.lib` o `libGL.so` para las funciones de la 3.3?
3. ¿Qué fija la `builtin-baseline` de `vcpkg.json`, y qué pasaría en el CI si no estuviera?
4. Un test golden salteado y uno pasado, ¿en qué se diferencian en la salida de `ctest`? ¿Por qué la regla hace fallar (y no saltear) una carpeta golden incompleta?
