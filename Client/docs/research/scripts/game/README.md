# Los scripts del juego (CL7: Lorencia)

Lo que respalda el barrido de Lorencia y lo que reusan las sub-olas del
[plan de la CL7](../../../../../docs/superpowers/plans/2026-11-05-ola-cl7-lorencia.md): el código real del
`main.exe` emulado con unicorn, los generadores de vectores, y los lectores del `Data\`, de las trazas
de GL, de los volcados de memoria y del cable para comparar contra ellos. Lo que vale de cada
resultado está en el research (06 § La escena del juego, § La cámara del juego, § Lo propio de
Lorencia, § Caminar y § Los cambios de mapa); acá queda cómo volver a sacarlo.

## Las carpetas

| Carpeta | Qué hay | Sub-olas |
| --- | --- | --- |
| [`camera/`](camera/README.md) | `0x635ae0` real, su reimplementación bit a bit y el borrador de `camera.txt` | CL7.3 A, CL7.3 B |
| [`protection/`](protection/README.md) | los `rand()` de la tabla de protección `0x57630c8` | CL7.P |
| [`world0/`](world0/README.md) | los objetos de Lorencia (creación, paso, dibujo), sus texturas en el volcado y en la traza, la luz | CL7.L2, CL7.L4, CL7.L5 |
| [`walk/`](walk/README.md) | el camino (`0x4eac00`, el A* de `0x4ead70`), el paquete (`0x56acd0`) y el paso del héroe | CL7.4 A, B, C |
| [`wire/`](wire/README.md) | el cable en claro (el `muproxy.log` y el PCAP de la PR #45) y sus estadísticas del caminar | CL7.L1, L3, CL7.4 B, E, CL7.5 C, D |
| [`maps/`](maps/README.md) | `Gate.bmd`, los objetos de cada mundo, el nombre de cada mapa y lo que cada mapa hace distinto | CL7.5 A, B, D, E, CL7.L2 |

`common.py` es lo que comparten: dónde está cada entrada (las variables de entorno), los lectores del
`Data\` (`.obj`, `.att`, alturas, con la carpeta sin mirar mayúsculas: `World7` y `world8` conviven) y el
pase del mundo de un frame de una traza.

## Cómo se corren

Cómo conseguir cada entrada y qué variable la nombra: [`../README.md`, El entorno](../README.md#el-entorno).
En corto:

- las emulaciones necesitan `MURET_MAIN_EXE` (el `c32bbd5f`), `unicorn` y `pefile`. Usan el emulador
  de la tela (`../emulate_cloth.py`, `ClothEmulator`) o el arnés del login (`../login/emu.py` y
  `draw_emu.py`): ninguna copia propia;
- el `Data\` es el del repo (`Client/assets/Data`), o el de `MURET_CLIENT_DATA`;
- las trazas de GL, el cable y los volcados, de `MURET_CLIENT_GL_TRACE_DIR`, `MURET_CLIENT_WIRE_DIR` y
  `MURET_CLIENT_MEMORY_DIR`; los C3 y C4 del cable, además, de `MURET_C3C4_REFERENCE`;
- `maps/mapcmp.py` y `maps/map0.py` leen la caché de [`../tools/`](../tools/README.md)
  (`regen_cache.py`).

Con `python -I`, desde fuera de las carpetas de los scripts (por ejemplo, la raíz del repo): cada script
agrega a `sys.path` solo lo que importa.

```bash
export MURET_MAIN_EXE=/ruta/a/main.exe
PY="python -I"
$PY Client/docs/research/scripts/game/camera/emu_camera.py lorencia     # "igual a la traza (bits): True"
$PY Client/docs/research/scripts/game/camera/camera_reference.py        # "frames 2400, distintos 0"
$PY Client/docs/research/scripts/game/walk/wire_vs_path.py              # 169 iguales y 2 distintos
$PY Client/docs/research/scripts/game/protection/protection_rand.py     # 3.848 y 4 rand()
```

## Lo que sale y la privacidad

- Lo que escriben (`objs0_emu.json`, un `camera.txt`, un log del PCAP) va a la carpeta actual o a la
  ruta que se les da, y **no se guarda acá**: se vuelve a sacar. Los vectores de los tests los arma
  cada sub-ola en `Client/tests/vectors/` con su generador, como `login/make_world55_vectors.py`.
- El cable lleva el login, el chat y los nombres de los personajes. `wire/decode.py` saca por defecto
  solo los paquetes que el plan deja guardar (la respuesta del `F3 03`, el `F3 10`, `D3`, `D7`, `1C`,
  `F3 04`, `F3 12`, `0F` y `A3`); lo demás, solo con `--all`, y esa salida no va a Git ni a un informe.
  Ningún script ni su salida lleva un nombre de personaje o de cuenta.
