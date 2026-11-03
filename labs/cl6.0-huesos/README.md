# CL6.0: huesos y cuaterniones, resuelta

Ejemplo trabajado de la [guía de la CL6.0](../../docs/learning/ola-cl6.0-huesos-y-cuaterniones.md).
Lo resolvió Claude porque Emanuel pidió, el 2026-11-02, que Claude escriba todo hasta que él diga
que participa (la pregunta 1 del plan de la CL6). Las salidas son reales, con GCC 13.3 en Linux y
glm 1.0.3. Windows y MSVC, sin probar acá: el CI los compila y los corre, porque nada de esta
carpeta abre GL.

Para cerrar la CL6.0 en el [cuaderno](../../docs/learning/PROGRESS.md) falta lo tuyo, cuando
entres: contar con tus palabras las "Preguntas para chequearte" de la guía y completar su sección
final. Las respuestas de Claude están [al final](#preguntas-para-chequearte), para comparar
después de contarlas.

| Archivo | Qué es |
| --- | --- |
| `Quaternion.h` / `.cpp` | la misión de la actividad 4: `AngleQuaternion` (`0x5fcd20`) y `QuaternionMatrix` (`0x5fcdb0`), con `Quat`, `Vec3` y `Matrix34` |
| `Slerp.h` / `.cpp` | la misión de la actividad 6: el slerp de `0x5fce90`, la rama que toma y la igualdad de `0x5fc700` |
| `Bones.h` / `.cpp` | la misión de la actividad 9: la cadena de huesos, `Concat` (`0x5fca90`), `AngleMatrix` (`0x5fc8f0`), `Transform` (`0x5fccc0`) y `Rotate` (`0x5fcbd0`) |
| `test_*.cpp` | sus tests, de GoogleTest, contra glm |
| `jefe/test_original_pose.cpp` | el jefe: el slerp contra los ángulos y el lerp normalizado, y en qué key está el original |

Las tres misiones van en la biblioteca `MuRetLabsCl60`, sin GL y sin glm, y sus tests y el jefe
en `LabCl60Tests`, enlazado con glm solo para comparar, con el prefijo `LabCl60.` en CTest: son
23.

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug -R "LabCl60"
```

Los tests se probaron con 20 mutantes a mano. 15 los ponen en rojo: el signo de la vuelta, el lazo
de cuatro en los opuestos, el orden del perpendicular, el 1e-8, el cos redondeado en la primera
comparación, el cos sin redondear en la segunda, un peso del slerp, un signo en `AngleQuaternion`,
otro en `QuaternionMatrix` y otro en `AngleMatrix`, la traslación de `Concat`, la escala del
objeto, el padre que viene después, la traslación en `Rotate` y, en el jefe, la key anterior que no
se guarda. Quedan cinco en verde, y se sabe por qué:

- `>` por `≥` en `AlmostEqual` es equivalente: la resta de dos floats en double no da nunca el
  double 0,001 justo (actividad 14).
- En el jefe, la vuelta con `frame − 7` en vez del módulo, también: con menos de una key por paso,
  el frame no pasa de 8.
- El ω o el sin ω sin redondear a float, y un coseno redondeado en `AngleQuaternion`, cambian un
  ulp, menos de lo que separa al lab de glm. Eso lo ve recién la CL6.2, contra la traza.

## Cómo hacen las cuentas

Las tres misiones hacen las cuentas como la x87 del original, con 53 bits de mantisa (la de un
`double`, la precisión que pone el CRT de MSVC; *inferido* como en la CL5.0): en `double`, y
redondeando a `float` donde el binario guarda en memoria (`FST` o `FSTP` a un `DWORD`). La
actividad 14 dice dónde. Contra glm, que hace todo en float:

- `AngleQuaternion`: a 1,2e-7 o menos de `glm::quat(glm::vec3)` en los 4.913 giros de la grilla,
  y 953 iguales bit a bit.
- `QuaternionMatrix`: a 5,4e-7 o menos de `glm::mat3_cast`.
- `HalfLifeSlerp`: a 1,8e-7 o menos de `glm::slerp` en los 1.331 pares, con t en 0,25, 0,5 y
  0,737. Los 1.331 van por los senos.
- `AngleMatrix` en grados contra `QuaternionMatrix(AngleQuaternion(…))` en radianes: 2,7e-7.

Los tests piden 1e-6 (1e-5 el slerp contra glm): unas veces el error de cuenta de arriba, y muy
por debajo de lo que mueve un error de verdad (un signo, un eje cambiado, la rama equivocada).

## 1. Calentamiento

Primero z y después x: (1, 0, 0) va a (0, 1, 0) y después a (0, 0, 1). Primero x y después z:
(1, 0, 0) no se mueve con x y va a (0, 1, 0) con z. Con las matrices del lab, `Rx · Rz` (z
primero) da (0, 0, 1) y `Rz · Rx`, (0, 1, 0). Para volver, los giros inversos en el orden
inverso: −90° en x y después −90° en z para el primero.

**Respuesta.** No alcanza: hacen falta el **orden** (y si los ejes son fijos o se mueven con el
objeto) y el sentido de cada giro. Half-Life usa Rz · Ry · Rx con los ejes fijos: x primero.
`AngleMatrix(90, 0, 90)` lleva (1, 0, 0) a (0, 1, 0) y (0, 1, 0) a (0, 0, 1), lo mismo que
`Rz · Rx`.

## 2. Ángulos de Euler

Con y en 90°, (0,3; 90°; 0,1) y (0,4; 90°; 0,2) dan el mismo cuaternión,
(0,0705929; 0,7035742; −0,0705929; 0,7035742), y (0,5; 90°; 0,1) da otro,
(0,1404804; 0,6930117; −0,1404804; 0,6930117).

**Respuesta.** Son el mismo giro, y depende solo de x − z: 0,3 − 0,1 = 0,4 − 0,2. Con y en 90°,
el eje x ya girado cae sobre el z, y los dos ángulos giran alrededor del mismo eje: queda un grado
de libertad menos, el gimbal lock. Cerca de ahí, dos poses parecidas pueden tener ángulos muy
distintos, y interpolar los números no da lo del medio.

## 3. Cuaterniones

**Respuesta.** Media vuelta alrededor de x: θ = π, así que w = cos(π/2) = 0 y (x, y, z) =
(1, 0, 0) · sin(π/2): (1, 0, 0, 0). `AngleQuaternion(π, 0, 0)` da (1, 0, −0, −4,4e-8): la w no es
0 porque π en float no es π. Una vuelta entera alrededor de z: w = cos(π) = −1, (0, 0, 0, −1), que
es −1 por la identidad: el mismo giro que no girar, con la misma matriz, pero otro cuaternión.
`AngleQuaternion(0, 0, 2π)` da (0, −0, −8,7e-8, −1).

## 4. Misión: `AngleQuaternion` y `QuaternionMatrix`

Los cinco tests de `QuaternionTest` pasan. Antes de la implementación, el primero que falla es el
del enlace: sin el `.cpp`, `AngleQuaternion` no existe.

**Decisiones, y por qué:**
- **En `double`, redondeando donde guarda el binario.** `0x5fcd20` guarda los tres senos en un
  float y deja los cosenos en la pila (actividad 14): el lab hace lo mismo. Con todo en float,
  como glm, da lo mismo a un ulp; la diferencia queda para la CL6.2, que compara con la traza.
- **`EXPECT_NEAR` con 1e-6.** Una componente que vale 0 sale en 1e-8 después de unos productos, y
  `EXPECT_FLOAT_EQ` pide 4 ulp del valor: cerca de 0, un ulp es de 1e-45 y falla. Lo que importa es
  el error en las unidades del problema.
- **El índice contra glm:** dos lazos de `int` (los índices de glm son `glm::length_t`, un `int`), y
  `byHand[fila][columna]` contra `fromGlm[columna][fila]`.
- **q y −q, bit a bit:** cada elemento es un producto de dos componentes, con el signo cancelado
  antes de redondear: la misma matriz exacta.

## 5. Detective de números

```text
0x6ba360   35 fa 8e 3c   0x3c8efa35   static_cast<float>(π / 180)  y  pi_v<float> / 180
0x6ba140   36 fa 8e 3c   0x3c8efa36   pi_v<float> * (1.0f / 180)
0x6ba720   00 00 00 60 fb 21 f9 3f   1,5707963705062866: π/2 de un float, guardado en double
                                     (π/2 en double es 0x3ff921fb54442d18)
```

- El de `0x6ba360` es π/180 redondeado una vez: el float más cercano. El de `0x6ba140` está un ulp
  más arriba, 2⁻²⁹ ≈ 1,9e-9, y sale de multiplicar dos floats ya redondeados (π en float por
  1/180 en float): el segundo redondeo lo sube. De las cuentas probadas, es la única que da esos
  bits (*inferido*: el código fuente no está).
- El π/2 de `0x6ba720` es el de un float: `0.5 * Q_PI` con un π en float, guardado como double
  porque la cuenta del slerp está en double.
- En 360°, el ulp de diferencia son 360 · 1,9e-9 ≈ 6,7e-7 rad, 3,8e-5°.

**Respuesta.** Salen de dos maneras de escribir lo mismo en el código fuente, una con la división
hecha en double por el compilador y otra con dos floats. No se ve: a 100 unidades del centro, el
giro de una vuelta entera se corre 7e-5 unidades. Lo que sí importa es usar cada uno donde lo usa
el original, para comparar bits con la traza.

## 6. Misión: el slerp

Los ocho tests de `SlerpTest` pasan. Lo que dicen:

- **El umbral de lo lineal no hace nada en float.** El float más cercano a 1 por debajo es
  1 − 2⁻²⁴ = 0,99999994, y 1 − cos = 5,96e-8 ya pasa el 1e-8. El slerp va por los senos salvo con
  el cos en 1 o más (un cuaternión que no es unitario).
- **Cada comparación mira su cos** (`EachComparisonSeesItsOwnCosine`): 1 − 2e-8 es 1 en float y
  va lineal, aunque sin redondear 1 − cos = 2e-8 > 1e-8; −1 + 2e-8 es −1 en float, pero la primera
  comparación lo ve sin redondear, 2e-8 > 1e-8, y no va a los opuestos.
- **La 0,001 es un double** (`TheToleranceIsTheDoubleOneThousandth`): 0,001f es
  0,0010000000474974513 y no pasa; el float de abajo sí.

**Decisiones, y por qué:**
- **Copiar q.** El original lo da vuelta por puntero, pero en `0x4eb860` ese q es un cuaternión
  local que arma para cada hueso, así que no se nota. El lab lo recibe por valor: nadie de afuera
  ve el cambio.
- **No normalizar.** El original no lo hace, y los tests lo comparan con glm, que tampoco.
- **El cos de abajo de 1:** (0, 0, √((1 − c)(1 + c)), c) con c = `std::nextafter(1.0f, 0.0f)`; con
  la identidad, el producto es c justo.

## 7. glm contra Half-Life

- **La vuelta es la misma.** Σ(p + q)² − Σ(p − q)² = 4 · p·q, para cualquier par: dar vuelta q
  cuando Σ(p − q)² > Σ(p + q)² es darla con el producto negativo. Solo puede cambiar cerca de un
  producto de 0, por cómo redondea cada uno: Half-Life suma en la pila, glm en float. Con 0 justo,
  ninguno da vuelta.
- **El umbral es otro.** glm pasa a lineal con cos > 1 − 2⁻²³, o sea con el cos en float en
  1 − 2⁻²⁴ o en 1; Half-Life, solo en 1. En 1 − 2⁻²⁴ los dos giros están a 0,04°, y el resultado
  queda a 1e-7.
- **Los opuestos no le hacen falta a ninguno.** Después de la vuelta el producto es positivo, y
  1 + cos > 1e-8 siempre. En Half-Life la rama está, pero con cuaterniones de largo 1 no corre:
  solo con un NaN o un infinito (actividad 14), o con uno enorme, de largo 1e8, en el que el
  redondeo de las dos sumas esconde la vuelta.

**Respuesta.** Dan distinto a nivel de redondeo, 1,8e-7 en los pares del test, y en dos bordes
que no cambian el giro: el producto de casi 0 y el cos de casi 1. En un personaje no se ve. La
CL6.2 usa el de Half-Life igual (la pregunta 5 del plan): lo que se espera de esa función es la
salida del original.

## 8. Una cadena de huesos

**Respuesta.** El original no lo mira: usa lo que haya en el arreglo de matrices en el lugar del
padre (`param_2 + padre · 0x30`), que es lo del frame anterior o lo de otro modelo. El lab lo
rechaza con `nullopt`, porque una pose armada con basura no es una pose. Si los `.bmd` del
laboratorio traen todos los padres antes de los hijos lo dice la CL6.1, que los lee: si alguno no,
es un quirk nuevo.

## 9. Misión: la cadena y el skinning

Los seis tests de `BonesTest` pasan.

**Decisiones, y por qué:**
- **Un padre que viene después es un error**, también uno negativo que no es −1, o el hueso mismo.
  La lista vacía da una pose vacía.
- **La escala multiplica el giro del objeto** (las tres columnas, como `0x4eb860` con `+0x68`), y
  por eso también las posiciones de los huesos: pasan por la matriz del padre, que ya trae la
  escala. Es lo que hace falta: el 0,9 de la selección y el 1,6 del personaje del login agrandan o
  achican el esqueleto entero. La traslación del objeto no se escala.

## 10. El frame fraccionario

**Respuesta.** Con la tabla en 1,737, el original tiene el frame en 2,737 y la key anterior en 1:
posa de la 1 a la 2 con 0,737, lo mismo que la tabla. Entre 1,737 y 3,320 hay 1,583 keys más 7 de
una vuelta, 8,583, que son 33,01 pasos de 0,26: 33 pasos.

## 11. Jefe

Los cuatro tests de `OriginalPoseTest` pasan.

1. Entre 170° y −170°, a mitad de camino, el slerp deja la punta en (−1, 0, 0), 180°, y los
   ángulos en (1, 0, 0), 0°. A un cuarto, 175° contra 85°: a 90° uno del otro, la punta queda a
   √2.
2. Un vértice a 50 de su hueso, lo más lejos que se aparta del slerp entre t = 0 y 1:

   | Keys | Lerp normalizado | Ángulos interpolados |
   | --- | --- | --- |
   | (10°, 20°, 30°) a (12°, 24°, 36°), 6,9° | 0,00035 | 0,039 |
   | (30°, 40°, 50°) a (50°, 20°, 80°), 33° | 0,037 | 1,66 |

3. El estado del original y la tabla dan la misma pose a 1e-6. El frame del original leído como la
   tabla da la pose de una key más adelante: la punta a 50 queda entre 15,6 y 17,5 unidades de
   donde va.
4. Desde 2,737, 33, 33, 34 y 34 pasos de 0,26 dejan el frame, menos 1, a menos de 0,005 de cada
   uno de los otros cuatro de la tabla. La key anterior es siempre la de antes de la de ahora.

**Respuesta.** Si MuRet guarda el frame como la tabla y posa de ⌊f⌋ a ⌊f⌋ + 1, la pantalla de una
animación que ya corre no cambia: es el mismo instante con otro número. Cambian dos cosas. La
primera, lo que se compara con el original: el frame + 2 del fin adelantado (Q-C21), el estado
que lee la CL6.2 de la traza y cuándo se cruza cada key. La segunda, el cambio de acción: el
original posa desde la key anterior, que al cambiar de acción es la de la acción vieja (la guarda
`0x4ec200` junto con su acción). La primera key de una acción nueva, entonces, mezcla la pose de
la vieja con la key 0 de la nueva (*inferido* de `0x4ec200` y `0x4eb860`; la CL6.2 lo prueba). Con
la lectura de la tabla, ese paso no existe y el cambio salta. Por eso la CL6.2 guarda el estado
como el original.

## 12. Comparar

Está en
[`labs/README.md`](../README.md#cl60-slerp-lerp-normalizado-y-ángulos-interpolados),
con los números del jefe.

## 13. Rompelo

1. **`glm::quat(q.x, q.y, q.z, q.w)`** con un cuarto de vuelta alrededor de z,
   (0, 0, √½, √½): glm lo lee como w = 0, x = 0, y = √½ y z = √½, media vuelta alrededor de
   (0, 1, 1)/√2. (1, 0, 0) va a (−1, 0, 0) y (0, 1, 0) a (0, 0, 1). Lo pinta `GlmTakesTheWFirst`.
2. **La matriz transpuesta**, con el objeto en (100, 0, 0) girado 90°. Bien, (5, 0, 0) cae en
   (100, 5, 0).
   - Con los floats tal cual en una `glm::mat3x4`, da (0, −5, 0) y una cuarta componente de 500:
     el giro al revés, y la traslación a la w.
   - En una `glm::mat4x3` da (0, −4, 0): las columnas mezclan elementos de filas distintas.
3. **Grados por radianes:**
   - 90 como radianes en `AngleQuaternion` son 90 mod 2π = 116,62°: (1, 0, 0) va a
     (−0,448; 0,894; 0).
   - π/2 como grados en `AngleMatrix` son 1,57°: (1, 0, 0) va a (0,99962; 0,0274; 0), casi nada.

**Respuestas:**
- **Los avisos.** Ninguno de los tres da un aviso: son floats en lugares de floats.
- **Los tests sin GL.** Los encuentran los tres, porque comparan con valores conocidos y con glm.
- **El "casi bien".** El de π/2 como grados: un personaje casi quieto, con los huesos apenas
  girados, se ve "rígido" y no "roto". Los otros dos lo retuercen o lo mandan lejos.

## 14. Detective (carril R)

- **`0x5fcd20`:** los tres senos se guardan en float (`FSTP` a `[ebp+0x8]`, `[ebp-0x8]` y
  `[ebp-0x4]`) y los tres cosenos quedan en la pila sin redondear (`FCOS` deja el resultado en
  `st(0)`, y no hay `FST`). Los cuatro productos usan los senos desde memoria y los cosenos de la
  pila, y cada componente se redondea al guardarla. El medio ángulo es el ángulo en float por el
  double 0,5 de `0x6ba718`, en la pila.
- **`0x5fce90`:** `FST` en `0x5fcf10` guarda el cos en un float y lo **deja en la pila**. La
  primera comparación (`FADD 1`, `FCOMP 1e-8`) usa ese, sin redondear. La segunda (`0x5fcf26`)
  hace `FLD` del float guardado: el cos redondeado. El decompilado
  (`005fce90_FUN_005fce90.c`) pone un solo `fVar2` float para las dos y escribe
  `(float)_DAT_006ba728 < fVar2 + (float)_DAT_006b8d60`: los dos detalles quedan escondidos, y
  además convierte a float dos constantes que son double.
- **Un NaN:** `FCOMP` con un NaN da "sin orden" y prende C0, C2 y C3. `test ah,0x41` mira C0 y C3:
  con NaN no da 0, y el `JNE` de `0x5fcf24` va a la rama de los opuestos (`0x5fcf9f`). La vuelta
  tampoco corre: con NaN, las dos sumas son NaN y `a > b` es falso. Es lo que prueba
  `ANanOrAnInfinityReachesTheOppositeBranch`: la x y la y salen NaN, la z sale −√½ y la w se queda
  en p.z. Con un infinito pasa algo parecido: las dos sumas dan ∞, `∞ > ∞` es falso y el cos es −∞.
  Ninguno de los dos sale de un `.bmd` sano.
- **`0x5fc700`:** `JE` salta a "distintos" cuando C0 y C3 están en 0, o sea con la diferencia
  mayor que 0,001. La igualdad es con ≤. Con floats da lo mismo que `<`: la resta de dos floats en
  la pila es exacta o casi, y no cae nunca en el double 0,001 justo, que tiene 53 bits de mantisa.
  El mutante que la cambia queda en verde, y es equivalente.
- **`0x4ec200`:** el `FST` de `0x4ec249` guarda el frame nuevo en float y deja la suma en la pila,
  y el `__ftol` de `0x4ec24b` trunca esa suma, sin redondear. Si la suma queda apenas debajo de un
  entero y el float sube a ese entero, el cruce no se guarda nunca: durante esa key, la pose sale
  de la key de dos antes. Con 0,26 por paso pasa unas pocas veces en un millón de pasos, ninguna en
  el jefe (*Verificado* en el desensamblado; en pantalla, sin ver). Es un dato para la CL6.2, que
  guarda el estado como el original.

## Preguntas para chequearte

1. **Euler.** Tres giros seguidos, cada uno alrededor de un eje. Los mismos tres números en otro
   orden dan otro giro (actividad 1): por eso un ángulo de Euler es los tres números y el orden.
   En Half-Life la matriz es Rz · Ry · Rx: al vértice le aplica primero el giro en x, después el
   de y y al final el de z (`AngleMatrix` y `AngleQuaternion`).
2. **La vuelta larga.** Cada ángulo se interpola como un número, sin saber que es un ángulo: de
   170° a −170° hay 20° de giro, pero el número baja 340 y pasa por 0°. Y aunque no den la
   vuelta, con varios ejes que cambian juntos el camino de en medio no es el arco más corto entre
   los dos giros. En el jefe, a mitad de camino, el slerp está en 180° y los ángulos en 0°.
3. **Un cuaternión unitario.** El eje por el seno del medio ángulo en x, y y z, y el coseno del
   medio ángulo en w. −q es el ángulo θ + 2π alrededor del mismo eje: el mismo giro. La matriz
   tiene solo productos de dos componentes, así que q y −q dan la misma, bit a bit
   (`QAndMinusQAreTheSameMatrix`).
4. **Slerp contra lerp normalizado.** El slerp recorre el arco a velocidad pareja: con el peso t,
   el giro es t veces el ángulo entre las keys. El lerp normalizado recorre el mismo arco, más
   rápido en el medio y más lento en las puntas. La diferencia crece con el cubo del ángulo: entre
   keys a 6,9°, 0,00035 a 50 unidades del hueso, debajo de lo que mide la traza; a 33°, 0,037 (la
   comparación de
   [`labs/README.md`](../README.md#cl60-slerp-lerp-normalizado-y-ángulos-interpolados)).
5. **glm contra Half-Life.** Los dos dan vuelta q para ir por el camino corto: glm con el producto
   negativo, `0x5fce90` con Σ(p − q)² > Σ(p + q)², que para unitarios es lo mismo. glm hace todo
   en float y pasa a lineal con cos > 1 − 1,19e-7 (`epsilon<float>()`); Half-Life lo hace en la
   x87 y pasa a lineal con 1 − cos ≤ 1e-8, con el cos redondeado a float en esa comparación.
   Dan distinto entre esos dos topes (con el cos en `std::nextafter(1.0f, 0.0f)`, Half-Life usa
   senos y glm es lineal), en los redondeos (1,8e-7 en el lab) y con un NaN o un infinito, que en
   Half-Life van a la rama de los opuestos.
6. **La matriz del mundo de un hueso.** La de su padre por la suya: `Concat(padre, local)`, con
   la local hecha del cuaternión y la posición del hueso en la columna 3. La raíz cuelga de la
   matriz del objeto, con sus ángulos, su escala y su posición. El padre va antes porque su
   matriz tiene que estar calculada: recorriendo los huesos en orden, una sola pasada alcanza, y
   `PoseChain` rechaza la cadena que no cumple.
7. **El skinning rígido.** Cada vértice tiene un hueso (05) y está guardado en el espacio de ese
   hueso: la matriz del hueso lo gira y le suma la traslación (`Transform`). Una normal es una
   dirección: solo gira, sin la traslación (`Rotate`).
8. **La traslación.** En la 3 × 4 de Half-Life, `m[fila][columna]`, es la columna 3:
   `m[0][3]`, `m[1][3]` y `m[2][3]`. En una `glm::mat4x3`, cuatro columnas de tres que se leen
   `m[columna][fila]`, es `m[3]`. Los 12 floats van en otro orden en la memoria, y copiarlos tal
   cual da otra matriz (actividad 13).
9. **Las dos keys.** El original posa desde la key anterior, la que guardó `0x4ec200` cuando
   cambió la parte entera del frame, junto con su acción, hasta ⌊frame⌋, con la parte fraccionaria
   como peso (`0x4eb860`). En una animación que ya corre, la anterior es ⌊frame⌋ − 1: lo que se ve
   va una key atrás del frame. La tabla del 06 se ajustó leyendo la pose entre ⌊f⌋ y ⌊f⌋ + 1, y
   por eso sus frames dan uno menos que los del original.
