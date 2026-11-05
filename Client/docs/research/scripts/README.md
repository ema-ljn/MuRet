# Los scripts del research del cliente

Lo que respalda el research de `Client/docs/research/` y lo que reusan los planes: el código real del
`main.exe` emulado con unicorn, reimplementaciones escritas desde la lectura del asm para compararlas
con él, generadores de los vectores de `Client/tests/vectors/` y lectores del `Data\`, de las trazas de
GL, de los volcados de memoria y del cable.

| Qué | Dónde | Ola |
| --- | --- | --- |
| la tela: el oráculo `ClothEmulator` (que usan los demás), su contrato en Python, los casos y el conversor a vectores | `emulate_cloth.py`, `cloth_reference.py`, `run_cloth_cases.py`, `make_cloth_vectors.py` | CL6.6 |
| los cargadores de modelos | `emulate_model_loaders.py` | CL6.1 |
| el barrido del login: el mundo 55, el dragón, el cuervo, el recorte del terreno | [`login/`](login/README.md) | CL7.R |
| las herramientas del binario: desensamblar, el grafo de llamadas, constantes, tablas de saltos | [`tools/`](tools/README.md) | todas |
| el barrido de Lorencia: la cámara, la tabla de protección, los objetos, caminar, el cable, los mapas | [`game/`](game/README.md) | CL7 |
| los paquetes de Python | [`requirements.txt`](requirements.txt) | todas |

Nada de lo que sacan se guarda acá: ni las salidas, ni los listados, ni los volcados. Los vectores de los
tests los arma cada sub-ola con su generador y su fila en el README de la carpeta de vectores.

## El entorno

Lo que hace falta para correr cualquier script de esta carpeta, de dónde sale cada cosa y qué variable
la nombra. Ningún script tiene una ruta fija: todo sale de una variable de entorno o de un argumento.

### Los repos

Además de este, dos repos del laboratorio de Emanuel. Los dos son grandes; con `--depth 1` alcanza.

| Repo | Qué trae | Rama |
| --- | --- | --- |
| [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i) | el `main.exe` `c32bbd5f` y su decompilado, las claves del servidor, los volcados de memoria, `c3c4_reference.py` | `master` (`14f0f931`, 2026-11-01) |
| [EmaNLJN/mu-online-reverse-lab](https://github.com/EmaNLJN/mu-online-reverse-lab) | el cable (`muproxy.log`), una copia del `Data\` | `main` (`9f83b4c7`, 2026-10-29) |
| ídem, la PR #45 | las trazas de GL y el PCAP de una sesión de juego | `feature/gameserver-passive-capture-20261030` (`cb575444`, 2026-10-31), sin mergear a `main` |

```bash
git clone --depth 1 https://github.com/EmaNLJN/mu-server-repack-research-102i "$RESEARCH"
git clone --depth 1 https://github.com/EmaNLJN/mu-online-reverse-lab "$LAB"
git -C "$LAB" fetch --depth 1 origin pull/45/head:pr45     # la rama de la PR #45, como pr45
```

`$RESEARCH` y `$LAB` son donde quedaron los clones; abajo, las rutas son desde ahí.

### Cada entrada

**El `main.exe`** (`MURET_MAIN_EXE`). El build `c32bbd5f`, el de las direcciones del research:
`$RESEARCH/client-research/mainexe-vmxp-c32bbd5f/original/main.exe`, de 3.058.308 bytes, con SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a` (en `SHA256SUMS` y
`reports/build-identity.md` de la misma carpeta). Está en `master` y en las dos ramas `x-…` del repo.
**No es** el `analysis/mainexe/originals/main.exe` de los dos repos: ese es el build histórico
`2f16a9c6…`, con el mismo `.text` y otras `.rdata` y `.data` ([README del research](../README.md#dos-builds-que-no-se-mezclan)).

**El decompilado.** En la misma carpeta, `decompiled/functions/`: un `.c` y un `.asm` por cada una de
las 5.648 funciones (`00401000_FUN_00401000.c`); `kali/decompiled/functions/`, el mismo binario con otro
motor; y el paquete entero, `client-research/mainexe-vmxp-c32bbd5f-decompilation.tar.gz` (su SHA-256,
`82f417fe…`, en el `.sha256` de al lado). Es una guía: la última palabra es el asm, que da
[`tools/`](tools/README.md).

**El `Data\`** (`MURET_CLIENT_DATA`, opcional). El de este repo, `Client/assets/Data/` (el original
entero; AGENTS.md). Es el mismo que `$LAB/analysis/decompilacion-maxima/assets/original-data/` en `main`.
Sin la variable, los scripts usan el del repo.

**Las trazas de GL** (`MURET_CLIENT_GL_TRACE_DIR`). GLIntercept 1.34 sobre el original, en la selección y
en Lorencia: `reports/gameserver-passive-2026-10-30/graphics-01/character-selection-Frame_003207.txt.gz` y
`lorencia-Frame_016215.txt.gz`, en la rama de la PR #45. Se descomprimen en una carpeta propia:

```bash
mkdir -p /ruta/a/trazas && cd /ruta/a/trazas
for f in character-selection-Frame_003207 lorencia-Frame_016215; do
  git -C "$LAB" show pr45:reports/gameserver-passive-2026-10-30/graphics-01/$f.txt.gz | gunzip > $f.txt
done
```

**El cable y las claves** (`MURET_CLIENT_WIRE_DIR`). Una carpeta con tres enlaces, como la de los tests
del cliente ([`Client/README.md`, Tests](../../../README.md#tests)): el `muproxy.log` que escribió el
proxy del laboratorio, `$LAB/analysis/mainexe/evidence/muproxy.log` (en `main` y en todas las ramas), y las
claves del servidor con que se capturó, `$RESEARCH/gameserver/evidence/keyfiles/Dec1.dat` y `Enc2.dat`
(las mismas están en `$LAB/analysis/gameserver/evidence/keyfiles/`). **El `muproxy.log` lleva el login del
jugador y el chat**: no se copia a ningún lado de este repo.

```bash
mkdir -p /ruta/a/cable
ln -s "$LAB/analysis/mainexe/evidence/muproxy.log" /ruta/a/cable/
ln -s "$RESEARCH/gameserver/evidence/keyfiles/Dec1.dat" "$RESEARCH/gameserver/evidence/keyfiles/Enc2.dat" /ruta/a/cable/
```

**El SimpleModulus del C3/C4** (`MURET_C3C4_REFERENCE`). Para descifrar los C3 y C4 del cable,
`game/wire/` usa la referencia del laboratorio, `$RESEARCH/client-research/analysis/mainexe/scripts/c3c4_reference.py`
(la misma que `$LAB/analysis/mainexe/scripts/c3c4_reference.py`). Sin ella, los C1 y C2 se leen igual.

**El PCAP de la PR #45** (sin variable: se le pasa a `game/wire/pcap2log.py`). La sesión de juego del
2026-10-30, con los cambios de mapa que usa la CL7.5 C:
`reports/gameserver-passive-2026-10-30/capture_20261030T192219Z_8920d1.tar.gz` en la rama de la PR, con
`20261030T192219Z_8920d1/network.pcap` adentro (y `packets.jsonl`, `events.jsonl`, `manifest.json` y sus
logs). **Lleva el login y el chat con nombres de personaje**: se extrae en una carpeta propia, fuera del
repo.

```bash
mkdir -p /ruta/a/pr45 && cd /ruta/a/pr45
git -C "$LAB" show pr45:reports/gameserver-passive-2026-10-30/capture_20261030T192219Z_8920d1.tar.gz | tar xz
```

**Los volcados de memoria** (`MURET_CLIENT_MEMORY_DIR`). La memoria del original con los archivos ya
cargados: `$RESEARCH/client-research/mu-golden/memoria-cliente/`, en `master`, con una carpeta por captura
(`2026-10-09-devias`, `2026-10-09-lorencia`, `2026-10-09-lorencia-obj`), cada una con su `SHA256SUMS`. Son
del 2026-10-09, con el build `2f16a9c6`. El laboratorio (`$LAB`) no los tiene: solo los scripts que los
leyeron (`analysis/mainexe/scripts/memoria_cliente.py`, `obj_memoria.py`). Este repo no los versiona
(AGENTS.md).

**La caché del binario** (`MURET_RESEARCH_CACHE`). El listado de `objdump` y el grafo de llamadas que
leen `tools/` y `game/maps/`. No viene de ningún repo: la arma `tools/regen_cache.py` desde el exe, en
`.research-cache/` de la raíz (que Git ignora) si la variable no está.

**Lo que no tiene repo.** Todas las entradas de arriba salen de los dos repos o de este. Lo que no está en
ningún repo es lo que se arma a mano o se genera desde ellas: la caché del binario, las carpetas de las
trazas y del cable (descomprimidas y con enlaces), el archivo de variables de abajo, y los informes y las
salidas del barrido de Lorencia, que quedaron fuera de Git (lo que vale de ellos está en el research).

### Las variables

| Variable | Qué es | La leen |
| --- | --- | --- |
| `MURET_MAIN_EXE` | la ruta del `main.exe` `c32bbd5f` | toda emulación, `tools/` |
| `MURET_CLIENT_DATA` | el `Data\`; sin ella, `Client/assets/Data` | `login/`, `game/` |
| `MURET_CLIENT_GL_TRACE_DIR` | la carpeta con las dos trazas descomprimidas | `login/frustum_reference.py`, `check_selection.py`, `game/world0/` |
| `MURET_CLIENT_WIRE_DIR` | la carpeta con `muproxy.log`, `Dec1.dat` y `Enc2.dat` | `game/wire/`, `game/walk/wire_vs_path.py` |
| `MURET_CLIENT_MEMORY_DIR` | la carpeta `memoria-cliente`, con una carpeta por captura | `game/world0/slots.py`, `trace_tex.py` |
| `MURET_C3C4_REFERENCE` | la ruta de `c3c4_reference.py` | `game/wire/` (solo los C3 y C4) |
| `MURET_RESEARCH_CACHE` | la carpeta de la caché del binario; sin ella, `.research-cache/` en la raíz | `tools/`, `game/maps/mapcmp.py`, `map0.py` |
| `MURET_CLOTH_CASES` | adónde escribe `run_cloth_cases.py` sus casos; sin ella, `cloth-cases/` en la carpeta actual | `run_cloth_cases.py` |

Las de los tests del cliente (`MURET_CLIENT_GOLDEN_DIR`, `MURET_CLIENT_FONT_FILE`,
`MURET_CLIENT_REQUIRE_GL`, y las mismas de las trazas, el cable y los volcados) están en
[`Client/README.md`, Tests](../../../README.md#tests). Un archivo para tenerlas todas, fuera del repo:

```bash
# muret-env.sh: source muret-env.sh
export MURET_MAIN_EXE=$RESEARCH/client-research/mainexe-vmxp-c32bbd5f/original/main.exe
export MURET_CLIENT_GOLDEN_DIR=$LAB/analysis/decompilacion-maxima/assets/original-data   # los tests
export MURET_CLIENT_GL_TRACE_DIR=/ruta/a/trazas
export MURET_CLIENT_WIRE_DIR=/ruta/a/cable
export MURET_CLIENT_MEMORY_DIR=$RESEARCH/client-research/mu-golden/memoria-cliente
export MURET_C3C4_REFERENCE=$RESEARCH/client-research/analysis/mainexe/scripts/c3c4_reference.py
```

### Python

Python 3.13 (se corrió con 3.13.18) en un venv propio, con [`requirements.txt`](requirements.txt):
`capstone`, `pefile` y `unicorn` para las emulaciones y el binario; `numpy` para las cuentas del login;
`Pillow` para el JPEG del OZJ de `game/world0/brighter.py`. `tools/regen_cache.py` necesita además
`objdump` de GNU binutils (`apt install binutils`).

```bash
python3 -m venv /ruta/al/venv
/ruta/al/venv/bin/pip install -r Client/docs/research/scripts/requirements.txt
```

**Correr con `python -I` y desde fuera de las carpetas de los scripts** (la raíz del repo, por ejemplo):

```bash
/ruta/al/venv/bin/python -I Client/docs/research/scripts/game/camera/emu_camera.py lorencia
```

`-I` deja afuera las variables `PYTHON*`, el site del usuario y la carpeta del script y la actual en
`sys.path`: así un archivo con nombre de módulo que esté al lado de una captura o de un volcado (un
`json.py` en una carpeta extraída) no se importa por accidente. Las `MURET_*` se leen igual. Cada script
agrega a `sys.path`, explícitamente, solo las carpetas de este repo que importa. Lo que viene de afuera
(el PCAP, las trazas) se extrae en una carpeta propia y se le pasa al script como ruta.

### La privacidad

- AGENTS.md: ni assets del juego (fuera de `Client/assets/Data/`) ni volcados de memoria en Git, solo
  hashes y scripts. El `muproxy.log` lleva el login del jugador y queda afuera; el PCAP de la PR #45 y lo
  que da `game/wire/pcap2log.py`, también.
- Ningún script lleva un nombre de personaje o de cuenta, una clave ni chat. `game/wire/decode.py` saca
  por defecto solo los paquetes que el plan de la CL7 deja guardar; con `--all`, todo, y esa salida no va
  a Git ni a un informe.
