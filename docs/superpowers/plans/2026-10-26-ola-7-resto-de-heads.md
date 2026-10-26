# Plan de la ola 7: Resto de heads

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** implementar los heads que faltan, incluidos los gremios, que en el original nunca
funcionaron (Q-01). Al final están los 22 heads del contrato.

**Arquitectura:** lo mismo de las olas 5 y 6: un archivo de handlers por dominio, repositorios
inyectados, tests de contrato contra memoria y contra la VM. Las respuestas se comparan byte a byte
con capturas del original donde el research tiene *seam*.

**Stack:** C++20, ODBC (ola 6), transacciones de SQL Server.

**Research que fija esta ola:** [05-protocolo](../../../Server/DataServer/docs/research/05-protocolo.md)
(`0x05`, `0x07`, `0x11`, `0xCF`, `0x08`, `0x09`, `0x12`, `0x20`, `0x52`, `0x60`, `0x30`–`0x38`,
`0x40`), [06-sql-y-base](../../../Server/DataServer/docs/research/06-sql-y-base.md),
[07 § caché de gremios](../../../Server/DataServer/docs/research/07-datos-auxiliares.md#caché-de-gremios-en-ram)
y Q-01, Q-08 de [08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md). *Seam* a
cerrar: H-2 (bodega) y H-6 (cuerpos de los `MX_*`) de
[10](../../../Server/DataServer/docs/research/10-pendientes.md).

**Terminada cuando:** los 22 heads están implementados y testeados contra memoria y contra la VM,
y los gremios funcionan de punta a punta (crear, agregar miembro, sync con `0x40`, borrar).

## Preguntas para decidir al empezar

1. **Cambios de wire.** Arreglar un quirk a veces cambia bytes que el GS ve: `0x05` responde 1
   aunque el `DELETE` falle; los gremios responden result 3/4 porque la base no está conectada.
   ¿Cuáles se arreglan y cuáles se dejan iguales por compatibilidad? Cada cambio de wire lleva un
   ADR.
2. **Gremios y el GS real.** ¿El GS 1.02 se comporta bien con gremios que **sí** funcionan? Nunca
   se probó, porque el original siempre falló. ¿Hace falta un flag `guilds.compat_broken`?
3. **Transacciones.** El original no usa: crear gremio hace 4 sentencias y deshace a mano si falla
   una. ¿Dónde hace falta una transacción de verdad?

---

## Sub-ola 7.1: cuenta y personajes (resto)

**Concepto:** efectos que van después de responder (la cascada de `0x05`), escalas de unidades
(vida y maná ×10 en el wire, ×0,1 al guardar), y paridad byte a byte con una captura.

**Archivos:**
- Modificar: `Server/DataServer/src/Handlers/CharacterHandlers.*`, `Protocol/CharacterPackets.*`
- Crear: `Server/DataServer/tests/handlers/CharacterHandlersTests.cpp` (casos nuevos), `tests/data/golden/` (capturas)

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests de `0x05`: result 0 solo con cuenta o nombre vacíos; la cascada de gremio (flag 1 borra el gremio, 2 quita al miembro) corre **después** de responder.
- [ ] **Paso 2 (Emanuel):** tests de `0x07`: guarda stats con la escala correcta y los tres blobs **solo** si el `UPDATE` anduvo; nombre vacío se rechaza con log.
- [ ] **Paso 3 (Emanuel):** `0x11` (inventario) y `0xCF` (`SP_CHARACTER_TRANSFER` sin `EXECUTE`; si el SP no existe responde 1, como en la base del laboratorio).
- [ ] **Paso 4 (Emanuel):** buscar el primer `GameID` libre sin devolver 0 cuando la consulta falla (Q-08).
- [ ] **Paso 5:** paridad de `0x01`, `0x04`, `0x06` contra las capturas.

## Sub-ola 7.2: bodega y varios

**Concepto:** respuestas que cambian de head según el caso (`0x08` responde con head `0x10` si la
bodega es nueva), procedimientos almacenados con blobs, y atomicidad del serial de items.

**Archivos:**
- Crear: `Server/DataServer/src/Handlers/WarehouseHandlers.h/.cpp`, `MiscHandlers.h/.cpp`, `Protocol/WarehousePackets.h/.cpp`
- Crear: `Server/DataServer/tests/handlers/WarehouseHandlersTests.cpp`, `MiscHandlersTests.cpp`

- [ ] **Paso 1 (Emanuel):** capturar el wire de abrir una bodega en la VM (cierra H-2).
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** tests de `0x08`: bodega nueva → `C1 10 10`; existente → `C2 04C8 08` con dinero, items (1200 bytes) y versión. `0x09` y `0x12` guardan dinero y items.
- [ ] **Paso 3 (Emanuel):** tests de `0x20` (actualiza `ItemCount`; pensá si el valor en memoria del login `0x00` también se actualiza), `0x52` (`WZ_GetItemSerial`; sin fila no responde) y `0x60` (opciones, insert o update, `SkillKey` como blob).
- [ ] **Paso 4 (Emanuel):** implementar.
- [ ] **Paso 5 (Emanuel, ejercicio):** ¿qué pasa si dos GameServers piden un serial a la vez? ¿Dónde está la atomicidad: en MuRet o en el procedimiento?

## Sub-ola 7.3: gremios

**Concepto:** arreglar un subsistema que nunca funcionó; caché en RAM consistente con la base;
el sync de `0x40` sin locks cruzados (C-1).

**Archivos:**
- Crear: `Server/DataServer/src/Handlers/GuildHandlers.h/.cpp`, `Protocol/GuildPackets.h/.cpp`
- Modificar: `Server/DataServer/src/Database/Memory/GuildCache.*` (carga desde la base al arrancar)
- Crear: `docs/adr/0005-gremios-funcionando.md`
- Crear: `Server/DataServer/tests/handlers/GuildHandlersTests.cpp`

- [ ] **Paso 1 (Emanuel):** ADR 0005: qué cambia en el wire respecto del original y por qué.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** tests: crear gremio (validación de nombre silenciosa como el original, `INSERT Guild`, mark, número, master con nivel 1, alta en caché, broadcast); agregar y quitar miembro; borrar; score y aviso; `0x40` devuelve los gremios de la caché.
- [ ] **Paso 3 (Emanuel):** cargar la caché desde la base al arrancar (el original no tiene loader).
- [ ] **Paso 4 (Emanuel):** implementar en el hilo de lógica, sin locks de gremios.
- [ ] **Paso 5:** prueba manual con el GS real en la VM, si la ola 8.1 ya está.

---

## Al cerrar la ola 7

- [ ] Nota de aprendizaje `docs/learning/ola-7-heads.md`.
- [ ] Actualizar `docs/learning/PROGRESS.md` y el research con lo que aprendiste de las capturas.
- [ ] Revisar el plan de la ola 8.
