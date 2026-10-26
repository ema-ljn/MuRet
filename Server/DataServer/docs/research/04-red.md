# 04. Red: framing, conexiones y envío

Este documento describe la capa de red: cómo se corta el flujo TCP en paquetes (el parser), cómo se
guardan las conexiones, y cómo se envía. El fuente original es `giocp.cpp` (ver los `__FILE__` en
[01](01-binario-y-metodo.md)). Todo lo de acá es **Verificado** en el assembly salvo donde diga lo
contrario.

## Dos sockets de escucha

| Socket | Puerto | Cómo | Para qué |
| --- | --- | --- | --- |
| principal | `P` (55960) | `WSASocket` overlapped + `bind` + `listen(5)` (`0x4029c0`); bucle bloqueante `WSAAccept` en el hilo de accept | tráfico GS↔DS; cada socket aceptado se asocia al IOCP |
| port check | `P+1` (55961) | `socket` + `bind` + `listen(8)` + `WSAAsyncSelect(FD_ACCEPT, 0x7EA)` (`0x405870`) | sonda de "¿el proceso está vivo?": acepta y loguea, **no lee ni cierra** el socket |

El port check corre sobre la UI: al llegar `FD_ACCEPT`, `CMainFrame::OnSocketNotify` llama a
`0x404a50`, que hace `accept` y loguea `port check accept`. El socket aceptado queda abierto para
siempre: es una fuga de handle **deliberada** (el que sondea solo quiere saber si el `connect`
entra). *Inferido:* lo usa un script de arranque del laboratorio para esperar a que el DS levante.

## El framing C1/C2

Dos formatos de cabecera. El tamaño es **total**, incluye la cabecera.

```text
C1:  c1 | size:u8        | head:u8 | payload…          size = largo total (3..255)
C2:  c2 | sizeHi:u8 sizeLo:u8 | head:u8 | payload…     size = (sizeHi<<8)|sizeLo, big-endian
```

C1 llega hasta 255 bytes; C2 codifica hasta 65535. El `head` (byte de operación) está en la
posición 2 (C1) o 3 (C2) y elige el handler ([05](05-protocolo.md)).

## El parser (`0x403010`)

Cada conexión acumula los bytes que llegan en `recvBuffer` (`ctx + 0x24`, 0x2000 bytes) y lleva un
contador `accum` (`ctx + 0x2028`). El worker llama al parser con el lock global tomado después de
cada recepción. El parser corta todos los paquetes completos que haya y deja el resto.

### Máquina de estados

```text
                    ┌─────────────────────────────────────────┐
   recv ─▶ accum += n │  entrar al parser solo si accum > 2    │
                    └───────────────┬─────────────────────────┘
                                    ▼
                         i = 0 ; repetir:
                                    ▼
             ┌── buf[i] == C1 ? ──▶ size = buf[i+1] ; head = buf[i+2]
             ├── buf[i] == C2 ? ──▶ size = (buf[i+1]<<8)|buf[i+2] ; head = buf[i+3]
             └── otro          ──▶ "error-L1 Header error" ; accum = 0 ; FIN   (descarta todo)

             size == 0  ──▶ "error : size" ; FIN                               (NO borra accum)

             accum < size (incompleto):
                 i == 0  ──▶ esperar más bytes ; FIN
                 i >= 1 y accum <= 0x1FFF ──▶ memmove(buf, buf+i, accum) ; "error : copy" ; FIN
                 i >= 1 y accum >  0x1FFF ──▶ FIN sin compactar

             cabe ──▶ encolar [buf+i .. buf+i+size) con head y connIdx
                      i += size ; accum -= size ; si accum > 0 repetir, si no FIN
```

### Propiedades (Análisis)

Son razonamientos sobre el parser; cada uno dice cómo se confirma con un test.

- **P-1, el head y el size se leen antes de comprobar que llegaron.** El parser lee `buf[i+1..i+3]`
  apenas ve `C1`/`C2`, aun si `accum` es 1 o 2 (la guarda `accum > 2` solo se evalúa al entrar al
  parser, no en cada vuelta). El buffer de 0x2000 siempre existe, así que no hay lectura fuera de
  rango, pero el `head`/`size` puede salir de **bytes viejos** de un paquete anterior. Para que esto
  pase, un paquete completo tiene que dejar 1 o 2 bytes de cola; el parser intenta otra vuelta con
  esos bytes. *Confirmación:* mandar `C1 04 20 AA` (completo) pegado a `C1 03` (cabecera a medias) y
  ver que el servidor actúa sobre un `head`/`size` inventado. **MuRet no copia esto.**
- **P-2, termina siempre.** Cada vuelta o bien avanza `i` en `size ≥ 1` y baja `accum`, o bien
  corta (`FIN`). Como `accum` es finito, el bucle para. No hay forma de colgar el parser con datos.
- **P-3, es O(accum) por recepción** (una pasada), salvo la compactación `memmove`, que es O(accum)
  extra cuando queda una cola incompleta. No es un vector de DoS por CPU.
