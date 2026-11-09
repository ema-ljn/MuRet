# 0014. Perfil matemático retenido de CreateAngle y las hojas Wave

- **Estado:** aceptado. Lo decidió Emanuel el 2026-11-09: el modelo portable y documentado (phi64
  es el atan64 bien redondeado, no FPATAN) y una aritmética propia en lugar de Boost.Multiprecision.
  La validación MSVC x64 Release del CI y la revisión del PR siguen pendientes antes del merge.
- **Fecha:** 2026-11-08; actualizado el 2026-11-09 (sin Boost.Multiprecision).
- **Alcance:** CL7, producto de `CreateAngle` y preparación numérica privada de dos senos Wave.

## Contexto

El perfil de [ADR0012](0012-perfil-fp-del-movimiento-original.md) distingue resultados retenidos
de stores binary32. R5-22, `(0,0)→(7,−11)`, devuelve `0x1.03c5021eae517p+5`; reducir phi a
binary64 antes de multiplicar devuelve `0x1.03c5021eae518p+5`. Ambos tienen el mismo store
binary32 en esta muestra. Eso no prueba divergencia de gameplay: algunos callers guardan
binary32 y otros suman yaw vivo antes de convertir a entero. Las Waves 871/990 también
distinguen un valor retenido que trunca a 0 de un seno double que llega a ±1.

Se reutilizan exclusivamente 1020 casos Wave y 38 casos CreateAngle PC53/RN archivados, con 35
cocientes/phi/productos observados. SHA256 del banco de valores racionales `references.json`:
`5bfd7be7e163c3a9637cd8656ff4f88d209dbd938ac82e5f11932d6d11de0c3f`.
Los esperados versionados son valores numéricos; no contienen registros, ejecutables ni volcados.
Las tablas faltantes de 0x4e93f0 y las premisas C0 de 0012 siguen abiertas.

## Decisión

`OriginalRetainedMath` evalúa un modelo matemático fijo: el seno, el coseno o el arcotangente del
argumento exacto, redondeado **una sola vez** a 64 bits de mantisa (al más cercano, empate a par),
multiplicado exactamente por un factor binary32 y redondeado una sola vez a 53 bits. Es el mismo
modelo que daba Boost.Multiprecision con `cpp_bin_float<113>` → 64 → producto → 53. Su valor privado
conserva signo, significando de 64 bits, exponente y clase, sin depender de `long double`, layout
x87, registros, control word ni sistema operativo. No es una biblioteca numérica general.

En CreateAngle se conserva el grafo cociente53 → atan → phi64 → producto por K24 → R53. El producto
64×24 cabe exacto en 88 bits y se redondea directamente a 53, sin phi → double intermedio. Guardas
axiales, diferencias/store binary32, suma de 90 y suma condicional de 180 conservan su orden.
`CreateAngle` devuelve `double` y `MovePath` sigue devolviendo `bool`; no se agrega Expected ni se
declara noexcept. NaN, ±inf y ±0 conservan la clase y el signo de antes; no hay excepciones de
biblioteca que atajar ni se inventa una salida NaN.

Las dos hojas Wave calculan producto53 y, para la segunda, suma53 antes del seno → retenido64. Si el
argumento finito tiene magnitud al menos 2^63, se retiene el argumento, como el C2 observado. La
evaluación devuelve sólo el valor retenido; la truncación signed64 se pide por separado. Una
conversión inválida enmascarada conserva el patrón `0x8000000000000000`, distinguiéndolo mediante
`valid` del INT64_MIN válido. No se fabrican W/V de reserva.

La base PR165 no contiene la interfaz pública `ILeafWaveEvaluator` ni `OriginalFtolLow(double)`.
No se copian tipos privados de fauna ni se inventan sustitutos. El adaptador y la integración
esperan la extracción Root; el contrato de la interfaz existente en su rama dueña debe conservar
T1/Eval1/H1; nueva lectura T2/Eval2; W; H2; V; Q. Las pruebas numéricas con T estable no acreditan
ese orden. El nullopt de otros callers no se cambia.

### phi64 es el atan64 bien redondeado, no FPATAN

phi64 es atan(cociente) redondeado al más cercano a 64 bits, no lo que deja el FPATAN de la x87.
Medido en la x87 de un Xeon, FPATAN difiere de phi64 en el 3,2 % de los cocientes. Después del
`fmul` por K24 en PC53 queda 1 ULP de double de diferencia en unos 13 casos por millón: 26 de 2 M
cocientes al azar y 29 de 4 M con coordenadas enteras en ±25.600. En la proyección f32 no hubo
ninguna diferencia. El resultado de FPATAN depende de la CPU, así que imitarlo ataría los golden a
un modelo de procesador. `OriginalAnglesTest.KeepsTheRoundedAtanModelWhereFpatanDiffers` fija el
modelo en tres contraejemplos desde (0,0), con el valor de la x87 en un comentario como divergencia
conocida: (7703, −8981), (8793, 6798) y (−643, −24380).

