# CL5.0: el pipeline gráfico, resuelta

Ejemplo trabajado de la [guía de la CL5.0](../../docs/learning/ola-cl5.0-el-pipeline-grafico.md).
Lo resolvió Claude porque Emanuel pidió, el 2026-11-02, que Claude escriba toda la CL5 hasta que él
diga que participa (la pregunta 1 del plan). Las salidas son reales, con GCC 13.3 y Clang 18 en
Linux, y con GCC y `-fsanitize=address,undefined`; las de GL, con Mesa 25.2.8 (llvmpipe) en Xvfb,
que da un contexto "4.5 (Core Profile)". Windows y MSVC, sin probar acá: el CI compila todo y corre
lo que no abre GL, y en su runner de Windows no hay OpenGL 3.3 (solo el 1.1 de GDI), así que los
tests de `gl/` que abren GL se saltean ahí.

Para cerrar la CL5.0 en el [cuaderno](../../docs/learning/PROGRESS.md) falta lo tuyo, cuando
entres: contar con tus palabras las "Preguntas para chequearte" de la guía y completar su sección
final.

| Archivo | Qué es |
| --- | --- |
| `OriginalQuad.h` / `.cpp` | la misión de la actividad 4: los cuatro vértices de `0x621b60`, con `ScaleX` y `ScaleY` de la 5 |
| `Ortho.h` / `.cpp` | la misión de la actividad 6: la matriz de `gluOrtho2D`, en column-major |
| `Fan.h` / `.cpp` | la misión de la actividad 8: de un fan a sus triángulos |
| `test_*.cpp` | sus tests, de GoogleTest |
| `jefe/test_original_pipeline.cpp` | el jefe: el pipeline del original contra el de MuRet, sin GL, y el viewport de `0x620db0` |
| `gl/` | lo de Claude (actividad 11): la ventana escondida, la tabla de GL del lab, los nombres de GL con dueño, la textura, el framebuffer, el shader del 2D y sus tests |

