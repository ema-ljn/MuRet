# 0014. Perfil matemático retenido de CreateAngle y las hojas Wave

- **Estado:** candidato para revisión Root/Astra; aceptación Windows pendiente.
- **Fecha:** 2026-11-08.
- **Alcance:** CL7, producto de `CreateAngle` y preparación numérica privada de dos senos Wave.

## Contexto

El perfil de [ADR0012](0012-perfil-fp-del-movimiento-original.md) distingue resultados retenidos
de stores binary32. R5-22, `(0,0)→(7,−11)`, devuelve `0x1.03c5021eae517p+5`; reducir phi a
binary64 antes de multiplicar devuelve `0x1.03c5021eae518p+5`. Ambos tienen el mismo store
binary32 en esta muestra. Eso no prueba divergencia de gameplay: algunos callers guardan
binary32 y otros suman yaw vivo antes de convertir a entero. Las Waves871/990 también
distinguen un valor retenido que trunca a0 de un seno double que llega a±1.

Se reutilizan exclusivamente 1020 casos Wave y38 casos CreateAngle PC53/RN archivados, con35
cocientes/phi/productos observados. SHA256 del banco de valores racionales `references.json`:
`5bfd7be7e163c3a9637cd8656ff4f88d209dbd938ac82e5f11932d6d11de0c3f`.
Los esperados versionados son valores numéricos; no contienen registros, ejecutables ni volcados.
Las tablas faltantes de0x4e93f0 y las premisas C0 de0012 siguen abiertas.

## Decisión

`OriginalRetainedMath` encapsula Boost.Multiprecision en su `.cpp`, con precisiones binarias
53/113/64, expresiones diferidas desactivadas y almacenamiento fijo del backend. Su valor
privado conserva signo, significando64, exponente y clase, sin depender de `long double`,
layout x87, registros, control word ni sistema operativo. No es una biblioteca numérica general.

En CreateAngle se conserva el grafo cociente53→atan113→phi64→producto por K24→R53. El producto
64×24 cabe exactamente en113 bits y se redondea directamente a53, sin phi→double intermedio.
Guardas axiales, diferencias/store binary32, suma de90 y suma condicional de180 conservan su
orden. `CreateAngle` devuelve `double` y `MovePath` sigue devolviendo `bool`; no se agrega
Expected ni se declara noexcept. Los especiales numéricos de Boost no se reinterpretan como
fallos recuperables, ni se inventa una salida NaN ante una excepción de biblioteca.

Las dos hojas Wave calculan producto53 y, para la segunda, suma53 antes del seno113→64. Si el
argumento finito tiene magnitud al menos2^63, se retiene el argumento, como el C2 observado.
La evaluación devuelve sólo el valor retenido; la truncación signed64 se pide por separado.
Una conversión inválida enmascarada conserva el patrón `0x8000000000000000`, distinguiéndolo
mediante `valid` del INT64_MIN válido. No se fabrican W/V de reserva ante fallos de biblioteca.

La base PR165 no contiene la interfaz pública `ILeafWaveEvaluator` ni `OriginalFtolLow(double)`.
No se copian tipos privados de fauna ni se inventan sustitutos. El adaptador y la integración
esperan la extracción Root; el contrato de la interfaz existente en su rama dueña debe conservar
T1/Eval1/H1; nueva lectura T2/Eval2; W; H2; V; Q. Las pruebas numéricas con T estable no acreditan
ese orden. El nullopt de otros callers no se cambia.

## Dependencia y alternativas

Se agrega `boost-multiprecision`, sin features opcionales, con enlace PRIVATE a ClientLib,
licencia BSL-1.0. Evita mantener un kernel numérico propio. MPFR/GMP añaden una dependencia
compilada más pesada; `long double` depende del host y el seno/atan double pierde retención.
No se usan clamps, tolerancias ni excepciones por patrón de bits para hacer pasar el corpus.

El baseline fijo `9a5dfef5d1a0438a7d24856a85bd60f309d355e9` resuelve efectivamente
Boost.Multiprecision **1.92.0#0**, feature `core`, target y host `x64-linux`. Se verificó el
baseline por Git y el paquete instalado antes de ejecutar los corpus del producto. La preparación
histórica usó headers oficiales1.90.0; no se atribuyen sus resultados a esta resolución. La nueva
ejecución con los headers1.92.0 instalados reproduce el ledger completo de discrepancias.
No cambia el baseline, no hay override de Boost ni mezcla de versiones. Windows necesita su
resolución/headers y validación efectivos; el triplet x64-windows no se instala desde Linux.

## Evidencia y límites

Las pruebas conservan angular136, contrastan38 retornos CreateAngle y35 productos, y2040
conversiones signed64/low32/clases en1020 entradas Wave. El banco analítico exige exactamente
R6/I10/C6/Z1/P35/F13, con13 valores F32 materializados y71 claves únicas; los siete spills
sensibles rechazan identidad y el gate rechaza filas ausentes/duplicadas incluso con Python-O.
La composición analítica de CreateAngle+lectura viva de yaw distingue la suma double del caller
que guarda primero binary32; no demuestra la composición completa de una sesión de juego.

Los senos retenidos **no son un reemplazo universal de FSIN**: difieren841 valores exactos,
65 proyecciones F64 y51 F32. Ocho signosNaN no se comparan; payload/signoNaN, flags, traps,
ABI x87 y equivalencia universal FSIN/FPATAN siguen excluidos. La concordancia de2040 enteros,
1020 composiciones numéricas W/V/Q/AL y38 retornos no elimina esas diferencias.

Los gates de producto locales usan C++20, Werror, PCH off, sin contracción FMA y FE_TONEAREST
en los tests; los formatos se comprueban en compilación. Main y los focales pasan con Linux
GCC Debug y Clang Debug/Release. GCC Release se detiene en un warning de `Hello.cpp` sin
cambios, reproducido también sin las definiciones de Boost; sus focales directos O3 pasan.
Eso no acredita FTZ/DAZ ni el runtime gráfico. Windows MSVC x64 Debug/Release, la interfaz Wave
y revisión Root/Astra siguen pendientes antes de ready/merge. No se anuncia macOS sin
AppleClang ARM64. Los costes, stack, asignaciones y tamaños se miden antes de optimizar y se
registran con comandos/hashes en el audit `cl7-portable-math-product-20261108`.

TurnAngle, AngleDistance y bobbing de CharacterPosition no cambian. Este ADR no cierra CL7,
P-C32 ni la equivalencia de una sesión XP. Cualquier diferencia angular, entera o de orden
adicional bloquea su frontera; una actualización de Boost requiere repetir los corpus completos.