### La aritmética propia

El 2026-11-09 Emanuel decidió que no se acepta Boost.Multiprecision. Salió de `vcpkg.json`, de
`Client/CMakeLists.txt` y del `.cpp`; el modelo numérico no cambió. Todo vive en
`OriginalRetainedMath.cpp`, privado:

- **Valor intermedio:** signo, exponente y una mantisa de 256 bits en ocho limbs de 32. Cada
  producto parcial entra en `uint64_t`: no hay `__int128`, `long double` ni operaciones de punto
  flotante en el cálculo, así que no hay FMA implícito que cambie nada. Los únicos doubles son
  comparaciones y los argumentos de las hojas, un producto exacto de dos f32 y una suma R53 que una
  FMA no cambiaría. Suma con un limb de guarda, producto 256×256, producto y cociente por un entero
  chico y recíproco por Newton (cuatro pasos desde una semilla entera de 32 bits), todo truncado. El
  error relativo antes del redondeo a 64 bits queda por debajo de 2^-250. Sin memoria dinámica.
- **sin y cos:** con |x| < 1/2 la serie va directa. Si no, la reducción de Payne-Hanek es exacta:
  el producto entero de x por 384 bits de 2/pi da x·2/pi con error menor que 2^-321 para |x| < 2^63.
  Por fracciones continuas, |x·2/pi − k| ≥ 2^-61,14 para todo double con 1/2 ≤ |x| < 2^63 (el mínimo
  está en `0x1.6c6cbc45dc8dep+5`, k = 29), así que el resto r = f·pi/2 conserva más de 256 bits
  exactos. Después, Taylor de sin y cos con |r| ≤ pi/4.
- **atan:** para todo double finito, subnormales incluidos. Con |q| > 1 se usa pi/2 − atan(1/q).
  En (0, 1] se reduce con k = round(8t): atan(k/8) de tabla más la serie de
  atan((t − k/8)/(1 + t·k/8)), con |u| ≤ 1/16. atan(±inf) = ±pi/2.
- **Redondeos:** uno solo a 64 bits desde los 256; el producto 64×24 exacto en dos mitades de 32;
  uno solo a 53 con exponente sin límite. Después, `RetainedProductPC53` y los productos de seno y
  coseno devuelven el double exacto o el error, y `AtanDegreesProductPC53` redondea a subnormal como
  `convert_to<double>`.
- **Tablas:** 2/pi, pi/2 y atan(k/8) salen de un script de Python con enteros, sin flotantes: pi
  por Machin (16·atan(1/5) − 4·atan(1/239)) y atan(k/8) por la serie de Euler, en punto fijo de 1664
  bits con cota de error. Cada palabra sale igual con las dos puntas del intervalo y coincide con
  mpmath 1.4.1 a 2000 bits. El comentario de la tabla dice cómo se generó.

El caso que Boost tapaba con `runtime_error` (H0F1 sin converger) y la guarda |sin| > 1 dejan de
existir: con esta aritmética no pueden pasar. Errores, dominios y clases quedan igual: argumento no
finito o con |a| ≥ 2^63, factor no positivo o no finito, y producto que no cabe exacto en double.

## Alternativas

- **Boost.Multiprecision `cpp_bin_float<113>`**, el perfil del 2026-11-08 (vcpkg 1.92.0#0, enlace
  PRIVATE). Evitaba un kernel propio, pero Emanuel no la acepta como dependencia.
- **MPFR/GMP:** una dependencia compilada más pesada todavía.
- **`long double`:** depende del host (80 bits en x87, 64 en MSVC, 128 en otros).
- **Seno/atan en double:** pierde la retención de 64 bits.
- **x87 real con asm en x86 (`fpatan`/`fsin`; MSVC x64 pide MASM), con el modelo fuera de x86:** en
  esa máquina da lo mismo que el original, pero los golden cambian según la CPU. Emanuel la dejó
  diferida el 2026-11-09: por ahora va el modelo genérico, y la de CPU se retoma sólo si hace falta.

No se usan clamps, tolerancias ni excepciones por patrón de bits para hacer pasar el corpus.

## Validación del 2026-11-09

- **Tests del módulo:** g++ 13 y clang 18 con `-std=c++20 -Wall -Wextra -Wpedantic -Wshadow
  -Wconversion -Wsign-conversion -Wold-style-cast -Werror`. Pasan los 37 tests de
  `OriginalRetainedMathTest`, `OriginalAnglesTest`, `ParticleRetainedMathTest` y
  `PortableParticleTrigonometryTest`, con los mismos esperados de antes; también pasan con
  ASan/UBSan y con el módulo Boost anterior.
