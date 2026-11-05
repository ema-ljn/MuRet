# Plan de seguimiento de la segunda auditoría del cliente

> **Para agentes:** usar `superpowers:executing-plans` para ejecutar una tarea autorizada. Los
> pasos se siguen con checkbox. **Quién escribe:** desde el 2026-11-02, Claude escribe el código
> del cliente hasta que Emanuel diga que participa (PROGRESS los marca "resuelto por Claude").
> Para este plan, Emanuel todavía no eligió: lo pidió el 2026-11-05 "para tener más adelante". Las
> tareas no se empiezan sin su pedido. Si toma una, cada tarea trae pistas en tres niveles, y la
> marca pasa a ser suya.

**Objetivo:** cerrar los hallazgos AUD-C05 a AUD-C11 de la auditoría del 2026-11-05 con un test
en rojo y un PR por concepto, manteniendo el contrato 1.02i, Q-C46 y el rol de tutor.

**Arquitectura:** conservar las capas y las dependencias. Ninguna tarea agrega una abstracción
general: cada arreglo reutiliza un patrón que ya está en el repo (los chequeos de `Hello.cpp`, el
aviso único del `Dispatcher`, el guardado del estado de `GlTexture`). T6 y T8 piden una decisión
antes del primer test.

**Stack:** C++20, biblioteca estándar, SDL3, OpenGL 3.3 core, GoogleTest, CTest, CMake, vcpkg,
clang-format 18 y Python para los scripts del research. Sin dependencias nuevas.

**Spec:** [auditoría y evidencia](../../audits/2026-11-05-cliente-robustez-y-oraculos.md),
[roadmap del cliente](../specs/2026-10-30-cliente-design.md) y los planes de las olas
[CL3](2026-11-02-ola-cl3-plataforma-y-bucle.md), [CL5](2026-11-02-ola-cl5-render-2d-y-login.md),
[CL6](2026-11-03-ola-cl6-modelos-y-seleccion.md) y [CL7](2026-11-05-ola-cl7-el-login-completo.md).

## Estado y orden

Es mantenimiento de sub-olas terminadas; no crea otra ola. Los IDs siguen los del
[primer plan](2026-11-03-seguimiento-auditoria-cliente.md): las tareas desde T5 y los refactors
desde R5. Los nombres de tests que aparecen abajo son **propuestos**, no tests ya incorporados.

| Unidad | Hallazgo | Estado | PR propuesto |
| --- | --- | --- | --- |
| DOC | Informe y este plan | Este PR | `docs(client): registrar la segunda auditoría y su seguimiento` |
| T5 | AUD-C05, la respuesta de crear incompleta | Pendiente | `fix(client): rechazar una respuesta de crear incompleta` |
| T6 | AUD-C06, el clic al perder el foco | Pendiente, con una decisión de diseño | `fix(client): perder el foco cancela el clic` |
| T7 | AUD-C07 a AUD-C09, oráculos que no fallan | Pendiente | `test(client): los oráculos fallan con evidencia incompleta` |
| T8 | AUD-C10, el log por paquete | Pendiente, con una decisión sobre el ADR 0006 | `fix(client): un aviso por causa, no por paquete` |
| T9 | AUD-C11, `SDL_Init` sin dueño | Pendiente | `fix(client): crear el dueño de SDL antes de inicializarlo` |
| B1 | AUD-C12 a AUD-C20 | Backlog | Un PR por hallazgo que se decida tomar |
| R5 a R8 | REF-C05 a REF-C08 | Opcionales | Un PR por refactor que se decida tomar |

El orden es el del contraste: primero lo que corrompe un dato o hace una acción que nadie pidió,
después la confiabilidad de los tests, después disponibilidad y caminos excepcionales. Si Emanuel
hace las tareas para aprender, `muret-reviewer` propuso otro orden: T5, T9, T7 y T6, porque T9 es
corto y fija RAII, y T6 cruza dos capas y cambia un test existente.

