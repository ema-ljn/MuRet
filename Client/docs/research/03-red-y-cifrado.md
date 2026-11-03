# 03. Red y cifrado

Fuentes: `analysis/mainexe/functions/paso3-protocolo.md`, `analysis/mainexe/evidence/s35-red/`,
`analysis/mainexe/evidence/s31-cifrado/`, `mu-client/docs/component-net-envio.md` y
`mu-client/docs/component-packet.md`.

Es la parte mejor validada del cliente: la recepción y el cifrado C3/C4 se compararon contra el
cable real del laboratorio (72 conexiones, 3.033 paquetes del servidor, 183 C3 del cliente).

## Sockets

Leído en el decompilado y confirmado en el desensamblado con `objdump` en la CL4.R (*estático*);
lo que dice *verificado* se vio en el cable (`muproxy.log`, 64 conexiones al CS y 8 al GS).

- Winsock por ordinal (`socket`, `connect`, `send`, `recv`, `setsockopt`, `shutdown`,
  `closesocket`, `WSAAsyncSelect`, `WSAStartup(0x202)`…), más `MessageBoxA` para los errores de
  red. FoxAnti reemplaza el slot de `send` (`0x6b8588`).
- **El objeto de la conexión principal** está en `0x57636c0`, y los emisores lo leen por el
  puntero de `0x6c82a0`: la ventana en `+0x0`; en `+0x4`, si es la principal (1) o una del chat
  (0); el socket en `+0xc`; el buffer de envío en `+0x10` (`0x2000` bytes) con lo pendiente en
  `+0x2010`; el de recepción en `+0x2014` (`0x2000`) con lo que hay en `+0x4014`; la cola de
  paquetes en `+0x4020`. Con `+0x4018` distinto de 0 avisa lo que mandó a `0x4e8880`, que en este
  build es un `ret`.
- **Iniciar:** `0x4e81b0`, `WSAStartup(0x202)`. Si falla, loguea "Winsock DLL Initialize error."
  (`0x6c8718`) y muestra un `MessageBoxA` con otra cadena (`0x6c8700`); si la versión no alcanza,
  loguea "Winsock version low." (`0x6c86e8`), y el `MessageBoxA` muestra la de `0x6c86cc`.
  `0x4c1340` la llama la primera vez que conecta (`DAT_057636bc`).
- **Crear:** `0x4e82e0`, `socket(2, 1, 0)`: IPv4 y TCP, todavía bloqueante. Guarda su segundo
  argumento en `+0x4`, y con uno distinto de 0 pone `DAT_05767918` en 0 (`0x4e8305`). Si falla,
  loguea "소켓 생성 에러 %d" con `WSAGetLastError`.
- **Conectar:** `0x4e8570(host, puerto, mensaje)`. Sin ventana en `+0x0`, un `MessageBoxA` y 0.
  `inet_addr` y, si no es una IP, `gethostbyname`; si el nombre no resuelve **devuelve 2**
  (`0x4e85cf`). Después rechaza `7F 00 00 01` (`0x4e85f7`..`0x4e8611`: devuelve 0), también si llegó
  por un nombre (`localhost`), y solo esa dirección. Ni con el 2 ni con el rechazo cierra el socket
  que creó `0x4e82e0`: lo cierra el primer envío que falla (en la escena de login, el `A9`), o
  nadie, y el `0x4e82e0` siguiente lo pierde (Q-C50, Q-C51). Recién entonces `connect` (`0x4e8625`),
  con el socket todavía bloqueante: espera a que termine. Si da -1 con otro error que
  `WSAEWOULDBLOCK` (`0x2733`, que un socket bloqueante no da), `closesocket` y 0. Por último
  `WSAAsyncSelect(socket, ventana, mensaje, 0x23)` (`0x4e865e`): `FD_READ`, `FD_WRITE` y `FD_CLOSE`,
  sin `FD_CONNECT`; desde ahí el socket es no bloqueante (Q-C50, Q-C51).
