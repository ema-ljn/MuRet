# Plan de la ola 5: Handlers en memoria

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** los primeros heads de verdad, con la base reemplazada por repositorios en memoria.
Al final de la ola un GameServer de prueba se loguea, recibe broadcasts y hace el camino de
entrar al juego: lista de personajes, crear uno y cargarlo.

**Arquitectura:** interfaces de repositorio en `Database/` (cuenta, personaje, bodega, gremios,
info del servidor) con una implementación en memoria. Los handlers en `Handlers/` reciben los
repositorios por constructor (inyección de dependencias), así en la ola 6 se cambia la base sin
tocarlos. Los datos auxiliares (stats por clase, tabla de items, filtros de nombres) se cargan
al arrancar.

**Stack:** C++20 (interfaces con `virtual` puro, `std::unique_ptr`), GoogleTest (fixtures).

**Research que fija esta ola:** [05-protocolo](../../../Server/DataServer/docs/research/05-protocolo.md)
(`0x00`, `0x02`, `0x40`, `0x01`, `0x04`, `0x06`),
[07-datos-auxiliares](../../../Server/DataServer/docs/research/07-datos-auxiliares.md) (stats,
blobs iniciales, items, filtros, XOR) y Q-05, Q-08 de
[08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md).

**Terminada cuando:** test de integración con dos clientes falsos contra el repositorio en memoria:
los dos se loguean como GS (`0x00`), uno manda un broadcast (`0x02`) y lo reciben los dos; uno pide
la lista (`0x01`), crea un personaje (`0x04`) y lo carga (`0x06`).

## Preguntas para decidir al empezar

1. **Tipo de error.** C++20 no tiene `std::expected` (llega en C++23). ¿Usás `tl::expected` por
   vcpkg, escribís un `Result<T>` chico, o `std::optional` + código de error? Lo que importa es que
   "no existe", "falló" y "el valor" no se confundan, como pasa con los `-100..-103` del original.
2. **Granularidad de los repositorios.** ¿Una interfaz por tabla (`AccountCharacter`, `Character`,
   …) o por dominio (cuenta, personaje, bodega, gremios)? Pista: las transacciones de la ola 7.
3. **Capturas del wire.** La respuesta de `0x06` (0x3BC bytes) y la lista de `0x01` tienen partes
   *seam* ([10-pendientes](../../../Server/DataServer/docs/research/10-pendientes.md), H-1, H-4,
   H-9). ¿Las capturás en la VM antes de la 5.3, o escribís los tests con el layout del research y
   los marcás como pendientes de golden?

---

## Sub-ola 5.1: interfaces de repositorio y versión en memoria

**Concepto:** interfaz (clase con métodos `virtual` puros), por qué un handler no tiene que saber
si hay SQL Server atrás, inyección de dependencias y tests sin base.

**Archivos:**
- Crear: `Server/DataServer/src/Database/IAccountRepository.h`, `ICharacterRepository.h`, `IWarehouseRepository.h`, `IGuildRepository.h`, `IServerInfoRepository.h`
- Crear: `Server/DataServer/src/Database/Memory/` (una implementación por interfaz)
- Crear: `Server/DataServer/tests/database/RepositoryContractTests.cpp`