Ningún checkbox significa que una corrección esté hecha hasta enlazar su PR y registrar su
verificación.

## Restricciones globales

- DataServer excluido. Los cambios van a `main`, una sub-ola y un concepto por PR.
- C++20 sin extensiones; nada de `std::expected`, `std::print` ni otra API de C++23.
- Aplicar `AGENTS.md`, `CODING_RULES.md` y `cpp-pro`. Consultar Context7 antes de fijar el
  comportamiento de una API; para la versión exacta, el header que instaló vcpkg (SDL 3.4.18 al
  2026-11-05).
- Primero el test rojo, después la implementación mínima. No desactivar tests ni cambiar un valor
  esperado para ocultar un defecto. Un test que fijaba el defecto (T6) se cambia con el arreglo, y
  el PR lo dice.
- Conservar el wire, los formatos, los slots, el orden de pases, el consumo de `rand()` y los
  redondeos del research. Rechazar una entrada inválida, liberar en el camino de error y prohibir
  operaciones que nadie usa no cambian el camino válido, pero llevan su test.
- No versionar reproductores, logs, assets, volcados ni rutas locales.
- Cada PR cita la sub-ola, el ID `AUD-Cxx` y la sección del research que fija el contrato.

## Foco de revisión

1. Una respuesta `F3 01` aceptada y cortada en cualquier byte da `TooShort`, y una completa se
   sigue leyendo igual (T5).
2. Perder el foco con un botón capturado no produce un clic, no deja el botón dibujado apretado y
   no impide un clic nuevo (T6).
3. Un archivo de vectores sin su `end`, o con un caso al que le faltan pasos, hace fallar su test
   (T7).
4. Un servidor que repite el mismo paquete fuera de turno deja una línea de log, no una por
   paquete (T8).
5. Si algo falla después de `SDL_Init`, SDL queda cerrado y un `Create` nuevo puede volver a
   intentar (T9).

## T5 — Rechazar una respuesta de crear incompleta (AUD-C05)

**Sub-ola:** CL6.5 A. **Archivos:** `Client/src/Protocol/Character/CharacterCreate.cpp` y, en el
mismo PR, `CharacterList.cpp`; tests en `Client/tests/protocol/test_character_create.cpp` y
`test_character_list.cpp`.

