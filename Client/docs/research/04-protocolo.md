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

Verificado contra dos capturas del cable (ver "Cómo se verificó" abajo): el ConnectServer en el
`muproxy.log` del laboratorio (build histórico, 72 conexiones: 64 al CS y 8 al GS) y el GameServer en la sesión real
de [mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45) (build
actual).

Los bytes son los del cable, con el XOR32 y el XOR `0xAA` ya deshechos donde los hay.

```text
Cliente                              ConnectServer (44405)
  │ connect ─────────────────────────►│
  │ C1 06 A9 00 00 01 ───────────────►│  sin identificar (Q-C16); en el cable, 20 9C 2F
  │◄──────────────────── C1 04 00 01 │  saludo
  │ C1 04 F4 02 ─────────────────────►│  pedir la lista de servidores           (0x4c6070)
  │◄──── C2 00 0A F4 02 01 00 00 00 CC │  lista del laboratorio: FoxAnti la pasa a F4 06
  │ C1 06 F4 03 00 00 ───────────────►│  elegir el servidor 0 (u16 little-endian)
  │◄── C1 16 F4 03 <IP:16> <puerto:2> │  "172.16.198.1" + 0xDA5D = 55901 (little-endian)
  │ (el cliente corta con el CS)

Cliente                              GameServer (55901)
  │ connect ─────────────────────────►│  seriales a 0                            (0x4c1340)
  │◄────── C1 0C F1 00 01 <idx:2> "10209" │  saludo: resultado, índice del jugador, versión
  │ C3 68 B: F1 01 ──────────────────►│  login (49 B lógicos)
  │◄──────────────────── C1 05 F1 01 01 │  login aceptado
  │ C3 24 B: 0E ─────────────────────►│  primer keepalive, después cada 20 s
  │ C1 04 F3 00 ─────────────────────►│  pedir la lista de personajes
  │◄──────────────────── C1 29 F3 00 … │  lista de personajes
  │◄──────────────────────── C1 .. 0D … │  aviso del servidor
  │ C1 0E F3 03 <nombre:10> ─────────►│  elegir personaje
  │◄── F3 11, 26, 27, 03, C3 F3 03, C4 F3 10 … │  entrar al mundo
```

**Qué capa toca cada paquete.** *Verificado* en el cable y explicado por el código de la FoxAnti
mínima (`analysis/foxanti/minimal/src/packet.c`):

- **XOR32 (C->S):** lo aplica el motor C1/C2 de `main.exe` ([03](03-red-y-cifrado.md)). Los `F4`
  del CS salen **en claro**: `0x4c6070` arma el pedido sin el motor (*estático*) y el `F4 03` llega
  al cable con su `03` sin tocar. El `A9` del CS y todo C1 del GS sí lo llevan (también los `F3`:
  `C1 04 F3 00` viaja como `C1 04 F3 7A`).
- **XOR `0xAA` (S->C):** `decode_inbound` lo deshace **salvo** en tres casos: C1 con opcode `00`,
  C1 con opcode `F4` y C2 con opcode `F4`. Son justo los paquetes del CS, por eso el CS parece "sin
  FoxAnti". En el GS todos los segmentos S->C de la sesión real llevan el `0xAA` (398 de 398).
- **Reescrituras:** FoxAnti cambia dos paquetes S->C antes de que lleguen al dispatcher (la lista
  `F4 02` → `F4 06` y el `F3 11`, ver abajo) y, C->S, remapea opcodes y el código del `F4 03`
  ([03](03-red-y-cifrado.md), "La capa de FoxAnti").

- **Opcode `0x00` en el CS** (`0x4e33e4`): con la escena en 2, loguea `"Send Request Server List"`
  y manda `C1 04 F4 02`. Verificado: el cable muestra `C1 04 00 01` y enseguida `C1 04 F4 02`; en 8
  de las 64 conexiones del `muproxy.log` el pedido sale dos veces. En el GS el mismo opcode `00` es
  el chat.
- **La lista de servidores: `F4 02` en el cable, `F4 06` en `main.exe`.** El CS del laboratorio
  manda `C2 00 0A F4 02 | 01 | 00 00 00 CC`: cantidad de 1 byte y entradas de 4 bytes (código u16
  little-endian, carga, relleno). `decode_inbound` la reescribe (`parse_f4_02`, `0x341170`) como
  `C2 00 0B F4 06 00 01 | 90 01 00 00`: cantidad en 2 bytes, el código sumado a un delta según
  `(código + 1) / 11` (para 0 a 9 el delta es `0x190`, así que 0 pasa a 400), la carga copiada y el
  relleno en 0. La procesa el sub 6 de `0x4e32f2` (`0x4c1860`), que agrupa por `código / 20`.
  *Verificado* el cable; la traducción es *estática* (código portado y probado de la FoxAnti
  mínima). El tercer byte cambia entre listas (`00` en 71, `02` en una): es la carga. El `0xCC` es
  siempre igual y FoxAnti lo descarta (*inferido*: memoria sin inicializar del CS).
