# Segunda auditoría del cliente: robustez, recursos y oráculos

**Fecha:** 2026-11-05. **Base revisada:** `e0100e2cd3d01c92723f2ad11b10eebe07665077` (#134).
**Estado:** informe de la revisión; los hallazgos siguen abiertos. Su ejecución se sigue en el
[plan de mantenimiento](../superpowers/plans/2026-11-05-seguimiento-segunda-auditoria-cliente.md).

La [primera auditoría](2026-11-03-cliente-cpp20.md) llegó hasta la CL5.2. Esta cubre el cliente
hasta el cierre del login (CL7.6) y busca otra cosa: datos de red que el parser no valida entero,
interacciones de la UI que terminan en una acción, recursos que quedan sin dueño en un camino de
error y oráculos que dan verde con evidencia incompleta. Se confirmaron un comportamiento
indefinido con bytes del servidor, un clic que el jugador no hizo, dos oráculos que no detectan un
archivo de vectores cortado y una amplificación del log. El resto es latente o de herramientas.

## Alcance y criterio

Se revisaron `Client/src/`, sus tests, `labs/`, las herramientas de `Client/tests/vectors/**/tools`
y de `Client/docs/research/scripts/`, el CMake, el CI y los manifiestos. Se compararon los 5.726
archivos de `Client/assets/Data/` con su manifiesto SHA-256. Del DataServer solo se miró el
andamiaje: todavía no tiene red ni SQL.

La base normativa es la misma de la primera: [AGENTS.md](../../AGENTS.md),
[CODING_RULES.md](../CODING_RULES.md), la skill `cpp-pro`, las pautas de `muret-reviewer`, los
planes, el [roadmap del cliente](../superpowers/specs/2026-10-30-cliente-design.md) y el
[research del cliente](../../Client/docs/research/README.md) como contrato del comportamiento.

La auditoría la hizo **Astra 6** en dos pasadas, con subagentes por área. Después, sus hallazgos
se contrastaron por separado con:

- **Astra 6, esfuerzo ultra**, en el CLI y solo lectura, que corrió 28 tests de los binarios ya
  compilados;
- **Opus 5.5, esfuerzo max**, como advisor, que compiló una prueba propia contra el build;
- un agente de exploración que verificó cada hallazgo en el código y recorrió todos los parsers
  de `Protocol/`;
- `muret-reviewer`, que armó las pistas de tutor según `PROGRESS.md`;
- Context7 (la wiki de SDL3, cppreference y docs.gl), el código de SDL 3.4.18 que instala vcpkg y
  el `Winmain.cpp` de MuMain.

La prioridad final incorpora las objeciones del contraste y las reproducciones; no es la suma de
todas las sospechas. Donde el contraste discrepó de la auditoría, el hallazgo lo dice.

La auditoría no modificó lógica, ejercicios ni `PROGRESS.md`, y no demuestra dominio de ningún
concepto por parte de Emanuel. Todo el código revisado lo escribió Claude como ejemplo trabajado
(PROGRESS lo marca "resuelto por Claude").

## Hallazgos

### AUD-C05 — La respuesta de crear un personaje desreferencia campos que no llegaron

**P2, defecto confirmado: comportamiento indefinido con bytes del servidor. Sub-ola:** CL6.5 A.
**Fuente:** [CharacterCreate.cpp:55](../../Client/src/Protocol/Character/CharacterCreate.cpp#L55).
**Contrato:** [research 04, la selección de personaje](../../Client/docs/research/04-protocolo.md#la-selección-de-personaje-f3):
con resultado 1, el nombre en `pkt[5..14]`, el lugar en `pkt[0xf]`, el nivel en `pkt[0x10..0x11]`
y la clase en `pkt[0x12]`.

`ReadCharacterCreateResult` lee el nombre, el lugar, el nivel y la clase con `Core::ByteReader`,
que no avanza cuando una lectura falla, y comprueba solo la clase antes de desreferenciar los
cuatro. Una lectura del medio puede fallar y la siguiente salir bien: con algunos cortes de una
respuesta aceptada, la clase existe y el nombre o el nivel no. Desreferenciar un `std::optional`
vacío es comportamiento indefinido (cppreference, `optional::operator*`).

Reproducción sintética ejecutada: en Debug, la aserción de libstdc++ corta el proceso; en Release,
`HasValue()` da verdadero con un nombre que no llegó. `AShortReplyIsTooShort` corta justo el byte
de la clase, el único que el código mira, y por eso pasa.

`CharacterList.cpp:63-70` tiene la misma forma (tres lecturas, un chequeo), pero hoy es seguro:
los tres campos miden un byte, así que si uno falla también fallan los siguientes. Dejaría de
serlo si se agrega un campo más ancho antes de uno más angosto. Los demás parsers de `Protocol/`
comprueban cada campo (`Hello.cpp:36`, `ServerList.cpp:90`) o son seguros por construcción.

La auditoría lo puso en P1. El contraste lo bajó a P2 porque ningún GameServer conocido manda esa
respuesta corta, pero coincidió en que va primero.

**Pista 1:** si el lector no avanza cuando falla, ¿qué garantiza que la lectura de la clase salió
bien?

### AUD-C06 — Perder el foco con el botón apretado completa el clic

**P2, defecto confirmado. Sub-olas:** CL3.3 (`Input`) y CL5.5 (`Button`).
**Fuentes:** [InputState.cpp:249](../../Client/src/Input/InputState.cpp#L249) y
[Button.cpp:76](../../Client/src/UI/Button.cpp#L76).
**Contrato:** [research 07, Q-C46](../../Client/docs/research/07-quirks-y-bugs.md) y
[02, sin foco no corre el frame](../../Client/docs/research/02-arranque-y-ciclo-de-vida.md#sin-foco-no-corre-el-frame).

`FocusLost` suelta todo lo que está abajo con su `WasReleased` (`InputState.h:32-36`). `Button`
toma cualquier liberación sobre el botón que capturó como un clic. Si el jugador mantiene apretado
OK y hace alt-tab, el paso siguiente acepta el aviso. Reproducción sintética, sin red: con el aviso
del código de borrado (`0x5f`) y siete dígitos escritos, apretar OK, correr un paso, perder el
foco y correr otro devuelve el aviso aceptado, y la selección pasa a `SendTheDeletion`
(`CharacterSelectScene.cpp:336`). El OK de los avisos de una conexión perdida cierra el juego
(Q-C69).

La liberación la inventa MuRet. SDL 3.4.18 suelta el teclado al perder el foco
(`SDL_ResetKeyboard` manda los `KEY_UP`), pero no el mouse: el reseteo está en un `#if 0` de
`SDL_SetMouseFocus` ([SDL_mouse.c](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/events/SDL_mouse.c)),
porque el sistema captura el mouse mientras hay un botón abajo. Y el comentario de
`InputState.cpp:251-254` dice "como MuMain", pero MuMain hace lo contrario: al perder el foco en
modo ventana pone `MouseLButton` en falso y también `MouseLButtonPop`, el flanco de soltar
([Winmain.cpp, `HandleFocusChange`](https://github.com/sven-n/MuMain/blob/154edfb959e030bc039edfff2730b936f7532506/src/source/App/Platform/Windows/Winmain.cpp#L1218-L1232)).

Sacar el flanco no alcanza. El advisor lo comprobó contra el build: si `Button` no ve la
liberación, la captura queda colgada, el botón se dibuja apretado con el mouse arriba, y apretar
afuera y soltar encima cuenta como clic. `Button.cpp:81` es el único consumidor de
`WasReleased(MouseButton)` en producción. Las dos formas de arreglarlo están en el plan.

No justifica pausar el bucle sin foco: Q-C46 eligió que la lógica siga, y eso no cambia.

La auditoría lo puso en P2; Astra 6 ultra lo subió a P1 porque se dispara con una interacción
normal.

**Pista 1:** ¿en qué se diferencia "el botón subió" de "el jugador terminó un clic"?

### AUD-C07 — Dos oráculos de vectores pasan con el archivo cortado

**P2, confiabilidad de tests confirmada. Sub-olas:** CL7.6 B2, B3 y C2.
**Fuentes:** [test_particles.cpp:110](../../Client/tests/world/test_particles.cpp#L110) y
[test_leaves.cpp:142](../../Client/tests/world/test_leaves.cpp#L142).
**Contrato:** [vectors/world55/README.md](../../Client/tests/vectors/world55/README.md): 28 casos
de 20 pasos en `particles.txt`, 3 casos de 150 pasos en `leaves.txt`.

Los dos leen con `while (std::getline(file, line) && line != "end")` y, al salir, no saben si
llegaron al `end` o al fin del archivo. Nadie cuenta los pasos de cada caso. Las cotas finales son
mínimos (`EXPECT_GT` en `test_particles.cpp:231-233` y `test_leaves.cpp:269`), y la línea
`cases N` del encabezado se lee y se descarta (`test_particles.cpp:86`, `test_leaves.cpp:133`).

Con copias cortadas fuera del repositorio, los dos tests pasaron: partículas con 540 de 560 pasos y
hojas con 300 de 450, los dos sin el `end`. Los archivos versionados están completos; el defecto es
que el oráculo no detectaría que dejaron de estarlo.

Otros cinco tests usan el mismo bucle (`test_character_head`, `test_dark_spirit`,
`test_original_rotation`, `test_billboard` y `test_scepter_glow`), pero cuentan exacto y detectan
un corte; solo les falta exigir el `end`. Dos huecos menores, P3: `test_billboard.cpp:79-130`
ignora en silencio un registro de GL desconocido, y en `ClothVectors.cpp:227-236` el bucle de
eventos de un frame termina en el fin del archivo sin pedir su `end`.

**Pista 1:** si el generador se cae a la mitad, ¿qué te dice hoy el CI?

### AUD-C08 — Comparadores con tolerancia que dejan pasar un NaN

**P3, confiabilidad de tests. Sub-olas:** CL6.2 y CL6.3.
**Fuentes:** [test_golden_character_drawing.cpp:199](../../Client/tests/scenes/test_golden_character_drawing.cpp#L199)
y [test_golden_skinning_trace.cpp:121](../../Client/tests/render/test_golden_skinning_trace.cpp#L121).

Rechazan solo `diferencia > tolerancia`, y cualquier comparación con un NaN es falsa: un vértice
NaN pasa. En `test_golden_skinning_trace.cpp` hay más casos de lo mismo:

- `:530` compara con `> SamePosition`;
- `:158` y `:563` acumulan el peor error con `std::max(worst, d)`, que con un NaN como segundo
  argumento devuelve `worst` y lo descarta.

No se demostró un golden entero alterado que pase: el comparador se leyó, no se mutó. Astra 6
ultra lo subió a P2.

**Pista 1:** ¿qué devuelve `d > tope` cuando `d` es NaN?

### AUD-C09 — Scripts del research que encuentran diferencias y terminan con 0

**P3, herramientas de research. Sub-ola:** CL7.R.
**Fuentes:** `compare_emu.py`, `check_selection.py`, `frustum_reference.py` y `raven_reference.py`,
en [Client/docs/research/scripts/login/](../../Client/docs/research/scripts/login/).

Imprimen "DISTINTO" o "hay diferencias" y terminan con 0. `cloth_reference.py` y
`run_cloth_cases.py` sí terminan con `sys.exit(1 if …)`. Ninguno corre en el CI, y
`login/README.md` documenta leer la salida a ojo. Un script que se encadene con `&&`, como el
ejemplo del README, sigue aunque el anterior encontrara diferencias.

**Pista 1:** ¿qué ve un `&&` cuando el script imprime "DISTINTO"?

### AUD-C10 — Un paquete repetido escribe una línea de log por paquete

**P2, disponibilidad confirmada. Sub-olas:** CL3.4 (el log) y CL4.3 a CL6.5 (la sesión).
**Fuentes:** [ServerSession.cpp:497](../../Client/src/App/ServerSession.cpp#L497) y los demás
avisos de paquetes fuera de turno o ilegibles de ese archivo (desde `:469`),
[Connection.cpp:281](../../Client/src/Network/Connection.cpp#L281) a `:324` y
[FileSink.cpp:63](../../Client/src/Core/FileSink.cpp#L63).
**Contrato:** [ADR 0006](../adr/0006-el-log.md), consecuencias: un log "que no crece sin fin".

Cada paquete fuera de turno o que no se puede leer deja su aviso, sin deduplicar. Un `Poll` lee
hasta 8 bloques de `0x2000` (`Connection.h:54-55`), cada línea se escribe y se vacía con su flush
(`FileSink.cpp:63-64`), y el tope de 1 MiB se mira solo al abrir (`FileSink.cpp:26`).
Reproducción sintética: 21.845 saludos `C1 03 00` en un solo `Poll` dieron 21.844 avisos,
1.660.280 bytes de log y 88 ms de `Poll`.

Alcanza un GameServer propio con un bug que repita un paquete. El patrón para acotarlo ya está en
el repo: el `Dispatcher` avisa una vez por opcode, "y este aviso no se repite"
(`Protocol/Dispatcher.cpp:52-57` y `:82-88`). Cortar la conexión al pasar un umbral cambiaría una
política observable y pediría un ADR.

**Pista 1:** ¿cuántas líneas escribe un servidor que repite el mismo paquete durante un frame?

### AUD-C11 — `SDL_Init` queda sin dueño si falla la creación del contexto

**P3, seguridad ante excepciones. Sub-ola:** CL3.1.
**Fuente:** [SdlContext.cpp:28](../../Client/src/Platform/SdlContext.cpp#L28).

`Create` llama a `SDL_Init` y pone `s_alive` en verdadero antes del `new` que crea al dueño. Si ese
`new` tira, nadie llama a `SDL_Quit` y el próximo `Create` se rechaza. Con un `bad_alloc` inyectado
se observó un `SDL_Init`, ningún `SDL_Quit` y el reintento rechazado. Es un camino excepcional, no
una fuga que crezca por frame. El destructor ya hace lo que la línea 33 hace a mano, y `SDL_Quit`
es seguro después de un error de inicialización (wiki de SDL3).

**Pista 1:** ¿qué parte de la sigla RAII no se cumple entre la línea 28 y la 38?

### AUD-C12 — `ReadPixels` supone el `GL_PACK_ALIGNMENT` por defecto

**P3, robustez latente de la API. Sub-ola:** CL5.1.
**Fuente:** [GlFramebuffer.cpp:79](../../Client/src/Render/Gl/GlFramebuffer.cpp#L79).

Reserva los píxeles compactos y lo dice en un comentario, pero no fija el estado. Con un
framebuffer de 1 × 2 y `GL_PACK_ALIGNMENT` en 8, un valor válido
([docs.gl, glPixelStore](https://docs.gl/gl4/glPixelStore)), ASan detectó una escritura de 4 bytes
fuera del vector de 8. Nadie cambia ese estado y `ReadPixels` solo lo usan los tests de
`render_gl/` (`GlDrawTest.h`). El patrón para fijarlo y restaurarlo ya está en
`GlTexture.cpp:146-150`, con `GL_UNPACK_ALIGNMENT`.

### AUD-C13 — Los slots de modelo pueden entrar en los del texto

**P3, parcial: falta el límite, pero no se alcanza con los datos reales. Sub-ola:** CL6.1 B.
**Fuente:** [ModelTextureSlots.cpp:97](../../Client/src/Render/Models/ModelTextureSlots.cpp#L97).

`Request` devuelve `m_next` y lo incrementa siempre: la guarda protege la tabla de nombres, no el
slot. El texto empieza en 1.845 (`TextureSlot.h:21-32`) y `TextureTable.cpp:22` solo rechaza desde
2.101, así que un slot de modelo entre 1.845 y 2.100 pisaría la caché del texto. La reproducción
arrancó la tabla en 1.844 a mano. Con los datos reales, el rango que empieza más alto es el de la
vista previa, `0x6f8`. Además, el `static_cast<std::uint16_t>` da la vuelta pasado 65.535.

### AUD-C14 — Las herramientas de vectores JPEG pierden recursos en sus errores

**P3, herramientas. Sub-ola:** CL1.4.
**Fuentes:** [hash6b.c:45](../../Client/tests/vectors/jpeg/tools/hash6b.c#L45) y
[cmyk6b.c:27](../../Client/tests/vectors/jpeg/tools/cmyk6b.c#L27).

- `hash6b.c:45-49`: si `fseek` falla, retorna sin `fclose`. En su bucle se acumula: con 64 fallos,
  los descriptores abiertos pasaron de 4 a 68.
- `hash6b.c:91-96`: si falla la salida `-raw`, retorna sin cerrar ni liberar el descompresor, el
  archivo y los píxeles. LeakSanitizer informó 1.736.704 bytes en 640 asignaciones.
- `cmyk6b.c:27-31`: abre la salida con `"wb"` antes de validar el ancho y el alto. Con un ancho de
  0 devuelve error, pero el archivo existente queda vacío: de 26 bytes a 0.
- `cmyk6b.c:44-48`: si `malloc` falla, retorna sin destruir el compresor ni cerrar la salida.

### Observaciones menores

| ID | Observación | Límite práctico |
| --- | --- | --- |
| AUD-C15 | `SystemFonts.cpp:160` acepta `REG_EXPAND_SZ` y usa el texto sin expandir: `%SystemRoot%\…` queda relativo y se le antepone la carpeta Fonts (`:178-182`). | Visto con dobles de la API de Windows; falta Windows nativo. |
| AUD-C16 | `PumpEvents` aplica el input antes de filtrar por ventana (`WindowEvents.cpp:63-67`), contra el contrato de `Window.h:58`. Incluye el `FocusLost`. | Hoy hay una sola ventana. |
| AUD-C17 | `ParseConfig` acepta un `\r` aislado al final del archivo (`Config.cpp:437-444`), contra `Config.h:44`. | No toca el wire. |
| AUD-C18 | `++world.logicSteps` sobre un `std::int32_t` (`Cloth.h:45`, `Cloth.cpp:211`): pasado `INT_MAX` es UB, y UBSan lo confirma. | A 25 pasos por segundo, unos 994 días seguidos. |
| AUD-C19 | `LabTexture.cpp:22-24` y `:44-57`: un ancho negativo pasa la validación y GL da `GL_INVALID_VALUE` sin que nadie lo mire. | Solo el lab; sus llamadores usan tamaños positivos. |
| AUD-C20 | Comentarios viejos: `CharacterSelectScene.h:39-41` dice "sin la vista previa" (ya está desde la CL6.5 B3) y `CharacterDrawing.cpp:558-560` dice que el cuervo "todavía no se dibuja" (ya se dibuja desde la CL7.6 C1). | Legibilidad. |

## Oportunidades opcionales de mejora

| ID | Oportunidad | Alcance recomendado |
| --- | --- | --- |
| REF-C05 | `IModelLibrary` vive en `CharacterDrawing.h:43`. `GameModels.h` y `LoginModels.h` la implementan y, para eso, incluyen todo el header del dibujo. | Un header propio para la interfaz, sin cambiar firmas. |
| REF-C06 | `CharacterSelectScene` crea callbacks con `[this]` en su constructor (`CharacterSelectScene.cpp:105-111`) y no borra la copia ni el movimiento, como sí hace `LoginScene.h:92-93`. La auditoría midió con traits que la escena se puede mover y que `SceneTextures` se puede copiar. | Expresar en el tipo lo que hoy nadie hace. Ningún llamador copia ni mueve: es prevención. |
| REF-C07 | `CreatePreview` carga y suelta assets, y además elige la clase, anima y calcula la cámara. | Separar la carga, conservando los slots `0x6f8..0x716` y el golden de la vista previa. |
| REF-C08 | `GameModels`, `WorldModels` y `CreatePreview` llevan tres mapas paralelos por modelo (modelo, esqueleto y slots de malla). | Una entrada por modelo, conservando el esqueleto opcional y la política de cada cargador. Sin un AssetManager común. |

REF-C07 y REF-C08 vienen de la auditoría y no se volvieron a verificar línea por línea. La
auditoría propuso además deduplicar las transformaciones de la tela y de la rotación, separar la
gramática del INI, compartir conversiones entre la config y la línea de comandos, y usar `pefile`
en las herramientas. No se registran como oportunidades hasta medir su beneficio.

Antes de agregar destructores que suelten slots solos, hay que decidir la copia, la vida del store
y que `Leave` siga siendo idempotente. El `SceneManager` ya hace la limpieza en las transiciones, y
la falta de esos destructores no mostró una fuga.

## Lo que conviene conservar

- Los tests de orden de pases, de slots, de RNG y los vectores numéricos de la tela y los efectos
  son el contrato. No conviene reemplazarlos por mocks que repitan llamadas internas.
- `GameText::PlaceArguments` no es un `printf` a medias: conserva todo salvo `%s` y `%d`, y
  `std::format` cambiaría la semántica. `TextField` es ASCII y Latin-1 a propósito.
- Las fórmulas en float de la x87 y el `rand()` de la CRT no se cambian por las de glm o
  `<random>` por elegancia: cambian bits y cantidad de sorteos. glm sigue para las matrices
  comunes ya cubiertas.
- No se encontró justificación para un ECS, un AssetManager universal ni otro framework de UI.
  D-C2 (el loader propio) y D-C12 (FreeType sin SDL_ttf) siguen vigentes.

## Descartes

- **LeakSanitizer y EGL.** Informa 128 bytes en la apertura mínima de EGL, igual sin recursos de
  render de MuRet que después de 500 ciclos de crear, mover y destruir objetos de GL. No crece y no
  se atribuye a MuRet.
- **Escenas.** El `SceneManager` sale de la escena antes de destruirla y llama a `Leave` aunque
  `Enter` falle. Entrar dos veces a la misma instancia no es un camino del manager.
- **Dibujo de personajes.** Los punteros locales de `CharacterDrawing.cpp` viven durante todos los
  pases, y `IndicesInRange` protege los índices antes de armar los triángulos.
- **Parsers.** Fuera de AUD-C05, los `*` sin chequeo de `Protocol/` van sobre spans de tamaño fijo
  o después de comparar la cantidad con lo que queda.
- **Contratos del original.** `Clear` de las partículas conserva el viento, las hojas conservan sus
  ángulos y crear con todas las clases deshabilitadas deja la clase 1. Están documentados, no son
  estado olvidado.

## Verificación ejecutada

Informada por la auditoría, sobre la base indicada, con GCC, C++20, warnings como errores, sin PCH
y con `-fsanitize=address,undefined`:

| Verificación | Resultado observado |
| --- | --- |
| Suite del cliente sin restricciones (Debug) | 2.231 casos: 2.164 aprobados, 67 salteados por fixtures externos, cero fallos. |
| Suite con ASan, UBSan y LSan, sin `gl` | 2.148 casos: 2.093 aprobados, 55 salteados, cero fallos. |
| Suite `gl` con sanitizers | 83 aprobados, sin salteos. |
| Headers incluidos de a uno, sin PCH | 233, sin errores. |
| Labs | 154 casos aprobados. |
| Protocolo y red | Más de cuatro millones de comprobaciones generadas de límites, cifrado y fragmentación. |
| Sesión | 18 estados y 105.202 casos, sin reportes de LSan. |
| Sockets | 1.000 ciclos; los descriptores, de 4 a 4. |
| Fallos parciales de `Data` | 1.000 ciclos; la memoria viva y los descriptores no cambiaron. |
| Caché de texto | 25.000 frames y fallos después de 0, 1 y 2 subidas; los slots quedan equilibrados. |
| FreeType | 100 aperturas y 100 fallos parciales, sin reportes de LSan. |
| Escenas y objetos de GL | 400 ciclos de escenas, 11 puntos de fallo al subir el mundo y 500 ciclos de objetos de GL, con todo liberado. |

En el contraste, Astra 6 ultra corrió 28 tests de los binarios existentes, y el advisor compiló la
prueba de la captura colgada de AUD-C06 contra el build.

Los hallazgos se reprodujeron con datos sintéticos fuera del repositorio. Esos reproductores no
son tests de regresión del proyecto, y la suite en verde no cierra ninguno.

### Límites de la validación

- No se ejecutaron Windows nativo, el binario original ni el login contra la VM del laboratorio.
  La fidelidad se contrastó con el research y los fixtures existentes.
- El índice de cobertura de la auditoría (417 fuentes y headers del cliente, 72 archivos de labs,
  44 herramientas) es un mapa de lo leído, no una garantía de revisión línea por línea.
- Un rechazo automático cortó la última revisión manual de los cuerpos de tests de escenas y
  render, y de algunos helpers compartidos. La suite se ejecutó, pero no reemplaza esa lectura.

No se versionan logs de ejecución, reproductores, capturas, volcados, assets ni rutas de esta
máquina.

### Repetir los checks

El flujo normal sigue siendo el de `AGENTS.md`. Para una revisión separada en Linux, con
`VCPKG_ROOT` configurado:

```bash
cmake -S . -B build/client-audit -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Debug -DMURET_WARNINGS_AS_ERRORS=ON \
  -DMURET_PRECOMPILED_HEADERS=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/client-audit --target Client/all
ctest --test-dir build/client-audit/Client --output-on-failure -LE '^gl$'
```

Para GL, el entorno de Xvfb del CI. Una excepción local de EGL no se convierte en una
desactivación del CI.

## Seguimiento

El [plan de mantenimiento](../superpowers/plans/2026-11-05-seguimiento-segunda-auditoria-cliente.md)
lleva las tareas, sus checks de aceptación y las pistas. Este informe conserva la evidencia de la
base auditada; los PR que cierren hallazgos se enlazan desde el plan.