- **El que conecta:** `0x4c1340(host, puerto)` loguea `[Connect to Server] ip address = %s, port
  = %d`, crea el socket con 1 y conecta con el mensaje `0x400`. Solo un 0 es error: loguea "Failed
  to connect." y abre el aviso `0x70` de la UI (`0x65fe50`, el texto 402 de `Text.bmd`), y
  devuelve 0; el 2 sigue como si hubiera conectado. Con error o sin él, pone en 0 los dos seriales
  (`0x4c14ac` y `0x4c162c`). Lo llaman la escena de login con el ConnectServer (`0x6349eb`), la
  vuelta al ConnectServer (`0x642837`, en `0x642810`), el `F4 03` con el GameServer (`0x4e3396`),
  el sub 0 de `B1` con otro GameServer (`0x412b86`, en `0x412b30`) y `0x4c1b9f`, en una copia del
  handler del `F4 03` que no tiene llamadores.
- **Otra conexión:** `0x49de50` arma un objeto de `0x402c` bytes por sala, llama a `0x4e81b0` sin
  la bandera, crea el socket con 0 (no toca `DAT_05767918`) y conecta al puerto 55980 con el
  mensaje `0x500` más un índice (`0x49df01`). La IP llega en el `CA` del servidor (`0x4dce10`,
  `pkt+3`, 15 bytes), con el número de sala y un ticket: es el chat del sistema de amigos
  (*inferido*: el layout del `CA` es el del resultado de crear una sala). No es del login.
- **Los eventos:** el mensaje `0x400` del WndProc (`0x4b31b5`) mira `lParam & 0xFFFF`, así que no
  ve el error que trae la palabra alta: 1 (`FD_READ`) va a `0x4e88a0`, 2 (`FD_WRITE`) a
  `0x4e87b0`, y `0x20` (`FD_CLOSE`) muestra el texto 3 de `Text.bmd` en pantalla (`0x555190`),
  loguea "서버와의 접속종료." ("se terminó la conexión con el servidor"), cierra con `0x4e8380` y
  abre el aviso `0x70`. Todo pasa en el hilo de la ventana.
- **Leer:** `0x4e88a0`. Con el buffer lleno no lee y lo loguea (Q-C40). Un `recv` hasta llenar los
  `0x2000`; con 0 o con -1, por cualquier error, vuelve sin cerrar: el cierre lo hace `FD_CLOSE`.
  Con menos de 3 bytes juntos espera; si no, salta a `decode_inbound` y al corte (abajo).
- **Despachar:** `0x4e1d50(cola, 0, 0)`, en cada vuelta del bucle de `WinMain` (`0x4be5bc`), con
  foco o sin él, saca **todos** los paquetes de la cola (cada caso vuelve a `0x4e1d75`) y llama al
  dispatcher. Enseguida `0x49e510` hace lo mismo con cada conexión del chat, con otro handler
  (`0x4be5ca`).
- **Mandar:** cada emisor manda en línea: un lazo de `send` desde lo ya enviado, sin mirar lo que
  quedó pendiente en `+0x2010` (Q-C52). Con `WSAEWOULDBLOCK`, si `pendiente + total` entra en
  `0x2000`, copia al buffer de envío lo que falta, pero **desde el principio del paquete** (`lea
  esi,[ebp-X]`): el mismo defecto que Q-C11. Si no entra, loguea `[Send Packet Error] SendBuffer
  Overflow` y cierra; con otro error, `[Send Packet Error] WSAGetLastError() != WSAEWOULDBLOCK` y
  cierra. Un `send` que da 0 corta el lazo como si hubiera salido todo. Leído en el keepalive del
  timer (`0x4b162a`..`0x4b17b7`) y en el aviso `F1 03` (`0x4e22f1`..`0x4e23c7`); las 492 copias en
  línea a `+0x10 + [+0x2010]` del binario toman el principio de un buffer de la pila (*estático*,
  contadas en el desensamblado: 485 con `lea esi,[ebp-X]` y 7 con un puntero guardado, `mov
  esi,[ebp-X]`, como `0x4ae48e`), y `0x4e8690`, la versión que no está en línea y no tiene
  llamadores, hace lo mismo. Por esos lazos, con el error y el desborde, `0x4e8380` tiene 1.264
  `call`.
