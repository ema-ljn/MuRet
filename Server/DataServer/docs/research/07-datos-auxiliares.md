# 07. Datos auxiliares

Lo que no es ni red ni SQL ni concurrencia: las tablas que el servidor carga o guarda en RAM y usa
dentro de los handlers. Casi todo se llena en el arranque desde archivos de texto o se recupera del
`.data` del proceso vivo.

## Stats de inicio por clase

Tabla en `0x643f974`, stride `0x65C`. **No está en el archivo** (queda en cero al cargar) y no hay
escrituras en el binario: se puebla en runtime. Los valores se recuperaron del `.data` vivo del
pid 624 en la VM y coinciden con los stats canónicos de MU. La usa `0x04` para
`MX_CreateCharacter`.

| Clase | Str | Dex | Vit | Ene | Life | MaxLife | Mana | MaxMana |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 Dark Wizard | 18 | 18 | 15 | 30 | 60 | 60 | 60 | 60 |
| 1 Dark Knight | 28 | 20 | 25 | 10 | 110 | 110 | 20 | 20 |
| 2 Fairy Elf | 22 | 25 | 20 | 15 | 80 | 80 | 30 | 30 |
| 3 Magic Gladiator | 26 | 26 | 26 | 26 | 110 | 110 | 60 | 60 |

Corrección de una revisión adversarial: el float en `+0x14` es **MaxLife**, no Mana; Mana/MaxMana
están en `+0x1C`/`+0x20`. El handler `0x04` pasa Life=`+0x10`, MaxLife=`+0x14`, Mana=`+0x1C`,
MaxMana=`+0x20`. Una clase > 3 se baja a Dark Knight ([05](05-protocolo.md)).

## Blobs del personaje nuevo

Al crear un personaje (`0x04`), si `MX_CreateCharacter` anduvo, el servidor escribe tres blobs
iniciales: Inventory `0x2F8` bytes, MagicList `0x3C`, Quest `0x32`. El inventario inicial se arma
con la tabla de items (durabilidad desde `rec + 0x30`).

## Tabla de items

Tabla en `0x6444238`, 512 registros de `0x58` bytes. Se carga en el arranque (`0x408080`) desde
`..\data\lang\kor\item(kor).txt` — **siempre la versión coreana**, sea cual sea el idioma de
`commonserver.cfg`. El índice de un item es `code = type*32 + idx`. Campos que importan: nombre,
ancho/alto en el grid (`+0x23`/`+0x24`), durabilidad (`+0x30`), flags de clase (`+0x3E..0x41`). Un
item es válido si tiene slot y skill distintos de cero (`0x407f80`). Nada se hardcodea: todo sale
del archivo en runtime.

## Filtros de nombres

Dos tablas que rechazan nombres de personaje y de gremio. Se cargan en el arranque (contenedor
`0x4048b4`).

| Filtro | Objeto | Capacidad | Fuente |
| --- | --- | --- | --- |
| badsyntax | `0x43f960` | 100 × 25 | `..\data\badsyntax.txt` |
| reservados | `0x63df720` | 1000 × 20, **acumulativo (3 pasadas)** | items, `Monster.txt`, `Skill(<idioma>).txt` |

La tabla de reservados se llena en tres pasadas (nombres de items, de monstruos y de skills), así
que impide usar como nombre de personaje cualquier nombre del juego. El `Skill(<x>).txt` sí depende
del idioma (`Language` de `commonserver.cfg`, 3 = chino en el laboratorio); los nombres de item para
el filtro salen de la misma tabla coreana.

Los dos chequeos de sintaxis (sin espacio `0x406850`, sin apóstrofo/espacio `0x406820`) más los dos
de tabla (badsyntax, reservados) los corre `0x04` sin corto-circuito, y `0x30` de forma silenciosa
([05](05-protocolo.md)). La comparación de nombre de gremio es **case-sensitive** (`strcmp`); la de
columnas SQL, no ([06](06-sql-y-base.md)).

## Cifrado del nombre en `0x06`

El nombre que llega en el "cargar personaje" (`0x06`) viene XOR con una clave de 3 bytes
`FC CF AB` (cíclica), en `0x43ac04`. El servidor lo descifra in situ (`0x4067e0`, simétrico) sobre
`resp + 0x12` antes de validarlo y consultarlo. Es ofuscación, no seguridad.

## Caché de gremios en RAM

Gestor en `0x6411e40`: lista doblemente enlazada de entradas `DS_GUILD` de `0x25C` bytes, con una
`CRITICAL_SECTION` (`0x6411e58`), un contador (`0x6411e54`) y un estado de sincronización
(`syncDone`/`syncBusy`).

Layout de una entrada (verificado con asserts de compilación):

| Offset | Campo |
| --- | --- |
| `+0x000` | number (u16) |
| `+0x002` | G_Name (13) |
| `+0x00F` | G_Mark (32, blob) |
| `+0x02F` | cantidad de miembros |
| `+0x030` | nombres de miembros (30 × 13; el slot 0 es el master) |
| `+0x1B6` | miembro activo (30) |
| `+0x1D4` | estado del miembro (30 × u16; `0xFFFF` = offline) |
| `+0x214` | score |
| `+0x218` | G_Notice (60) |
| `+0x254` / `+0x258` | enlaces prev / next |

**Esta caché está siempre vacía** en el original: solo se agrega un gremio en la rama de éxito de
`0x30`, que no ocurre, y no hay loader desde la base al arrancar. Es la consecuencia de
[Q-01](08-quirks-y-bugs.md#q-01-los-gremios-nunca-funcionan). El gate `CanOperate`
(`syncDone==0 && syncBusy!=0` → 0) y el sync del heartbeat `0x40` operan sobre esta lista vacía.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Stats por clase recuperados del `.data` vivo | tabla de datos versionada en el repo, citando la evidencia de la VM | 7 |
| Tabla de items cargada de `item(kor).txt` | cargador de la tabla con tests sobre un archivo de ejemplo; idioma configurable | 7 |
| Filtros de nombres en 3 pasadas, rutas fijas | cargador de filtros con las mismas reglas; rutas en config | 5, 7 |
| XOR `FC CF AB` in situ | helper nombrado y simétrico, con vector de prueba | 5 |
| Caché de gremios que nunca se llena | la caché se llena de verdad (Q-01 arreglado y documentado); sync sin locks cruzados | 7 |

Tests que fija: stats por clase exactos para `MX_CreateCharacter`; carga de la tabla de items desde
un archivo de ejemplo; filtros aceptan/rechazan los nombres correctos; round-trip del XOR; alta,
baja y búsqueda en la caché de gremios.