- [ ] **Paso 1 (Emanuel):** las interfaces, con los métodos que piden los heads de esta ola (mirá las SQL de [06](../../../Server/DataServer/docs/research/06-sql-y-base.md#catálogo-de-sql-verbatim) para ver qué operaciones hacen falta, no para copiarlas).
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** tests de **contrato** escritos contra la interfaz, no contra la implementación (en la ola 6 corren igual contra ODBC): crear cuenta, buscarla, slots `GameID1..5`, primer slot libre (y que "falló" no sea "slot 0", Q-08).
- [ ] **Paso 3 (Emanuel):** las implementaciones en memoria con `std::unordered_map`.

## Sub-ola 5.2: heads `0x00`, `0x02` y `0x40`

**Concepto:** el handler como función pura de (request, estado) → (respuestas, cambios); cómo se
testea sin red.

**Archivos:**
- Crear: `Server/DataServer/src/Handlers/GameServerHandlers.h`, `GameServerHandlers.cpp`
- Crear: `Server/DataServer/src/Database/Memory/GuildCache.h`, `GuildCache.cpp` (caché de gremios en RAM)
- Crear: `Server/DataServer/tests/handlers/GameServerHandlersTests.cpp`, `tests/integration/LoginBroadcastTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests de `0x00`: el login marca la sesión como GameServer y responde `C1 08 00` con result 1 e `ItemCount`; un segundo login en la misma sesión falla. El `ItemCount` sale de `IServerInfoRepository`, leído **al arrancar** como el original ([05 § 0x00](../../../Server/DataServer/docs/research/05-protocolo.md#0x00--login-del-gameserver)).
- [ ] **Paso 2 (Emanuel):** tests de `0x02`: el mismo paquete llega a todas las sesiones activas. Decidí qué hacer con un C2 (el original usa `byte[1]` como largo y lo corta mal).
- [ ] **Paso 3 (Emanuel):** tests de `0x40`: con la caché vacía salen los dos marcadores `C1 08 41` (sub 0 con count 0, sub 1); con dos gremios sale un `C1 40 40` por gremio entre los marcadores. Corre en el hilo de lógica, sin hilo nuevo (C-7).
- [ ] **Paso 4 (Emanuel):** implementar los tres handlers y registrarlos en el `Dispatcher`.
- [ ] **Paso 5:** test de integración: dos clientes, login y broadcast.

## Sub-ola 5.3: cuenta y personajes contra memoria

**Concepto:** flujos con varios pasos y qué pasa si uno falla a la mitad; datos como datos
(tablas cargadas de archivos, no `switch` en el código).

**Archivos:**
- Crear: `Server/DataServer/src/Core/GameData/ClassStats.h/.cpp`, `ItemTable.h/.cpp`, `NameFilters.h/.cpp`
- Crear: `Server/DataServer/src/Protocol/CharacterPackets.h/.cpp`, `Server/DataServer/src/Handlers/CharacterHandlers.h/.cpp`
- Crear: `Server/DataServer/tests/handlers/CharacterHandlersTests.cpp`, `tests/gamedata/` y archivos de ejemplo en `tests/data/`

- [ ] **Paso 1 (Emanuel):** cargadores de datos con tests sobre archivos de ejemplo chicos: stats por clase (los valores de [07](../../../Server/DataServer/docs/research/07-datos-auxiliares.md#stats-de-inicio-por-clase), con MaxLife en `+0x14`), tabla de items (`code = type*32 + idx`), filtros de nombres (badsyntax por subcadena, reservados exactos).
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** tests de `0x01`: cuenta nueva se crea; la lista saltea los previews que fallan **sin dejar hueco**; el u16 de `+0xE` vuelve en la respuesta.
- [ ] **Paso 3 (Emanuel):** tests de `0x04`: nombre inválido → result 0; sin slot → result 2; clase > 3 → Dark Knight con log; los blobs iniciales (Inventory 0x2F8, MagicList 0x3C, Quest 0x32) solo si la creación anduvo.
- [ ] **Paso 4 (Emanuel):** tests de `0x06`: descifra el nombre (XOR `FC CF AB`, round-trip), responde siempre `C2 03BC 06`, y además manda el paquete de opciones `C1 24 60` si las hay.
- [ ] **Paso 5 (Emanuel):** implementar los tres handlers.
- [ ] **Paso 6:** cerrar o marcar los *seam* según lo que decidiste en la pregunta 3.

---

## Al cerrar la ola 5

- [ ] Nota de aprendizaje `docs/learning/ola-5-handlers.md` (interfaces, inyección de dependencias).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la ola 6.