- **Drenar:** `0x4e87b0`, con `FD_WRITE`. `enviado` vuelve a 0 en cada `FD_WRITE` (`0x4e87b4`).
  Manda desde `buf + enviado` el largo `pendiente - enviado`, con `pendiente` ya descontado: resta
  dos veces lo que salió. Con `WSAEWOULDBLOCK` vuelve sin correr lo que quedó al principio del
  buffer. Con otro error loguea `FD_WRITE Send Error = %d` y cierra; con un `send` que da 0, cierra
  sin loguear (Q-C11). Basta un parcial para repetir el principio y perder el final, y después de
  un parcial ese `FD_WRITE` no termina bien: lo enviado nunca llega a lo pendiente, y el lazo sale
  con `WSAEWOULDBLOCK`, con un largo 0 o con uno negativo (*estático*; los casos, con el jefe de
  la CL4.0, en Q-C11).
- **Cerrar:** `0x4e8380`. Con `+0x4` distinto de 0, `DAT_05767918` en 0. `setsockopt(SOL_SOCKET,
  SO_LINGER, {1, 0})` (`0x4e83b9`), con lo que `closesocket` corta con un RST y tira lo que quedaba
  por mandar; vacía los dos buffers y la cola de paquetes, loguea `[Socket Closed][Clear
  PacketQueue]`, `closesocket` y el socket en -1. Es el que corre en todos los cierres: al pasar
  del CS al GS (`0x4e3388`), con `FD_CLOSE`, con un error de envío y en `WM_DESTROY` (Q-C54).
- **El cierre ordenado no se usa:** `0x4e8270` hace `shutdown(socket, 1)`, `recv` hasta 0 y
  `closesocket`, y `0x4e8520` `closesocket` con `DAT_05767918` en 0. Ninguna de las dos tiene un
  `call` ni un puntero en todo el archivo, que no tiene relocaciones.

## Cadena de recepción

```text
WndProc (FD_READ) → 0x4e88a0: recv a buf[0x2000], n en +0x4014
   └─ n >= 3 → 0x6b7cb0: n = FoxAnti.decode_inbound(n, buf)        (tabla 0x6b7d00[0])
        └─ 0x4e893d: corte C1/C2/C3/C4 → 0x4e8f70 push a una cola FIFO
WinMain (cada vuelta) → 0x4e1d50
   └─ 0x4e8a80 pop → si C3/C4: Dec2.Decrypt (0x685b30) + serial 0x5767914 → rearmado C1/C2
        └─ call [0x6b7d08](op, pkt, size, cifrado) = FoxAnti.recv_forward → dispatcher 0x4e2b10
                                                     (un C1 F3 11 pasa antes por reset_queue)
```

`decode_inbound` corre **antes** del corte, sobre todo lo que devolvió `recv`: decide el `0xAA`
por el primer paquete de la lectura y lo aplica a la lectura entera (Q-C18).

Verificado contra el cable (`red_reference.py --golden`): el flujo S->C de 72 conexiones se corta
en 3.033 paquetes sin bytes sobrantes, incluidas 337 lecturas con 2 a 7 paquetes juntos.

- **Corte:** igual que el DataServer: C1/C3 tienen el tamaño en `pkt[1]`, C2/C4 en `pkt[1..2]`
  big-endian. El tamaño es el del paquete completo. Con una cabecera incompleta, un tipo que no
  es `C1` a `C4`, un tamaño que no llega al opcode o uno de más de `0x2000`, el original se
  equivoca: Q-C03, Q-C38, Q-C39 y Q-C40 en [07](07-quirks-y-bugs.md).
- **Rearmado:** un C3 descifrado se reescribe como C1 (y un C4 como C2) con tamaño = longitud
  descifrada. Como esa longitud incluye el byte del serial, el paquete rearmado declara `n` pero
  ocupa `n + 1` (C4: `n + 2`). Verificado con `F1 02`.
