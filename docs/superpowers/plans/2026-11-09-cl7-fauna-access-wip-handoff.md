# CL7: continuación WIP del acceso de entorno Fauna

Publicado al detener el trabajo por pedido de Emanuel. Este slice conserva exactamente el
paquete ya preparado: tres archivos de FaunaEnvironmentAccess y cuatro fuentes/tests de Query
como dependencia existente de PR175. No se cambió algoritmo ni se ejecutaron nuevas pruebas
al preparar este checkpoint.

Rama: `EmaNLJN/cl7-fauna-access-wip`. Padre real de Git: rama
`EmaNLJN/cl7-pub-leaf-runtime`, commit `6a4d11739fb01042863914ddb96510f7bf89a337`.
Primero leer [el handoff del padre](2026-11-09-cl7-runtime-wip-handoff.md) y la
[guía general PR176](https://github.com/ema-ljn/MuRet/pull/176). No tratar esta rama como
independiente sobre main ni como un adapter completo de aves/peces.

## Archivos propios

- `Client/src/Scenes/FaunaStepServices.h`
- `Client/src/Scenes/FaunaStepServices.cpp`
- `Client/tests/scenes/test_fauna_step_services.cpp`

Presta WorldState, ClothWorld, WorldTerrain, luz dinámica y las entradas requeridas de modo,
flag, escena, escala y altura fija. No inventar valores por defecto, otro pool/tabla ni un
DTO rival. Destruir el servicio antes de sustituir su terreno. La vigencia de productores y
los hooks completos de fauna siguen fuera.

## Estado de validación

El export anterior aislado pasó sintaxis/headers/formato/includes y catorce tests de Query;
Main/Fauna no enlazaron por faltar el padre canónico WorldRuntime. El padre ahora se conserva
en el checkpoint, pero **esta combinación no fue compilada ni probada después del corte**.
Las cifras históricas de 51 casos/siete mutantes del paquete privado no validan esta rama.

El siguiente agente debe construir Main y `test_fauna_step_services`, comprobar las firmas
contra el padre y ejecutar sus diez casos junto con Query/luz/mundo afectados. Si aparece un
conflicto, documentar el campo/API exactos; no añadir stubs ni forzar verde. Las cuatro fuentes
Query son idénticas a PR175: deduplicarlas al integrarlo, sin crear otra implementación FTOL.

```sh
cmake --preset linux-x64-debug -DMURET_WARNINGS_AS_ERRORS=ON -DMURET_PRECOMPILED_HEADERS=OFF
cmake --build --preset linux-x64-debug --target Main test_fauna_step_services test_terrain_attribute_query -j1
ctest --preset linux-x64-debug -R 'FaunaStepServices|FaunaEnvironmentAccess|TerrainAttributeQuery' --output-on-failure --no-tests=error
```

Consultar los nombres efectivos mediante CTest; el filtro es orientativo y no acredita
cobertura si no recoge los casos. Completar revisión/estilo/integración por unidad antes de
retirar DRAFT. No es cierre CL7, Main gameplay ni equivalencia universal con el original.
Los WTs y catálogos privados se conservaron; no se exportan sus contenidos ni hay workers
activos al terminar el handoff.
