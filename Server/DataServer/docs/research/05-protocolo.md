# 05. Protocolo: los 22 heads

El fuente original es `protocol.cpp` (ver los `__FILE__` de los logs). El dispatcher es `0x40b190`
y los handlers van de `0x40b490` a `0x40d840`. Las SQL que cita cada head están verbatim en
[06](06-sql-y-base.md). Base de direcciones `0x400000`.

## El dispatcher (`0x40b190`)

El consumidor le pasa `(head, payload, len, connIdx)`. El dispatcher:

1. marca actividad en StateMode (`0x401070`): pone el estado en 1 y guarda el tick (es lo que apaga
   el banner tras 30 s de silencio);
2. hace `switch(head)` sobre dos tablas en `.text` (`0x40b360` y `0x40b3bc`) y llama al handler;
3. un `head` sin handler se ignora en silencio.

Hay **exclusión total**: el consumidor tiene el lock global, así que nunca corren dos handlers a la
vez (ver I-3 en [03](03-hilos-y-concurrencia.md)).

## Convenciones de las tablas

- **"off"** es el offset del campo dentro del **payload**, contando desde el byte `0xC1`/`0xC2`
  (igual que lo indexa el original). El `head` está en `+2` (C1) o `+3` (C2).
- Respuestas: `C1 <size> <head>` o `C2 <sizeHi> <sizeLo> <head>`; `result@+3` es el byte de
  resultado típico.
- Niveles: sin marca = Verificado en el assembly; *seam* = se conoce la estructura pero no los
  bytes exactos; *inferido* = deducción.

## Tabla de heads

| Head | Nombre | Qué hace | Respuesta |
| --- | --- | --- | --- |
| `0x00` | login de GameServer | registra el slot como GS (`0x4056b0`): clase@+3, puerto u16@+4, nombre@+6 (0x30), serverId u16@+0x38; ejecuta la query de `ItemCount` de arranque | `C1 08 00`: result@+3, ItemCount@+4; marca `active=2` y `serverId%20` en `+0x50` |
| `0x01` | lista de personajes de la cuenta | Id@+3 (10 bytes); `SELECT Id` → si no existe, `INSERT`; luego `SELECT *`; arma la lista con preview de cada personaje | `C2 … 01`: cabecera 0x18 + 0x4E por personaje; **saltea** los previews que fallan |
| `0x02` | broadcast | reenvía el paquete recibido a todos los slots activos; `len = byte[1]` | ninguna (no es ACK) |
| `0x04` | crear personaje | Id@+0xE, Name@+0x18, clase@+0x22 (nibble alto); 3 chequeos de nombre sin corto-circuito; `MX_CreateCharacter`; blobs Inventory/MagicList/Quest y `SetGameIdSlot` solo si el SP anduvo | `C1 34 04`: result 1/0/2; clase > 3 → DK, log `error-L3 : Class over value` |
| `0x05` | borrar personaje | id u16@+4, acc@+6, name@+0x10, guildFlag@+0x1A, G_Name@+0x1B | `C1 10 05`: result 0 solo si acc/name vacíos; cascada de gremio siempre, **después** del envío |
| `0x06` | cargar personaje | acc@+3, nombre cifrado@+0xE (XOR `FC CF AB`), userIdx@+0x1A; `MX_SelectCharacter`; Leaderpoint en out+0x3B7; actualiza `GameIDC`; manda además el paquete de opciones | `C2 03BC 06` siempre; Life/Mana ×10 *inferido* |
| `0x07` | guardar personaje | offsets como en el contrato; ushorts ×0.1; blobs solo si el `UPDATE` anduvo | sin ACK explícito |
| `0x08` | abrir bodega | acc@+… ; si no existe → `INSERT` + `C1 10 10`; si existe → `MX_SELECTmoney` (Money/DbVersion/"pw" *seam*) + `MX_OPENwarehouse` | `C2 04C8 08` |
| `0x09` | cerrar bodega + dinero | acc@+4, money@+0x10, items@+0x14 (1200 bytes), pw@+0x4C6; `MX_UPDATEmoney` + `MX_CLOSEwarehouse` con blob | sin ACK explícito |
| `0x11` | guardar inventario | name@+4 (8 bytes), blob@+0xE; `UPDATE … Inventory=?` | sin ACK explícito |
| `0x12` | guardar bodega + dinero | acc@+3, money@+0x10; `UPDATE warehouse SET Money` | sin ACK explícito |
| `0x20` | actualizar ItemCount del GS | int@+4; `UPDATE GameServerInfo SET ItemCount` | sin ACK explícito |
| `0x30` | crear gremio | validaciones de nombre (silenciosas); `CanOperate` → result 0; quirk → result 3 | `C1 44 30`: result@+3, flag@+4, Number@+8, master@+0xE, guild@+0x19, mark@+0x22 |
| `0x31` | borrar gremio | toma el lock de gremios vía `0x4039b0` | result 2/4, broadcast con flag@+4 |
| `0x32` | agregar miembro | — | result 4 al que pidió |
| `0x33` | quitar miembro | — | result 3 al que pidió |
| `0x37` | actualizar score del gremio | — | sin respuesta bajo el quirk |
| `0x38` | actualizar aviso del gremio | — | broadcast `C1 48 38` inalcanzable |
| `0x40` | heartbeat / sync de gremios | crea un hilo (`0x403d10`) que recorre la caché de gremios | `C1 08 41` sub0+count, `C1 40 40` por gremio, `C1 08 41` sub1 |
| `0x52` | pedir serial de item | — | `C1 1C 52`: serial@+8 |
| `0x60` | guardar opciones | name@+3, SkillKey@+0xD, opts@+0x17..+0x1B | sin ACK explícito |
| `0xCF` | transferir personaje | name@+3; `SP_CHARACTER_TRANSFER` | `C1 12 CF`: echo@+0xE, result@+0x10 |

