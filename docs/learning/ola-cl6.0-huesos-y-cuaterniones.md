# Ola CL6.0: huesos y cuaterniones

La N.0 de la CL6 del cliente. Antes de leer los `.bmd`, posar el esqueleto de `Player.bmd` y
dibujar a los cinco personajes de la selección, hace falta saber cómo se gira un punto en 3D, por
qué el original guarda los giros como cuaterniones y no como ángulos, cómo se interpola entre dos
keys, cómo cuelga un hueso de su padre y qué hace el skinning rígido con un vértice. Plan (tiene
las respuestas: abrilo después):
[CL6](../superpowers/plans/2026-11-03-ola-cl6-modelos-y-seleccion.md#sub-ola-cl60-huesos-y-cuaterniones-n0-en-labs).

**Resuelta por Claude** el 2026-11-03, a tu pedido: el código está en
[`labs/cl6.0-huesos/`](../../labs/cl6.0-huesos/), y las respuestas de las 14 actividades, con las
salidas reales, en su [README](../../labs/cl6.0-huesos/README.md). Si la hacés vos, abrilo después
de cada actividad, no antes. La sección final de esta guía sigue siendo tuya.

Qué cubre:

- **Teoría:**
  - rotaciones en 3D: ángulos de Euler, el orden en que se componen (Rz · Ry · Rx en Half-Life),
    por qué interpolar ángulos da otro giro que el del medio, y el gimbal lock;
  - cuaterniones: un eje y medio ángulo, cómo se arman desde ángulos, cómo dan una matriz, por qué
    q y −q son el mismo giro, y slerp contra el lerp normalizado;
  - una jerarquía de huesos: cada hueso relativo a su padre, la matriz del mundo como producto de
    la cadena, y por qué el padre tiene que estar calculado antes que el hijo;
  - skinning rígido: un vértice guardado en el espacio de su hueso y llevado al mundo con la
    matriz de ese hueso; la normal, solo con el giro;
  - matrices de 3 × 4 por filas (Half-Life) contra matrices por columnas (glm, GL), y el frame
    fraccionario: entre qué dos keys se interpola y con qué peso.
- **C++:**
  - `glm::quat`, `glm::mat3_cast`, `glm::mat4x3` y sus índices `[columna][fila]`;
  - `glm::quat::wxyz`, para que el orden de las componentes se lea;
  - comparar floats con tolerancia (`EXPECT_NEAR`) y por qué no `EXPECT_FLOAT_EQ` después de una
    cadena de productos;
  - `std::numbers::pi_v<float>` (C++20), `std::nextafter` y un literal de float en hexadecimal
    (`0x1p-24`);
  - `std::optional` para una cadena que puede venir rota, y `std::span` para recibirla.
- **Lectura:** Ken Shoemake, "Animating Rotation with Quaternion Curves"; el `mathlib.c` del
  SDK de Half-Life (el studiomdl y el visor de modelos); *3D Math Primer for Graphics and Game
  Development*, los capítulos de orientación; glm, en Context7.

## Mapa de la sesión

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | calentamiento | girar un punto dos veces, en los dos órdenes |
| 2 | idea | ángulos de Euler, su orden y por qué no se interpolan |
| 3 | idea | cuaterniones: un eje y medio ángulo |
| 4 | misión | `Labs::AngleQuaternion` y `Labs::QuaternionMatrix`, contra glm |
| 5 | detective de números | dos π/180 distintos y un π/2 de un float |
| 6 | misión | `Labs::HalfLifeSlerp`, `SlerpBranch` y `AlmostEqual` |
| 7 | idea | el slerp de glm contra el de Half-Life |
| 8 | idea | una cadena de huesos, el skinning rígido y filas contra columnas |
| 9 | misión | `Labs::PoseChain`, `Transform`, `Rotate`, `AngleMatrix` y `ObjectMatrix` |
| 10 | idea | el frame fraccionario: entre qué dos keys, por paso y por frame |
| 11 | jefe | entre dos keys, y en qué key está el original |
| 12 | comparar | slerp, lerp normalizado y ángulos, con los números del jefe |
| 13 | rompelo | el orden de glm, la matriz transpuesta, grados por radianes |
| 14 | detective (carril R) | `0x5fcd20`, `0x5fce90` y `0x5fc700` en el desensamblado |

Cada actividad termina en una pregunta. Las respuestas no están en la guía: salen de lo que
corrés. El plan, el [05](../../Client/docs/research/05-formatos-de-datos.md#modelos-bmd) y el
[06](../../Client/docs/research/06-render-escenas-y-ui.md) tienen algunas, así que abrilos
después de predecir.

## 1. Calentamiento: dos giros en los dos órdenes

El punto (1, 0, 0). Dos giros de 90°: uno alrededor de z (lleva x a y) y otro alrededor de x
(lleva y a z). Con la mano derecha: el pulgar en el eje, los dedos dicen hacia dónde gira.

**Predecí, antes de seguir:**
- primero z y después x: ¿dónde queda el punto?
- primero x y después z: ¿y ahora?
- ¿qué giro hace falta para volver al (1, 0, 0) desde cada uno?

**Pregunta:** si un modelo guarda "90° en x y 90° en z", ¿alcanza con eso para saber dónde queda
un vértice? ¿Qué más hace falta?

## 2. Idea: ángulos de Euler, su orden y por qué no se interpolan

Tres ángulos, uno por eje, son la manera más corta de escribir un giro, y la que usa el `.bmd`:
cada key de cada hueso trae una posición y tres ángulos en radianes (05). Pero tres ángulos no
dicen nada sin el **orden**. En Half-Life (y en el original, que usa su matemática) es
**Rz · Ry · Rx**: aplicado a un vector columna, primero gira alrededor de x, después de y y al
final de z, siempre en los ejes fijos.

Dos problemas:

- **Interpolar ángulos no es interpolar giros.** Entre 170° y −170° alrededor de z hay 20° de
  diferencia, pasando por 180°. El promedio de los números da 0°: la vuelta larga, por 90°. Con
  los tres ejes a la vez, el camino que dibujan los ángulos interpolados tampoco es el más corto
  entre los dos giros, aunque no haya vuelta.
- **Gimbal lock.** Con el ángulo del medio (y) en 90°, el giro de x y el de z caen sobre el mismo
  eje: cambiar los dos en lo mismo no cambia nada, y se pierde un grado de libertad. Cerca de
  90°, un cambio chico del giro pide un cambio grande de los ángulos.

**Pregunta:** con y en 90°, ¿(0,3; 90°; 0,1) y (0,4; 90°; 0,2) son el mismo giro? ¿Y
(0,5; 90°; 0,1)? ¿De qué combinación de x y z depende?

## 3. Idea: cuaterniones, un eje y medio ángulo

Un giro de θ alrededor de un eje unitario e es el cuaternión

```text
q = (x, y, z, w) = (e · sin(θ/2), cos(θ/2))
```

con largo 1. El original lo guarda en ese orden, x, y, z y w, como Half-Life. Tres cosas que
salen de ahí:

- **q y −q son el mismo giro.** −q es θ + 2π alrededor del mismo eje. La matriz del giro tiene
  solo productos de dos componentes, así que el signo se va. Al interpolar no da lo mismo: de p a
  q y de p a −q son dos caminos, uno corto y uno largo.
- **Desde ángulos:** `AngleQuaternion` arma los tres cuaterniones de cada eje (un seno y un
  coseno del medio ángulo cada uno) y los multiplica en el orden Rz · Ry · Rx. Quedan cuatro
  sumas de productos de tres factores.
- **De cuaternión a matriz:** `QuaternionMatrix` da la de 3 × 3, con 1 − 2 (y² + z²) en la
  diagonal y productos cruzados afuera.

Interpolar dos cuaterniones por el arco del círculo, a velocidad pareja, es el **slerp**:

```text
slerp(p, q, t) = sin((1 − t)·ω)/sin ω · p + sin(t·ω)/sin ω · q,   con cos ω = p·q
```

El **lerp normalizado** (nlerp) hace la recta y la vuelve a llevar al largo 1: el mismo camino, a
otra velocidad.

**Pregunta:** ¿qué cuaternión es media vuelta alrededor de x? ¿Y una vuelta entera alrededor de z?

## 4. Misión: `Labs::AngleQuaternion` y `Labs::QuaternionMatrix`

**El contrato:** `Quat AngleQuaternion(Vec3 radians)` es `0x5fcd20`, y `Matrix34
QuaternionMatrix(Quat)` es `0x5fcdb0`, con `struct Quat { float x, y, z, w; }` (w en 1 por
defecto, la identidad), `Vec3 = std::array<float, 3>` y `Matrix34 =
std::array<std::array<float, 4>, 3>`, la 3 × 4 por filas de Half-Life (m[fila][columna], la
traslación en la columna 3, en 0 acá). Van en `labs/cl6.0-huesos/Quaternion.h/.cpp`.

**Te toca**, en este orden:

1. Los tests primero (`test_quaternion.cpp`): `QuaternionTest.AQuarterTurnAroundZ` ((0, 0, π/2) da
   (0, 0, √½, √½)), `MatchesGlmForAnyAngles` (una grilla de −π a π cada π/8 en los tres ejes,
   contra `glm::quat(glm::vec3)`), `TheMatrixIsTheSameRotation` (contra `glm::mat3_cast`, dando
   vuelta filas y columnas), `QAndMinusQAreTheSameMatrix` (bit a bit) y
   `GlmTakesTheWFirst`. Antes de compilar, decí cuál va a fallar primero.
2. `Quaternion.h` y `Quaternion.cpp`, primero en rojo y después en verde.

**Decisiones que pesás vos:**
- ¿en qué tipo hacés las cuentas: `float` como glm, o `double` como la x87? ¿Dónde redondeás?
  (La actividad 14 mira dónde redondea el original.)
- ¿qué tolerancia usás contra glm, y por qué no `EXPECT_FLOAT_EQ`?
- ¿cómo escribís el índice de la matriz para que el test contra glm se lea?

Construís, comparás y usás el estándar (L-3): la CL6.2 arma los cuaterniones con glm, no con tu
`AngleQuaternion`.

## 5. Detective de números: dos π/180 distintos y un π/2 de un float

El original pasa de grados a radianes con un float. Hay dos: el de `0x6ba360` (lo usa
`AngleMatrix`, `0x5fc8f0`, para los ángulos del objeto) y el de `0x6ba140` (lo usa `0x4eb860` para
el giro de la cabeza). Y el slerp usa un double en `0x6ba720` para π/2.

**Te toca:**
- leer los bytes (`objdump -s -j .rdata --start-address=0x6ba140 --stop-address=0x6ba144`, y lo
  mismo con los otros dos) y compararlos con `std::bit_cast<std::uint32_t>` de
  `static_cast<float>(std::numbers::pi / 180)`, de `std::numbers::pi_v<float> / 180` y de
  `std::numbers::pi_v<float> * (1.0f / 180)`;
- **predecir** el double de `0x6ba720`: ¿es π/2 en double, o π/2 de un float guardado en double?
- calcular cuánto cambia un giro de 360° entre los dos π/180.

**Pregunta:** ¿de qué cuenta del código fuente puede salir cada uno? ¿Se ve la diferencia en un
personaje?

## 6. Misión: `Labs::HalfLifeSlerp`, `SlerpBranch` y `AlmostEqual`

**El contrato:** `Quat HalfLifeSlerp(Quat p, Quat q, float t)` es `0x5fce90`, el `QuaternionSlerp`
de Half-Life:

```text
a = Σ(p − q)²,  b = Σ(p + q)²;  si a > b, q = −q
cos = p·q
si 1 + cos > 1e-8:
    si 1 − cos > 1e-8:  ω = acos(cos);  sp = sin((1 − t)·ω)/sin ω;  sq = sin(t·ω)/sin ω
    si no:              sp = 1 − t;  sq = t
    qt = sp·p + sq·q
si no (p y q opuestos):
    qt = (−p.y, p.x, −p.w, p.z);  sp = sin((1 − t)·π/2);  sq = sin(t·π/2)
    qt[i] = sp·p[i] + sq·qt[i]  para i = 0, 1, 2
```

`SlerpCase SlerpBranch(Quat p, Quat q)` dice qué rama toma (`Sines`, `Linear` u `Opposite`), y
`SlerpBranchForCosine(double)`, lo mismo desde el cos. `bool AlmostEqual(Quat, Quat)` es
`0x5fc700`: las cuatro componentes a 0,001 o menos. Con dos keys así, `0x4eb860` copia la
primera y no llama al slerp. Van en `Slerp.h/.cpp`.

**Te toca**, tests primero (`test_slerp.cpp`):

1. `SlerpTest.HalfWayIsTheMiddleRotation`: entre 0 y un cuarto de vuelta, el octavo.
2. `TheFlipIsGlmsForUnitQuaternions`: con 1.331 pares sin azar, lo mismo que `glm::slerp`.
3. `GoesLinearOnlyBelowOneInAHundredMillion` y `EachComparisonSeesItsOwnCosine`: qué cos va por
   cada rama.
4. `ThePerpendicularCaseNeverRunsAfterTheFlip` y `OnlyANanReachesTheOppositeBranch`.
5. `AlmostEqualKeysAreTheSameKey` y `TheToleranceIsTheDoubleOneThousandth`.
6. La implementación.

**Decisiones que pesás vos:**
- el original da vuelta el q **del llamador**, por puntero. ¿Lo copiás o lo cambiás? ¿Qué pasa si
  el llamador usa q después?
- ¿normalizás el resultado? El original no.
- ¿cómo armás un cuaternión cuyo cos con la identidad sea el float de abajo de 1?

## 7. Idea: el slerp de glm contra el de Half-Life

glm 1.0.3 (`ext/quaternion_common.inl`, visto con Context7 y en el código) hace:

```text
z = q;  cos = dot(x, y)
si cos < 0: z = −q, cos = −cos
si cos > 1 − epsilon<float>(): lineal (mix de cada componente)
si no: (sin((1 − a)·ω) · x + sin(a·ω) · z) / sin ω
```

Tres diferencias con el de Half-Life:

- **La vuelta.** glm mira el signo del producto; Half-Life compara Σ(p − q)² con Σ(p + q)².
  Desarrollá los cuadrados: ¿qué queda de la resta?
- **El umbral de lo lineal.** glm pasa a lineal con cos > 1 − 2⁻²³. Half-Life, con
  1 − cos ≤ 1e-8, que en double es mucho más chico. Pero el cos que compara ya está en float.
- **El caso de los opuestos.** glm no lo tiene: con la vuelta, no le hace falta. ¿Y a Half-Life?

**Pregunta:** ¿para qué pares de cuaterniones dan distinto los dos? ¿Se ve en un personaje?

## 8. Idea: una cadena de huesos, el skinning rígido y filas contra columnas

Un esqueleto es un árbol: cada hueso tiene un padre (la raíz, −1) y su posición y su giro son
**relativos al padre**. La matriz de un hueso en el mundo es la del padre en el mundo por la suya:

```text
mundo(hueso) = mundo(padre) · local(hueso)        mundo(raíz) = objeto · local(raíz)
local = [ QuaternionMatrix(giro) | posición ]      (3 × 4, con la fila 0 0 0 1 abajo)
```

Por eso el padre tiene que estar calculado antes que el hijo: el original recorre los huesos del
0 al último y usa la matriz del padre ya escrita (`0x4eb860`). La del objeto la arma
`AngleMatrix` con sus ángulos en **grados** (`0x5fc8f0`), por la escala del objeto, con su posición
en la columna 3.

**Skinning rígido:** cada vértice de una malla guarda el índice de un hueso y su posición en el
espacio de ese hueso (05). Dibujarlo es `Transform(mundo(hueso), vértice)`: el giro y la
traslación. Una normal es una dirección: solo `Rotate`, sin la traslación. No hay pesos: un
vértice, un hueso.

**Filas contra columnas.** Half-Life guarda m[fila][columna], con la traslación en la columna 3 de
cada fila. glm guarda m[columna][fila]: `glm::mat4x3` son 4 columnas de 3, y la traslación es
`m[3]`. Copiar los índices tal cual da la transpuesta.

**Pregunta:** si un archivo trae un hueso cuyo padre viene después, ¿qué usa el original como
matriz del padre? ¿Qué hacés vos?

## 9. Misión: `Labs::PoseChain`, `Transform`, `Rotate`, `AngleMatrix` y `ObjectMatrix`

**El contrato** (`Bones.h/.cpp`):

- `struct Bone { int parent = -1; Vec3 position{}; Quat rotation{}; };`
- `Matrix34 Concat(const Matrix34& a, const Matrix34& b)`: a · b, como `0x5fca90`
  (`R_ConcatTransforms`);
- `Matrix34 AngleMatrix(Vec3 degrees)`: `0x5fc8f0`, con el π/180 de `0x6ba360`;
- `Matrix34 ObjectMatrix(Vec3 degrees, float scale, Vec3 position)`: la del objeto;
- `Matrix34 LocalMatrix(const Bone&)`;
- `std::optional<std::vector<Matrix34>> PoseChain(std::span<const Bone>, const Matrix34& object)`:
  la matriz de cada hueso en el mundo, o `nullopt` si un padre no está antes;
- `Vec3 Transform(const Matrix34&, Vec3)` (`0x5fccc0`) y `Vec3 Rotate(const Matrix34&, Vec3)`
  (`0x5fcbd0`).

**Te toca**, tests primero (`test_bones.cpp`): `BonesTest.AChildFollowsItsParent` (un brazo de tres
huesos con el hombro girado 90°: el codo en (0, 10, 0), la mano en (0, 20, 0)),
`TheRootHangsFromTheObject` (el objeto en (100, 0, 0), y después girado 90° con escala 2),
`RowsAgainstColumns` (la misma matriz en `glm::mat4x3`), `TheNormalOnlyRotates`,
`AParentAfterItsChildIsRefused` y `AngleMatrixIsTheQuaternionRotation`. Después, la
implementación.

**Decisiones que pesás vos:**
- ¿un padre que viene después es un error, o lo dejás pasar como el original?
- ¿la escala del objeto multiplica la traslación de los huesos? ¿Debería?

## 10. Idea: el frame fraccionario, por paso y por frame

Una acción tiene n keys. El frame es un float de 0 a n: la parte entera es la key, y la
fraccionaria, cuánto se avanzó hacia la siguiente. El original lo avanza en cada paso de lógica
de 40 ms con la velocidad de la acción (`0x4ec200`: 0,26 keys por paso en la acción 3), y cuando
la parte entera cambia guarda la key vieja como **anterior**. Al pasar la última, vuelve con el
módulo.

La pose (`0x4eb860`) interpola de la key **anterior** a la de **ahora**, ⌊frame⌋, con la parte
fraccionaria del frame como peso. No de ⌊frame⌋ a ⌊frame⌋ + 1.

El 06 encontró los frames de la selección ajustando la pose: 1,737, 3,320, 4,896, 6,739 y 1,578,
entre ⌊f⌋ y ⌊f⌋ + 1.

**Predecí:** con la tabla del 06 en 1,737, ¿qué frame tiene guardado el original, y qué key
anterior? ¿Cuántos pasos de 0,26 hay entre 1,737 y 3,320, módulo 7?

## 11. Jefe: entre dos keys, y en qué key está el original

**Te toca** (`labs/cl6.0-huesos/jefe/test_original_pose.cpp`), con lo de las actividades 4, 6 y 9:

1. `OriginalPoseTest.SlerpBeatsEulerBetweenTwoKeys`: un hueso con dos keys alrededor de z, 170° y
   −170°. A mitad de camino, ¿dónde queda la punta (1, 0, 0) con el slerp, y con los ángulos
   interpolados? ¿Y a un cuarto?
2. `NlerpHidesBelowTheTraceErrorAndEulerDoesNot`: un vértice a 50 de su hueso, entre dos keys de
   6,9° de diferencia y entre dos de 33°. ¿Cuánto se aparta del slerp con el lerp normalizado y
   con los ángulos? ¿Y comparado con los 0,015 de la selección en el 06?
3. `TheOriginalStoresTheNextKey`: con los cinco frames de la tabla, el estado del original (el
   frame y la key anterior) y la lectura de la tabla dan la misma pose. ¿Qué pose da el frame del
   original leído como la tabla?
4. `TheJumpsAreStepsOfTheAction`: desde el primer frame, 33, 33, 34 y 34 pasos de 0,26 caen en
   los otros cuatro.

**Pregunta:** si MuRet guarda el frame como la tabla del 06 y no como el original, ¿qué cambia en
la pantalla? ¿Y en la comparación con las trazas de la CL6.2?

## 12. Comparar: slerp, lerp normalizado y ángulos

El 05 dice que "slerp y nlerp no se distinguen en la traza; interpolar los ángulos da peor". Con
los números del jefe (paso 2): ¿por qué? ¿Vale para cualquier par de keys, o depende de qué tan
lejos estén? ¿Cuánto cuesta cada uno: cuántos senos, cuántas raíces?

**Pregunta:** si el skinning de la CL6.2 usara nlerp en vez del slerp del original, ¿lo
encontraría el test contra la traza? ¿Por qué igual conviene el del original?

## 13. Rompelo: tres maneras de retorcer al personaje

1. **El orden de glm.** Armá el `glm::quat` con `glm::quat(q.x, q.y, q.z, q.w)`, el orden del
   original. **Predecí** dónde va (1, 0, 0) con un cuarto de vuelta alrededor de z.
2. **La matriz transpuesta.** Con el objeto en (100, 0, 0) y girado 90° alrededor de z, pasá los
   12 floats de la 3 × 4 tal cual (`std::memcpy`) a una `glm::mat3x4` y a una `glm::mat4x3`, y
   multiplicá (5, 0, 0). **Predecí** dónde cae con cada una.
3. **Grados por radianes.** Pasale 90 a `AngleQuaternion` como si fueran grados, y π/2 a
   `AngleMatrix` como si fueran radianes. ¿Cuánto gira cada uno?

**Preguntas:**
- ¿cuál de los tres compila sin un aviso? ¿Cuál lo encuentra un test sin GL?
- ¿cuál se vería como un personaje "casi bien"?

## 14. Detective (carril R): el slerp y el cuaternión en el binario

Están en el repo de research,
`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/` (`005fcd20_FUN_005fcd20.asm`,
`005fce90_FUN_005fce90.asm`, `005fc700_FUN_005fc700.asm` y sus `.c`), y con
`objdump -d -M intel --start-address=...` sobre `mainexe-vmxp-c32bbd5f/original/main.exe`:

```text
5fcd29 fld   dword ptr [eax+0x8]
5fcd2c fmul  qword ptr [0x6ba718]
5fcd32 fld   st(0)
5fcd34 fsin
5fcd36 fstp  dword ptr [ebp+0x8]
5fcd39 fcos

5fcf0e faddp st(1),st
5fcf10 fst   dword ptr [ebp+0xc]
5fcf13 fadd  qword ptr [0x6b8d60]
5fcf19 fcomp qword ptr [0x6ba728]
5fcf26 fld   dword ptr [ebp+0xc]
5fcf29 fsubr qword ptr [0x6b8d60]
5fcf2f fcomp qword ptr [0x6ba728]

5fc713 fabs
5fc715 fcomp qword ptr [0x6b8a88]
5fc71b fnstsw ax
5fc71d test  ah,0x41
5fc720 je    0x5fc733
```

**Preguntas:**
- en `0x5fcd20`, ¿qué se guarda en un float y qué queda en la pila sin redondear: los senos o los
  cosenos?
- en `0x5fce90`, `FST` contra `FLD`: ¿la primera comparación usa el cos redondeado a float o el
  de la pila? ¿Y la segunda? ¿Qué dice el decompilado (`005fce90_FUN_005fce90.c`)?
- `test ah,0x41` mira C0 y C3 del estado de la FPU: ¿qué pasa con un NaN? ¿A qué rama del slerp
  va un cuaternión con un NaN?
- en `0x5fc700`, ¿la igualdad es con `<` o con `≤`? ¿Importa con floats?

## El C++ que entra

- **`glm::quat` y su orden.** Guarda x, y, z, w como el original, pero `glm::quat(a, b, c, d)` es
  (w, x, y, z): el constructor de cuatro recibe la w primero (salvo con
  `GLM_FORCE_QUAT_DATA_XYZW`, que el cliente no usa). `glm::quat::wxyz(w, x, y, z)` lo dice en el
  nombre. `glm::quat(glm::vec3)` arma desde ángulos en radianes, con el orden de Half-Life.
- **Índices de glm.** `m[c]` es la columna c; `m[c][r]`, el elemento de la fila r.
  `glm::mat4x3` es de 4 columnas de 3 filas: la 3 × 4 de Half-Life transpuesta en memoria.
- **`EXPECT_NEAR` contra `EXPECT_FLOAT_EQ`.** `EXPECT_FLOAT_EQ` acepta 4 ulp del valor. Cerca de 0
  un ulp es mínimo, y una componente que debería dar 0 da 1e-8 después de unos productos: falla.
  Una tolerancia absoluta dice cuánto error se acepta en las unidades del problema.
- **`std::numbers::pi_v<float>`** es π redondeado a float, en tiempo de compilación (C++20,
  `<numbers>`). No es lo mismo dividirlo por 180 que redondear π/180 en double: la actividad 5.
- **`std::nextafter` y `0x1p-24`.** El float de al lado, y un literal exacto de 2⁻²⁴: para armar un
  caso en el borde sin depender de cómo se escribe un decimal.
- **`std::optional<std::vector<...>>` y `std::span<const Bone>`.** La cadena puede venir rota y
  eso no es una excepción, es un dato; el span recibe un `std::vector` o un arreglo sin copiarlo.

## Ejemplo mínimo (fuera del proyecto)

Una cámara de seguimiento que gira hacia donde mira el jugador sin dar la vuelta larga:

```cpp
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cmath>

// Gira la cámara una fracción del camino hacia el destino en cada frame. dt en segundos.
glm::quat FollowTurn(const glm::quat& camera, const glm::quat& target, float dt)
{
    constexpr float TurnsPerSecond = 4.0f;
    const float t = 1.0f - std::exp(-TurnsPerSecond * dt);
    return glm::normalize(glm::slerp(camera, target, t));
}

// Hacia dónde mira el jugador: un giro alrededor de z, en radianes.
glm::quat Heading(float radians)
{
    return glm::angleAxis(radians, glm::vec3(0.0f, 0.0f, 1.0f));
}
```

**Preguntas:** ¿qué pasa si el jugador pasa de 179° a −179°, con este código y con ángulos
interpolados? ¿Por qué `1 − exp(−k·dt)` y no `k·dt`? ¿Hace falta el `normalize`?

## Errores típicos

- Armar un `glm::quat` con `glm::quat(x, y, z, w)` (actividad 13).
- Pasar los floats de una 3 × 4 de Half-Life a una matriz de glm sin dar vuelta los índices: el
  giro sale al revés y la traslación se pierde, o todo se mezcla (actividad 13).
- Pasar grados donde van radianes o al revés: el `.bmd` en radianes, el objeto y la cabeza en
  grados (actividad 13).
- Interpolar los ángulos de dos keys en vez de los cuaterniones (el jefe).
- Usar `glm::slerp` y esperar la salida del original bit a bit (actividad 7).
- Calcular un hueso antes que su padre.
- Mover una normal con la traslación.
- Guardar el frame como la tabla del 06 y posar como el original, o al revés: una key de error.
- Comparar floats con `EXPECT_FLOAT_EQ` cerca de 0.

## Cómo lo hacía el original

- La matemática de Half-Life en la x87: `AngleQuaternion` (`0x5fcd20`), `QuaternionSlerp`
  (`0x5fce90`), `QuaternionMatrix` (`0x5fcdb0`), `R_ConcatTransforms` (`0x5fca90`),
  `AngleMatrix` (`0x5fc8f0`), `VectorTransform` (`0x5fccc0`) y `VectorRotate` (`0x5fcbd0`), con
  las cuentas en la pila y un redondeo a float al guardar.
- Los cuaterniones de cada key, una vez, al leer el `.bmd` (`0x4f13d0`); salvo la cabeza, que
  pasa de ángulos a cuaternión en cada pose, con el giro del objeto restado (`0x4eb860`).
- La pose de cada hueso entre la key anterior y la de ahora, con el slerp, salvo dos keys casi
  iguales (`0x5fc700`), que copian la primera; la posición, lineal.
- Cada vértice al mundo en la CPU con la matriz de su hueso, y las coordenadas del mundo a GL
  (06): ningún bloque de la traza tiene matriz propia.
- El frame por paso de 40 ms con la velocidad de la acción (`0x4ec200`), y la key anterior
  guardada cuando la parte entera cambia.

## Para leer

- Ken Shoemake, "Animating Rotation with Quaternion Curves" (SIGGRAPH 1985): el slerp, y por qué
  no se interpolan los ángulos.
- El `mathlib.c` del SDK de Half-Life (en `utils/common` y en el visor de modelos):
  `AngleQuaternion`, `QuaternionSlerp`, `QuaternionMatrix`, `R_ConcatTransforms` y
  `AngleMatrix`.
- Fletcher Dunn e Ian Parberry, *3D Math Primer for Graphics and Game Development*: los capítulos
  de orientación (ángulos de Euler, el gimbal lock, cuaterniones y slerp).
- Jonathan Blow, "Understanding Slerp, Then Not Using It": slerp contra nlerp, y cuándo da igual.
- glm, en Context7 (`/g-truc/glm`): `glm::quat`, `glm::slerp`, `glm::mat3_cast` y el manual.

## Preguntas para chequearte

1. ¿Qué es un ángulo de Euler, y por qué tres ángulos no alcanzan sin el orden?
2. ¿Por qué interpolar los ángulos de dos keys puede dar la vuelta larga?
3. ¿Qué guarda un cuaternión unitario, y por qué q y −q son el mismo giro?
4. ¿Qué hace el slerp que no hace el lerp normalizado, y cuándo no se nota?
5. ¿En qué se diferencian el slerp de glm y el de Half-Life, y cuándo dan distinto?
6. ¿Cómo se calcula la matriz del mundo de un hueso, y por qué el padre va antes?
7. ¿Qué hace el skinning rígido con un vértice, y qué con una normal?
8. ¿Dónde está la traslación en una 3 × 4 de Half-Life y en una `glm::mat4x3`?
9. ¿Entre qué dos keys posa el original, y por qué los frames del 06 son uno menos?

## Al cerrar la CL6.0 (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **En qué tipo hace las cuentas mi `AngleQuaternion`, y dónde redondea:**
- **Qué vi en el jefe: la vuelta larga, el lerp normalizado y la key anterior:**
- **Qué rompí en la actividad 13 y qué se vio:**
- **Preguntas que me quedaron para más adelante:**
