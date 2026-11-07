# 0010. Un inventario F3/10 incompleto devuelve TooShort

- **Estado:** aceptado en CL7.L3, lector puro del wire (2026-11-06).
- **Contrato:** [04, «El F3 10, el inventario»](../../Client/docs/research/04-protocolo.md).

## Contexto

En la rama de segundo argumento no nulo de `0x4c55a0`, el original lee la cantidad u8 en `p+5`,
recorre entradas desde `p+6` con stride 8 y pasa el slot y el puntero `entrada+1` a un callee.
No comprueba longitud: un buffer incompleto puede producir lecturas fuera de sus límites. Ese
cuerpo no lee directamente los siete bytes opacos ni permite determinar el alcance del callee.

El original realiza inicialización común antes de elegir esta rama y relee la cantidad después
de cada callee. El lector puro de MuRet opera sobre bytes vivos y estables durante la llamada;
esa precondición no representa un snapshot atómico del handler original.

## Decisión

`ReadInventoryList` devuelve `ProtocolError::TooShort`, sin lista parcial, si falta la cantidad
o si esta supera `Remaining() / InventoryEntrySize`. Comprueba esa condición antes de reservar
o leer entradas y evita multiplicaciones dependientes de una longitud ajena. Conserva los siete
bytes opacos, el orden, los duplicados y cualquier slot en un resultado dueño de sus bytes.
Ignora los bytes posteriores a las entradas contadas.

La validación del contenedor sigue la API existente: `FieldsAfterSubcode` devuelve `WrongPacket`
para otro tipo, opcode o subcódigo, y `TooShort` si falta el subcódigo. El C2 es el contenedor
reconstruido por el transporte de MuRet desde C4, según el
[ADR0003](0003-serial-de-recepcion-y-rearmado.md); no es una lectura de cabecera demostrada por
`0x4c55a0`. `WasDecrypted` no cambia el layout ni la salida de este lector.

## Consecuencias y verificación

MuRet evita accesos fuera del buffer sin inventar un fallback ni reproducir una lectura nativa
indefinida. `InventoryListTest.EveryIncompleteBodyIsTooShort` recorta un cuerpo válido en cada
posición desde la ausencia de cantidad y ajusta su cabecera para que llegue al lector como
`Packet` válido. `ACountAboveTheAvailableEntriesIsTooShort` comprueba una cantidad excesiva;
`AMissingSubcodeIsTooShort` distingue la validación previa. Los casos de trailing comprueban
que bytes no contados no se interpretan como entradas.

Esta decisión solo fija la frontera de error del lector puro. No completa el handler, el
decoder de ítems, el equipo ni CL7: admisión por flag, inicialización incluso con cantidad cero,
posibles mutaciones de callees/alias, sus efectos/RNG y aplicación siguen en L3/P.
