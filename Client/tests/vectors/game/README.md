# Los vectores de la cámara del juego

`camera.txt` guarda lo que dejó **el código real** de `0x635ae0` del `main.exe` c32bbd5f,
emulado con Unicorn por `game/camera/emu_camera.py`. Es el contrato de 06 § La cámara del juego y
CL7.3 A, tarea 2. No usa `camera_reference.py` como fuente de expectativas. Lo lee
`tests/world/test_game_camera.cpp`, bit a bit, sin necesitar el binario en CI.

Son 60 casos de 50 frames consecutivos: cinco semillas, mapas 0, 2, 3, 5, 6 y 30, y escenas 2, 4 y 5.
Cada mapa aparece en las tres escenas. Cambian el héroe, el atributo (`00`, `01`, `40`, `80`, `c0`),
el temblor, el reloj y el desvío de `0x724588` (cero, xyz al azar o x cero con y/z no cero). La rama
especial del mapa 6 se prende y apaga por frame. Su entrada es un booleano: el arnés sintetiza el
texto que la activa solo en memoria; los vectores no guardan nombres.

El estado inicial varía D, zoom, yaw, nivel, far, altura fija y meta de D. Incluye zoom negativo,
200 ±0,01, 210 y 220, y nivel 5 para comprobar la conservación del far sin caso en el `switch`.
Los primeros 12 frames prueban x = 16.100, x = 19.000, y = 18.900 e y = 21.700, cada borde a −0,01,
exacto y +0,01, con la otra coordenada dentro del rectángulo. Después mezcla posiciones interiores
y por el mapa entero. La celda lógica del arnés queda fija: el atributo se proporciona aparte de
la posición visual. El recorrido de `CCameraMove` y la vista de arriba quedan fuera de estos casos.

Los frames de índices 12, 13 y 14 (desde cero), después de los bordes, usan los relojes largos exactos
`4f0d7740`, `4f2f0344` y `4f5ac415` en cada caso: aproximadamente 27,5, 34 y 42,5 días en ms. Sus
expectativas siguen siendo las del binario emulado con Unicorn. En esos relojes, el balanceo del
mapa 5 permite observar el límite de equivalencia con `FSIN` x87 nativo: la reproducción local de
sus instrucciones puede diferir un ULP de Unicorn y de `std::sin`. Esto queda abierto para la
corrida del original en [P-C29](../../../docs/research/08-pendientes.md).