- **`F4 03`:** el cliente manda el código del servidor como u16 little-endian. `main.exe` manda
  el suyo (400, `90 01`) y `encode_outbound` le resta según una escalera de 8 rangos (400 a 419:
  menos 400), así que al cable sale `00 00`. El CS responde con la IP en 16 bytes con `\0` y el
  puerto en little-endian. El sub 3 de `0x4e32f2` loguea, cierra el socket del CS (`0x4e8380`) y
  conecta al GS (`0x4c1340`). *Verificado* el cable; la ida y vuelta de códigos desde 21 no cierra
  (Q-C17).
- **`F1 00`** (`0x4c1e20`): `C1 0C F1 00 01 <idx:2> <versión:5>`. Con `pkt[4] == 1` guarda el
  índice del jugador (`pkt[5..6]`) y valida los 5 bytes de versión de `pkt+7` contra `0x6c8284`.
  Verificado: el GS manda `"10209"` al build actual (y `"09908"` al histórico).
- **`F1 01`, login** (emisor `0x641bf0`): 49 bytes lógicos, `C1 31 F1 01`, cuenta de 10 bytes y
  clave de 10 bytes con XOR `FC CF AB` (se reinicia en cada campo), `tick_count` de 4 bytes
  little-endian, versión `10209` y serial `MULORDSVERSAO102`, todo con XOR32, como C3 de 68
  bytes. **Verificado** en la sesión real: descifrado y sin XOR32 da 49 bytes con `10209` en
  `+28` y `MULORDSVERSAO102` en `+33`. La cuenta y la clave no se imprimieron.
- **`0x0E`, keepalive** (`0x4bee60` y la rama `WM_TIMER` de `0x4af4e0`): `C1 0C 0E 00 <tick:4>
  <x:2> <y:2>`, como C3 de 24 bytes. **Verificado** el layout y el ritmo: el primero sale 48 ms
  después del login aceptado y los siguientes cada 20,0 s (±0,1) con un timer que ya venía
  corriendo, porque el segundo llega a los 7,5 s del primero. Ojo: en el
  juego `x` e `y` valen por ejemplo `0x0328` y `0x0268`: **no son celdas** del mapa (0 a 255),
  son los WORD de `personaje+0x4C/+0x58` con su ajuste de -20 o -25 (la unidad está sin leer).
- **Selección de personaje (`F3`)**: `0x4e2f93` es un sub-dispatcher de hasta 0x40 casos. En el
  cable: `F3 00` (pedir lista, sin cuerpo), `F3 03` con el nombre de 10 bytes (elegir), `F3 06`,
  `F3 12`. El layout de la respuesta `F3 00` y del resto falta reconstruirlo (P-C06).
- **`F3 11`, S->C** (`0x4c5070`): llega dos veces en la sesión (`C1 08 F3 11 FE 04 16 68` y uno
  de 20 bytes con 5 entradas de 3 bytes). FoxAnti le mete un `00` en `pkt[5]` (`reset_queue`), y
  así el handler encuentra el slot en `pkt[6]`. El handler guarda en un array de slots y los
  resuelve contra la tabla de `0x076BD1D8` con paso `0x3C`, que es `Skill.bmd` en memoria
  ([05](05-formatos-de-datos.md)): *inferido*, **es la lista de skills**, no de items como dice
  `paso5d`. Con FoxAnti, `pkt[5]` llega siempre en 0, así que la rama `pkt[5] == 2` (borrar
  todo) no se ve con el GS del laboratorio.

## Una sesión real de juego (2026-10-30)

