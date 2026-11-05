# Los scripts del barrido del login (CL7.R)

Lo que respalda [06 § El mundo del login](../../06-render-escenas-y-ui.md#el-mundo-del-login) y las
secciones que le siguen: el código real del `main.exe` emulado con unicorn, una reimplementación
escrita solo desde la lectura del asm y las cuentas sobre el `Data\` del mapa 55.

## Cómo se corren

- **Las emulaciones** (`emu.py`, `draw_emu.py`, `dragon_emu.py`, `run_objs.py`, `run_draw.py`,
  `emulate_raven.py`, `emulate_raven_ctor.py`) necesitan `unicorn` y `pefile`, y la ruta del
  `main.exe` (`c32bbd5f`) en `MURET_MAIN_EXE`. Las del cuervo usan el emulador de la tela
  (`../emulate_cloth.py`).
- **Las cuentas geométricas** (`cam.py`, `pose.py`, `objvis.py`, `terrain_count.py`, `dragon_screen.py`)
  necesitan `numpy`.
- **El `Data\`** es el del repo (`Client/assets/Data`), o el de `MURET_CLIENT_DATA`.
- Lo que escriben (`dragon_traj.json`, `objs_draw.json`) va a la carpeta actual y no se guarda acá:
  se vuelve a sacar del binario. Los vectores de los tests (`Client/tests/vectors/`) los arma cada
  sub-ola de la CL7 desde estas salidas, con su conversor, como `make_cloth_vectors.py` en la tela.
- `emulate_raven.py semilla pasos arranque [contador]`: el arranque `cell0` es el del login (en
  (50, 50) y la altura del terreno + 300, Q-C130) y `near`, ya al lado del dueño (250 sobre él); el
  contador es el valor de `0x7d3a0f0` que lee el grito, que sale solo si es múltiplo de 60 (por
  defecto `0x12345677`, que no lo es).

```bash
export MURET_MAIN_EXE=/ruta/a/main.exe
python dragon_emu.py 1500           # el dragón: 0x518cf0 y 1.500 pasos de 0x5208a0
python emulate_raven.py 7 600 cell0 > raven.json
python raven_reference.py 7 600 cell0 raven.json   # "bit a bit igual en todos los pasos"
python raven_stats.py 1 40 5000      # el salto y la órbita del cuervo en 40 semillas
python terrain_count.py              # las celdas del terreno del login
python dragon_screen.py              # el dragón en la pantalla (con dragon_traj.json)
```

## Qué hace cada uno

| Script | Qué hace |
| --- | --- |
| `emu.py` | el harness: el PE mapeado, los imports como stubs, el `rand()` de la CRT y los modelos de mentira para los creadores |
| `pe.py` | lee bytes y constantes de la imagen |
| `world.py`, `bmd.py` | leen el `.obj`, el `.map`, el `.att`, las alturas y los `.bmd` del mapa 55; `world.height_at` es la altura de `0x5f93c0` |
| `cam.py`, `pose.py` | la cámara de `LoginCamera` y la pose de un modelo en su key 0 |
| `objvis.py` | qué objetos del `.obj` caen en el viewport del login (los 217) |
| `run_objs.py`, `draw_emu.py`, `run_draw.py` | cada tipo del `.obj`: la creación (`0x606660`), tres pasos (`0x604ab0`) y el dibujo (`0x6002e0`), con las llamadas a `RenderBody` y a `RenderMesh` |
| `dragon_emu.py` | el dragón: el efecto `0xb4` y su vuelo, paso a paso |
| `emulate_raven_ctor.py` | el constructor del cuervo (`0x4167b0` con `0x415be0`) y su estado inicial |
| `emulate_raven.py` | el paso real del cuervo (`0x416a20`) con el dueño quieto del login, y cada `rand()` con su sitio |
| `raven_reference.py` | el paso del cuervo escrito desde la lectura, comparado bit a bit con la salida de `emulate_raven.py` |
| `raven_stats.py` | el salto y la órbita del cuervo en muchas semillas, con `raven_reference.py` |
| `terrain_count.py` | las celdas del terreno del login: el bit `0x08`, las que se dibujan, el agua y el slot `0x122` |
| `dragon_screen.py` | dónde y de qué tamaño se ve el dragón en la pantalla del login, en toda su trayectoria |
| `embers_sim.py` | una simulación en Python (no una emulación) de las brasas, con las alturas de `World56`: cuánto duran en el aire |
