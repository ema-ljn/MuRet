# La cámara del juego (CL7.3)

`0x635ae0` corre una vez por frame dibujado, dentro del dibujo de la escena (`0x6371a0` → `0x637241`),
también en el login (`0x6350b0`) y en la selección (`0x634680`). Ver 06 § La cámara del juego.

Todos necesitan `MURET_MAIN_EXE`; `att_survey.py`, solo el `Data\`.

| Script | Qué reproduce | Sub-ola |
| --- | --- | --- |
| `emu_camera.py` | `0x635ae0` y `0x620db0` (el armado del pase 3D: `glViewport`, `gluPerspective`, los giros) con el código real. `CameraEmu` es el arnés: los inicializadores `0x403220` (el `CCameraMove`) y `0x635ad0` (D = 1.000), el héroe en `0x743a170`, el mapa 0, la escena 5 y el yaw −45 del primer paso (`0x636838`) | CL7.3 A, CL7.3 B |
| `camera_reference.py` | `0x635ae0` reescrita en doubles con un `f32` donde el binario guarda float, con `0x5fc8f0` (la matriz) y `0x5fcc30` (`VectorIRotate`), contra el emulado en casos al azar | CL7.3 A, tareas 1 y 3 |
| `make_camera_vectors.py` | el borrador de `Client/tests/vectors/game/camera.txt`: 8 casos, 480 frames, cada uno con la entrada y la salida en los bits de cada float | CL7.3 A, tarea 2 |
| `att_survey.py` | los bits `0x80` (el zoom) y `0x40` (la altura fija de `0x6c9d5c`) del `.att` de cada mundo, y el rectángulo del far (16.100–19.000 × 18.900–21.700) | CL7.3 A (`TheFarIs3000InsideTheRectangleOnAnyMap`), CL7.5 E |

Los casos de `emu_camera.py`:

| Caso | Qué muestra |
| --- | --- |
| `lorencia` | el héroe de la traza en (14.350; 12.050; 165) con el atributo `0x01`: la posición `0x466b446d`, `0x46313b93`, `0x447dc000`, igual a la traza bit a bit, y el pase de `0x620db0` |
| `zoom` | 30 frames con `0x81` y 30 con `0x01`: el zoom sube de a 10 hasta 210 y vuelve |
| `bit40` | la altura fija: 1.201 desde el primer `0x40` |
| `rect` | el far de 3.000 en los bordes del rectángulo y afuera |
| `level` | el far por el nivel del frame anterior y la D que baja de 1.400 a `0x447a0001` |
| `tour` | el `CCameraMove` (`0x6e57a8`) en el estado 2: el recorrido de `0xBD 00` |
| `map5` | el balanceo del mapa 5 con el reloj de `0x5767b88` |
| `webzen` | el mapa 6 con un héroe cuyo nombre contiene "webzen" (`0x576960`; los nombres del caso son inventados) |
| `shake` | el temblor de `0x7d325c8` sumado al pitch |
| `scene24` | las cámaras fijas del login y de la selección |

Desde la raíz del repo, con la carpeta delante (`Client/docs/research/scripts/game/camera/`):

```bash
python -I emu_camera.py lorencia rect          # sin casos, todos
python -I camera_reference.py 60 40            # "frames 2400, distintos 0"
python -I make_camera_vectors.py camera.txt    # el borrador; la tarea 2 lo agranda
python -I att_survey.py 1 2 3
```

`make_camera_vectors.py` es el borrador del barrido: la tarea 2 del plan lo lleva a unos 3.000 frames con
cinco semillas, "webzen", la D y el zoom de entrada, el nivel y los bordes del rectángulo ±0,01. El plan
lo nombra `game/make_camera_vectors.py`; está acá, en `game/camera/`.

Los `rand()` que gasta `0x635ae0` por frame (8, por la tabla de protección) los cuenta
`../protection/camera_rand.py`.