Las tres misiones van en la biblioteca `MuRetLabsCl50`, sin GL, y sus tests y el jefe en
`LabCl50Tests`, con el prefijo `LabCl50.` en CTest: son 20. Lo de `gl/` va en `MuRetLabsCl50Gl`,
que enlaza `ClientPlatform`, y sus tests en `LabCl50GlTests`, con el prefijo `LabCl50Gl.` y la
etiqueta `gl`: son 11, y 4 de ellos abren GL. Sin pantalla esos 4 se saltean, y con
`MURET_CLIENT_REQUIRE_GL=1` fallan (la pregunta 9 del plan); el CI los corre en Xvfb con la
variable:

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug -R "LabCl50"
MURET_CLIENT_REQUIRE_GL=1 xvfb-run -a ctest --preset linux-x64-debug -L '^gl$'
```

Sin pantalla, el salteo dice qué faltó: "sin OpenGL 3.3 core (SDL_Init falló: No available video
device): test salteado; con MURET_CLIENT_REQUIRE_GL=1 falla". Con la variable, el mismo motivo va
en el fallo.

## Lo que `gl/` toma del cliente

La ventana escondida (`HiddenGlWindow`) usa lo que ya está en `ClientPlatform`: `SdlContext` para
`SDL_Init` y `SDL_Quit`, los deleters de la ventana y del contexto de `Platform/Window.h`,
`LoadGlFunctions` (las cinco funciones de la CL0.3, que alcanzan para preguntar la versión),
`HasRequiredGlVersion`, `IsCoreProfile`, `GlString`, `IsValidProcAddress` y la versión pedida
(`GlMajorVersion` y `GlMinorVersion`). No usa `Window::Create`, porque esa ventana se muestra, es de
640 × 480 y pide VSync, que un test no necesita.

Lo que el cliente no tiene todavía es la tabla grande: el lab arma la suya con una X-macro
(`gl/LabGl.h`, la pregunta 5 del plan en chico), 41 funciones sobre las 5 de la CL0.3, con los
mismos chequeos de `IsValidProcAddress` y el mismo reintento en `opengl32.dll` de Windows. El de
`Platform/GlLoader.cpp` es interno, así que el lab lo copia (son 12 líneas); la CL5.1 deja uno solo,
con la X-macro en `Platform/GlFunctionList.h`, y esta lista, con quién usa cada grupo, es su punto
de partida. `MURET_CLIENT_REQUIRE_GL` se lee como `Client/tests/support/GoldenDir.cpp` lee su
variable: con `_dupenv_s` en MSVC y `getenv` en lo demás. **Las dos son copias, y se van:** en la
CL5.1 el lab pasa a pedir sus funciones con el loader de `Platform` (el pedido y el reintento en
`opengl32.dll` quedan en un solo lugar), y a leer la variable con `Core::ReadEnvironmentVariable`,
que trae la pila de la CL4 y todavía no está en esta base (CODING_RULES §4).

## 1. Calentamiento

El minimapa cae **arriba a la izquierda**: x de 40 a 240, y de 340 a 440 contando desde abajo,
que son de 40 a 140 desde arriba. La v = 0 va con los vértices de y = 440, los de arriba: la primera
fila del buffer queda arriba, como en `0x621b60`, que le da la v al vértice de `H − y`. Son 2
llamadas por vértice (`glTexCoord2f` y `glVertex2f`) y 4 por quad (`glBindTexture`, `glColor4f`,
`glBegin`, `glEnd`): 12, sin las de las matrices. El original manda color, coordenada de textura y
vértice por cada vértice: 285.000 llamadas para 95.000 vértices, tres por vértice (06).

**Respuesta.** Queda todo: el color (el alfa de 0,5 lo hereda el quad siguiente si no pone otro;
es lo que hace `0x621b60` cuando lo deja en blanco, "Lo que más puede morder" en el plan), la
última coordenada de textura, la textura atada, las dos matrices y el modo (`GL_MODELVIEW`). El 0,5
solo se nota con `glEnable(GL_BLEND)`.

## 2. De la función fija al pipeline programable

**Respuesta.** En el modo inmediato, recién en el `glEnd`: hasta ahí el driver junta los vértices
de a uno, y no sabe si el fan va a tener 4 o 4.000. Con un VBO los datos ya están en un buffer, y
`glDrawArrays` o `glDrawElements` dicen cuántos con un número: una llamada por lote.

## 3. Coordenadas, NDC y viewport

**Respuesta.** (512, 384) cae en (0, 0) del NDC y (0, 768) en (−1, 1). Con el viewport
`(0, 0, 512, 384)`, (1024, 768) es (1, 1) del NDC y va a (512, 384): la esquina de arriba a la
derecha del viewport, que está en el medio de la ventana.

## 4. La misión: `OriginalQuad`

Antes de las implementaciones, con los `.cpp` vacíos, compilan los tests y falla el linker:
`undefined reference to 'Labs::FanVertices(Labs::OriginalQuad const&, Labs::Screen)'`, y lo mismo
con `ScaleX`, `ScaleY`, `Ortho2D`, `Multiply` y `FanToTriangles`.

**Las decisiones:**

- **Multiplica por la inversa en float**, como el binario (actividad 5): `ScaleY(y) = H · y ·
  (1.0f / 480)`, no `H · y / 480`.
- **La cuenta en `double` y el resultado en `float`.** La x87 del original, con los 53 bits que
  pone Windows, es un `double`, y el que llama guarda el resultado con `FSTP` en un float. En la
  escala da lo mismo que en `float` (actividad 5), pero en el borde de abajo no (actividad 14).
- **El borde de abajo sale de la resta sin redondear**: `H − y` queda en la pila de la x87 y se le
  resta h, sin pasar por el float guardado (actividad 14).
- **`EXPECT_FLOAT_EQ` para los números del plan**, que son enteros, porque el original no los da
  justos (25 escalado en 640 × 480 es 25,000002), y `EXPECT_EQ` con el float exacto donde los bits
  importan.

| Test | Qué fija |
| --- | --- |
| `FlipsYLikeTheOriginal` | en 640 × 480, el `0x15`: (0, 455), (0, 135), (320, 135), (320, 455). Con las banderas prendidas el original escala igual, y la de abajo sale 134,99997, dos ULP debajo de 135 |
| `ScalesFrom640x480` | en 1024 × 768: (0, 728), (0, 216), (512, 216), (512, 728) |
| `ScalesPositionAndSizeApart` | el `0x16` (x = 320) en 1024 × 768: solo la posición, (512, 728), (512, 408), (832, 408), (832, 728); solo el tamaño, (320, 743), (320, 231), (832, 231), (832, 743); ninguna, (320, 743), (320, 423), (640, 423), (640, 743) |
| `TheUvGoesDownTheTexture` | (u, v), (u, v + dv), (u + du, v + dv), (u + du, v) |
| `ScalesByTheInverseOfTheSize` | la actividad 5 |
| `TheBottomComesFromTheUnroundedTop` | la actividad 14 |

## 5. 1/640 en float

En el `.rdata` de `main.exe`:

```text
$ objdump -s -j .rdata --start-address=0x6ba06c --stop-address=0x6ba074 main.exe
 6ba06c 8988083b cdcccc3a                    ...;...:
