# 05. Formatos de datos

Fuentes: `mu-client/docs/component-bmd-tables.md`, `component-terrain.md`,
`component-textures.md`, `component-pipeline.md`, `tier1-*.md`, `coverage-main.md` §0.1 y §5.1,
y `analysis/mainexe/evidence/s31-assets/` (incluido `byte-a-byte.md` y
`divergencias-cliente-servidor.md`). Del repo `mu-online-reverse-lab`:
`analysis/decompilacion-maxima/INDEX.md`, `assets/assets_manifest.csv`, `assets/assets_summary.json`,
`scripts/asset_manifest.py` y `abi/`.

## Qué hay en `Data\`

El inventario completo del cliente (`assets_manifest.csv`, 5.774 archivos; 5.701 identificados, 29
parciales, 44 binarios de soporte, 0 errores de decodificación):

| Formato | Archivos | Transformación |
| --- | ---: | --- |
| OZJ (JPEG) | 2.387 | prefijo de 24 bytes; 2.380 son JPEG y **7, TGA con extensión OZJ** (Q-C14) |
| OZT (TGA) | 664 | prefijo de 4 bytes |
| BMD de modelo | 1.962 | v0C cifrado con K16, y en claro cualquier otra versión (1.142 v0A y 4 con `0x00`); todos recorridos sin bytes sobrantes |
| BMD de tabla | 7 + 26 estructuras | XOR3 `FC CF AB` |
| terreno | 23 `EncTerrain` `.map`/`.obj`, 29 `.att`; además 18 `.map`, 20 `.obj` y 24 `.att` que el manifiesto clasifica como `Terrain/*` y no `EncTerrain/*`. El cliente carga solo los `EncTerrain` de `World<N>`, con el N de la carpeta o, en dos `.att`, otro número (confirmado en la CL1.3, ver abajo) | K16 (+ XOR3 en `.att`) |
| OZB | 19 | BMP con prefijo de 4 bytes |
| WAV | 484 | efectos de sonido |
| MP3 | 23 | música por mapa (`Music\Pub.mp3`, `Devias.mp3`, `MuTheme.mp3`…) |
| Ogg/Vorbis | 3 | por `wzAudio` → `vorbisfile` → `ogg` |
| `message.wtf` | 1 | XOR `0xCA`; es del launcher: `main.exe` no lo lee (ver el pipeline de las DLLs) |
| claves C3/C4 | 2 | `Enc1.dat`, `Dec2.dat` |

Es la otra parte bien validada: los cargadores de tablas, terreno y texturas reproducen **byte a
byte** lo que tiene en memoria el cliente original vivo (`mu-golden/memoria-cliente/`, Lorencia y
Devias). Todos trabajan sobre buffers: leer el archivo es tarea del que llama.

## Tablas `.bmd`