**Contrato:** con resultado 1, los cuatro campos tienen que estar
([04, la selección de personaje](../../../Client/docs/research/04-protocolo.md#la-selección-de-personaje-f3)).
Si falta cualquiera, `ProtocolError::TooShort` y ningún personaje a medias: un resultado 1 sin
nombre agregaría un personaje fantasma a la lista. Un rechazo corto (0, 2 u otro resultado) sigue
siendo válido. La sesión deja un aviso, sigue esperando y la cierra el tope de 30 s, como con un
resultado que el original no atiende (Q-C114).

**Fuera de alcance:** cambiar el contrato de `Core::ByteReader` para que quede en error después
del primer fallo. Lo usan `Data` y `Network`, y es otro concepto.

- [ ] **Paso 1:** escribir `CharacterCreateTest.EveryCutOfAnAcceptedReplyIsTooShort`: la respuesta
  completa de `ReadsTheNewCharacter` (19 bytes), cortada a cada largo de 5 a 18 con el byte de
  tamaño ajustado, da `TooShort` en todos.
- [ ] **Paso 2:** correrlo y registrar la falla. Devuelve un valor en algunos cortes; con
  `-D_GLIBCXX_ASSERTIONS`, la aserción de libstdc++ lo corta antes y el rojo es más claro. ASan y
  UBSan no lo detectan: el `optional` vacío se lee dentro de su propio objeto.
- [ ] **Paso 3:** comprobar todos los campos antes del primer `*`, como `Hello.cpp:36` y
  `ServerList.cpp:90`.
- [ ] **Paso 4:** `CharacterList.cpp:63-70`, el mismo criterio. Agregar
  `CharacterListTest.EveryCutOfTheHeaderIsTooShort`, que hoy pasa: es un test de caracterización y
  el PR no lo presenta como rojo.
- [ ] **Paso 5:** correr las suites de los dos parsers y la de la sesión.
- [ ] **Paso 6:** PR con CL6.5 A, AUD-C05 y la sección 04.

```bash
cmake --build --preset linux-x64-debug --target test_character_create test_character_list test_server_session
ctest --preset linux-x64-debug -R '^(CharacterCreateTest|CharacterListTest|ServerSessionTest)\.' --no-tests=error
```

**Pistas:**

1. Leé `Core/ByteReader.h` y mirá qué `optional` comprueba la línea 59. ¿Alcanza con mirar el
   último?
2. Armá a mano un `F3 01` aceptado con solo 4 bytes después del resultado y seguí la posición del
   lector lectura por lectura. ¿Qué tiene `name` cuando llegás a la línea 63?
3. Como el lector no avanza cuando falla, una lectura del medio puede fallar y la siguiente salir
   bien: `si falta cualquiera → TooShort`, antes de cualquier `*`.

**Pregunta:** ¿por qué `CharacterList` no falla hoy? ¿Y si `notice` fuera de 16 bits?

## T6 — Perder el foco cancela el clic (AUD-C06)

**Sub-olas:** CL3.3 y CL5.5. **Archivos:** `Client/src/Input/InputState.h/.cpp`,
`Client/src/UI/Button.cpp`; tests en `Client/tests/input/test_input_state.cpp`,
`Client/tests/ui/test_button.cpp` y `test_notice_window.cpp`; la fila Q-C46 de
`Client/docs/research/07-quirks-y-bugs.md`.

**Lo que no cambia:** la lógica sigue corriendo sin foco (Q-C46). No se pausa el bucle.

**La decisión.** Las dos opciones necesitan que `Button` suelte la captura. Sin eso, la captura
queda colgada: el botón se dibuja apretado con el mouse arriba, y apretar afuera y soltar encima
cuenta como clic. La diferencia es dónde se dice que el gesto se canceló.

| Opción | Qué hace | A favor | En contra |
| --- | --- | --- | --- |
| A, recomendada por el advisor | `FocusLost` deja el estado como un `InputState` nuevo, salvo la posición del mouse: suelta sin `WasReleased`. `Button` mantiene la captura solo mientras el botón izquierdo está abajo o se soltó en ese paso. | Es lo que hace MuMain (`MouseLButtonPop = false`). El único consumidor de `WasReleased(MouseButton)` es `Button.cpp:81`, y uno futuro (arrastrar ítems en la CL8) queda bien sin hacer nada. | Cambia 6 aserciones de 3 tests y deja sin sentido `FocusLostDoesNotReleaseWhatWasUp`. |
| B, recomendada por Astra 6 ultra | Una señal aparte de cancelación que `Button` consume para soltar la captura. `WasReleased` sigue como hoy. | Conserva los tests `FocusLost*`. | Cada consumidor futuro de `WasReleased` tiene que acordarse de mirar la señal. `WasReleased` sigue informando una liberación que no hubo. |

- [ ] **Paso 1:** elegir A o B y escribir la elección en esta sección, con su porqué.
- [ ] **Paso 2:** escribir los tests en rojo:
  - `ButtonTest.FocusLostCancelsACapturedClick`: apretar encima, correr un paso, perder el foco y
    correr otro. No hay clic, `Pressed()` da falso y el cuadro no es el apretado.
  - `ButtonTest.PressingOutsideAndReleasingOverIsNotAClick`, después de perder el foco.
  - `ButtonTest.ANewClickWorksAfterFocusLost`.
  - `NoticeWindowTest.FocusLostDoesNotAcceptTheDeletionCode`: el aviso `0x5f` con el código
    escrito, OK apretado, foco perdido: el aviso sigue abierto.
- [ ] **Paso 3:** correrlos y registrar la falla.
- [ ] **Paso 4:** la implementación mínima en la capa elegida, más la regla de la captura en
  `Button`.
- [ ] **Paso 5:** con la opción A, actualizar las aserciones de `test_input_state.cpp:107`, `:109`,
  `:111`, `:126`, `:129` y `:141`, y sacar o reescribir `FocusLostDoesNotReleaseWhatWasUp`. El PR
  explica que esos tests fijaban el defecto.
- [ ] **Paso 6:** los comentarios de `InputState.h:32-36` e `InputState.cpp:251-254`, que hoy dicen
  "como MuMain" sin serlo, y la columna MuRet de Q-C46. No hace falta ADR: según
  `docs/adr/README.md`, la fila es el registro.
- [ ] **Paso 7:** correr las suites de input y UI. A mano, en Windows: alt-tab con OK apretado en
  la ventana de la cuenta no entra.

```bash
cmake --build --preset linux-x64-debug --target test_input_state test_button test_notice_window
ctest --preset linux-x64-debug -R '^(InputStateTest|ButtonTest|NoticeWindowTest|LoginWindowTest|SelectionBarTest)\.' --no-tests=error
```

**Pistas:**

1. Apretá OK en el aviso de borrar y hacé alt-tab sin soltar. ¿Qué ve `Button::Step` en el paso
   siguiente?
2. `ReleaseEverything` convierte "perdí el foco" en "se soltó". Para una tecla, soltar no dispara
   nada; para un botón capturado, soltar encima es un clic. ¿Son el mismo hecho?
3. Separá "subió" de "se canceló", elegí la capa y cambiá el test que fija lo viejo.

**Pregunta:** ¿quién tiene que saber la diferencia entre "el botón subió" y "el jugador terminó un
clic": `InputState` o `Button`?

## T7 — Los oráculos fallan con evidencia incompleta (AUD-C07 a AUD-C09)

**Sub-olas:** CL6.2, CL6.3 y CL7.6. **Archivos:** `Client/tests/world/test_particles.cpp`,
`test_leaves.cpp`, `Client/tests/scenes/test_golden_character_drawing.cpp`,
`Client/tests/render/test_golden_skinning_trace.cpp`, los cuatro scripts de
`Client/docs/research/scripts/login/` y su `README.md`.

**Concepto:** un oráculo que no puede fallar no prueba nada. Es un solo concepto en tres lugares;
si el PR crece, se parte en tests y scripts.

**Fuera de alcance:** un helper compartido para leer vectores. Los otros cinco tests ya cuentan
exacto y los lectores de `support/` ya exigen su `end` (`DragonVectors.cpp:128`,
`WorldObjectVectors.cpp:211`): el helper tendría dos usuarios. Un test permanente del propio
oráculo pide mover el lector a `support/`, como `test_cloth.cpp:296`, y va en otro PR.

- [ ] **Paso 1:** el rojo. En la copia de trabajo, cortar `particles.txt` y `leaves.txt` a la mitad
  del último caso, con y sin el `end`, y correr cada test: hoy pasan. Restaurar los archivos con
  `git restore`. La carpeta sale de `MURET_CLIENT_WORLD55_VECTORS_DIR`, fijada al compilar.
- [ ] **Paso 2:** en los dos tests:
  - exigir que el bucle termine en `end`, no en el fin del archivo;
  - comparar el `cases N` del encabezado con los casos leídos;
  - contar exactos los pasos de cada caso: 20 en partículas, 150 en hojas
    (`Client/tests/vectors/world55/README.md`);
  - hacer fallar el test con una línea de tipo desconocido.
- [ ] **Paso 3:** volver a cortar los archivos como en el paso 1: ahora fallan, y los completos
  pasan.
- [ ] **Paso 4:** los comparadores con tolerancia, de la forma `!(d <= tope)`:
  `test_golden_character_drawing.cpp:199-204` y `test_golden_skinning_trace.cpp:121` y `:530`. En
  `:158` y `:563`, el peor error no puede perder un NaN en `std::max`. Verificarlo poniendo un NaN
  a mano en una copia local y viendo que el test falla.
- [ ] **Paso 5:** que `compare_emu.py`, `check_selection.py`, `frustum_reference.py` y
  `raven_reference.py` terminen con `sys.exit(1 if … else 0)`, como `cloth_reference.py`, y que el
  `README.md` de `login/` lo diga. Probar cada uno con una entrada igual y una distinta.
- [ ] **Paso 6, opcional:** el `end` en los otros cinco tests del mismo bucle, un registro de GL
  desconocido que falle en `test_billboard.cpp:79-130`, y el `end` del frame en
  `ClothVectors.cpp:227-236`.
- [ ] **Paso 7:** correr las suites tocadas con las carpetas golden y de vectores configuradas
  (`Client/README.md#tests`), y PR con AUD-C07 a AUD-C09.

```bash
cmake --build --preset linux-x64-debug --target test_particles test_leaves test_golden_character_drawing test_golden_skinning_trace
ctest --preset linux-x64-debug -R '^(ParticlesTest|LeavesTest)\.' --no-tests=error
```

**Pistas:**

1. Cortá `particles.txt` con `head -n` a la mitad del caso 28 y corré el test. ¿Falla?
2. El `while` tiene dos salidas, `end` y el fin del archivo, y después del bucle no sabés por cuál
   saliste. ¿Qué cuenta el test y qué solo acota?
3. Después del bucle, exigir `line == "end"` y los pasos de cada caso.

**Pregunta:** si `make_particle_vectors.py` se cae a la mitad y deja el archivo incompleto, ¿qué
te dice hoy el CI y qué debería decirte?

## T8 — Un aviso por causa, no por paquete (AUD-C10)

**Sub-olas:** CL3.4 y CL4.3 a CL6.5. **Archivos:** `Client/src/App/ServerSession.cpp` y
`Client/src/Network/Connection.cpp`; tests en `Client/tests/app/test_server_session.cpp` y
`Client/tests/network/test_connection.cpp`, con `support/RecordingSink.h`;
`docs/adr/0006-el-log.md`.

**Patrón:** el aviso único del `Dispatcher` (`Protocol/Dispatcher.cpp:52-57` y `:82-88`): avisa la
primera vez y dice que no se repite.

**Fuera de alcance:** cortar la conexión al pasar un umbral, o descartar paquetes que hoy se
despachan. Cambiaría una política observable y pediría un ADR.

- [ ] **Paso 1:** decidir y escribir acá:
  - la clave del aviso único: por mensaje y servidor, o por mensaje y estado, y si se vuelve a
    avisar al cambiar de estado o de conexión;
  - si conviene una línea de resumen con la cantidad descartada;
  - el ADR 0006: corregir su frase "no crece sin fin" (es cierta entre corridas, no dentro de una)
    o agregar un tope durante la corrida, que es otra decisión de retención.
- [ ] **Paso 2:** escribir `ServerSessionTest.ARepeatedOutOfTurnPacketWarnsOnce`: mil saludos del
  ConnectServer fuera de turno dejan una línea de aviso en el `RecordingSink`. Uno equivalente para
  los descartes de `Connection`.
- [ ] **Paso 3:** correrlos y registrar la falla: hoy, una línea por paquete.
- [ ] **Paso 4:** la implementación mínima, siguiendo el patrón del `Dispatcher`. Los demás tests
  de la sesión siguen igual.
- [ ] **Paso 5:** el ADR 0006 según el paso 1, y PR con AUD-C10.

```bash
cmake --build --preset linux-x64-debug --target test_server_session test_connection
ctest --preset linux-x64-debug -R '^(ServerSessionTest|ConnectionTest)\.' --no-tests=error
```

**Pista:** ¿cuántas líneas escribe un servidor que repite el mismo paquete durante un frame, y
cuántas te sirven para entender qué pasó?

## T9 — Crear el dueño de SDL antes de inicializarlo (AUD-C11)

**Sub-ola:** CL3.1. **Archivo:** `Client/src/Platform/SdlContext.cpp`.

**Contrato:** si algo falla después de `SDL_Init`, SDL queda cerrado, `s_alive` en falso y un
`Create` nuevo puede volver a intentar. `SDL_Quit` es seguro después de un error de inicialización
(wiki de SDL3, consultada en Context7).

- [ ] **Paso 1:** razonar el camino de error: un `bad_alloc` de un objeto vacío no se provoca sin
  un mecanismo de inyección, y no vale agregarlo para este test.
- [ ] **Paso 2:** que el dueño exista antes que el recurso: crearlo antes de `SDL_Init`, así su
  destructor deshace todo si algo falla. El `SDL_Quit` a mano de la línea 33 pasa a ser trabajo del
  destructor.
- [ ] **Paso 3, opcional:** un test del camino de error que ya existe: con `SDL_HINT_VIDEO_DRIVER`
  en un driver que no existe, `Create` falla, y después, con el driver `offscreen`, un `Create`
  nuevo anda. Prueba el estado después del fallo, no el `bad_alloc`.
- [ ] **Paso 4:** correr los tests que abren una ventana (`render_gl/`, en Xvfb en el CI) y PR con
  AUD-C11.

**Pistas:**

1. Suponé que el `new` de la línea 38 tira. ¿Quién llama a `SDL_Quit`? ¿Qué devuelve el próximo
   `Create()`?
2. Entre la línea 28 y la 38 ya está adquirido SDL y cambió `s_alive`, pero todavía no existe el
   objeto que los tiene que liberar.
3. Invertí el orden: primero el dueño, después `SDL_Init`.

**Pregunta:** si un `bad_alloc` de un objeto vacío es casi imposible, ¿por qué igual importa el
orden?

## B1 — Backlog (AUD-C12 a AUD-C20)

Cada uno, si se toma, va en su PR con su test.

| ID | Arreglo mínimo |
| --- | --- |
| AUD-C12 | En `GlFramebuffer::ReadPixels`, fijar `GL_PACK_ALIGNMENT` y restaurarlo, como `GlTexture.cpp:146-150` con `GL_UNPACK_ALIGNMENT`. Test en `render_gl/`: con 1, 2, 4 y 8, los mismos píxeles y el estado restaurado. |
| AUD-C13 | Un tope en `ModelTextureSlots::Request` donde nace el slot, que no cambie ninguna asignación válida: el golden de `lor_slots.bin` y de la vista previa tiene que quedar igual. |
| AUD-C14 | Un camino de limpieza por cada adquisición en `hash6b.c` y `cmyk6b.c`, y validar el ancho y el alto antes de abrir la salida. Prueba: un archivo centinela queda intacto con dimensiones inválidas. |
| AUD-C15 | `ExpandEnvironmentStringsA` para `REG_EXPAND_SZ`, probado en Windows nativo. |
| AUD-C16 | Filtrar por ventana antes de aplicar el input en `PumpEvents`. |
| AUD-C17 | Decidir si el parser rechaza el `\r` final o si `Config.h:44` se corrige, y fijarlo con un test. |
| AUD-C18 | `logicSteps` sin signo, o con su vuelta definida, sin cambiar el contador que lee el cifrado de la tela (`Cloth.cpp:459`). |
| AUD-C19 | Rechazar un ancho o un alto no positivo en el helper del lab. |
| AUD-C20 | Actualizar los dos comentarios. |

## R5 a R8 — Refactors opcionales (REF-C05 a REF-C08)

Se toman por separado, cuando haya un beneficio concreto, y sin mezclarlos con una corrección.
REF-C05 y REF-C06 son los más chicos: mover una declaración a su header y borrar en el tipo la
copia y el movimiento que nadie usa (con un test de traits). REF-C07 y REF-C08 piden antes
verificar sus líneas y que simplifiquen a los llamadores; no justifican un AssetManager común.
