# Los vectores de los objetos del mapa 55

Lo que hizo el código **real** del `main.exe` (c32bbd5f) con cada uno de los 276 registros de
`World56\EncTerrain56.obj`, en el orden del archivo, emulado con unicorn sobre el binario
(`Client/docs/research/scripts/login/draw_emu.py`): lo que deja `0x606660` al crearlo en la escena 2
(el login), lo que deja cada uno de tres pasos de `0x604ab0` con `+0x16c` prendido (como después de un
dibujo) y lo que `0x602e90` le pasa a `RenderBody` o a `RenderMesh`, con el color del modelo (`+0x48`)
y su luz por vértice (`+0x44`). La luz del terreno es una fija (el renglón `light`), la misma que el
test le da a MuRet. Cada float va como los 8 dígitos hexadecimales de sus bits. Los leen
`tests/support/WorldObjectVectors.h` y `world/test_golden_world_objects.cpp`.

| Renglón | Qué tiene |
| --- | --- |
| `object i tipo modelo` | el índice en el `.obj`, el tipo y si `Object56\Object<tipo + 1>.bmd` está |
| `created …`, `step k …` | la escala (`+0xc`), la posición (`+0x10`), los ángulos (`+0x1c`), la malla oculta (`+0x58`), el blendMesh (`+0x64`), su luz (`+0x68`), la U y la V (`+0x6c`, `+0x70`), la velocidad (`+0xd8`), la luz por vértice (`+0xe8`), la luz (`+0xf4`), el alpha (`+0x174`), su meta (`+0x170`), el frame (`+0x114`) y la acción (`+0x111`) |
| `draw n` y `call …` | las `n` llamadas de un dibujo: `RenderBody(modo, alpha, blendMesh, luz, U, V, malla oculta, −1)` o `RenderMesh(malla, banderas, alpha, blendMesh, luz, U, V, −1)`, el color y la luz por vértice |

Cómo se sacan, con `main.exe` en `MURET_MAIN_EXE`, unicorn y el `Data\` del laboratorio:

```bash
python make_world55_vectors.py Client/tests/vectors/world55/objects.txt
```
