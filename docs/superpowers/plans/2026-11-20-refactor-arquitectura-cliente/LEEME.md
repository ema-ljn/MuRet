# Anexo del plan de la arquitectura del cliente

Las tablas que salieron de medir `Client/src` en `main` `bb56680` (2026-11-20), para el
[plan](../2026-11-20-refactor-arquitectura-cliente.md) y la
[spec](../../specs/2026-11-20-arquitectura-cliente-design.md). Son una foto: el mapa vivo pasa a
`Client/docs/research/coverage/mapa-modulos.tsv` en AR0.1, y desde ahí lo mantienen los PRs. Las
direcciones del original que aparecían en las razones se reemplazaron por `(dir.)`, para que la
referencia no las cuente como citas de docs.

| archivo | filas | qué es |
| --- | ---: | --- |
| `mapa-modulos.tsv` | 1.418 | **cada archivo de `Client/src` y su módulo de destino** (`engine/Core`, `game/Guild`…), con su confianza y la razón. Los de `Scenes/` salen del análisis del grafo de includes, los nombres y lo que hace cada archivo; los de `UI/`, `World/`, `GameLogic/` y `App/`, de reglas por nombre (confianza media). 50 tienen confianza baja: el grafo los tira hacia otro módulo y hay que mirarlos a mano (AR0.1, Task 2). **Revisado el 2026-11-21**: 486 filas cambiaron de destino al pasar de 31 a 58 módulos (`engine/Crt`, `Crypto`, `Diagnostics`, `Pathfinding`, `Obfuscation`; `game/Tables`, `Maps`, `Sound`, `Assets`, `Terrain`, `Camera`, `Movement`, `Skills`, `Tooltips`, `Messenger`, `PersonalShop`, `Party`, `Quests`, `ChaosMix`, `Warehouse`, `PetTrainer`, `Events/<Evento>`, `MapScripts/<Mapa>`, `Fauna`, `Weather`, `Particles`, `Joints`, `FrontEnd`); la razón de cada una dice "(revisión 2026-11-21)" |
| `scenes-modulos-candidatos.tsv` | 26 | (de la primera versión, con los nombres de entonces) los 26 módulos candidatos de `Scenes/`: capa propuesta, líneas, archivos, olas, de quién depende y quién depende de él, y qué campos de `WorldRuntime` (con `*` los que escribe, aproximado) y qué accesores de `WorldState` usa |
| `scenes-ciclos.tsv` | 100 | (de la primera versión) los pares de módulos de `Scenes/` que se incluyen mutuamente, y si siguen mutuos después de bajar los headers de estado y el kit de la UI (48 sí) |
| `interfaces-y-bolsas.tsv` | 132 | las interfaces virtuales, las bolsas de callbacks y las de parámetros: métodos, callbacks, referencias y punteros, implementaciones en `src` y en tests, y cuántos archivos las usan |

Cómo se midió (scripts en el scratchpad de la sesión, no en el repo): el grafo de `#include "…"`
que resuelven a un archivo de `src`; lizard para el tamaño y la complejidad de las 8.800 funciones;
jscpd 4.3 y un detector propio por tokens (ventanas de 50, con los literales normalizados) para la
repetición. Los números de la spec salen de ahí.

La revisión del 2026-11-21 (más módulos, pedida por Emanuel) se hizo con reglas por nombre y carpeta sobre el mapa de
la primera versión; con el mapa nuevo, suben de capa 365 includes en 83 pares de módulos (210 archivos) y hay 80 pares
mutuos entre módulos del juego de la misma capa.