## Los flujos que importan

### `0x00` — login del GameServer

Es el primer mensaje que manda un GS al conectarse. Registra el slot (`0x4056b0`), ejecuta
`SELECT ItemCount, Number from GameServerInfo where Number=0` (la misma que el arranque) y devuelve
el `ItemCount`. Si la query falla, el `ItemCount` que viaja es `-103` o `-101` (ver
[06](06-sql-y-base.md)): **el error del arranque se propaga al GS** en vez de impedir el servicio.

### `0x01` — lista de personajes

Es el flujo más largo. Toma el Id de la cuenta, se asegura de que exista (`SELECT Id`; si no,
`INSERT`), trae la cuenta entera (`SELECT *`) y, por cada uno de los personajes de la cuenta, hace
un preview (`vCharacterPreview`). Si un preview falla, **saltea ese personaje sin reservar su lugar
en la respuesta** (`0x40b738`): el GS recibe una lista más corta, no un hueco. El u16 en `+0xE` del
request (`0x12C0 = 4800` en la captura) es *inferido* el índice de usuario del GS; el byte de
`+0xD` es relleno.

### `0x04` — crear personaje

Tres chequeos de nombre (sintaxis, apóstrofo/espacio, badsyntax) **sin corto-circuito**: los corre
los tres aunque el primero ya rechace. Llama a `MX_CreateCharacter` con la lista fija de stats
([07](07-datos-auxiliares.md)); solo si el SP devuelve éxito escribe los blobs iniciales
(Inventory 0x2F8, MagicList 0x3C, Quest 0x32) y reserva el slot con `SetGameIdSlot`. Una clase mayor
a 3 se baja a Dark Knight y loguea `error-L3 : Class over value` (línea 425 de `protocol.cpp`).

### `0x06` — cargar personaje

El nombre viene cifrado con un XOR de clave `FC CF AB` (3 bytes, cíclica). Arma una respuesta fija
de `0x3BC` bytes con todo el personaje, escribe el Leaderpoint en `out+0x3B7`, actualiza `GameIDC`
y manda **además** un segundo paquete con las opciones (`C1 24 60`). Multiplicar Life/Mana por 10 es
*inferido*.

### `0x40` — el que destapa el bug de gremios

Crea un hilo nuevo por cada pedido (`0x403d10`) que manda la caché de gremios en RAM. Como esa
caché está **siempre vacía** ([Q-01](08-quirks-y-bugs.md#q-01-los-gremios-nunca-funcionan)), lo
único que llega al GS son las dos cabeceras `C1 08 41` (sub0 con count 0 y sub1). Ese hilo es el
lado "gremios → global" de la inversión de locks C-1 ([03](03-hilos-y-concurrencia.md)).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| `switch(head)` con dos tablas y SQL embebido en cada handler | un mapa `head → handler`; cada handler usa un repositorio (memoria u ODBC), sin SQL inline | 5, 6 |
| Offsets a pelo sobre el payload | structs de request/response por head, con parseo y validación explícitos | 1, 5 |
| Chequeos sin corto-circuito, clase > 3 silenciosa, previews salteados | se conserva el **comportamiento observable** (mismos bytes de salida) y se documenta cada rareza como test | 5, 7 |
| `ItemCount` de error propagado al GS | igual, por compatibilidad, pero logueado como error | 5 |
| XOR `FC CF AB`, Life/Mana ×10 | helpers nombrados y testeados contra vectores | 5, 7 |

Tests que fija: por cada head, un vector request→(SQL, response) comparado byte a byte contra el
original/oráculo (ola 5+), incluidos los casos raros de `0x01`, `0x04` y `0x40`.