- **P-4, la cola incompleta se compacta al frente.** Si sobra un paquete a medias después de cortar
  al menos uno, se mueve al offset 0 con `memmove` y se espera. El siguiente recv apunta a
  `recvBuffer + accum`, así que los bytes nuevos se pegan atrás de los que sobraron. Correcto,
  salvo por P-5.
- **P-5, la compactación no corre si `accum > 0x1FFF`.** Con la cola casi llena y un paquete a
  medias grande, el parser corta sin compactar; el `accum` queda tal cual y el próximo recv se
  postea con `len = 0x2000 - accum` (muy chico o cero). Ver "dominio efectivo".

## Dominio efectivo del tamaño

**Análisis**, con una verificación pendiente marcada *inferido*.

- El buffer de recepción es 0x2000. `WSARecv` siempre se postea con `len = 0x2000 - accum`, así que
  `accum` **nunca** pasa de 0x2000. Para que un paquete se encole tiene que cumplirse `accum ≥ size`,
  o sea `size ≤ 0x2000`.
- Un paquete C2 con `size` entre 0x2001 y 0xFFFF **no puede completarse nunca**: `accum` llega como
  mucho a 0x2000, siempre `< size`, el parser lo deja incompleto, el recv se postea con `len = 0`, y
  una recepción de longitud cero produce una completion con `bytes = 0`, que el worker trata como
  cierre (`0x402eed` → `close_conn`). *Inferido:* un C2 sobredimensionado cierra la conexión. Se
  confirma mandando `c2 30 00 52 …` y viendo el cierre.
- **Paquetes de 0x801 a 0x2000 sí se encolan**, pero el consumidor los saca a un buffer de pila de
  **0x800** (`0x402da0`) sin comparar tamaños, así que lo desbordan. Es el bug más serio del
  original para MuRet; ver [08](08-quirks-y-bugs.md). Ningún head legítimo del GS 1.02 manda tanto,
  pero el servidor no se protege.

Resumen: tamaño útil **3..0x800**; 0x801..0x2000 desborda el consumidor; > 0x2000 (solo C2) cierra.

## La tabla de conexiones

- 100 entradas de `0x5C` bytes en `0x6441358`; el contador está justo después, en `0x6443748`
  (`(0x6443748 − 0x6441358) / 0x5C = 100`). Índices válidos 0..99.
- Layout de una entrada (Verificado en `0x4055e0`, `0x403140`, `0x4056b0`):

  | Offset | Campo | Notas |
  | --- | --- | --- |
  | `+0x00` | `active` | 0 libre, 1 conectado, 2 GameServer registrado |
  | `+0x04` | IP (cadena) | de `inet_ntoa`, hasta 0x50 bytes |
  | `+0x18` | puerto del GS (u16) | lo pone el login `0x00` |
  | `+0x1A` | clase del GS (u8) | login `0x00` |
  | `+0x1B` | nombre del GS | login `0x00`, 0x30 bytes |
  | `+0x4E` | serverId (u16) | login `0x00` |
  | `+0x50` | `serverId % 20` | login `0x00` |
  | `+0x54` | SOCKET | |
  | `+0x58` | puntero al contexto `DS_CONN_CTX` | |

- **Asignación de slot (`0x4055e0`):** recorre de 0 a 99 y toma el **primer** `active == 0`; si no
  hay, devuelve `0xFFFF`. Pone a cero la entrada, `active = 1`, guarda socket e IP, incrementa el
  contador. La búsqueda lineal es O(100), despreciable.
- **Registro de GS (`0x4056b0`):** lo hace el handler de login `0x00`. Falla si el slot no está en
  1, o si ya está en 2 (`System Name Overflow`). Pasa el slot a 2 y guarda puerto/clase/nombre/id.

## El contexto por conexión (`DS_CONN_CTX`, `0x4078` bytes)

`HeapAlloc` con `HEAP_ZERO_MEMORY` en `0x403270`/`0x4032f0`. Dos bloques per-IO idénticos de
`0x2030`: recepción desde `+0x08`, envío desde `+0x2038`. Campos clave:

| Offset | Campo | Para qué |
| --- | --- | --- |
| `+0x00` | SOCKET | |
| `+0x04` | índice de slot | para cerrar por índice |
| `+0x08` | OVERLAPPED de recepción | el parser recibe el puntero a este `+8` |
| `+0x1C` / `+0x20` | WSABUF de recv (len / buf) | `len = 0x2000 − accum`, `buf = recvBuffer + accum` |
| `+0x24` | `recvBuffer` (0x2000) | datos acumulados |
| `+0x2024` | tipo de la completion | ≠ 0 ⇒ fue un envío, el worker no parsea |
| `+0x2028` | `accum` | bytes acumulados |
| `+0x2038` | OVERLAPPED de envío | uno solo, se reusa (ver C-3 en [03](03-hilos-y-concurrencia.md)) |
| `+0x204C` / `+0x2050` | WSABUF de envío | **apunta al buffer del que llama**, no al del contexto |
| `+0x2054` | `sendBuffer` (0x2000) | **nunca se usa** |
| `+0x4068` | cola per-conexión | construida, nunca usada |
| `+0x406C` | `pendingOps` | operaciones WSA en vuelo |
| `+0x4070` / `+0x4074` | enlaces de la lista global de contextos | `0x403440` |

