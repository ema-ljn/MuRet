# Las herramientas del binario

Para leer el `main.exe` (`c32bbd5f`) con las direcciones del research: desensamblar un tramo, ver
quién llama a quién, leer una constante por sus bits o una tabla de saltos. Son las que usó el
barrido de Lorencia (CL7), limpias y sin rutas fijas.

Necesitan el exe en `MURET_MAIN_EXE` (o `--exe`), `pefile` y `capstone` (`../requirements.txt`) y,
para la caché, `objdump` de GNU binutils. Cómo conseguir cada cosa: [`../README.md`, El
entorno](../README.md#el-entorno).

## La caché

`funcof.py`, `reach.py`, `cg.py` y `listing.py` (y `game/maps/mapcmp.py` y `map0.py`) no leen el exe:
leen el listado de objdump y el grafo de llamadas que arma una vez `regen_cache.py`, en
`MURET_RESEARCH_CACHE`. Sin la variable, la caché va a `.research-cache/` en la raíz del repo, que
Git ignora. Son unos 50 MB y se vuelven a sacar del exe en unos segundos: no se versionan.

```bash
export MURET_MAIN_EXE=/ruta/a/main.exe      # el c32bbd5f
python -I Client/docs/research/scripts/tools/regen_cache.py
```

`source.txt` guarda el SHA-256 del exe; si `MURET_MAIN_EXE` apunta a otro, las herramientas avisan.

## Una línea por herramienta

| Herramienta | Qué hace | Ejemplo |
| --- | --- | --- |
| `regen_cache.py` | arma la caché: `all.txt` (`objdump -d -M intel --no-show-raw-insn`), `cg.pkl` y `source.txt` | `regen_cache.py` |
| `disas.py` | desensambla un rango con capstone, directo del exe | `disas.py 635ae0 635e40 > camara.asm` |
| `listing.py` | el mismo rango, del listado de objdump de la caché | `listing.py 5fcc30 5fcc90` |
| `funcof.py` | la función que contiene cada dirección y quién la llama; con `--group`, las direcciones juntadas por función | `funcof.py 635b20` |
| `reach.py` | si unas raíces llegan a unos destinos por `call` directos, y el camino | `reach.py 6371a0,6350b0 635ae0` |
| `cg.py` | desde una función y hasta una profundidad, cada llamada a uno de los destinos (y el módulo del grafo que usan las otras) | `cg.py 6371a0 2 635ae0 5fbb00` |
| `rd.py` | constantes de la imagen: `f` float con sus bits, `d`, `i`, `s` string, `p` puntero a string | `rd.py 6ba360 6c9e0c:i 6d2aec:s` |
| `jt.py` | la tabla de saltos de un `switch`: qué casos van a cada destino | `jt.py 606ed4 606ea0 0x97` |
| `imp.py` | el import de cada slot de la IAT (DLL y nombre) | `imp.py 6b8588` |
| `strref.py` | dónde está un texto en la imagen y qué `push imm32` lo usan | `strref.py FireLight Gate.bmd` |
| `image.py`, `cg.py` | los módulos que comparten: la imagen mapeada y la caché | — |

El grafo es aproximado (ver `cg.py`): solo sigue los `call` directos, y una función sin marco y sin
`call` directo queda pegada a la anterior. Lo que dice `reach.py` que no se alcanza puede alcanzarse
por una tabla o un puntero.
