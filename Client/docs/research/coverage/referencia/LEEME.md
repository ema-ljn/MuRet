# Referencia del cliente: métricas y trazas contra el `main.exe`

Una tabla por cada cosa que se consulta seguido, para no volver a medir ni a buscar lo mismo cada vez. Está generada
sobre MuRet `52bf9dbb` (2026-11-20: `main` en `2cad3274`, con la CL11 cerrada, el CI de #379, los globos de la
selección de #383 y los cierres de las ventanas de los NPC al caminar de #385; la rama de #386, con el tooltip de la
armadura de Chaos Castle, el barrido de las tiendas, el zen del `22` corto y el `F3 06`; y la de #387, apilada, con el
ítem 3D del diálogo de vender, `0x633110`, y las uniones del `0x1a18` de la tienda de la Potion Girl, Q-C1821)
contra el `main.exe` c32bbd5f del repo de research; `generado.json` dice el commit exacto. Sale de la auditoría del
2026-11-20 (`analisis/2026-11-20-auditoria-cliente/` en los archivos del proyecto) y de
[10-cobertura-roadmap.md](../../10-cobertura-roadmap.md).

Las columnas que vienen de la matriz CL.M, de las citas y del grafo de llamadas se rehacen solas en cualquier commit
(ver [Cómo regenerarla](#cómo-regenerarla)). Las que vienen de la auditoría (la categoría de lo que falta, la clase de
los cortes, el chat, el tipo de las filas de 08 y los pasos de VM) se hicieron a mano: quedan fijas en `entradas/`
hasta que alguien las vuelva a revisar.

## Los archivos

| archivo | filas | qué es |
| --- | ---: | --- |
| `funciones.tsv` | 5.648 | **una fila por función del `main.exe`**: ola, estado, cuánto está hecho, dónde está en MuRet (archivo y símbolo), tests, cortes, 07/08, VM, llamadores, los textos de su pseudocódigo y, si falta, por qué |
| `trazas.tsv` | ~55.800 | **cada cita de una VA en `Client/src` y `Client/tests`**: la función que la contiene, archivo:línea, el símbolo de MuRet (función, tabla o caso de test) y si es un corte |
| `trazas-docs.tsv` | ~29.500 | lo mismo en `Client/docs/research` y `docs/` (sin los TSV derivados): dónde está documentada cada función |
| `globals.tsv` | ~1.230 | **each address of the original's `.data` that `Client/src` cites**: the data member that holds it (its home), the home's module and the other modules citing it (AR3) |
| `huecos.tsv` | ~300 | **los bloques contiguos de funciones vivas sin citar ni explicar**, con quién los llama desde código citado y sus textos: dónde buscar lo que falta portar |
| `archivos.tsv` | ~1.410 | cada archivo de `Client/src`: qué funciones del original cita, de qué ola, sus cortes, qué tests lo cubren y su último commit |
| `ids.tsv` | ~1.120 | cada fila Q-C de 07 y P-C de 08: estado, clase o tipo, ola, si pide VM, las funciones que nombra y qué hace MuRet |
| `metricas.tsv` | ~950 | **los números**, en formato largo: avance total, por ola, por hito de la CL9 y por sub-ola de la CL8, lo que falta por categoría, los huecos, cortes, VM, 08, 07, chat, sustitutos, tamaño de MuRet, la serie desde el 13/11 and what is missing per module |
| `cortes.tsv` | 1.761 | los cortes vivos con su clase (P, O, X, R, S, G, D, I, V, H), qué falta y cómo se llega |
| `vm.tsv` | 234 | los pasos de VM pendientes de todas las olas, y si bloquean código |
| `sustitutos.tsv`, `sin-cablear.tsv` | 143, 185 | los sustitutos propios de MuRet y el código sin llamador |
| `chat.tsv` | 333 | el inventario del chat (funciones y tramos), con su estado |
| `consultar.py` | | las preguntas de siempre en una línea |
| `entradas/` | | lo que la auditoría clasificó a mano, que el generador lee |

## Las preguntas de siempre

Desde esta carpeta:

```
python3 -I consultar.py 0x5252e6            # una VA: su función, dónde está en MuRet, tests, cortes, 07/08, llamadores
python3 -I consultar.py Scenes/GameUi.cpp   # un archivo: qué funciones del original cita y en qué estado están
python3 -I consultar.py P-C232              # una fila de 07 u 08 y el estado de sus funciones
python3 -I consultar.py faltan CL9          # lo vivo sin citar de una ola, de mayor a menor
python3 -I consultar.py huecos CL5          # los bloques sin citar ni explicar (de una ola), con quién los llama
python3 -I consultar.py cierran             # los cortes que cierran el cliente jugando
python3 -I consultar.py buscar StepDebris   # un símbolo o un texto de MuRet: qué VA cita
python3 -I consultar.py module game/Guild   # a module: its metrics, live cuts, gaps and callers, VM steps, open 08
```

Con `grep` y `awk` (`trazas.tsv` empieza por `va`, así que `grep -P '^0x5252e6\t' trazas.tsv` da las citas exactas de
una dirección; las columnas de `funciones.tsv` van en el orden de la tabla de abajo):

```
awk -F'\t' '$5=="sí" && $6=="CL9" && ($11=="unknown"||$11=="not-started")' funciones.tsv   # lo de la CL9 sin citar
awk -F'\t' '$27=="sí"' funciones.tsv                  # citadas que en realidad faltan (falsos positivos)
awk -F'\t' '$31!=""' funciones.tsv                    # las funciones con un corte que cierra el cliente
grep -P '\tavance-ola\t' metricas.tsv | grep cota_baja   # la cota baja de cada ola
```

Los TSV se abren en una planilla: llevan encabezado, van separados por tabulador y en UTF-8.

## Cómo buscar lo que falta: los huecos

La auditoría dejó 470 funciones chicas (57 KB) como "sin clasificar" y miró una muestra. Ahí estaban los globos de la
selección de personaje (los nameplates, `0x63c940..0x63cfb0`): 13 funciones contiguas, CL5 `unknown`, sin ninguna
cita, llamadas desde `0x65f4b0`, `0x4c2290`, `0x6431b0` y `0x65fe00`, que están portadas. La P-C27 (b) los había
anotado.

`huecos.tsv` busca casos así sin mirar a mano: junta las funciones vivas sin citar que la auditoría no explica (no son
clones, plataforma, herramientas, STL ni código sin uso) en bloques contiguos por dirección, que en un binario de MSVC
suelen ser una misma clase o un mismo `.cpp`, y les pone:

- `senal`: `la llama código citado` (lo más sospechoso: MuRet porta al que llama y no a ésta), `la llama sólo código sin
  citar` o `sin llamador directo` (vtables, punteros);
- `entradas`: las funciones citadas que lo llaman, con su archivo de MuRet;
- `textos`: las cadenas de su pseudocódigo, para saber de qué se trata sin abrirlo.

En `f344773b` los globos eran el hueco `h006` (16 funciones, 1.877 B, 8 entradas citadas) y sus 13 funciones
figuraban como `falta`. Con #383 (`UI/CharacterBalloons`) ocho quedaron citadas, y los cinco constructores y
destructores de C++ que no tienen cita (`0x63c990`, `0x63ce00`, `0x63ce10`, `0x63ce40` y `0x63cf20`) pasan a `portada
sin cita`, porque los cubren la clase con RAII y `Release` (`0x63ce50`). Las tres de después (`0x63d010..0x63d0f0`,
hueco `h085`) son de otra clase y siguen sin clasificar.

En `dce38707` se cerró `h042`: `0x4d3200` sola (753 B, `la llama código citado`, desde el dispatcher `0x4e2b10`), la
respuesta del `F3 06`. Sin ella, sumar un punto en la ventana del personaje no hacía nada y la sesión avisaba que el
`F3 06` no tenía handler; ahora es `Scenes/LevelUpPointReply`.

Un hueco no es una prueba: puede estar portado sin citar, o ser un destructor que RAII hace innecesario. Hay que leer
el ASM.

## What is missing, per module

Each row of `funciones.tsv`, `cortes.tsv`, `huecos.tsv`, `archivos.tsv`, `ids.tsv` and `vm.tsv` carries the client
module it belongs to (`module`, the last column). It comes from `Client/modules.tsv` and `coverage/module-map.tsv` in
the commit measured, through the lookup of `Client/tools/architecture/check_layers.py`
(`scripts/tools/reference_modules.py`):

- a file: its row in `module-map.tsv`, or else the module whose `folders_today` holds it;
- a function: the module of the `Client/src` lines that cite it, one vote per line (its cut lines if it has no other);
  an uncited one takes that of its cited callers, one vote per caller;
- a cut: its file's; a gap: that of its cited callers; a VM step or an 07/08 row: that of the functions it names.

A tie goes to the first module in alphabetical order. `metricas.tsv` has a `module` scope with `lines`,
`cited_functions` and `cited_kb` (live functions `implemented` or `partial`), `live_cuts`, `closing_cuts`, `gaps`,
`gap_kb`, `vm_steps` and `open_08_ids`. What no module claims (a gap or a function called only from uncited code, a VM
step or an 08 row whose functions have no module) adds up under `unassigned`, so each metric summed over the modules
equals its total elsewhere in `metricas.tsv` (the KB within rounding). A commit without both tables has an empty
`module` column and no `module` scope.

## Las columnas

### `funciones.tsv`

| # | columna | qué es |
| --: | --- | --- |
| 1 | `va` | inicio de la función en el `main.exe` |
| 2 | `fin` | `va` + `bytes` (ojo: algunas gigantes siguen después, por ejemplo `0x5208a0`; `trazas.tsv` marca esas citas como `cola`) |
| 3 | `bytes` | tamaño según el inventario de Ghidra |
| 4 | `clase` | `game`, `library` (CRT, libjpeg, STL), `excluded` (GameGuard, ResourceGuard, launcher, hash, detector) o `thunk` |
| 5 | `viva` | sólo para `game`: `sí` si alguien la alcanza, `no` si es código muerto |
| 6 | `ola` | la ola de la matriz CL.M congelada (`mainexe-roadmap.tsv`) |
| 7 | `sub_ola` | la sub-ola del plan de la CL9 o, para la CL8, las que nombra el plan de la CL8 |
| 8 | `hito_cl9` | el hito del plan de la CL9 |
| 9 | `ola_blame` | la ola del commit que la citó (`git blame`), como la pone la matriz regenerada |
| 10 | `olas_consumidoras` | otras olas que la usan |
| 11 | `estado` | `implemented` (citada sin cortes), `partial` (con cortes, o gigante), `unknown` / `not-started` (sin citar), `not-applicable` |
| 12 | `hecho` | la fracción hecha: 1 − cortes / citas para las `partial` |
| 13 | `bytes_hechos` | `bytes` × `hecho` |
| 14 | `olas_de_sus_cortes` | a qué olas mandan sus cortes |
| 15–19 | `citas_impl`, `citas_corte`, `citas_pendiente`, `citas_test`, `citas_docs` | cuántas líneas la citan. `citas_pendiente`: citas que no son corte pero dicen que no está portada |
| 20 | `archivos_src` | los tres archivos de `Client/src` que más la citan |
| 21 | `primera_cita` | archivo:línea de la primera cita en el archivo que más la cita |
| 22 | `simbolos_muret` | las tres funciones o tablas de MuRet que más la citan (aproximado, ver [Límites](#límites)) |
| 23 | `tests` | los dos archivos de test que más la citan |
| 24 | `casos_de_test` | los tres casos (`Suite.Caso`) que más la citan |
| 25 | `doc_research` | la primera línea de `Client/docs/research` que la nombra |
| 26 | `categoria_faltante` | para las sin citar: clon, portada sin `0x`, plataforma, herramienta, STL, sin uso, pendiente CL9, falta, sin clasificar |
| 27 | `falso_positivo` | `sí` si la matriz la cuenta como citada pero falta (`0x59eb90`) |
| 28 | `clon_de` | la función citada de la que es un clon byte a byte |
| 29 | `chat` | el bloque del chat (A núcleo, B amigo y cartas, C amigos) o sus tramos |
| 30 | `cortes_vivos` | los cortes dentro de ella, por clase (`G3 R1`) |
| 31 | `cierra_el_cliente` | los cortes suyos que cierran el cliente jugando, archivo:línea |
| 32–34 | `ids_07`, `ids_08_abiertas`, `ids_08_cerradas` | las filas de 07 y 08 que la nombran (hasta 12) |
| 35 | `pasos_vm` | los pasos de VM que la tocan, por los IDs que citan (y si bloquean) |
| 36–38 | `llamadores_n`, `llamadores`, `llamados_n` | del grafo de llamadas del research: cuántos la llaman, los primeros cinco con su ola, a cuántas llama |
| 39–41 | `llamadores_citados`, `senal`, `hueco` | para las vivas sin citar ni explicar: cuántos que la llaman están citados, la señal y el bloque de `huecos.tsv` |
| 42 | `plan_cl9` | cómo la asignó el plan de la CL9 (el opcode que atiende, los paquetes que manda) |
| 43 | `textos` | hasta cuatro cadenas de su pseudocódigo |
| 44 | `proposito` | lo que dice la matriz (a veces es un trozo de un documento) |
| 45 | `nota_auditoria` | lo que dijo la auditoría (qué hace, para el chat y las sin citar) |
| 46 | `pseudocodigo` | el nombre base del `.c` y el `.asm` en `decompiled/functions/` del research |
| 47 | `module` | the client module (see [What is missing, per module](#what-is-missing-per-module)) |

### `trazas.tsv` y `trazas-docs.tsv`

`va` (la dirección citada), `funcion` (la que la contiene), `dentro` (`sí`; `cola` si cae después del tamaño de la
función y antes de la siguiente; vacío si es de datos), `seccion` (`código`, `datos` desde `0x6b8000`, `cabecera`),
`area` (`src`, `tests`, `docs-research`, `docs`), `archivo`, `linea`, `simbolo` (sólo C++), `tipo` (`impl`, `corte`,
`test`, `test-corte`, `doc`), `pendiente`, `en` (si la VA está en un `comentario`, una `cadena` o en el `código`) y
`texto` (la línea, recortada a 240).

Un corte es una línea que cumple la misma regex que usa `build_roadmap_matrix.py` (`Unexpected(`, `cut(`, "es de la
CL", "CL9"...). Por eso las cotas y estas tablas cuentan igual.

### `globals.tsv`

One row per address of the `.data` (`0x6c6000` up to `0x7d5eb64`, its virtual size) that a comment or a string of
`Client/src` cites (`scripts/tools/reference_globals.py`). A number in the code is a value (a flag, a text line's key),
and so is a power of two (a flag's bit), so neither gives a row.

`va`, `home` (`Struct::member` of each data member of a struct or class in a header that cites the address in its own
comment or in the comment block right above it; a reference member only views the address, and a function, a
constant or an enum's entry is not a home), `file` and `line` (the first home), `module` (the map's `target` of that
file), `users` (the modules of the other files that cite it, sorted, without the home's) and `flag` (`no-home`,
`two-homes` or empty). AR3 gives each global a home in the state of its module: a `no-home` row has no member citing it
yet, and a `two-homes` row has more than one (a copy, a pointer or a view field beside the global's owner).

### `huecos.tsv`

`hueco` (`h001`, de mayor a menor), `desde`, `hasta`, `funciones`, `bytes`, `ola`, `sub_ola`, `categorias`, `senal`,
`entradas_citadas`, `entradas`, `ids_08`, `textos`, `vas` (las primeras 40) and `module`.

### `archivos.tsv`

`archivo`, `lineas`, `citas` (sin cortes), `citas_corte`, `funciones` (distintas que cita sin corte), `kb_funciones`
(la suma de sus tamaños: en un `.h` de estado puede ser grande porque nombra funciones enteras), `ola_principal` y
`olas` (por KB), `cortes_vivos` (por clase), `cierran`, `tests` (los archivos de test que citan más funciones en común)
y `ultimo_cambio` / `ultimo_commit`, and `module`.

### `metricas.tsv`

`ref`, `ambito`, `clave`, `metrica`, `valor`, `unidad`, `fuente`, `nota`. Los ámbitos son `inventario`, `avance`,
`avance-ola`, `avance-cl9-hito`, `avance-cl8-subola`, `faltante`, `huecos`, `cortes`, `cortes-ola`, `vm`, `08`,
`08-abiertas`, `07`, `chat`, `sustitutos`, `sin-cablear`, `muret`, `citas`, `serie` and `module`. Al regenerar en otro
commit se pueden pegar las filas nuevas debajo: la columna `ref` las separa.

`serie` junta los puntos guardados en `entradas/serie/` (ordenados por la fecha del commit) y el de la corrida, con
`fuente` = `esta corrida`. Las refs con `nm` son con la `MENTION_ONLY` corregida; las otras se comparan entre sí.

## Cómo regenerarla

Con el clon del research de [scripts/README.md](../../scripts/README.md#el-entorno) (`$RESEARCH`, que tenga
`client-research/mainexe-vmxp-c32bbd5f/` y `client-research/analysis/mainexe/evidence/coverage/`) y MuRet con la
historia completa (el `git blame` de la matriz la necesita), desde la raíz del repo:

```bash
T=Client/docs/research/scripts/tools
python3 -I $T/build_roadmap_matrix.py --research "$RESEARCH" --repo . --ref origin/main \
    --out /tmp/matriz.tsv --summary /tmp/resumen.json --rows /tmp/filas.tsv
python3 -I $T/build_client_reference.py --research "$RESEARCH" --ref origin/main \
    --matriz /tmp/matriz.tsv --filas /tmp/filas.tsv --guardar-serie
```

`--guardar-serie` deja el avance total del commit en `entradas/serie/frozen-<ref>nm.json`, para que la serie lo
conserve en las corridas siguientes. Conviene usarlo sólo con commits de `main`.

La matriz tarda unos minutos y la referencia unos 15 segundos; las dos usan sólo la biblioteca estándar. La matriz va a
`/tmp` porque `mainexe-roadmap.tsv` es la asignación congelada de CL.M: la referencia toma de ahí las olas y de la
matriz regenerada los estados.

Las entradas a mano (`entradas/`) son de la auditoría del 2026-11-20. En un commit nuevo siguen valiendo para lo que no
cambió; lo que se porte después pasa solo a `implemented`, y lo que cambie de clase hay que volver a clasificarlo. Los
falsos positivos (columna 27) llevan las citas de implementación que tenían en la auditoría: si aparecen más, la marca
se cae sola y hay que mirar si de verdad se portaron.

### How a hand row finds its cut

Each row of `entradas/cortes-vivos.tsv` keeps the file and line where it was classified, and an `anchor`: the cut's
addresses, its name (`Unexpected`, `Cut`…), the MuRet symbol around it and a hash of its statement without comments
or extra spaces (`scripts/tools/reference_anchors.py`). The generator finds every cut call in the commit it measures
and gives each row the one with its anchor, so a move or an edit elsewhere in the file does not lose it. `cortes.tsv`
says where each one is now and how it was found (`resolved_by`):

- `anchor`: the same cut, maybe in another file or line.
- `similar`: the text changed; it is the cut with the same addresses, name and symbol and the closest text. Worth a
  look.
- `unresolved`: no cut matches any more (ported or rewritten). The row keeps its old place and counts in
  `metricas.tsv` (`entradas`, `cortes-vivos`, `unresolved-entries`) until someone reviews it.

A row added by hand gets its anchor on the commit where its file and line are right:

```bash
python3 -I Client/docs/research/scripts/tools/reference_anchors.py --anchor-entries --ref <commit>
```

### When a refactor moves code

The matrix gives each cited line the wave of the commit that last wrote it (`git blame -M -C`). A refactor commit
(`type(ARn.m): …`) has no wave of its own: it is `AR`, and the next commit without a wave inherits the last port wave,
not `AR`. So that a move, a comments pass or a design PR does not take a citation away from its wave:

1. After the squash, add its full hash to [`.git-blame-ignore-revs`](../../../../../.git-blame-ignore-revs), with its
   subject on a comment line above, and each moved file to `entradas/moves.tsv` (`commit`, `from`, `to`).
   `Client/tools/architecture/move.py --record <hash>` does both (skill `muret-move`).
2. `git blame` then skips that commit, and a moved or reworded line keeps the commit that wrote it.
3. A cited line still blamed on a refactor commit that is not listed counts in the last port wave before it, and the
   matrix warns `citation-attributed-to-AR` (on stderr, and under `warnings` in `--summary`): its squash was not
   recorded. A cited line that a listed commit wrote in a new file cannot go further back; it counts the same way,
   without a warning.

## La action

Para no regenerar a mano: en GitHub, **Actions → Referencia del cliente → Run workflow**, con la rama o el commit (vacío,
la rama elegida arriba). Tarda unos 5 minutos y no toca el repo. En la página de la corrida queda el resumen
(`scripts/tools/reference_summary.py`), y las tablas regeneradas, para bajar como artifact durante 30 días:

1. **Cuánto falta para portar:** las cotas alta, media, baja y baja con test, y los KB fuera de la cota baja, en total
   y por ola.
2. **Cuánto falta para cerrarlo contra el ASM:** las funciones vivas cerradas, las que están portadas (`implemented`)
   sin falso positivo, sin cortes vivos ni citas pendientes, con al menos un test, sin pasos de VM y sin filas de 08
   abiertas. Las explicadas (clones, portadas sin cita, plataforma, herramientas, STL y sin uso) van aparte y no
   entran en la base. Al lado, lo que frena a las demás: sin citar, con cortes (y las que cierran el cliente), sin
   test, con VM y con 08. Mide eso, no la fidelidad byte a byte (ver Límites).
3. **What is missing, per module:** the `module` scope of `metricas.tsv`, one row per module and its total.
4. **El estado de las trazas:** los cortes por clase, los que cierran el cliente jugando, los huecos más grandes y la
   diferencia con la `metricas.tsv` commiteada en la rama: si difiere, hay que regenerarla y commitearla.

Lee el repo de research con una deploy key de solo lectura de ese repo; la privada es el secreto `RESEARCH_DEPLOY_KEY`
de MuRet. El botón aparece cuando el workflow está en `main`; antes, corre en los PRs que lo cambian.

## Los números principales

| | |
| --- | --- |
| código de juego vivo | 3.278 funciones, 2.197 KB |
| cota alta / media / baja | **91,5 / 89,5 / 87,5 %** |
| cota baja con test | 77,9 % |
| explicado (baja + clones, plataforma, herramientas, STL, sin uso) | 92,1 % |
| cortes vivos | 1.761 sitios; 5 cierran el cliente jugando (los 5 V, de la VM) |
| pasos de VM | 234 (cota superior); 11 bloquean código |
| 08 | 305 filas: 246 abiertas (98 de código, cota superior) |
| chat (A+B, vivo) | 137.715 B (5,9 %): 99,2 % citado, 79,4 % con test |

El resto, por ola, por hito y la serie: `grep -P '\tavance-ola\t|\tavance-cl9-hito\t|\tserie\t' metricas.tsv`.

## Límites

- **Citar no es portar fiel.** `estado` y las cotas cuentan citas, no comparan con el ASM. Hay al menos tres falsos
  positivos (columna 27); `citas_pendiente` ayuda a encontrar más.
- `hecho` de una `partial` es cortes sobre citas, no bytes reales.
- `simbolo` sale de contar llaves, no de un parser de C++: se equivoca con macros raras y, en un comentario fuera de
  toda función, toma la siguiente definición si está pegada (sin una línea en blanco en el medio).
- Los llamadores son las llamadas directas del grafo de Ghidra; las vtables y los punteros no están.
- Las clases de los cortes G, S, R y D se revisaron por grupos, no una por una. Los pasos de VM y las filas de código
  de 08 son cotas superiores.
- Un G ("no salta en una partida normal") vale sólo si cada lugar que llega a esa función le presta el servicio: cada
  `GamePanels` se arma donde se usa y presta sus cables. El de `UI/PanelRegistry.cpp:789` (0x657ab0, "cerrar la
  ventana del NPC sin el mundo") saltó jugando hasta #385, al caminar con la tienda, el almacén o la caja del caos
  abiertos; la columna `nota` de `cortes.tsv` lo dice. Para encontrar otros, buscar los `GamePanels` que cierran o
  abren paneles sin prestar lo que esos paneles piden.