| Tabla | Original (carga / escritura) | Formato | En memoria |
| --- | --- | --- | --- |
| XOR3 | `0x5496c0` | XOR con `FC CF AB` (`0x6c8c74`) | — |
| checksum | dentro de los cargadores | semilla `key << 9`, ADD o XOR según la paridad, mezcla cada 4 dwords | — |
| `Gate.bmd` | `0x54a360` / `0x54a2e0` | 256 × 9 bytes ([los campos](#gatebmd)) | `*[0x07688AB8]`, 0x900 |
| `Item.bmd` | `0x54b9e0` / `0x54b8f0` | 8192 × 0x54 bytes + checksum `0xE2F1` | `*[0x07758550]`, 0xA8000 |
| `Skill.bmd` | `0x54acf0` / `0x54aa20` | 256 × 0x3C bytes + checksum `0x5A18`; luego un contenedor "guardia" con `rand()` | `*[0x076BD1D8]`, 0x3C00 |
| `Text.bmd` | `0x5495e0` / `0x549700` | 2048 × 300 bytes | `g_GlobalText` `0x076BD1DC` |
| `FilterName.bmd` | `0x549fa0` / `0x549ec0` | 1000 × 20 bytes, el XOR3 por registro, hasta el primero vacío, + checksum `0x2BC1` (la paridad de `Item.bmd`) | `0x076BAAC8`, la cuenta en `0x0775855C` |
| `Filter.bmd` | `0x549c60` / `0x549b80` | igual, con checksum `0x3E7D` | `0x077531DC`, la cuenta en `0x07758558` |
| `ItemSetType.bmd` | `0x40c660`, desde `0x40c600` / sin buscar | 8192 × 4 bytes, el XOR3 por registro, + checksum `0xE5F1` (la paridad de `Item.bmd`; [los campos](#las-tablas-de-los-conjuntos)) | `[0x6eed3c] + 4`, 0x8000 |
| `ItemSetOption.bmd` | `0x40c850`, desde `0x40c600` / sin buscar | 64 × 0x4c bytes, igual, + checksum `0xA2F1` | `[0x6eed3c] + 0x8004`, 0x1300 |
| `Slide.bmd` | `0x46b3b0`, desde `0x61ccb0` (`0x61ed18`) / sin buscar | 0xa030 bytes con el XOR3 entero (`0x4c1690`, la clave de `0x6c82e4`): el período, la velocidad y 5 tramos de 0x2008 bytes ([los campos](#slidebmd)) | `[0x5763528] + 0x11ac..+0x1218` |

Si falta un archivo, el original muestra un `MessageBox`, manda `WM_DESTROY` y **sigue cargando**
las demás tablas.

Los dos filtros (CL6.5, **verificado**: las sumas del laboratorio dan) los lee `0x61ccb0` con los
datos del juego. Como 20 no es múltiplo de 3, el XOR3 empieza en `FC` en cada registro, también en
los vacíos: un registro vacío está en el archivo como `FC CF AB …`. `FilterName.bmd` trae seis
palabras (`ADMIN`, `admin`, `GM`, `gm`, `Gm` y `gM`) y `Filter.bmd` ninguna. Los usa el nombre de
un personaje nuevo (06 § La ventana de crear).

### `Gate.bmd`

Leído en el barrido de Lorencia (2026-11-05). `0x54a360` (lo llama `0x61ccb0` con `Data\Gate.bmd`,
`0x6d2aec`) lee 256 registros de 9 bytes con XOR3 a la tabla de `*[0x7688ab8]` (`new(0x900)` en
`0x4bae26`, a cero en `0x4baf80` y liberada en `0x4aefbc`); `0x54a180` (lee `Data2\Gate.txt`) y
`0x54a2e0` (escribe el `.bmd`) son del modo de desarrollo, con `0x6c9e0c` en 0. La tabla en memoria
es el archivo (*verificado*, `GoldenGateTable` contra `s31_gate.bin`). El índice del registro es el
número del gate, y los nueve campos van en el orden del `gate.txt` del servidor ("Flag, Map number,
X1, Y1, X2, Y2, Target Gate, Dir, Level"):

| Byte | Campo | Quién lo lee |
| --- | --- | --- |
| 0 | el flag: 1 entrada, 2 salida, 0 destino de un `/move` | `0x599f27` (solo el 1) |
| 1 | el mapa | `0x599f36` (el de la entrada) y `0x59ad30` (el del destino) |
| 2 a 5 | x1, y1, x2, y2: el rectángulo, inclusivo y sin margen | `0x599f66..0x599ff8` |
| 6 | el gate destino | `0x59a00b` |
| 7 | la dirección al llegar | nadie en el cliente: la usa el servidor |
| 8 | el nivel | `0x59a06c` (el de la entrada; en las clases 3 y 4, `nivel · 2 / 3`) |

El uso es *estático*; los nombres de los campos, *verificado* contra la cabecera del `gate.txt` y el
servidor reconstruido (`gate.cpp`, `CGateC::GetGate` e `IsInGate`). **El único lector de los campos
es `0x599ef0`**, la detección de [06](06-render-escenas-y-ui.md#los-cambios-de-mapa): los otros
`cmp` con `0x7688ab8` (`0x514133`, `0x54ebcb`, `0x5b41f5`, `0x5b8930`, `0x5bb308`) son el final de
otra tabla (`0x7681eb8`, de paso `0x36`).

El `Gate.bmd` del laboratorio trae 125 gates y el `gate.txt` del servidor en uso 122, y **61
difieren** (*verificado*, la comparación; Q-C182): los niveles (el 3, de Dungeon a Lorencia, tiene 0
en el cliente y 20 en el servidor; los de Lost Tower, 80 contra 90 a 120) y los del 109 al 129, que
en el cliente son de otra versión (el 115 es una entrada del Stadium a Crywolf en el cliente y un
destino del 32 en el servidor). Los cuatro de Lorencia coinciden:

| Gate | Celdas | Destino | Nivel |
| --- | --- | --- | --- |
| 1 | 121..123 × 232..233 | el 2: Dungeon (1), 107..110 × 247 | 20 |
| 18 | 5..6 × 38..41 | el 19: Devias (2), 242..243 × 34..37 | 15 |
| 23 | 213..217 × 246..247 | el 24: Noria (3), 148..155 × 5..6 | 10 |
| 102 | 239..240 × 14..15 | el 103: el mapa 30, 29..30 × 42..37 (en el archivo, la y1 es mayor que la y2) | 10 |

### Las tablas de los conjuntos

Leído en la CL7 hito 1, T10b (*estático*, cotejado con el PE `c32bbd5f`). `0x40c600(arg)`, un
`thiscall` sobre `[0x6eed3c]`, lo llama solo `0x61ccb0`, en `0x61ecd5`, con `push 0` y sin mirar lo
que devuelve, después de `Item.bmd` (`0x61eb37`), `Skill.bmd`, `Gate.bmd` y los dos filtros. Con el
argumento en 0 lee `Data\Local\ItemSetType.bmd` (`0x6c63e8`) con `0x40c660` y después
`Data\Local\ItemSetOption.bmd` (`0x6c63c8`) con `0x40c850`; con otro valor, `ItemSetTypeTest.bmd`
(`0x6c6428`) e `ItemSetOptionTest.bmd` (`0x6c6404`), que el original no usa. Los dos cargadores
devuelven 1 siempre, así que el segundo corre aunque el primero falle.

Cada uno abre con `"rb"` y hace dos `fread` que no mira: los registros (`0x8000` o `0x1300` bytes, a
un `new`) y los 4 bytes del checksum, el de [arriba](#tablas-bmd) con la paridad de `Item.bmd` (XOR
en los dwords impares) y la clave `0xE5F1` (semilla `0x1cbe200`, `0x40c6bb..0x40c6fe`) o `0xA2F1`
(`0x145e200`, `0x40c8ae..0x40c8f1`). Si no coincide, escribe `"%s - File corrupted."` (`0x6c6460`) en
el log y en un `MessageBox`, manda `WM_DESTROY` y deja la tabla como estaba; sin el archivo, igual
con `"%s - File not exist."` (`0x6c6448`). Si coincide, descifra registro por registro con
`0x40c810` (el XOR3 con `FC CF AB` de `0x6c63c4`, que vuelve a empezar en cada registro) y los copia
al objeto. Con un archivo corto, lo que falta es heap sin inicializar (Q-C24).

El objeto es el estático de `0x6e5930` (`0x940c` bytes; `[0x6eed3c]` apunta a él). Su constructor,
`0x40f2a0` desde `0x40c5d0`, pone la vtable en `+0` y en 0 de `+0x9304` a `+0x9347`. Los tipos van
desde `+4`, los conjuntos desde `+0x8004`, y de `+0x9307` a `+0x940b` está lo que escriben `0x40e1b0`,
`0x40d010` y `0x40da20` ([06](06-render-escenas-y-ui.md#los-stats-del-registro-propio)).

`ItemSetType.bmd` trae un registro de 4 bytes por tipo de ítem, con el índice de `Item.bmd` (grupo ·
512 + número). Los bytes 0, 1 y 2 son el conjunto del ítem con su byte `+0x1c & 3` en 1, 2 o 3
(`0x40cfa0`); 0 o 0xff es ninguno, y el byte 3 no lo lee nadie. `ItemSetOption.bmd` trae 64 conjuntos
de `0x4c` bytes, que lee `0x40d010` con n = las piezas puestas − 1:

| Bytes | Campo | Dónde |
| --- | --- | --- |
| `+0..+0x1f` | el nombre, con su 0 (`strcpy` y `strcmp`) | `0x40d16d..0x40d233` |
| `+0x20..+0x2b` | seis pares de opciones: el par i entra con i < min(n, 6) | `0x40d25d` |
| `+0x2c..+0x37` | sus valores | `0x40d262` |
| `+0x38 + i`, `+0x3a + i` | una opción y su valor más, con 6 ≤ i < n − 6: con las 10 piezas que caben como mucho, ninguna | `0x40d28a`, `0x40d291` |
| `+0x3c` | las piezas del conjunto completo más 1: completo con n ≥ `+0x3c` − 2 (con signo) | `0x40d363` |
| `+0x3d..+0x41` | las cinco opciones del completo | `0x40d381` |
| `+0x42..+0x46` | sus valores | `0x40d376` |
| `+0x47..+0x4b` | una marca por clase (`+0x1d4 & 7` del héroe publicado, de 0 a 4); las clases 5 a 7 leen los bytes 0 a 2 del conjunto siguiente | `0x40d0d0..0x40d14d`, `0x40d2a7` |

Una opción 0xff no cuenta, y una de 0x18 en adelante pide la marca de la clase distinta de 0 y la
evolución (`+0x1d4 >> 3`) mayor o igual que la marca − 1.

Los archivos del repo (`Client/assets/Data/Local`; *verificado*, `GoldenItemSetTables`) son
`itemsettype.bmd`, de 32.772 bytes, e `itemsetoption.bmd`, de 4.868, los dos con su checksum
(`0x7a5a05a5` y `0xb35d7fa2`). De los 8.192 tipos, 78 tienen conjunto, en el byte 0 (un impar del 1
al 31) y con los otros tres en 0, y los 8.114 restantes están en 0xff: ningún ítem tiene conjunto
con `& 3` en 2 o 3. Hay 16 conjuntos con nombre, los impares del 1 al 31, y los otros 48 registros
están en 0xff. Ninguno tiene una opción de 0x18 en adelante, todas las marcas de clase están en 0, y
ninguno junta más de 16 opciones. Por ejemplo, `Hyperion` (el 3) es de Bronze Armor, Bronze Pants y
Bronze Boots (8 0, 9 0 y 11 0), con los pares (2, 15) y (1, 15), `+0x3c` en 4 y el completo con
(0x13, 20) y (0x0c, 30); `Rapier` (0 2) es del 21 y `Lightning Sword` (0 14) del 11.
`itemsettypetest.bmd` es igual a `itemsettype.bmd`; `itemsetoptiontest.bmd`, no.

MuRet: `Data::Tables::LoadItemSetTypeTable` y `LoadItemSetOptionTable` dejan la tabla en claro; con
otro checksum, `DataError::ChecksumMismatch`, y sin todos los registros y el checksum,
`DataError::FileTooShort`, sin tabla (Q-C24); lo que sobra se ignora. `Scenes::LoadGameData` las
carga después de `Item.bmd`, y sin ellas, o con un checksum que no da, es un error: el original se
cierra.

### `Slide.bmd`

Leído en la CL8 (hito 3, 2026-11-12; *estático*, y la forma *verificada* con el archivo real). `0x46b3b0` (desde
`0x61ccb0`, `0x61ed18`, con `Data\Local\Slide.bmd`) abre el archivo con `"rb"`; si no está, arma `%s - File not
exist.`, lo muestra y manda `WM_DESTROY`. Si alguno de los cinco tramos tenía textos, antes los vacía (`0x46b5b0`).
Lee 0xa030 bytes a la pila con `fread`, sin mirar cuántos leyó (Q-C516), y les pasa el XOR3 de una vez.

| Desplazamiento | Qué | En `[0x5763528]` |
| --- | --- | --- |
| `0x0` | el período del timer `0x3eb`, en segundos | `+0x11ac` |
| `0x4` | la velocidad de los carteles del timer (float) | `+0x1218` |
| `0x8 + i · 0x2008` | el tramo i (0 a 4): el nivel tope | `+0x11b0 + 4i` |
| `0xc + i · 0x2008` | la cuenta de textos | `+0x11c4 + 4i` |
| `0x10 + i · 0x2008 + j · 0x100` | el texto j, hasta su `\0`, si j es menor que la cuenta | la lista de `+0x11d8 + 0xc · i` |

Con una cuenta mayor que 32, los textos siguen de a 0x100 en el tramo de al lado, sobre su tope y su cuenta
(`0x46b4d7..0x46b56b`); con una negativa no lee ninguno, pero la cuenta queda para el `rand()` de `0x46b680` (06 § El
cartel de `Slide.bmd`). El real trae el período en 60 segundos, la velocidad en 2,5 y tres tramos: hasta el nivel 15
con 28 textos, hasta el 30 con 20 y hasta el 50 con 19; los otros dos tienen el tope en 0 y la cuenta en −1.

MuRet: `Data::Text::LoadSlideTable` lee los 0xa030 bytes (con menos, `DataError::FileTooShort`, Q-C516; lo que sobra
se ignora), y un texto sin `\0` antes del final es `DataError::InvalidData`, porque en el original seguiría en la
pila. `Scenes::LoadGameData` lo carga al final; uno que no está deja el cartel con lo de su constructor (Q-C517).

## Terreno de un mundo (`Data\World<N>\`)

Grilla de 256 × 256 celdas, `idx = y * 256 + x`.

| Archivo | Original | Formato |
| --- | --- | --- |
| capa K16 | inline en los tres cargadores | XOR de 16 bytes con `prev0 = 0x5E` y paso `0x3D`, un solo flujo |
| `EncTerrain<N>.map` | `0x5f87b0` | 2 bytes + capa 1 + capa 2 + alfa = 0x30002 bytes; antes de leerlo, `0x5f8300` limpia las capas y llena el corrimiento del pasto (abajo) |
| `EncTerrain<N>.att` | `0x5f8390` + XOR3 | 4 bytes + 65.536 celdas de 1 o 2 bytes (los atributos, [abajo](#los-bits-del-att)) |
| `EncTerrain<N>.obj` | `0x607370` | 4 bytes + N × 30 bytes (objetos del mapa), agrupados en 256 bloques |
| `TerrainHeight.OZB` | `0x5f9020` | 4 bytes + BMP de 0x10438 bytes; las alturas se leen desde BMP + 1080 |
| `TerrainLight.OZJ` | `0x63b490` | luz base: 256 × 256 × 3 floats |

Verificado contra la memoria del original: `.map`, `.att`, alturas y luz. El `.obj` se cerró en la
tanda 3 contra las 256 listas de objetos de Lorencia (2.870 nodos).

**Qué archivos se cargan** (CL1.3): el cargador de mundo (`0x61a560`) arma
`Data\World<N>\EncTerrain<N>.map`, `.att` y `.obj` (las cadenas de `0x6d12e8`, `0x6c7314` y
`0x6d12b8`) y compara el número de mundo que devuelve cada cargador con N, salvo que devuelva -1.
N sale del mundo en curso (`0x6c9dd0`) en `0x61a5e4`..`0x61a61d`: es el mundo + 1, pero los mundos
11 a 17 usan la carpeta 12, del 18 al 23 la 19, y del 24 al 29 y el 36 la 25. Dos `.att` llevan
otro número en el nombre, según el estado del juego: `World31\EncTerrain312.att` (`0x61a6ea`) y
`World35\EncTerrain351.att` o `352` (`0x61a710`); su cabecera dice 31 o 35, el N de la carpeta.
`0x4294c0` y `0x42fe80` los vuelven a cargar durante el juego, sin mirar lo que devuelve el
cargador. Ningún nombre lleva "test": los `EncTerraintest*` del laboratorio no los pide nadie.
Los `Terrain.map`, `Terrain.obj` y `Terrain.att` sueltos no los carga nadie: sus nombres solo
aparecen en el exportador de `0x61ada0` (una herramienta de desarrollo). Los del laboratorio tampoco
son lo que escribe ese exportador: su `.map` mide `0x30001` bytes, en claro y sin el byte del mundo
(el exportador `0x5f8960` escribe `0x30002` y los cifra con K16), y su `.att` de World31 en
adelante mide `0x20003` (el exportador `0x5f8690` escribe `0x10004`). Son otro formato, en claro,
de origen sin saber. Los `Terrain<N>.att` de 65.539 bytes, con cabecera `00 FF FF`, son el formato
del servidor. En el `Data\` del laboratorio, los 20 mundos tienen un `.map` de `0x30002`
bytes y un `.att` de `0x10004` o `0x20004` con su propio número en la cabecera, y los tres `.att`
con otro nombre son de `0x20004`.

**La carpeta de cada mapa** (CL5.R, con los `cmp` del desensamblado de `0x61a560`; *estático*).
Antes de elegir la carpeta, si el argumento y `DAT_006c9dd0` valen 32, el mapa pasa a 9
(`0x61a56c`..`0x61a57d`): carga `World10` y el juego queda en el mapa 9 (Q-C58). Los cinco que
llaman a `0x61a560` escriben `DAT_006c9dd0` con el mismo número justo antes, así que el 32 da
siempre eso. Las comparaciones son con signo. Los mapas que llegan:

- tres handlers leen un byte del paquete, sin signo (de 0 a 255, `xor eax, eax` y `mov al`), y
  ninguno mira el rango: el `F3 03` (`0x4c41cb`, `pkt[6]`, siempre), el `F3 04` (`0x4c4e94`,
  `pkt[6]`, si cambió) y el `1C` (`0x4c77ff`, `pkt[4]`, si cambió);
- el 54, de la primera entrada a la selección (`0x633f20`), y el 55, del login (`0x6349a0`).

| Mapa | Carpeta | En el `Data\` del laboratorio |
| --- | --- | --- |
| 0 a 10 | `World1` a `World11`, el mapa + 1 | sí; `World6` (el 5) sin alturas, luz ni tiles: sin alturas el original se cierra (Q-C180) |
| 11 a 17 | `World12` | sí |
| 18 a 23 | `World19` | sí |
| 24 a 29 y 36 | `World25` | sí |
| 30 | `World31`, con su `.att` y su luz según el estado (arriba) | sí |
| 31 | `World32` | sí |
| 32 | `World10`, y el mapa pasa a 9 | sí |
| 33 | `World34` | sí |
| 34 | `World35`, con su `.att` y su luz según el estado | sí |
| 35 | `World36` | **no**: sin el `.att`, el original se cierra (Q-C180) |
| 54 | `World55` (la selección) | sí |
| 55 | `World56` (el login) | sí |
| 37 a 53 y 56 a 255 | el mapa + 1 | no: ningún handler los descarta, y el original pide la carpeta igual |

Los 20 mundos del laboratorio son la carpeta de algún mapa (`World10`, la del 9 y la del 32), y
nadie pide `World33`. Antes de la carpeta, `0x616ec0` fuerza el mapa 7 si `DAT_07d366bc` está
prendida (`0x616ee1`), pero esa bandera está en la `.bss` y tiene 9 lecturas y ninguna escritura:
es código muerto, y queda siempre en 0.

En MuRet, `MapAtWorldLoadStart(argumento, observado)` y `WorldFolderAfterModels(observado)`
representan por separado la guarda inicial 32/32→9 y la selección posterior signed, para todo
`int32_t` y con el incremento DWORD modular. El caller debe aportar una observación nueva después
de modelos: un 32 observado entonces da carpeta 33, aunque la guarda inicial haya devuelto 9;
un 9 da 10. `WorldFolderNumber(byte)` conserva su proyección previa de argumento=mapa y mapa
estable. `LoadedWorld::Load(byte)` aplica esa normalización local a terreno (incluida luz), tiles y
vista, y `LoadedMap()` publica el mapa efectivo sólo mientras posee el terreno: vacío tras fallo,
`Release` o `TakeTerrain`. Los alias de carpeta conservan su identidad (11 y 17, por ejemplo).
Esto no implementa el loader nativo completo, sus globals mutables, efectos de modelos, variantes
30/34, errores/continuación o retención nativa; tampoco cambia el mapa solicitado del wire.

**El `.obj`** (CL1.3): K16 de punta a punta, sin XOR3. Cabecera de 4 bytes (uno que no se lee, el
mundo y la cantidad de objetos, un int16 con signo) y un objeto cada 30 bytes: tipo (int16),
posición, ángulo y escala (7 floats). Si la cantidad es cero o negativa no hay objetos, y no se mira
contra el tamaño del archivo. En el laboratorio, cada `EncTerrain<N>.obj` mide justo 4 + 30 ×
cantidad; el de World6 no trae ninguno. Los floats llegan con sus bits aunque no sean un número
(Q-C28). En la memoria del original, cada objeto de Lorencia guarda el tipo en +2, la escala en +12,
la posición en +16 y el ángulo en +28: los 2.870 son los del archivo, con los mismos bits, en la
escena 5 (medido sobre `obj_nodes.bin`; ahí cada nodo lleva antes 8 bytes, la lista y la
dirección, y los campos quedan en +10, +20, +24 y +36). Están en el orden del archivo: cada lista
guarda los suyos en ese orden, y el archivo ya viene ordenado por bloque, así que las listas de la
0 a la 255 dan el orden del archivo. Los offsets salen de los volcados, no del asm de `0x606660`,
que cambia la escala de algunos objetos en las escenas 2 y 4. Lo prueba un test de memoria desde
la CL1.4.

**Las alturas** (CL1.3): `TerrainHeight.OZB` mide 66.620 bytes en los 19 mundos del laboratorio
que lo traen (World6 no tiene). Se lee un byte por celda desde el byte `0x43C` del archivo, por
1,5 (por 3 en el mundo `0x37`, World56), sin dar vuelta las filas y sin mirar la cabecera del BMP
(Q-C02). Contra la memoria de Devias, desde `0x43C` dan las 65.536 alturas de `s31_height.bin`;
desde donde dice `bfOffBits` (el byte `0x43A` del archivo, en los 17 de 8 bits), 52.139 de ellas
salen distintas.

**El corrimiento del pasto** (`0x5f8300`, que llama el cargador del `.map`; *estático*). Antes de
leer el archivo pone las capas en sus valores por defecto (la capa 1 en 0, la 2 en `0xFF` y el alfa
en 0, Q-C27) y llena `0x7b0cca4` con 65.536 `(rand() % 4) · 0,25` (`0x5f831e..0x5f8342`): **un
`rand()` por celda en cada carga de mapa**, también la del login y la de la selección (Q-C146). El
pase del pasto lee solo los 256 primeros, uno por fila
([06](06-render-escenas-y-ui.md#lo-propio-de-lorencia)).

### Los bits del `.att`

El cargador (`0x5f8390`) guarda el byte de abajo de cada celda en un WORD de `0x7d0d348`, el
`TerrainWall`, y rechaza el archivo si una celda trae `0x80` o más (`0x5f85fd`): ese bit solo llega
por el paquete `0x46` ([04](04-protocolo.md#movimiento-y-mundo)). Qué es cada bit (barrido de
Lorencia; *estático*, y los valores, *verificado* en los archivos):

| Bit | Qué es | Quién lo usa |
| --- | --- | --- |
| `0x01` | la zona segura | `0x511290` y `0x505de0` (`+0x3e2` del personaje), el segundo intento del camino, el clic que no ataca y la música |
| `0x02` | hay un personaje: lo saca y lo pone `0x508f00` en cada paso, en tiempo de ejecución | el camino, que lo toma como pared |
| `0x04` | no se camina | el camino |
| `0x08` | no se dibuja, pero el picking la prueba (Q-C163) | `0x5fac90`; el cursor (`DontMove`, con 8 o más sin `0x20` ni `0x40`) |
| `0x20` | una meta permitida aunque bloquee | `FindPath` con `bErrorCheck`; `DontMove` |
| `0x40` | la altura fija de `0x6c9d5c`: 1.200, y 1.201 desde que la cámara pisa una (Q-C150) | `0x5f93c0`, `0x5fac90` y la cámara |
| `0x80` | el zoom de la cámara | `0x635ae0` |

El camino no mira `0x20`, `0x40` ni `0x80` para saber si una celda bloquea (`Map & ~0xe0`,
[06](06-render-escenas-y-ui.md#caminar)). En los 29 `EncTerrain*.att` del `Data\` (*verificado*),
ninguna celda trae el `0x80`, y el `0x40` está solo en los cuatro de `World31`, el mapa 30: 835
celdas, de x 160 a 190 e y 190 a 220, sobre el castillo y el rectángulo del far de la cámara
(Q-C149, Q-C150). El volcado vivo de Lorencia (`lor_att.bin`) tampoco tiene `0x40` ni `0x80`. Las
celdas de Lorencia valen 0, 1, 2, 4 y 5, y la única con el 2 es la (221, 11) (Q-C170).

## Los modelos de cada mundo

`0x616ec0`, desde la carga del mapa (`0x61a560`,
[06](06-render-escenas-y-ui.md#la-entrada-al-mundo)). Leído en el barrido de Lorencia, *estático*
salvo lo que se marca. El primer `switch`, el de los `.smd` de `Data2\`, solo corre con `0x6c9e0c`
en 0, y vale 1: está muerto.

- **En todos los mapas**: `Object8\drop01.jpg` en `0x607` (al empezar), `Object8\wt00..wt31.jpg` en
  `0x41..0x60` (salvo en el 7, que los carga en su caso), `Object8\light01.jpg` en `0x68` (que
  `0x61a560` pisa después con `rain03`, Q-C136) y los 160 del mundo, desde `0x60d530(0x41c)`.
- **Los 160 del mundo**, en todos los mapas menos el 0: el lazo genérico (`0x61a0c4..0x61a124`),
  `Data\Object<N>\Object<k+1>.bmd` con sus texturas de `Object<N>\`
  ([06](06-render-escenas-y-ui.md#el-mundo-del-login)).
- **El mapa 0 no usa ese lazo.** Con el mapa 0 (`0x618e57`) salta a `0x619a10` y carga **112
  modelos con nombre** de `Data\Object1\`, en 39 llamadas a `0x60da30(k, "Data\Object1\", nombre,
  n)`, que pide `nombre` + `%02d`; los otros mapas van al lazo (`0x619edf`). *Verificado* en los
  datos: `Object1` no tiene ningún `ObjectNN.bmd` y los otros `Object<N>` sí, salvo los siete u ocho
  con nombre de cada uno.

La tabla de Lorencia (tipo, archivo):

| Tipos | Archivos | Tipos | Archivos |
| --- | --- | --- | --- |
| 0 a 12 | `Tree01..13` | `0x5a` | `StreetLight01` |
| `0x14`–`0x1b` | `Grass01..08` | `0x5b`–`0x5d` | `Cannon01..03` |
| `0x1e`–`0x22` | `Stone01..05` | `0x5f` | `Curtain01` |
| `0x28`–`0x2a` | `StoneStatue01..03` | `0x60`, `0x61` | `Sign01`, `Sign02` |
| `0x2b` | `SteelStatue01` | `0x62`–`0x65` | `Carriage01..04` |
| `0x2c`–`0x2e` | `Tomb01..03` | `0x66`, `0x67` | `Straw01`, `Straw02` |
| `0x32`, `0x33` | `FireLight01`, `FireLight02` | `0x69` | `Waterspout01` |
| `0x34` | `Bonfire01` | `0x6a`–`0x6d` | `Well01..04` |
| `0x37` | `DoungeonGate01` | `0x6e` | `Hanging01` |
| `0x38`, `0x39` | `MerchantAnimal01`, `MerchantAnimal02` | `0x6f` | `Stair01` |
| `0x3a` | `TreasureDrum01` | `0x73`–`0x77` | `House01..05` |
| `0x3b` | `TreasureChest01` | `0x78` | `Tent01` |
| `0x3c` | `Ship01` | `0x79`–`0x7e` | `HouseWall01..06` |
| `0x41`–`0x43` | `SteelWall01..03` | `0x7f`–`0x81` | `HouseEtc01..03` |
| `0x44` | `SteelDoor01` | `0x82`–`0x84` | `Light01..03` |
| `0x45`–`0x4a` | `StoneWall01..06` | `0x85` | `PoseBox01` |
| `0x4b`–`0x4e` | `StoneMuWall01..04` | `0x8c`–`0x92` | `Furniture01..07` |
| `0x50` | `Bridge01` | `0x96` | `Candle01` |
| `0x51`–`0x54` | `Fence01..04` | `0x97`–`0x99` | `Beer01..03` |
| `0x55` | `BridgeStone01` | | |

Después, `0x60d5f0(k, "Object1\", 0x2901, 0x2600, 1)` para k de 0 a `0x9f`: las texturas de los 160
desde `Object1\`. *Verificado* en `lor_slots.bin`: los slots `0x41c..0x47e` son 99 texturas de
`Object1\` en el orden de k (de `tree.jpg` y `Tree_a.tga` a `plate2.jpg`), todas en el `Data\` del
laboratorio, y `0x47f` y `0x480` (`w_merchant_b` y `w_merchant_a`) son de la NPC de la tienda, que
se carga después. El último argumento en 1 es el que avisa y cierra con una textura que falta
(Q-C23); en Lorencia no pasa. El `.obj` de `World1` pide 109 tipos (2.870 registros) y los 109
`.bmd` están (*verificado*).

**Los especiales de cada mapa** (el `switch` de `0x617325`, con la tabla en `0x61a23c`), con sus
texturas desde `0x60d530(0x69)`:

| Mapa | Modelos | Otros |
| --- | --- | --- |
| 0 | `0xad` `Bird01` y `0xb2` `Fish01` (`Data\Object1\`) | sus texturas, `bird.tga` y `fish.tga`, en `0x69` y `0x6a`; el `0x6b` sigue con el `drr_03.jpg` del login (*verificado* en `lor_slots.bin`) |
| 1, 4 | `0xda` `DungeonStone01`, `0xaf` `Bat01` y `0xb0` `Rat01` (`Object2`) | |
| 2 | `0x201` `obj_donkey` y `0x202` `obj_flag` (`Data\Npc\`) | los crea en (19.100; 1.600; 270) y (19.100; 1.700; 270) |
| 3 | `0xae` `Butterfly01` (`Object1`) y `0x142..0x144` `warp01..03` (`Npc`) | crea el `0x142` en (22.300; 3.000; 0), con el giro (0, 0, 10) |
| 5 | `0xe7..0xeb` `Meteo01..05`, `0xef` `BossHead01` y `0xf0` `Princess01` (`Object6`) | no están en el laboratorio |
| 6 | `0xb1` `Bug01` (`Object7`) | |
| 7 | `0xb3..0xba` `Fish02..09` (`Object8`) | las `wt00..31` en `0x41..` y sus nombres en `0x7d3afd0` |
| 8 | `0xb2` `Bug02` (`Object9`) | `sand01` y `sand02` en `0x5c2` y `0x5c3`, `Impack03` en `0x716` |
| 10 | `0xbb` `cloud01` (`Object11`) | `Effect\clouds` en `0x622` y `cloudLight` en `0x623` |
| 11 a 17 | `0xbc` `Crow01` (`Object12`), con las texturas de `0x10c` y `0x10d` | `clouds` e `iBloodCastle.wav` |
| 18 a 23 | `0x1f7` `Angel` (`Player`) | `clouds` e `iChaosCastle.wav` |
| 24 a 29 y 36 | `0x11d..0x124` `cd71a..h`, `0x125` `dragonhead`, `0x126` `phoenix` y `0x127` `cundun_gone` | `Object25\water1.tga` en `0x23` y `water2.jpg` en `0x24` |
| 30 | — | el humo en `0x5c2` y `0x5c3`, `World31\Map1` y `Map2` en `0x69d` y `0x69e`, 33 sonidos `BattleCastle` |
| 31 | `0xae` `Butterfly01` | `clouds`, las texturas del jefe y los sonidos `w31` |
| 33 | `0xae`, `0x141` `tree_eff` (`Object34`) y `0xb2` `Bug02` (`Object9`) | `clouds` y los sonidos `w34` |
| 34 | `0x148` `scorpion` (`Object35`) y `0xed`, `0xee` `arrowstusk` | el humo, `mhoujin`, `Impack03`, `chasellight` en `0x729` y los sonidos `w35` |
| 55 | `0xb4` `Dragon` (`Object56`) | crea al dueño en (5.600; 23.000; 0) ([06](06-render-escenas-y-ui.md#el-mundo-del-login)) |

Después del lazo, en el mapa 1 el `+0x14` de un objeto del modelo 40 va a 0,4, y en el 8 el byte
`+0xc8` de los modelos 11, 12, 13, 73, 75 y 79 va a 0 (`0x61a126..`): sin leer qué son.

**Los tipos del `.obj` sin modelo** son pocos (*verificado*): `World2` 60, `World4` 43, `World5` 40,
`World9` 3, 35 y 38, `World12` 37, `World31` 35, `World35` 79 y 80, `World56` 82, y el −515 de
`World7` (Q-C28: `0x607370` y `0x606660` lo crean igual; que no se alcance desde el piso,
*inferido*). El original los saltea sin avisar (Q-C23).

## Texturas

| Formato | Original | Qué es |
| --- | --- | --- |
| tamaño | bucles `0x63bf81` / `0x63bb34` | potencia de 2 desde 1 hasta cubrir ancho y alto, tope 512 |
| OZT | `0x63bd00` | 4 bytes de prefijo + TGA de 32 bpp; lee ancho, alto y bpp, ignora tipo y descriptor; rechaza bpp ≠ 32 y lados > 512 |
| OZJ | `0x63b870` | 24 bytes de prefijo + JPEG, baseline o progresivo; lo decodifica libjpeg con sus parámetros por defecto, sin dar vuelta las filas; rechaza lados > 512 y lo que libjpeg no puede leer |
| JPEG | `0x66b8f0`..`0x67f0c0` | IJG libjpeg 6b enlazada estática |

Verificado: 9 tiles OZJ de Lorencia y 10 OZT, byte a byte contra los buffers de textura del cliente
vivo. `mu-client` escribió un decodificador JPEG propio compatible con libjpeg 6b para lograrlo.

**El OZT** (CL1.4): en el laboratorio son 664, todos TGA de color sin comprimir y sin id, con el
origen abajo; 649 con el descriptor `0x08` y 15 con `0x00`. El original no mira ninguno de esos
campos, así que no importa. En 17 los 4 bytes del principio no son los 4 primeros del TGA, y
tampoco importa, porque no se leen. Uno, `Monster\wolfa02.OZT`, es de 24 bits, y el original lo
rechaza (Q-C29). Ninguno pasa de 512 de lado, y 133 no son de lados potencia de 2. Después de los
píxeles, 663 traen el pie de 26 bytes de un TGA 2.0 y uno nada. Los 9 cursores de `Interface` y
`TileGrass01.OZT` de World1 dan, con las filas dadas vuelta y en RGBA, los buffers de la memoria
del original (`lor2_slot002..00a.bin` y `lor2_slot032.bin`).

**El OZJ** (CL1.4): en el laboratorio son 2.387, y 2.380 son JPEG: 1.641 baseline 4:4:4 con un
reinicio por fila de MCU, 623 progresivos 4:4:4 y 116 baseline 4:2:0 (25 con reinicios). Todos son
de 3 componentes, y con ninguno libjpeg avisa. Los otros 7 son TGA, y libjpeg los rechaza porque no
empiezan con `FF D8` (Q-C14). Uno, `skill\null.OZJ`, es de 800 × 600, y el original no lo carga
porque pasa de 512. De los 2.379 que cargan, 179 no son de lados potencia de 2. El cargador no mira
cuántos componentes trae el JPEG (Q-C31), pero en el laboratorio son siempre 3.

MuRet decodifica con libjpeg-turbo 3.2.0 (fija en `vcpkg.json`), con la API de la 6b
(`JPEG_LIB_VERSION` 62) y sus parámetros por defecto, como el original. Da los mismos bytes que la IJG libjpeg 6b en los 2.380
JPEG del laboratorio: `Client/tests/vectors/jpeg/lab-6b.txt` guarda el tamaño y un hash de lo que
da la 6b de cada uno, decodificado como lo hace el original, y `test_golden_jpeg_image.cpp` lo
compara con lo que da turbo. Los vectores de la misma carpeta, que corren en CI, traen los píxeles
de la 6b. Los 9 tiles OZJ de World1 dan los buffers de la memoria del original
(`lor2_slot023..02b.bin`). Fuera del laboratorio, turbo hace algunas cosas distinto que la 6b, y
quedan en Q-C33.

**La luz** (CL1.4): `TerrainLight.OZJ` es un OZJ con los mismos 24 bytes delante y el mismo
libjpeg, pero su cargador (`0x63b490`) no arma una textura. Da vuelta las filas, así que la fila 0
de la tabla es la última del JPEG, y pasa cada byte a float multiplicándolo por la constante de
`0x6b88f8`, 1/255 en float (`0x3B808081`). La tabla es la de `0x078ecc10`: 256 × 256 colores RGB,
`0xC0000` bytes. No mira el tamaño ni los componentes del JPEG (Q-C31), y si libjpeg corta, quien
lo llama (`0x5f8f40`) no se entera (Q-C32). En el laboratorio hay 24 luces, todas de 256 × 256 en
color: un `TerrainLight.OZJ` en los 19 mundos que la traen (World6 no tiene), `TerrainLight2`,
`TerrainLightTest` y `TerrainLightTest2` en World31, y `TerrainLight1` y `TerrainLight2` en World35.
Como con los `.att`, el cargador de mundo (`0x61a560`) y `0x4294c0` y `0x42fe80` piden
`TerrainLight2` en World31 y `TerrainLight1` o `TerrainLight2` en World35 según el estado del
juego. Los `TerrainLightTest` no los pide nadie. Las de Lorencia y Devias dan, con los mismos bits,
la luz que tenía en memoria el original (`lor_light.bin` y `s31_light.bin`).

## El pipeline de las DLLs

| DLL | Qué hace | Algoritmo |
| --- | --- | --- |
| `npcipher.dll` | cifra datos de NPC y monstruos | `K = SHA1(clave)`; modo 1 Blowfish, modo 0 AES-192; CFB de 8 bits; IV = `E_K(FF…FF)`; base64 |
| `mumsg.dll` | `message.wtf` | cabecera `CC 01 "WTF File"`, entradas `id:2 len:2 texto`, texto con XOR `0xCA` |
| `wz_zp.dll` | `.WZC` | bzip2 nivel 9 estándar |
| `wzCipher.dll` | `GetCRC32` | CRC-32 estándar, solo si el GUID coincide |
| `npchk.dll` | `CheckCRC` | MD5 contra 14 hashes permitidos |

Verificado: `npcipher` con 4 vectores reales en la VM; el resto con vectores estándar.

`main.exe` **no** carga estas cinco DLLs ni lee `message.wtf`: son del launcher y de las
herramientas. El launcher, `Mu.exe`, está decompilado en `decompilacion-maxima/Mu-unpacked.exe/`,
y sus imports, sacados de la memoria porque ASPack borra los del archivo
(`Mu-unpacked.exe/imports_candidates.csv`), traen `mumsg.dll` y `wz_zp.dll`; también lo dice
`decompilacion-maxima/metadata/relaciones_componentes.csv`. Cómo abre el juego está en
[02](02-arranque-y-ciclo-de-vida.md#del-launcher-al-juego). Revisado al arrancar la CL1.5
(2026-11-02), en el `main.exe` `c32bbd5f`:

- Mientras corre, no están cargadas: ninguna de las 92 listas de módulos del `main.exe` corriendo
  que guarda el research (`client-research/analysis/foxanti/evidence/`, por ejemplo
  `live-state.txt`) tiene `mumsg.dll` ni `wz_zp.dll`. Todas son de un `main.exe` de
  `SizeOfImage` `0x07963000`, el de las dos builds del laboratorio.
- *Estático*: no están en su tabla de imports, que trae 15 DLLs: `IMM32`, `DSOUND`, `OPENGL32`,
  `GLU32`, `WINMM`, `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `ole32`, `WS2_32`,
  `VERSION`, `wzAudio` y `FoxAnti`. La tabla está en la sección `.Silvana` (RVA `0x796201b`),
  donde va también el import de FoxAnti, que se inyectó; `objdump -p` muestra solo el primer
  descriptor, así que la lista sale de recorrer los descriptores hasta el que está en cero. No
  hay imports diferidos ni enlazados, ni callbacks de TLS.
- *Estático*: `npcipher`, `npchk`, `wzCipher`, `GetCRC32` y `CheckCRC` no aparecen en el binario,
  ni las tablas de bzip2 (`coverage-main.md` §3), ni pedazos de esos nombres armados en la pila
  (`"mums"`, `"wz_z"`, `"npci"`, `"wzCi"`).
- *Estático*: `mumsg.dll`, `wz_zp.dll` y `message.wtf` aparecen en un solo lugar, `0x4b96b0`, que
  corre en cada arranque, al principio de `WinMain` y después de cargar `Text.bmd`
  ([02](02-arranque-y-ciclo-de-vida.md#de-entry-al-bucle-de-mensajes)). Termina una actualización
  que dejó el launcher: si encuentra `mu._xe`, `mumsg._ll`, `wz_zp._ll` o `message._tf`, espera
  hasta 6 s a que se cierre la ventana "MU Auto Update" y copia cada uno sobre su nombre de verdad
  (`0x4b9550`: `CopyFileA`, que reintenta hasta 5 s, y después borra el temporal con
  `DeleteFileA`). Si llegó un `mu.exe` nuevo, avisa con un `MessageBox` "Update", lo abre con
  `WinExec` y no arranca el juego; tampoco arranca si la ventana no se cierra a tiempo o si falla
  una copia. Los archivos solo los copia: no los interpreta ni los carga. Hasta los textos de esos
  avisos salen de `Text.bmd` (los registros 13 y 14 de `g_GlobalText`).
- *Estático*: no nombra ninguno de los 14 exports de `mumsg.dll` (`CMsg::LoadWTF`, `CMsg::Get`…),
  y en todo su `.text` no hay un `xor` con `0xCA`, la clave de `message.wtf` (desensamblado
  lineal; el mismo método la encuentra en el lector del JoinServer, Q-C15). El `message.wtf` del
  laboratorio trae 57 mensajes del launcher (`tier1-mumsg.md`); los textos del juego salen de
  `Text.bmd` y de las demás tablas que carga.
- Las llamadas a `LoadLibraryA` con el nombre a la vista cargan `DDRAW.DLL`, `DINPUT.DLL`,
  `D3D8.DLL`, `D3D9.DLL` y `user32.dll`. Las demás están en un mismo bloque de código, de
  `0x688b60` a `0x68bbb0`: cinco llamadas en cuatro funciones (`LoadLibraryA` en `0x688c81`,
  `0x68a797` y `0x68ae6c`, y `LoadLibraryExA` en `0x68bb6e` y `0x68bb97`), que arman el nombre al
  correr con cadenas que se descifran en ese bloque (`0x68de80`). En el mismo bloque hay también
  `CreateFileA` y `FindFirstFileA` con nombres armados al correr. No se descifraron: son cadenas
  escondidas a propósito, probablemente de la protección, que está fuera de alcance.

Lo que queda abierto es ese bloque: carga y libera las DLLs en la misma función, así que una lista
de módulos tomada en un momento no las ve, y podría abrir un archivo por su nombre cifrado. Sin
un `xor` con `0xCA` en el binario, eso sería, a lo sumo, mirar que el archivo esté, no leer sus
mensajes. *Inferido* (alta) que no lee `message.wtf`; lo cerraría una traza de Procmon en la VM
filtrada por `mumsg` y `message.wtf` ([08](08-pendientes.md)). Alcanza para que MuRet no lo lea:
la CL1.5 salió de la ola, y vuelve si esa traza lo muestra.

## Modelos `.bmd`

Los modelos 3D (personajes, monstruos, objetos) también son `.bmd`. El contenedor está recorrido
en los 1.962 modelos del cliente sin bytes sobrantes (`scripts/asset_manifest.py`, `walk_bmd`):

```text
"BMD" + versión (1 byte)
  v0x0C: tamaño (u32) + payload cifrado con K16 (el mismo de EncTerrain)
  otra:  el resto va en claro (v0x0A, y cuatro con 0x00)
nombre[32]
n_mallas, n_huesos, n_acciones        (int16 cada uno)
por malla:   n_vert, n_norm, n_uv, n_tri, textura (int16 x5)
             vértices 16 B · normales 20 B · UV 8 B · triángulos 64 B · nombre de textura[32]
por acción:  n_keys (int16), lock_positions (u8), y si lock: 12 B por key
por hueso:   dummy (u8); si 0: nombre[32] + padre (int16) + 24 B por key de cada acción
```

Verificado el **tamaño** de cada parte (el recorrido cierra exacto en los 1.962 archivos) y,
contra las trazas de OpenGL del original ([06](06-render-escenas-y-ui.md)), el **significado** de
lo que hace falta para dibujar un personaje animado:

```text
vértice    hueso (int16) + relleno (int16) + posición float[3] en el espacio de ese hueso
normal     hueso (int16) + relleno en 0 (int16) + normal float[3] + índice de vértice (int16)
           + relleno en 0 (int16)
UV         u, v (float)
triángulo  tipo (u8; 3 en todas las mallas) + relleno; índices int16 de 4 vértices en +2, de 4
           normales en +10 y de 4 UV en +18; en +0x1a..+0x1f, 6 bytes que el lector pisa (abajo);
           en +0x20..+0x23, 4 que copia como vienen; los 28 que siguen no se copian
hueso      por cada acción: n_keys posiciones float[3] y después n_keys rotaciones float[3]
           (ángulos de Euler en radianes); los dos, relativos al padre; padre −1 en la raíz
```

**El lector, en el binario** (CL6.R, *estático* en `0x4f13d0`, con los 1.962 archivos recorridos
por un script que lee como él, sin bytes de sobra en ninguno):

- `0x60da30(modelo, carpeta, nombre, número)` arma el nombre: `"%s.bmd"` con -1, `"%s0%d.bmd"`
  con un número menor que 10 (con signo: un -2 da `Nombre0-2.bmd`) y `"%s%d.bmd"` con los demás.
  Con `DAT_006c9e0c` en 1 (el valor de la `.data`; las tres escrituras de 0 están detrás de
  `DAT_07d366bc`, que nadie escribe) lee con `0x4f13d0`; `0x4f1ac0` **escribe** un `.bmd` (con
  `"wb"`) y solo corre en el modo de desarrollo, con `DAT_006c9e0c` en 0.
- Un archivo que no abre se saltea sin avisar, salvo que el nombre sea `Monster`, `Player` o
  `PlayerTest`: `"%s file does not exist."`, `MessageBoxA` y `WM_DESTROY` (Q-C23). `Player.bmd`
  con mallas también cierra el juego (`"Player.bmd file error!!!"`, Q-C88).
- El byte 3 es la versión y va a `modelo + 0x20`: con `0x0C`, el tamaño en `+4` y K16 desde `+8`
  (la misma clave, semilla y paso que el terreno); con cualquier otra, en claro desde `+4`. No mira
  el `"BMD"` ni que el tamaño entre en el archivo (Q-C86). Hay 816 de `0x0C`, 1.142 de `0x0A` y 4
  de `0x00` (`Object3\Object25`, `27`, `33` y `90`), que se leen en claro.
- Los tres int16 de la cabecera van, en el orden del archivo, a `+0x24` (mallas), `+0x22`
  (huesos) y `+0x26` (acciones) del modelo de `0xe0` bytes; los arreglos, a `+0x28` (mallas de
  `0x28`), `+0x2c` (huesos de `0x8c`), `+0x30` (acciones de `0x10`) y `+0x34` (los nombres de
  textura), y `+0x38` guarda un slot de textura por malla (el de
  [06](06-render-escenas-y-ui.md#el-modelo-en-el-binario)).
- De cada triángulo copia 36 bytes, y `0x4f0870` y `0x4f0910` pisan los de `+0x1a..+0x1f` con la
  adyacencia de las aristas que calculan: lo que trae el archivo ahí no se usa.
- La normal: los 301.367 índices de vértice entran en el rango de su malla (que sea el vértice
  que la usa, *inferido*).
- Los 450.338 triángulos del laboratorio son de tipo 3, y sus índices de vértices, normales y UV
  entran en su malla. Cada padre va antes que su hijo, salvo en `NPC\BloodCastle01.bmd`, donde
  los huesos 43 y 44 cuelgan del 45 (Q-C108). El más grande es `player\playerTest.bmd`, de
  2.414.277 bytes, y ningún cifrado trae bytes después de su bloque (*verificado* con `LoadBmd`, CL6.1:
  `GoldenBmd`).
- De cada acción escribe `+0` (no repite) en 0, las keys en `+8`, el `lock_positions` en `+0xa` y
  las posiciones de `lock` en `+0xc`; **no escribe la velocidad de `+4`**: el arreglo sale de
  `operator new` sin limpiar, y una acción a la que `0x60dbe0` no le pone velocidad queda con lo
  que había en el heap (Q-C87). Las posiciones de `lock_positions` se guardan y nada las lee (129
  modelos tienen alguna acción con `lock`).
- De cada malla, lo que sigue al primer `_` del nombre de su textura, hasta el primer punto, es
  un script (`0x459940`): los caracteres 1 a 4 prenden, en mayúscula, `R`, `H`, `S` y `N` los
  bytes 0 a 3 de `malla + 0x24`, y cualquier otro deja la malla sin script (los casos `DC` y `DT`
  son código muerto: el byte 4 queda en 0). En el laboratorio, 142 mallas tienen script (135 `R`,
  6 `H` en `Object11\Object01` a `06` y 1 `S` en `Item\Quest03`, ninguna `N`), y 17 son de partes
  del personaje, todas `R`: el pelo de los cascos (`hair_R.jpg`), `FShoots_R.jpg` y `lowerTest_R.jpg`
  y `lowerTest2_R.jpg` (*verificado* en la CL6.1, `GoldenBmd.TheTextureScriptsOfTheLab`; antes este
  punto decía que ninguna); los 13 nombres en minúscula (entre ellos los `_s` de `player\`) no
  cuentan. Una letra que no es de las cuatro deja la malla sin script, también las que ya prendió:
  `0x459a2f` devuelve 0 y el llamador (`0x4f17f3`) no lo guarda. Qué hace
  cada byte en el dibujo, en [06](06-render-escenas-y-ui.md#el-modelo-en-el-binario).
- Al final, `0x4f1f20(0)`: el hueso de la cabeza (`+0x54`) en -1 y, por hueso, una caja con el
  mínimo y el máximo de los vértices que cuelgan de él (`0x4f1f80`, las 8 esquinas en `hueso +
  0x2c..0x8b`, con la z más rápida). No mira que el hueso del vértice entre en la cuenta.

- **Cada vértice cuelga de un solo hueso**, rígido: no hay pesos.
- La pose de un hueso es su rotación (los tres ángulos pasados a cuaternión como hace
  `AngleQuaternion` de Quake/Half-Life: Rz · Ry · Rx) y su posición, compuestas con las del padre.
- **Entre una key y la siguiente se interpola** con el frame fraccionario: la posición lineal y la
  rotación por cuaternión (slerp y nlerp no se distinguen en la traza; interpolar los ángulos da
  peor). Después de la última key se vuelve a la primera.
- **El esqueleto y las mallas van en archivos distintos.** `Player.bmd` es el esqueleto con las
  animaciones: 60 huesos (10 dummy), 205 acciones y ninguna malla. Las partes (`HelmMale17.bmd`,
  `ArmorMale17.bmd`, `PantMale02.bmd`…) traen las mallas, y el hueso de cada vértice es un índice
  en los huesos de `Player.bmd`: la pose sale de las acciones del esqueleto. Las partes traen su
  propia copia de los huesos (125 con 56, 10 dummy; 86 con 51, 6 dummy; todas con una acción de
  una key): el lector la lee y solo sirve para las cajas de `0x4f1f80`. El dibujo de una parte
  (`0x60cf70`) no posa sus huesos: usa las matrices que dejó la pose del cuerpo (*estático*).
- Las normales se contrastaron contra las trazas por lo que sale de ellas: las UV del brillo
  (CL6.2 C) y la luz por vértice (CL6.4 PR B), en [06](06-render-escenas-y-ui.md). Sin contrastar,
  los huesos dummy, que la pose no toca (`0x4eb860`, *estático*).

Cómo se verificó: cada bloque de `GL_TRIANGLES` de las trazas se emparejó con la malla que manda la
misma secuencia de UV (cada triángulo, sus tres UV en orden); con esa malla se probaron las 205
acciones de `Player.bmd` cada 0,25 frame, se afinó la mejor y solo se ajustaron la escala, una
rotación y una posición del objeto. El personaje de la selección queda a menos de 0,016 unidades
de la traza en los 5 frames (una celda del mapa mide 100). El detalle está en
[06](06-render-escenas-y-ui.md) y lo que falta en [08](08-pendientes.md) (P-C01).

## Audio

`wzAudio.dll` está decompilada entera (390 funciones) y es un derivado de **CoolPlayer 215**
(`decompilacion-maxima/INDEX.md` §3): 17 exports con ABI revisada (`abi/wzAudio_abi.h`), Ogg con
`vorbisfile.dll` + `ogg.dll` (compatibles con libogg 1.1 y libvorbis 1.0.x) y MP3 propio. La
música es un MP3 por mapa (`paso6-ui-map.md` §6); los efectos son WAV por DirectSound
(`0x41ed10` play, `0x41ee00` stop). *Estático.*

## Cliente contra servidor

Las tablas del servidor en uso del laboratorio están personalizadas (portales, requisitos de
items, skills, 70 celdas de atributos en la plaza de Lorencia). **El cliente se reconstruye fiel a
sus propios datos**, no a los del servidor: si difieren, el servidor decide y el cliente se entera
por el protocolo (`divergencias-cliente-servidor.md`).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Cargadores que escriben en globales del juego | `Data::` funciones puras: bytes → tipos de C++ (`ItemTable`, `TerrainMap`…), sin globales, testeables sin ventana | CL1 |
| `MessageBox` + `WM_DESTROY` si falta un archivo | error explícito al que llama (un `Expected` propio, porque `std::expected` es de C++23), sin UI en la capa de datos | CL1 |
| libjpeg 6b estática | libjpeg-turbo de vcpkg, con la API de la 6b y sus parámetros por defecto: da los mismos bytes que la 6b en los 2.380 JPEG del laboratorio, y lo que difiere fuera de ellos queda en Q-C33 (pregunta 1 del roadmap, decidida el 2026-11-01) | CL1 |
| Rarezas de datos (checksum anulado de `Item.bmd`, alturas desde BMP + 1080, registro 7 de `Text.bmd` sin NUL, OZJ que son TGA) | **se reproducen**: son parte de cómo se leen los archivos reales (ver [07](07-quirks-y-bugs.md)) | CL1 |
| La carpeta de un mapa sale de `0x61a560`, con tres rangos de evento, el 32 que usa la del 9, y sin mirar el rango | `Data::WorldFolderNumber(map)` y `WorldFolder(map)` para cualquier mapa de 0 a 255, con la tabla de arriba; un mapa sin carpeta en el `Data\` es un error que la nombra | CL5, CL7 |
| Esqueleto (`Player.bmd`) y partes del equipo en archivos separados | `Data::Model` lee los dos con el mismo parser; la pose se calcula con el esqueleto y cada parte se dibuja con esa pose | CL6 |
| `0x4f13d0` no mira `"BMD"` ni el tamaño, y lee en claro cualquier versión que no sea `0x0C`; un modelo que falta se saltea salvo `Monster`, `Player` y `PlayerTest` | `LoadBmd` acepta en claro cualquier versión que no sea `0x0C` (como el original) y rechaza un tamaño que no entra (Q-C86); `LoadModelFile` devuelve el error y quien carga decide con la regla de los tres nombres (Q-C23) | CL6.1 |
| Los assets son del juego | MuRet **no** versiona ningún asset; los tests golden reciben la ruta por `MURET_CLIENT_GOLDEN_DIR` y se saltean si no está | CL1 |
| `Gate.bmd`: nueve campos que solo lee la detección de `0x599ef0`, y una tabla que no es la del servidor | `GateTable` con los nueve campos (hoy son 9 bytes sin nombre) y la detección como una función pura; el rebote del servidor se acepta (Q-C182; propuesta del barrido de Lorencia) | CL7.5 |
| Los modelos del mundo: el lazo genérico, los 112 con nombre del mapa 0 y los especiales de cada mapa desde `0x69` | `WorldModels` conserva el lazo genérico y el dragón del 55; en mapa 0 usa las 112 solicitudes de `LorenciaModelFiles`, Bird01 y Fish01. Las texturas de objetos se asignan por ID después de las cargas. Otros especiales, texturas comunes y ciclo de objetos siguen pendientes | CL7.L2, entrega de modelos |
| Los bits del `.att`, con el `0x02` vivo y el `0x80` rechazado en el archivo | el WORD de cada celda como estado del juego, con el `0x02` que se limpia y se marca en cada paso (propuesta del barrido) | CL7.4 |

Tests que fija:

- Por cada formato, un round-trip con datos sintéticos (siempre corre) (CL1).
- Por cada formato, comparación byte a byte contra `mu-golden` (`cliente-data*`, y la memoria del
  original para lo que tenga volcado), salteada sin la carpeta (CL1).
- Por cada formato, recorrer **todos** los archivos de `assets/original-data/` y comparar con
  `assets/decoded/` y con los conteos de `assets_manifest.csv` (CL1; los modelos en CL6).
- XOR3, K16 y checksum con vectores literales chicos escritos a mano (CL1).
- La carpeta de cada mapa de la tabla, y que los 20 mundos del laboratorio son la de alguno
  (`WorldFolderTest`, `GoldenWorldFolder`, CL5.4).
- Los 1.962 modelos se leen sin bytes de sobra, con las cuentas de versión (816, 1.142 y 4) y de
  script (142 mallas) de arriba (`GoldenBmd`, CL6.1); un `.bmd` sintético con la versión
  `0x00` se lee en claro, y uno con un tamaño que no entra se rechaza (`BmdTest`, CL6.1).
- Con lo que propone el barrido de Lorencia: los slots `0x69..0x6b` y `0x41c..0x480` de Lorencia
  dan los nombres de `lor_slots.bin`; la tabla de tipos y archivos del mapa 0, sacada del asm por
  script, contra la de MuRet; cada mapa de `OriginalMaps` menos el 35 carga su terreno, su `.obj` y
  sus modelos, con los tipos sin modelo de arriba; y los cuatro gates de Lorencia con el `Gate.bmd`
  real (adentro, en el borde, afuera y con el nivel justo) (CL7, CL7.5).
