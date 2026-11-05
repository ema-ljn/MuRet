# La tabla de protección (CL7.P)

El original guarda la celda del héroe, el contador de warp `0x77f3b04`, el contador de frames
`0x7d3a0f0`, el registro propio `0x7681eb4` y otras variables en una tabla cifrada (`0x57630c8`). Cada
acceso que se cierra las vuelve a cifrar con `rand()`. MuRet guarda los valores en claro pero gasta
los mismos `rand()` en el mismo lugar (decisión 3 del plan; 06 § La tabla de protección, Q-C125).

Los dos necesitan `MURET_MAIN_EXE`. El `rand()` es el de la CRT (`0x69fd70`) desde el estado 1.

| Script | Qué reproduce | Sub-ola |
| --- | --- | --- |
| `protection_rand.py` | los dos cierres con el código real: `0x40b4d0`, el del registro propio de `0xf08` bytes (3.848 `rand()`, uno por byte), y `0x412890`, el de una variable de 4 bytes (4) | CL7.P (un test por clase de acceso) |
| `camera_rand.py` | los `rand()` de `0x635ae0` por frame dibujado (8: la celda del héroe pasa por la tabla), en tres frames seguidos y en las escenas 2, 4 y 5 | CL7.P, CL7.3 B (los golden del login cambian con la CL7.P) |

Desde la raíz del repo, con la carpeta delante (`Client/docs/research/scripts/game/protection/`):

```bash
python -I protection_rand.py     # 3848 y 4
python -I camera_rand.py         # 8 por frame, en las tres escenas
```

Lo que falta leer de la tabla (los otros accesos: abrir, leer, escribir, rearmar; `0x57cfc0`,
`0x508f00`, `0x4c3940`) lo lista el plan en la CL7.P; se agrega acá con el mismo arnés
(`ClothEmulator` con `ecx = 0x57630c8` y un `Rand` propio por llamada).
