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
  **Contradicción abierta para el `0x1C`:** [Los cambios de
  mapa](#los-cambios-de-mapa-1c-f3-04-y-f3-12) dice lo contrario, que con el argumento en 0 no lo
  atiende y manda `F1 03`. Las dos son *inferidas* y no se elige ninguna: se resuelve contra el ASM
  antes de que el hito 2 admita el `1C` (P-C33 en [08](08-pendientes.md)).

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
  │ C1 06 A9 00 00 01 ───────────────►│  la info de la mascota (Q-C16); en el cable, 20 9C 2F
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

- **`A9`, C->S: pedir la información de una mascota** (Q-C16, CL4.R). `C1 06 A9 <tipo>
  <lugar> <casilla>`, con XOR32. Lo arman tres emisores en línea:
  - `0x415be0` manda `A9 00 00 01` siempre (`0x416040`..`0x416300`), al armar el objeto del cuervo
    oscuro de un personaje (`0x4167b0`, desde `0x420cf0` y `0x420d60`). Solo `0x420d60` mira si en
    `+0x2d8` está el modelo `0x1c17` (`0x420d7c`; el ítem `0x1a05` más `0x212`, *inferido*);
    `0x420cf0` lo arma salvo en los mapas 18 a 23 (`DAT_006c9dd0`, `0x420d1a`..`0x420d22`). La
    preparación de la escena de login (`0x6349a0`) conecta al ConnectServer (`0x6349eb`), le pone al
    personaje de la escena un equipo fijo con el cuervo (`0x517270`, desde `0x634a43`; `0x51734b`
    escribe el `0x1c17`) y llama a `0x420d60` (`0x634a9d`): como el `connect` es bloqueante, el `A9`
    sale por el socket del CS, recién abierto, antes de que se despache el saludo. *Estático* el
    camino; *verificado* que sale primero en las 64 conexiones al CS del `muproxy.log`, siempre
    `00 00 01` sin el XOR32, y seguido del saludo y del `F4 02`.
  - `0x424230` manda `A9 01 00 08` desde la lógica de la UI del juego (`0x5ee140`, que corre desde
    la escena 5, `0x636ee1`), una vez mientras la bandera de panel `0x2000000` esté prendida: la
    pide con `0x659290(0x2000000)`, la consulta del estado de los paneles de la UI (364 llamadas,
    un bit por panel), que lee `DAT_07d53709`; la escriben solo el sistema de paneles (`0x6599a0`
    la pone en 1 en `0x65c95f` y `0x657ab0` en 0 en `0x657af9`). `DAT_00723911` recuerda que ya se
    pidió (`0x4246dd`) y vuelve a 0 cuando el panel se cierra. No es del anticheat. Es la ventana del
    personaje (C), la que abre la tecla C (06 § HUD y ventanas). *Verificado* en el cable: 9 en el `muproxy.log` y 10 en la sesión real, todos
    `01 00 08` y todos después de entrar al mundo.
  - `0x422990` manda el de un ítem `0x1a04` o `0x1a05` cuando cambia el que se mira (lo llama la UI,
    `0x5b2b0d`), con el lugar y la casilla que recibe.

  La respuesta S->C es el handler `A9` (`0x4e4ca1` → `0x421db0`): guarda por `pkt[3]` (el tipo) el
  nivel de `pkt[6]` y un DWORD de `pkt[8]` en la tabla de `0x723960`, y recalcula los atributos
  (`0x550d10`, [06](06-render-escenas-y-ui.md#los-stats-del-registro-propio)). Por todo eso es *inferido* (alto) que el `A9` pide la información de la mascota,
  como la llama OpenMU: el tipo 0 es el cuervo oscuro (`0x1a05`, el ítem 13, 5) y el 1 el caballo
  oscuro (`0x1a04`, el 13, 4); el lugar 0 es el inventario, y las casillas 1 y 8 son la mano
  izquierda, donde va el cuervo, y la de la mascota, donde va el caballo. En el cable, después
  del `A9` el CS manda solo el saludo; que el saludo no sea su respuesta, porque el CS lo manda
  al aceptar, es *inferido*.
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
  La cantidad va en **big-endian**: `0x4c1860` la arma como `pkt[5] * 0x100 + pkt[6]`; de cada
  entrada lee el código como u16 little-endian y la carga del byte que sigue, y el cuarto no lo
  lee (*estático*). No compara la cantidad con el tamaño del paquete: con una que no entra, lee
  después del final. MuRet devuelve `TooShort` (Q-C42, CL2.5).
  *Verificado* el cable; la traducción es *estática* (código portado y probado de la FoxAnti
  mínima). El tercer byte cambia entre listas (`00` en 71, `02` en una): es la carga. El `0xCC` es
  siempre igual y FoxAnti lo descarta (*inferido*: memoria sin inicializar del CS).
- **`F4 03`:** el cliente manda el código del servidor como u16 little-endian. `main.exe` manda
  el suyo (400, `90 01`) y `encode_outbound` le resta según una escalera de 8 rangos (400 a 419:
  menos 400), así que al cable sale `00 00`. El CS responde con la IP en 16 bytes con `\0` y el
  puerto en little-endian. El sub 3 de `0x4e32f2` loguea, cierra el socket del CS (`0x4e8380`) y
  conecta al GS (`0x4c1340`). *Verificado* el cable; la ida y vuelta de códigos desde 21 no cierra
  (Q-C17). De la IP se leen 15 bytes: el dispatcher (`0x4e3341..0x4e3378`) copia `pkt[4..18]` a
  un buffer de 16 bytes en 0 y le pasa eso y el puerto (`pkt[20..21]`) a `0x4c1340`, así que el
  byte 16, `pkt[19]`, no se lee nunca, y una IP de 16 caracteres sin `\0` queda en 15. En el cable
  del laboratorio, las 8 respuestas traen `pkt[19]` en 0. MuRet lee lo mismo (CL2.5).
- **`F4 05`:** la otra respuesta al `F4 03`, la de un servidor ocupado. El sub 5 de `0x4e2b10`
  llama a `0x4c1bf0` sin argumentos, que arma un `C1 04 F4 ..` como el pedido de la lista de
  `0x4c6070`, pero sin mirar la escena: vuelve a pedir la lista, y el jugador elige de nuevo
  (*estático* el salto; que el byte del subcódigo sea `02` es *inferido*, del decompilado y de
  `ReceiveServerConnectBusy` de MuMain, que manda el pedido de la lista). Nada del paquete se lee:
  MuMain le da un byte, el servidor. No aparece en el cable del laboratorio. MuRet vuelve a pedir
  la lista como el original desde la CL5.5 (Q-C71); en la CL4, con la elección automática, fallaba.
- **`F1 00`** (`0x4c1e20`): `C1 0C F1 00 01 <idx:2> <versión:5>`. Con `pkt[4] == 1` guarda el
  índice del jugador y valida los 5 bytes de versión de `pkt+7` contra `0x6c8284`. El índice va en
  **big-endian**: lo arma como `pkt[5] * 0x100 + pkt[6]` (*estático*), igual que el `NumberH` y el
  `NumberL` de los paquetes del GS. Así, `00 05` es el jugador 5, y no el 1280.
  Verificado: el GS manda `"10209"` al build actual (y `"09908"` al histórico).
  El índice va al DWORD `0x57678bc` (`0x4c1ec0`), con `0x57678c0 = 2` justo antes (`0x4c1eb6`) y
  **antes** del lazo de la versión: queda guardado aunque la versión no coincida. Es la clave del
  héroe: `0x4c3940` la copia a su `+0x1f4` y `0x510f70` la compara al cargar un mapa
  ([06](06-render-escenas-y-ui.md#la-entrada-al-mundo)). Con `0x5767928` distinto de 0 la escribe
  sin mirar `pkt[4]` (`0x4c1e3e`), llama a `0x412bb0` y `0x447ae0` y sale sin validar la versión
  (*estático*, T7). MuRet la guarda en `ServerSession::PlayerIndex` con el `F1 00` aceptado, también
  antes del chequeo de versión.
- **`F1 01`, login** (emisor `0x641bf0`): 49 bytes lógicos, `C1 31 F1 01`, cuenta de 10 bytes y
  clave de 10 bytes con XOR `FC CF AB` (se reinicia en cada campo), `tick_count` de 4 bytes
  little-endian, versión `10209` y serial `MULORDSVERSAO102`, todo con XOR32, como C3 de 68
  bytes. **Verificado** en la sesión real: descifrado y sin XOR32 da 49 bytes con `10209` en
  `+28` y `MULORDSVERSAO102` en `+33`. La cuenta y la clave no se imprimieron. En el
  `muproxy.log`, del build histórico, los 7 login traen `09908` en el mismo lugar (CL2.5).
- **`0x0E`, keepalive** (`0x4bee60` y la rama `WM_TIMER` de `0x4af4e0`): `C1 0C 0E 00 <tick:4>
  <velocidad de ataque:2> <velocidad de magia:2>`, como C3 de 24 bytes. **Verificado** el layout y
  el ritmo: el primero sale 48 ms después del login aceptado y los siguientes cada 20,0 s (±0,1)
  con un timer que ya venía corriendo, porque el segundo llega a los 7,5 s del primero. Los dos
  WORD valen en el juego, por ejemplo, `0x0328` y `0x0268`: **no son celdas** del mapa (0 a 255),
  sino los WORD de `personaje+0x4C/+0x58` con su ajuste de -20 o -25. Del lado del GS son **la
  velocidad de ataque y la de magia**: en el GS original, `CGLiveClient` (`0x415140`) compara el
  de `+8` y el de `+0xa` con dos campos del objeto del jugador (`0x415291` y `0x4152ac`) y, si no
  pasan, el log de `0x41532b` dice "探测到客户端有黑客工具 : 编辑攻击速度" (detectó una
  herramienta de hack: velocidad de ataque editada), con los cuatro valores (*estático*). Qué
  compara no está decidido: `!=`, o un `<` con o sin signo (`repack_patches.tsv`, "predicado no
  decidido"). El GS del repack la tiene anulada: `0x41529b` es `EB 19` (era `75 1F`) y `0x4152b6`
  es un `JMP` (era un `JE`), así que siempre va al else y no mira las velocidades. La
  reconstrucción del GS lo llama `PMSG_CLIENTTIME { Time; AttackSpeed; MagicSpeed; }`. El byte 3
  es el relleno que deja el alineado del struct, y el cliente lo manda en 0.
- **Quién manda el keepalive y cuándo** (CL4.R, *estático* en el desensamblado salvo lo que dice
  *verificado*). Hay dos emisores, y los dos miran solo `DAT_05767918`:
  - **El timer.** `WinMain` llama `SetTimer(ventana, 1000, 20000, NULL)` en `0x4bad89`, después de
    poner la primera escena (`0x632d80`) y antes del bucle de mensajes, y el binario no importa
    `KillTimer`: corre desde el arranque hasta que se cierra. La rama `WM_TIMER` del WndProc resta
    `0x3e8` al id y salta por la tabla de `0x4b8c5f`; el 1000 va a `0x4af945`, que llama a
    `thunk_FUN_00450376` (`0x450350`, que no hace nada: abajo) y, si `DAT_05767918` no es 0
    (`0x4af94a`), arma y manda el keepalive en línea (`0x4af958`..`0x4b1a3e`). No mira el login,
    ni el foco, ni la escena.
  - **El primero.** El sub 1 de `F1` con resultado 1 pone `DAT_057678c0 = 0x14` y `DAT_05767928 =
    2` y llama a `0x4ad100` (`0x4e2bbd`), que llama a `0x4bee60` si `DAT_05763570` es 0. Ese byte
    solo lo escribe `0x4ad070` (`0x4ad085`), el camino con que el anticheat muestra un error y
    cierra el cliente, así que con el cliente andando siempre es 0. Lo que sigue en `0x4ad100`, los
    `FindWindowA` con `DAT_05767928` en 2 o más, es del anticheat: fuera de alcance.
  - **`DAT_05767918` es "hay conexión al GameServer".** Lo pone en 0 cada socket nuevo de la
    conexión principal (`0x4e82e0` con 1, `0x4e8305`) y cada cierre de ella (`0x4e8380`,
    `0x4e8395`), y en 1 un `0x4c1340` que no dio 0 desde el `F4 03` (`0x4e33a2`) o desde el sub 0
    de `B1` (`0x412b92`, la otra dirección de GameServer). También lo pone en 1 la copia del
    handler sin llamadores (`0x4c1bab`) y en 0 `0x4e8520`, que tampoco tiene. Con el 2 de un nombre
    que no resuelve queda en 1 sin conexión (Q-C51).
  - **`thunk_FUN_00450376`** es `mov eax,[0x5763540]`, `test eax,eax` y un `jmp` (`EB 1D`, en
    `0x450357`) a su propio `ret`: no hace nada. El código que saltea compara lo que da `0x6866c0`
    con `0x6bd888` y, si no coincide, llama a `0x4ad070`: es del anticheat, no se leyó más. Que el
    `jmp` es un parche, como el de Q-C34, es *inferido*.
  - **Verificado en el cable**, con los C3 del `muproxy.log` descifrados y mirando solo el opcode,
    el serial y las velocidades: en 2 de las 8 conexiones al GS el primer C3 es un keepalive con
    serial 0, mandado 2 y 4 s antes que el login (que sale con serial 1); y en una conexión sin
    login hay 34 keepalives, 32 de sus 33 intervalos de 20 s (el log tiene la hora al segundo). El
    timer manda con el GS conectado, haya login o no, y al CS nunca: desde el CS, `0x4c1340` no lo
    pone en 1 (Q-C53).
  - **Las velocidades antes de que haya personaje.** `WinMain` llena de ceros el personaje propio
    (`0x4bafbc`..`0x4bafc2`, `0xF08` bytes en `[0x7681eb4]`) y deja `[0x7681eb0]` apuntando a él
    (`0x4bafc9`), y `0x4bee60` lee de ahí `+0x3c` (los bits del ajuste), `+0x4c` y `+0x58`. Hasta
    que llega el personaje, las dos velocidades salen en 0, sin ajuste. *Verificado*: el primer
    keepalive de las 8 conexiones al GS lleva 0 y 0, y el byte 3 en 0. Con el personaje, `0x4bee60`
    lee las tres del registro propio cada vez que arma uno, entre la apertura del registro
    (`0x40b240`, `0x40b440`) y su cierre (`0x40b4d0`). MuRet las toma de
    `WorldState::OwnCharacter()` antes de cada `Poll` de la sesión (`main.cpp`, desde 2026-11-11:
    antes iban siempre en 0); el `rand()` del cierre del registro y el del relleno no los saca, como
    los del `F3 03` y el `F3 10` (Q-C211).
- **`F1 02`, la salida** (`0x4c2710`, desde `0x4e2d80`): el GS contesta `C1 05 F1 02
  <resultado>` como C3 (`GCCloseMsgSend`, `0x417280` en la reconstrucción del GS) cuando termina la
  cuenta de 5 s de `gObjCloseSet` (`CloseCount` en 6, solo con el jugador logueado). Con 0 cierra
  la conexión enseguida (`CloseClient`), con 1 manda solo si `gObjGameClose` dio 1, y con 2 no
  cierra. En el cliente, con 0 el juego se cierra sin aviso (`SendMessageA(WM_DESTROY)`,
  `0x4c391b`; Q-C106), con 1 vuelve a la selección y con 2 a la lista, solo si llegó cifrado
  (Q-C79), y con otro no hace nada. La cuenta la empieza el pedido del propio cliente desde el menú
  del juego (`CGClientCloseMsg`, con el mismo valor), o el GS solo: con 0 cuando echa a un jugador
  (`gObjUserKill`), y con 2 con la respuesta del DataServer a la transferencia del personaje
  (`DGMoveOtherServer`). Un GS que se apaga o se cae no la manda: solo cierra cada socket
  (`gObjAllDisconnect`; el cierre programado avisa antes con mensajes del servidor), y el cliente
  ve el `FD_CLOSE` ([03](03-red-y-cifrado.md#sockets)). *Estático*.
  **Con 1** (CL7 hito 1, T14; *estático*): cifrado, limpia el juego, pasa a la escena 4 y manda, en
  este orden, el `F3 00` (`C1 04 F3 00`, cifrado, `0x4c3653`), un `C1 03 31` (`0x419900`, desde
  `0x4c2480`) y otro `C1 03 31` (`0x4499b0`, desde `0x657670`, si la caja de `0x723c80` está vacía;
  Q-C214). Sin cifrar no limpia ni cambia la escena: solo manda `C1 06 F1 03 00 F1` en C3, como el
  `F3 03` y el `F3 10` en claro (Q-C211). Los pasos, en
  [06](06-render-escenas-y-ui.md#la-vuelta-a-la-selección).
- **`C1 03 31`, C->S** (`0x419900` en `0x419a0e`, `0x4499b0` en `0x449ad1`): tres bytes, sin
  cuerpo, en claro y sin el XOR32. `0x419900` pone en 0 `+0x21383` de `[0x710148]` antes de
  mandarlo, y `0x4499b0` sale sin mandar si la caja de `0x723c80` tiene algo (`0x449c10`) y, si no,
  pone en 0 `0x724580` y `0x724584`. En el GS reconstruido es `CGCloseWindow`
  (`protocol.cpp:443`, `0x42ab20`): MuRet lo llama `WindowClose`. Los llaman también otros lugares de la interfaz (`0x44b5da`,
  `0x44b9ce`, `0x44c62e` y `0x658dfd` a `0x4499b0`), que MuRet no tiene.
- **La suma del ejecutable (`03`)**, S->C y C->S (`0x4e2b10` → `0x4e16c0`; *estático*, ASM
  c32bbd5f, y del lado del GS la reconstrucción del research del servidor). Al entrar al mundo
  (`gObjSetCharacter`, con el `F3 03`) el GS manda `C1 06 03 00 <clave:2>` (`GCCheckMainExeKeySend`):
  sortea una tabla de 0 a 1.023, la esconde en los bits 0..3 y 6..15 con seis bits de `rand()` en el
  medio, la cifra con `0xB479` y anota la hora (`CheckSumTime`). **Si en tres segundos no llega la
  respuesta, `gObjSecondProc` cierra la conexión** (`GetTickCount() − CheckSumTime > 3000`, mirado una
  vez por segundo): el cliente entra al mundo y a los 3 o 4 s el GS lo corta. El cliente, sin guarda
  de escena ni del cifrado:
  1. lee la clave de `pkt[4..5]` sin mirar el tamaño (`0x4e1846`) y saca la tabla (`0x4ad3d0`:
     `c = (k ^ 0xB479) & 0xFFFF`, tabla `= ((c >> 6) ^ c) & 0xF ^ (c >> 6)`);
  2. pide la línea de comandos (`GetCommandLineA`, `0x4ad4d6`) y saca el nombre del ejecutable
     (`0x4ad270`): lo que sigue a la última barra invertida de **toda** la línea, cortado en el primer
     espacio, comilla, barra o barra invertida;
  3. prueba abrir ese nombre, relativo a la carpeta actual, **para escribir** (`0x4ad51b`). Si puede,
     arma otro paquete, con un `F1` (la protección, fuera de alcance); un ejecutable en uso no se
     abre así en Windows. Si no, lo abre para leer (`0x4aec13`), lo lee entero y lo suma (`0x4ad400`):
     sin abrirlo, la suma es 0 (`0x4aec28`);
  4. `0x4ad400(imagen, tamaño, tabla)`: `s = tabla << 9` y, de a cuatro bytes mientras el offset no
     pase de tamaño − 4 (sin signo), con `w` el u32 y `q` su índice: según `(tabla + q) % 3`,
     `s ^= w`, `s += w` o `s = (s << (w % 11)) ^ w`; y siempre `s ^= (s + tabla) >> ((q & 15) + 3)`;
  5. contesta `C1 08 03 00 <suma:4>` con el XOR32 y como C3 (en la PCAP, el `03` del cliente va en C3,
     [Una sesión real de juego](#una-sesión-real-de-juego-2026-10-30)).

  El GS (`CGCheckMainRecv`) sólo compara la suma con sus `CheckSum*.dat` si el cliente viene de
  `218.234.76.33`; desde cualquier otra dirección acepta lo que llegue y pone `CheckSumTime` en 0. En
  MuRet: `Protocol::Connection::MainCheck` y `ServerSession::OnMainCheck`, con el nombre que
  `main.cpp` saca de `argv` unido con espacios (Q-C319).
- **Selección de personaje (`F3`)**: `0x4e2f93` es un sub-dispatcher de hasta 0x40 casos. En el
  cable: `F3 00` (pedir lista, sin cuerpo), `F3 03` con el nombre de 10 bytes (elegir), `F3 06`,
  `F3 12`. Los subcódigos 0 a 3, campo por campo, están abajo, en
  [La selección de personaje](#la-selección-de-personaje-f3) (CL6.R; P-C06 cerrado).
- **`F3 11`, S->C** (`0x4c5070`): llega dos veces en la sesión (`C1 08 F3 11 FE 04 16 68` y uno
  de 20 bytes con 5 entradas de 3 bytes). FoxAnti le mete un `00` en `pkt[5]` (`reset_queue`), y
  así el handler encuentra el slot en `pkt[6]`. El handler guarda en un array de slots y los
  resuelve contra la tabla de `0x076BD1D8` con paso `0x3C`, que es `Skill.bmd` en memoria
  ([05](05-formatos-de-datos.md)): *inferido*, **es la lista de skills**, no de items como dice
  `paso5d`. Con FoxAnti, `pkt[5]` llega siempre en 0, así que la rama `pkt[5] == 2` (borrar
  todo) no se ve con el GS del laboratorio.

### La selección de personaje (`F3`)

Leída en la CL6.R en el desensamblado (*estático*) y comparada con la sesión real de abajo solo
por el largo, el tipo y el subcódigo: los tres que están en el cable coinciden. `0x4e2f93` toma el
subcódigo de `pkt[3]` (C1) o de `pkt[4]` (C2): el 0 va a `0x4c20a0`, el 1 a `0x4c2290`, el 2 se
lee en línea y el 3 va a `0x4c3940`. Los C->S salen con el XOR32 desde el byte 3.

| Paquete | Dónde | Forma | Estado de la conexión | En el cable |
| --- | --- | --- | --- | --- |
| `F3 00` C->S, pedir la lista | `0x634e0e`, en la lógica del login (`0x634c60`) con el estado en `0x14` | C1, 4 bytes, sin cuerpo | pasa a `0x32` y a la escena 4 | coincide |
| la lista S->C | `0x4c20a0` | C1, 7 + `0x22` por personaje | `0x33` | coincide (41 = 7 + 34) |
| `F3 01` C->S, crear | `0x63d990` | C1, 15: el nombre hasta su primer cero y con ceros hasta 10, y un byte con la clase de la vista previa por 16 más su piel (que nadie escribe: 0) | `0x34` | no está |
| su respuesta | `0x4c2290` | `pkt[4]`: 1, creado (con el nombre en `pkt[5..14]`, el lugar en `pkt[0xf]`, el nivel en `pkt[0x10..0x11]` y la clase en `pkt[0x12]`); 0, el aviso `0x36`; 2, el `0x37`; otro, nada (Q-C114) | `0x35` con 1; con 0 o 2 queda en `0x34` | no está |
| `F3 02` C->S, borrar | `0x643740` | C1, 24: el nombre del elegido (hasta su primer cero y con ceros hasta 10) y los 10 bytes del campo de texto de la ventana (`0x7798bf8`), como vienen | `0x38` | no está |
| su respuesta | en línea (`0x4e2b10`, el caso del subcódigo 2) | `pkt[4]`: 0, el aviso `0x92`; 1, el `0x60`, que borra el elegido; 3, el `0x62`; otro, el `0x89` | queda en `0x38` | no está |
| `F3 03` C->S, entrar | `0x636290`, el primer paso de la escena 5 | C1, 14: el nombre en 10 bytes | `0x3c` | coincide |
| su respuesta | `0x4c3940` (1.289 líneas) | C3 de `0x34` bytes: los campos, en [La entrada al mundo](#la-entrada-al-mundo-la-respuesta-del-f3-03-y-el-f3-10); pone el mundo (`0x61a560`), el personaje propio y el estado `0x3d` (`0x4c4854`) | `0x3d` | coincide (52 bytes, con `ff ff` en `0x32`) |

- **La lista** (`0x4c20a0`): `pkt[4]` va a `0x622b20` (la clase más alta que se puede crear:
  `0x63d7c0` habilita los botones de la ventana de crear de la 0 a ella, *estático*, Q-C119), los
  dos números de `pkt[5]` al texto 1240, y `pkt[6]` es la cantidad (con 0, el estado `0x33` y
  nada más). Desde `pkt[7]`, una entrada de `0x22` bytes por personaje: `[0]` el lugar; `[1..10]` el nombre (10 bytes, sin terminador); `[0xc..0xd]` el
  nivel (un `word`, a `+0x1d6`; *inferido*: la selección lo copia al `+0xe` del personaje propio,
  que es el nivel); `[0xe]` las banderas (a `+0x1d8`; con `0x1`, bloqueado); `[0xf]` la clase
  (`((b & 0x10) | (b >> 4)) >> 1`: la clase y la segunda clase en el bit 3); `[0x10..0x20]` el
  equipo (`0x5139f0`, [06](06-render-escenas-y-ui.md#la-selección-de-personaje)); y `[0x21]`, a
  `+0x368`, el estado en el gremio (*inferido*: `0xff` por defecto en `0x511ca6` y `0x415d56`, y
  comparado con `0x80` en `0x4148aa` y `0x4c64f5`). No mira el largo del paquete, y un lugar de
  más de 4 deja al personaje sin posición (Q-C101).
- **Crear** (`0x4c2290`): arma al nuevo con la clase de la vista previa, la pisa con la del
  paquete y no le pone equipo (Q-C103). Los avisos llevan un texto por índice: `0x36` el 1716 y
  `0x37` el 396 (los dos vuelven a abrir la ventana de crear), y `0x5d` el 471 (sin botón, después
  de mandar el `F3 01`). Antes de mandarlo, `0x63d990` mira el nombre del campo de la ventana
  (`0x7798bf8`): con menos de 4 bytes, el aviso `0x79` (390); con una palabra prohibida, el `0x72`
  (1715): `0x622c70` busca las de `FilterName.bmd` y las de `Filter.bmd` (estas, si el nombre no
  empieza con `/`, `0x622b40`) en el nombre sin espacios, y unas cadenas fijas y los textos 457 y 458
  en el nombre tal cual, con `0x576960` (Q-C117); y con un byte malo, el `0x7a` (391, `0x447ca0`,
  parchado con NOPs en este binario: mira solo los bytes pares, y uno es malo si es menor que
  `0x21`, está de `0x7f` a `0xaf` o es mayor que `0xf7`, también los que quedaron de un intento
  anterior, Q-C118). El detalle, en
  [06](06-render-escenas-y-ui.md#la-ventana-de-crear). El campo tiene un tope de 10 bytes, así que
  un nombre más largo no llega desde la UI; si llegara, el original no completa con ceros y lo manda
  entero. El byte de la clase es `DAT_07437224 · 16 + DAT_07437225`, la clase
  y la piel de la vista previa (`0x7437050 + 0x1d4` y `+ 0x1d5`): la clase la pone el botón de cada
  una (`0x63e3e0`, con el modelo `0xa8` + clase, el `NewFace` de esa clase) y la piel no la escribe
  nadie. Con 1, `0x4c2290` arma al nuevo con `0x517270(lugar, la clase y la piel de la vista previa,
  x, y, ángulo)`, con el lugar y el ángulo de cada uno de los cinco, y le escribe el nivel, la clase
  del paquete, el nombre y su terminador; cierra la ventana de crear (`0x65f6c0`).
- **Borrar**: el campo de 10 bytes de la ventana sale como está. En otras versiones es un código
  personal de la cuenta, así que se trata como la clave (nunca en el log). El aviso `0x60` (1714)
  borra `chars[DAT_006d5edc]`, el elegido al llegar la respuesta (Q-C104); `0x92` lleva el 1654,
  `0x62` el 439 y `0x89` el 401.
- **Entrar**: con el personaje bloqueado, el aviso `0x84` (434) en vez de entrar
  ([06](06-render-escenas-y-ui.md#la-selección-de-personaje)). Con `DAT_07d366bc` en 1 (nadie lo
  escribe) no se manda el `F3 03`.

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

- **S->C**, más frecuentes: `D3` ×229 (el movimiento de otros y el eco del propio), `18` ×46,
  `D7` ×46 (el daño), `27` ×40, `26` ×34, `13` (C2) ×31, `2A` ×24, `14` ×23, `F3` ×19, C3 ×16,
  `0F` ×11, `20` (C2) ×8, `21` (C2) ×6.
- **C->S en C1** (sacando solo el XOR32): `A3` ×59 y `A4` ×4 (el `D3` de caminar y el `D7` de
  pegar, que FoxAnti remapea: [03](03-red-y-cifrado.md#la-capa-de-foxanti) y [Caminar y
  pegar](#caminar-y-pegar-d3-y-d7)), `18` ×17 (acción o dirección), `F3` ×11, `A9` ×10 (`A9 01
  00 08` las diez veces: la info del caballo oscuro, *inferido* alto, Q-C16), `00` ×5. **El `00`
  del cliente es el chat:** `C1 .. 00 <nombre:10> <texto>`, con los comandos del servidor (`/move
  Lorencia`, `/addstr 10`) como texto.
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
  **El `A0` no es una respuesta**: lo manda el mismo primer paso de la escena 5 que el `F3 03`,
  después de armar la UI, si el byte `[0x710148] + 4` vale `0xff` (`0x63693a`, *estático*). En la
  PCAP sale 21 ms después del `F3 03` y 3 ms después de que llegó la respuesta; en el
  `muproxy.log`, que ordena por llegada al proxy, sale antes de ella (*verificado* los dos). Antes
  este documento lo daba como algo que el cliente manda después de la respuesta.

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

Lo del mundo se leyó en el barrido de Lorencia (2026-11-05), en el desensamblado y contra el
`muproxy.log` y la PCAP de arriba, sin copiar nombres ni chat. Lo que hace el juego con cada paquete
está en [06](06-render-escenas-y-ui.md#la-escena-del-juego).

| Opcode | Sentido | Handler o emisor | Qué es |
| --- | --- | --- | --- |
| `F3 03` | S->C (C3) | `0x4c3940` | la entrada al mundo (abajo) |
| `F3 10` | S->C (C4) | `0x4c55a0`, desde `0x4e309c` | el inventario, con entradas de 8 bytes (abajo) |
| `0xD3` | C->S | `0x56acd0` (`SendMove`) | caminar; FoxAnti lo manda como `A3` (abajo) |
| `0xD3` | S->C | `0x4c6790` | dónde va un personaje: la celda X/Y y la dirección en `pkt[7] >> 4` (abajo) |
| `0xD7` | C->S | `0x5650c0` (`0x565a79`) | pegar; FoxAnti lo manda como `A4` (abajo) |
| `0xD7` | S->C | `0x4caae0` | **el daño** a un personaje, con el bit 15 del id como "golpe con sacudida" (abajo; antes esta tabla decía posición y dirección) |
| `0x1C` | C->S (C3) | `0x599ef0` | pedir un gate (abajo) |
| `0x1C` | S->C (C3) | `0x4c6e10`, desde `0x4e3f2a` | el warp: el gate, el mapa, la celda y la dirección (abajo) |
| `F3 04` | S->C (C3) | `0x4c4b80` | revivir (abajo) |
| `F3 12` | C->S | `0x4c6e10` | "ya está el mapa", después de un `1C` con gate (abajo) |
| `0x0F` | S->C | `0x4e36b5` | el clima: la meta de la lluvia (abajo) |
| `0xBD` | C->S y S->C | `0x4c421a`, `0x4c785d`; `0x4e2b10` → `0x4033b0` | el estado de Crywolf; el recorrido de la cámara (abajo) |
| `0x46` | S->C | `0x4dab90` → `0x5f8750` o `0x5f8710` | cambia bits del `.att`: el único camino al `0x40` y al `0x80` de la cámara ([06](06-render-escenas-y-ui.md#la-cámara-del-juego)) |
| `0x18` | S->C | `0x4cb750` | acción o animación de un personaje remoto, según `pkt[6]` |
| `0x22` | S->C | `0x4d0cc0` | crea objetos de 0xF08 bytes desde una plantilla |
| `0x26` / `0x27` | S->C | `0x4d3510` / `0x4d3820` | la vida y el maná (abajo); el objeto de 0xF08 bytes que esta tabla les daba es la entrada del registro propio en la tabla de protección, que abren y cierran |
| `0x0D` | S->C | `0x4c65b0` | avisos del servidor: chat, banner, notificación |
| `0x02` | S->C | `0x4e34ea` | mensaje privado: nombre de 10 bytes en `+3`, texto de 60 en `+0xD` |

Los 140 handlers tocan estas estructuras (`paso5-protocolo-handlers.md`, "Estructuras de datos
recurrentes"): la tabla de jugadores (400 entradas de 0x438 bytes en `DAT_0743a16c`, id de 16 bits
big-endian), el personaje propio (0xF08 bytes en `DAT_07681eb4`), la cola circular de 10 paquetes
de 0x100 bytes, el historial de chat y la máquina de acciones diferidas.

### La entrada al mundo: la respuesta del `F3 03` y el `F3 10`

**La respuesta del `F3 03`** (`0x4c3940`). Llega cifrada, y rearmada ocupa `0x34` bytes: el
`PMSG_CHARMAPJOINRESULT` del GS 1.02 (`gs_protocol.h`), el mismo largo que en el cable
(*verificado*). Sin cifrar no entra al mundo: arma un `C1 06 F1 03 00 F3` y lo manda en C3
(`0x4c396a..0x4c404e`, *estático*), sin leer el paquete, como el `C1 06 F1 03 00 1C` del `1C`. El
GS lo atiende como `CGClientHackLogRecv` (*inferido*, del `gs_protocol.h`); MuRet ya mandaba el
`F1 03 06 00` y ahora manda también este (T7). "El registro" es `[0x7681eb0]`, que al loguear apunta al mismo bloque que
`[0x7681eb4]` (`0x4bafc9`). Todo *estático* salvo lo que dice el cable:

| `pkt` | Qué es | Adónde va | En el cable |
| --- | --- | --- | --- |
| `[4]`, `[5]` | la celda X, Y | `0x511290(héroe, 0x20a, X, Y, ángulo)` (`0x4c44fc`) | (133, 127) |
| `[6]` | el mapa | `0x6c9dd0` y `0x61a560(mapa)` (`0x4c41cf`, `0x4c41d4`) | 0 |
| `[7]` | la dirección | el ángulo, (dir − 1) · 45 (`0x4c44bc..0x4c44d1`, `0x6b87d4`, `0x6b8848`), a `+0x24` | 0: −45° (Q-C175) |
| `[8]`, `[0xc]` | la experiencia y la de la próxima | registro `+0x10`, `+0x14` | |
| `[0x10]` | los puntos | `+0x68` | |
| `[0x12..0x18]` | la fuerza, la agilidad, la vitalidad y la energía | `+0x18`, `+0x1a`, `+0x1c`, `+0x1e` | |
| `[0x1a..0x24]` | la vida, la vida máxima, el maná, el maná máximo, el BP y el BP máximo | `+0x22`, `+0x26`, `+0x24`, `+0x28`, `+0x38`, `+0x3a` | |
| `[0x28]` | el zen | `[0x7681eb4] + 0xecc` | |
| `[0x2c]`, `[0x2d]` | el PK y el CtlCode | el héroe, `+0x375` y `+0x1d8` | |
| `[0x2e]`, `[0x30]` | el AddPoint y el MaxAddPoint | `+0x44`, `+0x46` | |
| **`[0x32]`, `[0x34]`, `[0x36]`** | **fuera de los campos** (Q-C144) | `+0x20` (el Leadership), `+0x48`, `+0x4a` (`0x4c410b`, `0x4c4163`, `0x4c416b`) | `0x32..0x33` es el relleno del struct, `ff ff`; `0x34..0x37` cae fuera del paquete |

Además pone en 0 el byte de `+0x3c` y los WORD de `+0x3e`, `+0x40` y `+0x42`. Las velocidades del
keepalive (`+0x4c`, `+0x58`) no las escribe. Qué hace con el mundo y el héroe, en
[06](06-render-escenas-y-ui.md#la-entrada-al-mundo).

**El `F3 10`, el inventario** (C4, `0x4e309c` → `0x4c55a0`): la cantidad y una entrada de **8
bytes** por ítem (`pbVar11 += 8`), el lugar y 7 bytes del ítem, no los 5 de `PMSG_INVENTORYLIST`
del GS reconstruido. Cada entrada va a `0x5bc740(…, lugar, bytes)`, que la pasa al héroe con
`0x512a30`: el equipo del héroe sale de acá, no de la lista de la selección (*estático*). En el
cable, `F3 10 05` y 40 bytes: cinco entradas, en los lugares 0, `0x0c`, `0x0e`, `0x11` y `0x17`
(*verificado*). Llega en el mismo segmento que la respuesta del `F3 03` (P-C28 a).

El cotejo completo de `0x4c55a0` (661 instrucciones, 2327 bytes) confirma, en la rama de segundo
argumento no nulo y después de la inicialización común, cantidad **u8 en `p+5`**, primera entrada
en `p+6`, stride 8, slot u8 en `entrada+0` y puntero opaco `entrada+1` pasado al callee. Relee la
cantidad tras cada llamada; no inspecciona `p[0..4]`, no comprueba longitud ni lee directamente los
siete bytes. El tamaño de esos datos opacos del wire se apoya en el stride y las cinco entradas/40
bytes de arriba; el C2 reconstruido corresponde al transporte de MuRet y al ADR0003. El lector
puro `ReadInventoryList` exige bytes vivos y estables durante la llamada y produce una copia
propia, en orden y con duplicados y todos los slots; ignora trailing no contado, sin exigir
`WasDecrypted`. Devuelve `TooShort` sin resultado parcial si falta cantidad o una entrada
([ADR0010](../../../docs/adr/0010-inventario-truncado.md)); la infraestructura rechaza otro
tipo/opcode/subcódigo. Esta proyección no reproduce el handler, que está leído en
[06 § El F3 10, el inventario](06-render-escenas-y-ui.md#el-f3-10-el-inventario) (T9 del hito 1,
*estático*): el segundo argumento es el flag de cifrado, el mismo del `F3 03`; la inicialización común
(el equipo del registro propio y las dos grillas, con un cierre del registro) corre antes de mirarlo,
también sin cifrar y con la cantidad en 0; sin cifrar manda el mismo `C1 06 F1 03 00 F3` que el `F3 03`
sin leer el paquete; cifrado, cada entrada va a `0x5bc740`, que decodifica el tipo con `0x54b3a0` (el
byte 0, el bit `0x80` del byte 3 y el nibble alto del byte 5) y pone el ítem en el equipo (del lugar 0
al 11, con `0x512a30` sobre el héroe publicado) o en una grilla (del 12 en adelante). Ninguna llamada
escribe el paquete: la cantidad releída es la misma.

El golden `GoldenEncryptedPacket.ReadsAllSevenInventoryCapturesAgainstTheIndependentReference`
contrasta las siete capturas F3/10 del log del laboratorio (SHA-256
`f09225602bdd1cb0cf8184eda60639de0871ed59d5d4a6fe9bec14b013f0dfb0`): cantidades 5, 5, 2, 5, 5,
5 y 6; slots 0/12/14/17/23 para cinco, 0/1 para dos y el slot 28 adicional para seis. Procesa
primero los 25 C3/C4 S->C con sus seriales por conexión y después compara cada C4 y C2 completo,
slot y siete bytes opacos contra sidecars de la referencia matemática Python independiente.
El C2 está normalizado según ADR0003, sin afirmar identidad con la cabecera defectuosa nativa.
Los pares quedan fuera de Git y las comparaciones no revelan valores. Son siete casos observados,
no una nueva captura viva ni una certificación universal del handler o la aplicación del equipo.

**Lo que ya recibe MuRet:** `ServerSession` admite F3/03 y F3/10 sólo con `Packet::WasDecrypted`
preservado por el transporte, incluidos Identity y FoxAnti, y con sus lectores existentes. Entrega
una secuencia propietaria de ambos tipos que conserva orden, multiplicidad, los 23 campos wire de
F3/03 (mínimo `0x34`), y todos los slots, duplicados y siete bytes opacos de F3/10, también con
cantidad cero; no normaliza el mapa 32 ni exige `EnteringWorld`. Como elección de API MuRet,
`Start` descarta pendientes de la conexión anterior, `Fail` conserva lo recibido y cada drenaje
transfiere propiedad sin afectar los vectores ya entregados; `CharacterEvents` queda separado.
Esta recepción filtrada no aplica mundo, héroe, equipo, efectos ni estado `0x3d`, ni cambia selección,
generación o keepalive. No reproduce los handlers completos: quedan fuera el prefijo y efectos
comunes de `0x4c55a0` y la resolución de Q-C144; no inventa los WORD nativos `+0x34..37` ni aumenta
el mínimo wire a `0x38`. Desde la T7 del hito 1, el `F3 03` sin cifrar manda el `C1 06 F1 03 00 F3`
de `0x4c396a` y el cifrado va al aplicador nativo de `0x4c3940`
([06](06-render-escenas-y-ui.md#la-entrada-al-mundo)). Desde la T9, el `F3 10` cifrado va a
`Scenes::ApplyInventory` y el que llega en claro hace la inicialización común en el aplicador
(`App::ClearInventoryReceived`) y después manda el mismo `C1 06 F1 03 00 F3`
([06](06-render-escenas-y-ui.md#el-f3-10-el-inventario)).

### Caminar y pegar: `D3` y `D7`

**El `D3` del cliente, caminar** (`SendMove`, `0x56acd0`; *estático*; *emulado* y *verificado* en el
cable). Sale una vez por cada camino nuevo que calcula `MoveHero`
([06](06-render-escenas-y-ui.md#caminar)), por `send()` directo y sin C3:

```text
C1 len D3 X0 Y0 P0 P1 … Pk     con el XOR32 (la clave se arma en la pila, desde 0x56ae30)
len     = 6 + (PathNum >> 1)
X0, Y0  = PathX[0], PathY[0]    la celda lógica del héroe
P0      = (dir_final << 4) | (PathNum − 1)
P1..Pk  = los pasos, de a dos nibbles, el alto primero (el 1 en el alto de P1, el 2 en el bajo…)
```

- **`PathNum` va con un tope de 14** (`0x56ad8d..0x56ad92`: con 15 manda 14), aunque el cliente
  camine las 15 celdas: dice un paso menos (Q-C158). Con 15 celdas sale, en claro (antes del
  XOR32), `C1 0D D3 64 64 3D 33 33 33 33 33 33 30` (*emulado*).
- **Las direcciones** (`0x6c82a4`, (dx, dy); *verificado* contra el eco del servidor): 0 (−1, −1), 1
  (0, −1), 2 (1, −1), 3 (1, 0), 4 (1, 1), 5 (0, 1), 6 (−1, 1) y 7 (−1, 0). Un paso que no está en la
  tabla da 0.
- **`dir_final`** (*emulado*): con `PathNum == 1`, `ftol((ángulo + 22,5) · (1/45) + 1) & 7`
  (`0x56b0b7`), así que el ángulo 0 da 1, el 90 da 3, el 180 da 5 y el 270 da 7; si no, la dirección
  de la última celda que se manda al blanco (`TargetX/Y`), y si el blanco no está al lado, la del
  último paso. Con el tope de 14, esa celda es `PathX[13]` y no la última del camino
  (`0x56b0ed..0x56b13f`, con el `PathNum` ya en 14): si el blanco es la celda 15, da la dirección
  del paso que no se manda, y si está más lejos, la del último paso. En el cable, en 171 de 178 `P0`
  lleva la dirección del último paso (que el blanco sea la última celda es *inferido*: el cable no
  trae el blanco), y en los 7 restantes otra: ahí el blanco está al lado del final, en otra
  dirección (*inferido* igual). `C1 07 D3 A9 7E 21 30` sale igual en la emulación.
- **El byte a byte**: el camino (b6, 73) → (b5, 73) → (b4, 74) → (b3, 74) da, en claro, `C1 08 D3
  B6 73 73 76 70`; con el XOR32, `C1 08 D3 EC 23 E2 0B 08`; y FoxAnti (`encode_outbound`) deshace el
  XOR32, cambia `D3` por `A3` y lo vuelve a aplicar: `C1 08 A3 9C 53 92 7B 78`, **igual** al
  segmento de las 13:27:37 de la conexión 6 (*verificado*). En el cable hay 229 `A3`, con 0 a 6
  pasos (51 de 0: los giros antes de pegar).

**Precisión y conversión del ángulo, contraste CL7.4 B** (*estático*, build `c32bbd5f`):
`0x56b0b7..0x56b0cc` suma 22,5, multiplica por el float de bits `3cb60b61` y suma 1,
sin guardar floats intermedios. El helper `0x69fd3c` guarda la CW, fuerza RC=11 en
`0x69fd4b`, ejecuta **`FISTP m64int`** en `0x69fd55`, restaura la CW y devuelve EDX:EAX.
Así se confirma truncamiento a entero de 64 bits en la ruta completada, sin atribuir al
original las validaciones de NaN o fuera de rango de la API de MuRet. El helper conserva PC;
la precisión de las operaciones anteriores depende de la CW heredada. Una muestra nativa
del hilo de MU observó `0x027f`: PC53, nearest y las seis excepciones de la x87 enmascaradas
(bits 0 a 5). Se tomó con EIP `0x7c90e4f4`, en `ntdll` de XP (*inferido* por la dirección): el
hilo estaba esperando, **fuera de SendMove** y de cualquier cálculo. No dice qué CW hay adentro
de esta rama.
El cálculo double del port es conforme bajo PC53/nearest; falta comprobar la CW en esa rama
para cerrar la equivalencia nativa incondicional (P-C34 en [08](08-pendientes.md)). Los tests
del dominio double están identificados como contrato numérico del port, y no como ejecuciones
del original.

**El `D7` del cliente, pegar** (`0x5650c0`, el opcode en `0x565a79`; *verificado* en el cable): `C1
07 D7 idH idL 78 dir`. Antes de pegar, `0x5650c0` manda un `D3` con `PathNum` 1 para girar (`A3 X Y
d0` en el cable, 51 veces). FoxAnti lo manda como `A4`
([03](03-red-y-cifrado.md#la-capa-de-foxanti)). Sin la capa, el cable lleva `D3` y `D7`.

**El `D3` del servidor** (`0x4c6790`): `C1 08 D3 idH idL X Y dir<<4` (*verificado*: 1.122 en el
`muproxy.log`, todos de 8 bytes). El servidor contesta cada `A3` con un `D3` del héroe con la celda
final del camino que le mandaron y la dirección de `P0` (*verificado*: los 212 ecos que se
encuentran en el cable). Qué hace el cliente con él, en [06](06-render-escenas-y-ui.md#caminar).

El contraste estático de CL7.4 B confirma que `0x4c6798..0x4c67a9` arma el ID BE
**completo**, sin `& 0x7fff`, y lo entrega así al lookup `0x5110d0`. La dirección se carga
en `0x4c67d4` y se desplaza cuatro bits en `0x4c67dc`, sin `& 7`: el nibble `f` conserva
15. Esto fija el lector puro; no demuestra cómo el lookup resuelve un ID desconocido.

**El `D7` del servidor, el daño** (`0x4caae0`): `C1 08 D7 idH|f idL dañoH dañoL t`. El personaje es
`id & 0x7fff`, el bit 15 (f) es el "golpe con sacudida", el daño es `pkt[5] · 256 + pkt[6]`, el tipo
es `t & 0x3f` y `t >> 7` es otra bandera (*estático*; *verificado* en el cable, 614, con el id de un
monstruo y con el del héroe). El bit 15 para a un jugador que camina, el héroe incluido, y sin el
bit el cliente para a otro jugador una vez de cada dos, con un `rand() & 1` (las condiciones, en
Q-C178). Este documento lo daba
como posición y dirección: lo de caminar es solo esa parada.

### Los cambios de mapa: `1C`, `F3 04` y `F3 12`

*Estático* salvo lo que se marca; qué hace el cliente, en
[06](06-render-escenas-y-ui.md#los-cambios-de-mapa).

- **El `1C` del servidor, el warp** (`0x4c6e10`): `C3 [C1 08 1C gate mapa x y dir]`, el
  `PMSG_TELEPORT_RESULT` del servidor (`GCTeleportSend`). `pkt[3]` es el gate (0, la habilidad
  teleport en el mismo mapa), `pkt[4]` el mapa, `pkt[5..6]` la celda y `pkt[7]` la dirección. Con el
  cuarto argumento del dispatcher en 0 no lo atiende y manda `C1 06 F1 03 00 1C` en C3 (*inferido*:
  el aviso de FoxAnti). **Contradicción abierta:** [el dispatcher](#el-dispatcher-0x4e2b10) dice
  que el `0x1C` solo responde si ese argumento vale 0. No se elige ninguna de las dos: se resuelve
  contra el ASM antes de que el hito 2 admita el `1C` (P-C33 en [08](08-pendientes.md)).
- **El `1C` del cliente, pedir un gate** (`0x599ef0`): `C3 [C1 06 1C gate 00 00]`, con el XOR32
  adentro y el serial de `0x5767913`. No está en ningún cable: en la PCAP todos los cambios fueron
  por `/move` (P-C31 a).
- **El `F3 12` del cliente** (`0x4c6e10`): `C1 04 F3 12`, en C1 con el XOR32 y no en C3, después de
  cargar el mapa de un `1C` con el gate distinto de 0. En el cable sale como `C1 04 F3 68` (`0x12 ^
  0x89 ^ 0xF3`; *verificado*). Del lado del servidor pasa `RegenOk` de 1 a 2 (`Seam_4789b0`, del GS
  reconstruido).
- **El `F3 04` del servidor, revivir** (`0x4c4b80`): `C3 [C1 18 F3 04 x y mapa dir vida:2 maná:2
  bp:2 relleno:2 exp:4 zen:4]`, el `PMSG_CHARREGEN`. La vida va a `0x7681eb0 + 0x22`, el maná a
  `+0x24`, el BP (`pkt[0xc]`) a `+0x38`, la experiencia a `+0x10` y el zen a `0x7681eb4 + 0xecc`.
  **No manda el `F3 12`.** En el `muproxy.log` hay dos, los dos cinco segundos después del `17` (la
  muerte) del héroe, y ningún `F3 12` detrás (*verificado*).

Contraste estático de CL7.5 C, build `c32bbd5f`: las 363 instrucciones y 1.258 bytes
de `0x4c4b80` coinciden con el PE. Las cargas x86 de `0x4c4c23..0x4c4c4a` fijan
vida/maná/BP **U16 little-endian**, experiencia y zen **U32 little-endian**. Los bytes
`+0xe/+0xf` no se leen; no se exige relleno cero. El mayor byte leído es `+0x17`:
el paquete lógico necesita 24 bytes. La dirección de `+7` se conserva completa;
convertirla a yaw pertenece al handler, no al lector puro.

Para `0x4c6e10` se contrastaron 1.523 instrucciones y 5.415 bytes con el PE: gate,
mapa, X, Y y dirección son bytes crudos en `+3..+7`, sin máscaras. Gate cero omite
el procesamiento del mapa y la confirmación; esa condición pertenece a la escena.
La confirmación `F3 12` se arma en `0x4c7c63..0x4c7cfe` y se envía como C1 con XOR32
en `0x4c7e2e`. El lector devuelve los campos sin aplicar la condición del gate.

El pedido de gate se contrastó en `0x599ef0` (1.040 instrucciones y 3.840 bytes del
export coinciden con el PE): `0x59a524` escribe el gate, `0x59a613` y `0x59a702` los
ceros, `0x59a7f1..0x59a7f7` fija longitud 6 y `0x59a90e` arma C3. Esto fija
`C1 06 1C gate 00 00` lógico con XOR32 y entrega cifrada; no aporta una captura nativa
del pedido. `Protocol::World` reutiliza el serial y el encoder existentes de Network.

En la PCAP, los cuatro `/move` de la sesión (*verificado*; el C3 del servidor, descifrado):

| Hora | `1C` del servidor | Gate → mapa, celda | `F3 12` del cliente |
| --- | --- | --- | --- |
| 19:24:34.036 | `C1 08 1C 11 00 8A 79 00` | 17 → 0 (desde el 0), (138; 121) | +37 ms |
| 19:24:47.872 | `C1 08 1C 1B 03 AB 6C 00` | 27 → 3, (171; 108) | +299 ms |
| 19:24:50.185 | `C1 08 1C 4B 07 E1 35 00` | 75 → 7, (225; 53) | +227 ms |
| 19:25:02.856 | `C1 08 1C 11 00 90 81 00` | 17 → 0 (desde el 7), (144; 129) | +141 ms |

Antes de cada `1C` va el `/move` del cliente en el chat, y después el servidor manda `C1 05 B2 19
00` (del Castle Siege) y un `C3 [22 FE …]` (el zen del `/move`). Los monstruos vuelven a llegar
(`13`) después del `F3 12`. El tiempo hasta el `F3 12` es la carga: 37 ms sin cambiar de mapa y de
141 a 299 ms con carga (*inferido* que sea la carga).

### El clima y Crywolf: `0F` y `BD`

- **El `0F` del servidor, el clima** (`0x4e36b5`): `C1 04 0F xy`. Con el nibble alto en 0,
  `0x75fdc38 = 0`; con 1, `(pkt[3] & 0xf) · 6`; con otra, va al default del dispatcher
  (`0x4e5b4a`) sin tocar la lluvia (*estático*). `0x75fdc38` es la meta de la cuenta de la lluvia de
  las hojas ([06](06-render-escenas-y-ui.md#lo-propio-de-lorencia)). En el cable llegan de `0x01` a
  `0x29` (en el `muproxy.log`, `24`, `21`, `26`, `01`, `17`, `27` y `07`; *verificado* que llegan).
  Contraste ASM `0x4e36b5..0x4e36f9` (2026-11-10): `mov al,[pkt+3]` después de `xor eax,eax`,
  `shr ecx,4`; `0x4e36c4` escribe el DWORD cero, y `0x4e36e3..0x4e36ed` toma el nibble bajo y
  lo multiplica por seis con `lea` y `shl`. Los otros 224 bytes van por `0x4e36dd` a
  `0x4e5b4a`, que retorna 1 sin escrituras. No hay guarda de escena, mapa, login ni flag C3;
  no se lee ni escribe la cuenta actual `0x75fdc3c`, no hay llamadas ni `rand()` y no se modifica
  la tormenta `0x723c50`. La meta y la cuenta están en `.bss`, inicialmente cero; el valor de
  hojas de `.data[0x6c8c5c]` es 30 (`1e 00 00 00`).
  **MuRet, H4.11:** lector puro de C1, con `TooShort` sin `xy`, `WrongPacket` para otro tipo/opcode
  y bytes sobrantes ignorados. Conserva el byte entero en el evento; el aplicador síncrono sólo
  escribe `WorldRuntime.leafWeather.target` en las ramas 0/1. El estado sobrevive a la sesión y
  no requiere terreno/modelos. La sesión entrega en orden antes del siguiente paquete, sin ACK;
  un error del consumidor conserva lo aplicado y corta el lote. Tests de los 256 bytes y de una
  Connection real con socket/GS de prueba, en claro y normalizada desde C3; no son observación
  nativa. No hay consumidor productivo de esa meta para hojas/lluvia o audio en este slice ([P-C57](08-pendientes.md)).
- **El `BD` del cliente**: `C1 04 BD 00`, en C1 con el XOR32, al entrar al mapa 34 (Crywolf), desde
  la respuesta del `F3 03` (`0x4c421a`) y desde el `1C` (`0x4c785d`): el byte que sigue es 0 en los
  dos (`0x4c4269`, `0x4c78ac`; *estático*; *inferido*: el pedido del estado de Crywolf). Hay otros
  dos emisores de `BD`, con el 3 (`0x435d4c`) y con el 9 (`0x65a986`), sin leer.
- **El `BD 00` del servidor** (`0x4e2b10` → `0x4033b0`): pone en su estado el recorrido de la
  cámara `CCameraMove`, el único camino a él ([06](06-render-escenas-y-ui.md#la-cámara-del-juego);
  *estático*).

### El chat que llega: `00`, `01`, `02`, `0C`, `0D` y `F3 30`

Leído en el ASM en la CL8 (hito 3, 2026-11-12; *estático* todo). Ninguno de los seis mira el estado de la sesión ni
el flag de FoxAnti, y ninguno mira el largo del paquete: leen a desplazamientos fijos, también más allá del final
(Q-C510). Los textos se copian crudos, con lo que traigan después del primer `\0`.

- **`00`** (`0x4e2b10` caso 0 → `0x4c6070`). Con la escena 2 (`[0x6d5dc8]`, el login) no lee el paquete: anota en el
  log (`0x41fa40`) y manda `C1 04 F4 02` en claro por el socket abierto. Es el pedido de la lista que sigue al saludo
  del CS, y sale igual si el `00` llega por el GS (Q-C511). Fuera de la escena 2: `C1 len 00 <nombre:10> <texto>`.
  El nombre va a un buffer de 11 bytes en cero, y `pkt[0xd]` elige: `~`, la party (tipo 4, 59 bytes desde
  `pkt[0xe]`); `@@`, el aviso del gremio (tipo 6, 58 desde `pkt[0xf]`); `@`, el gremio (tipo 5, 59 desde
  `pkt[0xe]`); `#`, solo un globo por nombre que también va al registro (`0x557560` con 1, 59 desde `pkt[0xe]`); y
  si no, un globo por nombre (`0x557560` con 0) y el chat (tipo 3), 60 desde `pkt[0xd]`. Cada copia va a un buffer
  de 61 en cero (`REP MOVSD` y `MOVSB`, sin mirar el `\0`). Los tipos van a `0x555190`; los globos, en
  [06](06-render-escenas-y-ui.md#el-chat). Sin `rand()`.
- **`01`** (`0x4e33fc`): `C1 len 01 <clave:2, big-endian> <texto>`. `0x5110d0` busca entre los 400 el primero vivo
  con esa clave (con `MOVSX` de `+0x1f4`: una clave desde `0x8000` no la tiene nadie, Q-C515) y, sin ninguno,
  devuelve el 400. Con el héroe en `+0x368 = 0x80` y ese personaje llamado `"길드 마스터"` (`0x6c8404`, 12 bytes con
  `REPE CMPSB` desde `+0x1d9`) abre la ventana del gremio (`0x6599a0` con `0x40000`, CL9.4). Si no,
  `0x557170(nombre, pkt + 5, personaje, 0, sin color)`: el texto se lee en el paquete mismo, hasta su `\0`.
- **`02`** (`0x4e34ea`): `C1 len 02 <nombre:10> <texto>`. Con `0x77f3b98` en 1 (`/whisper off`) no hace nada. Si
  no, `0x554d20(10, nombre)` lee el nivel propio (`+0xe`) con el registro propio abierto y lo cierra (3.848
  `rand()`), y anota al emisor si el nivel es menor que 10 (Q-C453); con `0x77f3b78` suena el `0x26`; y
  `0x555190(nombre, 60 bytes desde pkt[0xd], 0)`.
- **`0C`** (`0x4e366f`): `C1 04 0C <resultado>`. Con 0, `0x555190(0x57678c4, texto 482, 2)`: el destino del último
  susurro que salió (`0x4b87e9`) y el error. Con otro, nada.
- **`0D`** (`0x4c65b0`): `C1 len 0D <tipo> <b4> <b5> <WORD 6> <DWORD 8> <b12> <texto>`, el texto desde `pkt[0xd]`.
  Con 0, `0x554610(texto, 0)`, el aviso centrado. Con 1, fuera de la escena 4, `0x555190("", texto, 1)`; en la 4,
  si la barra de la selección (la ventana `+0x7f50`) tiene su `+0x510` en 0, `0x63eda0` lo pone en 1 y copia el
  texto a `+0x511` con `strcpy` (Q-C513); si ya está, va al área de mensajes con el `""` de `0x5767984`. La barra
  se arma una vez (`0x63e6e0`, desde `0x65ec60`): el aviso queda hasta que se cierra el juego. Con 2,
  `sprintf(buffer de 100, texto 483, texto)` (Q-C514), `0x554610(buffer, 1)` y `0x651680` sobre `[0x7d536f0]`, la
  ventana del gremio (CL9.4). De 10 a 15 con texto, `0x46b720(b4, WORD 6, texto, tipo − 10, b12 · [0x6b8a34],
  DWORD 8)`, el cartel de `Slide.bmd`: solo en la escena 5 y con `b4` de 1 a 30 (`0x46b73e` y el bucle de
  `0x46abf0`) agrega algo a sus colas ([06 § El cartel de `Slide.bmd`](06-render-escenas-y-ui.md#el-cartel-de-slidebmd)).
  `pkt[5]` no se lee.
- **`F3 30`** (`0x4e32c9` → `0x4dd570(pkt + 4)`): `C1 13 F3 30 <teclas:10> <banderas> <Q> <W> <E> <chat>`. Abre el
  registro propio y llena las teclas del elegido: `+0x26c + 0x100 · [0x6d5edc]`, 0x100 bytes en `0xff`, y para cada
  tecla con skill (no `0xff`), el número de la tecla en el primer lugar `j` de las skills del registro (`+0x6c + j`)
  que tenga ese número; lo cierra (3.848 `rand()`). Después: el bit `0x01`, el ataque automático (`0x6c8e3c`);
  `0x04`, el sonido de los avisos (`0x77f3b78`); `0x10`, los carteles apagados (`0x6c7a8c` en 0); Q, W y E,
  `0x1c00 +` el byte en las ranuras rápidas; y el byte del chat: `(alto · 3)` con `IMUL` de un byte son los renglones
  del registro (con 0, el registro sale de su ventana; si no, entra y `0x472020` los pone en `+0x104`) y `bajo ·
  0,1f` (`FILD` y `FMUL` por el float de `0x6b87b8`), su transparencia. Sin elegido (`[0x6d5edc]` en −1), escribe
  en `+0x16c`.

**MuRet** (CL8 hito 3): `Protocol::Messages` y `Protocol::OptionData` leen solo los bytes que llegaron, con lo que
falta en `\0` (Q-C510): el `00` y el `02` no fallan por cortos, y el `01`, el `0C`, el `0D` y el `F3 30` sin sus
campos fijos son `TooShort`. Otro tipo u opcode, `WrongPacket`. La sesión los pasa al aplicador sin mirar su
estado (`SessionEventApplier`): el `00` en el login pide la lista por el GS (`ServerSession::ResendServerListRequest`),
y lo demás va a `Scenes/ChatReceive`, que corta con la dirección donde el original abre otra ola: el Guild Master del
`01` (`0x4e3450`), la ventana del aviso del gremio (`0x4c665f`, después del aviso centrado) y el `F3 30` sin
elegido (`0x4dd645`, antes de abrir el registro). El `0D` de 10 a 15 encola en el cartel (`UI::QueueTheSlide`, con la
velocidad `b12 · 0,1f` en la x87 y guardada en float). Los tests usan nombres y textos inventados.

### El chat que se manda: `00` y `02`

(CL8 hito 3, *estático*.) Los arma el WndProc al apretar Enter (`0x4b5b0d` el chat y `0x4b70a7` el susurro) y las
macros (`0x5a1388`), y salen en C1 con el XOR32 desde el byte 3 y en claro por `send()`, sin la cola de los cifrados:

- **`C1 (0xd + n) 00`**: el nombre del héroe (`+0x1d9`, sus diez bytes con lo que haya detrás del `'\0'`) y el texto.
- **`C1 (0xd + n) 02`**: los diez primeros bytes del buffer del susurro (`0x7798cf8`) y el texto. El destinatario
  queda en `0x57678c4`.

El texto va con su `'\0'` si tiene menos de `0x3c` bytes; si no, sus primeros `0x3c`, sin él (`0x4b7aaf` y
`0x5a1515`). Del lado del GameServer son `PMSG_CHATDATA` y `PMSG_CHATDATA_WHISPER`: el nombre en `+3` y el texto en
`+0xd`, de hasta 60 bytes (`CGChatRecv` y `CGChatWhisperRecv`). El camino cifrado de `0x4b6989` no se toma con estos
dos. El chat no saca `rand()`; el susurro, los 3.848 del registro propio de `0x554a40`
([06](06-render-escenas-y-ui.md#el-chat), "La entrada del chat"). **MuRet**: `Protocol::Chat::WriteChat` y
`WriteWhisper`, que manda `App::ServerSession::SendChat`; lo que llevan lo escribió el jugador y no va al log.

### Mover y equipar ítems: `24`, `28`, `2A`, `F3 13` y `F3 14`

(CL8 hito 4, 2026-11-12, *estático*; el cable de la sesión del laboratorio queda para la VM, P-C72.) Los lugares son
los del `F3 10`: 0 a 11 el equipo y 12 + fila · 8 + columna las 64 casillas del inventario; desde `0x4c`, la tienda
personal. Los siete bytes de un ítem son los del `F3 10`.

- **`24` del cliente**: `C1 0E 24 <0> <lugar de origen> <ítem:7> <0> <lugar de destino>`, con el XOR32 desde el
  byte 3 y en C3: 2 `rand()` (el relleno y el serial, `0x40b0a0`). Lo arman `0x449d80` y, en línea, `0x5ca46c..`
  (la grilla), `0x5bf294..` (el equipo) y `0x59bf1c` (MoveHero). El ítem va como `+0` (el tipo & 0xff), `+4`,
  `+0x1a`, `+0x1b`, `+0x1c`, `(tipo con signo >> 5) & 0xf0` y 0. Antes, la traba `0x78abb39` en 1: el paso del
  equipo, el de la grilla y el del ítem del cursor no hacen nada hasta la respuesta.
- **`24` del GameServer** (`0x4d1400`): `C3 0C 24 <resultado> <lugar> <ítem:7>`. Si llegó en C1, sin pasar por
  SimpleModulus, `0x4d166b..0x4d1690` manda `C1 06 F1 03 00 16` con el XOR32 y en C3 sin leerlo y sin soltar la
  traba. Cifrado, suelta la traba; con 0xff (rechazado), `0x5be150` vuelve el ítem del cursor a su lugar. Si no,
  arma las listas de la tienda personal (`0x4d14c6..0x4d1520`), deja la cuenta del cursor en 0 y pone el ítem con
  `0x5bc740(lugar, ítem, …, 0)` según la grilla del resultado: 0 el inventario, 1 el comercio (`0x78a92a8`), 2 el
  almacén o la tienda (`0x78a7070`), 3 la caja del caos y 5 la caja del entrenador (`0x44ad70`). Después, el sonido
  `0x1d`.
- **`28`** (`0x4c5ec0`): `C1 05 28 <lugar> <fin de la espera>`. Con el lugar distinto de 0xff, `0x5bd750(lugar)` lo
  saca (desde `0x4c` lee antes `0x6c931c` y la grilla de la tienda personal); con el segundo byte distinto de 0,
  `0x5767954` (la espera del uso de un ítem) en 0.
- **`2A`** (`0x4d3cc0`): `C1 06 2A <lugar> <durabilidad> <fin de la espera>`. Del 0 al 11, `+0x1a` del lugar con el
  registro propio abierto (3.848 `rand()` al cerrarlo); desde el 12, en toda la huella del tipo de esa casilla, desde
  la casilla misma (`0x4d3f4c..`). El tercer byte, como el del `28`.
- **`F3 13`** (`0x4e316e`): `C1 18 F3 13 <clave:2, big-endian> <sin leer> <equipo:17>`. `0x5110d0` busca el personaje
  de la clave (el 400 sin ninguno, que en el arreglo existe) y `0x5139f0(índice, pkt + 7, 0, 0)` lee su equipo si es
  un jugador (`+2` en `0x20a`); si no, vuelve sin tocar nada (`0x513a12`).
- **`F3 14`** (`0x4e31a3`): `C1 0C F3 14 <lugar> <ítem:7>`. La cuenta del cursor en 0, `0x5bc740(lugar, ítem, …, 0)`
  y el sonido `0x45`.

**MuRet** (CL8 hito 4): `Protocol::Inventory` escribe el `24` y el aviso del `24` en claro y lee los otros cinco;
`App::ServerSession` manda el `24` (`SendItemMove`), contesta el `24` en claro sin leerlo y sin sacar sus 2
`rand()` (P-C72) y pasa los demás al mundo (`Scenes::ApplyItemMoveResult`, `ApplyItemDelete`, `ApplyDurability`,
`ApplyItemPut` y `ApplyEquipmentChange`). Cortan con su dirección las grillas de otras olas del `24` (`0x4d1ce0`,
`0x4d1d0a` y `0x4d1d34`, CL9.3 y CL9.4; `0x4d1d55`, CL9.3), el `28` de la tienda personal (`0x4c5ece`, CL9.4) y el
equipo de un jugador del `F3 13` (`0x513a1e`, CL9.2). Los tests usan ítems inventados.

### La vida y el maná: `26` y `27`

(CL8, 2026-11-13, *estático*, contra `PMSG_REFILL` y `PMSG_MANASEND` del GameServer reconstruido.) Los dos llegan en
cualquier escena del GameServer, también antes del `F3 03`: los handlers no miran cuál es. Cada uno abre el registro
propio (`0x7681eb4`, sin `rand()`), hace su tramo y lo cierra: 3.848 `rand()` si queda sin accesos (Q-C183).

- **`26`** (`0x4d3510`): `C1 07 26 <lugar> <vida alta> <vida baja> <Flag>`. Con `0xfd`, `0x5767954` (la espera del uso
  de un ítem) en 0 y nada más; con `0xfe`, la vida máxima (`+0x26`); con `0xff`, la vida (`+0x22`), las dos con
  `pkt[4] << 8 | pkt[5]`. Con otro lugar no lee la vida: es la poción de la casilla lugar − 12 de la grilla
  (`0x78a9bb0`, de a `0x48`, `0x4d35f6`): su durabilidad (`+0x1a`) baja 1 si no es 0 y, en 0, la casilla queda con el
  tipo `0xffff` y `+0x3c` en 0. Sin la huella y sin mirar el rango. El `Flag` no lo lee nadie.
- **`27`** (`0x4d3820`): `C1 08 27 <lugar> <maná:2> <BP:2>`, los dos con el byte alto primero. Con `0xfe`, el maná
  máximo (`+0x28`) y el BP máximo (`+0x3a`); con `0xff`, el maná (`+0x24`) y el BP (`+0x38`). Con otro lugar, el
  maná y la poción del lugar, como el `26` (`0x4d3917`); el BP no lo lee. No tiene el `0xfd`: es un lugar más.

El GameServer 1.02 manda el `26` con `0xfd` (rechazos del uso de un ítem, con la vida, que el cliente no lee),
`0xfe` y `0xff`, y el `27` con `0xfe` y `0xff`: el camino de la poción no lo usa (saca las pociones con el `28`;
07, Q-C770 y Q-C771).

**MuRet**: `Protocol::Inventory::ReadLifeRefill` y `ReadManaRefill` leen lo mismo que el original (la vida y el BP
solo con `0xfe` y `0xff`); `App::ServerSession` los pasa al mundo y `Scenes::ApplyLifeRefill` y `ApplyManaRefill`
escriben el registro propio que leen los orbes y el AG del HUD (06). Un lugar fuera de la grilla corta con
`0x4d35f6` o `0x4d3917` (08, P-C150). Los tests usan valores inventados.

### Las skills que llegan: `19`, `1E`, `1B`, `07`, `48`, `69`, `73`, `F3 11` y `F3 20`

(CL9 hito 4, 2026-11-13, *estático*, contra `gs_protocol.h` y `protocol.cpp` del GameServer reconstruido.) Lo que
hacen con el mundo está en [06](06-render-escenas-y-ui.md#las-skills-que-llegan). Las claves son índices de
`0x5110d0`, que compara con `MOVSX`: una clave desde `0x8000` nunca está, y sin el personaje es el 400.

| Paquete | Handler | Layout que lee el cliente | GameServer 0.97 |
| --- | --- | --- | --- |
| `19` | `0x4cc530` | `C3 08 19 <skill> <fuente H L> <blanco H L>`; el bit 15 del blanco es el éxito (`+0x38d`) y el blanco se busca con los 15 de abajo | `PMSG_MAGICATTACK_RESULT` (`GCMagicAttackNumberSend`, `0x426200`), igual. El FoxAnti lo pasa como `A5` cifrado (`EncryptedRemaps`) |
| `1E` | `0x4ce000` | `C3 09 1E <skill> <clave H L> <x> <y> <rumbo>` | del mismo largo; el emisor no está nombrado en el reconstruido. `A6` cifrado |
| `1B` | `0x4cbe90` | `C1 06 1B <skill> <clave H L>` | `PMSG_MAGICCANCEL` (`0x4262f0`), igual |
| `07` | `0x4cbe00` | `C1 0A 07 <tipo> <máscara: 4, little endian> <clave H L>` | `GCSend_07` (`0x4263a0`) arma 8 bytes y nadie lo llama (P-C260) |
| `48` | `0x4dbf50` | `C1 06 48 <clave H L> <tipo>` | no lo manda (P-C262) |
| `69` | `0x4cc290` | `C1 08 69 <skill> <fuente: 2> <blanco: 2>`, los dos WORD en little endian y sin máscara | no lo manda (P-C262) |
| `73` | `0x4d5b10` | cifrado no lee nada; sin cifrar, avisa | no lo manda (P-C262) |
| `F3 11` | `0x4c5070` | `C1 .. F3 11 <cantidad> <lista>` y desde `pkt[6]`: con `0xff`, el lugar a vaciar; con `0xfe`, el lugar y la skill; con la lista en 2, el lugar a vaciar; si no, `cantidad` entradas de 3 bytes (lugar, skill y uno que no lee) | `PMSG_MAGICLISTCOUNT` mide 5 bytes y las entradas (`PMSG_MAGICLIST`, con `(nivel << 3) \| (skill & 7)` de tercero) van desde `pkt[5]`: el byte de la lista lo mete el FoxAnti del repack (P-C261) |
| `F3 20` | `0x4e31e1` | `C1 05 F3 20 <vida>` (la del invocado, `0x5767960`) | — |

Los avisos: el `19` (`0x4cc562..0x4cce21`) y el `1E` (`0x4ce032`) que no llegaron cifrados no se leen y mandan
`C1 06 F1 03 00 <opcode>` con el XOR32 y para cifrar como C3, como el `F3 03`; el `73` sin cifrar manda el mismo con
`0xf1` (`0x4d5d8c`, P-C263) y cifrado no hace nada. Los demás no miran el cifrado.

**MuRet**: `Protocol::Skill` lee los nueve (WrongPacket con otro tipo u opcode, TooShort si faltan campos) y escribe
el aviso (`WriteClearSkillReport`); `App::ServerSession` los registra juntos (`RegisterSkillHandlers`) y los pasa al
mundo, donde `Scenes::SessionEventApplier::ApplySkillEvent` los manda a `Scenes/SkillReplies`. El `B7` y los C→S de
las skills (`19`, `1E`, `D3`, `DF` y `26` de las cuatro funciones por clase) son de la fase 2 (08, P-C264). Los tests
usan claves inventadas.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Un `switch` de 10 KB con tres niveles de tablas | `Protocol::Dispatcher`: tabla opcode → handler, con subdispatchers por opcode; handlers en un archivo por dominio | CL4, CL9 |
| Handlers que escriben globales del juego | handlers que traducen bytes a eventos/comandos del `GameLogic`; no tocan render ni sockets (regla de capas) | CL4, CL9 |
| Constantes del build esparcidas | versión `10209`, serial `MULORDSVERSAO102` y XOR `FC CF AB` en un solo archivo de constantes del protocolo | CL2 |
| El dispatcher solo se alcanza por FoxAnti | el dispatcher es parte del cliente; la ofuscación es una capa aparte | CL2 |
| 104 opcodes al default | el default loguea el opcode desconocido una vez (sirve para descubrir lo que falta) | CL4 |
| El `A9 00 00 01` al CS sale porque la escena de login arma un personaje con el cuervo (Q-C16) | se manda igual al abrir el CS, como pedido de la información de la mascota; el `A9 01 00 08` del juego va con la UI de las mascotas | CL4, CL8 |
| El keepalive: un timer desde el arranque que manda con el GS conectado, sin mirar el login (Q-C53), y el primero con el login aceptado; las velocidades en 0 hasta que hay personaje | el primero con el login aceptado y después cada 20 s desde ese, en `LoggedIn` y, desde la CL6.3 y la CL6.5, también en la espera de la lista, la elección, crear, borrar y la entrada al mundo; 0 y 0, sin ajuste, hasta que la CL7 sepa quién las calcula: la respuesta del `F3 03` no escribe `+0x4c` ni `+0x58` ([La entrada al mundo](#la-entrada-al-mundo-la-respuesta-del-f3-03-y-el-f3-10), P-C28 i) | CL4, CL6.3, CL6.5, CL7 |
| Los `F3` de la selección: el `F3 00` desde la lógica del login, la lista sin mirar el largo y el `F3 03` desde el primer paso de la escena 5 | `Protocol::Character` (`WriteCharacterListRequest`, `ReadCharacterList` con `TooShort` y el lugar de más de 4 descartado, Q-C101, y `WriteCharacterSelect`) y tres estados más de la `ServerSession`; crear y borrar, con sus avisos, en la CL6.5 | CL6.3, CL6.5 |
| La respuesta del `F3 03`: un C3 de `0x34` bytes que `0x4c3940` lee hasta el `0x37` (Q-C144), y el `F3 10` con entradas de 8 bytes | un lector de la respuesta con `TooShort`, el estado `0x3d` en la sesión, y un lector del `F3 10` con el lugar y los 7 bytes de cada ítem (propuesta del barrido de Lorencia) | CL7 |
| Caminar: el `D3` con el tope de 14 y la dirección final por el blanco o por el ángulo; el `D3` y el `D7` del servidor | un escritor del `D3` contra el `SendMove` real emulado y los bytes del cable, y lectores del `D3` y del `D7` (este, al menos, con la sacudida); el `A3` y el `A4` siguen siendo de la capa de FoxAnti (propuesta del barrido) | CL7.4 |
| Los cambios de mapa: el `1C` en los dos sentidos, el `F3 04`, el `F3 12` y el `BD 00` | lectores del `1C` y del `F3 04`, escritores del `1C` (en C3), del `F3 12` y del `BD 00` (en C1 con el XOR32), y la espera del warp en la sesión (propuesta del barrido) | CL7.5 |

Tests que fija:

- `C1 04 F4 02` exacto al recibir el saludo `C1 04 00 01` del CS en la escena de login (CL4).
- Con la capa FoxAnti, la lista del cable llega al dispatcher como `F4 06` con el código 400 y la
  elección sale al cable como `F4 03 00 00` (CL4; la capa se prueba sola en CL2).
- El intercambio del CS y del GS de arriba, byte a byte, contra un CS y un GS falsos que respondan
  con los bytes del cable (CL4).
- El login `F1 01` byte a byte contra los fixtures de `mu-client` (CL2) y, cuando exista, contra
  una captura del mismo build (CL4).
- El keepalive con los cinco fixtures de `mu-client` (CL2).
- Sin personaje, el keepalive manda las dos velocidades en 0 y el byte 3 en 0 (CL4.5,
  `ServerSessionTest.WithoutACharacterTheSpeedsAreZero`).
- Integración: un CS y un GS falsos en el test, el cliente llega a "login aceptado" (CL4).
- La lista con entradas sintéticas (nombres como `"char0001"`), y el `F3 00` y el `F3 03` de MuRet
  contra los del cable por el largo, el tipo y el subcódigo, sin imprimir valores
  (`SameBytesWithoutValues`, CL6.3).
- Del mundo, con lo que propone el barrido de Lorencia y sin ningún nombre: la respuesta del `F3
  03` del `muproxy.log` (la celda (133, 127), el mapa 0 y la dirección 0) y el `F3 10` de cinco
  ítems (CL7); el camino (b6, 73) … (b3, 74) da `C1 08 D3 EC 23 E2 0B 08`, y con FoxAnti `C1 08 A3
  9C 53 92 7B 78`, 15 celdas salen como 14, y el `D3` y el `D7` del servidor con los formatos del
  cable, sin ids reales (CL7.4); los cuatro `1C` de la PCAP y los cuatro `F3 12` (`C1 04 F3 68`),
  los dos `F3 04` del `muproxy.log`, y el `1C` del cliente contra el cifrado C3 con un serial fijo
  (CL7.5).