- **Nadie en `main.exe` llama al dispatcher directo:** solo se llega por el puntero que instala
  FoxAnti. Sin FoxAnti, el cliente original no despacha nada.

## Envío: el motor C1/C2 y el XOR32

Casi todo C1/C2 que manda el cliente pasa por un motor inlineado en cada emisor (vtable
`0x6b85f4`), con esta tabla de 32 bytes; los `F4` al ConnectServer salen en claro
([04](04-protocolo.md)):

```text
e7 6d 3a 89 bc b2 9f 73 23 a8 fe b6 49 5d 39 5d 8a cb 63 8d ea 7d 2b 5f c3 b1 e9 83 29 51 e8 56
```

- **begin:** C1 → `buf[0]=C1`, `buf[2]=op`, tamaño 3. C2 → `buf[0]=C2`, `buf[1..2]=01 00`
  (provisorio), `buf[3]=op`, tamaño 4. En los dos el opcode va en claro.
- **put:** cada byte que se agrega después del opcode (desde el índice 3 en un C1, desde el 4 en
  un C2) hace `valor ^= key[i & 31] ^ buf[i-1]`, con `buf[i-1]` ya cifrado: el primero se
  encadena con el opcode. Tope `0x800` bytes; lo que sobra se ignora en silencio.
- **finish:** escribe el tamaño (C2 en big-endian).

*Estático*. El C1, con los fixtures de `mu-client` (`test_net_send`) y verificado en el cable
(`C1 04 F3 00` sale como `C1 04 F3 7A`). El C2, en su único emisor del build, la ventana de cartas
(`0x498820`): `0x498b2c..0x498b89` escribe `C2`, `01 00` y el opcode `C5` sin XOR, y el XOR32 de
lo que sigue (`0x498c54..0x498c83`) empieza en el índice 4 encadenado con el `C5`. En el cable
del laboratorio el cliente no manda ningún C2. `mu-client` cifra el opcode del C2 con `key[3]`
(`src/net/send.c`), sin evidencia, y sus fixtures `FIX_C2_PLAIN` y `FIX_C4_SERIAL2` salen de ese
modelo: no sirven de oráculo para el C2.

## C3/C4: cifrado por bloques con `Enc1.dat` y `Dec2.dat`

| VA | Rol |
| --- | --- |
| `0x686240` | carga del archivo de clave: magic `0x1112`, 54 bytes, XOR con la tabla `0x6d84fc` |
| `0x685ac0` / `0x685bb0` | `Encrypt`: bloques de 8 bytes en claro → 11 bytes cifrados |
| `0x685b30` / `0x685cc0` | `Decrypt`: bloques de 11 → 8, con checksum |
| `0x685e20` / `0x685f60` | copia y desplazamiento de campos de bits, MSB primero |

- Cada clave tiene 4 carriles de (módulo, clave, xor). Las del cliente son el inverso modular de
  las del servidor: `Enc1 = (M1, Dec1⁻¹, X1)` y `Dec2 = (M2, Enc2⁻¹, X2)`.
- **Serial de envío:** byte global `0x05767913`, 0 al conectar al GS (`0x4c1340`), +1 por cada
  C3/C4 enviado. Va como primer byte del texto en claro.
- **Serial de recepción:** `0x05767914`. Si no coincide, el cliente loguea, **no** incrementa y
  despacha igual (ver Q-C04 en [07](07-quirks-y-bugs.md)).
- **El envoltorio**, inlineado en cada emisor (por ejemplo `0x4e2198..0x4e2500`, el del aviso
  `F1 03`): copia el C1/C2 ya con XOR32, pone un byte de `rand()` detrás (`buf[len]`), pisa el
  tamaño con el serial (`[1]` en un C1, `[2]` en un C2) y cifra desde ahí: `size - 1` bytes útiles
  en un C1 y `size - 2` en un C2. El resto del último bloque es lo que haya en la pila. Si lo
  cifrado mide menos de `0x100` bytes sale como C3, `[C3][largo + 2]`, y si no, como C4,
  `[C4][hi][lo]` con el largo + 3 (`cmp eax,0x100`, en `0x4e22a8`): un C1 de 186 bytes o más sale
  como C4. La ventana de cartas (`0x498820`), el único emisor de un C2, arma siempre un C4: pone el
  `0xC4` sin el `cmp` (`0x499414..0x49945d`, el `mov` en `0x49941e`).
