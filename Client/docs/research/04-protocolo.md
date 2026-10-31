# 04. Protocolo

Fuentes: `analysis/mainexe/functions/paso3-protocolo.md`, `paso5-protocolo-handlers.md` (la
tabla de los 141 opcodes, con su evidencia), `paso5c` a `paso5e` (sub-handlers),
`analysis/mainexe/evidence/dispatch-map.json` y `mu-client/docs/component-net-envio.md`.

## El dispatcher `0x4e2b10`

`FUN_004e2b10(op, pkt, len, flag)` es un `switch` de tres niveles en **una sola función** (~10,5 KB
de casos):

1. **Nivel 1, el opcode** (0x00 a 0xF4): mapa de bytes estático `0x4E5D90[245]` → tabla de saltos
   `0x4E5B58`. 245 opcodes: **140 handlers distintos** y 104 opcodes que caen al default
   `0x4E5B4A`. (El conteo viejo de 141 cuenta dos veces el default.)
2. **Nivel 2, `pkt[3]`**, para los opcodes con subcódigo (`0xF1`): mapa `0x4E5EA8[19]` → tabla
   `0x4E5E88`.
3. **Nivel 3, `pkt[4]`**: mapa `0x4E5F04[211]` → tabla `0x4E5EBC[18]`.

- El opcode está en `pkt[2]` en C1/C3 y en `pkt[3]` en C2/C4; `pkt` es el paquete entero, con
  cabecera.
- Los opcodes `0xF3` y `0xF4` toman su subcódigo de `pkt[3]` si el paquete es C1 y de `pkt[4]` si
  es C2.
- Muchos handlers siguen el patrón "objeto": `obj = GetObjectByType(t)` (`0x65ee60`) y
  `obj->Method()` (`0x65fe50`). El juego enruta opcodes a sistemas.
- El cuarto argumento (`flag`) lo pasa FoxAnti; algunos handlers (0x19, 0x29, 0x1E, 0x73, 0x30,
  0x36, 0x1C) solo responden si vale 0 (*inferido*: "no interceptado").

Verificado: los 25 opcodes que aparecen en el cable real tienen handler propio en el nivel 1
(`s35-red/README.md`). La **semántica** de cada handler es *estática* o *inferida*: está en la
tabla de `paso5-protocolo-handlers.md`, con una columna C/I por fila.

## El flujo de conexión

```text
Cliente                         ConnectServer (44405)              GameServer (55901)
  │ connect ───────────────────────►│
  │◄──────────────────── saludo ... │  opcode 0x00 (bytes: tomar del cable)
  │ C1 04 F4 02 ───────────────────►│  pedir lista de servidores        (0x4c6070)
  │◄──────────────── C2 .. F4 06 .. │  lista: cantidad + entradas       (0x4c1860)
  │ C1 06 F4 03 <id> ──────────────►│  pedir IP y puerto del GS elegido
  │◄──────────────── C1 .. F4 03 .. │  IP + puerto: el cliente corta con el CS
  │ connect ─────────────────────────────────────────────────────────►│  seriales a 0 (0x4c1340)
  │◄─────────────────────────────────────────────────── C1 .. F1 00 ..│  saludo + versión
  │ C3: F1 01 cuenta/clave/tick/versión/serial ──────────────────────►│  login
  │◄─────────────────────────────────────────────────── C1 .. F1 01 ..│  resultado
  │ ... lista de personajes, crear, seleccionar (F3 xx) ...            │
  │ C3: 0E keepalive cada 20 s ──────────────────────────────────────►│
```

- **Opcode `0x00`** (`0x4e33e4`): solo si la escena es 2, loguea `"Send Request Server List"` y
  manda `C1 04 F4 02` (*estático*, reconstruido en `mu-client` con y sin FoxAnti).
- **`F4`** (`0x4e32f2`): sub 3 → loguea, cierra el socket del CS (`0x4e8380`) y conecta al GS
  (`0x4c1340`); sub 5 → `0x4c1bf0`; sub 6 → `0x4c1860`, llena la tabla de servidores
  (`0x7d36bc4`, cantidad en `pkt+5`). *Estático.* El layout exacto de cada entrada se toma del
  cable al implementar (ver [08](08-pendientes.md)).
- **`F1 00`** (`0x4c1e20`): con `pkt[4] == 1` guarda el índice del jugador (`pkt[5..6]`) y pone el
  estado de conexión en 2; si no, muestra "Connectting error__". Valida 5 bytes de versión en
  `pkt+7` contra `0x6c8284`. *Estático.*
- **`F1 01`, login** (emisor `0x641bf0`, build `c32bbd5f…`): 49 bytes lógicos, `C1 31 F1 01`,
  cuenta de 10 bytes y clave de 10 bytes con XOR `FC CF AB` (se reinicia en cada campo),
  `tick_count` de 4 bytes little-endian, versión `10209` y serial `MULORDSVERSAO102`. Viaja como C3
  de 68 bytes. *Estático*, con dos fixtures sintéticos; la comparación con una sesión real del
  mismo build está pendiente.
- **`0x0E`, keepalive** (`0x4bee60` y la rama `WM_TIMER` de `0x4af4e0`): `C1 0C 0E 00 <tick:4>
  <x:2> <y:2>`, como C3 de 24 bytes. Las coordenadas son las del personaje, con -20 o -25 según
  dos bits de estado. Verificado el ritmo (cada 20 s en el cable) y el layout *estático*.
