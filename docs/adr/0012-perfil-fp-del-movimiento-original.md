# 0012. Perfil FP explícito para el movimiento del cliente original

- **Estado:** adoptado para el port de valores; validación de sesión XP pendiente.
- **Fecha:** 2026-11-07.
- **Alcance:** CL7.4C, `OriginalAngles`, `MovePath` y `CharacterPosition`; reutilizan
  `OriginalRotation`, `MovementSpeed` y el servicio de terreno existente.

## Contexto

Las hojas 0x4e93f0, 0x4e9560 y 0x4e96f0 distinguen stores binary32 de resultados retenidos
por x87. Varias ramas leen C0 después de FLD/FADD/FSUB: la ISA deja ese condition code
indefinido para esas instrucciones. PC53 y nearest por sí solos no fijan esas ramas.

La evidencia nativa `cl7-native-angular-probe-20261107/run3` contiene 1596 llamadas a los bytes
originales enlazados por hash, con 12 control words, sin emulación. Los 133 casos PC53/RN
(4 de rumbo, 80 de giro, 49 de distancia) conservaron el C0 anterior en los cinco sitios;
los retornos trazados y sin traza coincidieron. Es una observación local acotada, no una
captura del cliente en XP ni una garantía para otras CPU o todo estado excepcional.

El dictamen independiente `cl7-native-angular-astra-review-20261107/report.md` habilita un
port condicionado. El texto recuperado de 0x4e93f0 es parcial: conserva tres tablas sin
recuperar. No se lo declara un contrato íntegro. Se reutilizan los datos numéricos y el
ledger existente, sin abrir cuerpos nuevos para esta implementación.

## Decisión

El port elige explícitamente este perfil de compatibilidad:

1. Operaciones de valores con precisión de significando de 53 bits, nearest, excepciones
   enmascaradas y memoria ordinaria estable. La plataforma debe usar binary32 y binary64
   ordinarios, sin fast-math ni contracción FMA. El port no modifica el entorno FP del caller.
2. Conservación del C0 observado en los cinco sitios concretos de run3, incluida la muestra
   de AX anterior a la comparación de dy y las dos muestras distintas de los fallbacks de giro.
   Esta premisa reproduce una observación del procesador del experimento: la ISA no la garantiza.
3. Constantes iguales al snapshot del archivo original. Sus secciones permiten escritura;
   no se demuestra que esos valores permanezcan intactos en una sesión real.
4. Continuidad de las comparaciones con excepciones enmascaradas: C0 significa menor **o
   unordered**, y C0|C3=0 significa mayor ordenado. No se impone una precondición de finitud
   a los ángulos de gameplay. Se portan esas ramas también para NaN; no se promete preservar
   payloads, quieting, flags FP ni el flujo con excepciones no enmascaradas. El corpus acotado
   tampoco garantiza identidad universal de `std::atan`/`std::sin` con FPATAN/FSIN o todos los
   extremos/subnormales.

Se conservan explícitamente los spills binary32 de diferencias, preajustes negativos, P del
giro, objetivos de ruta, medio paso, velocidad negada y seno previo a abs. Q del giro y la
distancia angular permanecen en binary64 hasta el store real del caller. Cada ángulo negativo
recibe una sola suma de 360; no hay fmod, atan2 ni interpolación circular convencional. El byte
de modo sólo activa la negación cuando vale exactamente 1. Se respetan las asimetrías de igualdad.

`MovePath` acepta el dominio de ruta válido (`count<=15`, `progress<=3`); no inventa un objetivo
para el scratch sin inicializar de las entradas inválidas del original. Los servicios de las
celdas lógicas reciben pre/store/post por eje, distinguiendo inicio y llegada, sobre referencias
vivas. Esas fronteras permiten al dueño del estado preservar P y sus efectos, sin duplicar pools.
`CharacterPosition` calcula la matriz antes de consultar velocidad, escribe XYZ en orden,
expone Z provisional a terreno y relee tipo/fase después de esa consulta.

## Evidencia y límites

`OriginalAnglesTest.NativePc53NearestCorpus` confronta 136 retornos PC53/RN nativos (133 de run3
y tres adicionales de run4) a la
frontera binary64 y al store binary32. Los esperados son números, no bytes ejecutables ni dumps.
Hash SHA256 de la salida nativa completa:
`5b4d0c36cc15bd2496d3f5382f5614c35a50c1ab7c3f8125e82910d8bfba5e0e`.
Hash del manifest que enlaza entradas, perfil y cuerpos:
`8dfa928a480c0e0f9f8abe38bb3bf0c7b8d7ee84a32a1414bec229abf44f32d7`.
El PE se identifica por
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.

La ampliación nativa run4 confirmó con qNaN sintético binary32 `0x7fc00001` y paso10:
`TurnAngle(NaN,20,10)=20`, `TurnAngle(10,NaN,10)=+0` y
`TurnAngle(180,NaN,10)=170`. El port anterior fallaba los tres al usar comparaciones ordenadas
como sustitutos de C0; el probe de objetos lejanos de Devias permitió descubrirlo. Se corrigieron
los predicados de las tres hojas angulares según sus tablas, manteniendo precisión y orden.
No se convirtió ese defecto en una nueva exclusión de dominio.

Hash de salida nativa completa run4:
`e6481dc2b83c8dae0b579dbb21cffcec14c53b340371667625fc8e5e46775a7b`.
Hash de su manifest:
`955292e53a06cd6b653d2a8510662e571868b0ef773bc5c1131232dde96dff4c`.
Run4 contiene 1632 llamadas trazadas/sin traza con retornos iguales; esa comparación valida el
observador, y los tres esperados adicionales validan el candidato en sus fronteras binary64/f32.
Los casos adicionales analíticos verifican NaN en los dos guardas axiales de CreateAngle, paso NaN
bajo/alto y FCHS de resultado unordered en modo1, sin prometer bits universales de su payload.

Las pruebas analíticas adicionales distinguen Q retenido/P derramado, la separación
`45-2^-20` antes del store, epsilon estricto, modo 2/255, antipodas y preajuste único.
Las pruebas de movimiento fijan cuartos, índices, centros limitados, llegada, giro y efectos
por eje; las de posición fijan precedencia, Z provisional, altura y seno. No son evidencia
nativa de la composición completa ni prueban presupuesto RNG transitivo, bootstrap T,
profundidad P efectiva de gameplay o toda CL7.

P-C32 permanece abierto para la equivalencia de una sesión real: faltan CW, status, TOP/tags,
constantes runtime y dominio representativo en sus callsites de XP. Los snapshots y registros
nativos permanecen fuera de Git. Cambiar este perfil o ampliar sus promesas exige evidencia y
nuevos vectores; una suite verde no elimina esas premisas.
