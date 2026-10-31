# 03. Red y cifrado

Fuentes: `analysis/mainexe/functions/paso3-protocolo.md`, `analysis/mainexe/evidence/s35-red/`,
`analysis/mainexe/evidence/s31-cifrado/`, `mu-client/docs/component-net-envio.md` y
`mu-client/docs/component-packet.md`.

Es la parte mejor validada del cliente: la recepción y el cifrado C3/C4 se compararon contra el
cable real del laboratorio (72 conexiones, 3.033 paquetes del servidor, 183 C3 del cliente).

## Sockets

- Winsock por ordinal (`socket`, `connect`, `send`, `recv`, `shutdown`, `closesocket`,
  `WSAAsyncSelect`, `WSAStartup(0x202)`…), más `MessageBoxA` para los errores de red.
- `0x4e81b0`: `WSAStartup` con mensajes de error en coreano ("소켓 생성 에러").
- `0x4e8270` / `0x4e8380`: cierre ordenado: `shutdown(sock, 1)`, drenar `recv` hasta 0,
  `closesocket`.
- Los eventos llegan a la ventana con `WSAAsyncSelect` (`FD_READ`, `FD_WRITE`): todo pasa en el
  hilo de la ventana. *Estático.*
- El objeto de conexión vive en `0x57630C8`.

## Cadena de recepción

```text
WndProc (FD_READ) → 0x4e88a0: recv a buf[0x2000], n en +0x4014
   └─ n >= 3 → 0x6b7cb0: n = FoxAnti.decode_inbound(n, buf)        (tabla 0x6b7d00[0])
        └─ 0x4e893d: corte C1/C2/C3/C4 → 0x4e8f70 push a una cola FIFO
WinMain (cada vuelta) → 0x4e1d50
   └─ 0x4e8a80 pop → si C3/C4: Dec2.Decrypt (0x685b30) + serial 0x5767914 → rearmado C1/C2
        └─ call [0x6b7d08](op, pkt, size, cifrado) = FoxAnti.recv_forward → dispatcher 0x4e2b10
```

Verificado contra el cable (`red_reference.py --golden`): el flujo S->C de 72 conexiones se corta
en 3.033 paquetes sin bytes sobrantes, incluidas 337 lecturas con 2 a 7 paquetes juntos.

- **Corte:** igual que el DataServer: C1/C3 tienen el tamaño en `pkt[1]`, C2/C4 en `pkt[1..2]`
  big-endian. El tamaño es el del paquete completo.
- **Rearmado:** un C3 descifrado se reescribe como C1 (y un C4 como C2) con tamaño = longitud
  descifrada. Como esa longitud incluye el byte del serial, el paquete rearmado declara `n` pero
  ocupa `n + 1` (C4: `n + 2`). Verificado con `F1 02`.
- **Nadie en `main.exe` llama al dispatcher directo:** solo se llega por el puntero que instala
  FoxAnti. Sin FoxAnti, el cliente original no despacha nada.

## Envío: el motor C1/C2 y el XOR32

Todo C1/C2 que manda el cliente pasa por un motor inlineado en cada emisor (vtable `0x6b85f4`),
con esta tabla de 32 bytes:

```text
e7 6d 3a 89 bc b2 9f 73 23 a8 fe b6 49 5d 39 5d 8a cb 63 8d ea 7d 2b 5f c3 b1 e9 83 29 51 e8 56
```

- **begin:** C1 → `buf[0]=C1`, `buf[2]=op`, tamaño 3 (el opcode en claro). C2 → `buf[0]=C2`,
  `buf[3]=op ^ key[3]`, tamaño 4.
- **put:** para cada byte nuevo en el índice `i`: si `i >= 3`, `valor ^= key[i & 31] ^ buf[i-1]`.
  Tope `0x800` bytes; lo que sobra se ignora en silencio.
- **finish:** escribe el tamaño (C2 en big-endian).

*Estático*, con fixtures de `mu-client` (`test_net_send`).

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
- Checksum por bloque con las constantes `0xF8` / `0x3D`; un byte alterado lo hace fallar.

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
| `send_forward` | en el camino hacia el cifrado C3 (`0x685AC0`): remapeos de opcode `0x10→0x1D`, `0x19→0xA5`, `0x1E→0xA6`, y luego el trampolín de envío |
| `recv_forward` | calcula el opcode según la cabecera y llama al dispatcher |
| `encode_outbound` | sobre todo C1 saliente: deshace el XOR32 del juego, remapea `D3→A3` y `D7→A4` y lo vuelve a aplicar (sin remapeo, el cable queda igual); a `C1 06 F4 03` le traduce el código de servidor con una escalera de 8 rangos. C3/C4 pasan sin tocar |

El GameServer del laboratorio (con `MyDll.dll`) espera esta capa: **un cliente sin ella no habla
con ese servidor**. Un servidor de MuRet podría no usarla.

Verificado en el cable ([04](04-protocolo.md), "Cómo se verificó"): las excepciones del `0xAA`
(el CS entero va en claro y en el GS los 398 segmentos S->C lo llevan), los remapeos `A3`/`A4` y
`A6`, y que fuera de ellos un C1 del cliente se lee deshaciendo solo el XOR32. La traducción de la
lista y del `F4 03` es *estática*: el cable solo muestra el código 0, y la ida y vuelta de códigos
desde 21 no cierra (Q-C17).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Corte de paquetes con bugs de cabecera incompleta (Q-C03) | `PacketReader` puro (bytes → paquetes), sin red, con los vectores del cable como tests; los bugs se arreglan y se documentan | CL2 |
| Motor XOR32 inlineado en cada emisor | un `PacketWriter` C1/C2 con el XOR32 como etapa separada y testeable | CL2 |
| C3/C4 con dos seriales globales | `SimpleModulus` (nombre de la comunidad) + `CryptoKey` cargada de `Enc1.dat`/`Dec2.dat`; seriales por conexión, no globales | CL2 |
| FoxAnti como DLL que parchea el binario | `Network::Obfuscation` opcional (interfaz con dos implementaciones: identidad y FoxAnti), elegida por config según el servidor | CL2, CL4 |
| `WSAAsyncSelect` en el hilo de la ventana | socket no bloqueante consultado por el bucle (un hilo); el envío que no entra se encola como en `FD_WRITE` | CL4 |
| Pila de ~2 KB en `0x4e1d50` que un C4 grande desborda (Q-C05) | buffers con tamaño máximo explícito; un paquete más grande se rechaza | CL2 |

Tests que fija:

- Vectores del corte: los casos de `red_reference.py --selftest` (corte, resto parcial, cabecera
  incompleta, cabecera inválida, tamaño 0, serial, checksum malo) (CL2).
- XOR32: los fixtures de `test_net_send` de `mu-client` (CL2).
- C3/C4: round-trip con claves sintéticas y, si está `mu-golden`, descifrar los C3 reales con la
  clave real y comparar byte a byte (CL2).
- Seriales: 0 al conectar, +1 por paquete, comportamiento ante desfase (CL2).
- FoxAnti: los fixtures de `test_net_foxanti` (CL2), las tres excepciones del `0xAA`, y la lista
  `C2 00 0A F4 02 01 00 00 00 CC` del cable → `C2 00 0B F4 06 00 01 90 01 00 00`, con
  `C1 06 F4 03 90 01` → `C1 06 F4 03 00 00` de vuelta (CL2).