La sesión de
[mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
(rama `feature/gameserver-passive-capture-20261030` @ `c399c70e`, sin mergear) es un PCAP de una
conexión completa al GS del laboratorio (3,5 minutos): login, selección de personaje, caminar,
cambios de mapa a Noria y Atlans 2, recoger items, atacar monstruos y mover y equipar items. Las
acciones las anotó el usuario, sin la hora exacta de cada una.

| Sentido | Bytes | Paquetes | Corte |
| --- | ---: | ---: | --- |
| S->C | 5.571 | 553 | sin sobrantes, con el XOR `0xAA` deshecho |
| C->S | 1.297 | 129 | sin sobrantes |

- **S->C**, más frecuentes: `D3` ×229 (movimiento de otros), `18` ×46, `D7` ×46, `27` ×40,
  `26` ×34, `13` (C2) ×31, `2A` ×24, `14` ×23, `F3` ×19, C3 ×16, `0F` ×11, `20` (C2) ×8,
  `21` (C2) ×6.
- **C->S en C1** (sacando solo el XOR32): `A3` ×59 y `A4` ×4 (el `D3`/`D7` de movimiento que
  FoxAnti remapea), `18` ×17 (acción o dirección), `F3` ×11, `A9` ×10 (`A9 01 00 08` las diez
  veces, sin identificar, Q-C16), `00` ×5. **El `00` del cliente es el chat:**
  `C1 .. 00 <nombre:10> <texto>`, con los comandos del servidor (`/move Lorencia`, `/addstr 10`)
  como texto.
- **C->S en C3:** los 23 descifran con el `Dec1.dat` del servidor
  (`mu-server-repack-research-102i/gameserver/evidence/keyfiles/`) con checksum bien y serial
  **0, 1, 2… 22 sin cortes**. Opcodes: `F1` (login), `0E` ×11 (keepalive), `26` ×3, `A6` ×3, `A0`,
  `03`, `23`, `22`, `24`. El `A6` es el `0x1E` que remapea `send_forward`: los tres llevan `0x29`
  como primer byte y el servidor contesta con `1E` y el mismo `0x29` (*inferido*, fuerte).
- **S->C en C3/C4:** los 17 (16 C3 y 1 C4) descifran con el `Dec2.dat` del cliente (el de `Data\`
  del laboratorio) con checksum bien y serial **0 a 16 sin cortes**. Opcodes: `F3 03` y `F3 10`
  (el C4, 302 B lógicos) al entrar al mundo, y después `1C`, `22`, `1E`, `16` y `24`.
- **El orden**, por la hora de cada segmento TCP: tras la lista de personajes llega un `0D`. Al
  `F3 03` del cliente el servidor responde en 18 ms con `F3 11`, `26`, `27`, `03`, `F3 03` (C3),
  `F3 10` (C4) y `F3 11`; el cliente manda `A0` (C3), el servidor `A0` y `F3 30`, el cliente `03`
  (C3), el servidor `13` (C2) y empieza el movimiento (`D3`, y el primer `A3` del cliente).

## Cómo se verificó

Un script de una página, con las piezas de referencia del repo de research
(`c3c4_reference.py` y la tabla XOR32 de [03](03-red-y-cifrado.md)):

1. Ordenar los segmentos TCP de cada sentido por número de secuencia y descartar repetidos.
2. En el GS, S->C: deshacer el XOR `0xAA`.
3. Cortar con la gramática C1/C2/C3/C4 (tamaño total en `pkt[1]` o `pkt[1..2]` big-endian).
4. C3 del cliente: descifrar con `Dec1.dat` del servidor (la tabla XOR de la clave sale del
   `main.exe` `c32bbd5f…`), reemplazar el serial por la cabecera C1 y deshacer el XOR32.
5. C1 del cliente: deshacer el XOR32: `plano[i] = cable[i] ^ key[i & 31] ^ cable[i - 1]` para
   `i >= 3`.
6. C3/C4 del servidor: descifrar con el `Dec2.dat` del cliente (`assets/original-data/` de
   `mu-online-reverse-lab`) y comparar el serial con el contador.
7. Lista y `F4 03`: pasar los bytes del cable por `fox_decode_inbound` y `fox_encode_outbound`
   de la FoxAnti mínima, compilada en el host.

Al descifrar hay que cortar la salida al largo que devuelve `Decrypt`: los bloques de 8 traen
relleno (el keepalive descifra 16 bytes y solo 11 son del paquete).

Lo que **no** cubre: los bytes internos de la mayoría de los paquetes de juego, qué paquete
corresponde a qué acción y el CS del build actual (el `muproxy.log` es del histórico, 64
conexiones al CS y 8 al GS; la sesión nueva solo tiene longitudes del CS). El script no imprime
ningún payload: cuenta, cabeceras y offsets conocidos.

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

- `C1 04 F4 02` exacto al recibir el saludo `C1 04 00 01` del CS en la escena de login (CL4).
- Con la capa FoxAnti, la lista del cable llega al dispatcher como `F4 06` con el código 400 y la
  elección sale al cable como `F4 03 00 00` (CL4; la capa se prueba sola en CL2).
- El intercambio del CS y del GS de arriba, byte a byte, contra un CS y un GS falsos que respondan
  con los bytes del cable (CL4).
- El login `F1 01` byte a byte contra los fixtures de `mu-client` (CL2) y, cuando exista, contra
  una captura del mismo build (CL4).
- El keepalive con los cinco fixtures de `mu-client` (CL2).
- Integración: un CS y un GS falsos en el test, el cliente llega a "login aceptado" (CL4).