- **El rearmado** (`0x4e1d50`): un C3 vuelve como `[C1][n]` y un C4 como `[C2][n >> 8][n & 0xFF]`,
  seguidos de lo descifrado sin el serial. `n` cuenta el serial, que no se copia, así que el paquete
  declara uno menos de lo que ocupa en un C1 y dos menos en un C2 (Q-C10). Un C1 que salió como C4
  vuelve como C2.
- **Checksum malo:** no despacha, suma 1 al serial de recepción y manda el aviso
  `C1 06 F1 03 06 00` (con XOR32, `C1 06 F1 7B C1 73`) como C3 (`0x4e1e35..0x4e22d4`,
  `004e1d50-protocol-compiler.md`); qué hace el servidor con él no se sabe. Es lo único que no
  despacha: con un bloque que dice más de 8 útiles, uno del medio con menos o el último con 0,
  suma lo que dicen, despacha y compara el serial como siempre (`0x685cc0` no acota el largo útil
  y `0x685b30` lo suma). Despacha también un solo bloque que trae solo el serial: el opcode que lee,
  `dec[1]`, es el byte que le sigue, el de relleno.
- **Lo que no mide bloques enteros:** `0x685b30` lee `11 * ceil(largo / 11)` bytes, así que con
  el último bloque partido lee hasta 10 bytes después del paquete, y el checksum de ese bloque casi
  siempre falla: lo más probable es que mande el mismo aviso (*probable*, sin probar;
  `00685b30-decrypt-buffer.md`).
- Checksum por bloque con las constantes `0xF8` / `0x3D`: es un byte, así que un cambio en un carril
  falla casi siempre (pasa uno de cada 256, más o menos), uno en el checksum siempre, y uno en el
  byte del largo útil nunca.

Verificado: los 183 C3 del cliente en el cable descifran con el `Dec1.dat` del servidor y, al
recifrarlos con `Enc1`, dan los mismos bytes (183/183). Los 25 C3/C4 del servidor descifran con el
`Dec2.dat` real del cliente (`s31-cifrado/README.md`, `s35-red/README.md`). En la sesión real del
build actual ([04](04-protocolo.md)), lo mismo: 23 C3 del cliente con serial 0 a 22 y 17 C3/C4
del servidor con serial 0 a 16, todos con checksum bien.

## La capa de FoxAnti

FoxAnti instala tres punteros en `0x6B7D00` y reemplaza el slot de `send` de la IAT. Sin su
protección, la parte de paquetes (la "FoxAnti mínima", validada en vivo: el cliente entra al
mundo) es:

| Pieza | Qué hace |
| --- | --- |
| `decode_inbound` | deshace el XOR `0xAA` que el servidor del laboratorio aplica a lo que manda, **salvo** C1 con opcode `00`, C1 con opcode `F4` y C2 con opcode `F4` (los paquetes del CS); además reescribe la lista `C2 F4 02` del laboratorio como el `C2 F4 06` que espera `main.exe` (`parse_f4_02`, ver [04](04-protocolo.md)) |
| `send_forward` | en el camino hacia el cifrado C3 (`0x685AC0`): remapeos de opcode `0x10→0x1D`, `0x19→0xA5`, `0x1E→0xA6`, y luego el trampolín de envío. Deshace y rehace el XOR32 desde el byte que sigue al opcode hasta el de relleno, así que con un remapeo cambian también los campos y el relleno, que se encadenan con el opcode (*estático*, con la FoxAnti mínima: cada byte queda con el XOR del opcode viejo y el nuevo). Solo transforma cuando el destino es NULL: es la primera de las dos llamadas que hace el juego por cada C3, la que pide el tamaño (`0x406831`: `push 0; call 0x685ac0`); el remapeo queda en el buffer y la segunda llamada cifra el buffer ya remapeado |
| `recv_forward` | calcula el opcode según la cabecera (si no es C1..C4, usa su primer argumento) y llama al dispatcher. Antes, a todo C1/C3 `F3 11` lo reescribe (`reset_queue`, `0x3412C0`): mete un `00` en `pkt[5]` y suma 1 al tamaño (`C1 08 F3 11 FE 04 16 68` → `C1 09 F3 11 FE 00 04 16 68`) |
| `encode_outbound` | sobre todo C1 saliente: deshace el XOR32 del juego, remapea `D3→A3` y `D7→A4` y lo vuelve a aplicar (sin remapeo, el cable queda igual); a `C1 06 F4 03` le traduce el código de servidor con una escalera de 8 rangos. C3/C4 pasan sin tocar |