- **Diferencial contra Boost:** fuera del proyecto, un arnés compara la aritmética nueva con Boost
  1.83 `cpp_bin_float<113>` (el de apt, sólo como oráculo) con la misma cadena 113 → 64 →
  producto → 53. Compara los retenidos de sin, cos y atan y las siete funciones públicas con:
  argumentos chicos, grandes hasta 2^63, subnormales y ±0; vecinos de múltiplos de pi/2 (los
  convergentes de cada exponente y k·pi/2 al azar); atan en todo el rango double, en bordes de
  rama y con cocientes como los de CreateAngle; y factores f32 al azar, subnormales y FLT_MAX
  incluidos. Sobre el código del commit dio 0 diferencias en 256 millones de comparaciones con 136
  millones de entradas, y una corrida previa, 0 en otros 248 millones. Boost 113 se aleja hasta
  unos 9 ulp de 113 bits del valor de 256 bits, así que su redondeo a 64 sólo podría diferir en
  alrededor de 2^-45 de las evaluaciones: la igualdad es estadística, no una prueba. Se comparó con
  Boost 1.83 porque la 1.92 no está en la máquina.
- **Tests nuevos:** `ReducesArgumentsNearQuarterTurnMultiples` y `ReducesHugeArgumentsExactly`, con
  esperados de un script de Python con enteros que no comparte código con la implementación (pi por
  Machin a 3064 bits, reducción y Taylor en punto fijo de 3000 bits). Coinciden también con Boost.
  Con 2/pi truncado a 128 bits, o truncando en vez de redondear a 64, fallan. Los valores del
  modelo de `KeepsTheRoundedAtanModelWhereFpatanDiffers` se verificaron con Boost y con mpmath.

## Consecuencias

- Se gana: el perfil no depende de terceros, es determinista y da lo mismo en cualquier compilador
  con enteros de 32 y 64 bits, sin excepciones ni memoria dinámica. Las tablas se pueden regenerar.
- Se pierde: hay un kernel numérico propio que mantener, unas 460 líneas de código más tablas y
  comentarios. Cambiar la aritmética o sus tablas obliga a repetir el diferencial y los corpus
  completos.
- Divergencia aceptada frente al original: phi64 no es FPATAN, con 1 ULP de double en unos 13
  cocientes por millón después del producto y ninguna diferencia en el store f32.
- Lo fijan `OriginalRetainedMathTest`, `OriginalAnglesTest` (incluido
  `KeepsTheRoundedAtanModelWhereFpatanDiffers`), `ParticleRetainedMathTest` y el paso
  «Perfil matemático en MSVC Release» del CI.

## Evidencia y límites

Las pruebas conservan los 136 casos angulares, contrastan 38 retornos CreateAngle y 35 productos, y
2040 conversiones signed64/low32/clases en 1020 entradas Wave. El banco analítico exige exactamente
R6/I10/C6/Z1/P35/F13, con 13 valores F32 materializados y 71 claves únicas; los siete spills
sensibles rechazan identidad y el gate rechaza filas ausentes/duplicadas incluso con `python -O`.
La composición analítica de CreateAngle + lectura viva de yaw distingue la suma double del caller
que guarda primero binary32; no demuestra la composición completa de una sesión de juego.

Los senos retenidos **no son un reemplazo universal de FSIN**: difieren 841 valores exactos,
65 proyecciones F64 y 51 F32. Ocho signos NaN no se comparan; payload/signo NaN, flags, traps,
ABI x87 y equivalencia universal FSIN/FPATAN siguen excluidos. La concordancia de 2040 enteros,
1020 composiciones numéricas W/V/Q/AL y 38 retornos no elimina esas diferencias.

Los gates de producto locales usan C++20, Werror, PCH off, sin contracción FMA y FE_TONEAREST
en los tests; los formatos se comprueban en compilación. Main y los focales pasaban con Linux GCC
Debug y Clang Debug/Release el 2026-11-08; GCC Release se detenía en un warning de `Hello.cpp`,
ajeno a este módulo, y sus focales directos O3 pasaban. Eso no acredita FTZ/DAZ ni el runtime
gráfico. Windows MSVC x64 Debug/Release, la interfaz Wave y la revisión siguen pendientes antes de
ready/merge. No se anuncia macOS sin AppleClang ARM64. Los costes, stack y tamaños se miden antes de
optimizar y se registran con comandos/hashes en el audit `cl7-portable-math-product-20261108`.

TurnAngle, AngleDistance y bobbing de CharacterPosition no cambian. Este ADR no cierra CL7,
P-C32 ni la equivalencia de una sesión XP. Una diferencia nueva contra los corpus archivados,
angular, entera o de orden, se clasifica antes de aceptar la frontera: si sale del modelo
documentado, como la de FPATAN, queda como divergencia conocida con su test; si no, es un defecto y
la bloquea.
