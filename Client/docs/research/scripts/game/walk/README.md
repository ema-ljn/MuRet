# Caminar (CL7.4)

El camino, el paquete y el paso del héroe con el código real (06 § Caminar). El `.att` de Lorencia es el
del `Data\`; el cable, el de `MURET_CLIENT_WIRE_DIR`. Todos necesitan `MURET_MAIN_EXE`.

| Script | Qué reproduce | Necesita además | Sub-ola |
| --- | --- | --- | --- |
| `emu_path.py` | `PathFinding2` (`0x4eac00`) con el A* de `0x4ead70`, sobre un mapa de paredes en `0x7d0d348`. `PathEmu` es el arnés de los demás: arma el PATH (`0x4eaaa0`, `0x4eaaf0`) y da el camino de hasta 15 celdas | el `Data\` | CL7.4 A (`make_path_vectors.py`) |
| `path_limits.py` | cada pasada de `0x4ead70` sola en un mapa vacío: los topes de 500 (exacta) y 50 (la más cercana), y el corte en 15 celdas | — | CL7.4 A (`TheExactSearchGivesUpAfter500AndTheNearestAfter50`, `OnlyTheFirstFifteenCellsAreKept`) |
| `path_edges.py` | los bordes: el pasillo cortado por personajes dentro y fuera de la zona segura, la meta ocupada (`0x02`, `0x22`, `0x24`), clics sobre paredes de Lorencia | el `Data\` | CL7.4 A (`InTheSafeZoneTheSecondTryWalksThroughCharacters`, `AnOccupiedGoalEndsOnTheDiagonalByTheTieOrder`) |
| `heuristic.py` | la h del A*, el tramo `0x4eb2a1..0x4eb301` y el `sar` de `0x4eb301`: h(1, 1) = h(1, 0) = 4 (Q-C159) | — | CL7.4 A (`TheDiagonalNextToTheGoalCostsLikeTheSide`) |
| `wire_vs_path.py` | cada `A3` del cable contra `PathFinding2` emulado entre las mismas celdas: 169 iguales y 2 distintos (una celda ocupada que el cable no muestra) | el `Data\`, el cable | CL7.4 A (`TheWireWalksMatch`) |
| `emu_send.py` | `SendMove` (`0x56acd0`) con `send` enganchado (`stub_overrides` de `ClothEmulator`): los bytes del `D3`, 15 celdas que salen como 14, la dirección por el ángulo, el blanco al lado del final, `MouseUpdateTimeMax` (`0x6c8dcc`) | — | CL7.4 B (`make_walk_packet_vectors.py`) |
| `emu_walk.py` | el paso del héroe: `0x4f37c0` (`SetPlayerWalk`), `0x4ea270` (`MovePath`) y `0x508a80` (`MoveCharacterPosition`, con `0x508940` y `0x5f93c0`), sobre el `.att` y las alturas | el `Data\` | CL7.4 C (`make_movement_vectors.py`) |

Desde la raíz del repo, con la carpeta delante (`Client/docs/research/scripts/game/walk/`):

```bash
python -I emu_path.py
python -I path_limits.py
python -I path_edges.py
python -I heuristic.py
python -I wire_vs_path.py 1            # el mundo (world1) y, opcional, otro cable
python -I emu_send.py
python -I emu_walk.py
```

Los nombres entre paréntesis son los generadores y los tests que el plan pide: cada uno sale de copiar el
arnés de su script y barrer los casos al azar, como `../../login/make_head_vectors.py`. El picking del
terreno (CL7.4 D) no tiene script todavía: `0x5fc260(1)`, `0x622440` y `0x622170` se emulan con el
mismo `ClothEmulator`. Lo que dice el cable del caminar (los `A3` y el eco de los `D3`) está en
`../wire/walk_stats.py`.