En la fuente de [Unicorn 2.1.4](https://github.com/unicorn-engine/unicorn/blob/8028ec436f2d9376525352dd38ed9ed6b9f6be10/qemu/target/i386/fpu_helper.c#L966),
FSIN/FCOS convierten x80 a double, usan `sin`/`cos` de C y convierten a x80. Estos fixtures son
regresiones históricas del emulador; no prueban la aproximación trascendental de una CPU x87.
La revisión actual no ejecutó los generadores ni cambió sus bits.

## Formato `camera 2`

Los floats son sus 32 bits como ocho dígitos hexadecimales, incluso ceros con signo. El atributo va
como dos dígitos hexadecimales; semilla, mapa, escena, nivel y booleano van en decimal.

| Renglón | Campos, en orden |
| --- | --- |
| `camera 2` | versión del formato |
| `case …` | semilla, mapa, escena, D inicial, zoom inicial, yaw inicial, nivel inicial, far inicial, altura fija inicial, meta de D inicial |
| `frame …` | héroe xyz, atributo, temblor, reloj, rama especial del mapa 6 (`0`/`1`), desvío xyz; luego la salida: posición xyz, ángulos xyz, zoom, D, meta de D, nivel, far, altura fija, fov, near |
| `end` | fin del archivo |

Cada `case` reinicia el arnés. El estado anterior de cada frame es el inicial o la salida del frame
previo; el yaw siguiente es el ángulo z de la cámara. El mapa y la escena se mantienen durante el
caso. El siguiente `case` cierra el anterior.

## Regenerar

Con `MURET_MAIN_EXE` apuntando al build descrito en
[`scripts/README.md`, El entorno](../../../docs/research/scripts/README.md#el-entorno), y un entorno
Python con `unicorn` y `pefile`, desde la raíz del repo:

```bash
python -I Client/docs/research/scripts/game/camera/make_camera_vectors.py Client/tests/vectors/game/camera.txt
```

También puede invocarse por ruta absoluta desde fuera del repo. Antes de emular exige el SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a` del exe. No lee `Data\`, volcados,
trazas ni cable. Produce LF, UTF-8 y orden fijo; escribe tras completar todos los frames. La semilla
local es `semilla * 1000 + índice`, donde el índice recorre los seis mapas y sus dos variantes de
escena (0 a 11).

Generado y regenerado byte a byte con Python 3.14.4, Unicorn 2.1.4 y pefile 2024.8.26: 3.062 renglones;
SHA-256 de `camera.txt`: `ae1721522f5a434b9c6a945796ed5204c60a836672105f9e32cddcb545fe95dd`.

## El giro inverso: `inverse_rotation.txt`

Expectativas del **código real** de `0x5fcc30` (`VectorIRotate`), ejecutado con el arnés de
`login/make_rotation_vectors.py` sobre el mismo exe, con la x87 en 53 bits (`0x027f`). Los primeros
400 casos reusan el vector y la matriz de cada caso de `world55/rotation.txt`, en su orden. Se
descartan sus ángulos y su salida de `VectorRotate`: las salidas inversas se calculan ejecutando
`0x5fcc30`. El SHA-256 de ese archivo fuente es
`daea6b29aff3cdbfd118b5d7f511396edf76f2a9171909b6edf678c0f1e30452`.

Los últimos tres casos usan matrices sintéticas (no necesariamente giros ortonormales) y vectores
con 2^70 para aislar la cancelación de cada componente. Hacen distinguible `(2^70 + 1) − 2^70` de
`(2^70 − 2^70) + 1`, que los vectores de la cámara, con x = z = 0, no pueden distinguir. Sus
expectativas también salen del binario: no se usa una fórmula Python como oráculo. Los compara
`tests/render/test_original_rotation.cpp`.

El test agrega tres casos analíticos complementarios, sin modificar el archivo: los seis casos
en conjunto detectan las dos asociaciones alternativas de cada componente. Los tres casos
guardados por sí solos no las distinguían todas. El lector exige ocho dígitos por float y filas
completas para impedir truncamientos diferentes entre Linux y Windows.

| Renglón | Campos, en orden |
| --- | --- |
| `inverse_rotation 1` | versión del formato |
| `case …` | vector xyz, matriz de 12 floats por filas (3 × 4), salida inversa xyz; todos como ocho dígitos hexadecimales de sus bits |
| `end` | fin del archivo |

Para regenerarlo, con el mismo entorno y `MURET_MAIN_EXE` que la cámara:

```bash
python -I Client/docs/research/scripts/game/camera/make_inverse_rotation_vectors.py Client/tests/vectors/game/inverse_rotation.txt
```

Funciona por ruta absoluta desde fuera del repo, comprueba el SHA-256 del exe antes de emular y
reemplaza el archivo de salida solo cuando el texto completo está listo. No modifica `rotation.txt`
ni `camera.txt`. Generado y regenerado byte a byte con Python 3.14.4, Unicorn 2.1.4 y pefile
2024.8.26: 403 casos, 405 renglones, 67.324 bytes. SHA-256 de `inverse_rotation.txt`:
`91f093559d176dc353398df9f0c02e7fb2e1a27781c23c7a77af422909c00ad5`.

## Los objetos de Devias en la memoria del original: `devias_object_lists.txt`

No sale de un emulador: es un resumen numérico de la memoria del `main.exe` original, leída dos veces
(las dos iguales) en la VM del 2026-11-09, con el personaje en Devias (mapa 2) y la escena 5 (P-C37
de [08](../../../docs/research/08-pendientes.md)). La captura está en la rama
`research/cl7-capturas-vmxp-20261109` del repo de research, en
`client-research/analysis/mainexe/evidence/cl7-vmxp-2026-11-09/`; acá va solo el resumen, sin
volcados. Lo compara `world/test_golden_devias_world_objects.cpp` con lo que MuRet crea de
`World3\EncTerrain3.obj`.

Los renglones que empiezan con `#` son comentarios. Cada uno de los demás es una de las 256 listas de
`0x7d325d0` que tiene nodos: el número de la lista (sector x · 16 + sector y) y, en el orden de la
lista, el índice en el archivo del registro de cada nodo. Los dos nodos que no salen del archivo, el
primero de las listas 176 y 177, van con su tipo en hexadecimal (`0x201` y `0x202`): los crea
`0x616ec0` al cargar el mapa 2. El 492 y el 493, con la y en NaN, están en la lista 32 (Q-C186 de
[07](../../../docs/research/07-quirks-y-bugs.md)). 218 renglones; SHA-256 de `devias_object_lists.txt`:
`d94f3ea8b57ee2f1e14a5748900be20a3fb4aee29695332619b1ba2c650dc6ac`.
