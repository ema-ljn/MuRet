# Tarea pendiente: el paso de los personajes en el login (`0x634d13`)

> Copia en el repo de la nota del 2026-11-10, para que la tome otro agente. Índice: [el traspaso](../2026-11-10-cl7-traspaso.md).

Estado al 2026-11-10 05:40Z: **en pausa**. Nada en el repo, ninguna rama pushada, ningún PR.

## Qué pasó

El agente que la empezó (05:20Z) leyó el orden del paso del login, cómo se arma el lugar 2 y dónde nace y se mueve el cuervo. Mientras desensamblaba la entrada del login (`0x6349a0`), un control de seguridad automático le bloqueó la respuesta y el agente se frenó. No commiteó nada. El control no dice por qué bloquea. Si se vuelve a bloquear, el agente se frena y avisa; no busca otra forma de sacar esa salida.

## El objetivo

Que el login (escena 2) corra `Scenes::StepCharacters` en `0x634d13`, en el orden del original, como ya lo hace el juego, y que `LoginScene` deje de correr por su cuenta las piezas de ese paso.

Hoy `LoginScene::Step` corre solo tres piezas, en su propio orden:
- la animación (`StepCharacterAnimation`, `0x4fcd00` → `0x4fcb10`);
- el cuervo (`StepDarkSpirit`, `0x416a20`, que el original llama desde el cuerpo del dueño);
- la cabeza (`StepCharacterHead`, con `+0x16c` calculado ahí).

Así, el login saca solo una parte de los `rand()` que saca el original en cada paso.

## Lo que ya está en main

- `0x508ba0`, el cuerpo `0x4fd5f0` (`StepTheBody`) y `0x505de0` entero (#219) para todos los `0x20a` quietos. En 06 están las secciones "Los efectos de los personajes (`0x505de0`)", "El paso de los personajes en la selección y el login" y "Lo demás del login". En 08, P-C50 y P-C54 (a).

## Lo que bloquea el cableado (P-C54 a)

1. El lugar 2 no está armado en el pool de 400 como lo arma el original (`0x6349a0` → `0x517270` → `0x511290`, con `0x512930`).
2. El cuervo vive en `LoginScene` y no en el paso del cuerpo (`0x50544b` → `0x420dd0` → `0x416a20`; nace en `0x420d60`).
3. El dibujo del login lee la animación y la cabeza de la escena, no las del personaje del pool.
4. `StepCharacters` marca el `TerrainWall` de `WorldState::Map()`, y el mapa 55 del login vive en `LoadedWorld`.

## Lo que encontró el primer agente

Todo esto es *estático* y está **sin verificar**: no llegó a escribirlo en 06 ni a probarlo con código.

- En el login, el final del gesto (`0x4f2250`) no lee Item.bmd: la rama de la mascota `0x1c16` pone antes la acción `0x4a`. Mientras `+0x3e2` sea 0, el bloqueo de Item.bmd de P-C54 (a) no aplica.
- `rand()` al entrar al login, después del logo (`0x606660`): 28 para armar el lugar 2 (`0x511290`), 1 del sufijo de `0x4f2250` y 12 al crear el cuervo.
- `rand()` por paso del lugar 2:
  - 8 del `TerrainWall`;
  - 8 de `0x508ba0`;
  - 1 del `rand() % 6`;
  - 1 de `0x432bf1`, desde el frame 3;
  - 4 a 7 del cuervo;
  - 8 de la celda;
  - 1 o 3 de la cabeza;
  - 1 del `& 7`.
- Los vectores `tests/vectors/world55/head.txt` y `raven.txt` cubren una pieza cada uno, no el paso entero.
- El orden del paso del login está en `0x634c60`.
- El lugar 2 sigue vivo en la selección: la selección (`SelectionState`, mapa 54) va a necesitar el mismo `StepCharacters` con el mundo de la escena.

## Qué hacer

1. Leer estático, con direcciones:
   - el paso del login alrededor de `0x634d13` (qué corre antes y después);
   - cómo arma `0x6349a0` el lugar 2 (`0x517270`, `0x511290`, cada `rand()` y cada campo, con el equipo: el cetro `0x620` y el cuervo `0x1c17`);
   - qué tiene el lugar 0 (el héroe) en el login y si está vivo;
   - dónde va el cuervo dentro del paso del cuerpo.

   Verificar los datos de arriba y poner 06 al día (*estático*).
2. Portar:
   - armar el personaje del login en el pool como el original, con sus `rand()`;
   - que `StepCharacters` reciba el mundo de la escena (paredes y celdas: el `LoadedWorld` del 55), pensado para que la selección (54) lo use después, sin cablear la selección;
   - llamar a `StepCharacters` desde `LoginScene::Step` en `0x634d13`, con el cuervo dentro del paso del cuerpo;
   - sacar la animación, la cabeza y el cuervo propios del login, y que el dibujo lea el personaje del pool;
   - donde falte algo, cortar con `Core::Expected` y la dirección, después de los `rand()` de antes.
3. Pruebas:
   - el presupuesto de `rand()` por paso del login, los cortes y el armado del lugar 2;
   - cada test del login (`test_login_scene`, `test_golden_login_scene`) cuyo estado de `rand()` cambie, explicado en el PR.
4. Docs:
   - Q-C286..Q-C295 reservados en 07;
   - P-C56 en 08;
   - poner al día P-C50 y P-C54 (a).

Rama: `claude/project-thread-02udvb-m1-login-step`, desde main. PR: `CL7: el paso de los personajes en el login (0x634d13)`. Reglas comunes y formato del PR: las de [el traspaso](../2026-11-10-cl7-traspaso.md#cómo-se-trabajó).

## Después

- El paso en la selección (`0x6342f9`), que depende de esta tarea:
  - armar los lugares de la lista como `0x20a`, con el `rand()` de su `0x511290`;
  - la vista previa `0xa9` (`0x634303`): la rama de los otros tipos de `0x4f9580`, la cola de `0x4fcd00`, `0x4fcee0` con otros tipos, `0x4ea190` y el caso de `0x5085db`;
  - Item.bmd para el `0x54efe0` del registro propio antes de la primera entrada.
