# Plan de seguimiento de la auditoría del cliente

> **Para agentes:** usar `superpowers:executing-plans` para ejecutar una tarea autorizada, o
> `superpowers:subagent-driven-development` si Emanuel elige ejecución delegada. Los pasos se
> siguen con checkbox. **Quién escribe:** desde el 2026-11-02, Claude escribe el código del
> cliente, también los pasos `(Emanuel)` y `TODO(human)`, hasta que Emanuel diga que participa
> (PROGRESS los marca "resuelto por Claude"). Para este plan, Emanuel eligió el 2026-11-03 que
> Claude haga T3 ya, antes de que entre la CL5.4, y T2 y T4 después. Los ADR 0001 y 0002 del
> camino de aprendizaje siguen siendo de Emanuel.

**Objetivo:** cerrar los hallazgos de la auditoría del 2026-11-03 con una regresión y un PR por
concepto, manteniendo el contrato 1.02i y el rol de tutor.

**Arquitectura:** conservar las capas y dependencias actuales. El primer arreglo mantiene las
interfaces; la comunicación de errores de E/S requiere una mini-spec previa, a cargo de
Emanuel. Los refactors opcionales se separan de las correcciones funcionales.

**Stack:** C++20, biblioteca estándar, SDL3, OpenGL 3.3 core, GoogleTest, CTest, CMake, vcpkg y
clang-format 18. Sin nuevas dependencias previstas.

**Spec:** [auditoría y evidencia](../../audits/2026-11-03-cliente-cpp20.md),
[roadmap del cliente](../specs/2026-10-30-cliente-design.md) y contratos de las
[olas CL3](2026-11-02-ola-cl3-plataforma-y-bucle.md) y
[CL5](2026-11-02-ola-cl5-render-2d-y-login.md).

## Estado y orden

Este es mantenimiento de CL3.4 y CL5.1; no crea otra ola ni reabre las sub-olas terminadas.
Los nombres de tests nuevos que aparecen abajo son **propuestos**, no tests ya incorporados.

| Unidad | Hallazgo | Estado | PR propuesto |
| --- | --- | --- | --- |
| DOC | Informe y este plan | En `main` por el #77 | `docs(client): registrar la auditoría y su seguimiento` |
| T1 | AUD-C01, escritura append entre sinks | Hecha en el #78, con su test rojo y verde | `fix(client): conservar las líneas de otros sinks tras vaciar el log` |
| T2 | AUD-C02, fallo de escritura observable | Hecha en el #82 | `fix(client): informar los fallos de escritura del log` |
| T3 | AUD-C03, fallo del driver al subir | Hecha en el #79 | `fix(client): rechazar una subida de textura que falla en GL` |
| T4 | AUD-C04, registro de decisiones | AUD-C04 cerrado: el índice y las aclaraciones en el #83, y los ADR 0003 a 0008 del inventario, uno por PR. Falta lo de Emanuel, los 0001 y 0002, que no son de la auditoría | `docs(client): ordenar las decisiones de compatibilidad pendientes` |
| R1 a R4 | REF-C01 a REF-C04 | Opcionales, por separado | Un PR por refactor que se decida tomar |

T1 está en el #78, T3 en el #79 y T2 en el #82. T3 entró antes de que la CL5.4 conectara la tabla de
texturas con las escenas; la caché del texto de la CL5.3 ya subía por `GlTexture`. El índice de T4
está en el #83, y los ADR del inventario cierran AUD-C04. Ningún checkbox de esta lista
significa que una corrección esté hecha hasta enlazar su PR y registrar su verificación.

## Restricciones globales

- DataServer excluido. Los cambios del cliente van a `main`, una sub-ola y un concepto por PR.
- C++20 sin extensiones; no `std::expected`, `std::print` ni otras APIs de C++23.
- Aplicar `AGENTS.md`, `CODING_RULES.md` y `cpp-pro`. Consultar Context7 antes de fijar un
  comportamiento de API; para la versión exacta, contrastar el header instalado por vcpkg.
- Primero el test rojo, después la implementación mínima, después refactor. No desactivar
  tests ni cambiar el valor esperado para ocultar el defecto.
