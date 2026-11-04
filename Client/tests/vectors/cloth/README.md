# Los vectores de la tela

Lo que hizo el código **real** del `main.exe` (c32bbd5f) con la tela del personaje, emulado con
unicorn sobre el binario: el bloque `0x50d58b..0x50db57` de `0x50ba30` (las telas del Dark Lord y de
la clase 3) o, en los casos "direct", las funciones de la clase de la vtable `0x6b8cd8` con
parámetros a elección. Cada float va como los 8 dígitos hexadecimales de sus bits. Los leen
`tests/support/ClothVectors.h`, `render/test_cloth.cpp` y `scenes/test_character_cloth.cpp`.

| Archivo | Qué cubre |
| --- | --- |
| `a_login_dark_lord.txt` | el Dark Lord del login: 3 frames, las 3 telas, el mapa `0x37` |
| `b_fourth_cloth_and_wings.txt` | la cuarta tela (`ArmorClass05`): armada sin usar, rearmada encima, suelta y rearmada; sin las alas `0x1c30`; el color de un duelo |
| `c_class3_robe.txt` | la clase 3, fuera del `0x37`, con el viento base paseado y otro acceso al contador abierto |
| `e_broken.txt` | la rotura (los huesos saltan 4000) y el rearmado del frame siguiente |
| `f_direct_flags.txt` | las banderas que `0x50ba30` no usa: `0x100`, `0x20000000`, `0x2000`/`0x3000` con brillo, `0x400`/`0x800`, los arcos 2 y 3, sin ancho que crece |
| `g_direct_edges.txt` | las distancias mínimas (0,001 y 0,01), la restricción que separa, la rotura por el mínimo y el color nulo |

Cómo se sacan, con `main.exe`, unicorn y los scripts de `Client/docs/research/scripts/`:

```bash
python emulate_cloth.py <caso>.json > salida.json      # el código real; los casos, en su docstring
python cloth_reference.py salida.json                  # el contrato en Python: igual bit a bit
python make_cloth_vectors.py salida.json > <caso>.txt  # el formato de esta carpeta
```

La mezcla de cada cara (`opaque`, `alpha`, `additive`) sale de simular el estado de GL con las
llamadas que hizo el binario (los wrappers de `0x620xxx` con sus cachés), no de las banderas de la
tela. Cada vértice de una cara va como el índice de la partícula del último paso y sus UV.
