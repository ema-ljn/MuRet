# Vectores JPEG

JPEG sintéticos y chicos para los tests de `DecodeJpeg`, `LoadOzjTexture` y `LoadTerrainLight`.
Están en el repo para que corran en CI, que no tiene la carpeta golden. No son archivos del juego:
son imágenes armadas con una fórmula.

Cada `<nombre>.6b.raw` es lo que da decodificar `<nombre>.jpg` con la IJG libjpeg 6b, la que
enlaza el `main.exe` original, como lo hace el original: los píxeles fila por fila, de arriba hacia
abajo, sin cabecera. Los tests piden que libjpeg-turbo dé esos mismos bytes (`CODING_RULES.md` §10).
`mixed-sampling.turbo.raw` es lo mismo con libjpeg-turbo 3.2.0: es el único vector en que no hay
cómo dar los bytes de la 6b (Q-C33), y el test pide los de turbo.

`lab-6b.txt` es lo mismo para los OZJ de la carpeta golden, que no están en el repo: de cada uno,
el tamaño y un hash de lo que da la 6b (más abajo).

## Los vectores

| Vector | Tamaño | Qué es | Para qué |
| --- | --- | --- | --- |
| `baseline-444-dri` | 16 × 24 | baseline 4:4:4, un reinicio por fila de MCU | el tipo de 1.641 OZJ del laboratorio |
| `baseline-420` | 13 × 11 | baseline 4:2:0, lados que no son múltiplo de 16 | el tipo de 116; el borde del submuestreo |
| `progressive-444` | 16 × 16 | progresivo 4:4:4 | el tipo de 623 |
| `baseline-440` | 16 × 16 | baseline 4:4:0: el color, a la mitad de alto | turbo lo agranda distinto si no se le apaga el filtro (Q-C33) |
| `gray` | 8 × 8 | baseline en grises | un byte por píxel (Q-C31) |
| `cmyk-3x2` | 3 × 2 | baseline CMYK, 4 componentes | una fila CMYK que entra en la textura (Q-C31) |
| `truncated` | 16 × 24 | `baseline-444-dri` cortado en la mitad de sus datos | libjpeg avisa y completa la imagen |
| `mixed-sampling` | 16 × 16 | baseline con el Y en 2 × 2, el Cb en 2 × 1 y el Cr en 1 × 1 | un componente al doble de alto y otro al doble de ancho (Q-C33) |
| `arithmetic` | 16 × 8 | baseline 4:4:4 con codificación aritmética | la 6b no lo lee (Q-C33) |
| `lossless` | 16 × 8 | sin pérdida (SOF3) | la 6b no lo lee (Q-C33) |
| `precision-12` | 16 × 8 | DCT de 12 bits (SOF1) | la 6b no lo lee (Q-C33) |
| `no-huffman-tables` | 16 × 24 | `baseline-444-dri` sin sus DHT | la 6b no lo lee; turbo pone las tablas estándar (Q-C33) |
| `flat-512x8`, `flat-513x8`, `flat-8x513` | | baseline 4:4:4 de un solo color | el tope de 512 |
| `light-256` | 256 × 256 | baseline 4:4:4 en bloques de 8 × 8 | una luz del terreno |
| `light-256x255`, `light-255x256`, `light-gray-256` | | la misma, de una fila o una columna menos, o en grises | las luces que no se aceptan |
| `cmyk-4x2` | 4 × 2 | baseline CMYK, 4 componentes | una fila que no entra en la textura (Q-C31) |

Los ocho primeros tienen su `.6b.raw`. Los cuatro que siguen, la 6b los rechaza: hash6b da `ERR 1`
(`JERR_ARITH_NOTIMPL`), `ERR 60` (`JERR_SOF_UNSUPPORTED`), `ERR 15` (`JERR_BAD_PRECISION`) y
`ERR 50` (`JERR_NO_HUFF_TABLE`). Los demás no lo necesitan: se usan por su tamaño, o se comparan
con lo que da `DecodeJpeg`, que ya está probado con los otros.

## Cómo se armaron

Las imágenes salen de estas fórmulas, con `x` e `y` desde 0 y `&` el y de bits:

- `detailed` (`baseline-444-dri`, `baseline-420`, `progressive-444`, `baseline-440`, `gray`,
  `mixed-sampling` y los que la 6b no lee): rojo `(47x + 13y) & 255`, verde `(x² + 3y²) & 255`,
  azul `(7xy + 91) & 255`. En `gray` va solo el rojo.