- Conservar el wire, los formatos, las constantes del build y los redondeos del research.
  Una diferencia deliberada se registra con ADR y test; una corrección de MuRet que restaura
  su contrato no implica reproducir un bug interno del original.
- No versionar assets, claves, cable, volcados, caches, resultados generados ni rutas locales.
- Los PR citan CL3.4 o CL5.1, el ID `AUD-Cxx` y la sección del research. Las APIs y firmas
  indicadas abajo describen el alcance del plan; su fuente de verdad sigue siendo el código.
- Claude escribe los pasos marcados `(Emanuel)` o `TODO(human)` mientras siga la decisión del
  2026-11-02, y los marca como resueltos por Claude. Si Emanuel retoma una tarea, la marca vuelve
  a ser suya.

## Foco de revisión

1. Un sink que vació un archivo y otro que agrega: ninguna línea posterior a abrir B se pisa
   (T1, `AppendsFromIndependentSinksAfterTheCap`).
2. Un archivo exactamente en el tope no se vacía; uno mayor se vacía solo según la política
   existente (T1, `KeepsAFileExactlyAtTheCap` y `EmptiesAFileOverTheCap`).
3. La apertura funciona pero una escritura falla: la pérdida de persistencia se observa sin
   reentrar al logger ni atascar el cierre (T2, `AWriteFailureCanBeObserved`).
4. GL rechaza la imagen de un slot ya ocupado: se informa el slot y se conserva su textura
   anterior (T3, `ADriverFailureKeepsThePreviousTexture`).
5. Ya había un error de GL antes de la llamada: no se lo atribuye a la nueva subida ni se
   publica un éxito sin explicar la frontera de errores (T3, `ReportsAPreexistingGlError`).

## T1 — Append después de vaciar el log (#78)

**Archivos:** modificar `Client/src/Core/FileSink.cpp`; test en
`Client/tests/core/test_file_sink.cpp`. Revisar los comentarios de `FileSink.h` y Q-C48 solo
si atribuyen a los tests una garantía que no verifican.

**Interfaces:** consume `FileSink::Open(const std::filesystem::path&)` y
`FileSink::Write(LogLevel, std::string_view)`; produce el mismo contrato público. No agrega
rotación, locks entre procesos ni otra biblioteca de log.

