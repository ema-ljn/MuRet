# 0004. JPEG con libjpeg-turbo, y no con la 6b del original

- **Estado:** aceptado (lo decidió Emanuel el 2026-11-01, en la CL1.4).
- **Fila:** Q-C33 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
- **Código:** `Client::Data::Textures::DecodeJpeg` (`Client/src/Data/Textures/JpegImage.cpp`), que
  usan los OZJ, y la versión fija en `vcpkg.json`.

## Contexto

Las texturas `.OZJ` son JPEG con 24 bytes delante. El original las decodifica con la libjpeg 6b de
IJG, enlazada adentro del binario (`0x66b8f0`..`0x67f0c0`), desde `0x63b870` y `0x63b490`. Lo que
sale de ahí es lo que se ve: un byte distinto es otro color.

La pregunta 1 del [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) era si
libjpeg-turbo, de vcpkg, alcanza para dar los mismos bytes que la 6b, y si no, si se acepta una
diferencia chica o se escribe un decodificador. La
[regla 10](../CODING_RULES.md#10-pensar-antes-de-agregar) pide eso mismo: una biblioteca entra si
resuelve ese problema como se espera, y lo prueban los golden.

El 2026-11-01 Emanuel eligió primero un decodificador propio compatible con la 6b, y esa misma noche
lo cambió por turbo: "deja este plan y hagamos la implementacion de turbo". El
[plan del decodificador](../superpowers/plans/2026-11-01-cl1.4-decodificador-jpeg.md) quedó en el
repo, en pausa.

## Decisión

- MuRet decodifica con **libjpeg-turbo 3.2.0**, fija en los `overrides` de `vcpkg.json`, con la API
  de la 6b y sus parámetros por defecto. Un `static_assert` en `JpegImage.cpp` corta la compilación
  con otra versión: subirla es un PR propio que corre los golden (regla 10).
- **Lo que se puede llevar a la 6b, se lleva.** Se rechaza como `InvalidData` lo que la 6b no lee:
  la codificación aritmética, el JPEG sin pérdida, otra precisión que 8 bits, y un scan que pide
  una tabla de Huffman que no llegó (turbo pondría la estándar). Y se apaga el filtro que agranda
  el 4:4:0 cuando la 6b no lo usa, así da los mismos bytes.
- **Lo demás queda como en turbo**, escrito en Q-C33: el progresivo incompleto, una mezcla de
  muestreos de más de 4 píxeles de ancho, ids de componentes repetidos, un DAC en un JPEG de
  Huffman, la IDCT con SIMD que satura donde la 6b se queda con los bits bajos, y los datos
  corruptos. Nada de eso está en el laboratorio.
- Si turbo deja de alcanzar (una textura real que da otro byte), se retoma el plan en pausa.

## Consecuencias

- **Se gana:** una biblioteca mantenida y rápida, con SIMD, en lugar de un decodificador propio que
  habría que escribir y cuidar. En los 2.380 JPEG del laboratorio da los bytes de la 6b, con SIMD y
  sin él, y también en los tiles de `mu-golden`.
- **Se pierde:** fuera del laboratorio, un JPEG que caiga en lo que queda de Q-C33, del (3) al (7),
  se ve distinto que en el original, y algunos que el original lee (un DAC mal armado, ids de
  componentes repetidos) turbo los rechaza. La IDCT con SIMD depende de la CPU y de
  `JSIMD_FORCENONE`, que es de todo el proceso: no se apaga desde un descompresor.
- **Lo fijan:**
  - `GoldenJpegImage.DecodesEveryOzjOfTheLabLikeTheLibjpeg6b`, con la carpeta de datos: cada OZJ
    del laboratorio contra el hash de la salida de la 6b
    ([`lab-6b.txt`](../../Client/tests/vectors/jpeg/lab-6b.txt)).
  - `JpegImageTest.DecodesLikeTheLibjpeg6b`, en CI: los vectores chicos de
    [`Client/tests/vectors/jpeg`](../../Client/tests/vectors/jpeg), con el 4:4:0 entre ellos.
  - `JpegImageTest.WhatThe6bCannotReadIsInvalidData`: lo que la 6b rechaza.
  - `JpegImageTest.ASamplingThatMixesDoubleHeightAndDoubleWidthDecodesLikeTurbo`: avisa si turbo
    cambia una de las diferencias que quedan.

El contrato y los pasos están en la sub-ola CL1.4 del
[plan de la CL1](../superpowers/plans/2026-11-01-ola-cl1-formatos-sin-ventana.md).