- `flat`: rojo 128, verde 64 y azul 192 en todos los píxeles.
- `blocks` (las luces): rojo `8 ⌊x/8⌋ & 255`, verde `8 ⌊y/8⌋ & 255`, azul
  `(128 + ⌊x/8⌋ + ⌊y/8⌋) & 255`. En `light-gray-256` va solo el rojo.
- `cmyk-3x2` y `cmyk-4x2`: el byte i de la fila y es `(15i + 40y) & 255`, con i desde 0 hasta 4
  por el ancho, sin incluirlo.

Se guardaron como PPM (o PGM, las de grises) y se codificaron con `cjpeg` de libjpeg-turbo 3.2.0,
con `-quality 75` y además:

- `baseline-444-dri`: `-sample 1x1 -restart 1` (un reinicio cada fila de MCU, como los del
  laboratorio);
- `baseline-420`: `-sample 2x2`;
- `baseline-440`: `-sample 1x2`;
- `mixed-sampling`: `-sample 2x2,2x1,1x1`;
- `progressive-444`: `-sample 1x1 -progressive`;
- `arithmetic`: `-sample 1x1 -arithmetic`;
- `lossless`: `-lossless 1`;
- `precision-12`: `-sample 1x1 -precision 12`;
- `flat-*`, `light-256`, `light-256x255` y `light-255x256`: `-sample 1x1`;
- `gray` y `light-gray-256`: nada más.

`truncated` son los primeros bytes de `baseline-444-dri.jpg`, hasta la mitad de sus datos: el
final de la cabecera del scan más la mitad de lo que queda sin contar el `FF D9`.
`no-huffman-tables` es `baseline-444-dri.jpg` sin sus segmentos DHT (`FF C4`). Los CMYK no salen
de `cjpeg`, que no escribe CMYK: los escribe `cmyk6b`, con el compresor de la 6b,
`in_color_space = JCS_CMYK`, `jpeg_set_defaults` y calidad 75.

## La 6b

La 6b es el `libjpeg6b_6b1.orig.tar.gz` del archivo de Ubuntu (SHA-256
`dbe673129e69eaaed47e3cfccf86b1582fa19862a3ce8189ea8f8cfc1800d9aa`, la biblioteca idéntica a la 6b
de la IJG), compilada con `INT32` de 32 bits, como en el `main.exe`. No está en CI: `tools/` tiene
lo que hace falta para armarla y volver a sacar todo.

- `tools/build6b.sh <carpeta>` baja el tarball, mira su SHA-256 y compila la biblioteca, `hash6b`
  y `cmyk6b`.
- `tools/hash6b.c` decodifica como el original (`0x63b870` y `0x63b490`): `jpeg_stdio_src`, los
  parámetros por defecto y `jpeg_read_scanlines` de a una fila, sin `jpeg_finish_decompress`
  (Q-C32). De cada archivo imprime `archivo ancho alto componentes fnv`, con el FNV-1a de 64 bits
  de los píxeles en hexadecimal, o `archivo ERR código`, el `msg_code` de su `jerror.h`. Con
  `-raw salida`, escribe los píxeles: así salen los `.6b.raw`. En `truncated`, la 6b avisa
  ("Premature end of JPEG file") y completa la imagen.
- `tools/cmyk6b.c` escribe los dos CMYK.
- `hashturbo` es `hash6b.c` compilado contra libjpeg-turbo 3.2.0 en vez de la 6b
  (`cc -O2 tools/hash6b.c -I<turbo>/include <turbo>/lib/libjpeg.a -o hashturbo`): da
  `mixed-sampling.turbo.raw`.
- `tools/make_vectors.py <cjpeg> <hash6b> <cmyk6b> <hashturbo> <carpeta>` arma todos los vectores
  de la tabla, y sus `.6b.raw`, en la carpeta.
- `tools/lab6b.sh <hash6b> <carpeta golden> > lab-6b.txt` escribe `lab-6b.txt`: una línea por OZJ,
  con `-skip 24` (el `fseek` de `0x18`), ordenadas por ruta.

De los archivos del juego, `lab-6b.txt` solo tiene la ruta, el tamaño, los componentes y un hash
(`AGENTS.md`). `GoldenJpegImageTest.cpp` lo compara con lo que da `DecodeJpeg` de cada OZJ, y es lo
que hay que correr al cambiar de versión de libjpeg-turbo.