- [x] **Paso 1 (hecho en el #78):** escribir
  `FileSinkTest.AppendsFromIndependentSinksAfterTheCap`. A hace crecer el archivo por encima
  de `MaxLogFileBytes`; B abre y vacía. B escribe `BBB`, A `AAAA`, B `CCC`. Afirmar contenido
  exacto `BBB\nAAAA\nCCC\n` y ambos sinks todavía abiertos al leer.
- [x] **Paso 2 (hecho en el #78):** compilar el test y correrlo con `--no-tests=error`. Debe fallar
  por contenido sobrescrito, como en AUD-C01; registrar la salida antes de cambiar producción.
- [x] **Paso 3 (hecho en el #78):** corregir exclusivamente el modo de escritura que
  queda después del vaciado. Mantener el tope de 1 MiB y la política de vaciar al abrir.
- [x] **Paso 4 (hecho en el #78):** agregar `FileSinkTest.KeepsAFileExactlyAtTheCap` y conservar los
  tests de append, vaciado y dos hilos. Verificar el borde exacto y el caso mayor al tope.
- [x] **Paso 5:** ejecutar la suite `FileSinkTest` en Linux y Windows. Dos sinks en un hilo
  prueban este defecto; no presentarlos como prueba de atomicidad entre procesos.
- [x] **Paso 6:** revisar como tutor, enlazar el PR y hacer un commit Angular en castellano,
  con el porqué en el cuerpo. Cerrar AUD-C01 cuando su regresión pase.

Checks previstos desde la raíz:

```bash
cmake --build --preset linux-x64-debug --target test_file_sink
ctest --preset linux-x64-debug -R '^FileSinkTest\.' --no-tests=error
```

**Hecho así (#78):** al vaciar, `Open` abre con `trunc`, cierra y vuelve a abrir con `app`; si
algo falla, cae en el error de siempre, que nombra la ruta. `AppendsFromIndependentSinksAfterTheCap`
fallaba con el `FileSink` de antes (`"BBB\nCCC\n\n"`) y `KeepsAFileExactlyAtTheCap` fija el borde
(`>` y no `>=`). El CI pasó en Linux y en Windows. La fila Q-C48 de 07 cita el test de dos sinks
desde el #82.

**Límite:** que B vacíe mensajes anteriores de A es la tensión de retención señalada en el
informe. T1 no cambia esa decisión; su aclaración corresponde a T4.

## T2 — Fallo de escritura observable (#82)

**Archivos:** `Client/src/Core/FileSink.h/.cpp`,
`Client/tests/core/test_file_sink.cpp`; según la mini-spec, el punto de notificación en
`Client/src/main.cpp` y sus tests. Ampliar a `ILogSink`, `TeeSink` o `StderrSink` solo si el
contrato elegido lo necesita y queda justificado.

**Interfaces actuales:** `ILogSink::Write(LogLevel, std::string_view)` devuelve `void`.
`FileSink::Open` informa únicamente errores de apertura. No hay hoy una interfaz de fallo
posterior: esta tarea no está lista para implementación automática.

- [x] **Paso 1 (resuelto por Claude):** escribir una mini-spec breve en esta sección con la
  firma exacta de la señal de error, quién la observa y dónde se informa una sola vez. Pesar
  devolución hasta el dueño frente a consulta de estado fuera de `Write`; justificar la
  opción con menor cambio de llamadores. Definir también si el fallo queda latente hasta
  abrir otro sink o si existe recuperación. No introducir reintentos implícitos.
- [x] **Paso 2 (resuelto por Claude):** una vez fijada esa interfaz, escribir
  `FileSinkTest.AWriteFailureCanBeObserved`: abrir correctamente, provocar fallo posterior y
  comprobar la señal. En Linux `/dev/full` sirve para el caso real; el equivalente portable
  necesita el punto de prueba definido en la mini-spec. No cambiar la firma solo para el test.
- [x] **Paso 3 (resuelto por Claude):** agregar cobertura de un stream que sigue recibiendo mensajes
  tras fallar, notificación única y cierre sin bloqueo. Comprobar que `stderr` conserva los mensajes
  y que ningún `Write` vuelve a entrar al logger.
- [x] **Paso 4:** correr los tests nuevos y registrar el fallo rojo antes del arreglo.
- [x] **Paso 5 (resuelto por Claude):** implementar el contrato elegido, actualizar el
  comportamiento operativo en `Client/README.md` y verificar las suites afectadas.
- [x] **Paso 6:** revisar lifetime y orden de locks, correr Linux/Windows y abrir el PR de T2
  separado de T1. Cerrar AUD-C02 con sus evidencias.

Check previsto, más los tests de integración que defina la mini-spec:

```bash
ctest --preset linux-x64-debug -R '^(FileSinkTest|TeeSinkTest|LogTest)\.' --no-tests=error
```

El caso `/dev/full` por sí solo no certifica el comportamiento de Windows.

**Hecho así (#82), la mini-spec:**

- **La señal:** una línea `[warning] no se pudo escribir en el archivo de log <ruta>: el log sigue
  solo en stderr`, que `FileSink` escribe con un `fwrite` en la primera escritura que falla. No pasa
  por el `Log`: desde un sink, el `Log` volvería a entrar al `TeeSink`, que tiene su mutex tomado.
  El aviso se arma al abrir.
- **Quién la observa:** la persona que mira `stderr`, donde ya van todas las líneas por el
  `TeeSink`. Se eligió esto y no devolver el fallo hasta el dueño ni una consulta de estado: no
  cambia `ILogSink` ni a ningún llamador, y `main.cpp` no tiene nada que hacer con el fallo.
- **La firma:** `FileSink::Open(path, std::FILE* noticeOutput = stderr)`, como el `std::FILE*` de
  `StderrSink`. Los llamadores no cambian; los tests le pasan un `tmpfile`.
- **Después del fallo:** latente hasta abrir otro sink, sin reintentos. `FileSink` cierra el
  archivo en el momento, y las escrituras siguientes no hacen nada. Abierto, lo que no se pudo
  escribir seguía en el buffer del `ofstream` de libstdc++, y su destructor lo mandaba al salir,
  cuando quizás ya había lugar. Un error que el sistema da recién al cerrar no se ve.
- **Los tests:** `AWriteFailureCanBeObserved`, `ReportsAWriteFailureOnce` y
  `StderrKeepsTheLinesAfterTheFileFails`, con `/dev/full`, fallaban sin el arreglo. El de stderr
  arma el `TeeSink` de `main` y ve que llegan todas las líneas y un solo aviso, por su lado.
  `WritesNothingAfterAFailure` pone un tope al tamaño de los archivos del proceso (`RLIMIT_FSIZE`),
  hace fallar una línea, saca el tope y escribe otra: el archivo queda como estaba, también después
  de cerrar. Sin el cierre fallaba: la línea que había fallado llegaba al destruir el sink. En
  Windows esos cuatro se saltean, porque no tiene un dispositivo como `/dev/full` ni
  `RLIMIT_FSIZE`; `AWorkingFileGivesNoNotice` corre en los dos. Mutantes muertos: sin cerrar (avisa
  en cada línea, y la que falló llega al cerrar), `clear` en vez de cerrar, sin la salida temprana,
  nunca avisar, avisar siempre, el aviso a `stderr` y no a la salida que se pasa, y sin `flush`.

## T3 — Publicar la textura solo si la subida funcionó (Claude)

**Archivos:** `Client/src/Render/Gl/GlTexture.h/.cpp`,
`Client/src/Render/Gl/TextureTable.cpp`,
`Client/tests/render_gl/test_gl_texture.cpp` y `test_texture_table.cpp`.
La tabla de funciones ya tiene `getError`; no sumar otro loader ni una dependencia.

**Interfaces:** conservar `GlTexture::Upload(const Platform::GlFunctions&,
const Data::Textures::Texture&, Data::Textures::Sampling)` con
`Core::Expected<GlTexture, std::string>`, y `TextureTable::Load` con su error que nombra el slot.

- [x] **Paso 1 (resuelto por Claude):** confirmar la frontera de errores con la referencia
  de GL y Context7. Propuesta del plan: un error ya pendiente se informa como previo y la
  nueva subida no se publica; uno de la subida devuelve error y no reemplaza el slot.
  Dejar esa precondición y sus efectos sobre el flag escritos en `GlTexture.h`. No prometer
  recuperación del contexto tras `GL_OUT_OF_MEMORY`.
- [x] **Paso 2 (resuelto por Claude):** escribir `GlTextureTest.RejectsADriverFailure`: consultar
  `GL_MAX_TEXTURE_SIZE = M`, pasar RGBA `(M + 1) × 1` con buffer completo y esperar error.
  No usar `TextureSide`, que limita a 512, ni provocar falta de memoria real.
- [x] **Paso 3 (resuelto por Claude):** escribir
  `TextureTableTest.ADriverFailureKeepsThePreviousTexture`: cargar el slot `0x1a`, guardar su
  nombre, intentar reemplazarlo con esa textura y afirmar error con `0x1a` y nombre anterior
  conservado. Agregar `GlTextureTest.ReportsAPreexistingGlError` según el paso 1.
- [x] **Paso 4:** correr los tests en contexto real, con `MURET_CLIENT_REQUIRE_GL=1`, y
  registrar el rojo. Conservar `GlTextureRulesTest`: un buffer inválido sigue rechazándose
  antes de llamar a GL y debe poder probarse con la tabla de funciones vacía.
- [x] **Paso 5 (resuelto por Claude):** implementar el manejo mínimo de fallo y comprobar
  que el nombre nuevo se libera por RAII, el alignment se restaura y el slot anterior queda.
- [x] **Paso 6:** correr los tests de GL y el resto del cliente. Abrir el PR de CL5.1 con el
  alcance de AUD-C03: robustez de la API, sin afirmar que los assets válidos exceden el límite.

Check previsto en Linux, como el CI:

```bash
cmake --build --preset linux-x64-debug --target test_gl_texture test_texture_table
MURET_CLIENT_REQUIRE_GL=1 xvfb-run -a ctest --preset linux-x64-debug -L '^gl$' --no-tests=error
```

La limitación de LeakSanitizer/EGL de la auditoría es evidencia pendiente. No trasladar
`detect_leaks=0` al CI como parte de esta corrección.

**Hecho así (#79):** el paso 1 quedó como proponía el plan, verificado con la referencia de
`glGetError` en Context7. GL puede tener varias banderas y da una por llamada, así que `Upload`
las saca en un bucle con un tope de 16. Un error que ya estaba antes de subir corta sin subir y
se nombra como de otra llamada. Los de la subida se leen después de su última llamada a GL
(`glTexImage2D`, el alignment y los `glTexParameteri`); con uno, se borra el nombre nuevo,
`GL_TEXTURE_2D` queda en 0 y el slot conserva su textura. Los tres tests del plan fallaban antes
del arreglo. La revisión sumó `GlTextureDriverTest`, con un driver falso sin contexto: fija el
bucle, el primer error, el tope y un error de `glTexParameteri`, y corre también en Windows, donde
el CI no tiene GL. `TextureOverTheGlLimit` (`support/GlTestContext.h`) arma la textura que el
driver rechaza. Los nombres de los errores van a mano: `gluErrorString` es de GLU, que no está en
el perfil core, y `KHR_debug` pide GL 4.3.

## T4 — Trazabilidad de decisiones (#83, y los ADR después)

**Archivos:** crear `docs/adr/README.md` como índice de pendientes; revisar la introducción de
`docs/superpowers/specs/2026-10-30-cliente-design.md`, y los comentarios de Q-C48 y `FileSink.h`.
Los ADR individuales se planifican después de comprobar los números reservados.

- [x] **Paso 1 (resuelto por Claude):** releer la actividad de ADR de la ola 0.3 y confirmar qué
  cubren los ADR 0001 y 0002 pendientes. No reasignar sus números ni dar por resuelto el ejercicio.
- [x] **Paso 2 (resuelto por Claude):** preparar el índice con columnas de decisión, research, test,
  responsable y estado: serial/rearmado (Q-C04/Q-C10), JPEG (Q-C33), pacing (Q-C44), log
  (Q-C48), sesión (Q-C53/Q-C56) y wrap (Q-C73). Enlazar la evidencia existente sin copiarla.
- [x] **Paso 3 (resuelto por Claude):** aclarar «fiel» en la introducción del roadmap frente
  a D-C4. Mantener `Decoupled` salvo una decisión explícita nueva, con su ADR y tests.
- [x] **Paso 4 (resuelto por Claude):** resolver por escrito el límite de retención de Q-C48 entre
  varias instancias. Corregir las garantías de la documentación y qué prueba cada test; si se desea
  cambiar la política de vaciado, abrir otra mini-spec y PR funcional, separado de T1.
- [x] **Paso 5:** revisar los enlaces y abrir el PR documental del índice y las aclaraciones.
  Cerrar AUD-C04 solo cuando los ADR exigidos existan: el índice de pendientes no los reemplaza.
- [x] **Paso 6 (resuelto por Claude):** completar los ADR del inventario, un concepto por PR, con
  original, decisión de MuRet, motivo y tests. Actualizar el índice con los enlaces y recién
  entonces registrar el cierre de AUD-C04 en este plan.
  - [ ] **Los 0001 y 0002 (Emanuel):** el paso los nombraba, pero son del servidor y del camino de
    aprendizaje, no de AUD-C04 (la auditoría dejó afuera el DataServer). Siguen pendientes.

**Hecho así (#83):** el paso 1 salió del [plan de la ola 0](2026-10-26-ola-0-fundaciones.md),
pasos 2 y 3: el 0001 es el contrato wire y SQL del servidor, y el 0002, IOCP y epoll a mano; los
dos son del servidor y quedan reservados, sin reasignar. El índice está en
[`docs/adr/README.md`](../../adr/README.md), con el inventario de la auditoría y los Q-C de diseño
que quedan sin decidir si llevan ADR. «Fiel» quedó aclarado en la introducción del roadmap: por
defecto queda lo observable del original, y cuántos frames se dibujan no es parte de eso, así que el
ritmo por defecto sigue siendo `Decoupled`; lo que ese modo cambia en un frame largo, el tope
de 1 s, queda nombrado con su Q-C44. El límite de retención de Q-C48 quedó escrito en
`FileSink.h`, en la fila de 07 y en el README: un segundo cliente que abre pasado el tope vacía lo
que el primero ya había escrito, lo que escribe después queda, y la política no cambia. La fila
dice además qué prueba cada test de `FileSink`: ninguno prueba dos procesos a la vez.

**Hecho así (paso 6):** un ADR por PR, apilados: 0003 el serial y el rearmado, 0004 JPEG con
libjpeg-turbo, 0005 el ritmo, 0006 el log, 0007 el keepalive y las esperas, y 0008 el wrap. Cada
uno enlaza su fila del 07, y la fila lo enlaza a él. La fila de Q-C56 decía "cada espera
`Waiting*`", y desde la CL5.5 `ChoosingServer` y `WaitingCredentials` no tienen tope: el 0007 la
corrigió. Cada ADR se revisó contra su fila, el research, el código y los tests de `main`, y se
comprobaron sus enlaces. La revisión corrigió también tres filas del 07 que decían algo viejo o de
más: Q-C04 y Q-C44 anunciaban un aviso en el log que ya existe, y Q-C73 hacía más transparente el
borde de toda textura, cuando el color del borde solo cambia el alfa de una que lo tiene. Con el
0008, AUD-C04 queda cerrado. Los 0001 y 0002 son ejercicios de Emanuel en el camino de aprendizaje,
del servidor: no son parte de AUD-C04, y siguen pendientes en el índice. Queda abierto, en el
índice, qué otras filas de diseño del 07 merecen un ADR propio.

Cada ADR posterior registra un concepto y su porqué. Esta tarea no autoriza al tutor a escribir
la decisión que corresponde a Emanuel ni modifica el research del original para acomodar MuRet.

## Refactors opcionales

Tomarlos por separado, con un beneficio visible al tocar ese límite. No son prerequisitos de T1.

| Unidad | Archivos previstos | Condición de aceptación |
| --- | --- | --- |
| R1, ASCII común | Crear `Client/src/Core/Ascii.h/.cpp` y `Client/tests/core/test_ascii.cpp`; cambiar `Config.cpp`, `CommandLine.cpp`, `DataPath.cpp` | Una comparación ASCII compartida; bytes `>= 0x80` intactos. Pasan config, CLI y rutas, incluyendo sus distintas políticas de mayúsculas. |
| R2, texto de filename | `Client/src/Data/DataPath.cpp` | Reutilizar `Core::PathText(path.filename())`; tests de rutas ASCII y UTF-8 sin cambio. No reemplazar la conversión de entrada por otra que valida distinto. |
| R3, fases de config | `Client/src/App/Config.cpp` y `Client/tests/app/test_config.cpp` | Separar lectura, precedencia y combinaciones; sin cambiar primer error, rutas relativas ni defaults. Si hace falta otro archivo, elegirlo por responsabilidad en una mini-spec previa. |
| R4, dato de claves | Crear `Client/src/Network/ConnectionKeys.h`; ajustar `Connection.h` y `App/ConnectionKeyFiles.h` | Consumir las claves sin incluir toda la conexión; mismas firmas y tests. Evaluar al tocar ese código. |

Para cada unidad elegida: fijar el contrato, ejecutar la cobertura existente antes y después,
agregar un caso nuevo solo si cubre comportamiento no fijado, revisar el diff y abrir su PR.
No sumar un framework de strings, parser genérico ni otra capa para hacer estas extracciones.

## Verificación y cierre por PR

- [ ] Registrar el test rojo del defecto y el verde con la corrección; conservar regresiones.
- [ ] Compilar con warnings como errores y probar sin PCH para no ocultar includes faltantes.
- [ ] Correr las suites afectadas y el CI de Linux/Windows; las limitaciones se escriben.
- [ ] Ejecutar clang-format 18 sobre lo cambiado y `git diff --check`.
- [ ] Si cambia lectura de datos o una dependencia, correr los golden con el corpus necesario;
  los salteos no se cuentan como aprobados.
- [ ] Revisar como `muret-reviewer`: contrato, corrección/recursos, concurrencia, tests y estilo.
- [ ] Anotar aquí el PR, commit y resultado, y conservar el informe histórico.
- [ ] Actualizar aprendizaje solo después de la explicación de Emanuel, con el nivel que
  demostró (usar, explicar, romper o transferir).

La evidencia ya ejecutada está en el informe. Los comandos y tests de este plan son checks
**previstos** hasta que cada PR los ejecute. Preparar el plan no inicia la implementación.
