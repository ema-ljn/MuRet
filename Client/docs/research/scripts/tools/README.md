# Las herramientas del binario

Para leer el `main.exe` (`c32bbd5f`) con las direcciones del research: desensamblar un tramo, ver
quién llama a quién, leer una constante por sus bits o una tabla de saltos. Son las que usó el
barrido de Lorencia (CL7), limpias y sin rutas fijas.

Necesitan el exe en `MU_MAIN_EXE` (o `--exe`), `pefile` y `capstone` (`../requirements.txt`) y,
para la caché, `objdump` de GNU binutils. Cómo conseguir cada cosa: [`../README.md`, El
entorno](../README.md#el-entorno).

## La caché

`funcof.py`, `reach.py`, `cg.py` y `listing.py` (y `game/maps/mapcmp.py` y `map0.py`) no leen el exe:
leen el listado de objdump y el grafo de llamadas que arma una vez `regen_cache.py`, en
`MU_RESEARCH_CACHE`. Sin la variable, la caché va a `.research-cache/` en la raíz del repo, que
Git ignora. Son unos 50 MB y se vuelven a sacar del exe en unos segundos: no se versionan.

```bash
export MU_MAIN_EXE=/ruta/a/main.exe      # el c32bbd5f
python -I Client/docs/research/scripts/tools/regen_cache.py
```

`source.txt` guarda el SHA-256 del exe; si `MU_MAIN_EXE` apunta a otro, las herramientas avisan.

## Una línea por herramienta

| Herramienta | Qué hace | Ejemplo |
| --- | --- | --- |
| `regen_cache.py` | arma la caché: `all.txt` (`objdump -d -M intel --no-show-raw-insn`), `cg.pkl` y `source.txt` | `regen_cache.py` |
| `disas.py` | desensambla un rango con capstone, directo del exe | `disas.py 635ae0 635e40 > camara.asm` |
| `listing.py` | el mismo rango, del listado de objdump de la caché | `listing.py 5fcc30 5fcc90` |
| `funcof.py` | la función que contiene cada dirección y quién la llama; con `--group`, las direcciones juntadas por función | `funcof.py 635b20` |
| `reach.py` | si unas raíces llegan a unos destinos por `call` directos, y el camino | `reach.py 6371a0,6350b0 635ae0` |
| `effect_sites.py` | censo crudo CL11.R de calls directos en `.text` con familia y fase | `effect_sites.py > /tmp/effect-sites.tsv` |
| `effect_step_switches.py` | índice de las tablas y los sitios de efectos del paso con modelo `0x5208a0` | `effect_step_switches.py --sites > /tmp/model-step-sites.tsv` |
| `cg.py` | desde una función y hasta una profundidad, cada llamada a uno de los destinos (y el módulo del grafo que usan las otras) | `cg.py 6371a0 2 635ae0 5fbb00` |
| `rd.py` | constantes de la imagen: `f` float con sus bits, `d`, `i`, `s` string, `p` puntero a string | `rd.py 6ba360 6c9e0c:i 6d2aec:s` |
| `jt.py` | la tabla de saltos de un `switch`: qué casos van a cada destino | `jt.py 606ed4 606ea0 0x97` |
| `imp.py` | el import de cada slot de la IAT (DLL y nombre) | `imp.py 6b8588` |
| `strref.py` | dónde está un texto en la imagen y qué `push imm32` lo usan | `strref.py FireLight Gate.bmd` |
| `image.py`, `cg.py` | los módulos que comparten: la imagen mapeada y la caché | — |

El grafo es aproximado (ver `cg.py`): solo sigue los `call` directos, y una función sin marco y sin
`call` directo queda pegada a la anterior. Lo que dice `reach.py` que no se alcanza puede alcanzarse
por una tabla o un puntero.

## La cobertura del roadmap (CL.M)

Cinco scripts aparte, que no usan la caché ni `capstone`: solo la biblioteca estándar. Arman y
verifican la matriz de [10](../../10-cobertura-roadmap.md), una fila por función del `main.exe` con
su ola, y la referencia por función de
[`coverage/referencia/`](../../coverage/referencia/LEEME.md). El método está en esos dos textos.

| Herramienta | Qué hace | Ejemplo |
| --- | --- | --- |
| `build_roadmap_matrix.py` | lee el clon del research (el inventario, el grafo, los exports y el exe de `original/`) y MuRet en un commit, y escribe la matriz; con `--summary`, un JSON con los conteos y el avance | `build_roadmap_matrix.py --research "$RESEARCH" --repo . --ref origin/main --out Client/docs/research/coverage/mainexe-roadmap.tsv` |
| `test_commit_waves.py` | the wave of each commit and the blame of the matrix on made-up git repos: a refactor commit (`AR`) does not change anyone's wave, and the commits in `.git-blame-ignore-revs` are skipped | `python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_commit_waves.py' -v` |
| `check_roadmap_coverage.py` | el verificador del plan de CL.M: el inventario fijado, cada VA una vez con sus columnas originales, olas, estados y evidencia válidos; con `--require-complete`, sin pendientes | `check_roadmap_coverage.py --inventory "$INV" --mapping Client/docs/research/coverage/mainexe-roadmap.tsv --require-complete` |
| `test_check_roadmap_coverage.py` | sus controles negativos, sobre copias de la matriz del repo; el inventario va en `MU_MAINEXE_INVENTORY` | `MU_MAINEXE_INVENTORY="$INV" python3 -I test_check_roadmap_coverage.py` |
| `build_client_reference.py` | con la matriz regenerada (`--out` y `--rows` a `/tmp`), arma la referencia: una fila por función, cada cita con su símbolo, los huecos sin citar, los cortes y las métricas | `build_client_reference.py --research "$RESEARCH" --ref origin/main --matriz /tmp/matriz.tsv --filas /tmp/filas.tsv` |
| `client_reference_symbols.py` | el símbolo de MuRet (función, tabla o caso de test) de cada línea de un `.cpp`/`.h`, contando llaves; lo usa la referencia | — |

`$INV` es `$RESEARCH/client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv`.

## El censo bruto de efectos (CL11.R)

`effect_sites.py` usa Python estándar y lee `all.txt` y `source.txt` de `regen_cache.py`.
Prepará la caché con el exe c32bbd5f y corré estos comandos.

```bash
python3 -I Client/docs/research/scripts/tools/effect_sites.py > /tmp/effect-sites.tsv
python3 -I Client/docs/research/scripts/tools/test_effect_sites.py
```

Emite un TSV ordenado por VA con `site_va`, `target_va`, `familia` y `fase`. Cuenta calls directos
de `.text` y distingue `alta/actualización`, `paso` y `dibujo`. Exige el SHA-256 c32 completo,
VAs únicas y los conteos del research antes de emitir la salida. Un listado truncado o con otro
formato se rechaza si no reproduce esos conteos.

Son 1.486 sitios, de los cuales 1.482 corresponden a `alta/actualización`, uno a `paso` y tres a
`dibujo`. No son cantidades de efectos en ejecución. El censo no atribuye dueño ni alcanzabilidad.
Regenerá `all.txt` y `source.txt` juntos y conservá el TSV fuera de Git. El contexto está en el
[research del cliente](../../README.md).

## Los saltos del paso de efectos con modelo (CL11.R)

`effect_step_switches.py` coteja la misma caché con los bytes del exe
`c32bbd5f` indicado en `MU_MAIN_EXE` o `--exe`. Lee las nueve tablas
de punteros y sus mapas de bytes. Asocia 518 valores de selector con sus
bloques. El subtipo 4 que `0x52b5a1` despacha antes de `0x52b60b` queda
fuera de esa tabla.

```bash
python3 -I Client/docs/research/scripts/tools/effect_step_switches.py > /tmp/model-step-switches.tsv
python3 -I Client/docs/research/scripts/tools/effect_step_switches.py --sites > /tmp/model-step-sites.tsv
python3 -I Client/docs/research/scripts/tools/test_effect_step_switches.py
```

La primera salida registra el sitio del salto, el valor de tipo o subtipo,
el bloque de entrada y los sitios posibles antes de la cola común
`0x52cb98`. Su tabla propia se registra por separado. La salida por sitio
reúne las 233 llamadas directas, incluidas las que llegan desde comparaciones
fuera de las tablas. El recorrido sigue ambas salidas de los saltos
condicionales. Una asociación orienta la lectura del ASM y deja por auditar
las guardas que exigen dueño, mapa, vida o estado del pool.