El GameServer del laboratorio (con `MyDll.dll`) espera esta capa: **un cliente sin ella no habla
con ese servidor**. Un servidor de MuRet podría no usarla.

Además de ofuscar, **la capa traduce formatos**: el servidor del laboratorio y `main.exe` no
escriben igual la lista de servidores (`F4 02` contra `F4 06`), el `F3 11` (el byte de más en
`pkt[5]`), algunos opcodes C->S (`D3`, `D7`, `0x10`, `0x19`, `0x1E`) ni el código del servidor en
`F4 03`. FoxAnti es lo único que conoce las dos formas; el dispatcher y los handlers de `main.exe`
leen siempre la suya. Del lado del servidor, `MyDll.dll` tiene su propia parte de la capa (el
mismo `0xAA` y otros remapeos al recibir:
`mydll-gameserver/decompilacion-completa/reimpl/ANALISIS_FOXANTI_PACKETS.md`), que importa para
un servidor de MuRet, no para el cliente.

Verificado en el cable ([04](04-protocolo.md), "Cómo se verificó"): las excepciones del `0xAA`
(el CS entero va en claro y en el GS los 398 segmentos S->C lo llevan), los remapeos `A3`/`A4` y
`A6`, y que fuera de ellos un C1 del cliente se lee deshaciendo solo el XOR32. Los dos `F3 11` de
la sesión real quedan, después de `reset_queue`, con el índice del slot en `pkt[6]`, que es donde
lo lee el handler de `main.exe` (`0x4c5070`). La traducción de la lista y del `F4 03` es
*estática*: el cable solo muestra el código 0, y la ida y vuelta de códigos desde 21 no cierra
(Q-C17).

