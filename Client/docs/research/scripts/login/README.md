# Los scripts del barrido del login (CL7.R)

Lo que respalda [06 § El mundo del login](../../06-render-escenas-y-ui.md#el-mundo-del-login) y las
secciones que le siguen: el código real del `main.exe` emulado con unicorn, una reimplementación
escrita solo desde la lectura del asm y las cuentas sobre el `Data\` del mapa 55.

## Cómo se corren

- **Las emulaciones** (`emu.py`, `draw_emu.py`, `dragon_emu.py`, `run_objs.py`, `run_draw.py`,
  `emulate_raven.py`, `emulate_raven_ctor.py`) necesitan `unicorn` y `pefile`, y la ruta del
  `main.exe` (`c32bbd5f`) en `MURET_MAIN_EXE`. Las del cuervo usan el emulador de la tela
  (`../emulate_cloth.py`).
- **Las cuentas geométricas** (`cam.py`, `pose.py`, `objvis.py`) necesitan `numpy`.
- **El `Data\`** es el del repo (`Client/assets/Data`), o el de `MURET_CLIENT_DATA`.
- Lo que escriben (`dragon_traj.json`, `objs_draw.json`) va a la carpeta actual. Ninguna salida se
  guarda en Git: se vuelven a sacar del binario.

```bash
export MURET_MAIN_EXE=/ruta/a/main.exe
python dragon_emu.py 1500           # el dragón: 0x518cf0 y 1.500 pasos de 0x5208a0
python emulate_raven.py 7 600 cell0 > raven.json
python raven_reference.py 7 600 cell0 raven.json   # "bit a bit igual en todos los pasos"
```

## Qué hace cada uno

| Script | Qué hace |
| --- | --- |
| `emu.py` | el harness: el PE mapeado, los imports como stubs, el `rand()` de la CRT y los modelos de mentira para los creadores |
| `pe.py` | lee bytes y constantes de la imagen |
| `world.py`, `bmd.py` | leen el `.obj`, el `.att`, las alturas y los `.bmd` del mapa 55 |
| `cam.py`, `pose.py` | la cámara de `LoginCamera` y la pose de un modelo en su key 0 |
| `objvis.py` | qué objetos del `.obj` caen en el viewport del login (los 217) |
| `run_objs.py`, `draw_emu.py`, `run_draw.py` | cada tipo del `.obj`: la creación (`0x606660`), tres pasos (`0x604ab0`) y el dibujo (`0x6002e0`), con las llamadas a `RenderBody` y a `RenderMesh` |
| `dragon_emu.py` | el dragón: el efecto `0xb4` y su vuelo, paso a paso |
| `emulate_raven_ctor.py` | el constructor del cuervo (`0x4167b0` con `0x415be0`) y su estado inicial |
| `emulate_raven.py` | el paso real del cuervo (`0x416a20`) con el dueño quieto del login, y cada `rand()` con su sitio |
| `raven_reference.py` | el paso del cuervo escrito desde la lectura, comparado bit a bit con la salida de `emulate_raven.py` |
| `embers_sim.py` | una simulación en Python (no una emulación) de las brasas, con las alturas de `World56`: cuánto duran en el aire |