El contexto se inserta al frente de una lista global doblemente enlazada (`0x44052c` cabeza,
`0x440530` cola) al asociarse al IOCP. Esa lista solo se recorre entera en el cleanup del hilo de
accept (`0x4035d0`), cuyo cuerpo quedó vacío en el decompilado (*seam* menor; no afecta el
comportamiento observable).

## Recepción

1. Al aceptar, el hilo de accept crea el contexto, lo asocia al IOCP y postea el primer `WSARecv`
   (`buf = recvBuffer`, `len = 0x2000`), con el lock global tomado.
2. Cada completion de recepción (worker, `0x402e80`): `pendingOps--`; si `bytes == 0`, cierra; si
   fue un envío (`+0x2024 ≠ 0`), no parsea; si no, `accum += bytes`, parsea, re-postea `WSARecv` con
   `buf = recvBuffer + accum`, `len = 0x2000 − accum`.

Una conexión tiene **un solo `WSARecv` en vuelo** a la vez (se re-postea después de parsear), lo que
da el orden por conexión (I-1 en [03](03-hilos-y-concurrencia.md)).

## Envío (`0x403140`, envoltorio `ds_SendPacket` `0x405980`)

Toma el lock global y:

1. si `len > 0x2000`, loguea `Error : Max msg` y devuelve 0 (no envía);
2. si el slot no está activo (`< 1`), devuelve 0;
3. si el contexto es null, loguea `user socket context buffer free` y devuelve 0;
4. apunta el WSABUF de envío **al buffer que le pasaron** (típicamente la pila del handler) y hace
   `WSASend` con el OVERLAPPED de envío del contexto;
5. incrementa `pendingOps` **solo si `WSASend` terminó sincrónicamente**; si quedó pendiente, no lo
   incrementa (C-2), aunque igual habrá completion.

El envoltorio `0x405980` toma `(connIdx:u16, buf, len)` y es lo que llaman los 26 sitios de envío
de los handlers. Los paquetes se arman en la pila del handler y se mandan desde ahí (C-3). El
broadcast (`0x02`) y el sync de gremios (`0x40`) hacen varios envíos seguidos.

## Límites, de un vistazo

| Qué | Valor | Dónde |
| --- | --- | --- |
| conexiones simultáneas | 100 | tabla `0x6441358` |
| backlog listener principal | 5 | `listen` en `0x4029c0` |
| backlog port check | 8 | `listen` en `0x405870` |
| buffer de recepción por conexión | 0x2000 | `ctx + 0x24` |
| tamaño máximo de envío | 0x2000 | guarda en `0x403140` |
| tamaño útil de paquete | 3..0x800 | buffer del consumidor |
| workers IOCP | CPUs × 2, tope 64 | `0x402a90` |

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Framing C1/C2 con tamaño total | un `FrameParser` puro (bytes → paquetes), sin red, bien testeado primero | 1 |
| P-1: lee head/size de bytes viejos | el parser solo mira bytes efectivamente recibidos; **se arregla** y se documenta | 1 |
| Tamaño útil tope 0x800 por el buffer del consumidor; 0x801..0x2000 desborda | tamaño máximo explícito y validado; nada desborda; se rechaza con log | 1, 4 |
| Accept bloqueante + IOCP a mano | ola 2 sockets bloqueantes (un hilo por conexión, didáctico); ola 3 asíncrono como el original (IOCP en Windows, epoll en Linux) detrás de `Network::IoBackend` | 2, 3 |
| Tabla de 100 slots, índice reusado sin generación | tabla de sesiones con id + generación, para que una completion vieja no toque al slot nuevo (C-4) | 2, 3 |
| Un OVERLAPPED de envío, datos en la pila | cola de envío por conexión, buffers propios, un envío en vuelo | 3 |
| Port check que filtra el socket | sonda de salud explícita (o endpoint de estado), sin fugas | 8 |
| C2 sobredimensionado cierra | rechazo explícito con log, sin depender de un recv de longitud cero | 1, 3 |

Tests que fija:

- Vector de tablas del `FrameParser`: C1/C2 bien formados, cabecera partida entre dos recepciones,
  varios paquetes en una, `head`/`size` inválidos, `size` 0, `size` fuera de dominio, y el caso P-1
  (que MuRet **no** debe reproducir) (ola 1).
- Límite de 100 conexiones y reuso de slot sin cruce de datos (ola 2).
- Paridad de bytes de salida contra el original/oráculo para cada head (ola 5+).
