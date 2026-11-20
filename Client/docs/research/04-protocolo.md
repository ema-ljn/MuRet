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
    `0x5b2b0d`): la mascota (1 el caballo, 0 el cuervo) y los bytes de abajo de `0x78abb28` y `0x78abb2c`, la
    ventana y el lugar del ítem del cartel (desde el inventario, 0 y la casilla + 12 o el lugar del equipo).
    *Estático*; MuRet lo manda desde la CL9 hito 4 (`Scenes::DrawThePetTooltip`, [06](06-render-escenas-y-ui.md#el-a9-de-las-mascotas)).

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
  `0x44b9ce`, `0x44c62e` y `0x658dfd` a `0x4499b0`): MuRet tiene los dos del entrenador con `0x724580` en 2
  (`0x44b9ce` y `0x44c62e`, CL9 hito 6, `Scenes/PetTrainer`). Cerrar un NPC de evento (`0x657ab0` con
  `0x1000` o `0x4000`, `0x658a18` y `0x658c0e`) lo arma en la pila y lo manda, en claro, antes de poner `0x7d53712` en 0
  (CL9 hito 11, fase 2; `UI::PanelContext::eventNpcClosed`, que prestan el paso de la UI, caminar, el `90`, los
  cierres de todos los paneles y la entrada, el teleport y el regen).
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
| `F3 05` | S->C (C1) | `0x4e30b9 → 0x4d2ed0` | [subir de nivel](#subir-de-nivel-f3-05) |
| `F3 12` | C->S | `0x4c6e10` | "ya está el mapa", después de un `1C` con gate (abajo) |
| `0x0F` | S->C | `0x4e36b5` | el clima: la meta de la lluvia (abajo) |
| `0xBD` | C->S y S->C | `0x4c421a`, `0x4c785d`; `0x4e2b10` → `0x4033b0` | el estado de Crywolf; el recorrido de la cámara (abajo) |
| `0x46` | S->C | `0x4dab90` → `0x5f8750` o `0x5f8710` | cambia bits del `.att`: el único camino al `0x40` y al `0x80` de la cámara ([06](06-render-escenas-y-ui.md#la-cámara-del-juego)) |
| `0x18` | S->C | `0x4cb750` | acción o animación de un personaje remoto, según `pkt[6]` |
| `0x20` / `0x21` | S->C (C2) | `0x4d0ba0` / `0x4e3849` | los ítems del piso que aparecen y los que se van (abajo) |
| `0x22` | C->S (C3) y S->C (C3) | `0x5650c0` (`0x566bd2`) / `0x4d0cc0` | levantar un ítem del piso y el resultado (abajo; antes esta tabla decía "crea objetos de 0xF08 bytes": es el registro propio, que abre para el zen) |
| `0x23` | C->S (C3) y S->C | `0x5d8d50` (`0x5e04cc`) / `0x4d10d0` | tirar el ítem del cursor y el resultado (abajo) |
| `0x26` | C->S (C3) | `0x59ff10` (`0x5a30dc`), `0x5c4110` (`0x5c5e4a`) | usar un ítem del inventario (abajo) |
| `0x2C` | S->C | `0x4de500` | la fruta de stats (abajo) |
| `0x26` / `0x27` | S->C | `0x4d3510` / `0x4d3820` | la vida y el maná (abajo); el objeto de 0xF08 bytes que esta tabla les daba es la entrada del registro propio en la tabla de protección, que abren y cierran |
| `0x0D` | S->C | `0x4c65b0` | avisos del servidor: chat, banner, notificación |
| `0x02` | S->C | `0x4e34ea` | mensaje privado: nombre de 10 bytes en `+3`, texto de 60 en `+0xD` |

Los 140 handlers tocan estas estructuras (`paso5-protocolo-handlers.md`, "Estructuras de datos
recurrentes"): la tabla de jugadores (400 entradas de 0x438 bytes en `DAT_0743a16c`, id de 16 bits
big-endian), el personaje propio (0xF08 bytes en `DAT_07681eb4`), la cola circular de 10 paquetes
de 0x100 bytes (`0x77f27b4`: el dispatcher copia ahí el `0x0B`, el `0x12`, el `0x1C`, el `0x24` y el `0x2A` antes de
su handler, sin mirar el cifrado; [06](06-render-escenas-y-ui.md#los-otros-jugadores-en-el-viewport)), el historial de
chat y la máquina de acciones diferidas.

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

### Subir de nivel (F3 05)

`GCLevelUpMsgSend` del GameServer 1.02i (`0x417ab0`) emite `C1 12 F3 05`.
Desde el offset `0x04` vienen siete palabras little-endian ordenadas por
nivel, puntos de nivel, máximos de vida, maná y BP, puntos añadidos y máximo
de puntos añadidos. El cliente descifra el registro propio, actualiza esos
campos y lleva la vida y el maná actuales a sus respectivos máximos. El BP
actual y la experiencia actual se conservan. `0x550ca0` calcula la siguiente
meta en aritmética de 32 bits con `10 · (L + 9) · L²` y, para `L > 255`,
suma `1000 · (L - 246) · (L - 255)²`. El handler lee además dos palabras
que el emisor de 18 bytes no manda. MuRet copia cada palabra completa si
llegó y usa cero si falta. [Q-C1369](07-quirks-y-bugs.md#subida-de-nivel).

El registro se cierra antes de los efectos. El cierre exterior consume
`0xf08` sorteos. `0x4d319d..0x4d31f4` usa el héroe publicado para intentar
15 joints `0x60f/0` de ancho 40 y clave 2, con tres sorteos por alta, luego
un calco `0x61e/0` y el sonido `0x98` sin origen ni bucle. Se pide el
sonido también cuando los pools están llenos. MuRet comparte este tramo con
el efecto de actor `48/10`, que conserva sus propias guardas.

La longitud se verificó en el ASM del emisor. La captura histórica no tiene
una subida de nivel observada. Los tests ejercitan el lector, el despacho
real de sesión, la actualización de estado, el orden del RNG y el sonido.

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

### Pegar, el daño y la muerte: `D7`, `15`, `16`, `17` y `9C`

CL9 hito 3 (2026-11-13). Cada paquete, contra la función del GameServer reconstruido
(`gameserver/src/reconstructed/win32-vc6`) que lo arma o lo lee (*estático* los dos lados); qué hace
el cliente con ellos, en [06](06-render-escenas-y-ui.md#pegar-al-llegar). Lo que no coincide, en
[08](08-pendientes.md) (P-C252).

- **El `D7` del cliente** (`0x5650c0`, `0x565a49..0x56601e`): `C1 07 D7 idH idL 78 dir`, con el
  XOR32 y por `send()` directo, sin `rand()`. `id` es `+0x1f4` del blanco, entero (el bit 15 viaja
  si lo tiene); la acción es siempre `0x78`, y `dir`, `ftol((yaw + 22,5) · 0,0222 + 1) & 7` con el
  yaw del héroe publicado ya girado hacia el blanco (`0x5659b8..0x5659e2`). La GS lo lee como
  `PMSG_ATTACK` (`CGAttack`, `0x4254d0`: `NumberH NumberL AttackAction DirDis`, el número en 16 bits
  y hasta `0x15df`) y contesta con el `18` de esa acción (`GCActionSend`): **coincide**. Antes sale
  el `D3` de girar, `C1 06 D3 X Y dir<<4`, con la celda del héroe (`0x56548b..0x565954`).
- **El `DF` del cliente, la celda del elfo** (`0x59cf88..0x59d3d7`, en el clic de ataque de `MoveHero`;
  *estático*, uno en el cable): `C1 05 DF X Y`, con el XOR32 y por `send()` directo, sin `rand()` propios (la
  celda lógica sale de `+0x41c` y `+0x420`, la Y leída antes que la X, 4 `rand()` cada una). Lo manda un elfo que
  camina (`+0x378`) sin interacción (`+0x379` en 0) con una ballesta en la derecha o un arco en la izquierda, antes
  de `0x59d3de` ([06](06-render-escenas-y-ui.md#pegar-al-llegar), "El clic de ataque"). FoxAnti no lo cambia (solo
  el `D3` y el `D7`). La GS reconstruida no tiene caso para el `DF` que llega: su posición es el `C1 11`
  (`CGPositionRecv`, `0x4250d0`), así que lo descarta (Q-C1163). MuRet: `Protocol::Combat::WriteElfCell` y
  `ServerSession::SendElfCell`.
- **El `15` de la GS** (`GCDamageSend`, `0x425880`): `C1 07 15 idH|f idL dañoH|tipo dañoL`. El
  cliente 1.02 **no tiene caso** para el `15`: el despacho cae en el default `0x4e5b4a`, que no
  hace nada. El daño que lee es el `D7` de 8 bytes (`0x4caae0`, arriba), el que manda la GS del
  laboratorio (*verificado*: 614 en el cable).
- **El `16`, la experiencia de una muerte** (`0x4cf200`, desde `0x4e3a74` con el flag de
  descifrado): `C3 [C1 09 16 idH|f idL expH expL dañoH dañoL]`, el `PMSG_KILLPLAYER_EXP` de
  `GCKillPlayerExpSend` (`0x425a70`, `PHeadSetBE`): **coincide**, con el `MSBFlag` en el bit 15 de
  la clave. Con el flag en 0 manda `C1 06 F1 03 00 16` en C3 y no lee nada (`0x4cf2aa`). El muerto
  es `0x5110d0(id & 0x7fff)`.
- **El `9C`, la experiencia de Blood Castle** (`0x4cfce0`, desde `0x4e3a93`, con el flag igual):
  `C3 [C1 0C 9C idH|f idL — eH eL dañoH dañoL]`, donde `eH` y `eL` son dos WORD en little endian
  en `pkt[6..9]` y `pkt[5]` es el relleno: la experiencia es `eH · 65536 + eL`. Es el
  `PMSG_KILLPLAYER_EXT` de `CalcSendRewardEXP` (`0x49af90`), con `HIWORD` y `LOWORD` en WORD
  nativos: **coincide**. Esa GS manda siempre `NumberH = NumberL = 0xFF` y el daño en 0: la clave
  `0x7fff` con el bit 15, que no tiene nadie, cae en el lugar 400 (Q-C1160).
- **El `17`, la muerte** (`0x4d0a30`, desde `0x4e3ab2`, sin flag): lee `pkt[3..4]` (la clave, sin
  máscara), `pkt[5]` (la skill, a `+0x391`) y, con la skill `0x28` o `0x3b`, `pkt[6..7]` (quién lo
  mató). La GS manda el `PMSG_DIEPLAYER` de **5 bytes** (`GCDiePlayerSend`, `0x425bb0`: solo
  `NumberH NumberL`): la skill y el que mató salen de lo que quedó en el buffer de recepción
  (Q-C1161).
### La acción: `18`

CL9 hito 1 (2026-11-13). *Estático*, con el ASM de `0x4cb750`, `0x576a60`, `0x576eb0`, `0x4f2250` y `0x59b456`,
cotejado con `CGActionRecv` (`0x425cd0`) y `GCActionSend` (`0x425c50`) del GameServer del laboratorio; sin captura
del cable con un emote ni sentado.

- **C→S `C1 05 18 [dir] [acción]`**, con el XOR32 en línea (`Protocol::Movement::WriteAction`). `dir` sale del
  ángulo `+0x24` de un personaje: `_ftol((ángulo + 22,5f) · 0,0222f + 1) & 0xff` y el resto con signo por 8, que es
  `& 7` (Q-C777). Lo mandan el héroe quieto que gira (`0x59b456`, con `0x7a`); sentarse (`0x4f2416` volando y
  `0x4f2834` por el piso, en `0x4f2250`), con `0x82` y el ángulo del que se sienta; y el emote (`0x576a60` y cada
  caso de `0x576eb0`, [06](06-render-escenas-y-ui.md#el-chat)), con la acción del caso y siempre el ángulo del héroe
  (`[0x743a170] + 0x24`): con el héroe parado salen dos iguales (Q-C776). Sin portar: `0x56c440`, `0x406050` (la
  ventana de comandos, CL9.4), `0x4295f0` dos veces (Fenrir) y `0x4e1010` (las misiones: el único que prende
  `0x200000` en `+0x7c`, solo en el héroe, pone la `0xca` y manda `0x82`).
- **S→C `C1 07 18 [clave:2, big-endian] [dir] [acción]`** (`0x4cb750`, desde `0x4e39ab` sin guarda de escena ni
  del flag): `0x5110d0` busca la clave (MOVSX: desde `0x8000` no la tiene nadie; sin nadie, el 400, Q-C778). Con el
  caballo oscuro (`+0x318` en `0x1c16`) no hace nada. Si no, `+0x24` = (dir − 1) · 45 (`0x6b87d4` y `0x6b8848`),
  `+0x378` (Movement) en 0 y la X y la Y en el centro de la celda blanco (`+0x39a`/`+0x39b` · 100 + 50, Q-C779), y
  según la acción (`acción − 0x12` contra `0x89`, las tablas `0x4cbd70` y `0x4cbcec`), con `0x4ea030`, que no pone
  la que el modelo no tiene ni repite la de ahora:

| Acción | Qué hace |
| --- | --- |
| `0x12` | la `0x98` y el sonido `0x41ed10(0x6f, 0, 0)` (CL10) |
| `0x78`, `0x79` | `0x4f4a10` (SetPlayerAttack, [06](06-render-escenas-y-ui.md#pegar-al-llegar)), `+0x381` en 1, `+0x114` en 0, `+0x3a4` con el WORD bajo de `[0x57678b0]` (el lugar del héroe) y `[0x5767964]` con el índice |
| `0x7a`, `0x7b` | `0x4f2250`, la de parado: sentado, la `0xca` y el `C1 18` de `0x82`; después su `rand()` |
| `0x7e` | la 5 |
| `0x80`, `0x81`, `0x82` | `0xc5`, `0xcb` y `0xc9`; con el elfo (`+0x1d4 & 7 == 2`), `0xc7`, `0xcc` y `0xca` |
| `0x83` a `0x91` | `0x99`, `0x9b`, `0x9d`, `0xa3`, `0xa1`, `0xa5`, `0xa7`, `0x9f`, `0xa9`, `0xab`, `0xad`, `0xaf`, `0xb1`, `0xb3` y `0xb5`, la del emote de esa acción; con el elfo, la siguiente |
| `0x92` a `0x9b` | `0xb7`, `0xb8`, `0xc0`, `0xb9`, `0xba`, `0xbb`, `0xbc`, `0xbd`, `0xbe` y `0xbf`, sin mirar la clase |
| cualquier otra | ella misma |

  No saca `rand()` salvo en `0x4f2250` (el sufijo) y en `0x4f4a10` (el `0x78` y el `0x79`): con un jugador
  (`CMP AX,0x20a` en `0x4f4a20`), `0x4f3f80` abre y cierra el registro propio (3.848 si queda sin accesos) y, de
  las ocho llamadas a `_rand` (`0x69fd70`), las de la rama que toque: a pie, por las armas, `0x4f4db9`, `0x4f4e07`
  o `0x4f4e1c`, y con el frame en 0 el sonido del arma (`0x4f5192`); con un monstruo, el `0x172` (`0x4f4fd7` y
  `0x4f4fed`), los subtipos `0xd1` a `0xd3` (`0x4f51d4`) y la tabla de sonidos del modelo (`0x4f5222`, 08 P-C253).
  Lo que los demás ven de un emote es la animación que se puso el que lo hizo.
- **El GameServer** (`CGActionRecv`): guarda la dirección y la acción sin validarlas y reenvía el `C1 07 18` con la
  clave del que la hizo a los jugadores de su viewport (`VpPlayer2`), no a él: el emote propio se ve por `0x576a60` y
  el de los demás por `0x4cb750`. Las acciones de descanso que mira son `0x6c`, `0x6d` y `0x6e` (`m_ViewState` 2 a 4
  y `m_Rest`): el `0x82` de sentarse del 1.02 no las prende (08, P-C152). `GCActionSend` (`MsgSendV2`) la manda de un
  ataque con `0x78` (`CGAttack`) y del escudo (la skill 18) con `0x12`; caminando en Blood Castle, `CGMoveRecv` le
  manda al propio jugador la `0x66`, que el cliente pone tal cual.

En MuRet: `Protocol::Movement::ReadAction`, `ServerSession::OnAction` y `Scenes::ApplyActionReply` (una clave
desconocida se descarta, como el `D3`; el `0x78`/`0x79` pega con `Scenes::SetActorAttack` y deja el lugar en
`WorldRuntime::lastAttacker`, CL9 hito 3, fase 2a); los que mandan,
`Scenes::SendTheAction` con `ServerSession::SendAction` (sin el GameServer abierto, un aviso y nada).

### Los cambios de mapa: `1C`, `F3 04` y `F3 12`

*Estático* salvo lo que se marca; qué hace el cliente, en
[06](06-render-escenas-y-ui.md#los-cambios-de-mapa).

- **El `1C` del servidor, el warp** (`0x4c6e10`): `C3 [C1 08 1C gate mapa x y dir]`, el
  `PMSG_TELEPORT_RESULT` del servidor (`GCTeleportSend`). `pkt[3]` es el gate (0, la habilidad
  teleport en el mismo mapa), `pkt[4]` el mapa, `pkt[5..6]` la celda y `pkt[7]` la dirección. Antes, el
  dispatcher lo copia a la cola de `0x77f27b4` (`0x4e3e98..0x4e3f1d`), cifrado o no. Con el
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
  cámara `CCameraMove`, el único camino a él ([06](06-render-escenas-y-ui.md#el-recorrido-de-la-cámara);
  *estático*): con la etapa 2 empieza 5.000 ms después. Los `BD 02` a `0C` llenan la interfaz de Crywolf
  ([Los eventos](#los-eventos-0b-46-47-909e-af-bc-bd-y-f3-222340)).

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

- **`24` del cliente**: `C1 0E 24 <grilla de origen> <lugar de origen> <ítem:7> <grilla de destino> <lugar de
  destino>`, con el XOR32 desde el byte 3 y en C3: 2 `rand()` (el relleno y el serial, `0x40b0a0`). Las grillas: 0 el
  inventario (y el equipo), 1 la propia del comercio (`0x78a92a8`, CL9 hito 7, fase 4), con el lugar sin el 12. Lo
  arman `0x449d80` y, en línea, `0x5ca46c..` (la grilla), `0x5bf294..` (el equipo), `0x59bf1c` (MoveHero) y, en
  `0x5ca250`, `0x5ce4fe..` (del comercio al inventario, de la 1 a la 0), `0x5cf438..` (del inventario al comercio, de
  la 0 a la 1) y `0x5d07fa..` (de la 1 a la 1). El ítem va como `+0` (el tipo & 0xff), `+4`, `+0x1a`, `+0x1b`,
  `+0x1c`, `(tipo con signo >> 5) & 0xf0` y 0. Antes, la traba `0x78abb39` en 1: el paso del equipo, el de la grilla
  y el del ítem del cursor no hacen nada hasta la respuesta. Del inventario al comercio, antes de mirar la traba, el OK
  propio (`0x78abac1`) en 0 y el `C1 04 3C 00` en C3 (`0x5cf438`); del comercio al inventario, después, con el OK
  propio en 0, la espera del OK (`0x78abac8`) en `0x96` (`0x5cf379..0x5cf396`, [06](06-render-escenas-y-ui.md#lo-social-en-pantalla),
  "Las grillas del comercio").
- **`24` del GameServer** (`0x4d1400`): `C3 0C 24 <resultado> <lugar> <ítem:7>`. Antes, el dispatcher lo copia a la
  cola de `0x77f27b4` (`0x4e38ca..0x4e394f`), también sin cifrar. Si llegó en C1, sin pasar por
  SimpleModulus, `0x4d166b..0x4d1690` manda `C1 06 F1 03 00 16` con el XOR32 y en C3 sin leerlo y sin soltar la
  traba. Cifrado, suelta la traba; con 0xff (rechazado), `0x5be150` vuelve el ítem del cursor a su lugar. Si no,
  con el lugar de origen (`0x78a6758`) desde `0x4c`, mueve su precio en el mapa de la tienda que se mira (`0x7245e4`
  con `0x6c931c` en 1, `0x7245e0` con 2, si está armado): lo saca y, con el lugar del resultado desde `0x4c`, lo pone
  ahí en lugar del que hubiera (`0x4d1b76..0x4d1c7d`); deja la cuenta del cursor en 0 y pone el ítem con
  `0x5bc740(lugar, ítem, …, 0)` según la grilla del resultado: 0 el inventario, 1 el comercio (`0x78a92a8`, 8 × 4,
  `0x4d1ccc`), 2 el almacén o la tienda (`0x78a7070`), 3 la caja del caos y 5 la caja del entrenador (`0x44ad70`).
  Después, el sonido `0x1d`. Por último, con o sin rechazo, el `3D` que llegó con la traba puesta (`0x5767968`, su
  cabeza en `0x57635bc`) se aplica con `0x4d57f0` y la marca vuelve a 0 (`0x4d1d70..0x4d1d85`).
- **`28`** (`0x4c5ec0`): `C1 05 28 <lugar> <fin de la espera>`. Con el lugar distinto de 0xff, `0x5bd750(lugar)` lo
  saca (desde `0x4c` lee antes `0x6c931c` y la grilla de la tienda personal); con el segundo byte distinto de 0,
  `0x5767954` (la espera del uso de un ítem) en 0.
- **`2A`** (`0x4d3cc0`): `C1 06 2A <lugar> <durabilidad> <fin de la espera>`. Antes, el dispatcher lo copia a la cola
  de `0x77f27b4` (`0x4e3ace..0x4e3b53`). Del 0 al 11, `+0x1a` del lugar con el
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
`ApplyItemPut` y `ApplyEquipmentChange`). El hito 7 de la CL9 suma la grilla 1 (los tres `24` del comercio, el `3C`
por `InventoryServices::sendGamePacket`, la respuesta con 1, el precio que sigue al ítem y el `3D` diferido; tests
`TradeGridsTest` e `ItemMoveReplyTest`). Cortan con su dirección las grillas de otras olas del `24` (`0x4d1d0a` y
`0x4d1d34`, CL9.3; `0x4d1d55`, CL9.3) y el equipo de un jugador del `F3 13` (`0x513a1e`, CL9.2); el `28` de la tienda
personal (`0x4c5ece`) es del hito 7, fase 4. Los tests usan ítems inventados.

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

### El comercio: `36` a `3D`

(CL9 hito 7, 2026-11-13, *estático*, contra los handlers del GameServer 0.97 reconstruido y los casos de `0x4e2b10`.)
Del GS al cliente (los tamaños son los que lee el cliente):

| Op | Bytes | Quién | Qué hace |
| --- | --- | --- | --- |
| `36` | `C1 0D 36 <nombre:10>` | `0x4d4af0` | Sin cifrar (`[EBP+0xc]` en 0), solo el aviso `C1 06 F1 03 00 36` en C3 (2 `rand()`). Cifrado: con la tienda personal propia o ajena abiertas (`0x800`, `0x20000`, sin `rand()`), el `C1 04 37 00` con el XOR32 en claro y nada más; si no, el nombre en `0x78aba1c` (con su 0 en `0x78aba26`), el diálogo `0x78` y, con un ítem en el cursor, `0x5be150` |
| `37` | `C1 14 37 <resp> <nombre:10> <nivel:2 LE> <gremio:4 LE>` | `0x4d5570` | 0: texto 492; 2: texto 493 (los dos tipo 2). 1: `Open(0x20)`, el pedido de `SetCursorPos` (08 P-C202), el zen y los OK en 0, `0x5bbdf0` si `0x78abaa5` está en 0 y el nombre cambió (`strcmp`), y el nombre, el gremio (`0x6c9324`) y el nivel (`0x78abab0`) |
| `38` | `C1 04 38 <lugar>` | `0x4e3d6a` | `0x5bd750(lugar, 0x78a5a20, 8)`: la copia (`0x5bc5e0`), la huella vacía y `0x5bc470`; el sonido `0x1d`. Con 0xff, solo el sonido. Un lugar desde 32 lee fuera de la grilla y un duodécimo ítem igual a su copia pisa la cuenta en la pila de `0x5bc470` (07 Q-C200 y Q-C961; el undécimo solo se pierde): MuRet descarta el paquete entero, sin sonido y con un aviso |
| `39` | `C1 0B 39 <lugar> <ítem:7>` | `0x4e3d99` | `0x5bc740(0x78a5a20, 8, 4, lugar, ítem, 1)` (`0x5bd307..0x5bd3ec`) y `0x5bc470`; el sonido `0x1d`. Una huella que pasa la casilla 31 o un duodécimo ítem igual a su copia (07 Q-C200 y Q-C961; el undécimo solo se pierde): MuRet descarta el paquete entero, sin sonido y con un aviso |
| `3A` | `C1 04 3A <sí>` | `0x4e3dd0` | `0x78abab8` con el zen mandado (`0x57678ac`) si es 1, o 0 |
| `3B` | `C1 08 3B 00 <zen:4 LE>` | `0x4e3df3` | el zen del otro en `0x78abab4` |
| `3C` | `C1 04 3C <estado>` | `0x4e3e0a` | 0 apaga el OK del otro, 1 lo prende, 2 apaga el propio; siempre el sonido `0x19` |
| `3D` | `C1 04 3D <resultado>` | `0x4d57f0` | Con la traba del `24` (`0x78abb39`) se guarda el primero (`0x5767968`, `0x57635bc`) para `0x4d1400`. Si no: 0 el texto 494 y la copia de cada ítem del otro; 2 el 495; 3 el 496 y el diálogo 0. Después la cuenta del cursor en 0, el cierre del diálogo `0x73` y `Close(0x20, 1)`, que no manda el `3D` |

Del cliente: el `36` (`C1 05 36 <clave BE>`, C3; `0x406050` y `0x56c440`), el `37` (`C1 04 37 <resp>`, XOR32 en claro;
el sí y el no del diálogo `0x78`, `0x62c20a` y `0x62d4a2`), el `3A` (`C1 08 3A 00 <zen LE>`, XOR32 en claro; el OK
del diálogo `0x73`, `0x626b2a..0x626eaf`), el `3C` (`C1 04 3C <OK>`, C3; `0x5ecde0`) y el `3D` (`C1 03 3D`, C3; el botón de
cancelar de `0x5ecde0` y `0x657fe1` al cerrar la ventana a mano).

Contra el GameServer 0.97 coinciden todos los tamaños menos uno: el `39` del GS lleva el `ItemInfo` de 4 bytes
(`PMSG_TRADE_OTHER_ADD`, `C1 08 39`, `GCTradeOtherAdd` `0x41e1a0`) y el cliente lee 7 desde `pkt + 4`; MyDll lo
reescribe a `C1 0B 39` con los 7 (`mydll_xlat_s2c_tradeadd_39`), como el resto de los ítems (08 P-C200). La ventana es la grilla de
`0x78a5a20` (la del otro, 8 × 4 de `0x48`) y la de `0x78a92a8` (la propia); `0x77f3dc8` guarda 32 copias de lo que el
otro sacó o tenía al cancelar, que `0x5bc470` compara (`0x5bc300`) para marcar con 99 lo que volvió peor (el
"anti-estafa"). Las dos grillas y la copia arrancan vacías: `0x5bbe10`, desde `0x633f20` al entrar a la selección,
pone `0x78abaa5` en 0 y las 96 entradas con el tipo en −1 y `+0x3c` en 0 (`0x5bc107..0x5bc13f`); después solo las
vacían `0x5bbdf0` (la copia) y el cierre de la ventana (`0x657fb5`, las grillas). El paso del inventario
(`0x5e513a..0x5e5205`) pone `+0x44` en cada casilla de las dos grillas (0 vacía, 1 con un ítem) y en la del otro
conserva el 99 (`0x5e5172`). Con la ventana abierta, el paso del comercio (`0x5ecde0`) mira los tres botones del pie
(el zen en `x + 0x1a`, el OK en `x + 0x61` y cancelar en `x + 0x89`, de `0x18` × `0x18` en `y + 0x186`): el OK con la
grilla del otro cambiada y el propio apagado abre el aviso `0x633300(4, 0x173, 0x94)` en vez de mandar el `3C`.

### La party: `40` a `44`

| Op | Bytes | Quién | Qué hace |
| --- | --- | --- | --- |
| `40` | `C1 05 40 <clave BE>` | `0x4e3f41` | la clave en `0x78aba4c` y el diálogo `0x77` |
| `41` | `C1 04 41 <resultado>` | `0x4e3f6f` | 0 a 5: los textos 497 a 501 y 535 (tipo 2); el diálogo 0 siempre |
| `42` | `C1 xx 42 <resultado> <cuenta> <miembro:0x18>…` | `0x4d67e0` | la cuenta en `0x78aba48` y cada miembro (nombre, número, mapa, x, y, dos bytes que no lee, vida y vida máxima LE) en su lugar de `0x77f3cd0`, de `0x24`, con 0 en `+0xa`. No mira el largo: con una cuenta que no entra lee pasado el paquete; MuRet devuelve `TooShort` y la sesión lo ignora con un aviso (Q-C42) |
| `43` | `C1 03 43` | `0x4e409b` | la cuenta en 0, el texto 502 y `0x6c8e50` en −1 si estaba en 0 o más |
| `44` | `C1 xx 44 <cuenta> <byte>…` | `0x4d6850` | por byte, `+0x18` del lugar del nibble alto con el bajo, hasta 10. Tampoco mira el largo: MuRet, `TooShort` como el `42` (Q-C42) |

Del cliente: el `40` (`C1 05 40 <clave BE>`, C3; `0x406050` desde `0x40739b` y `0x56c440`), el `41` (`C1 06 41 <sí>
<clave BE>`, C3; el diálogo `0x77`, `0x62bb1d` y `0x62cd52`), el `42` (`C1 03 42`, en claro, al abrir la ventana) y
el `43` (`C1 04 43 <número>`, XOR32 en claro; el botón de echar de `0x5e3c80`). Los tamaños coinciden con
`PMSG_PARTYLIST` y `PMSG_PARTYLIFEALL` del GS 0.97. Ni el `42` ni el `44` ponen tope: más de cinco miembros o un
nibble de 5 o más escriben pasados los cinco lugares (07 Q-C960); MuRet descarta esos paquetes enteros, con un aviso en
el log.

### El duelo: `AA` a `AD`

El GameServer 0.97 no los tiene (08 P-C200): el oráculo es el ASM del cliente.

| Op | Bytes | Quién | Qué hace |
| --- | --- | --- | --- |
| `AA` | `C1 10 AA <resultado> <clave BE> <nombre:10>` | `0x4dac90` | Con el resultado: el duelo en curso (`0x78abad8`), la clave (`0x78abadc`), el nombre (`0x78abae0`), el texto 912 con el nombre y el sonido `0x8d`; sin él, el nombre y el texto 913 |
| `AB` | `C1 0F AB <clave BE> <nombre:10>` | `0x4e4d10` | el duelo terminado, la clave, el nombre, el texto 914 y, con el tiempo del `F3 22` del tipo 2 (`0x5767814`), el tiempo en 0 (`0x5767816`; 08 P-C201) |
| `AC` | `C1 0F AC <clave BE> <nombre:10>` | `0x4e4d75` | con el diálogo `0x9a`, nada; si no, la clave, el nombre, el pedido (`0x78abad9`) y el sonido `0x8c` |
| `AD` | `C1 09 AD <clave1 BE> <clave2 BE> <p1> <p2>` | `0x4e4dd1` | los puntos (`0x78abaf8` el propio, `0x78abafc` el del rival) según cuál clave es la del rival |

Del cliente: el `AA` (`C1 0F AA <clave BE> <nombre>`, C3), el `AB` (`C1 03 AB`, C3; `/duelend` con el duelo en curso,
`0x56ea6c`) y el `AC` (`C1 10 AC <sí> <clave BE> <nombre>`, C3; `0x5ebca0`).

### Las tiendas personales: `3F`

(CL9 hito 7, fase 3, 2026-11-13, *estático*.) El GameServer 0.97 tampoco las tiene: el oráculo es el ASM del cliente,
sin cable (08 P-C200). `0x4e2b10` despacha el `3F` por el byte que sigue al opcode (`0x4e4e3a`: `pkt[3]` en un C1,
`pkt[4]` en un C2), y cada caso lee sus campos en lugares fijos del paquete, sin mirar el tipo. El personaje de una
clave es el primero vivo de los 400 con ella (`0x5110d0`, que da 400 sin ninguno); los carteles, el mapa de `0x7245c8`
(06 § Las tiendas personales).

| Sub | Forma | Lector | Qué hace |
|---|---|---|---|
| `00` | `C2 <tamaño:2> 3F 00 <n> n × (<clave BE> <título:36>)` | `0x4dae80` | por entrada, con el personaje de la clave, su cartel (`0x4511f0`); el título va hasta el primer 0 de sus 36 bytes (el 0 de la pila en el 37) |
| `02` | `C1 05 3F 02 <resultado>` | `0x4db700` | con 1, `0x78aba50` en 1 y el cartel del héroe (`MOVSX` de `+0x1f4`) con el título propio (`0x78aba64`); si no, el log "Fault ReceiveCreatePersonalShop" |
| `03` | `C1 07 3F 03 <resultado> <clave BE>` | `0x4e4ea5` | con 1 y el personaje de la clave: si es el héroe, `0x78aba50` en 0; su cartel sale (`0x451430`). Si no, el log `0x6c8504` |
| `08` | `C1 0F 3F 08 <?> <nombre:10>` | `0x4e4f5d` | `strncpy` del nombre (`pkt + 5`, 10) y `sprintf` del texto 1122 con él en los 0x70 bytes de la pila; a los mensajes con el tipo 1 (`0x555190`, sin nombre). `pkt[4]` no se lee |
| `10` | `C1 34 3F 10 <clave BE> <título:36> <nombre:10>` | `0x4dafe0` | con el personaje de la clave y su nombre igual (`strncmp` de 10 con `pkt + 0x2a`), su cartel |
| `12` | `C1 04 3F 12` | `0x4e4fcc` | con `0x6c931c` en 2 (mirando la tienda de otro), cierra `0x800` y `0x20000` y el texto 1126 con el tipo 2 |
| `01` | `C1 06 3F 01 <resultado> <lugar>` | `0x4db120` | con un resultado distinto de 1 y `0x6c931c` en 1 (la tienda propia): `0x5be150` con un ítem en el cursor, el lugar sale del mapa `0x7245e4` y el `C1 04 F3 10` en C3. Si no, nada; el log `0x6c84a0` siempre (fase 4) |
| `05` | `C2 <tamaño:2> 3F 05 <resultado> <clave BE> <?:10> <título:36> <n> n × (<lugar> <ítem:7> <precio:4 LE>)` | `0x4db890` | con 1: `Close(0x40)` y `Close(0x4000000)` si están abiertas, la copia del dueño (`0x78a6320`) en ceros, la grilla propia a su copia (`0x78ab0b8` → `0x78a6768`) si `0x20000` no estaba abierta, el título (`strncpy` de 0x24 a `0x78aba64`, 07 Q-C962), la grilla de la tienda vacía, `Open(0x20000)` y el mapa `0x7245e0` vacío; por entrada, con un precio de 0 o menos `Close(0x800)` y `Close(0x20000)` y sale; si no, `0x5bc740(0x78a9bb0, 8, 8, lugar, ítem, 0)` y el precio en el mapa. Al final, la copia del registro del dueño si `0x5110d0` lo encuentra. Con 3, el texto 1120 con el tipo 2; con otro, el log `0x6c8578`. `pkt[8..0x11]` no se lee (fase 4) |
| `06` | `C1 0F 3F 06 <resultado> <?:2> <ítem:7> <lugar>` | `0x4dbcb0` | con 1: mirando la de otro (`0x6c931c` en 2) el lugar de `0x78a6758` sale del mapa `0x7245e0` y de la grilla (`0x5bd750`), si no el mapa vacío; después lo comprado en su lugar (`0x5bc740` con el último argumento en 0). Con 6, el texto 1166 y `Close(0x800)` y `Close(0x20000)`; con 7, el 423; con 8, el 375 (los tres con el tipo 2); con 7, 8 u otro, `0x5be150` (fase 4) |
| `13` | `C2 <tamaño:2> 3F 13 <resultado> <?:0x30> <n> n × (<lugar> <ítem:7> <precio:4 LE>)` | `0x4dbb40` | con 1 y `0x6c931c` en 2: la grilla de la tienda vacía, el mapa `0x7245e0` vacío y cada entrada en su lugar con su precio, sin mirar el precio; si no, el log `0x6c85b0` (fase 4) |

Los ítems de los `3F` `05`, `06` y `0x13` van a las grillas del inventario con `0x5bc740` (`0x78a9bb0`), que no mira el
tope (07 Q-C200): una entrada que se pone con una huella fuera de su grilla hace que MuRet descarte el paquete entero,
antes de cerrar o abrir nada, con un aviso en el log (en el `05`, solo las de antes del primer precio de 0 o menos).

Del cliente: el `3F 05` (`C1 10 3F 05 <clave BE> <nombre:10>`, C3), el comando 2 de la ventana de comandos sobre el
elegido (`0x406b02`, 06 § "El comando sobre el elegido"; `Protocol::PersonalShop::WriteShopItemsRequest`) y `/purchase`
(`0x574ceb..`, `0x575570..` y `0x575e90..`, 06 § Las tiendas personales; `Scenes::RequestTheShopItems`); el `3F 07`
(`C1 10 3F 07 <clave BE> <nombre:10>`, C3), que manda `0x5eb020` al cerrar la tienda de otro con la clave del dueño
(`0x78a6514`) distinta de 0 (`WriteViewClosed`); y el `C1 04 F3 10` en C3 (`WriteInventoryRequest`), que mandan el `01`
de arriba y abrir una tienda (`0x65ac87` y `0x65b359`) con `0x6c931c` distinto de −1. Los de la ventana propia (fase 5,
06 § Las tiendas personales, "La ventana propia"), todos con el XOR32 desde el byte 3 y en C3: el `3F 01`
(`C1 09 3F 01 <lugar> <precio:4 LE>`, el byte bajo de `0x78a6758` y `0x77f3b6c`; `0x5dab08..` y `0x5dc05f..`,
`WriteSetPrice`), el `3F 02` (`C1 28 3F 02 <título:36>`, los 36 bytes de `0x78aba64` con un `MOVSD` × 9, lo que haya
detrás del terminador incluido, 07 Q-C965; `0x5ea407..` y `0x62378b..`, `WriteOpenShop`), el `3F 03` (`C1 04 3F 03`;
`0x5eaab1..`, `0x5cc2f7..`, `0x5d50b3..` y `0x5dc81c..`, `WriteCloseShop`) y el `3F 06` (`C1 11 3F 06 <clave BE>
<nombre:10> <lugar>`, la clave de `0x78a6514`, el nombre de `0x78a64f9` y el byte bajo de `0x78a6758`; `0x5d9c9e..`,
`WriteBuyItem`). Los tamaños de los que llegan son
los que leen los lectores (no hay un servidor que los mande). MuRet los lee en `Protocol::PersonalShop` (que solo mide
que el paquete los traiga) y los aplica en `Scenes::SocialEvents` (`ApplyShopTitles`, `ApplyShopCreated`,
`ApplyShopClosed`, `ApplyShopNotice`, `ApplyOtherShopTitle` y `ApplyShopOwnerLeft`) y, los de las listas, en
`Scenes::PersonalShopLists` (`ApplyPriceResult`, `ApplyShopItemList`, `ApplyShopPurchase` y `ApplyShopRefresh`, con
`OpenThePersonalShop` y `CloseThePersonalShop`, 06 § Las tiendas personales). El `12` cierra ahora por `0x5eb020`.

**MuRet**: `Protocol::Trade`, `Protocol::Party` y `Protocol::Duel` leen y escriben lo de arriba;
`App::ServerSession::RegisterSocialHandlers` los pasa al mundo y `Scenes::SocialEvents` hace lo de cada caso. Los
botones están en `Scenes::GameSocialSteps` (comercio y duelo), `GamePartyWindow` (echar) y `GameDialogSteps` (sí y no
de `0x77` y `0x78`); `/trade`, `/duelstart` y `/duelend`, en `UI::ChatCommands`, hasta donde necesitan un
personaje elegido. La entrada a la selección porta la cola de `0x5bbe10` (`Scenes::ClearTheTradeForSelection`). Lo
que queda de las tiendas personales (su dibujo y los carteles) lo dice 08 P-C208; levantar y soltar en
las grillas del comercio está portado (06
§ Lo social en pantalla, "Las grillas del comercio"), con el clic derecho sobre la propia (fase 5, 08 P-C235). Los tests (`TradePartyDuelTest`, `SocialEventsTest`, `GameSocialStepsTest` y los
de los diálogos, la party, los comandos y el registro de las ventanas) usan nombres inventados.
### Los eventos: `0B`, `46`, `47`, `90`..`9E`, `AF`, `BC`, `BD` y `F3 22`/`23`/`40`

(CL9 hito 11, 2026-11-13, *estático*, contra el ASM y contra `gs_protocol.h` y `protocol.cpp` del GameServer
reconstruido.) Todos llegan por `0x4e2b10` sin guarda de escena, de estado de la sesión ni del flag C3. Los formatos
de Text.bmd los arma `sprintf` de la CRT (`0x69fd9b`) o `wsprintfA` (`[0x6b851c]`) según el handler: el primero
entiende `"% d"` (la bandera del espacio) y el segundo no (P-C290, *inferido*): copia el espacio y la letra y no toma
el argumento, así que los textos con `"% s"` y `"% d"` del 91 salen sin el nombre ni el número. Un `%d` que toma el
nombre del evento (un puntero a su registro, `0x76bd1dc + 300 · índice`) imprime la dirección (Q-C1321).

| Paquete | Handler | Lo que lee | GameServer 1.02 |
| --- | --- | --- | --- |
| `C1 05 0B estado evento` | `0x4e3594` | guarda la copia en la cola de `0x77f27b4` (como el `1C`); con evento 1 o 3, la tormenta (`0x723c50`) en 1 o 3 si estado ≠ 0, y siempre los 40 pájaros apagados (`0x4455c0`) | `PMSG_EVENTSTATE` (`GCEventStateSend` `0x416560`, `GCMapEventStateSend` `0x4165c0`), igual |
| `C1 46 tipo atributo modo cuenta …` | `0x4dab90` | tipo 1: `cuenta` celdas `(x, y)` desde `pkt[7]`, `0x5f8730` (quita) con modo ≠ 0 y `0x5f8710` (agrega) con 0. Tipo 0: `cuenta` rectángulos `(x1, y1, x2, y2)`; en Blood Castle (11..17) con el atributo 8, antes el derrumbe `0x5ff930(mapa, 0x24, 0, 1)`; cada uno con `0x5f8750(x1, y1, x2 − x1 + 1, y2 − y1 + 1, atributo, 1 − modo)` en un byte (Q-C1320). Otro tipo, nada | `PMSG_SETMAPATTR_COUNT` (Blood Castle `0x497cb0`, `0x497ea0`, `0x498090`): el tipo 0 con cuenta 1, el atributo 4 (la entrada y la puerta) u 8 (el puente), `btMapSetType` = vivo ? 0 : 1 y dos pares `(x, y)`, que el cliente lee como un rectángulo; el largo cubre seis pares (`7 + 2 · 6`) y los otros 8 bytes no los lee |
| `C1 47 clave:2 … ítem:4 nivel` (hasta `pkt[0xc]`) | `0x4d6990` | la clave big-endian; con la del héroe, nada; con otra, el aviso con el nombre del ítem (`0x5b1c70`) | no está; el oráculo es el cliente |
| `C1 04 90 resultado` | `0x4e4870` | cierra el NPC de evento (`0x65cae0(0x1000, 0)`) y, con 1..5, el aviso 677, 678, 686, 687 o 679 | `PMSG_RESULT`, igual |
| `C1 91 tipo …` | `0x4da8c0` | tipo 1 (Devil Square): `pkt[4]` 0 → 643, si no 644 con los minutos; tipo 2 (Blood Castle): 850 o 851 con el nombre 1146; tipo 4 (Chaos Castle): la palabra `pkt[4]:pkt[6]` y `pkt[5]`, con 850 + 1156 o 1164 + 851 en los registros 1154/1155 y el diálogo `0x9a` | `PMSG_ENTERCOUNT_RESULT` (`CGEnterCountRequest` `0x429c10`), sólo los tipos 1 y 2: el 4 no lo manda |
| `C1 04 92 tipo` | `0x4e4930` | con el objeto del evento (`[0x723c60]`), `0x40ba60(pkt[3] + 1)`: `+8` y `GetTickCount` en `+0xc`, que dibuja la cuenta de 30 s (`0x40bab0`) | `PMSG_SET_DEVILSQUARE`: Devil Square 0..2 y Blood Castle 3..6; el 10..13 de Chaos Castle no lo manda |
| `C1 93 puesto cuenta registros…` | `0x4e4959` → `0x4e7360` → método `+0xc` | Devil Square (`0x40bd20`): con cuenta < 200, `cuenta` registros de 24 bytes a `+0x2c` y el diálogo `0x8b`; Blood Castle (`0x40c0c0`) con cuenta `0xff` y Chaos Castle (`0x404da0`) con `0xfe`: uno, con los diálogos `0x97` y `0x9b` | `PMSG_DEVILSQUARERESULT` de 11 registros (`0x10d`); Blood Castle manda puesto 1, cuenta `0xff` y `0x35` bytes. Con más de 11, Q-C1324 |
| `C1 94 tipo cuenta:2 [a:2 b:2 c:2]` | `0x4dd8a0` | cierra las ventanas, `0x78abb04` = cuenta y `0x78abb00` = tipo + 1; con el 1 (Muto) los tres números si ninguno es `0xffff`; con el 3, el número de serie en cero y su entrada (`0x4dd8ce..0x4dd920`: `0x553570(0)`, el chat cerrado, un solo campo para el Tab y `0x77f3b6b` en 1; 06 § La ventana de los chips) | `PMSG_EVENTCHIPINFO` (`0xc`), igual |
| `C1 06 95 tipo cuenta:2` | `0x4e49d4` | `0x78abb04` = cuenta salvo `0xffff` | `PMSG_EVENTCHIP_RESULT`, −1 al fallar, igual |
| `C1 96 … a:2 b:2 c:2` | `0x4e49fb` | los tres números (`0x6c9328`, `0x6c932c`) si ninguno es `0xffff` | `PMSG_EVENTCHIP_MUTO`; al fallar a = −1, que el cliente salta |
| `C1 04 99 tipo` | `0x4e4a40` | con 0 o 1, los diálogos `0x8f` o `0x90` | `PMSG_RESULT`, igual |
| `C1 04 9A resultado` | `0x4da780` | cierra las ventanas y, con `sprintf`: 1 → 854; 2 → 852 con 1146; 3 → 686; 4 → 687; 5 → 853 con 1146 y 10; 6 → 867 con 6; 8 → 852 con 1147; 9 → 853 con 1147 y 100 | `PMSG_RESULT`, igual |
| `C1 0E 9B estado quedan:2 máx:2 actual:2 dueño:2 arma` | `0x4e4999` → método `+8` | Blood Castle (`0x40c140`): 0 gira a los jugadores (`0x5111a0(0xc0)`) y la música `0x8b`; 0, 1 y 4, `+0x10` = estado + 1, el tiempo y las cuentas (`0x40ba80`) y el arma en `+0x360` del dueño (`0x511150`, Q-C1326); 2, `0x40ba20` y la música apagada; 3, el derrumbe `(mapa, 0x24, 0x14, 1)`. Chaos Castle (`0x404e20`), 5..10: la trampa armada, la entrada, la música; el tiempo; el fin; los tres derrumbes con cuatro rectángulos del atributo 8 cada uno. Devil Square, `ret` | `PMSG_STATEBLOODCASTLE` (`0xe`), igual |
| `C1 44 9D resultado datos:0x40` | `0x4e4a86` | el aviso 886 + resultado (0..4) o 899 (5) y siempre los `0x40` bytes en `0x78ab078` | no está |
| `C1 06 9E … sonido:2` | `0x4e4ad4` | con 0, 1 o 2, el sonido `0x5b`, `0x5c` o `0x5d` (CL10) | no está |
| `C1 05 AF 01 resultado` | `0x4e5032` → `0x4daa90` | como el 9A con Chaos Castle (1147, 100), y 7 → 423 | no está en el 0.97; el oráculo es el cliente |
| `C1 05 BC 00/01 resultado` | `0x4e4547`, `0x4da540` | el texto de la mezcla (1801/1800 con 1816 y 868, o 1817, 1811, 1812, 1815) en `"%s%s %s"`, `0x6236f0(0)` y `0x6236f0(0xa6)`, y la ventana de la mezcla (`0x402e50`, del hito 6: `Scenes::ReturnToTheJewelChoice`) | no está |
| `C1 06 BD 00 estado etapa` | `0x4e5965` | `0x42fe80(estado, etapa)` (la interfaz de Crywolf; con otro estado que el anterior, la luz y el `.att` del mapa en curso, `0x42ff81` y `0x42ffe8`, [06](06-render-escenas-y-ui.md#los-objetos-de-crywolf), Q-C1340) y `0x4033b0(etapa)` en el recorrido de la cámara | no está: el GameServer 0.97 no tiene Crywolf y el oráculo es el ASM del cliente |
| `C1 0D BD 02 luz:4 a0 a1 a2 a3 a4` | `0x4e5990` → `0x4e0fe0` → `0x430000` | la luz a `0x723c08` y los cinco altares a `0x6c73cc..0x6c73d0` (`.data` en 2), en cualquier mapa | no está (Crywolf) |
| `C1 08 BD 03 resultado valor clave:2` | `0x4e59a5` → `0x4e1010` | el resultado del contrato de los altares, con la clave big-endian (`pkt[6]·256 + pkt[7]`), en cualquier mapa: con 0, el héroe sin `0x200000` en `+0x7c`, el nivel (`+0xe`) con el registro propio abierto (0xf08 `rand()` al cerrarlo) y `0x435b00(0x36)` + `(0x37)` con menos de 350 o `(0x3a)`; con 1, `0x435b00(3)` + `(4)`, `0x435ad0(clave − 0x13c, valor)` (sin tope, Q-C1347), `0x200000` en `+0x7c`, la acción `0xca` (`0x4ea030`, con las acciones del modelo del tipo del héroe: un héroe transformado cuyo modelo no tiene la `0xca` se queda con la suya) y el `C1 05 18 rumbo 82` de abajo; con otro, nada. `0x435b00` y `0x435ad0` solo hacen algo con la etapa 3 o 4 en el 34 (06 § La interfaz de Crywolf) | no está |
| `C1 06 BD 04 a minutos` | `0x4e59ba` → `0x435a20` | con la etapa 4 (`0x4359f0`) en el 34 (`0x4303c0`): `0x723be4` = a, `0x723be8` = minutos, `0x7239f4` = `GetTickCount` y `0x723bf8` en 1 | no está |
| `C1 09 BD 05 vida:4 n` | `0x4e59eb` → `0x435a70` | con la etapa 3 o 4 (`0x435a00`) en el 34: `0x723bd8` = n; la barra `0x723a3d` en 1 con la vida en más de 0 y en 0 con 0 o menos; `0x6c73d4` = vida (`0x436590`) | no está |
| `C1 0C BD 07 rango … exp:4` | `0x4e5a09` → `0x437600` | `0x723bed` = `pkt[4]`, `0x723bf0` = el dword de `pkt+8` | no está |
| `C1 BD 08 n registros de 0x14…` | `0x4e5a39` → `0x4e1680` → `0x437620` | por registro (desde `pkt+5`): los puntos (`+0xc`) a `0x6c73e4 + 4 · lugar`, la clase (`+0x10`) a `0x723a30 + lugar`, el nombre (`+1`, 10 bytes y el 0) a `0x7239f8 + 11 · lugar` y `0x723bf9` en 1. El lugar (`+0`) no tiene tope (Q-C1341) | no está |
| `C1 05 BD 09 b` | `0x4e5a25` | `0x7d2dc9d` = b (lo lee la caja del caos, `0x5ff2f2`) | no está |
| `C1 BD 0C … a b c d` | `0x4e59d6` → `0x4e15f0` → `0x430040` | un solo efecto `0xee` (`0x518cf0`, CL11) en (`pkt[8]` · 100, `pkt[9]` · 100, 100) con el héroe de dueño, el caso 2 con `pkt[6]` en `0x7a` o `0x74`, el 1 con `0xb7` y el 0 con otro, y un segundo punto corrido con z 800; no escribe estado ni saca `rand()` fuera de `0x518cf0` (leído entero en la fase 4); MuRet corta | no está |
| `C1 F3 22 b w:2` | `0x4e31f8` | el byte de `pkt[4]` a `0x5767814` y la palabra de `pkt[6]` a `0x5767816` | `PMSG_F3_22` (`0x418330`, la guerra de gremios) mide 6: la palabra la lee fuera del paquete (Q-C1322) |
| `C1 16 F3 23 nombre:8 p1 nombre:8 p2` | `0x4d9f00` | los nombres con su `'\0'`, los puntos, `0x576796f` = p1 ≠ `0xff`, y el efecto `0x60e` en la celda del héroe (`0x518cf0`, corta) | `PMSG_BATTLESOCCER_SCORE` (`0x16`): la palabra de `pkt[0x15]` toma `pkt[0x16]` (Q-C1323) |
| `C1 07 F3 40 comando a b` | `0x4da070` | por la tabla `0x4da24c`: 0, el efecto `0x60e` en la celda (`0x518cf0`, corta); 1, el aviso 650 + a (810 + a desde `0x14`); 2, el sonido `0x5a`; 3, 710 + a; 5, el diálogo a de `0x632f70` (`UI::OpenTheAnswersDialog`; sin tope, Q-C1325); 6, 449; 7, con a en 0 el texto 2020 en el diálogo `0xaa` y si no el aviso 2020 + a; `0xd`, 1826; `0x2f..0x31`, 1776 + comando | `PMSG_F3_40`, igual |

Lo que manda el cliente:

- **`C1 03 97`** (`0x56b4e8..0x56b5a0`), en claro y sin XOR32: `SendMove` lo agrega después del `D3` con `0x78abb00`
  distinto de 0; después, con 3, cierra la entrada del número de serie (`0x56b6a9..0x56b72a`), y pone `0x78abb00`
  en 0, el cursor con `SetCursorPos` en x · ancho / 260 (Q-C1330), `0x6c8dcc` en 6 y el izquierdo suelto. El GS lo
  atiende en `CGEventChip97Request` (apaga `m_IfState` del tipo 9). También lo manda, en claro, la X de la ventana
  de `0x78abb00` (`0x5e72e3`, el paso `0x5e6520`), que después cierra el inventario (`0x65cae0(0x4000000, 0)`).
- **`C1 04 98 01`** (`0x5e65f6..0x5e67a0`), **`C1 05 95 tipo casilla`** (`0x5e68ed..0x5e6bc9`) y **`C1 12 9D
  a:4 0 b:4 0 c:4 0`** (`0x5e6db9..0x5e7184`), los tres con el XOR32 y en claro, desde el paso de la ventana de
  `0x78abb00` al soltar el clic (`0x7d366ec`) sobre su botón (fase 4 del hito 11; *estático*; 06 § La ventana de los
  chips). El `98` con el tipo 4 (el número del Muto): el GS lo atiende en `CGEventChip98Request` (`0x42a6e0`), que no
  lee el cuerpo y le pide el número al servidor de los chips. El `95` con 1, 2 u otro que no sea 3: el tipo es
  `0x78abb00` − 1 en un byte y la casilla la de `0x5583a0` para el `0x1c15` de nivel 1 (con 2) o 0, sin sumarle nada;
  sin el ítem no sale. El GS (`CGEventChipRegRequest`, `0x429fc0`) acepta los tipos 0 y 1 y le suma 12 a la casilla.
  El `9D` con 3, si `0x558740(−1, −1)` encuentra alguna casilla libre (si no, el aviso 896): los 12 bytes del texto del
  EDIT de `[0x5763504]` (GetText, la vtable + `0x50`) en tres pedazos de 4, cada uno con un 0; detrás del `'\0'` de un
  texto más corto el original deja la pila del paso y MuRet ceros (P-C247). El GS 0.97 no atiende el `9D`: el oráculo
  es el ASM del cliente. MuRet: `Protocol::Events::WriteMutoNumberRequest`, `WriteEventChipRegister` y
  `WriteSerialNumber`.
- **`C1 05 90 cuadrado casilla`** (`0x5e5b2f..0x5e5eb4`, el paso de la ventana de Devil Square) y **`C1 05 9A puente
  casilla`** (`0x5e6156..`, la de Blood Castle), con el XOR32 y en claro (fase 2; *estático*). El cuadrado va de 0 a
  5 y la casilla de la invitación (`0x1c13`) más `0x18`; el puente de 1 a 7 y la casilla de la capa (`0x1a12`) más
  `0xc` (06 § Las ventanas de los NPC de evento). El GS reconstruido resta 12 a la casilla del `90` (la del
  inventario, de 12 + casilla) y acepta los cuadrados 0 a 3; al `9A` le resta 1 al puente y usa la casilla tal cual.
  MuRet: `Protocol::Events::WriteDevilSquareEntry` y `WriteBloodCastleEntry`.
- **`C1 06 AF 02 x y`** (`0x4fbee8`, desde `0x4fbe10`), con el XOR32: en Chaos Castle, el héroe 10 s en la misma
  celda con el bit 8 del atributo. `MOV CL,0x2` en `0x4fbf6a`: no es el `AF 01` que decía 06.
- **`C1 06 BD 03 alto bajo`** (`0x435d1c..0x4361a2`, el paso `0x435bd0`), con el XOR32 y en claro: el Sí de la
  ventana del contrato de Crywolf (`0x723a44` en 1), sin la mascota `0x1c14`, `0x1c15` o `0x1c37` en `+0x318` del
  héroe; los bytes son `SAR 8` y el bajo de `0x723be0`, la clave del altar con signo que dejó `0x435b00` (fase 4 del
  hito 11; *estático*: GS 0.97 no tiene Crywolf). MuRet: `Protocol::Events::WriteCrywolfContract`.
- **`C1 05 18 rumbo 82`** (`0x4e11e1..0x4e15bd`), con el XOR32 y en claro: el `BD 03` con el resultado 1, con el rumbo
  de `+0x24` del héroe por las cuentas de `0x56b0b7` (`0x4e1260..0x4e128a`) y la acción `0x82`; es el mismo `18` del
  héroe quieto (`0x59b333`, que manda `7A`). MuRet: `Protocol::Movement::WriteAction` y `DirectionFromAngle`.

**MuRet**: `Protocol::Events` lee los 21 con `TooShort` cuando faltan los bytes que el original lee siempre (los que
dependen de un campo se miran al aplicar) y escribe el `97` y el `AF 02`. `App::ServerSession::RegisterTheEventPackets`
los registra en un bloque y `Scenes::ApplyEventPacket` (`Scenes/EventPackets.cpp`) los aplica sobre
`WorldRuntime::events` (`EventGlobals`), el objeto del evento, las celdas de `TerrainWall`, los diálogos y la caja de
mensajes; `UI::FormatOriginal` hace las dos funciones de formato con los argumentos que pasa el original. Los cortes
están en 08 (P-C291 y P-C292). Los tests usan textos inventados, salvo los formatos de `Text.bmd` que se citan
arriba.

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
las skills (`19`, `1E`, `D3`, `DF` y `26` de las cuatro funciones por clase) son de la fase 2 (08, P-C264; los `19` y
`1E` de la rama del caballero, abajo). Los tests usan claves inventadas.

### Las skills que se mandan: `19` y `1E`

(CL9 hito 4, fase 2c, 2026-11-13, *estático*, contra `protocol.cpp` del GameServer reconstruido.) Las funciones que
mandan (`0x55b6f0`, `0x55a368`, `0x561f60`) y las ramas de las clases de `0x57cfc0` los arman en línea, con el XOR32
desde `pkt[3]`, y los mandan cifrados como C3: el relleno (`0x69fd70`) y el serial (`0x40ae60`, `0x40b0a0`,
`0x5767913`) sacan dos `rand()` antes de mirar el socket (`+0xc` de `0x6c82a0`). Lo que hacen y cuándo, en
[06](06-render-escenas-y-ui.md#usar-una-skill) y [Las skills del caballero](06-render-escenas-y-ui.md#las-skills-del-caballero).

| Paquete | Quién | Layout que manda el cliente | GameServer 0.97 |
| --- | --- | --- | --- |
| `19` | `0x55b6f0` (`0x55bf36..0x55c622`), `0x55a368` (la opción `0x12`) | `C1 06 19 <skill> <clave H L>`: la clave entera del blanco (`+0x1f4`) | `PMSG_MAGICATTACK` (`CGMagicAttack`, `0x425f60`) lee 7 bytes, con un `Dis` que el cliente no manda; el repack lo espera como `A5`. *Estático* |
| `19` | las ramas del caballero (`0x584e09..0x5851b0` y las otras tres) | `C1 06 19 <skill> <clave H L>`: las `0x30`, `0x3f`, `0x40` y `0x4b` mandan la clave del jugador (`0x57678bc`) | `CGMagicAttack` (`0x425f60`) lo pide cifrado (`DataEncryptCheck`) y lee la skill y la clave; `PMSG_MAGICATTACK` tiene un `Dis` más que no lee. El repack lo recibe como `A5` |
| `1E` | `0x561f60` (`0x562b36` y `0x563f5a`) | `C1 0C 1E <skill> <X> <Y> <rumbo> <hacia> <espalda> <clave H L> 00`: la celda lógica de c (la Y se lee antes); la `0x18`, el rumbo · 256/360 y dos ceros; la `0x36`, el rumbo · 255/360, la celda hacia el blanco (`((dx + 8) << 4) \| ((dy − 8) & 0xf)`, cada eje entre −8 y 7) y el rumbo + 180°. La clave es la de `0x558eb0` con `0x7758588`, o `0xffff` | `PMSG_DURATION_MAGIC_RECV` (`CGDurationMagicRecv`, `0x427870`) lee 7: los cinco de después son del cliente (Q-C1211). *Estático* |
| `1E` | las ramas del caballero (`0x582439..0x5896a3`) | `C1 0C 1E <skill> <x> <y> <rumbo> <hacia> <espalda> <clave H L> <b11>`: la celda lógica del héroe, el rumbo (`· 256/360`; la `0x38`, `· 1/360 · 255`), en el de "hacia" la posición del blanco y en el de "espalda" la celda de `0x4e9920` o el rumbo opuesto, la clave del elegido y en `b11` la clave de la magia (`0x57cf90`, la `0x4c`) | `CGDurationMagicRecv` (`0x427870`) lo pide cifrado y lee los cuatro primeros (`PMSG_DURATION_MAGIC_RECV`); `EnableSkill` (`0x457694`) deja `0x29`, `0x2a` y `0x37`, no `0x38`, `0x3e`, `0x41` ni `0x4c` (Q-C1230). También `A6` |

**MuRet**: `Protocol::Skill::WriteMagicAttack` y `WriteDurationMagic` (`EncryptedC1`; `DurationMagicSend` lleva en
`magicKey` el byte 11, 0 salvo en la `0x4c` del caballero); los dos `rand()` del C3 los saca quien los manda
(`Scenes::SendTheSkillPacket`, `Scenes::UseTheKnightSkill`). Los tests usan claves inventadas.

### Lo que manda la rama del Wizard: `19`, `1E`, `1C` y `B0`

(CL9 hito 4, fase 2c, 2026-11-13, *estático*, contra `protocol.cpp` del GameServer 0.97 reconstruido.) La rama del
Wizard de `0x57cfc0` y `0x55cbc0` ([06](06-render-escenas-y-ui.md#la-rama-del-wizard)) los arman en línea como los de
arriba: el XOR32 desde `pkt[3]` y cifrados como C3, con el `rand()` del relleno (`0x69fd70`) y el del serial
(`0x5767913`) antes de `send()`, aunque el socket esté cerrado. Sin "webzen" en el nombre del héroe publicado; el `19`,
además, con el freno de 300 ms (`0x576791c`, que la `0x28` no mira).

| Paquete | Quién lo manda | Layout | GameServer 0.97 |
| --- | --- | --- | --- |
| `19` | `0x58a1e5`, `0x58a954` y `0x58ba70` (Wizard) y `0x55cbc0` | `C1 06 19 <skill> <clave H L>`: la `0x3a`, la Soul Barrier sin elegido y la `0x28` sin la clave de `0x558eb0` mandan la del jugador (`0x57678bc`) | `CGMagicAttack` (`0x425f60`), como el de arriba |
| `1E` | `0x58c1b6`, `0x58cf37`, `0x58dcc4`, `0x58ef1e`, `0x58fced`, `0x590b18`, `0x591859` (Wizard) y `0x55e67e` | `C1 0C 1E <skill> <x> <y> <rumbo> 00 <espalda> <clave H L> <clave de la magia>`: `<espalda>` en 0 salvo en la `0x4c` (la celda de `0x4e9920`); la clave de la magia (`0x57cf90` en línea) en la 8, la 9 y la `0x4c`, 0 en las demás; sin blanco, la clave `0xffff`. El rumbo es `ftol(ángulo · 256/360)` (`0x6ba440`) | `CGDurationMagicRecv` (`0x427870`) lee la skill, X, Y y el rumbo: lo demás no lo usa (08, P-C221; Q-C1211) |
| `1C` | el Teleport (`0x593151..0x59350e`) | `C1 06 1C 00 <x> <y>` | `CGTeleportMagicRecv` (`0x426450`): el 0 es la skill 6 |
| `B0` | el Teleport Ally (`0x59293b..0x592d1b`) | `C1 07 B0 <clave L H> <x> <y>`: el WORD `+0x1f4` del elegido tal cual, en little endian | `CGTargetTeleportRecv` (`0x426870`) la arma con `NumberH \| NumberL << 8`, igual |

**MuRet**: `Protocol::Skill::WriteMagicAttack`, `WriteDurationMagic` (con la clave de la magia en
`DurationMagicSend::magicKey`), `WriteTeleportMagic` y `WriteTargetTeleport`; los `rand()` los saca
`Scenes::SendTheSkillPacket`. El `D3` de girar de `0x55cbc0` es el `WriteWalk` de un paso, sin C3 ni `rand()`.

### Lo que manda la rama del elfo: `19`, `1E` y `D3`

(CL9 hito 4, fase 2c, 2026-11-13, *estático*.) La rama del elfo de `0x57cfc0` y `0x55faa0`
([06](06-render-escenas-y-ui.md#la-rama-del-elfo)) los arman en línea como la del Wizard: C3 con sus dos `rand()`
antes de `send()`, sin "webzen" en el nombre del héroe publicado, y el `19` con el freno de 300 ms.

| Paquete | Quién lo manda | Layout | GameServer 0.97 |
| --- | --- | --- | --- |
| `19` | las invocaciones (`0x57f2fe`), las `0x1a` a `0x1c` (`0x57fadb`) y la `0x35` (`0x58021d`); `0x55faa0` (`0x56018f`, `0x5608fd`, `0x56109a`) | `C1 06 19 <skill> <clave H L>`: la rama manda la clave del jugador (`0x57678bc`); `0x55faa0`, la del blanco (`+0x1f4` de `0x7758588`) | `CGMagicAttack` (`0x425f60`) |
| `1E` | la `0x34` (`0x57e2c8..0x57ef4e`) y la `0x4c` (`0x580af3..0x5817a5`) | `C1 0C 1E <skill> <x> <y> <rumbo> 00 <espalda> <clave H L> <clave de la magia>`, como el de la rama del Wizard: la `0x34` deja la espalda y la clave de la magia en 0 y manda la clave de `0x558eb0` con `0x7758588` (o `0xffff`); la `0x4c`, la celda de `0x4e9920` y la clave de `0x57cf90` | `CGDurationMagicRecv` (`0x427870`) lee la skill, X, Y y el rumbo (Q-C1211) |
| `D3` | `0x55faa0` (`0x55fac2..0x55ffcf`) | el giro de un paso de `0x55cbc0`, con la celda lógica del héroe publicado (la X antes que la Y) y su ángulo de antes de girar (Q-C1214) | como el de `0x55cbc0` |

| `10` (FoxAnti: `1D`) | `0x517a20` (`0x517c74..0x5184c7`), desde el choque de las flechas de skill (`0x520497`) | `C1 [8 + 3n] 10 <skill> <x> <y> <tanda> <n>` y, por blanco, `<clave H> <clave L> <extra>`: la skill del lugar, la celda del punto (`ftol(· 0,01f)`, el byte bajo), `+0x8c` de la flecha, hasta cinco claves (`+0x1f4`) y el sexto argumento (0 desde la flecha). CL9 hito 4 | `CGBeattackRecv` (`0x426db0`) pide el `1D` (`PMSG_BEATTACK_COUNT`: la skill, x, y, el serial y la cuenta, con la cuenta en 5 como mucho) pero lee entradas de dos bytes (`PMSG_BEATTACK`): desde el segundo blanco toma otra clave (07, Q-C1219). *Estático* |

**MuRet**: `Protocol::Skill::WriteMagicAttack` y `WriteDurationMagic` con `Scenes::SendTheSkillPacket`, el `D3` con
`SendTheTurn`, el de la rama del Wizard, y el `10` con `WriteSkillHits` desde `Scenes::HitTheTargetsAround`.

### Lo que manda la cola de las 0x43 a 0x48: `19` y `1E`

(CL9 hito 4, fase 2c, 2026-11-13, *estático*, contra `protocol.cpp` y `ObjUseSkill.cpp` del GameServer 0.97
reconstruido.) La cola de `0x57cfc0` (`0x5808da..0x599d5d`, [06](06-render-escenas-y-ui.md#la-cola-de-las-0x43-a-0x48),
de todas las clases) los arma en línea como las ramas: C3 con sus dos `rand()` antes de `send()` y sin "webzen" en el
nombre del héroe publicado; el `19`, además, con el freno de 300 ms, y el `1E` sin él.

| Paquete | Quién lo manda | Layout | GameServer 0.97 |
| --- | --- | --- | --- |
| `1E` | la `0x43` (`0x594137..0x594de1`) | `C1 0C 1E 43 <x> <y> <rumbo> <hacia> <espalda> <clave H L> 00`: la celda lógica del héroe (la X se lee antes), `ftol(+0x24 · 256/360)`, la celda hacia el blanco de `0x5ae300` (desde la celda lógica, cada eje entre −8 y 7), la de `0x4e9920` (desde la posición), la clave de `0x558eb0` con `0x7758588` (o `0xffff`) y la clave de la magia de `0x57cf90(0)`, siempre 0 (Q-C1228) | `CGDurationMagicRecv` (`0x427870`) lee los cuatro primeros, pero `EnableSkill` (`0x457680`) no tiene la 67 en su lista: la descarta. No la usa nadie: el ASM del cliente es el oráculo |
| `19` | las `0x44` a `0x48` (`0x594e42..0x599d1c`, dos copias cada una) | `C1 06 19 <skill> <clave H L>`: con `0x6c8e30` en −1, la del jugador (`0x57678bc`); si no, el WORD `+0x1f4` del elegido, sin mirar si se lo puede atacar (Q-C1229) | `CGMagicAttack` (`0x425f60`): la skill tiene que estar entre las del personaje (`gObjGetMagicSearch`) y en su clase (`SkillGetRequireClass`); `UseSkill2` (`0x457b30`) cobra el maná y la stamina si `RunningSkill` (`0x457c70`) da 1, y como no tiene un caso para 68 a 72 va al `default` (`0x457f7a`): `gObjAttack` sobre la clave que llega (sin elegido, la del propio jugador). Sin lo que hacen en 1.02 (Stern, Swell Mana, Invisibility...): el ASM del cliente es el oráculo |

**MuRet**: `Protocol::Skill::WriteMagicAttack` y `WriteDurationMagic` (`magicKey` en 0) con
`Scenes::SendTheSkillPacket`, desde `Scenes::UseTheSharedTail`. Los tests usan claves inventadas.

### Las órdenes del cuervo: `A7` y `A8`

(CL9 hito 4, 2026-11-13, *estático*.) El GameServer 0.97 no tiene el Dark Lord y no los manda (P-C262): el oráculo es
el cliente. Lo que hacen está en [06](06-render-escenas-y-ui.md#las-órdenes-del-cuervo).

- **`A7`** (`0x4e4c0a` → `0x421b50`): `C1 07 A7 <?> <orden> <clave H L>`. `pkt[3]` no lo lee nadie.
- **`A8`** (`0x4e4c42` → `0x421b70`): `C1 09 A8 <?> <tipo> <dueño H L> <blanco H L>`. `pkt[3]`, igual.
- **`A7` que sale**: `C1 07 A7 <mascota> <orden> <clave H L>`, con el XOR32 desde `pkt[3]` y por `send()` directo
  (sin cifrar como C3). El paso del cuervo (`0x416a20`, en `0x416b05..0x417095` y una copia en `0x417113..0x4176a9`)
  lo arma cuando el blanco de `+0x3b0` o el dueño se esconden en un ataque: `C1 07 A7 00 78 FF FF` (la mascota es el
  `+0x444` del cuervo, que `0x4167b0` pone en 0; la orden `0x78`, la primera skill del cuervo; sin blanco). El
  GameServer 0.97 no lo lee. **MuRet**: `Protocol::Pet::WritePetCommand`.
- **`A7` de las órdenes** (`0x420f70`, desde `0x57cfc0` con el derecho y el lugar de `0x78` a `0x7b`, fase 2b): con
  `0x78` a `0x7a`, `C1 07 A7 00 <lugar − 0x78> FF FF`; con `0x7b`, si `0x558bb0` deja atacar al elegido y es un jugador
  o un monstruo, `C1 07 A7 00 03 <clave H L>` con la clave del elegido (`+0x1f4`). Las órdenes van de 0 a 3, como las
  que el `A7` que llega le pasa a `0x416510`; la del paso (`0x78`) no es una de ellas (Q-C1209). **MuRet**:
  `Scenes::UseTheHeroSkill` ([06](06-render-escenas-y-ui.md#usar-una-skill)).

Ninguno mira el largo. **MuRet**: `Protocol::Pet::ReadPetOrder` y `ReadPetAttack` (TooShort si faltan campos),
registrados con las skills; `Scenes/PetOrders` los aplica. El `A9` de respuesta (`0x4e4ca1` → `0x421db0`): `C1 0C A9
<tipo> <clase> <casilla> <nivel> <?> <experiencia: 4, little endian>`, sin mirar el cifrado ni el largo; `pkt[7]` no lo
lee nadie. **MuRet**: `Protocol::Pet::ReadPetInfoReply` (TooShort sin los 12 bytes) y `Scenes::ApplyPetInfo`
([06](06-render-escenas-y-ui.md#el-a9-de-las-mascotas)). Abrir el panel de la mascota manda `A9 00 00 01` y `A9 01 00
08` (`0x659ff6..0x65a8a3`), y abrir el inventario con un Dark Lord, los mismos dos (`0x65c039..0x65c946`, CL9 hito 7).

### Las misiones y los NPC: `9F`, `A0`..`A3` y `30`

(CL9 hito 2, 2026-11-13, *estático*, contra `protocol.cpp` del GameServer reconstruido: `GCQuestInfoSend`
`0x42a7c0`, `GCQuestStateSend` `0x42a920`, `CGSetQuestState` `0x42a9c0`, `GCQuestResultSend` `0x42aa70` y
`CGTalkRequestRecv`.) Los handlers están en línea en el dispatcher (`0x4e4b3a`, `0x4e4b6d`, `0x4e4b9f`, `0x4e4ba9`
y `0x4e4bf2`, copias de `0x4ddaa0`, `0x4ddad0`, `0x4ddb00`, `0x4ddb40` y `0x4ddb80`), sin guarda de escena ni del
cifrado, y no miran el largo.

- **`9F`** (`0x4e4b3a`): `C1 05 9F <i> <v>`; con `i` de 0 a 3, `[0x710148] + 6 + i = v`. Nadie más lee esos
  bytes en lo portado. El GS 0.97 no lo manda; el `C1 9F` que arma `0x41b170` (la pestaña de la ventana) es código
  muerto (Q-C845).
- **`A0`** (`0x4e4b6d` → `0x418e20`): `C1 xx A0 <cuenta> <estados...>`. Copia `cuenta` bytes desde `pkt[4]` sobre
  los 50 de `+0x2134c` (después de ponerlos en 0) y busca entre las `cuenta` primeras misiones la primera no
  terminada. El GS manda `Count = i`, el índice de la última misión cargada (3 con las cuatro del `quest.bmd`), y
  un largo de `i / 4 + 4`: con las cuatro, ningún byte de estado (Q-C840). El cliente copia igual los 3 bytes que
  siguen en el buffer; MuRet los pone en 0 (08, P-C171). El A0 se pide con `C1 03 A0` (C3) en el primer paso del
  juego con `+4` en `0xff` (`0x63693a`) y al llegar a un NPC (`0x5676a6`).
- **`A1`** (`0x4e4b9f`): `C1 05 A1 <misión> <estado>`. `0x418ee0` escribe el byte entero `misión >> 2` con
  `estado`: el GS manda `GetQuestStateBYTE`, el byte con las cuatro misiones (Q-C843). Después `0x657a90` (cierra
  todos los paneles) y `0x4198b0(−1)`.
- **`A2`** (`0x4e4ba9`): `C1 06 A2 <misión> <resultado> <estado>`; con resultado 0, lo mismo que el `A1` con
  `pkt[5]`. El cliente lo manda (`C1 05 A2 <misión> 1`, C3) desde el paso de la ventana: las respuestas (`0x419bd0`,
  la acción 1 cuando las condiciones de `0x418fb0` se cumplen y la 3 sin mirarlas) y el botón de terminar
  (`0x41a9e0`, con los ítems de `0x419b10`). MuRet: `Scenes::StepTheQuestWindow` (CL9 hito 2, fase 5b;
  [06](06-render-escenas-y-ui.md#las-misiones)).
- **`A3`** (`0x4e4bf2` → `0x4ddb80`): `C1 07 A3 <claveH> <claveL> <tipo> <valor>`, también al viewport
  (`MsgSendV2`). La clave va `& 0x7fff` a `0x5110d0` (el 400 sin nadie). El sonido `0x66` y, por tipo: `0xc8` y
  `0xca` suman `valor` a `+0x68` del registro propio si es el héroe; `0xc9` pone la clase `((v & 0x10) | (v >> 4))
  >> 1` en `+0x1d4` del personaje y, si es el héroe, en `+0xb` del registro; `0xcb` solo el efecto. Los cuatro
  siguen con quince vueltas de `0x530860` con el personaje de dueño, el ancho 40 y la key 2: el `0x60f`/0 en su
  posición y, salvo en `0xc8`, el `0x60f`/`0xa` desde la posición con la z + 200 (`0x6b8928`, copiada una vez antes de
  las vueltas), que gira como el /0 pero baja (`0x53b2d5`). Después el calco `0x61e`/0 de `0x518cf0` (el de subir de
  nivel). `0xc8` termina con el sonido `0x65`; las otras tres, con `0x513100` (la ropa de la clase, Q-C1024), la acción
  `0xb8` (`0x4ea030`) y otra vez el `0x66`. Otro tipo: solo el sonido.
- **`30`** (C→S, `0x567c2d..0x567db5`, y las copias de `0x568288`, `0x568908` y `0x568f54`): `C1 05 30 <claveH>
  <claveL>` con la clave `+0x1f4` del NPC (MOVSX, el alto con SAR), C3. Lo manda la llegada al NPC elegido
  (`0x5650c0` con `+0x379` en 2; 06, "Hablar con un NPC"): cada uno saca el `rand()` del relleno y el del serial.
  El GS contesta con el `C3 30` (`PMSG_TALKRESULT`) que abre la ventana del NPC (hito 6).
- **`31`** (C→S, `0x419900`): `C1 03 31` en claro, al cerrar la ventana de las misiones (`CGCloseWindow`).

**MuRet**: `Protocol::Quest` (lectores y escritores), `App::ServerSession` los registra juntos y el sink los aplica
con `Scenes::ApplyQuest*` (06, "Las misiones"); el `A0` y el `30` de la llegada, `Scenes::TalkToTheNpc`. Un paquete corto se ignora con un aviso. El `A3`
entero (joints `0x60f`/0 y /`0xa`, calco, `0x513100` en `Scenes::ApplyTheClassParts` y la acción) desde el 2026-11-18;
sin Player.bmd, un jugador corta antes de `0x513100`.

### Los monstruos y los NPC que entran: `13`, `1F`, `68` y `1A`

(CL9.R del hito 2, 2026-11-13, *estático*; los handlers se registran en la fase 2.) Contra
`PMSG_MONSTER_VIEWPORTCREATE` y `PMSG_CALLMONSTER_VIEWPORTCREATE` del GS reconstruido (`user.cpp`, `gObjViewportProc`).

- **`13`** (`0x4c9c60`): `C2 xx xx 13 <n>` y `n` elementos de `0x10` desde `pkt[5]`: `+0` y `+1` la clave (el bit 7
  de `+0`, el `m_State` 1 del GS, decide entre la creación nueva y la que ya existía, `0x4c9f4c`), `+2` el tipo alto
  (`& 3`) con `& 0x80` en `+0x42d` y `(>> 4) & 7` en `+0x93`, `+3` el tipo bajo, `+4..+7` los estados (`0x4e6380`),
  `+8` y `+9` la celda, `+0xa` y `+0xb` el blanco, `+0xc` la dirección `<< 4`. Por elemento: `0x5141e0(tipo, x, y,
  clave & 0x7fff)` (un 0 corta el lazo entero), `0x4e6380`, el ángulo `+0x24` (`(dir − 1) · 45` con los casos del
  `0x1b5`), las celdas protegidas `+0x41c`/`+0x420` y `+0x39a`/`+0x39b`, en Castle Siege el `0x115` con
  `0x4ea030` y `0x42a110`, y sin el bit 7 el camino de `0x4eac00` (`+0x378`) o, con él, `0x4c9790`. El GS 0.97 arma
  elementos de `0xc` (`Type` de un byte en `+2`, `ViewSkillState` de dos en `+4`): no es el cliente 1.02 (08, P-C172).
- **`1F`** (`0x4ca0b0`): `C2 xx xx 1F <n>` y `n` elementos de `0x18`: la clave como el `13`, el tipo entero en `+2` y
  `+3` (el alto primero), los estados en `+4..+7`, la celda en `+8`/`+9`, el blanco en `+0xa`/`+0xb`, la dirección
  (arriba) y el PK (abajo) en `+0xc` y el nombre del dueño en `+0xd..+0x16` (`0x4ca132` y `0x4ca547`). Los tipos
  `0x98..0x9e` van por `0x517400` (el nivel, `tipo − 0x97`, y `wsprintfA` con el nombre del caso como formato y el
  dueño), los demás por `0x5141e0` con el nombre del dueño, el Text 485 y el del monstruo; el recién llegado, con el
  efecto `0xec` (`0x518cf0`) y el joint `0x5d5` (`0x53fe70`), CL11.
- **`68`** (`0x4e0980`): `C2 xx xx 68 <n>` y `n` elementos de `0x1c`: `+0` el tipo de registro, `+1` la clase o el
  tipo alto, `+2` el tipo bajo, `+3`/`+4` la clave (el alto primero, sin el bit 15), `+5`/`+6` la celda, `+7..+0x17`
  el equipo (los 17 bytes de la lista) y `+0x18..+0x1b` los estados. Con 1 (`0x4e0c1d`), un jugador: `0x512870(clave,
  0x20a, x, y, 0)` con la clase de `+1` y el equipo (`0x5139f0`); con 2 o 3 (`0x4e09ca`), el monstruo del tipo
  `+1 << 8 | +2` por `0x5141e0`; con otro, nada. No tiene recién llegado ni camino: el ángulo es `rand() % 360`.
- **`1A`** (`0x4cedd0`): no crea nada: es la skill que pega a varios (`PMSG_MAGICATTACK_COUNT_SEND`). `C1 xx 1A`, la
  clave del que pega en `pkt[3..4]` (el alto primero, entera), la skill en `pkt[5]` (va al `+0x38e`), la celda en
  `pkt[6]` y `pkt[7]` (que nadie lee), la cuenta en `pkt[8]` y las claves de los blancos desde `pkt[9]`, de a dos
  bytes con el alto primero (`PMSG_MAGICATTACK_COUNT_INFO`). Lo que hace con el mundo, en
  [06](06-render-escenas-y-ui.md#la-skill-que-pega-a-varios).

El dispatcher llama a los tres sin copiarlos a la cola de `0x77f27b4` ni mirar el flag de cifrado (`0x4e37d1`,
`0x4e37e9` y `0x4e4643`). Lo que cada handler hace con el mundo, en
[06](06-render-escenas-y-ui.md#los-monstruos-y-los-npc-en-el-viewport).

**MuRet** (CL9 hito 2, fase 2): `Protocol::Viewport` lee los tres con los offsets del ASM (`ReadMonsters`,
`ReadSummons` y `ReadObjects`; un registro corto es `TooShort`, hasta el último byte que lee el handler: `+0xc`,
`+0x16` y `+0x1b`); `App::ServerSession` los registra después del `DF` y los pasa al mundo
(`Scenes::ApplyViewportMonsters`, `ApplySummons` y `ApplyViewportObjects`). Los tests arman los paquetes con los
structs del ASM (`support/ViewportPackets.h`). El `1A` lo lee `ReadMagicAttackCount` (`TooShort` sin la cuenta o
sin el último byte de la última clave), lo registra `App::ServerSession` y lo aplica `Scenes::ApplyMagicAttackCount`
con la clave del jugador.

### Los ítems del piso: `20`, `21`, `22`, `23`, `26` y `2C`

(CL9 hito 5, 2026-11-13, *estático*, contra `gs_protocol.h` y `protocol.cpp` del GameServer 0.97 reconstruido. El
cable de la sesión del laboratorio queda para la VM, 08 P-C270.) El dispatcher (`0x4e2b10`) no mira si llegaron
cifrados: el `22` y el `23` llegan en C3 o en C1, igual. Los siete bytes de un ítem son los del `F3 10`, y la entrada
del piso es la de `0x220` bytes de `0x77f46c8` (1000), con el objeto en `+0x4c`.

- **`20`** (`0x4d0ba0`): `C2 <largo:2> 20 <cuenta> {<clave:2, alto primero> <x> <y> <ítem:7>}`, de a 11 bytes desde
  `pkt[5]`. La entrada es `clave & 0x7fff` (la 0 si pasa de 999) y el bit 15 dice "recién caído". Cada una va a
  `0x608350(entrada, ítem, ((x + 0,5) · 100, (y + 0,5) · 100), recién caído)`, en orden. El GameServer 0.97 arma
  entradas de 9 bytes (`PMSG_ITEMVIEWPORTCREATE`, el ítem de 5): con él, el cliente lee las entradas corridas
  (07 Q-C1240).
- **`21`** (`0x4e3849`, en el dispatcher): `C2 <largo:2> 21 <cuenta> {<clave:2, alto primero>}`. Apaga `+0x4c` de la
  entrada de la clave entera (sin la máscara del `20`), con las de 1000 o más en la 0. El GameServer usa el mismo
  par de bytes (`PMSG_VIEWPORTDESTROY`, *inferido* por el struct).
- **`22` del cliente** (el caso 1 de `0x5650c0`, `0x566755`): `C1 05 22 <índice:2, alto primero>`, con el XOR32
  desde el byte 3 y en C3 (2 `rand()`). Antes, el índice en `0x6c82e8`; mientras no vuelva a −1, no sale otro. Es
  `PMSG_ITEMGETREQUEST` del GameServer, de 5 bytes.
- **`22` del GameServer** (`0x4d0cc0`): `C3 <largo> 22 <resultado> …`. Con `0xfe`, el zen en `pkt[4..7]` con el
  byte alto primero (`+0xecc` del registro propio, abierto); con un lugar (o `0xfd`, "en otro lado"), los siete
  bytes del ítem desde `pkt[4]`; con `0xff`, nada. El sonido (CL10) sale de `pkt[4..]` como un ítem, también con el
  zen. El GameServer 0.97 lo manda en C3 de 8 bytes (`PMSG_ITEMGETRESULT`, `Data[4]`): el ítem de un lugar sale
  corto (07 Q-C1240). El zen de comprar, vender o reparar llega así también (`GCMoneySend`, `C3 08 22 FE <zen:4>`):
  el zen y el aviso no leen más allá de `pkt[7]` y se aplican; el sonido, que lee el tipo de `pkt[4..10]`, no.
- **`23` del cliente** (`0x5dfe27..0x5e0685`): `C1 06 23 <x> <y> <lugar>`, la celda del terreno bajo el mouse
  (`0x5fc260(1)` y `0x5fac90`, como MoveHero) y el byte bajo de `[0x78a6758]`, de donde salió el ítem del cursor;
  con el XOR32 y en C3. Es `PMSG_ITEMTHROW`, de 6 bytes.
- **`23` del GameServer** (`0x4d10d0`): `C1 05 23 <resultado> <lugar>` (`PMSG_ITEMTHROW_RESULT`). Con 0, `0x5be150`
  vuelve el ítem del cursor; si no, el lugar sale del equipo (0 a 11, con el registro abierto) o de la grilla
  (`0x5bd750`), y la cuenta del cursor queda en 0. Las dos ramas dejan `0x6c82ec` en −1. Con 0 no lee `pkt[4]`
  (`0x4d10de`).
- **`26` del cliente** (`0x5a2c01..`, `0x5a3623..`, `0x5a3da8..`, `0x5a452f..` y `0x5c5937..`): `C1 06 26 <lugar>
  <destino> <[0x78abb26]>`, el lugar + 12 de la casilla, 0 y el byte que ponen dos diálogos de `0x623e70`; con el
  XOR32 y en C3. `PMSG_USEITEM` del GameServer 0.97 es de 5 bytes, sin el tercero (07 Q-C1241). Lo mismo mandan los
  libros y las esferas (`0x5c91a3..`) y el `0x1a0b` (`0x5c84e1..`) desde la grilla. La joya sobre un ítem
  (`0x5d83ee..`, CL9 hito 5, fase 2) lleva un destino: `C1 06 26 <[0x78a6758]> <[0x77f3cc8]> <[0x78abb26]>`, el lugar
  de la joya y el del origen del ítem, + 12; con los mismos dos `rand()` del envío.
- **`2C`** (`0x4de500`): `C1 06 2C <resultado> <puntos> <stat>`. Con 0 o 3 y un stat de 0 a 4 (la energía, la
  vitalidad, la agilidad, la fuerza y el comando), suma o resta los puntos con el registro abierto; otros resultados
  abren un aviso. El GameServer 0.97 manda `C1 04 2C <resultado>` (`PMSG_USE_STAT_FRUIT`): sin los puntos ni el stat,
  el cliente lee lo que quedó en su buffer (07 Q-C1240).

**MuRet** (CL9 hito 5): `Protocol::FloorItems` lee los cinco que llegan y escribe los tres que salen;
`App::ServerSession` los registra en un bloque y manda el `22`, el `23` y el `26` (`SendPickUp`, `SendDrop` y
`SendUseItem`). Un `22`, un `23` o un `2C` más cortos de lo que lee el cliente no llegan al mundo, salvo lo que el
original escribe en todos sus caminos (`ShortPickUpResult`, `ShortDropResult` con `pkt[3]` si llegó, y
`ShortStatFruit`; 07 Q-C1240). Del `22` con `0xfe` corto llegan el zen y el aviso (`ShortZenPickUp`,
`Scenes::ApplyShortZenPickUp`, desde el 2026-11-20: antes el zen de una compra en la tienda no bajaba).
Un `20` corto no escribe nada fijo y se ignora entero. Lo que hace el mundo con cada uno, en [06](06-render-escenas-y-ui.md#los-ítems-del-piso). Los tests
usan ítems inventados.

### Los otros jugadores: `12`, `14`, `45`, `25` y `DF`

(CL9 hito 1, 2026-11-13, *estático*; el cable queda para la VM, P-C160.) Los cinco llegan del GameServer cuando
otros jugadores entran, salen o cambian a la vista del héroe. La clave es la del objeto en el GameServer, en dos bytes
con el alto primero; el bit 15 marca al recién llegado (el `12` y el `45`) y los otros tres la leen entera. Lo que
cada handler hace con el mundo, en [06](06-render-escenas-y-ui.md#los-otros-jugadores-en-el-viewport).

- **`12`** (`0x4e372e` → `0x4c8860`): `C2 <largo:2> 12 <cuenta> <registro:0x2c>…`. El dispatcher copia antes el
  paquete entero, con su largo, a una cola de diez (`0x77f27b4`, la misma del `0B`, el `1C`, el `24` y el `2A`; nadie
  la lee), aunque no se pueda leer. Cada registro:

  | offset | qué | lo lee |
  | --- | --- | --- |
  | `+0`, `+1` | la clave, el alto primero; el bit 15, recién llegado | `0x4c8886..0x4c88ad` |
  | `+2`, `+3` | la celda X e Y | `0x4c8973..0x4c8986` |
  | `+4` | el estado (1 a 4) en el nibble de abajo; la clase, `((b & 0x10) \| (b >> 4)) >> 1` | `0x4c89f3` |
  | `+5..+0x15` | el equipo, los 17 bytes de la lista de personajes | `0x5139f0` (`0x4c91e9`) |
  | `+0x16`, `+0x17` | nadie | — |
  | `+0x18` | las banderas de efectos, 32 bits, el bajo primero | `0x4e6380` y `0x42ab40` |
  | `+0x1c..+0x25` | el nombre, sin terminador si lo llena | `0x4c88b0` y `0x4c9212` |
  | `+0x26`, `+0x27` | adónde camina | `0x4eac00` |
  | `+0x28` | la dirección (nibble alto) y el PK (bajo) | `0x4c8a1a`, `0x4c8f21` |
  | `+0x29..+0x2b` | nadie | — |

- **`14`** (`0x4ca780`): `C1 <largo> 14 <cuenta> <clave:2>…`. Las claves van enteras: `0x4ca7a2` compara la
  palabra con `0x13d` y `0x4ca7e1` le saca el bit 15 para la baja.
- **`45`** (`0x4c92a0`): `C2 <largo:2> 45 <cuenta> <registro:0x1c>…`, el jugador transformado: la clave y la celda como
  el `12`, el tipo en `+4..+5` (el alto primero), `+6` y `+7` sin leer, las banderas en `+8` (32 bits), el nombre en
  `+0xc..+0x15`, adónde camina en `+0x16` y `+0x17` y la dirección y el PK en `+0x18`; `+0x19..+0x1b` sin leer.
- **`25`** (`0x4c8380`): `C1 0C 25 <clave:2> <ítem:7>`, el equipo que se ve de otro. La clave entera (`0x5110d0` con
  el bit 15 no encuentra a nadie); del ítem, el byte 1 (el lugar arriba, el nivel abajo), el 3 (el excelente,
  `& 0x3f`) y el 4 (el set), y `0x54b3a0` el tipo del 0, el 3 y el 5. El último que se lee es `pkt[10]`; el byte 6
  (`pkt[11]`) no lo lee nadie.
- **`DF`** (`0x4c6b40`): `C1 07 DF <clave:2> <X> <Y>`, la celda nueva de otro sin camino: la clave entera.

**Contra el GameServer 0.97 reconstruido** (CL9.R, `gs_protocol.h`). Solo el `14` coincide: `PMSG_VIEWPORTDESTROY`
son los dos bytes de la clave. `PMSG_VIEWPORTCREATE` (el `12`) mide `0x20`: `CharSet[11]` desde `+4` y un `WORD
ViewSkillState` en `+0x10`, con el nombre en `+0x12`; el cliente 1.02 lee registros de `0x2c`, con 18 bytes desde `+4` y
las banderas de 32 bits en `+0x18`. `PMSG_VIEWPORTCREATE_CHANGE` (el `45`) mide `0x16`, con un `Skin` de un byte en
`+4` y el `WORD` en `+6`; el cliente lee `0x1c`, con el tipo de dos bytes y las banderas de cuatro en `+8`.
`PMSG_USEREQUIPMENTCHANGED` (el `25`) mide 9, con `ItemInfo[4]`; el cliente lee hasta `pkt[10]`. El `DF` no tiene
constructor en el GameServer 0.97. MuRet sigue el ASM del cliente; lo que mande un GameServer 1.02 real queda para la
VM (08, P-C160).

**Captura real del anillo (2026-11-15):**12 con32 bytes y45 con22 por registro en esta sesión.
En45, el nombre coincide con el offset12 del cliente, aunque falta su sufijo de destino/dirección;
no se clasifica como el layout0.97 de arriba sólo por compartir tamaño. El campo de flags8..11 ya
contiene20 y el actor queda inmóvil. [Datos, A/B y límites](14-transformacion-en-vmxp.md).

**MuRet** (CL9 hito 1): `Protocol::Viewport` lee los cinco con los offsets del ASM (`ReadPlayers`, `ReadLeave`,
`ReadTransformed`, `ReadEquipmentShown` y `ReadPosition`; un registro corto es `TooShort`, hasta el último byte que
lee el handler); `App::ServerSession` los registra juntos después del `D7` y los pasa al mundo
(`Scenes::ApplyViewportPlayers`, `ApplyViewportLeave`, `ApplyTransformed`, `ApplyEquipmentShown` y
`ApplyViewportPosition`). Los tests arman los paquetes con los structs del ASM y nombres inventados
(`support/ViewportPackets.h`).

### El gremio, la alianza y la guerra: `50`..`67` y `E1`..`EB`

(CL9 hito 8, 2026-11-13, *estático*, contra el GameServer 0.97 reconstruido; la alianza, `E1`..`EB`, no la tiene:
el ASM del cliente es el oráculo.) `0x4e2b10` los reparte sin mirar la escena ni el flag de cifrado. El `55` y el
`5C` caen en el `default`: el cliente los ignora (el GS 0.97 manda el `5C`, `GCGuildViewportNowPaint`). Las
funciones de cada uno y lo que hacen están en [06](06-render-escenas-y-ui.md#el-gremio-la-alianza-y-la-guerra).

| Opcode | Función | Formato (lo que lee) |
| --- | --- | --- |
| `50` | `0x4d74b0` | `C1 05 50 <clave:2>`, el byte alto entero (sin `& 0x7f`) |
| `51`, `53`, `56`, `60` | `0x4d7a20`, `0x4d7c30`, `0x4d7ff0`, `0x4d8150` | `C1 04 <op> <resultado>` |
| `52` | `0x4d7b60` | C2: `pkt[5]` la cuenta, el DWORD de `pkt[8]` el puntaje (negativo, 0), `pkt[0xd..0x14]` un nombre de gremio y las entradas desde `pkt[0x18]`, de `0xd` bytes: el nombre (10), el número, el servidor (bit 7 conectado, los 7 de abajo; sin él, `0xff`) y el puesto. `pkt[4]` no se lee |
| `54` | `0x4e4279` | sin campos: abre el panel `0x40000` |
| `5D` | `0x4e42f0` | `C1 05 5D <clave:2>` con el byte alto `& 0x7f` |
| `61` | `0x4e4382` | `C1 0C 61 <gremio:8> <tipo>` |
| `62` | `0x4d8260` | `C1 0D 62 <gremio:8> <tipo> <equipo>` |
| `63` | `0x4d8870` | `C1 0C 63 <resultado> <gremio:8>`; el nombre no se lee |
| `64` | `0x4e4400` | `C1 06 64 <nuestro> <suyo> <x>`; `pkt[5]` no se lee |
| `65` | `0x4e42af` → `0x4d9260` | C2 (*inferido*: lee la cuenta de `pkt[4]`): entradas de `0xc` bytes desde `pkt[5]`: `<gremio:4 LE> <puesto> <+0x369> <relación> <clave:2>`, el byte alto `& 0x7f`; `+9..+0xb` no se leen (fase 2) |
| `66` | `0x4e42c7` → `0x447b90` | `C1 39 66 x <clave:4 LE> x <alianza:8> <gremio:8> <marca:32>` |
| `67` | `0x4e44aa` (en línea) | C2 como el `65`: entradas de `0x14` bytes desde `pkt[5]`: `<clave:2>` entera (sin `& 0x7f`), `x x`, `<gremio:4 LE> <relación> <alianza:8>`; `+0x11..+0x13` no se leen (fase 2) |
| `E1` | `0x4d9810` | `C1 05 E1 <tipo> <resultado>` |
| `E5` | `0x4e4444` → `0x6565e0` | `C1 07 E5 <relación> <pedido> <clave alta> <clave baja>` |
| `E6` | `0x4d9940` | `C1 08 E6 <relación> <pedido> <resultado> <clave:2 BE>` |
| `E9` | `0x4d9e50` | C2: `pkt[4]` la cuenta, `pkt[5]` el resultado; con 1, las entradas desde `pkt[8]`, de `0x29` bytes: un byte, la marca (32) y el nombre (8) |
| `EB` | `0x4e4486` → `0x4d9b80` | `C1 <op> EB <tipo> [<resultado>]`: `pkt[4]` solo con el tipo 1 |

Lo que manda el cliente, por el socket del GameServer (`0x6c82a0`) sin la capa del `C3`:

- `C1 06 51 <sí> <clave:2>` con el XOR32 (`0x4d6b60`, y `0x4d754d` con el diálogo ocupado: el 0).
- `C1 03 52`, sin campos (`0x4d7d95`, `0x6599a0(0x10)`).
- `C1 04 54 <respuesta>` con el XOR32 (`0x655d70` con Crear: el 1; `0x6570b0` al cerrar la ventana 2 y la página 0 y
  la confirmación antes de cerrarla: el 0, dos seguidos, Q-C1289).
- `C1 2C 55 00 <gremio:8> <marca:32>` con el XOR32 (`0x6552b0`, el alta: el byte en 0 lo escribe `0x6553bf`; la
  marca, los 64 nibbles de `0x78a5986` de a dos, el par primero arriba; Q-C1287).
- `C1 17 53 <nombre:10> <número:10>` con el XOR32 (`0x62a878`, el sí del diálogo `0x7d`, CL9 hito 8, fase 6): los
  diez bytes del lugar `0x6d5e2c` de `0x787a5e8` y los diez de `0x7798bf8`, donde GetText dejó lo escrito en
  `[0x5763508]` (hasta 7 dígitos y el `'\0'`; detrás, lo que había en el buffer). Cotejado (CL9.R) con el GS 0.97:
  `PMSG_GUILDDELUSER` (`0x17` bytes, `Name` en +3 y `JoominNumber` en `+0xd`, `CGGuildDelUser`, `0x420bf0`) copia los
  10 del número a un buffer de 14 en cero y lo pasa a `gObjCheckPassword`; el maestro con su propio nombre disuelve
  (`GDGuildDestroySend`), con otro lo echa, y el que no es maestro sale (`GDGuildMemberDel` con el nombre que llega).
  Responde `C1 04 53 <resultado>` (3 sin `gGuildDestroy`, 0 con el número mal del maestro).
- `C1 05 18 <dirección> <acción>` con el XOR32: la guerra (`0x4d8473` con `0x94`, `0x4d8a39` y `0x4d8e1b` con
  `0x8d` y `0x89`), después del de `0x576a60`, que manda el suyo si el héroe está parado (hito 1, `Scenes::PlayTheEmote`).
- `C1 04 61 <sí>` con el XOR32 (2026-11-17): la respuesta al pedido de guerra del diálogo `0x7f`, 1 con el sí
  (`0x62b39c`) y 0 con el no o Esc (`0x62c5c2`, que además olvida la guerra con `0x4c1800`).
- `C1 05 50 <clave:2 BE>` con el XOR32 (2026-11-17): `/guild` (`0x573f49`) y el comando 5 del elegido (`0x4085d1`)
  piden entrar al gremio de otro.
- `C1 07 E5 <relación> <pedido> <clave alta> <clave baja>` con el XOR32 (2026-11-17): `/union` (1, 1), `/rival` (2, 1)
  y `/rivaloff` (2, 2) desde `0x570d48`, y los comandos 6 a 8 del elegido.
- `C1 08 E6 <relación> <pedido> <resultado> <clave alta> <clave baja>` con el XOR32 (`0x653c00`, y `0x656616` con el
  diálogo ocupado: el 0).
- `C1 03 E9`, sin campos (`0x4d9bc8`).
- `C1 08 66 0 <gremio:4 LE>` con el XOR32 desde `pkt[3]` (`0x4d930f..0x4d9690`): el `65` pide el `66` de cada gremio
  que la tabla no tiene, antes de agregarlo con `0x447b50` (fase 2). El 0 es el relleno hasta el DWORD, como el
  `pkt[3]` del `66`.

Contra el GS 0.97 (08, P-C283): el `52` tiene la cabecera de `0x10` bytes y entradas de `0xc` (`PMSG_GUILDLIST`, sin
el puesto), y el 1.02 lee una de `0x18` y entradas de `0xd`; el `5C` no tiene handler; la alianza (`E1`..`EB`) no
existe, y tampoco el `65`, el `66` ni el `67` (el GS 0.97 manda el `5C`, que el cliente ignora): el ASM es el oráculo
de sus formatos, y que sean C2, *inferido* de dónde leen la cuenta. MyDll (`mydll_siege.cpp`) sí manda el `65`, el
`66`, el `67`, el `E5`, el `E6` y el `E9`; su `E9` (`alliance_list_send`) anuncia todos los gremios de la alianza y
escribe solo los que encuentra, con una cola de 8 ceros (07, Q-C1365).

**MuRet**: `Protocol::Guild::ReadGuildPacket` lee los 20 (el `52`, el `65`, el `67` y el `E9` solo en C2) y sus `Write*` arman lo que
manda; `App::ServerSession` los registra en un bloque (`RegisterGuildHandlers`) y `Scenes::ApplyGuildPacket` los
aplica (06). Los tests usan nombres inventados.
### Los amigos: `C0`..`C4`

(CL9 hito 9, 2026-11-13, *estático*.) La reconstrucción del GameServer 0.97 no tiene amigos: el `switch` de
`protocol.cpp` no tiene `C0`..`C4` (sus casos llegan a `0xB0`, `0xF1`, `0xF3` y `0xFF`) y el EDSProtocol tampoco. El
oráculo es el ASM del cliente; el cable, de un servidor que los tenga, queda para la VM (08, P-C184). El dispatcher
(`0x4e5a4e..0x4e5aae`) los pasa sin compuertas, ninguno mira el largo y ninguno saca `rand()`. Los nombres van en 10
bytes y el cliente los copia con `strncpy` de 10 a un buffer de 11 en cero: uno de 10 no trae `'\0'`. La lista es el
`std::deque` de `[0x5763518]` (elementos de `0x14` bytes: elegido, el nombre, una marca y el estado).

- **`C0`** (`0x4dc010`): `C2 len C0 <cartas> <tope> <n> n × (<nombre:10> <estado>)`, los tres sin signo. Vacía la
  lista, suma cada amigo al final (`0x49b220`), ordena por nombre y después por estado (`0x49c260`, Q-C881), copia la
  lista a la pestaña de amigos si la ventana está (`0x49ddd0`), `0x4918d0(1)` (`+0xe8` del administrador en 1 y el
  título de la ventana, el 990) y el tope en `0x5767970`. Con cartas, el 1072 con las dos cuentas al chat
  (`0x555190`, con el nombre vacío de `0x5767a94`). El `C4 02` que arma en `0x4dc0d7..0x4dc37a` no sale (Q-C886).
- **`C1`** (`0x4dc3d0`, la tabla `0x4dc590`): `C1 0F C1 <resultado> <nombre:10> <estado>`. 0: la caja del 1047 con el
  nombre; 1: el 1075 al chat, lo suma, ordena con el modo que quedó, la pestaña y `0x4a6840` (la ventana de chat con
  ese amigo, hito 10); 2: nada; 3: el 1048; 4: el 1049 con el nombre; 5: el 1050; 6: el 1068; más de 6, nada. Las
  cajas son `0x48ee60(10, −1, −1, texto, 0, 0)`.
- **`C2`** (`0x4dc5b0`): `C1 0D C2 <nombre:10>`. La pregunta del 1051 con el nombre,
  `0x48ee60(9, −1, −1, texto, −1, 0)`; si la armó, `0x4a4440` le guarda el nombre (`strncpy` de 10 a `+0x2b8`).
  Suena `0x2a` igual.
- **`C3`** (`0x4dc690`): `C1 0E C3 <resultado> <nombre:10>`. 0: la caja del 1052; 1: lo saca (`0x49b690`, el primero
  con ese nombre) y la pestaña; otro, nada.
- **`C4`** (`0x4dc720`): `C1 0E C4 <nombre:10> <estado>`. Con `0xfc`, todos en `0xfc` (`0x49c0e0`), ordena, la
  pestaña, `0x4a75c0` (hito 10) y `0x4918d0(0)` (el título 1066); si no, el estado del primero con ese nombre
  (`0x49c010`), ordena, la pestaña y el estado en el chat con ese amigo (`0x4a6840`, `0x4a6430` y `0x495630`,
  hito 10).

Lo que manda la ventana de amigos sale en C1, con el XOR32 desde el byte 3 y en claro por el `send()` del socket,
sin la cola de los cifrados:

- **`C1 03 C0`**: la fábrica, el caso 3 (`0x48ee60`), al armar la ventana con `+0xe8` del administrador en 0. El
  primer paso del juego lo pone en 1 (`0x48ea50`), el `C0` también y el `C4` con `0xfc` lo vuelve a 0 (`0x4918d0`):
  la ventana pide la lista solo después de ese `C4`. No tiene bytes cifrados.
- **`C1 0D C1 <nombre:10>`**: `0x49d0b0` con el 11 de la caja de agregar (`0x4a37e0`). Copia 10 bytes del texto del
  mensaje (`0x49d4ca..0x49d4f0`): con un nombre corto, lo que haya detrás del `'\0'` en el buffer de `0x100` de la
  caja (Q-C882).
- **`C1 0E C2 <1 | 0> <nombre:10>`**: la respuesta de la caja del `C2` (`0x4a3b80` con dueño −1, `0x4a3bde` el sí
  y `0x4a3fc8` el no), con el nombre de `+0x2b8`.
- **`C1 0D C3 <nombre:10>`**: `0x49d0b0` con el 12 y 1, la respuesta de la pregunta de borrar, con el nombre del
  elegido (`0x49cad0`).
- **`C1 04 C4 <estado>`**: 0 desde la pregunta de salir de línea (`0x4a2be0`, el 12 con 1) y 1 desde el botón del
  estado con `+0xe0` en 1 (`0x4a3030`). El `C4 02` de `0x48eb84` (el primer paso del juego) tampoco sale (Q-C886).

**MuRet** (CL9 hito 9): `Protocol::Friends` lee los cinco (`ReadFriendList` en C2, los demás en C1, con `WrongPacket`
y `TooShort`) y escribe los cinco que manda (`WriteFriendListRequest`, `WriteFriendAdd`, `WriteFriendAnswer`,
`WriteFriendDelete` y `WriteFriendState`, con `Delivery::Clear`); el nombre va completado con ceros (Q-C882).
`App::ServerSession` los registra juntos y los pasa al mundo (`Scenes::ApplyFriendList`, `ApplyFriendAddResult`,
`ApplyFriendRequest`, `ApplyFriendDeleteResult` y `ApplyFriendState`); lo que manda la ventana sale por
`IGameUiRequests::SendFriendPacket`. Lo del chat con un amigo y las cartas corta con su dirección para el hito 10
(08, P-C183). Los tests usan nombres inventados.

### Las cartas y el chat con un amigo: `C5`..`CB` y la conexión de la sala

(CL9 hito 10, 2026-11-13, *estático*.) La reconstrucción del GameServer 0.97 no tiene cartas ni chat con un amigo:
el `switch` de `protocol.cpp` no tiene `C5`..`CB`. El oráculo es el ASM del cliente; el cable, de un servidor que los
tenga, queda para la VM (08, P-C214). El dispatcher (`0x4e5ac6..0x4e5b42`) pasa `C5`..`C8`, `CA` y `CB` sin
compuertas y sin `rand()`, como los de los amigos; no tiene caso para el `C9`, que solo sale. Los nombres van en 10
bytes con `strncpy` a un buffer de 11 en cero.

- **`C5`** (`0x4dc840`): `C1 len C5 <resultado> <ventana:u32>`. 1: el 4 a la de escribir (`0x459f70`) y
  `sprintf(1046, 1000)` al chat (`0x555190`); 0, 2, 3, 4, 6 y 7: `+0x778` de la ventana en 0 sin mirar que esté
  (Q-C1001) y la caja del 1053, 1061, 1064, 1065, 1068 o 423 (`0x48ee60(10, −1, −1, texto, 0, 0)`); 5 y más de 7,
  nada.
- **`C6`** (`0x4dc9e0`): `C1 len C6 <relleno> <número:u16> <nombre:10> <fecha:10 en pkt[0x10]> <hora:8 en pkt[0x1b]>
  <asunto:0x20 en pkt[0x2e]> <tipo en pkt[0x4e]>`. 0 y 1: la carta al modelo (`0x49eb60`, si el número no está) y el
  orden por la fecha (`0x49f870(2)`); 2: el sonido `0x29`, la marca de carta nueva (`0x4a6420`: `+0x144` de la lista
  de ventanas), el 1062 al chat, la carta sin leer y el orden que estaba (−1); después, la pestaña (`0x4a0960`) y,
  con el tope del `C0` (`0x5767970`) alcanzado, el 1073 al chat.
- **`C7`** (`0x4dcb70`, en C2): `C2 len C7 <relleno> <número:u16> <largo:u16> <clase> <equipo:17> <dirección>
  <acción> <cuerpo>`. Pone un 0 en `pkt[0x1c + largo]` sin mirar el tamaño (Q-C1000); guarda los primeros `0x404`
  bytes (`0x4a03c0`, si no había copia), marca la carta leída (`+0x134 = 1`), la pestaña, y arma la de leer
  (`0x48ee60(6)` con `sprintf(1054, asunto)` en (100, 100) o, con `0x5762f0c` distinto de −1, donde quedó la
  anterior con el bit 0 de las banderas), el cuerpo (`0x499cb0`), la ventana en el mapa de las cartas (`0x491720`)
  y, salvo que el que la manda sea `webzen` (`_strnicmp` de 10, `0x4dccbd..0x4dccd2` con `0x6a3450`: sin importar las
  mayúsculas, y el 0 del final también se compara, así que tiene que ser el nombre entero), el retrato: la clase (los bits 4..6 y el 4 de
  `pkt[8]`, `0x4977d0`), el equipo (`0x497810`), la acción + `0x78` (`0x4978f0`), el ángulo (los 6 bits de abajo de
  la dirección por 6, con el personaje armado) y el zoom (`((dir >> 6) · 5 + 0x28) · 2 · 0,01`). Por último, el 14 a
  la ventana.
- **`C8`** (`0x4dcd90`): `C1 len C8 <resultado> <número:u16>`. 0: la caja del 1055; 1: la carta fuera del modelo
  (`0x49ee20`) y de las copias (`0x4a0500`); después, la pestaña.
- **`CA`** (`0x4dce10`): `C1 len CA <IP:15> <sala:u16 en pkt[0x12]> <ticket:u32> <tipo> <nombre:10> <resultado>`. 0 y 2:
  el amigo fuera de los pedidos (`0x4a6bc0`) y la caja del 1069 o 1070; 1: con el tipo 0, fuera de los pedidos y el
  chat (`0x48ee60(1, 100, 100, 994)`) que entra a la sala (`0x493530`); con el 1, el chat que ya había con el amigo
  (`0x4a6430`: con −1 nada; con uno, sale de su sala, con el `C1 03 01` y el cierre de la conexión en el momento
  (`0x493a70`, `0x49dfd0`), y entra a la nueva, aunque tenga el mismo número; sin ninguno, como el 2); con el 2, el chat
  minimizado (`0x48ee60(2)`), la sala, el estado 5, al fondo (el 5 al administrador) y fuera de la pestaña de las
  ventanas (`0x49e710`).
- **`CB`** (`0x4dd020`): `C1 len CB <resultado> <ventana:u32>`. Con el chat de esa ventana: 0, el 1056; 1, el elegido
  de la lista de invitar con el 1057; 3, el 1074; renglones del sistema del chat (`0x493f80` con 0xff y 1).

Lo que mandan las ventanas sale en C1 con el XOR32 desde el byte 3 y en claro por el `send()` del socket, salvo la
carta:

- **`C1 0D CA <nombre:10>`**: `0x49d0b0` con el 3, con el amigo en línea, sin chat con él (`0x4a6430`) y sin pedirlo
  ya (`0x4a6af0`); lo anota (`0x4a6910`). Con `+0xe0` del administrador en 1 (fuera de línea), nada.
- **`C1 14 CB <nombre:10> <0> <sala:u16> <ventana:u32>`**: `0x4944c0` con el 10 y 2 (o el doble clic en la lista de
  invitar), con el EDIT sin el foco, el elegido y de 2 a `0x1d` miembros; con más, el 1074 al chat.
- **`C2 len C5 <ventana:u32> <nombre:10> <asunto:0x20> <dirección> <acción> <largo:u16> <cuerpo>`**: `0x498820` con
  el 10 y 1, con el XOR32 desde el byte 4, y sale cifrado como C4 por la cola de los cifrados (`0x4992fe..0x499416`):
  antes, un `rand()` del relleno y el serial de la tabla de protección (`0x5767913`), como el `C3 F1 02` (Q-C1002).
  El cuerpo pasa `"\r"` a `"\n"` y se come el que sigue; `"\r\n\r\n"` deja un espacio en medio (Q-C1003). La
  dirección es `(ángulo / 6) & 0x3f | ((zoom · 100 − 80 + 5) · 0,1) << 6` y la acción, la del retrato − `0x78`.
- **`C1 06 C7 <0> <número:u16>`**: la pestaña de las cartas con el 10 y 2 (o el doble clic) y la de leer con el 5 y
  el 6 (la que sigue y la de antes, `0x49ff50` y `0x4a0070`), si la carta no tiene ventana (`0x491570`) ni copia
  (`0x4a04a0`, que vuelve a `0x4dcb70`).
- **`C1 06 C8 <0> <número:u16>`**: el sí de la pregunta de borrar, uno por cada carta marcada de la pestaña
  (`0x4a11b0` con el 12) o el de la de leer (`0x49a1f0`, que después se cierra).
- **`C1 03 C9`**: la pestaña con el 10 y 5, en claro.

La conexión de la sala (`0x49de50`): un lugar de 256 (`0x49e280`; sin lugar, `"max chat count over"` al chat) y un
socket al puerto 55980 de la IP del `CA` con el mensaje `0x500 + lugar`; por ella, `C1 10 00 <0> <sala:u16>
<ticket:10>` (el ticket en decimal con `_itoa` y el XOR de 3 bytes `FC CF AB`), los mensajes (`C1 len 04 <0> <largo>
<texto>`, con el XOR de 3 bytes), la salida (`C1 03 01`, del destructor `0x493a70`) y, cada 15 s, `C1 03 05` (el timer
1002, `0x4b1a5b` → `0x4a6550`, a la sala de cada chat de la lista de ventanas). Lo que llega (`0x49e510` →
`0x4e1d50` con 1, cuyo switch `0x4e2a60` despacha por `pkt[3]` a `0x4dd1b0..0x4dd540` con la ventana): `00`
(`0x4dd1b0`: con `pkt[3]` en 0, la caja del 1058, `0x4dd1b9`), `01` (`0x4dd1e0`, un miembro entra o sale con el 1059 o
el 1060), `02` (`0x4dd350`, los miembros: `pkt[6]` y, de a 11 bytes desde `pkt[8]`, el número y el nombre), `04`
(`0x4dd3d0`, un mensaje de hasta 100: minimizado, el 1063 al chat y la ventana oculta en la lista de ventanas; oculto
o no arriba, el sonido `0x28` y la ventana en espera, `0x4a5e50`) y `0D` (`0x4dd540`: `pkt[0x68] = 0` y el texto al
chat). `0x4e6240` es una copia de ese switch con los cinco casos (los tres cortos en línea) que no llama nadie: código
muerto.

**MuRet** (CL9 hito 10): `Protocol::Letters` y `Protocol::FriendChat` leen y escriben todo lo de arriba (con
`WrongPacket` y `TooShort`; el `C7` cuyo cuerpo pasa del paquete, rechazado, y el que lo llena justo, sin el 0, se lee:
Q-C1000). `App::ServerSession`
registra `C5`..`C8`, `CA` y `CB` juntos y los pasa al mundo (`Scenes::ApplyLetterSendResult`, `ApplyLetterRow`,
`ApplyLetterRead`, `ApplyLetterDeleteResult`, `ApplyRoomAnswer` y `ApplyInviteResult`); lo que mandan las ventanas
sale por `Scenes::SendAFriendPacket`, que saca los `rand()` de la carta. La conexión de la sala es otra
`Network::Connection` de la sesión (`App::IChatRoomLinks`, P-C211): lo que llega por ella va a `Scenes::ApplyRoomPacket`
(lo de `0x4e2a60`; uno más corto que lo que lee su handler se ignora con un aviso y la sesión sigue: Q-C1011) y su
cierre a `Scenes::ApplyChatRoomClosed`; lo que mandan las ventanas lo saca
`Scenes::FlushTheChatRoomOrders`. El connect no espera, como el del GameServer de MuRet: la sala se anota al empezar y
el `C1 10 00` sale con el connect. Ni el GameServer 0.97 ni el repo de research tienen un servidor del chat: lo del
cable es del ASM del cliente (*estático*). Los tests usan nombres y textos inventados.
### Las ventanas de los NPC: `30` a `34`, `81` a `83`, `86` y `87`

(CL9 hito 6, 2026-11-13, *estático*, contra el GameServer 0.97 reconstruido: `PMSG_TALKRESULT`, `PMSG_SHOPITEMCOUNT`,
`PMSG_BUYRESULT`, `PMSG_SELLRESULT`, `PMSG_ITEMDURREPAIR_RESULT`, `PMSG_WAREHOUSEMONEYINOUT_RESULT` y
`PMSG_CHAOSMIXRESULT`.) Los handlers leen por posición, sin mirar si el paquete es C1 o C2; solo el `30` mira si llegó
cifrado. Abrir y cerrar las ventanas son casos de `0x6599a0` y `0x657ab0` (06 § HUD y ventanas, "Los paneles").

- **`30`** (`0x4d1e00`): sin cifrar, el aviso `C1 06 F1 03 00 30` con el XOR32 y en C3, sin leer nada más. Cifrado:
  `0x657a90` (cierra todo) y `pkt[3]` por la tabla de `0x4d2914`: 2 el almacén (`0x40`), 3 la caja del caos (`0x100`,
  y `pkt[4..9]` a los seis int de `0x6c9da8`), 4, 5 y 6 los NPC de evento (`0x1000`, `0x2000`, `0x4000`), 7 el
  entrenador (`0x8000`), 9 el diálogo `0xa1` con `0x6e5798` en 0, 12 la lista de CS (`0x100000`, con `pkt[4] == 1` en
  `[0x7d536fc] + 0x3d4`), 13 el `C1 04 B2 00`; los demás (0, 1, 8, 10, 11, 14..) la tienda (`0x10000`, `0x4d288e`).
  Después, los sonidos `0x19` y `0x1c` y `SetCursorPos(((260 · mx / 640) · ancho) / 640, alto · my / 480)` con el
  mouse en 640 × 480 (`0x7d3682c`, `0x7d36828`). El GameServer 0.97 lo manda en C3 de 8 bytes, con cuatro tasas: las
  dos que faltan el original las lee de su buffer (07, Q-C920).
- **Abrir** (`0x6599a0`, con lo demás cerrado por `0x657ab0`): el almacén (`0x659f66`) abre el inventario y pone
  `0x7d5370d` en 1 y `0x6c932f` y `0x78abb20` en 0; la caja del caos (`0x65a93d`) pone `0x7d2dc9d` en 0, manda
  `C1 04 BD 09` con el XOR32 en claro, abre el inventario, pone `0x7d5370e` en 1 y `0x7d2dc80` en 0 y abre el diálogo
  `0x8e`; la tienda (`0x65ba46`) abre el inventario, escribe 1 en el byte protegido `0x7d53710` (un `rand()`) y pone
  `0x78abb27` en 0.
- **Cerrar** (`0x657ab0`): el almacén, con la traba del `24` (`0x78abb39`) no cierra (el resultado queda en 0 y sigue
  con los demás); si no, vacía las 120 casillas de `0x78a7070` (tipo `0xffff`, `+0x3c` en 0), devuelve el ítem del
  cursor (`0x5be150`), manda `C1 03 82` en claro y pone `0x7d5370d` y `0x6c932f` en 0. La caja del caos, con algo en
  su grilla (`0x5e3c20`) o en el cursor, el texto 593 de error y 0; si no, `C1 03 87` en claro, sus 32 casillas
  vacías, el diálogo `0x8e` en 0 y `0x7d5370e` en 0. La tienda escribe 0 en su byte (un `rand()`), manda `C1 03 31`
  en claro y vacía `0x78a7070`.
- **`31`** (`0x4c5f60`): `pkt[4]` la grilla y `pkt[5]` la cuenta, con entradas de 8 bytes desde `pkt[6]` (el lugar y
  los siete del ítem). La 3 es la caja del caos (`0x7d2dc80` en 2, los sonidos `0x57` y `0x44`, la grilla vacía y
  `0x5bc740(0x7d2d380, 8, 4, lugar, datos, 1)`), la 5 la caja del entrenador (el texto 1208, `0x449c40` y
  `0x44ad70`), y otra la tienda o el almacén (`0x78a7070` vacía y `0x5bc150(lugar, datos)`: 8 por fila desde el lugar,
  sin tope, y `0x54bf40` con B1 y B4 sin bits arriba y B3 con los del tipo). El GameServer 0.97 lo manda en C2 con
  entradas de 5 bytes (08, P-C190).
- **`32`** (`0x4e3c2a`): con `pkt[3]` distinto de `0xff`, `0x5bc740(0x78a9bb0, 8, 8, pkt[3], pkt + 4, 0)` y el sonido
  `0x1d`; siempre, `0x5767950` en 0. **`33`** (`0x4e3c6e`): con 0, `0x5be150`; si no, el cursor vacío y el zen
  (`pkt[4..7]`) en `+0xecc` del registro propio abierto, y el sonido `0x1d`. **`34`** (`0x4e3cd7`): con zen distinto
  de 0, en `+0xecc` con el registro abierto, `0x550d10` y el sonido `0x25`.
- **`81`** (`0x4e4692`): con `pkt[3]` distinto de 0, el zen del almacén (`pkt[4..7]`, `+0xed0`) y el del inventario
  (`pkt[8..11]`, `+0xecc`). **`82`** y **`87`** (`0x4e4819`): el cursor vacío y, con el diálogo `0x73`, su cierre,
  `0x553570(0)`, `0x6c8e78` en `0x2a`, los campos de Tab en 2 y `0x77f3b6a` y `0x6c8e68` en 0. **`83`** (`0x4e46df`,
  las tablas de `0x4e60c0` y `0x4e60dc`): 0 y 1 escriben la traba (`0x6c932f`) y ponen `0x78abb1c` en 0; `0xa`, `0xb`
  y `0xd` abren los diálogos `0x85`, `0x86` y `0x89`; `0xc`, con la traba y `0x78abb1c` en 0, manda lo que esperaba
  (`0x78ab9bc` en −1: `0x4e6f80` con el tipo y el zen; si no, `0x449d80` con los cuatro) y deja los dos en 1.
- **`86`** (`0x4da280`, las tablas de `0x4da38c` y `0x4da3a8`): 0 el texto 594 (error); 1 el texto 595, la caja vacía
  y el ítem de `pkt[4..10]` en su lugar 0 (`0x5bc740(0x7d2d380, 8, 4, 0, pkt + 4, 1)`) con los sonidos `0x57` y `0x45`;
  2 y `0xb` el texto 596 con `0x7d2dc80` en 0; 4 y 9 la caja `0x632e30` con los textos 649 y 689; 100 la caja del
  entrenador (`0x449c40` y `0x44ad70(0, pkt + 4, 1)`: también lee el ítem de `pkt[4..10]`, `0x4da378`, sin tocar
  `0x7d2dc80`). Salvo 2, `0xb` y 100, todos dejan `0x7d2dc80` en 2,
  también los que no tienen caso.
- **Lo que manda el cliente**: comprar `C1 04 32 <lugar>` (`0x5c4bdc`, desde la grilla de la tienda), vender
  `C1 04 33 <lugar>` (`0x5d8e1a`), reparar `C1 05 34 <lugar> <0x78abb10>` (`0x5c4485`, un ítem de la grilla; `0x5edaa7`,
  todo con `0xff 00`), la contraseña del almacén `C1 10 83 <tipo> <contraseña:2> <número:10>` (`0x5e7e90`), todos con el
  XOR32 y en C3; el zen del almacén `C1 08 81 <tipo> <zen:4>` (`0x4e6f80`) y la mezcla `C1 04 86 <0x7d2dc98>`
  (`0x5d9362`, que el GameServer 0.97 no lee), con el XOR32 en claro.

**MuRet**: `Protocol::NpcWindows` lee y escribe todos; `App::ServerSession` los registra (el `30` con su aviso) y
`Scenes/NpcWindows.cpp` los aplica sobre `WorldRuntime::npcWindows` (las dos grillas y los globales). `UI::PanelRegistry`
abre y cierra `0x40`, `0x100` y `0x10000` con los cables de la escena (`PanelContext::npcWindowOpened` y
`npcWindowClosing`), con el `rand()` del byte protegido. Reparar un ítem desde la grilla del inventario manda su `34`
(`0x5c4472`). Lo que sigue cortado, con su dirección, está en 08, P-C191. Los tests usan ítems y textos inventados.

**Fase 2: lo que manda el cliente desde las ventanas** (CL9 hito 6, 2026-11-13, *estático*; el GameServer 0.97
reconstruido es la referencia de cada paquete, `protocol.cpp` y `gs_protocol.h`, y donde no lo lee el oráculo es el
ASM del cliente):

- **`32`** comprar (`0x5c4b64..0x5c510f`, desde `0x5c4110` con el tipo 1): el clic sobre un ítem de la grilla de la
  tienda, sin el modo de reparar y sin otra compra esperando (`0x5767950`), manda `C1 04 32 <lugar de la esquina>` y
  deja en `0x5767950` el precio de `0x54d0f0` en el modo 0. El 0.97 lo lee como `PMSG_BUYREQUEST` (`Pos` en `+3`) y
  con `DataEncryptCheck`: lo pide en C3, como sale.
- **`33`** vender (`0x5deca7..0x5df448`, desde `0x5d8d50` con el clic fuera de las grillas): con la tienda abierta (su
  `rand()`) y el mouse en `[x + 0xf, x + 0xc3) × [y + 0x32, y + 0x172)`, lo que no se vende no hace nada, lo valioso
  abre el `0x98` con `0x78abb14` en 1 y lo demás sale con `C1 04 33 <0x78a6758>`; el sí del `0x98` (`0x6c932e` en 1)
  lo manda en el paso siguiente (`0x5d8e1a`) y el no devuelve el ítem (`0x5be150`). `PMSG_SELLREQUEST`, cifrado.
- **`34`** reparar: la grilla del inventario (`0x5c4485`) y el equipo (`0x5c2dd5`, con el registro de `0x5bec50`)
  mandan `C1 05 34 <lugar> <0x78abb10>`; el botón de reparar todo de la tienda (`0x5edaa7`, con `0x78abb09`)
  `C1 05 34 FF 00` sin gastar el clic. `PMSG_ITEMDURREPAIR` (`Position`, `Requestpos`): `0xff` repara todo y
  `Requestpos` 1 pide nivel 50; el 0.97 no mira si llegó cifrado, el cliente lo manda en C3.
  El entrenador con `0x724580` en 2 (`0x44b910`, fase 3b) manda `C1 05 34 08 <precio>` (el caballo oscuro, `0x1a04`,
  en el lugar 8) o `C1 05 34 01 <precio>` (el cuervo, `0x1a05`, en la mano izquierda), con el byte bajo del precio de
  `0x44b190` como `Requestpos` (07, Q-C948), en C3, y detrás el `C1 03 31` de `0x4499b0` en claro.
- **`24`** guardar y sacar (`0x5ca250` desde `0x5deb57` y desde la grilla del inventario): `0x449d80` con la grilla
  del cursor (0 el inventario, 2 el almacén, 3 la caja del caos, 5 la del entrenador) y el lugar de destino, en C3.
  La caja del entrenador (fase 4b): soltar en ella (`0x44aca9`, `0x5ca250` con el tipo 5) manda `0x449d80(0, lugar,
  5, fila · 8 + columna)` y soltar lo que sale de ella en el inventario, `0x449d50` (`0x449d80(5, lugar, 0,
  destino)`); el `24` que vuelve con 5 la pone en la caja (`0x4d1d55`, `0x44ad70` con la cola de `0x5bc740`). Sacar del almacén con
  la traba (`0x6c932f`) y sin `0x78abb1c` no manda nada: guarda los cuatro valores en `0x78ab9bc..` y abre el
  teclado (`0x5cb3af..0x5cb455`); el `83` con `0xc` los manda después (`0x4e47c6`). `CGInventoryItemMove`.
- **`81`** el zen del almacén (`0x6270ed..0x6282c8`, el OK del `0x73` con Enter): más de 100.000.000 abre el `0x75`;
  con el comercio abierto corta (CL9.4); si no, con el registro propio abierto, meter más que el zen del inventario
  (sin signo) o sacar más que el del almacén (con signo) abre el `0x74`, y sacar lo que deja el inventario arriba de
  2.000.000.000 (sin signo) solo cierra el diálogo; sacar con la traba
  y sin `0x78abb1c` guarda `{−1, 1, zen, 0x77f3cc8}` y abre el teclado (`0x5e7aa0(1)`, sus 40 `rand()` antes de
  los 3.848 del cierre); si no, `C1 08 81 <tipo> <zen:4>` con el XOR32 en claro (`0x4e6f80`).
  `PMSG_WAREHOUSEMONEYINOUT` (`Type` 0 meter y 1 sacar, `Money` con signo en `+4`); el 0.97 también rechaza más de
  100.000.000.
- **`82`**, **`87`** y **`31`**: cerrar el almacén (el botón de `x + 0xd7`, `0x5ea24d`), la caja del caos (el botón
  de la esquina del inventario) y la tienda (el mismo) pasan por `0x657ab0`. `CGWarehouseUseEnd`, `CGChaosBoxUseEnd`
  y `CGCloseWindow` no leen cuerpo.
- **`86`** la mezcla: el sí del `0x98` con `0x78abb14` en 2 (`0x5d934f..0x5d9633`) pone `0x7d2dc80` en 1 y manda
  `C1 04 86 <0x7d2dc98>` con el XOR32 en claro. `CGChaosBoxItemMixButtonClick` no lee el tipo: el byte sale como lo
  arma el cliente (el ASM es el oráculo): el tipo de `0x5fd4f0` filtrado por el modo de la caja (`0x5fd4b0`), que
  `0x5d8d50` recalcula en cada paso con la caja abierta, y el sí llega del `0x98` que abre el botón de mezclar
  (`0x5fe690`; 06 § "La caja del caos"). Los tipos que pueden salir: 1 a 8, `0xb`, `0xf` a `0x11` y `0x16` a `0x1c`
  (el botón no abre el `0x98` con el 0 ni con los negativos). El entrenador en el modo 3 (`0x44d010`, fase 4b) manda
  `C1 04 86 0D` o `0E` (la mezcla del caballo o la del cuervo, de `0x724584`) con el XOR32 en claro, sin mirar el zen
  (07, Q-C956). La respuesta `86` con 100 (`0x4da373`) vacía la caja del entrenador y pone el ítem en su lugar 0; el
  `31` con la grilla 5 (`0x4c5fb4`) avisa con el 1208 y la llena.
- **`83`** la contraseña (`C1 10 83 <tipo> <contraseña:2> <número:10>`, `PMSG_WAREHOUSEPASSSEND` de `0x10` bytes,
  con el XOR32 y en C3): la manda el OK de la caja de la contraseña (`0x5e7bf0`, 06 § Los dibujos de las ventanas de
  los NPC) según su modo. El 1 (abrir la traba, `0x5e931c`) con 0, el `atoi` de las cuatro cifras y los diez primeros
  bytes del buffer del mensaje del chat (`0x7798bf8`, Q-C924); el 2 (`0x5e7e90`, después de la contraseña nueva dos
  veces en el 4 y el 5) con 1, esa contraseña (`0x78abb24`) y las siete cifras del número personal; el 3 (`0x5e8c02`,
  sacar la traba) con 2, 0 y el número. `CGWarehouseCheckPass` hace lo mismo con los tres: 0 compara `Pass` con la
  contraseña y no mira el número; 1 y 2 comparan el número con `gObjCheckPassword2` y ponen o sacan la contraseña.
- **`99`** el número personal (`C1 0D 99 <número:10>`, con el XOR32 y en C3): lo manda el modo 6 de la misma caja
  (el que abre el OK del `0x8f`, `0x628276`), después de un `C1 03 31` en claro y de cerrar todo (`0x657a90`,
  `0x5e8552..0x5e8bfd`). El GameServer 0.97 despacha el `99` a `CGAttributeMixRequest` (`PMSG_REQ_ATTRMIX`), que no
  es esto (Q-C925): quién lo atiende en el servidor del 1.02 queda para la VM.
- **`BC`** la mezcla de joyas (CL9 hito 6, fase 3b; 06 § "El modo de la caja del caos y la mezcla de joyas"): el sí
  del `0xa3` llama a `0x4029e0`, que con `0x6e5798` en 0 y `0x6e5799` en 0 pone la primera en 1 y, según
  `0x6e5794`, manda `C1 06 BC 00 <joya> <cuántas / 10 − 1>` (`0x402500`: `0x6c62e0`, y `0x6c62e1` con signo dividido
  hacia el cero) o `C1 07 BC 01 <joya> <nivel> <lugar>` (`0x401f30`: `0x6c62e0`, `0x6c62dc` y `0x6c62d8` + 12, que
  queda así en el global, Q-C947). Los dos arman el C1 de a un byte con el XOR32 de cada byte nuevo y lo mandan con
  `send()` en claro, sin la tabla de protección: sin `rand()`. El GameServer 0.97 no atiende el `BC` (no está en
  `protocol.cpp`): el ASM es el oráculo. Las respuestas (fase 4b): `C1 05 BC <0 o 1> <resultado>` (con otro pkt[3],
  nada). Con 0 (juntar, `0x4e4570`, la tabla `0x4e60a8` con pkt[4] de 0 a 5) y con 1 (deshacer, `0x4da540`, la tabla
  `0x4da620` con pkt[4] de 0 a 8): el 1 abre el `0xa6` (`0x6236f0(0)` y `0x6236f0(0xa6)`); el 0, el 2 y el 3 de juntar
  y el 0 y el 5 de deshacer muestran "%s%s %s" (`0x6c8490` y `0x6c8498`) con el 1801 o el 1800, el 1816 y el 868 en un
  buffer de `0x100`; los demás, un texto (juntar: 4 el 1817 y 5 el 1811; deshacer: 2, 3, 4 y 6 el 1812, 7 el 1815 y
  8 el 1811). Los mensajes son de tipo 1 con el nombre vacío (`0x5767a54..0x5767a6c`, que nadie escribe) y después
  `0x402e50` vuelve a la elección de la joya. Sin `rand()`. `0x4da460`, el gemelo de juntar como función aparte (la
  tabla `0x4da524`), no tiene quien la llame (08, P-C341). **MuRet**: `Protocol::JewelMix` (lo que sale); la respuesta
  llega como `Protocol::Events::JewelMixResult` del hito 11 y `Scenes::ApplyEventPacket` la atiende con
  `Scenes::ReturnToTheJewelChoice` (`0x402e50`).
- **`AF 01`** la entrada a Chaos Castle (CL9 hito 6, fase 4a; 06 § "Los diálogos del juego"; *inferido* del diálogo,
  `UI::ChaosCastleDialog`): el sí del `0x9a` (`0x628582..0x628aaf`) con el dword `0x78a59cc` (el `+4` del ítem de los
  diálogos de `0x78a59c8`) distinto de `0xff` manda `C1 06 AF 01 <nivel> <lugar>` (`0x628594..0x628947`): el nivel es
  `(0x78a59cc >> 3) & 0xf` (`0x628702..0x628710`) y el lugar el byte bajo de `0x78a6758` (`0x6287f7`); con `0xff` no
  hace nada y el diálogo sigue. Después cierra con el sonido `0x19` (`0x628a70..0x628aac`). *Estático*.
- **`D0 00`** el sí de la pregunta del `F3 40` (CL9 hito 6, fase 4a): el `0xaa`, que abre `0x4da1b9` con el texto
  2020, manda `C1 04 D0 00` con el sí (`0x62a480..0x62a5e8`); el no solo cierra. *Estático*.
- Los dos arman el C1 de a un byte con el XOR32 de cada byte nuevo y lo mandan con `send()` en claro, sin la tabla de
  protección: sin `rand()`. El GameServer 0.97 no los atiende: `ProtocolCore` (`0x414180`) no tiene el caso del `AF`
  ni del `D0`, y su `default` loguea (`LogAddC`) y cierra la conexión (`CloseClient`). El ASM es el oráculo (CL9.R).
  **MuRet**: `Protocol::DialogAnswers`.

Los `rand()`: con la tienda abierta, cada `IsOpen(0x10000)` saca uno (también el de `0x5decc1` al vender); con el
almacén abierto, la cadena de Q, W y E (`0x5a343e`) e `IsOpen(0)` (`0x5a5511`) se paran en `0x40` antes de la tienda:
uno menos cada una. **MuRet**: `Scenes/NpcWindowSteps.cpp` (los pasos de `0x5ed960`, `0x5b1570`, `0x5ea060`,
`0x5fe610`, `0x5d8d50` con `0x78abb14` en 1, 2 y 4, vender, el OK del `0x73` y `0x5e7aa0`),
`Scenes/WarehouseKeypad.cpp` (`0x5e7bf0`, el `83` y el `99`), las grillas de la tienda, del almacén y de la caja del
caos en `Scenes/GameUi.cpp` y los de sí y no (`0x6282dc`, con el `AF 01` y el `D0 00`) en
`Scenes/GameDialogSteps.cpp`. Tests: `NpcWindowStepsTest` (comprar en Lorencia, Devias y Noria, vender, reparar,
guardar, sacar, el zen, la mezcla y el teclado del 4, byte a byte con sus `rand()`), `WarehouseKeypadTest` (los modos
de la caja, el `83` y el `99` byte a byte con sus `rand()`),
`NpcWindowRepliesTest.TakingAnItemSends24WhenThePasswordIsAccepted`, `DialogAnswersProtocolTest` y
`DialogAnswerStepsTest` (el `AF 01` y el `D0 00` byte a byte). Lo que queda, en 08, P-C192, P-C194, P-C338 y P-C339.

### Castle Siege, las ventanas del castillo

(CL9 hito 12, 2026-11-14, *estático* todo.) El GameServer 0.97 reconstruido no tiene Castle Siege: no hay con qué
contrastar, y cada byte sale del ASM del cliente. Lo que sigue es lo que llega y sale fuera de la batalla del mapa 30;
la batalla (el `B2` 0x11..0x19, 0x1D, 0x1E y 0x20, el `B6` con el panel de clase 2, el `B7`, el `B9` que llega, el
`BA` y el `BB`) es del hito 13 ([08](08-pendientes.md), P-C301 y P-C302).

**El subcódigo.** `0x4e509c` (el `B2`) y `0x4e5883` (el `B8`) toman `pkt[3]` en un C1 y `pkt[4]` en los demás, el
byte que sigue al opcode, como el `Dispatcher` de MuRet. Después cada handler lee a desplazamientos fijos desde el
primer byte, sin mirar el tipo ni el largo: el "resultado" es `pkt[4]`, que en un C2 es el mismo subcódigo (un `C2 B2
01` es un `B2 01` con resultado 1). La tabla del `B2` es `0x4e6140` (33 casos, 0..0x20); 0x0A..0x0F, 0x1B y 0x1C caen
al default (`0x4e5b4a`). El laboratorio manda `C1 05 B2 19 00` después de cada `/move` ([arriba](#los-cambios-de-mapa-1c-f3-04-y-f3-12)):
es de la batalla y sigue sin handler.

**Los mensajes.** Cada caso que muestra algo llama a `0x555190("", Text[n], 1)`: un mensaje de sistema con un nombre
vacío de la `.bss` (`0x5767ab0..0x5767b48`, que nadie escribe, solo se apilan). `n` sale de la dirección del texto,
`(addr − 0x76bd1dc) / 300`.

**La zona de los registros** (`0x7d5c504..0x7d5c658`, `.bss`): las estatuas (tipo `0x11b`) desde `0x7d5c504` y las
puertas (`0x115`) desde `0x7d5c574`, registros de `0x1c`: `+0` el tipo, `+4` el índice, `+8` la defensa, `+0xc` la
recuperación, `+0x10` la vida máxima, `+0x14` la vida, `+0x1a` el NPC está. Después, `0x7d5c638`/`0x7d5c63c` las
elegidas de las ventanas, `0x7d5c640`/`0x7d5c644` los impuestos que muestra y cambia el Castle Senior,
`0x7d5c648`/`0x7d5c64c` los del servidor y `0x7d5c650..0x7d5c657` la plata (64 bits). `0x668fd0` copia un registro
entero (7 dwords) a `base + índice · 0x1c` según su tipo (otro tipo, nada); `0x669030(sel, índice)` da la dirección
con `sel` 0 para las puertas y otro valor para las estatuas. El índice no se mira: un servidor con índices grandes
escribe fuera de la zona (Q-C1360).

| Paquete | Handler | Lo que lee | Lo que hace |
| --- | --- | --- | --- |
| `B2 00` | `0x4e50bd` (tabla `0x4e61c4`) | `pkt[4]`; con 1 o 2, `pkt[5..0x2d]` | 0, 3, 4: los textos 1500, 1501, 1502. 1 y 2: `0x6599a0(0x80000)` sin mirar lo que devuelve y `0x64b0f0`: `+4 = 1`, `+0x1c` = `MOVSX pkt[5]`, `strncpy(+0xa8, pkt + 0x18, 8)` (el gremio dueño), `strncpy(+0xb1, pkt + 0x20, 10)` (su maestro), tres fechas en `+0xbc..+0xcd` (cada una, el año `pkt[i] << 8 | pkt[i+1]` y cuatro bytes, desde `pkt[6]` de a 6) y `+0xd0` = `pkt[0x2a..0x2d]` con el primero arriba |
| `B2 01` | `0x4df080` (tabla `0x4df170`) | `pkt[4]` | 0..8: los textos 1503..1511; con 1, antes, `0x64d290(1)` (`+0xd8` = el byte de abajo) |
| `B2 02` | `0x4df1a0` (tabla `0x4df6ac`) | `pkt[4]` | 0, 2, 3: 1512, 1514, 1515. 1: manda `C1 04 B2 03` y `C1 03 B4`, `0x64d290(0)` y el 1513 |
| `B2 03` | `0x4e5186` | `pkt[4]`; con 1, `pkt[0xd..0x11]` | 0 y 2: `0x64d290(0)`. 1: `0x64d290(pkt[0x11] == 0)` y `0x64d2b0` (`+0xd4`) con `pkt[0xd..0x10]`, el primero arriba |
| `B2 04` | `0x4e51fa` (tabla `0x4e61d8`) | `pkt[4]`; con 1, `pkt[0xd..0x10]` | 0, 2, 3: 1516, 1517, 1518. 1: `0x64d2b0`, sin mensaje |
| `B2 05` | `0x4df7d0` (tabla `0x4df890`) | `pkt[4]`; con 1, los dwords de `pkt + 8` y `pkt + 0xc` | 0, 2, 3, 4: 1519, 1386, 1520, 1616. 1: un registro en la pila (el tipo, el índice, `+8` y `+0xc` en 0, las dos vidas en 500000 para una puerta y 400000 si no, `+0x1a` en 1; `+0x18`, `+0x19` y `+0x1b` sin escribir, Q-C1361) a `0x668fd0` |
| `B2 06` | `0x4e52b7` (tabla `0x4e61e8`) | con 1, cuatro dwords desde `pkt + 8` | 0, 2, 3: 1519, 1386, 1520. 1: `0x669030(tipo == 0x115 ? 0 : 1, índice)`, `+0x14` = `pkt + 0x10` y `+0x10` = `pkt + 0x14` |
| `B2 07` | `0x4df950` (tabla `0x4dfa64`) | con 1, el tipo, el índice y cuál (`pkt + 0x10`); el valor (`pkt + 0x14`) con cuál en 1..3 | 0, 2, 3..7: 1519, 1386, 1520..1524. 1: `0x669030` y, según cuál, `+8` (1), `+0xc` (2) o `+0x10` y `+0x14` (3) |
| `B2 08` | `0x4e537b` | con 1, `pkt[5..0xe]` | 0, 2: 1525, 1386. 1: `0x669070`: `0x7d5c648 = 0x7d5c640 = pkt[5]`, `0x7d5c64c = 0x7d5c644 = pkt[6]` y la plata `pkt[7..0xe]` con el primero arriba |
| `B2 09` | `0x4e53e7` | con 1, `pkt[5]` y, con 1..3, `pkt[6..9]` (el primero arriba) | 0, 2: 1526, 1386. 1: con 3, `[0x7d53700] + 0x18` (el precio de la zona de caza); si no, `0x6690f0`: con 1 el par `0x7d5c648`/`0x7d5c640`, con 2 el `0x7d5c64c`/`0x7d5c644` |
| `B2 10` | `0x4e548a` | con 1, `pkt[5..0xc]` | 0, 2: 1527, 1386. 1: `0x669160`, la plata, el primero arriba |
| `B2 1A` | `0x4e54f6` | `pkt[4]`; con 1 o 2, `pkt[5]` | 1: `[0x78aba34]`; 2: `[0x78aba30]` (`MOVZX`). Los leen `0x44d680`, `0x5b1200`, `0x5b12f0`, `0x5f4720` y `0x5febf0`, la batalla. `0x4dfbb0` hace lo mismo y nadie lo llama |
| `B2 1F` | `0x4e5532` | `pkt[4]`; con 1, `pkt[5]` | 0 y 2: `0x6603c0` y `0x6600d0(Text[860 o 1386], 1, 0x32, 1, 0, 0)` sobre `[0x7d53704]`, el diálogo de los paneles. 1: `[0x7d53700] + 0x15` = `pkt[5] != 0` |
| `B3` | `0x4dfc60` | `pkt[4]`; con 1, la cuenta (int en `pkt + 8`) y registros de `0x1c` desde `pkt + 0xc` | 0, 2: 860, 1386. 1: `0x668fd0` por registro (el lazo con `JLE`/`JL`: una cuenta en 0 o negativa no corre) |
| `B4` | `0x4dfce0` | `pkt[4]`; con 1, la cuenta y registros de `0xe` desde `pkt + 0xc` | 0: 860. 1: vacía la lista de `[0x7d536f8] + 0xe4` (`0x64d2f0`), agrega cada registro (`0x64d2c0(nombre, marcas: rec[8..0xb] con el primero arriba, rec[0xc], rec[0xd])`) y la ordena (`0x64d300`) |
| `B5` | `0x4dfd80` (tabla `0x4dfe24`) | `pkt[4]`; con 1, la cuenta y registros de 10 | 0: 860; 2 y 3: 1609. 1: vacía la lista de `+0x214` (`0x64d340`) y agrega (`0x64d310(rec + 2, rec[0], rec[1])`) |
| `B6` | `0x4e0ed0` | la cuenta (int en `pkt + 4`) y pares `(x, y)` desde `pkt + 8` | solo con `[0x5763530]` y su clase 2: `0x411e80` y `0x411ed0(0, x, y)` por par |
| `B8 01` | `0x4e0700` | `pkt[4]` | sobre el héroe: con 0, si `+0x373 != 0xff` la habilidad (`+0x425`) vuelve a `+0x373`; `+0x373 = 0xff` y `+0x372 = 0`. Con otro valor, `+0x372 = pkt[4]`. `0x511290` inicia `+0x373` en `0xff` (`0x511cc8`) y `+0x372` en 0 (`0x511ccf`) |

**Lo que mandan las ventanas**, todos con el escritor de `0x6b85f4` en la pila, el XOR32 desde el byte 3 y `send()`
directo por el socket del juego (como el `B9 02` de `0x42a760`):

| Paquete | Quién | Bytes |
| --- | --- | --- |
| `C1 04 B2 01` | `0x64c0b0`, el botón del registro con el estado 1 (el héroe con `+0x368 = 0x80` y `+0xd8` en 0) | |
| `C1 05 B2 02 01` | `0x64ccd0`, la respuesta 8 del diálogo | |
| `C1 04 B2 03` | `0x64ae10` (abrir el 0x80000 con `+4` en 0) y el `B2 02` con 1 | |
| `C1 05 B2 04 <lugar>` | `0x64c0b0` con el estado 3: el lugar de la marca en el inventario (`0x64b9d0`) | |
| `C1 0C B2 05` / `06` | `0x6623e0` (las puertas, `0x7d5c590 + elegida · 0x1c`) y `0x664370` (las estatuas, `0x7d5c520`): 05 con `+0x1a` en 0, 06 si no | el tipo y el índice del registro, dwords tal cual |
| `C1 14 B2 07` | `0x662d50` y `0x664ce0` (3), `0x663440` y `0x6653d0` (1), `0x665ab0` (2) | el tipo, el índice, cuál y el valor, dwords tal cual |
| `C1 04 B2 08` | `0x661310`, al abrir el 0x100000 | |
| `C1 09 B2 09 <cuál> <valor>` | `0x666b30` (1 y 2, desde `0x7d5c640`/`0x7d5c644`) y `0x648a30` (3) | el valor con el byte de arriba primero |
| `C1 08 B2 10 <plata>` | `0x667a40`, la respuesta 2 (el texto del campo con `sscanf`) | el byte de abajo primero |
| `C1 05 B2 1F <+0x15 ^ 1>` | `0x648a30` (`0x648c55`) | |
| `C1 04 B3 01` y `C1 04 B3 02` | `0x661310` | |
| `C1 03 B4` y `C1 03 B5` | `0x64d350`; el `B4`, también el `B2 02` con 1 | sin nada que cifrar |
| `C1 08 B9 05 <precio>` | `0x648a30`, el botón de `+0x1c4` en los dos modos | el dword de `+0x18` tal cual |

**MuRet**: `Protocol::CastleSiege::ReadCastleSiegePacket` lee lo mismo a los mismos desplazamientos, y un campo que el
caso lee y no llegó es `TooShort` (se ignora con un aviso, como cualquier paquete que no se lee); los bytes sobrantes se
ignoran. Los escritores dan los bytes de la tabla. `App::ServerSession` registra en un bloque (`RegisterCastleSiegeHandlers`)
el `B2` con los 13 subcódigos de las ventanas, el `B3`..`B6` enteros y el `B8 01`. `Scenes::ApplyCastleSiegePacket`
hace lo de la tabla sobre `WorldRuntime::castleSiege` (la zona plana, como bytes; los campos de `[0x7d536f8]` y
`[0x7d53700]` que escriben los paquetes; `0x78aba30`/`0x78aba34`), el héroe (`ActorState::byte372` y `byte373`) y el
chat. Sin `rand()` en ningún caso de esta sección. Un registro que caería fuera de la zona descarta el paquete entero
con un aviso, sin cerrar la sesión (Q-C1360). Cortan con su dirección: abrir el 0x80000 de verdad (`0x6599a0` →
`0x64ae10`; con el panel ya abierto, `0x6599a0` sale con 0 y `0x64b0f0` corre), las listas del `B4` y el `B5`
(P-C300), los diálogos del `B2 1F` (hito 8, P-C90) y el `B6` con el panel de clase 2 (P-C301). Tests en
`tests/protocol/test_castle_siege.cpp` (byte a byte, en C1 y en C2), `tests/scenes/test_castle_siege_receive.cpp`
(en los mapas 0, 2 y 30) y la sesión; los nombres son inventados.

**Las respuestas del diálogo del Castle Senior** (CL9 hito 12, fase 2, *estático*). Son las funciones que el diálogo de
los paneles (`0x6600d0` sobre `[0x7d53704]`, hito 8, P-C90) llama con la respuesta, un int en `[EBP + 8]`. Todas salen
sin hacer nada si la respuesta no es la que esperan (`CMP EAX,8`; la de la plata, `CMP EAX,2`), toman el registro
elegido como `primero + elegida · 0x1c` en 32 bits (la elegida, un int de `0x7d5c638` para las puertas y de
`0x7d5c63c` para las estatuas; el primero, `0x7d5c590` y `0x7d5c520`, el registro 1 de cada lista: una elegida en −1
cae en el 0) y calculan el valor antes de armar el paquete:

| Respuesta | Qué manda |
| --- | --- |
| `0x6623e0` (puertas) y `0x664370` (estatuas) | `B2 05` con `+0x1a` en 0 y `B2 06` si no: el tipo y el índice del registro |
| `0x662d50` (puertas) y `0x664ce0` (estatuas) | `B2 07` con 3 y `0x660d20(tipo, 0x660da0(registro) + 1)`: la vida máxima del nivel siguiente |
| `0x663440` (puertas) y `0x6653d0` (estatuas) | `B2 07` con 1 y `0x660f30(registro) + 1`: la defensa siguiente |
| `0x665ab0` (estatuas) | `B2 07` con 2 y `0x661020(registro) + 1`: la recuperación siguiente |
| `0x666b30` | dos `B2 09`: el 1 con `[0x7d5c640]` y el 2 con `[0x7d5c644]` (`SAR` 0x18, 0x10, 8: el byte de arriba primero) |
| `0x667a40` | `0x6602d0` da el texto del campo del diálogo y `sscanf(texto, "%u", &[EBP − 0x34])` (`0x6a1dd4`, el formato en `0x6c7b04`); manda `B2 10` con ese dword, el byte de abajo primero. Sin número, el dword es lo que había en la pila (Q-C1363) |

Las tablas, por el tipo de `+0`: `0x660da0` da el nivel de la vida por `+0x10` con signo (una puerta: hasta 500000 el
0, hasta 800000 el 1, 1500000 justo el 2 y si no el 3; una estatua: hasta 400000, 750000 y 1300000, y si no el 3);
`0x660d20(tipo, nivel)` la vida máxima (una puerta: 500000, 800000, y con `SUB 2, NEG, SBB, AND` 1500000 con el 2 y
3000000 con cualquier otro; una estatua: 400000, 750000, 1300000 con el 2 y 2500000 con cualquier otro); `0x660f30` la
defensa (`+8` de una puerta o una estatua); `0x661020` la recuperación (0 para una puerta, `+0xc` para una estatua).
Otro tipo da −1 en las cuatro: la defensa y la recuperación salen en 0, y la vida en −1 (`0x660d20(tipo, 0)`).

En `0x6a1dd4` se llama a `_input` (`0x6a91fc`). La tabla inicial del CRT (`0x6e2100`)
marca como espacios `09..0d` y `20`. `0x6a990f` acepta un signo, `0x6a9aa4/0x6a9aea`
acumula los dígitos módulo 2³² y `0x6a9b1f` niega después si el signo es menos.
Sin dígitos, la conversión vuelve sin escribir el destino (`0x6a9b2a`).

En MuRet, `CastleWindowAnswers` conecta el diálogo de los paneles con las respuestas de
`CastleSeniorAnswers`. Las pruebas de `CastleSeniorAnswersTest` cubren la clasificación
de los 256 bytes iniciales y el desborde del importe. El cliente registra un aviso y
omite el envío si la selección cae fuera de la zona (Q-C1360) o si no hay dígitos
(Q-C1363). Queda por capturar el DWORD residual que envía el original en este último caso.

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
| Los paquetes de los eventos, sin guarda de escena, con `sprintf` o `wsprintfA` según el handler | `Protocol::Events` (lectores y los escritores del `97` y del `AF 02`) y `Scenes::ApplyEventPacket` sobre `WorldRuntime::events` y el objeto del evento; `UI::FormatOriginal` para los dos formatos ([Los eventos](#los-eventos-0b-46-47-909e-af-bc-bd-y-f3-222340)) | CL9 hito 11 |

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
