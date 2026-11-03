# Ola CL5.0: el pipeline gráfico

La N.0 de la CL5 del cliente. Antes de armar el `Renderer`, el `SpriteBatch` y la escena de login,
hace falta saber qué hacía la función fija de OpenGL 1.1 que usa el `main.exe` (`glBegin`,
`glVertex`, la pila de matrices), qué la reemplaza en el perfil core, cómo llega un vértice de los
píxeles de la ventana al NDC y de vuelta al píxel, por qué el original da vuelta la y, y qué puede
salir mal al subir una textura. Plan (tiene las respuestas: abrilo después):
[CL5](../superpowers/plans/2026-11-02-ola-cl5-render-2d-y-login.md#sub-ola-cl50-el-pipeline-gráfico-n0-en-labs).

**Resuelta por Claude** el 2026-11-02, a tu pedido: el código está en
[`labs/cl5.0-pipeline/`](../../labs/cl5.0-pipeline/), y las respuestas de las 14 actividades, con
las salidas reales, en su [README](../../labs/cl5.0-pipeline/README.md). Si la hacés vos, abrilo
después de cada actividad, no antes. La sección final de esta guía sigue siendo tuya.

Qué cubre:

- **Teoría:**
  - la función fija de OpenGL 1.1 (`glBegin`, `glVertex`, `glColor`, la pila de matrices,
    `glAlphaFunc`, `glTexEnv`) y lo que la reemplaza en el perfil core: buffers de vértices (VBO)
    y su formato (VAO), un vertex shader, un fragment shader y `discard`;
  - las coordenadas: del espacio del objeto al clip, la división por w, el NDC de −1 a 1 y el
    viewport; por qué `gluOrtho2D(0, W, 0, H)` deja la y para arriba y el original da vuelta la
    suya (`H − y`);
  - matrices en column-major, y qué espera `glUniformMatrix4fv`;
  - un `GL_TRIANGLE_FAN`, sus triángulos, el sentido de giro y el culling;
  - texturas: `glTexImage2D`, `GL_UNPACK_ALIGNMENT`, el filtro, el wrap y los lados potencia de 2;
  - mezcla normal y aditiva, y por qué el orden importa con una y no con la otra.
- **C++:**
  - RAII sobre nombres de GL: un `GLuint` que se borra en el destructor, se mueve y no se copia, y
    que necesita el contexto vivo;
  - `std::span` para subir un buffer, con el largo chequeado antes de llamar a GL;
  - `glm::mat4`, `glm::ortho` y por qué `glm::value_ptr` da los 16 floats en el orden de GL;
  - una X-macro para la tabla de funciones de GL (la pregunta 5 del plan, en chico);
  - `std::bit_cast` para ver los bits de un float (de la CL1.3), ahora en un `static_assert`.
- **Lectura:** Joey de Vries, *LearnOpenGL*; las páginas de manual de `glOrtho`, `glViewport`,
  `glPixelStorei` y `glBlendFunc`; la especificación de OpenGL 3.3 core, el apéndice E; glm.

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | un quad con `glBegin` y `glVertex`, a mano |
| 2 | idea | de la función fija al pipeline programable |
| 3 | idea | coordenadas: el clip, el NDC y el viewport |
| 4 | misión | `Labs::OriginalQuad`, los cuatro vértices de `0x621b60` |
| 5 | detective de números | 1/640 en float |
| 6 | misión | `Labs::Ortho2D`, a mano y contra `glm::ortho` |
| 7 | idea | el fan, el sentido de giro y el culling |
| 8 | misión | `Labs::FanToTriangles` |
| 9 | jefe | el pipeline del original contra el de MuRet, sin GL |
| 10 | idea | texturas: `GL_UNPACK_ALIGNMENT`, el filtro, el wrap, potencias de 2, y la mezcla |
| 11 | GL | el quad dibujado en un framebuffer (lo escribe Claude, `gl/`) |
| 12 | comparar | 95.000 vértices de a uno contra un VBO; el orden con mezcla |
| 13 | rompelo | `GL_UNPACK_ALIGNMENT`, la y dada vuelta dos veces, la matriz transpuesta |
| 14 | detective (carril R) | `0x621b60`, `0x620d50` y `0x620db0` en el desensamblado |

Cada actividad termina en una pregunta. Las respuestas no están en la guía: salen de lo que
corrés. El plan y el [06](../../Client/docs/research/06-render-escenas-y-ui.md) tienen algunas,
así que abrilos después de predecir.

## 1. Calentamiento: un quad a mano

Un minimapa de 200 × 100 en una ventana de 640 × 480, con la función fija de OpenGL 1.1:

```cpp
glMatrixMode(GL_PROJECTION);
glLoadIdentity();
gluOrtho2D(0, 640, 0, 480);
glMatrixMode(GL_MODELVIEW);
glLoadIdentity();
glBindTexture(GL_TEXTURE_2D, minimap);
glColor4f(1, 1, 1, 0.5f);
glBegin(GL_TRIANGLE_FAN);
glTexCoord2f(0, 0); glVertex2f(40, 440);
glTexCoord2f(0, 1); glVertex2f(40, 340);
glTexCoord2f(1, 1); glVertex2f(240, 340);
glTexCoord2f(1, 0); glVertex2f(240, 440);
glEnd();
```

**Predecí, antes de seguir:**
- ¿en qué parte de la ventana cae: arriba o abajo, a la izquierda o a la derecha?
- la primera fila del buffer de la textura es la v = 0. ¿Queda arriba o abajo en la pantalla?
- ¿cuántas llamadas a GL hace este quad por vértice? El 06 dice que el original manda unos 95.000
  vértices por frame: ¿cuántas llamadas son?

**Pregunta:** ¿qué de este código queda guardado en GL entre una llamada y la siguiente, y lo
hereda el quad que venga después?

## 2. Idea: de la función fija al pipeline programable

En OpenGL 1.1 el driver tiene un pipeline **fijo**: cada `glVertex` toma el color y la coordenada
de textura que se pusieron antes, lo multiplica por las matrices de la pila (`GL_MODELVIEW` y
`GL_PROJECTION`), recorta, rasteriza, combina la textura con el color como diga `glTexEnv`
(`GL_MODULATE`: los multiplica), tira los fragmentos que no pasan el alpha test (`glAlphaFunc`) y
mezcla con lo que ya está en la ventana (`glBlendFunc`). Se configura, no se programa.

En el perfil **core** de 3.3 (D-C3), casi todo eso no existe: no hay `glBegin`, `glVertex`, pila
de matrices, `glAlphaFunc` ni `glTexEnv`. En su lugar:

| Función fija (el original) | Perfil core (MuRet) |
| --- | --- |
| `glBegin`, `glColor`, `glTexCoord`, `glVertex`, `glEnd` | los vértices en un buffer de la GPU (VBO), su formato en un VAO (`glVertexAttribPointer`: qué atributo, cuántos floats, cada cuánto y desde dónde), y una llamada que los dibuja (`glDrawArrays`, `glDrawElements`) |
| `glMatrixMode`, `gluOrtho2D`, `glRotatef`, `glTranslatef` | una matriz que se calcula en la CPU (glm) y se pasa al shader como `uniform` con `glUniformMatrix4fv` |
| lo que hace la pila con cada vértice | un **vertex shader**: multiplica cada vértice por la matriz y escribe `gl_Position`, en clip |
| `glTexEnv(GL_MODULATE)` | un **fragment shader**: `texture(image, uv) * color` |
| `glAlphaFunc(GL_GREATER, 0.25)` | `if (color.a <= 0.25) discard;` en el fragment shader |
| `GL_QUADS` | no está; `GL_TRIANGLE_FAN` sí, pero un lote usa `GL_TRIANGLES` con índices (actividad 7) |
| `glBlendFunc`, `glViewport`, `glEnable(GL_BLEND)` | igual: no se programan |

Un shader se compila y se enlaza en tiempo de ejecución (`glCompileShader`, `glLinkProgram`), y
si no compila, el error está en un log que hay que pedir (`glGetShaderInfoLog`).

**Pregunta:** en el modo inmediato, ¿cuándo se entera el driver de cuántos vértices tiene un
`glBegin`? ¿Y con un VBO?

## 3. Idea: coordenadas, NDC y viewport

Un vértice pasa por tres espacios:

```text
posición (x, y)                     lo que manda el programa: píxeles, en el 2D del original
  × matriz          → clip (x, y, z, w)
  ÷ w               → NDC, de −1 a 1 en cada eje; lo que cae afuera se recorta
  viewport          → la ventana: x0 + (ndc_x + 1) · ancho / 2, y0 + (ndc_y + 1) · alto / 2
```

En GL la fila y = 0 de la ventana es **la de abajo**. `gluOrtho2D(l, r, b, t)` es
`glOrtho(l, r, b, t, −1, 1)`: lleva de l a r en x, y de b a t en y, al −1 a 1 del NDC. Con
`gluOrtho2D(0, W, 0, H)` y el viewport entero, un vértice en (x, y) cae en el píxel (x, y) contado
desde abajo a la izquierda: es lo que hace `0x6219b0` antes del 2D (06).

La UI del original, en cambio, piensa en la y para abajo (el (0, 0) arriba a la izquierda, como
el mouse del `WndProc`). La vuelta la da en cada vértice: `0x621b60` pone `H − y` (actividad 4).
Y para el pase 3D, `0x620db0(x, y, w, h)` recibe un rectángulo en 640 × 480 con la y para abajo, lo
escala a la ventana y lo da vuelta para `glViewport(x, H − y − h, w, h)` (06, "Cámara").

**Pregunta:** con `gluOrtho2D(0, 1024, 0, 768)`, ¿en qué NDC cae (512, 384)? ¿Y (0, 768)? Si el
viewport fuera `(0, 0, 512, 384)`, ¿en qué píxel cae (1024, 768)?

## 4. Misión: `Labs::OriginalQuad`

**El contrato:** `FanVertices(const OriginalQuad&, Screen)` da los cuatro vértices y las cuatro UV
de `0x621b60(slot, x, y, w, h, u, v, du, dv, escalar tamaño, escalar posición, alfa)` en una
pantalla de W × H, en el orden del fan (sin el slot ni el alfa, que no cambian los vértices):

```text
(x, H − y)            con (u, v)
(x, H − y − h)        con (u, v + dv)
(x + w, H − y − h)    con (u + du, v + dv)
(x + w, H − y)        con (u + du, v)
```

Con "escalar posición", x e y pasan antes por `0x620d50` (`x · W / 640`) y `0x620d80`
(`y · H / 480`); con "escalar tamaño", w y h. Cada eje por su lado. Va en `labs/cl5.0-pipeline/`,
con `ScaleX`, `ScaleY` y `Screen`.

**Te toca**, en este orden:

1. Los tests primero (`test_original_quad.cpp`): `OriginalQuadTest.FlipsYLikeTheOriginal` (en
   640 × 480, el `0x15` del login, `0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0)`: (0, 455),
   (0, 135), (320, 135), (320, 455)), `ScalesFrom640x480` (en 1024 × 768: (0, 728), (0, 216),
   (512, 216), (512, 728)), `ScalesPositionAndSizeApart` (solo la posición, solo el tamaño, y
   ninguna) y `TheUvGoesDownTheTexture`. Antes de compilar, decí qué etapa va a fallar.
2. `OriginalQuad.h` y `OriginalQuad.cpp`, primero en rojo y después en verde.

**Decisiones que pesás vos:**
- ¿`ScaleX` divide por 640 o multiplica por 1/640? ¿Da lo mismo? (La actividad 5 lo mira.)
- ¿en qué tipo hacés las cuentas: `float`, `double`? ¿Y en qué tipo las hacía el original?
- el borde de abajo, `H − y − h`: ¿se lo restás al de arriba ya guardado, o hacés la cuenta
  entera de nuevo? (La actividad 14 lo mira en el binario.)
- ¿con qué comparás los floats en los tests: `EXPECT_EQ` o `EXPECT_FLOAT_EQ`?

## 5. Detective de números: 1/640 en float

`0x620d50` son tres instrucciones de la FPU x87: carga W como entero (`FILD`), lo multiplica por
la x (`FMUL`) y después por un float que está en `0x6ba070`. `0x620d80`, lo mismo con H y el float
de `0x6ba06c`.

**Te toca:**
- ver los bits de `1.0f / 640` y de `1.0f / 480` con `std::bit_cast<std::uint32_t>`, y
  compararlos con los cuatro bytes de `0x6ba070` y `0x6ba06c` en el `.rdata` de `main.exe`
  (`objdump -s -j .rdata --start-address=0x6ba06c --stop-address=0x6ba074`);
- **predecir** `ScaleY(25)` en 640 × 480 y en 1024 × 768. ¿Da 25 y 40 justos?
- la x87 que usa el original trabaja, en Windows, con 53 bits de mantisa: la de un `double`. ¿Qué
  cambia si tu `ScaleY` hace la cuenta en `float`, en `double` o en `long double`?

**Pregunta:** ¿por qué 1/640 no se puede guardar exacto en un float y 1/512 sí? ¿Con qué test
dejarías fijo que MuRet multiplica por la inversa y no divide?

## 6. Misión: `Labs::Ortho2D`, y glm

**El contrato:** `Matrix4 Ortho2D(float left, float right, float bottom, float top)` es la matriz
de `gluOrtho2D`, la de la página de manual de `glOrtho` con near −1 y far 1: 2/(r − l) y
2/(t − b) en la diagonal, −2/(f − n) = −1 en z, y la traslación −(r + l)/(r − l), −(t + b)/(t − b)
y −(f + n)/(f − n) = 0. En un `std::array<float, 16>` en **column-major**, como la espera GL: los
cuatro primeros floats son la primera columna. `Multiply(matrix, Vec4)` la aplica a un punto.

**Te toca**, tests primero (`test_ortho.cpp`):

1. `OrthoTest.MapsTheCornersToNdc`: (0, 0) va a (−1, −1) y (W, H) a (1, 1).
2. `MatchesGlm`: los 16 floats de `glm::ortho(0.0f, W, 0.0f, H)` (por `glm::value_ptr`) en
   640 × 480 y en 1024 × 768.
3. `IsColumnMajor`: la traslación en los elementos 12 y 13.
4. La implementación.

**Decisiones que pesás vos:**
- ¿comparás con glm con `EXPECT_EQ` o con `EXPECT_FLOAT_EQ`? ¿Por qué podría dar igual bit a bit?
- ¿cómo escribís el índice de (columna, fila) para que el código se lea como la página de manual?

Construís, comparás y usás el estándar (L-3): la CL5.2 usa `glm::ortho`, no tu `Ortho2D`.

## 7. Idea: el fan, el sentido de giro y el culling

Un `GL_TRIANGLE_FAN` de n vértices son n − 2 triángulos que comparten el primero: (0, 1, 2),
(0, 2, 3), (0, 3, 4)... Cada fan es una llamada aparte (en el original, un `glBegin` por quad). Para
juntar muchos quads en una sola llamada, el lote los pasa a `GL_TRIANGLES` y usa un buffer de
índices (EBO) con `glDrawElements`: cada quad, cuatro vértices y seis índices.

El **sentido de giro**: en el NDC, con la y para arriba, un triángulo cuyos vértices van en contra
del reloj mira **de frente** (`glFrontFace(GL_CCW)`, el de GL por defecto). Con
`glEnable(GL_CULL_FACE)` y `glCullFace(GL_BACK)`, los que miran de espaldas no se dibujan. El
original prende y apaga el culling por malla (06).

**Predecí:** el fan del original, (x, arriba), (x, abajo), (x + w, abajo), (x + w, arriba), con la
y para arriba: ¿gira a favor o en contra del reloj? ¿Y si la matriz da vuelta la y?

**Pregunta:** si un lote junta 1.000 quads en un `glDrawElements`, ¿cuántos índices lleva? ¿Y
cuántos vértices, con índices y sin ellos?

## 8. Misión: `Labs::FanToTriangles`

**El contrato:** `std::vector<Triangle> FanToTriangles(std::uint32_t vertexCount)` da los n − 2
triángulos de un fan de n vértices, con `Triangle = std::array<std::uint32_t, 3>`. Va en
`labs/cl5.0-pipeline/`.

**Te toca**, tests primero (`test_fan.cpp`): `FanTest.FourVerticesAreTwoTriangles` ((0, 1, 2) y
(0, 2, 3)), `ThreeVerticesAreOne` y `LessThanThreeAreNone`; después, `Fan.h` y `Fan.cpp`.

**Decisiones que pesás vos:**
- ¿qué devolvés con 0, 1 o 2 vértices: un error o una lista vacía? ¿Qué hace GL con un fan de 2?
- ¿por qué `std::uint32_t` y no `std::uint16_t`, si un quad tiene cuatro vértices? ¿Qué cambia en
  `glDrawElements`?

## 9. Jefe: el pipeline del original contra el de MuRet, sin GL

El original manda cada vértice con `gluOrtho2D(0, W, 0, H)` y el viewport entero: el píxel donde
cae es el vértice mismo. MuRet manda los mismos vértices en un VBO, el vertex shader los
multiplica por la matriz de `Ortho2D`, y GL los pasa del NDC a la ventana con el viewport. Si los
dos caminos dan el mismo píxel, el 2D del original se puede reproducir al vértice.

**Te toca** (`labs/cl5.0-pipeline/jefe/test_original_pipeline.cpp`), con lo de las actividades 4,
6 y 8:

1. Con el `0x15` en 1024 × 768: la esquina de arriba a la izquierda cae en (−1; 0,8958) del NDC y
   la de abajo a la derecha, en (0; −0,4375). **Predecí** antes los dos puntos de la ventana.
2. Un punto en el medio de la UV, (0,5; 0,5), cae en el centro del quad. ¿En cuál de los dos
   triángulos del fan está? Buscalo con coordenadas baricéntricas: los pesos del punto en las UV
   del triángulo, aplicados a las posiciones. Es lo que hace GL al interpolar.
3. De vuelta: el NDC pasado por el viewport entero da el mismo punto de la ventana, en las cuatro
   resoluciones del original (640, 800, 1024 y 1280 de ancho).
4. El sentido de giro de los dos triángulos, con la y para arriba y para abajo (actividad 7).
5. El pase 3D: el login llama a `0x620db0(0, 25, 640, 430)`, como la selección, y la traza de la
   selección dice `glViewport(0, 40, 1024, 688)` en 1024 × 768 (06, *verificado*). Y el juego,
   `0x620db0(0, 0, 640, 432)`: la traza dice `0,77,1024,691`. **Predecí** de qué cuenta sale el
   691. Escribí `0x620db0` como lo hace el binario (lo vas a leer en la actividad 14).
6. En 800 × 600, el mismo rectángulo de (0, 25, 640, 430) como viewport y como quad escalado:
   ¿caen en los mismos píxeles?

**Pregunta:** ¿el 2D del original se puede reproducir al vértice con un VBO y una matriz? ¿Y el
borde entre el 2D y el 3D, al píxel, en una resolución que no es múltiplo de 640 × 480?

## 10. Idea: texturas, y la mezcla

`glTexImage2D(target, nivel, formato interno, ancho, alto, 0, formato, tipo, píxeles)` copia una
imagen de la memoria del programa a la GPU. GL lee el buffer fila por fila desde la primera, que
queda en v = 0. Y cada fila **empieza en un múltiplo de `GL_UNPACK_ALIGNMENT` bytes**, que vale 4
por defecto: una fila RGB de 2 píxeles mide 6 bytes, y GL busca la siguiente en el byte 8. Con
`glPixelStorei(GL_UNPACK_ALIGNMENT, 1)` las filas van juntas. El OZJ de la CL1 es RGB, y su buffer
puede ser de 1 o de 2 de ancho (`TextureSide`).

- **El filtro** (`GL_TEXTURE_MIN_FILTER`, `GL_TEXTURE_MAG_FILTER`): `GL_NEAREST` toma el texel más
  cercano; `GL_LINEAR` mezcla los cuatro más cercanos. El de minificación vale por defecto
  `GL_NEAREST_MIPMAP_LINEAR`, que pide mipmaps: sin ellos la textura no está completa.
- **El wrap** (`GL_TEXTURE_WRAP_S`, `GL_TEXTURE_WRAP_T`): `GL_REPEAT` repite (u = 1,25 es 0,25);
  `GL_CLAMP_TO_EDGE` se queda en el borde. El original pide `GL_CLAMP` (`0x2900`), que no existe
  en el perfil core ("Lo que más puede morder", en el plan).
- **Lados potencia de 2:** OpenGL 1.1 solo acepta texturas de lados 1, 2, 4, 8...; los otros
  llegaron con la 2.0. Por eso el original pone cada imagen en un buffer de lados potencia de 2
  (05, CL1.4), y las UV del quad (`du`, `dv`) dicen qué parte usar.

**La mezcla** combina el color que sale del fragment shader (la fuente) con el que ya está en la
ventana (el destino):

```text
normal  (SRC_ALPHA, ONE_MINUS_SRC_ALPHA):  fuente · α + destino · (1 − α)
aditiva (ONE, ONE):                        fuente + destino, recortado a 1
```

**Pregunta:** una imagen RGB de 3 × 2 con `GL_UNPACK_ALIGNMENT` en 4: ¿cuántos bytes lee GL? ¿Y una
RGBA del mismo tamaño? ¿Por qué una RGBA nunca necesita cambiar el 4?

## 11. GL: el quad en un framebuffer (lo escribe Claude, `gl/`)

Este paso del plan es de Claude: `LabCl50Gl.LabFramebufferTest.DrawsTheQuadIntoAFramebuffer`
abre una ventana escondida con un contexto 3.3 core, arma un framebuffer de 64 × 64 (un FBO con
una textura RGBA8 de color), sube una textura de 2 × 2 con cuatro colores (la fila 0, rojo y
verde; la 1, azul y blanco), dibuja el quad que la cubre entera con tus `FanVertices`, `Ortho2D` y
`FanToTriangles`, y lee los píxeles con `glReadPixels`. Sin pantalla o sin 3.3 se saltea
(`GTEST_SKIP`); con `MURET_CLIENT_REQUIRE_GL=1`, falla (la pregunta 9 del plan). Los tests de
`gl/` llevan la etiqueta `gl` en CTest.

**Te toca:** leer `gl/LabQuadRenderer.cpp` y `gl/test_lab_framebuffer.cpp`, y antes de correrlo
con `xvfb-run -a ctest --preset linux-x64-debug -R LabCl50Gl`:
- **predecí** de qué color sale cada cuadrante del framebuffer. Ojo: `glReadPixels` da las filas
  desde abajo;
- ¿qué guarda el VAO cuando se ata el EBO, y por qué `Draw` no lo vuelve a atar?
- ¿por qué `uniformMatrix4fv` va con `GL_FALSE`?

**Pregunta:** cada nombre de GL del test (la textura, los buffers, el programa) se borra en su
destructor. ¿Qué pasaría si el `HiddenGlWindow`, que tiene el contexto, muriera antes que la
textura?

## 12. Comparar: 95.000 vértices de a uno contra un VBO, y el orden con mezcla

El paso 7 del plan. El 06 dice que el original hace unas 285.000 llamadas a GL por frame, casi
todas por vértice: unos 95.000 vértices mandados de a uno.

**Te toca:** anotarlo en `labs/README.md`.
- Con la traza del 06 a mano: ¿cuántas llamadas hace el modo inmediato por vértice, y cuántas un
  VBO por frame?
- Si tenés GL, medilo: un frame de 95.000 vértices con `glBegin` y `glEnd` (en un contexto de
  compatibilidad: en core no existen) contra un `glBufferData` de los mismos vértices.
- La mezcla: dos quads semitransparentes uno encima del otro, en un orden y en el otro, con la
  normal y con la aditiva. **Predecí** el color con los números de la actividad 10.
  `LabFramebufferTest.TheOrderMattersOnlyWithNormalBlending` lo prueba con GL.

**Preguntas:**
- ¿dónde está el costo del modo inmediato: en la GPU o en la CPU?
- ¿por qué un lote no puede reordenar quads para juntar los de la misma textura si hay mezcla
  normal? ¿Y si todos son aditivos?

## 13. Rompelo: tres maneras de ver otra cosa

1. **`GL_UNPACK_ALIGNMENT`.** Subí la textura de 2 × 2 RGB del framebuffer con el 4 en vez del 1:
   los mismos 12 bytes y 4 más de marca detrás, para que GL no lea fuera del buffer. **Predecí**
   qué cuadrante cambia y a qué color
   (`LabTextureTest.TheDefaultAlignmentReadsTheSecondRowFromByteEight`).
2. **La y dada vuelta dos veces.** En vez de `Ortho2D(0, W, 0, H)`, `Ortho2D(0, W, H, 0)`, con los
   vértices de `FanVertices`, que ya hacen `H − y`. ¿Dónde cae el quad y cómo se ve la textura? ¿Y
   con `glEnable(GL_CULL_FACE)`?
3. **La matriz transpuesta.** Pasá la de `Ortho2D` transpuesta (o con `GL_TRUE`). **Predecí** el
   clip de (0, 0), (0, 64) y (64, 64) con la de 64 × 64.

**Preguntas:**
- ¿qué te dice GL en cada caso: un error de `glGetError`, un aviso, o nada?
- ¿cuál de los tres lo encuentra un test sin GL, y cuál solo uno que lee píxeles?

## 14. Detective (carril R): el quad, la escala y el viewport en el binario

Están en el repo de research,
`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/` (`00620d50_FUN_00620d50.asm`,
`00621b60_FUN_00621b60.asm` y `00620db0_FUN_00620db0.asm`), y en Ghidra con
`mainexe-vmxp-c32bbd5f/original/main.exe` (o con `objdump -d -M intel --start-address=...`):

```text
620d65 fild  qword ptr [ebp-0x8]
620d68 fmul  dword ptr [ebp+0x8]
620d6b fmul  dword ptr [0x6ba070]

621bc9 fild  qword ptr [ebp-0x8]
621bd7 fsub  dword ptr [ebp+0x10]
621beb fst   dword ptr [ebp-0x44]
621bee fld   st(0)
621bf0 fsub  dword ptr [ebp+0x18]
621bf3 fst   dword ptr [ebp-0x3c]

620db9 mov   eax,0xcccccccd
620dc1 imul  edx,dword ptr [ebp+0x8]
620dc9 mul   edx
620dd3 mov   esi,edx
620dd7 mov   eax,0x88888889
620ddc imul  edx,dword ptr [ebp+0xc]
620de4 mul   edx
620deb mov   ebx,edx
620e06 shr   esi,0x9
620e09 shr   ebx,0x8
```

**Preguntas:**
- ¿qué float hay en `0x6ba070`? ¿Y en `0x6ba06c`?
- `FST` contra `FSTP`: ¿de dónde sale el número al que `0x621bf0` le resta h, del float que quedó
  en `[ebp-0x44]` o de otro? ¿Qué dice el decompilado (`00621b60_FUN_00621b60.c`)? Buscá una
  resolución del original donde la diferencia se vea.
- `0xcccccccd` con `SHR 9` y `0x88888889` con `SHR 8`: ¿qué división es cada una? ¿`MUL` o `IMUL`:
  con signo o sin signo? ¿Qué hace con 768 · 432?

## El C++ que entra

- **RAII sobre nombres de GL.** Un `GLuint` es un nombre, no un puntero: `glGenTextures` lo da y
  `glDeleteTextures` lo devuelve. Una clase lo guarda y lo borra en el destructor; se mueve (el
  viejo queda en 0, con `std::exchange`) y no se copia, porque dos dueños lo borrarían dos veces.
  Y tiene una regla que la memoria no tiene: el nombre es del contexto, que tiene que estar vivo
  y ser el actual cuando se borra. Por eso el contexto se arma antes y se destruye después.
- **`std::span<const std::uint8_t>`.** Un puntero y un largo, sin dueño: `glTexImage2D` recibe
  `data()`, y el largo se compara antes con lo que GL va a leer (`RequiredUnpackBytes`), porque GL
  no lo sabe.
- **`glm::mat4`, `glm::ortho` y `glm::value_ptr`.** glm guarda la matriz en column-major como GL:
  `m[columna][fila]`, y `value_ptr(m)` da los 16 floats en el orden que espera
  `glUniformMatrix4fv` con `GL_FALSE`.
- **Una X-macro.** Una lista `X(tipo, miembro, "glNombre")` que se expande dos veces: una arma los
  miembros del struct y la otra el lazo que los carga con `SDL_GL_GetProcAddress`. Sumar una
  función de GL es sumar una línea. Es la pregunta 5 del plan; el lab tiene una chica
  (`gl/LabGl.h`).
- **`offsetof` en `glVertexAttribPointer`.** El último argumento es un desplazamiento dentro del
  VBO que la API recibe como puntero: `reinterpret_cast<const void*>(offsetof(Vertex, u))`.
- **`std::bit_cast` en un `static_assert`.** `std::bit_cast<std::uint32_t>(1.0f / 640)` es
  `constexpr`: el compilador compara la constante con la del binario antes de que corra nada.

## Ejemplo mínimo (fuera del proyecto)

La forma de onda de un `.wav` como una línea, con un buffer de GL con dueño:

```cpp
// Un nombre de buffer de GL con dueño: se borra una vez, se mueve y no se copia.
class ScopedBuffer
{
public:
    explicit ScopedBuffer(const Gl& gl) : m_gl(&gl) { gl.genBuffers(1, &m_name); }
    ~ScopedBuffer()
    {
        if (m_name != 0)
        {
            m_gl->deleteBuffers(1, &m_name);
        }
    }
    ScopedBuffer(ScopedBuffer&& other) noexcept
        : m_gl(other.m_gl), m_name(std::exchange(other.m_name, 0))
    {
    }
    ScopedBuffer(const ScopedBuffer&) = delete;
    ScopedBuffer& operator=(const ScopedBuffer&) = delete;
    ScopedBuffer& operator=(ScopedBuffer&&) = delete;
    GLuint Get() const { return m_name; }

private:
    const Gl* m_gl;
    GLuint m_name = 0;
};

// x es el tiempo y y la amplitud, ya en el NDC: de −1 a 1 los dos.
void UploadWaveform(const Gl& gl, const ScopedBuffer& buffer, std::span<const float> samples)
{
    std::vector<float> points;
    points.reserve(samples.size() * 2);
    const auto last = static_cast<float>(samples.size() - 1);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        points.push_back(-1.0f + 2.0f * static_cast<float>(i) / last);
        points.push_back(samples[i]);
    }
    gl.bindBuffer(GL_ARRAY_BUFFER, buffer.Get());
    const auto bytes = static_cast<GLsizeiptr>(points.size() * sizeof(float));
    gl.bufferData(GL_ARRAY_BUFFER, bytes, points.data(), GL_STREAM_DRAW);
}
```

**Preguntas:** ¿qué hace `UploadWaveform` con un `.wav` de una sola muestra? ¿Y qué pasaría si
`ScopedBuffer` tuviera la copia que da el compilador por defecto?

## Errores típicos

- Dar vuelta la y dos veces: en los vértices (`H − y`) y en la matriz, o con un `glTexImage2D`
  "corregido" (actividad 13).
- Subir una textura RGB de 1 o 2 de ancho con `GL_UNPACK_ALIGNMENT` en 4 (actividad 13).
- Dejar el filtro de minificación por defecto en una textura sin mipmaps (actividad 10).
- Pasar la matriz en row-major con `GL_FALSE`, o en column-major con `GL_TRUE` (actividad 13).
- Escalar con `x * W / 640` y creer que da lo mismo que el original (actividad 5).
- Restar el borde de abajo del float de arriba ya guardado (actividad 14).
- Reordenar quads con mezcla normal para ahorrar cambios de textura (actividad 12).
- Borrar un nombre de GL sin el contexto vivo, o copiarlo y borrarlo dos veces.
- Pedir `glBegin` o `GL_QUADS` en un contexto core: no existen.

## Cómo lo hacía el original

- Solo OpenGL 1.1 en modo inmediato: cada vértice con su color, su coordenada de textura y su
  `glVertex3fv`; unas 285.000 llamadas y 95.000 vértices por frame (06, *verificado*).
- El 2D con `gluOrtho2D(0, W, 0, H)` en píxeles reales (`0x6219b0`), y la y dada vuelta en cada
  vértice, `H − y` (`0x621b60`). Cada quad, un `GL_TRIANGLE_FAN` de 4 vértices.
- Lo escalado desde 640 × 480, cada eje por su lado (`0x620d50`, `0x620d80`), con la x87; el
  pase 3D, en un viewport que sale de un rectángulo de 640 × 480 en enteros (`0x620db0`,
  `0x620d10`).
- Las texturas en buffers de lados potencia de 2 (CL1.4), con el filtro y el wrap que pide cada
  una, y `GL_CLAMP` (`0x63b870`, `0x63bd00`).
- El alpha test `GL_GREATER 0,25` con la mezcla normal, apagado con la aditiva (06).

## Para leer

- Joey de Vries, *LearnOpenGL* (learnopengl.com): "Hello Triangle", "Shaders", "Textures",
  "Coordinate Systems", "Face culling", "Blending" y "Framebuffers".
- Las páginas de manual de OpenGL (registry.khronos.org/OpenGL-Refpages): `glOrtho` y
  `gluOrtho2D`, `glViewport`, `glPixelStorei`, `glTexImage2D`, `glTexParameter`, `glBlendFunc` y
  `glReadPixels`.
- La especificación de OpenGL 3.3 core, el apéndice E: lo que se sacó del perfil core.
- glm, en Context7 (`/g-truc/glm`): `glm::ortho`, `glm::value_ptr` y el manual.
- Bruce Dawson, "Intermediate Floating-Point Precision" (randomascii): la x87, su precisión de 53
  bits en Windows y por qué una misma cuenta da otro float según dónde se redondea.

## Preguntas para chequearte

1. ¿Qué hace un vertex shader que antes hacía la función fija? ¿Y un fragment shader?
2. ¿Qué es el NDC, y cómo se pasa de él a un píxel con el viewport?
3. ¿Por qué el original da vuelta la y en cada vértice, y qué pasa si MuRet la da vuelta también
   en la matriz?
4. ¿Qué espera `glUniformMatrix4fv` con `GL_FALSE`, y dónde está la traslación de una matriz en
   column-major?
5. ¿Cuáles son los triángulos de un fan de n vértices, y por qué un lote los pasa a triángulos?
6. ¿Qué hace `GL_UNPACK_ALIGNMENT`, y con qué texturas del original importa?
7. ¿Por qué el orden de dibujo importa con la mezcla normal y no con la aditiva?
8. ¿Por qué `ScaleY(25)` en 1024 × 768 no da 40 justos, y por qué el viewport de `0x620db0` sí da
   enteros?
9. ¿Qué tiene que estar vivo para borrar un nombre de GL, y cómo lo asegura el orden de los
   miembros de una clase?

## Al cerrar la CL5.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Qué hace mi `OriginalQuad` con las banderas, y en qué tipo hace las cuentas:**
- **Qué vi en el jefe: el píxel del original y el de MuRet, y el viewport en 800 × 600:**
- **Lo que anoté al comparar 95.000 vértices de a uno con un VBO, y el orden con mezcla:**
- **Qué rompí en la actividad 13 y qué se vio:**
- **Preguntas que me quedaron para más adelante:**
