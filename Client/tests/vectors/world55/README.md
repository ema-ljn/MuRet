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

## Los demás archivos

Cada uno lo saca su script, también con unicorn sobre el código real y `MURET_MAIN_EXE`, y lo lee
un test, bit a bit:

| Archivo | Qué tiene | Script | Test |
| --- | --- | --- | --- |
| `dragon.txt` | el dragón (`0x518cf0` con el `0xb4` y 1.500 pasos de `0x5208a0`) | `make_dragon_vectors.py` | `world/test_model_effects.cpp` |
| `rotation.txt` | `0x5fc8f0` (AngleMatrix) y `0x5fcbd0` (VectorRotate), 400 casos | `make_rotation_vectors.py` | `render/test_original_rotation.cpp` |
| `billboard.txt` | `0x621400`, 300 casos: la vista, la textura y lo que manda a GL | `make_billboard_vectors.py` | `render/test_billboard.cpp` |
| `scepter.txt` | el caso `0x620` de `0x509340`, 300 casos: el hueso 0, la escala y el origen del cuerpo, la semilla del `rand()`, los cinco sprites que deja en `0x760f990` y el `rand()` al final | `make_scepter_vectors.py` | `world/test_scepter_glow.cpp` |
| `particles.txt` | las partículas con el aliento del caballo (`0x5f2`, subtipo 0) en 20 casos y la chispa del cuervo (`0x5c6`, subtipo 5, escala 0,8) en 8 más (CL7.6 C2), de 20 pasos: el viento y el estado del `rand()` al empezar, cada aliento que nace (el reloj, la posición, los ángulos y la luz), y después de cada paso de `0x5446e0` el viento, el `rand()`, los 0x78 bytes de cada lugar usado de `0x743a198` y lo que `0x548550(0)` le pide a `0x621400` y a las mezclas | `make_particle_vectors.py` | `world/test_particles.cpp` |
| `leaves.txt` | las 80 brasas del mapa 55 y la altura del terreno, 3 casos de 150 pasos (el primero, el del primer paso del login: el héroe y la cámara en (0, 0, 0)): la semilla, el héroe, la y de la cámara y los cuatro números de la fórmula del mapa de alturas y de sus atributos; 40 alturas sueltas de `0x5f93c0` y 15 de sus bordes (el double de la x87); en cada paso de `0x530490` el estado del `rand()` y el CRC32 de los 80 lugares de `0x75e34b8`; cada 30, los lugares enteros; cada 75, lo que `0x5306a0` le pide a GL | `make_leaf_vectors.py` | `world/test_leaves.cpp` |
| `raven.txt` | el cuervo, 8 casos: el constructor real (`0x4167b0`, con la altura de `0x5f93c0` sobre el mapa de una fórmula) y sus pasos (`0x416a20`): en cada uno el estado del `rand()`, la posición, los ángulos, la dirección, la velocidad, el estado, la acción, el frame, el contador de lejanía, el grito y, si nació, la chispa entera (`0x53fe70` con el `0x5c6`, subtipo 5), con una pose de mentira en sus huesos | `make_raven_vectors.py` | `scenes/test_dark_spirit.cpp` |
