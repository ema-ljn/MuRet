# Los otros mapas, los gates y los cambios de mapa (CL7.5)

Lo que carga cada mundo, `Gate.bmd` y lo que cada mapa hace distinto (05 § `Gate.bmd`, 06 § Los cambios
de mapa).

| Script | Qué reproduce | Necesita | Sub-ola |
| --- | --- | --- | --- |
| `objsurvey.py` | para cada mundo del `Data\`: los registros y los tipos del `.obj`, la carpeta `Object<N>` y los tipos sin modelo (por nombre en Lorencia, `Object<k+1>.bmd` en los demás, el lazo de `0x61a0c4..0x61a124`); el −515 de `World7` incluido | el `Data\` | CL7.5 A (`missing-models.txt`) |
| `gate.py` | `Gate.bmd` como lo lee `0x54a360`: 256 registros de 9 bytes con XOR3, en los campos de `GateRecord` | el `Data\` | CL7.5 B |
| `mapname.py` | `0x5f0b40` para los 256 mapas: el número de texto de `Text.bmd` con su nombre (el del mensaje de llegada), con el arnés de `../../login/emu.py` | `MURET_MAIN_EXE` | CL7.5 D |
| `mapcmp.py` | cada lectura de `0x6c9dd0` (el mapa) por función, con los inmediatos con que se compara | la caché de `../../tools/` | CL7.5 E (la tabla por mapa), CL7.L2 |
| `map0.py` | dónde se pregunta por el mapa 0: `test r,r`, `cmp …,0x0` y las tablas de saltos con el mapa (que se leen con `../../tools/jt.py`) | la caché de `../../tools/` | CL7.L2 (los casos del mapa 0), CL7.5 E |

Desde la raíz del repo, con la carpeta delante (`Client/docs/research/scripts/game/maps/`):

```bash
python -I objsurvey.py                 # o los mundos: objsurvey.py 1 7
python -I gate.py
python -I mapname.py
python -I ../../tools/regen_cache.py   # una vez, para los dos que siguen
python -I mapcmp.py                    # o otro global: mapcmp.py 6d5dc8
python -I map0.py
```

El cambio de mapa en el cable (`1C`, `F3 04`, `F3 12`) se saca con `../wire/decode.py --heads
1c,f304,f312`, del `muproxy.log` y del PCAP de la PR #45.
