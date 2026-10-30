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

**Stack:** C++20, CMake 3.25+, vcpkg (SDL3, loader de GL), GoogleTest.

**Research que fija esta ola:** [01-binario-y-metodo](../../../Client/docs/research/01-binario-y-metodo.md)
(qué se reimplementa y con qué evidencia).

**Terminada cuando:** `Main` compila y corre en Windows y Linux en CI, `ClientTests` corre (con
los tests golden salteados si no hay `MURET_CLIENT_GOLDEN_DIR`) y una ventana SDL3 vacía abre y
cierra.

## Preguntas para decidir al empezar

1. **¿Cuándo arranca el cliente respecto del servidor?** La CL0.2 necesita GoogleTest (sub-ola 0.2
   del servidor). Recomendación del roadmap: CL1 después de la ola 1 del servidor.
2. **¿Cómo se saltea un test golden?** GoogleTest tiene `GTEST_SKIP()`. ¿El test se saltea en
   silencio si falta la carpeta, o falla si la carpeta está pero le falta un archivo? (El repo de
   research hace lo segundo: "si no está, se saltea; si está incompleto, falla".)

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

## Sub-ola CL0.2: `ClientLib`, `ClientTests` y la carpeta golden

**Concepto:** biblioteca + ejecutable + tests como tres targets; tests que dependen de datos
externos y cómo se saltean sin mentir.

**Archivos:**
- Modificar: `Client/CMakeLists.txt` (targets `ClientLib`, `ClientTests`; opción `MURET_CLIENT_GOLDEN_DIR`)
- Crear: `Client/tests/` con un test trivial y un test golden de ejemplo que se saltea

- [ ] **Paso 1 (Claude, andamiaje):** `ClientLib` estática, `Main` y `ClientTests` linkeando contra ella; registrar en CTest.
- [ ] **Paso 2 (Claude, andamiaje):** la opción `MURET_CLIENT_GOLDEN_DIR` llega a los tests como definición de compilación o variable de entorno.
- [ ] **Paso 3 (Emanuel, `TODO(human)`):** el helper que da la ruta golden o saltea el test, con la regla que decidiste en la pregunta 2, y su propio test.
- [ ] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R Client
  ```

## Sub-ola CL0.3: SDL3, loader de GL y CI

**Concepto:** dependencias de terceros con vcpkg en modo manifiesto; qué es un loader de OpenGL y
por qué hace falta (las funciones de GL 3.3 no se enlazan, se piden en runtime).

**Archivos:**
- Modificar: `vcpkg.json` (sdl3 y el loader de GL elegido), `Client/CMakeLists.txt`
- Modificar: el workflow de CI de la 0.2 para compilar también `Client/`

- [ ] **Paso 1 (Claude, andamiaje):** `sdl3` y el loader en `vcpkg.json`; `find_package` y link en `ClientLib`.
- [ ] **Paso 2 (Emanuel):** en `main.cpp`, abrir una ventana SDL3 con contexto OpenGL 3.3 core, limpiar la pantalla de un color y cerrar con la X. Sin capas todavía: es el "hola mundo" que la CL3 va a ordenar.
- [ ] **Paso 3 (Emanuel):** imprimir `GL_VERSION` y `GL_RENDERER` al arrancar y anotar qué dio en cada sistema.
- [ ] **Paso 4:** el CI compila `Main` en Windows y Linux (la ventana no se abre en CI).