- **Selección de personaje (`F3`)**: `0x4e2f93` es un sub-dispatcher de hasta 0x40 casos. Los
  sub-handlers están anotados, pero **la selección de personaje no está reconstruida ni
  verificada** (ver [08](08-pendientes.md)).

## Una sesión real de juego (2026-10-30)

[EmaNLJN/mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
(rama `feature/gameserver-passive-capture-20261030` @ `c399c70e`, sin mergear) trae un PCAP de una
conexión completa al GS del laboratorio (puerto 55901, 3,5 minutos): login, selección de
personaje, caminar, cambios de mapa a Noria y Atlans 2, recoger items, atacar monstruos y mover y
equipar items. Las acciones están anotadas por el usuario, sin la hora exacta de cada una.

Leído con un script de una página (ordenar los segmentos por secuencia TCP, deshacer el XOR
`0xAA` de FoxAnti en el sentido S->C y cortar con la gramática de [03](03-red-y-cifrado.md)):

| Sentido | Bytes | Paquetes | Corte |
| --- | ---: | ---: | --- |
| S->C | 5.571 | 553 | sin sobrantes, con el XOR `0xAA` deshecho |
| C->S | 1.297 | 129 | sin sobrantes, tal cual (C1 con la ofuscación de FoxAnti, C3 cifrados) |

- **S->C, en orden:** `F1 00` (saludo del GS, `pkt[4] = 01`), `F1 01 01` (login aceptado),
  `F3 00` (lista de personajes), `0D`, `F3 11`, `26`, `27`, `03`, los primeros C3/C4, `A0`…
  Opcodes más frecuentes: `D3` ×229, `18` ×46, `D7` ×46, `27` ×40, `26` ×34, `13` (C2) ×31,
  `2A` ×24, `14` ×23, `F3` ×19, C3 ×16, `0F` ×11, `20` (C2) ×8, `21` (C2) ×6.
- **C->S, en orden:** un **C3 de 68 bytes** (el login `F1 01`) y un **C3 de 24 bytes** (el
  keepalive `0x0E`), justo los tamaños que predice el contrato estático; después `C1 04 F3 ..`
  (pedir la lista de personajes), más C3 y una racha de `C1 .. A3` y `C1 .. A4`: el movimiento
  `D3`/`D7` remapeado por FoxAnti a `A3`/`A4`. Opcodes: `A3` ×59, C3 ×23, `18` ×17, `F3` ×11,
  `A9` ×10, `00` ×5, `A4` ×4.

Esto **verifica en el cable** el corte, la capa `0xAA`, el remapeo de movimiento y los tamaños del
login y del keepalive del build actual. No verifica sus bytes internos (hace falta descifrar los
C3 con la clave del servidor) ni qué paquete corresponde a qué acción. Falta también el
ConnectServer: esta sesión es solo la conexión al GS.

## Movimiento y mundo

| Opcode | Handler | Qué hace |
| --- | --- | --- |
| `0xD3` | `0x4c6790` | posición de un jugador: celda X/Y en `+0x41c`/`+0x420`, dirección en `pkt[7] >> 4` |
| `0xD7` | `0x4caae0` | posición y dirección de un jugador (bit 15 del id como bandera) |
| `0x18` | `0x4cb750` | acción o animación de un personaje remoto, según `pkt[6]` |
| `0x22` / `0x27` | `0x4d0cc0` / `0x4d3820` | crean objetos de 0xF08 bytes desde una plantilla (el 0x27 además con un XOR) |
| `0x0D` | `0x4c65b0` | avisos del servidor: chat, banner, notificación |
| `0x02` | `0x4e34ea` | mensaje privado: nombre de 10 bytes en `+3`, texto de 60 en `+0xD` |

Los 140 handlers tocan estas estructuras (`paso5-protocolo-handlers.md`, "Estructuras de datos
recurrentes"): la tabla de jugadores (400 entradas de 0x438 bytes en `DAT_0743a16c`, id de 16 bits
big-endian), el personaje propio (0xF08 bytes en `DAT_07681eb4`), la cola circular de 10 paquetes
de 0x100 bytes, el historial de chat y la máquina de acciones diferidas.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Un `switch` de 10 KB con tres niveles de tablas | `Protocol::Dispatcher`: tabla opcode → handler, con subdispatchers por opcode; handlers en un archivo por dominio | CL4, CL9 |
| Handlers que escriben globales del juego | handlers que traducen bytes a eventos/comandos del `GameLogic`; no tocan render ni sockets (regla de capas) | CL4, CL9 |
| Constantes del build esparcidas | versión `10209`, serial `MULORDSVERSAO102` y XOR `FC CF AB` en un solo archivo de constantes del protocolo | CL2 |
| El dispatcher solo se alcanza por FoxAnti | el dispatcher es parte del cliente; la ofuscación es una capa aparte | CL2 |
| 104 opcodes al default | el default loguea el opcode desconocido una vez (sirve para descubrir lo que falta) | CL4 |

Tests que fija:

- `C1 04 F4 02` exacto al recibir el saludo del CS en la escena de login (CL4).
- El login `F1 01` byte a byte contra los fixtures de `mu-client` (CL2) y, cuando exista, contra
  una captura del mismo build (CL4).
- El keepalive con los cinco fixtures de `mu-client` (CL2).
- Integración: un CS y un GS falsos en el test, el cliente llega a "login aceptado" (CL4).