Los defectos de la capa original (largos y cantidades sin validar) están en Q-C09; la FoxAnti
mínima los reproduce a propósito, MuRet no.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Corte de paquetes con bugs de cabecera incompleta (Q-C03) | `PacketReader` puro (bytes → paquetes), sin red, con los vectores del cable como tests; los bugs se arreglan y se documentan | CL2 |
| Motor XOR32 inlineado en cada emisor | un `PacketWriter` C1/C2 con el XOR32 como etapa separada y testeable | CL2 |
| C3/C4 con dos seriales globales | `SimpleModulus` (nombre de la comunidad) + `EncryptionKey` y `DecryptionKey`, cargadas de `Enc1.dat`/`Dec2.dat`; seriales por conexión, no globales | CL2 |
| Las claves y los seriales son globales: un C3/C4 que llegara en la conexión al CS se descifraría con los mismos que en el GS (el CS del laboratorio manda todo en claro) | las claves son de cada conexión (`Connection::Open` las recibe) y el CS abre sin claves: un C3/C4 que llega ahí es un `Undecryptable` sin error (`std::nullopt`), que no manda el `F1 03` ni mueve un serial (`AnEncryptedPacketWithoutKeysIsUndecryptable`) | CL4 |
| FoxAnti como DLL que parchea el binario | `Network::IObfuscation`, una interfaz con dos implementaciones, `IdentityObfuscation` y `FoxAntiObfuscation`, elegida por config según el servidor (CL4). Cada pieza de FoxAnti es una etapa, en el orden del original: `InboundMask` en el `PacketReader`, `AfterCut` (la lista), `BeforeDispatch` (el `F3 11`), `BeforeEncrypt` dentro de `EncryptPacket` (con el relleno) y `BeforeSend` | CL2, CL4 |
| FoxAnti traduce el formato del laboratorio al de `main.exe` | la traducción vive solo en `Network::IObfuscation`; `Protocol` lee siempre el formato de `main.exe` (`F4 06`, `F3 11` con su byte de más), venga del servidor que venga | CL2, CL4 |
| El `0xAA` se decide por lectura de `recv` | se decide por paquete, mirando el primer byte en el cable (`6B`, `68`, `69`, `6E` llevan `0xAA`; `C1`, `C2`, `C3` y `C4` van en claro) (Q-C18) | CL2 |
| `WSAAsyncSelect` en el hilo de la ventana | socket no bloqueante consultado por el bucle (un hilo); el envío que no entra se encola como en `FD_WRITE` | CL4 |
| `connect` bloqueante; rechaza `127.0.0.1` (Q-C50) y sigue con un nombre que no resuelve (Q-C51) | `connect` no bloqueante con un `ConnectTimeout`; `127.0.0.1` se acepta y un nombre que no resuelve es `ResolveFailed` | CL4 |
| Cada emisor manda en línea sin mirar lo pendiente y, con `WSAEWOULDBLOCK`, encola el principio del paquete (Q-C52); el drenaje repite y pierde bytes (Q-C11) | una `SendQueue` con cursor: con algo pendiente lo nuevo va detrás, y lo que salió no se repite | CL4 |
| Todo cierre es con RST (`SO_LINGER {1, 0}`, Q-C54); el cierre ordenado de `0x4e8270` no se usa | `close` o `closesocket` sin `SO_LINGER`: el otro lado ve un FIN | CL4 |
| El rearmado declara menos de lo que ocupa (Q-C10), el serial de recepción no se resincroniza (Q-C04) y el descifrado suma los largos útiles sin mirarlos | `DecryptPacket` rearma con el tamaño que ocupa, resincroniza el serial y lo informa (`SerialCheck::Resynced`), y rechaza un largo útil imposible (`InvalidBlock`) y lo que no son bloques enteros (`NotWholeBlocks`); `EncryptPacket` sigue la regla de C3 o C4 del envoltorio, con el relleno como parámetro y el resto en 0 | CL2 |
| Pila de ~2 KB en `0x4e1d50` que un C4 grande desborda (Q-C05), y un paquete de más de `0x2000` que deja de leer la conexión (Q-C40) | el corte rechaza lo que pasa de `MaxPacketSize` (`0x2000`), y el descifrado escribe en un buffer del tamaño de lo que entra | CL2 |

Tests que fija:

- Vectores del corte: los casos de `red_reference.py --selftest` (corte, resto parcial, cabecera
  incompleta, cabecera inválida, tamaño 0, serial, checksum malo) (CL2).
- XOR32: los fixtures de `test_net_send` de `mu-client`, salvo los del C2, que se arman de nuevo
  con el opcode en claro (CL2).
- C3/C4: round-trip con claves sintéticas y, si está `mu-golden`, descifrar los C3 reales con la
  clave real y comparar byte a byte (CL2).
- Seriales: 0 al conectar, +1 por paquete, comportamiento ante desfase (CL2).
- FoxAnti: los fixtures de `test_net_foxanti` (CL2), las tres excepciones del `0xAA`, y la lista
  `C2 00 0A F4 02 01 00 00 00 CC` del cable → `C2 00 0B F4 06 00 01 90 01 00 00`, con
  `C1 06 F4 03 90 01` → `C1 06 F4 03 00 00` de vuelta, y el `F3 11` del cable
  `C1 08 F3 11 FE 04 16 68` → `C1 09 F3 11 FE 00 04 16 68` (CL2).
- FoxAnti, defectos corregidos: `C1 04 F3 11` y una lista con `count` mayor que el paquete se
  rechazan sin leer de más; con paquetes bien formados la salida es la de la FoxAnti mínima (CL2).
