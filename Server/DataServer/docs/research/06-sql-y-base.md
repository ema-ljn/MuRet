# 06. SQL y base de datos

El DataServer es el único proceso que toca SQL Server. Toda la base se accede por ODBC a través de
una clase propia, `CQuery`. Este documento tiene el catálogo de SQL verbatim, el esquema que se
deduce de él, y la semántica de `CQuery`. Las cadenas están copiadas tal cual del `.rdata`: **la
ortografía importa**, el CustomDB implementa los `MX_*` con esos nombres exactos.

## Las ocho conexiones

Al arrancar (`OnStart`, [02](02-arranque.md)) se abren 7 conexiones y una octava la abrió un
constructor estático. Cada una es una instancia de `CQuery` con sus propios handles ODBC. Todas
llaman a `CQuery::Connect(3, "MuOnline", "Admin", "BobStudio")`, o sea `SQLConnect` contra el DSN
`MuOnline`. Como el DSN tiene `Trusted_Connection=Yes`, el driver **ignora** `Admin`/`BobStudio` y
entra con la cuenta de Windows del proceso.

| Instancia | Dirección | Tablas / SP que toca | Heads |
| --- | --- | --- | --- |
| serial | `0x6437de0` | `GameServerInfo`, `WZ_GetItemSerial` | `0x20`, `0x52`, arranque, `0x00` |
| cuenta | `0x6430460` | `AccountCharacter` | `0x01`, `0x04`, `0x05`, `0x06` |
| personaje | `0x6428ae0` | `Character`, `MX_CreateCharacter`, `MX_SelectCharacter`, `SP_CHARACTER_TRANSFER` | `0x04`–`0x07`, `0x11`, `0xCF` |
| preview | `0x640a4c0` | `vCharacterPreview` | `0x01` |
| bodega | `0x63fb1c0` | `warehouse`, `MX_OPENwarehouse`, `MX_SELECTmoney`, `MX_UPDATEmoney`, `MX_CLOSEwarehouse` | `0x08`, `0x09`, `0x12` |
| opciones | `0x63e4548` | `OptionData` | `0x06`, `0x60` |
| `CItemExt` | `0x63ebec8` | — | **conecta, nadie la usa** |
| `EventDBSet` | `0x6402b40` | — | **conecta en un ctor estático, nadie la usa** |

