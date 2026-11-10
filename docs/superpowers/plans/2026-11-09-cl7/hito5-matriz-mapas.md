> Informe escrito el 2026-11-09 contra ramas que ya no existen (`refs/pr/183`, #184, #185). Sirve como lista de tareas y de lecturas; antes de usar un dato, verificalo contra `main`, el ASM y el research (`Client/docs/research/`). Lo que ya está hecho y lo que falta, en [el traspaso](../2026-11-10-cl7-traspaso.md).

# CL7, hito 5: matriz de todos los mapas (2026-11-09)

Relevamiento de un subagente, solo lectura. Base: `Client/assets/Data`, el plan de la CL7, el research 05/06/08, `refs/pr/183` (checkpoint WIP) y la rama de integración (PR #184, marcada **[HEAD]**). **[183]** marca lo que existe solo en el checkpoint.

## Inventario

- 20 carpetas `World<N>` y 19 `Object<N>`. Faltan `Object6`, `World36` y `Object36`.
- `Gate.bmd`: 125 registros. Son 38 entradas (flag 1), 38 llegadas (flag 2) y 49 destinos de `/move` (flag 0), y todos caen en los mapas 0 a 36.
- Los tipos del `.obj` sin modelo coinciden exactamente con la tabla del research 05.

## Matriz: datos, familia y gates

`obj` = objetos del `.obj` (tipos distintos). `.att` = bytes por celda. `t/14` = tiles cargados de `0x61a560`. `p` = cantidad de `TileGrass*.OZT`. E = gate de entrada → mapa (nivel). A = llegadas. mv = destinos de `/move`.

| Mapa | World / Object | Terreno | Object: bmd/tex; tipos sin modelo | Familia | Gates |
|---|---|---|---|---|---|
| 0 Lorencia | World1/Object1 | obj 2.870 (109); 2; H/L; 9/14; p1; hojas; lluvia | 115/105; ninguno | base: hojas 80, pájaros/peces, taberna | E 1→1 L20, 18→2 L15, 23→3 L10, 102→30 L10; A 4, 21, 26, 108; mv 17 |
| 1 Dungeon | World2/Object2 | 4.488 (61); 1; H/L; 9/14 | 63/29; 60 | Dungeon: Stone/Bat/Rat; modelo 40 `+0x14`=0,4 | E 3→0; internos 5, 7, 9, 11, 13, 15 |
| 2 Devias | World3/Object3 | 2.144 (97); 1; 14/14; p1; leaf01/02 | 112/102; ninguno | nieve: doble pase de hojas, techos, donkey/flag | E 20→0, 28→4 L80; A 19, 44; mv 22, 72–74 |
| 3 Noria | World4/object4 | 9.399 (44); 1; 9/14; p2 | 43/35; 43 | Butterfly, warp01..03, 322 | E 25→0 L10, 45→7 L60, 120→33 L160 |
| 4 Lost Tower | World5/Object5 | 5.380 (39); 1; 11/14 | 41/21; 40 | `0x602ed0` en tipos 3, 4, 0x13, 0x14, 0x17 | internos 30..40; 43→2; 62→10 L160 |
| 5 Exile | World6/— | .map, .att y .obj vacíos; sin H, L ni tiles | no hay Object6 | Q-C180: avisa `World6\TerrainHeight` y cierra | — |
| 6 Stadium | World7/Object7 | 1.489 (40); 1; 10/14 | 42/26; −515 | Bug01, cámara webzen | 115→34 L190, solo ida |
| 7 Atlans | World8/Object8 | 5.205 (40); 1; 9/14 | 49/64; ninguno | agua wt00..31, Fish02..09, sombra con alfa 0,2 | E 47→3, 53→8 |
| 8 Tarkan | World9/Object9 | 3.327 (77); 1; 11/14; p2 | 78/54; 3, 35, 38 | sand, Bug02, `+0xc8`=0 | E 55→7 |
| 9 Devil Square | World10/Object10 | 564 (7); 1; 9/14 | 7/13; ninguno | evento `0x447d70` | mv 58–61 |
| 10 Icarus | World11/Object11 | 670 (15); 1; 1/14 | 16/6; ninguno | sin terreno, nubes, clear color, lluvia, margen −10 | E 64→4 |
| 11–17 Blood Castle | World12/Object12 | 210 (35); 1; 10/14; p2 | 43/24; 37 | Crow01; el pasto no se dibuja por celda | mv 66–71, 80 |
| 18–23 Chaos Castle | World19/object19 | 194 (33); 1; 14/14; p3 | 36/5; ninguno | Angel, sin pasto, equipo al entrar | mv 82–87 |
| 24–29 Kalima 1–6 | World25/object25 | 2.266 (58); 1; tiles sin cargar; p2 | 59/58; ninguno | agua propia, segundo pase, clear color | mv 88–93 |
| 30 Valle de Loren | World31/object31 | 3.724 (86); 2 + EncTerrain312; luz ×2; 14/14 | 89/87; 35 | Castle Siege: celdas 0x40, niebla, estado `7239Ec` | internos 96, 98; 107→0; 117→34; 129→31 |
| 31 Land of Trials | World32/object32 | 5.638 (57); 2; 14/14 | 57/39; ninguno | Butterfly, nubes, módulo `0x43ff10` | 109→30 |
| 32 → 9 | World10/Object10 | los del 9 | los del 9 | Q-C58: carga y queda como 9 | mv 111, 112 |
| 33 Aida | World34/object34 | 2.979 (70); 2; 14/14 | 79/105; ninguno | tree_eff, Bug02, `0x425070` | 121→3; A 113, 124 |
| 34 Crywolf | World35/object35 | 6.644 (78); 2 + 351/352; luz ×2 | 84/130; 79, 80 | `BD 00`; `.att` y luz por estado (Q-C181) | 123→33; 127→30 |
| 35 | no existe | — | — | módulo `0x42ee20`; cierra sin `.att` | — |
| 36 Kalima 7 | World25/object25 | los de Kalima | los de Kalima | Kalima | mv 116 |
| 54 Selección | World55/object55 | 192 (31); 2; 14/14 | 78/89 | escena 4 | — |
| 55 Login | World56/object56 | 276 (32); 2; alturas ×3 | 83/70; 82 | escena 2, dragón | — |

## Qué está probado y qué falta

Lo genérico, en los 38 mapas con carpeta, está probado sobre `LabWorlds`: `WorldFolderNumber` para los 256 valores y los lectores de `.map`, `.att`, `.obj`, alturas y luz. También está la cadena de creación por defecto **[HEAD]**.

Lo que falta en todos (menos el 55):
- los modelos especiales de `0x616ec0`;
- las texturas fijas `drop01`, `wt`, `light01` y `rain`;
- el pase del pasto;
- la luz dinámica en la escena;
- el Step y el Draw de objetos por mapa: el componente existe **[183]**, pero la escena sigue con el código viejo.

Por mapa:
- **0:** carga golden, F3 03 real, picking, 2.870 objetos golden y los gates golden. El teleport 0→0 y 0↔2 está **[183]**. Falta la ida y vuelta por los gates 1, 18, 23 y 102 en la escena.
- **1, 2, 3:** entrada real **[183]**; hay reglas directas. Faltan los casos de familia.
- **4, 6, 7, 8, 10, 31, 33:** casi todo lo propio está sin hacer.
- **5 y 35:** falta el test del cierre por recursos ausentes.
- **9, 11–23, 32:** `0x447d70` falla a propósito, como frontera explícita.
- **24–29 y 36:** **el Render dibuja el terreno en Kalima y no debería** (solo excluye el 10). Faltan el agua y el segundo pase.
- **30 y 34:** faltan las variantes de `.att` y luz por estado, la niebla y la recarga.
- **54:** el mundo no se dibuja en la selección.

## Propuesta del test transversal

`Client/tests/scenes/test_all_maps_sweep.cpp`, con un `GoldenDataTest` parametrizado. Tres suites:
- `MapSweep`: los 39 de `OriginalMaps`.
- `GateSweep`: las 38 entradas.
- `SameMapSweep`: los gates internos de los mapas 1, 4 y 30.

Los esperados salen de `support/MapSweepCases.h` y de tablas versionadas, recalculadas desde `Gate.bmd` y los `.obj` reales. Nunca desde MuRet.

Pares de ida y vuelta: 0↔1, 0↔2, 0↔3, 0↔30, 2↔4, 3↔7, 4↔10, 7↔8, 3↔33, 30↔31 y 30↔34. Solo de ida: 6→34 y 34→33. Los mapas sin gate se ejercitan con `1C` de `/move` y con `F3 04`.

| Columna | Sin GL, con el Data real | GL en Xvfb | Evidencia nativa en la VM |
|---|---|---|---|
| Entrada | sí | primer frame no vacío | primer frame tras `0x3d` (P-C28 b) |
| Caminar, picking, colisión, altura | sí | — | cadencia del caminar |
| Cámara y recorte | sí, contra los vectores existentes y la fórmula | — | FSIN del mapa 5 y P-C32 |
| Objetos y efectos (orden y conteos) | sí | píxeles por familia, 640×480 y 1024×768 | trazas GL fuera de Lorencia y Devias |
| Gates, ida y vuelta | sí | ningún frame mientras espera | P-C31 a, b, c, i |
| Mismo mapa y otro | sí | — | P-C31 g, h |
| Recursos ausentes | sí | — | texto exacto del aviso (Q-C180) |
| Estado que se conserva | sí | — | orden de `rand()` (CL7.P) |

## Casos especiales sin código

| Familia | Caso | Dirección |
|---|---|---|
| Todos | texturas fijas (Q-C136) | `0x616ec0`, `0x61a560` |
| Todos | pase del pasto y sus exclusiones | `0x5fc260` |
| Por mapa | `switch` de modelos especiales | `0x617325`, tabla `0x61a23c` |
| Por mapa | ajustes después del lazo (mapas 1 y 8) | `0x61a126..` |
| Eventos 9, 11–23 | objetos de evento | `0x447d70` |
| Atlans 7 | agua, sin segundo pase, sombra 0,2 | `0x5fa973`, `0x5fa4b0`, `0x5fc33e`, `0x60b41a` |
| Kalima | mundo, agua, segundo pase, perturbación | `0x4377a0`, `0x41cc70`, `0x437850`, `0x41d880`, `0x41d2d0` |
| Icarus y Kalima | color de fondo | `0x637700` |
| Icarus 10 | nubes y margen −10 | `0x616ec0`, `0x6046a6` |
| Devias 2 | segundo dibujo de hojas | `0x63754c` |
| Castle Siege 30 | hook, `.att`, luz, niebla | `0x42a760`, `0x61a6ea`, `0x63741d`, `0x42b0a0` |
| Crywolf 34 | `.att`/luz por estado, agua, cámara | `0x61a710`, `0x42fe80`, `0x5fa695`, `0x4033d0` |
| Módulos de evento | Step de objetos y hojas | `0x52fe30`, `0x42b1e0`, `0x443996`, `0x4292c0`, `0x5301a0` |
| Dibujo por objeto | casos de `0x6002e0` y `0x604580` | componente **[183]** sin cablear |
| Cambios de mapa | `1C` con gate 0; macros de 30/31 | `0x53d930`, `0x61c210`, `0x61c170` |
| Recursos ausentes | aviso y cierre | `0x5f9167..0x5f91bb` |
