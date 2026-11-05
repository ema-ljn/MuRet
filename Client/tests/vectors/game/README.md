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