Y dos `CQuery` de gremios que **nunca llaman a `Connect`**: `0x63f3848` (tabla `Guild`) y
`0x6411e78` (miembros). Esa es la raíz de
[Q-01](08-quirks-y-bugs.md#q-01-los-gremios-nunca-funcionan): todas las SQL de gremios corren sobre
conexiones muertas.

## Catálogo de SQL (verbatim)

### Cuenta / AccountCharacter

```sql
SELECT * FROM AccountCharacter where Id='%s'            -- 0x43acb4
SELECT * FROM AccountCharacter WHERE Id='%s'            -- 0x43aee8 (otra, con WHERE en mayúscula)
SELECT Id FROM AccountCharacter WHERE Id='%s'
INSERT AccountCharacter (Id) VALUES ('%s')
UPDATE AccountCharacter SET GameID1='%s' WHERE Id='%s'  -- …GameID2..5 y GameIDC iguales
```

### Personaje (Character)

```sql
SELECT Quest     from Character where Name='%s'
SELECT MagicList from Character where Name='%s'
SELECT Inventory from Character where Name='%s'
DELETE FROM Character WHERE AccountID='%s' AND Name='%s'
UPDATE Character SET cLevel=%d,Class=%d,LevelUpPoint=%d, Experience=%d, Strength=%d,
  Dexterity=%d, Vitality=%d, Energy=%d, Money=%d, Life=%f, MaxLife=%f, Mana=%f, MaxMana=%f,
  MapNumber=%d, MapPosX=%d, MapPosY=%d, MapDir=%d, PkCount=%d, PkLevel=%d, PkTime=%d,
  DbVersion=1 where Name = '%s'
UPDATE Character SET Inventory=? where Name='%s'   -- blob por parámetro
UPDATE Character SET MagicList=? where Name='%s'
UPDATE Character SET Quest=?     where Name='%s'
```

### Preview, GameServerInfo

```sql
SELECT Name, cLevel, Class, CtlCode, DbVersion FROM vCharacterPreview WHERE Name='%s'
SELECT Inventory FROM vCharacterPreview WHERE Name='%s'
SELECT ItemCount, Number from GameServerInfo where Number=0   -- arranque y 0x00
UPDATE GameServerInfo SET ItemCount = %d where Number=0       -- 0x20
```

### Gremios (todas sobre conexiones sin `Connect` → Q-01)

```sql
SELECT Number   FROM Guild where G_Name='%s'
SELECT G_Master FROM Guild where G_Master='%s'
INSERT INTO Guild (G_Name, G_Master) VALUES ('%s', '%s')
DELETE FROM Guild WHERE G_Name='%s'
UPDATE Guild SET G_Mark=?   where G_Name='%s'
UPDATE Guild SET G_Notice='%s' WHERE G_Name='%s'
UPDATE Guild SET G_Score=%d WHERE G_Name='%s'
SELECT G_Name, G_Level FROM GuildMember where Name='%s'
INSERT GuildMember (Name, G_Name, G_Level) VALUES ('%s','%s',1)
DELETE FROM GuildMember where Name='%s' AND G_Name='%s'
DELETE FROM GuildMember where G_Name='%s'
```

### Bodega, OptionData, procedimientos

```sql
SELECT AccountID FROM warehouse WHERE AccountID='%s'
INSERT INTO warehouse (AccountID, Money, EndUseDate, DbVersion) VALUES ('%s',0, getdate(), 1)
UPDATE warehouse SET Money=%d where AccountID='%s'

SELECT * FROM OptionData WHERE Name='%s'
INSERT INTO OptionData (Name, GameOption, Qkey, Wkey, Ekey, ChatWindow) VALUES ('%s', %d,%d,%d,%d,%d)
UPDATE OptionData SET GameOption=%d, Qkey=%d, Wkey=%d, Ekey=%d, ChatWindow=%d where Name='%s'
UPDATE OptionData SET SkillKey=? where Name='%s'

EXECUTE MX_CreateCharacter '%s','%s',%d,%d,%d,%d, %d,%d,%d,%d,%d,%f,%f,%f,%f,%d,%d,%d,%d
EXECUTE MX_SelectCharacter '%s'
EXECUTE MX_OPENwarehouse '%s'
EXECUTE MX_SELECTmoney '%s'
EXECUTE MX_UPDATEmoney %d,%d,'%s'
EXECUTE MX_CLOSEwarehouse ?,'%s'
SP_CHARACTER_TRANSFER '%s'
WZ_GetItemSerial
```

## Esquema deducido

El DataServer no crea tablas; las asume. Del SQL se deduce el mínimo:

| Tabla | Columnas que se usan |
| --- | --- |
| `AccountCharacter` | `Id`, `GameID1..5`, `GameIDC` |
| `Character` | `Name`, `AccountID`, `cLevel`, `Class`, `LevelUpPoint`, `Experience`, `Strength`, `Dexterity`, `Vitality`, `Energy`, `Money`, `Life`, `MaxLife`, `Mana`, `MaxMana`, `MapNumber`, `MapPosX/Y`, `MapDir`, `PkCount`, `PkLevel`, `PkTime`, `DbVersion`, `Inventory` (blob), `MagicList` (blob), `Quest` (blob) |
| `vCharacterPreview` (vista) | `Name`, `cLevel`, `Class`, `CtlCode`, `DbVersion`, `Inventory` |
| `GameServerInfo` | `Number`, `ItemCount` |
| `warehouse` | `AccountID`, `Money`, `EndUseDate`, `DbVersion`, `Items` (blob, vía SP) |
| `OptionData` | `Name`, `GameOption`, `Qkey`, `Wkey`, `Ekey`, `ChatWindow`, `SkillKey` (blob) |
| `Guild` | `G_Name`, `G_Master`, `G_Mark` (blob), `G_Notice`, `G_Score`, `Number` |
| `GuildMember` | `Name`, `G_Name`, `G_Level` |

Los `MX_*` y `SP_CHARACTER_TRANSFER` son del CustomDB del repack; su cuerpo no está en el binario
(*seam*). La verificación dinámica confirmó el esquema de `MuOnline` en la VM.

## `CQuery`: la capa ODBC

Clase `thiscall` sin vtable (ctor `0x4019f0`). Lo que hay que conservar:

- **Buffer de resultado:** 100 columnas × 255 bytes (`SQL_C_CHAR`); los nombres de columna, 100 × 50.
  Un `SELECT` que devuelva más de 100 columnas o celdas de más de 255 bytes se trunca.
- **Búsqueda de columna por nombre:** `FindCol` (`0x401f50`) usa `_stricmp` (sin distinguir
  mayúsculas).
- **Códigos de los getters** (hay que mapearlos, no confundirlos con datos):

  | Código | Significa |
  | --- | --- |
  | `-100` | la celda es `NULL` |
  | `-101` | el `SELECT` no devolvió filas |
  | `-102` | índice de columna fuera de rango |
  | `-103` | el `Exec` falló |

  Por eso un `ItemCount` de `-103`/`-101` viaja al GS como si fuera un número ([05](05-protocolo.md)).
- **Blobs:** `ExecBlobRead` (`0x402210`) y `ExecParam` (`0x402360`) mueven los binarios en trozos de
  **10000 bytes** (`SQLGetData`/`SQLPutData`). Los `?` de las SQL son estos parámetros.
- **Reconexión automática:** `Diag` (`0x402120`) mira el SQLSTATE; si es `08S01` (link caído),
  reconecta y reintenta. Mientras reconecta, el lock global sigue tomado, así que **congela el
  servidor entero** ([03](03-hilos-y-concurrencia.md)).

## Inyección SQL

**Análisis.** Casi todas las SQL arman la cadena con `%s` sin escapar (`ExecQueryf` =
`vsprintf` + `ExecQuery`). Un nombre o Id con comilla simple rompe o inyecta. En el modelo de
amenaza del original esto no importa: el único cliente es el GameServer, que valida los nombres
antes (y el propio DS corre los filtros de [07](07-datos-auxiliares.md)). Pero es deuda que MuRet
no debe heredar: los blobs ya van por parámetro (`?`); el resto tiene que ir igual.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| `CQuery` con ODBC crudo y buffers fijos | wrapper ODBC con RAII (handles que se liberan solos), detrás de interfaces de repositorio | 6 |
| 8 conexiones + 2 muertas, credenciales hardcodeadas | cadena de conexión en config; pool chico; nada de conexiones sin usar | 6 |
| `%s` sin escapar | **todo** parametrizado (`?`), nunca concatenado | 6 |
| Códigos `-100..-103` mezclados con datos | `std::optional` / `expected`: ausencia y error distintos del valor | 6 |
| Reconexión `08S01` con el lock tomado | reconexión fuera del camino crítico; la red no se congela | 6 |
| SQL Server solo en la VM | tests unitarios con repo en memoria; tests de integración contra la VM desde la ola 6, fuera de CI | 5, 6 |

Tests que fija: parseo de los códigos de `CQuery`; que cada SQL se emite verbatim (comparación de
texto) para cada head; round-trip de blobs grandes (> 10000 bytes) en trozos; integración contra la
VM (ola 6).
