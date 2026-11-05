# Lorencia, el mapa 0 (CL7.L2 a L5)

Los objetos del `.obj` de Lorencia con el código real, sus texturas en la memoria del original y en la
traza, y la luz dinámica (06 § Lo propio de Lorencia). El `.obj`, los `.bmd` y el OZJ son los del
`Data\` (`World1\`, `Object1\`).

| Script | Qué reproduce | Necesita | Sub-ola |
| --- | --- | --- | --- |
| `lorencia_models.py` | la tabla de los modelos con nombre del mapa 0 (tipo ← `Object1\<nombre>.bmd`, `0x618e4f..0x619edf`), que usan los demás | — | CL7.L2 (la tabla como dato) |
| `obj0.py` | el `.obj`: la cabecera, los 2.870 registros y cada tipo con su nombre, cuántos hay y si falta su `.bmd` (ninguno) | el `Data\` | CL7.L2 |
| `run_objs0.py` | los 30 tipos con caso del mapa 0: la creación (`0x606660`, el `switch` de `0x6069a3`), tres pasos (`0x604ab0`) y el dibujo (`0x602e90` → `0x602ed0`, `0x6002e0`), con `RenderBody`, `RenderMesh` y los `rand()`, con el arnés de `../../login/draw_emu.py`; escribe `objs0_emu.json` | `MURET_MAIN_EXE`, el `Data\` | CL7.L2 (la base de `make_world0_vectors.py`, como `run_objs.py` de `make_world55_vectors.py`) |
| `lor_objs.py` | los 109 tipos: la creación y un paso con un modelo de mentira, la tabla de las poses (`0x7d34818`), las partículas que piden (`0x743a198`) y la luz dinámica que tocan (`0x7b4ccc4`) | `MURET_MAIN_EXE`, el `Data\` | CL7.L2 (los pedidos), CL7.L4 (la luz), CL7.L5 (las partículas) |
| `slots.py` | la tabla de texturas (`0x7d3a198`, 1.845 slots de `0x38`) del volcado `lor_slots.bin` | `MURET_CLIENT_MEMORY_DIR` | CL7.L2 (el golden de los slots: `0x41c..0x47e`, `bird.tga` y `fish.tga` en `0x69` y `0x6a`) |
| `trace_tex.py` | los bloques del pase del mundo de la traza de Lorencia con su slot, su archivo y su mezcla (la textura de GL + 595 da el slot del volcado) | `MURET_CLIENT_GL_TRACE_DIR`, `MURET_CLIENT_MEMORY_DIR` | CL7.L2 (los 120 bloques de objetos contra el lote) |
| `brighter.py` | las esquinas del terreno de la traza más brillantes que `TerrainLight.OZJ`: 27, 23 alrededor del `FireLight01` #1933 (`0x5fbf60`, `0x5f9780`) y 4 en la fila 136 sin identificar | `MURET_CLIENT_GL_TRACE_DIR`, el `Data\`, Pillow | CL7.L4 |

Desde la raíz del repo, con la carpeta delante (`Client/docs/research/scripts/game/world0/`):

```bash
python -I obj0.py
python -I run_objs0.py 32 69          # tipos en hexadecimal; sin tipos, los 30
python -I lor_objs.py                 # unos 20 s
python -I slots.py
python -I trace_tex.py
python -I brighter.py
```

Los números de línea que dan `trace_tex.py` y `brighter.py` son del pase del mundo del primer frame
(`common.trace_frame`: la línea 1 es su `glClearColor`, la 59 de `lorencia-Frame_016215.txt`), los mismos
que cita el informe del barrido. El volcado es del 2026-10-09 con el build `2f16a9c6` y la traza de otro
día con el `c32bbd5f`: el corrimiento de 595 entre sus nombres de GL es el de esas dos sesiones (09 § Las
texturas de las trazas ya tienen archivo).

Lo vivo de la L5 (las hojas `0x52fa60`, los pájaros `0x446120`, los peces `0x447030`, los fuegos
`0x52f360`) no tiene script propio todavía: se emula con el mismo arnés, por paso y con semilla, como
`../../login/make_leaf_vectors.py` y `make_particle_vectors.py`.
