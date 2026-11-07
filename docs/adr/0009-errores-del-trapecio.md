# ADR 0009: errores explícitos fuera del dominio portable del trapecio

- Estado: aceptado para CL7.5E.
- Fecha: 2026-11-06.

## Contexto

`0x5fb570` convierte D y meta con `0x69fd3c`: truncamiento a int64, comparación de los bajos32
como signed32. Si el nivel efectivo elegido no está entre0 y4, el dispatch nativo usa locales sin
inicializar; no hay fallback geométrico definido. Para NaN, infinito o desbordamiento de int64,
`FISTP` depende de máscaras y excepciones x87 de entrada que la evidencia actual no fija.
Ver [06 § El trapecio completo](../../Client/docs/research/06-render-escenas-y-ui.md#el-trapecio-completo).

## Decisión

En el camino normal, MuRet exige D/meta finitos en `[-2^63,2^63)` antes del cast y devuelve
`InvalidCameraDistance` si fallan. Después de la decisión de adopción/retención, exige nivel0..4
y devuelve `InvalidEffectiveLevel` si falla. La distancia tiene precedencia. Un error no publica
estado ni polígono; no elige recuperación ni saturación. Un solicitado válido puede sanar un
previo inválido. En los especiales6/webzen y30/escena5 se omiten esas conversiones/validaciones y
se conserva exactamente el previo, también inválido.

Es una frontera portable observable, no una reproducción del resultado excepcional o indefinido
del original. Se conserva la proyección numérica de `OriginalRotation`, sus stores binary32 y
operaciones de53bits, dentro de su dominio existente; no se promete equivalencia universal con
x87/libm, estado FP arbitrario, mutación reentrante de callees o aliasing nativo.

## Consecuencias

El caller sólo publica `nextState` al recibir éxito. La consulta de geometría no avanza el nivel.
Los defaults de los DTO no prueban el inicio/reset nativo. Dueño persistente, orden cámara→trapecio,
cadencia, reentrada de escenas y política de recuperación son trabajo de integración posterior;
esta decisión no los implementa ni cierra CL7.