```

En little-endian son `0x3b088889` en `0x6ba06c` y `0x3acccccd` en `0x6ba070`: justo
`std::bit_cast<std::uint32_t>(1.0f / 480)` y `(1.0f / 640)`, y los `static_assert` de
`OriginalQuad.cpp` lo comprueban al compilar. Como decimales, 0,0020833334419876337 y
0,0015625000232830644: los dos, un poco más que 1/480 y 1/640.

Las predicciones (`ScalesByTheInverseOfTheSize`): `ScaleY(25)` da 25,000001907348633 en 640 × 480 y
40,000003814697266 en 1024 × 768, un ULP arriba de 25 y de 40; `ScaleY(320)` en 640 × 480,
320,0000305175781, también un ULP arriba. `ScaleX(320)` en 1024 × 768 da 512 justos: ahí el error
es menor que medio ULP.

**El tipo de la cuenta.** Con todas las coordenadas enteras de 0 a 640 y de 0 a 480, en 640 × 480,
800 × 600, 1024 × 768, 1280 × 1024 y 1920 × 1080, la cuenta en `float` y en `double` dan el mismo
float (un modelo en Python con NumPy, fuera del repo). Lo que cambia es **dividir**: `H · y / 480`
da otro float que el original en 294 (con H = 768) a 401 (con H = 480) de las 481 coordenadas y; en
x, solo en 1024 de ancho, en 127 de las 641. Con `long double` (los 64 bits de la x87, con GCC en
x86-64) tampoco cambia ninguno: un programa en C++ fuera del repo, con las mismas coordenadas.

**Respuesta.** 640 es 2⁷ · 5, y 1/5 en binario es periódico (0,00110011...): no entra en 24 bits.
1/512 es 2⁻⁹, exacto. El test que lo deja fijo es `ScalesByTheInverseOfTheSize`: `ScaleY(25)` en
640 × 480 es `25.000001907348633f` y no `25.0f`; dividiendo daría 25 justos y falla.

## 6. La misión: `Ortho2D`, y glm

`MatchesGlm` compara con `EXPECT_EQ`, bit a bit, los 16 floats de `glm::ortho(0.0f, W, 0.0f, H)`
(por `glm::value_ptr`) en 640 × 480 y en 1024 × 768, y pasa: glm hace las mismas cuentas en el
mismo orden y en float (`2 / (right − left)`, `−(right + left) / (right − left)`), así que da los
mismos floats.

**Las decisiones:**

- **`EXPECT_EQ`**, porque las dos son la misma fórmula: si algún día difieren, es que una cambió.
- **`At(columna, fila) = columna · 4 + fila`**: el código escribe `m[At(3, 0)]` donde la página de
  manual pone la traslación en x (la fila 0, la columna 3), y en el `std::array` queda en el
  elemento 12 (`IsColumnMajor`).
- glm guarda la matriz como cuatro columnas: `value_ptr(m)` es `&m[0][0]`, y los 16 floats van
  columna por columna, como los espera `glUniformMatrix4fv` con `GL_FALSE`.

## 7. El fan, el sentido de giro y el culling

**Predicción.** Con la y para arriba, (x, arriba), (x, abajo), (x + w, abajo) baja y después va a
la derecha: gira **en contra del reloj**, de frente. Con la matriz que da vuelta la y, a favor: de
espaldas (`OriginalPipelineTest.BothTrianglesFaceTheCamera`, con el área con signo en el NDC).

**Respuesta.** 1.000 quads son 6.000 índices; con índices, 4.000 vértices, y sin ellos (cada
triángulo con los suyos), 6.000.

## 8. La misión: `FanToTriangles`

**Las decisiones:**

- **Con menos de 3, una lista vacía**, sin error: es lo que hace GL con un fan de 2, no dibuja
  nada y no da error (`LessThanThreeAreNone`).
- **`std::uint32_t`**: un lote del original, de 95.000 vértices, no entra en los 65.535 de un
  `std::uint16_t`. Va con `glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, ...)`; con 16 bits
  sería `GL_UNSIGNED_SHORT` y la mitad de los bytes, y eso lo decide el lote de la CL5.2.

Además de los tres del plan, `EachTriangleSharesTheFirstVertex`: un fan de 6 son (0, 1, 2),
(0, 2, 3), (0, 3, 4) y (0, 4, 5).

## 9. El jefe

| Test | Qué da |
| --- | --- |
| `TheLoginBackgroundLandsInNdc` | el `0x15` en 1024 × 768 va de (0, 728) a (512, 216) en la ventana: arriba a la izquierda cae en (−1; 43/48 = 0,8958) del NDC, y abajo a la derecha, en (0; −0,4375) |
| `TheMiddleOfTheUvIsTheMiddleOfTheQuad` | la UV (0,5; 0,5) está en la diagonal 0-2, que comparten los dos triángulos: los dos dan (256, 472) en la ventana y (−0,5; 11/48 = 0,2292) en el NDC |
| `TheNdcGoesBackToThePixelOfTheOriginal` | en 640 × 480, 800 × 600, 1024 × 768 y 1280 × 1024, cada vértice del `0x15` vuelve del NDC a su punto de la ventana con menos de un milésimo de píxel de diferencia |
| `BothTrianglesFaceTheCamera` | con la y para arriba, área positiva en los dos triángulos; con la y para abajo, negativa |
| `TheLoginThreeDViewportIsTheSelectionTraceRow` | `0x620db0(0, 25, 640, 430)` da `glViewport(0, 40, 1024, 688)` en 1024 × 768, la fila de la traza de la selección; en 640 × 480, `(0, 25, 640, 430)` |
| `TheGameViewportTruncatesLikeTheTrace` | `0x620db0(0, 0, 640, 432)` en 1024 × 768: 768 · 432 / 480 es 691,2, y la división entera da 691: `(0, 77, 1024, 691)`, la de la traza de Lorencia |
| `At800x600TheViewportTruncatesAndTheQuadDoesNot` | el mismo rectángulo en 800 × 600: como viewport, `(0, 32, 800, 537)`, de 32 a 569 desde abajo; como quad escalado, de 31,249998 a 568,75 (*estático*) |

El viewport es `OriginalViewport`, la cuenta de la actividad 14 en `std::uint32_t`: W · x / 640 y
H · y / 480 truncando, y `glViewport(x, H − y − h, w, h)`.

**Respuesta.** El 2D, sí: con la matriz de `gluOrtho2D` y el viewport entero, cada vértice cae en
el punto de la ventana que le daba el original (con menos de un milésimo de píxel, lo que pierde
el float de ida y vuelta), y los triángulos del fan cubren lo mismo que el fan. El borde entre el
2D y el 3D, no siempre: en 640 × 480 y en 1024 × 768 caen en el mismo lugar (el quad, a menos de
un diezmilésimo de píxel del entero del viewport), pero en 800 × 600 el viewport trunca y el quad
no, y el 3D queda 0,75 píxeles arriba del 2D abajo y 0,25 arriba. En 1280 × 1024, que no es 4:3,
pasa lo mismo con 0,667 y 0,333 (un modelo en Python de las dos cuentas, fuera del repo). MuRet lo
reproduce si hace las dos cuentas como el original: la del viewport en enteros sin signo, y la del
quad en float.

## 10. Texturas, y la mezcla

**Respuesta.** RGB de 3 × 2 con el 4: cada fila mide 9 bytes y ocupa 12, y la última no lleva
relleno: 12 + 9 = 21. RGBA del mismo tamaño: 24, filas de 12, que ya son múltiplo de 4. Una fila
RGBA mide 4 · ancho bytes: siempre es múltiplo de 4 (`RequiredBytesFollowTheUnpackAlignment`: 12,
14, 3, 21, 16 y 0).

## 11. El quad en un framebuffer

**Predicción.** Arriba a la izquierda rojo, arriba a la derecha verde, abajo a la izquierda azul
y abajo a la derecha blanco: la fila 0 de la textura, arriba. `glReadPixels` da primero la fila de
abajo, así que `ReadQuadrants` las da vuelta para mirarlas como una pantalla. Con Xvfb pasa.

- **El VAO guarda el EBO**: el `GL_ELEMENT_ARRAY_BUFFER` atado es parte del estado del VAO, así
  que `Create` lo ata una vez con el VAO atado y `Draw` solo ata el VAO. El `GL_ARRAY_BUFFER` no es
  del VAO: cada atributo se queda con el buffer que estaba atado en su `glVertexAttribPointer`.
- **`GL_FALSE`**: la matriz de `Ortho2D` ya está en column-major. Con `GL_TRUE`, GL la transpone
  (actividad 13).

**Respuesta.** Sin contexto, el `glDeleteTextures` no tiene a quién decirle: según el driver se
ignora o se cae, y el nombre queda vivo en el driver hasta que se destruya su contexto. En los
tests no pasa: el fixture abre la ventana en `SetUp` y las texturas son variables del test, que
mueren antes; y `HiddenGlWindow` declara sus miembros en el orden SDL, ventana, contexto, así que
se destruyen al revés.

| Test de `gl/` | Qué fija |
| --- | --- |
| `GlNameTest.DeletesItsNameOnce` | el destructor borra el nombre una vez (con un borrador falso, sin GL) |
| `GlNameTest.AMovedNameIsDeletedByItsNewOwner` | al moverlo, el viejo queda en 0 y lo borra el nuevo |
| `GlNameTest.MoveAssignmentDeletesTheOldName` | la asignación por movimiento borra el que tenía |
| `GlNameTest.AnEmptyNameDeletesNothing` | un nombre en 0 no llama al borrador |
| `GlRequirementTest.OnlyOneRequiresGl` | solo `"1"` pide GL: sin la variable, vacía, `"0"`, `"true"` o `" 1"`, no |
| `HiddenGlWindowTest.OpensAThreeThreeCoreContext` | el contexto es 3.3 o más, con el perfil core |
| `LabFramebufferTest.DrawsTheQuadIntoAFramebuffer` | el paso 6 del plan: los cuatro cuadrantes de arriba |
| `LabFramebufferTest.TheOrderMattersOnlyWithNormalBlending` | la actividad 12 |
| `LabTextureRulesTest.RequiredBytesFollowTheUnpackAlignment` | la actividad 10 |
| `LabTextureRulesTest.RefusesABufferShorterThanWhatGlReads` | 12 bytes con el 4: `UploadTexture` no llama a GL y el error dice que hacen falta 14 (con una tabla de GL vacía, sin contexto) |
| `LabTextureTest.TheDefaultAlignmentReadsTheSecondRowFromByteEight` | la actividad 13 |

El nombre en CTest lleva el prefijo y la suite: el `LabCl50Gl.DrawsTheQuadIntoAFramebuffer` del
plan es `LabCl50Gl.LabFramebufferTest.DrawsTheQuadIntoAFramebuffer`.

## 12. Comparar

Está en
[`labs/README.md`](../README.md#cl50-95000-vértices-de-a-uno-contra-un-vbo-y-el-orden-con-mezcla),
y el test de la mezcla es `LabFramebufferTest.TheOrderMattersOnlyWithNormalBlending`.

## 13. Rompelo

Con la textura de 2 × 2 del framebuffer, en Xvfb y Mesa (un programa fuera del repo, con las
bibliotecas del lab):

```text
y para arriba: arriba rojo y verde, abajo azul y blanco
y para arriba, culling: arriba rojo y verde, abajo azul y blanco
y para abajo: arriba azul y blanco, abajo rojo y verde
y para abajo, culling: no dibuja nada (0 píxeles de 4.096)
transpuesta: solo el cuadrante de arriba a la derecha (1.024 píxeles de 4.096)
glGetError: 0
```

1. **`GL_UNPACK_ALIGNMENT` en 4.** La fila 0 sale bien, pero la 1 empieza en el byte 8 y no en el
   6: abajo a la izquierda va (255, 255, 255), los bytes 8 a 10, y abajo a la derecha
   (255, 0xaa, 0xaa), el último byte del blanco y dos de las marcas
   (`TheDefaultAlignmentReadsTheSecondRowFromByteEight`). Sin las marcas, GL habría leído 2 bytes
   fuera del buffer: `UploadTexture` lo rechaza antes (`RefusesABufferShorterThanWhatGlReads`).
2. **La y dos veces.** Con `Ortho2D(0, W, H, 0)`, el `0x15` en 1024 × 768 cae abajo y cabeza
   abajo: (0, 728), con la v = 0, va a −0,8958 del NDC, y (0, 216) a 0,4375. El quad del test,
   que llena el framebuffer, sale dado vuelta: azul y blanco arriba. Y como gira a favor del reloj,
   con el culling prendido no se dibuja.
3. **La transpuesta.** La traslación pasa a la fila de w: (0, 0) va a (0, 0, 0, 1), pero (0, 64) a
   (0, 2, 0, −63) y (64, 64) a (2, 2, 0, −127). Con w negativo el recorte se queda con un pedazo
   del triángulo, y la división por w lo estira: la textura entera termina en el cuadrante de
   arriba a la derecha, deformada.

**Respuestas:**

- **GL no dice nada** en ninguno de los tres: `glGetError` da 0. Lo que lee de un buffer, lo que
  da vuelta una matriz o lo que tira el culling es lo que se le pidió.
- **Sin GL** se ven la y dos veces (el giro, `BothTrianglesFaceTheCamera`, y el NDC del jefe), la
  matriz transpuesta (`IsColumnMajor`, y el clip calculado en la CPU) y el largo del buffer
  (`RequiredUnpackBytes`). **Solo con píxeles**: qué bytes toma GL con el 4, y qué tira el culling.

## 14. Carril R

Las líneas de la guía están en `00620d50_FUN_00620d50.asm`, `00621b60_FUN_00621b60.asm` y
`00620db0_FUN_00620db0.asm` (build `c32bbd5f`), y en `main.exe` con `objdump -d -M intel`.

**Respuestas:**

- **Las constantes**: `0x6ba070` es `0x3acccccd`, 1/640 en float, y `0x6ba06c` es `0x3b088889`,
  1/480 (actividad 5).
- **`FST` deja el número en la pila.** `0x621beb` guarda `H − y` como float en `[ebp-0x44]` (el
  borde de arriba) y lo deja en `ST0`; `FLD ST0` lo duplica, y `0x621bf0` le resta h a ese, el que
  no pasó por el float. El decompilado de Ghidra lo escribe como si restara del guardado
  (`local_4c[3] = local_4c[1] - param_5;`), y no es lo mismo: en 1280 × 1024, con el `0x15`, el
  borde de arriba es 970,6666870117188 y el de abajo 287,99996948242188; restando del float de
  arriba daría 288 justos (`TheBottomComesFromTheUnroundedTop`). La resta de dos floats en 53
  bits es exacta, así que con la x87 en 64 bits daría lo mismo.
- **Las divisiones del viewport.** `MUL` por `0xcccccccd` y `SHR 9` (sobre la parte alta, `EDX`) es
  dividir por 640, truncando; `MUL` por `0x88888889` y `SHR 8`, dividir por 480. El `MUL` es la
  multiplicación sin signo, y es lo que hace que la división sea sin signo. Los `IMUL` que
  multiplican antes por W y H son con signo, pero los 32 bits de abajo dan lo mismo que un `MUL`.
  768 · 432 = 331.776, y 331.776 / 480 da 691 (691,2 truncado): la altura del viewport del juego en
  la traza.
