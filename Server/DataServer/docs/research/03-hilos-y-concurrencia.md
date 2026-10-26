# 03. Hilos y concurrencia

Este documento responde tres preguntas: qué hilos corren, qué lock protege qué, y qué puede salir
mal. Las dos primeras son **Verificado** (assembly). La tercera es casi toda **Análisis**:
consecuencias derivadas del código, cada una con cómo se confirmaría.

## Inventario de hilos

| Hilo | Función | Lo crea | Cuántos | Qué hace | Locks que toma |
| --- | --- | --- | --- | --- | --- |
| UI | bucle de mensajes MFC | el proceso | 1 | arranque ([02](02-arranque.md)), timers del log, accept del "port check" (mensaje `0x7EA`), cierre | ninguno de red (ver C-6) |
| accept | `0x402a90` | `0x402970` (`push 0x402a90` en `0x40297d`) | 1 | crea IOCP, workers, consumidor y listener; bucle bloqueante `WSAAccept` | global |
| worker IOCP | `0x402e80` | el hilo de accept (`0x402b36`) | CPUs × 2, tope 64 | espera completions, parsea, encola, re-postea `WSARecv` | global → cola |
| consumidor | `0x402da0` | el hilo de accept (`0x402b6c`) | 1 | saca un mensaje de la cola y lo despacha al handler (incluye SQL y envío) | global, cola (por separado) |
| watchdog StateMode | `0x4010f0` | `0x401140` en el arranque | 1 | cada 1 s, si no hubo paquetes en 30 s pone el estado en 0 (solo cosmético) | StateMode |
| sync de gremios | `0x403d10` | handler `0x40` (`0x40ca30`, `CreateThread` + `CloseHandle`) | **uno nuevo por cada `0x40` recibido**, sin tope | manda la lista de gremios en RAM al GS que la pidió | gremios → global |

Con 2 CPUs (la VM del laboratorio) hay 4 workers, así que en régimen el proceso tiene 8 hilos más
uno transitorio por cada sync de gremios.

## Locks

Todos son `CRITICAL_SECTION` de Win32: recursivos para el mismo hilo, sin timeout, sin detección
de deadlock.

| Lock | Dirección | Protege | Lo toman |
| --- | --- | --- | --- |
| **global** | `0x440510` | tabla de conexiones, lista de contextos, `pendingOps`, el parser, el despacho entero (handlers, SQL, caché de gremios) y cada envío | accept, workers, consumidor, `0x403140` (envío), hilo de gremios vía envío |
| cola | `0x4404f4` (`0x4404e0 + 0x14`) | la lista enlazada de la cola de mensajes | enqueue `0x405b00` / push `0x405a30`, pop `0x405ab0`, count `0x405a10` |
| gremios | `0x6411e58` | la lista de gremios en RAM | hilo de sync (`0x403d51`), handler `0x31` vía `0x4039b0` |
| StateMode | `0x440554` (`0x440540 + 0x14`) | estado activo y último tick | dispatcher vía `0x401070`, watchdog |

La cola usa `InitializeCriticalSectionAndSpinCount(…, 4000)` (`0x4059be`); las demás, la versión
sin spin.

**Lo que no tiene lock** (Verificado por ausencia de `EnterCriticalSection`):

- El log (`0x406270` → `0x4061e0`): `vsprintf` a un buffer de pila de 0x200 bytes y escritura en un
  ring buffer global de 30 líneas con un índice que se incrementa sin atomicidad. Lo llaman todos
  los hilos. El archivo de log sí queda serializado porque la CRT es la multihilo (`LIBCMT`: el
  binario contiene `unexpected multithread lock error`).
- El getter de StateMode (`0x4010a0`, `mov eax,[ecx+4]; ret`).
- El cierre desde la UI (`0x404c20`): recorre y libera la tabla de conexiones sin tomar el global.

## El recorrido de un mensaje

```text
   GS ──TCP──▶ kernel ──completion──▶ worker (con lock global)
                                        │  recvAccum += bytes
                                        │  parser 0x403010: corta paquetes C1/C2
                                        │  enqueue 0x405b00 ──▶ [ cola, lock propio, máx 5000 ]
                                        │  re-postea WSARecv
                                        ▼
                                      suelta el global

   consumidor, para siempre:
     1. global: count = tamaño de la cola           (0x402dc4–0x402e01)
     2. si count > 0: pop de UN mensaje (solo lock de la cola) a un buffer de pila de 0x800
     3. global: dispatcher 0x40b190 → handler → SQL → ds_SendPacket → WSASend
     4. global: si count > 30, log "RecvQ Count : %d"
     5. Sleep(1)
```

Tres propiedades salen de este esquema y son las que MuRet tiene que conservar:

- **I-1, orden por conexión.** Cada conexión tiene un solo `WSARecv` en vuelo y el worker parsea y
  encola con el lock global tomado, así que los paquetes de una conexión entran a la cola en el
  orden en que llegaron.
- **I-2, orden total.** Hay un único consumidor y saca de a uno, así que existe un orden global de
  despacho entre todas las conexiones: la cola es una linealización.
- **I-3, exclusión de handlers.** Un handler nunca corre en paralelo con otro, ni con el parser, ni
  con un accept. Los handlers no necesitan sincronización propia, salvo contra el hilo de gremios.

## La cola

Verificado en `0x4059a0` (constructor), `0x405b00`, `0x405a30`, `0x405ab0`, `0x405c00`:

- Lista simplemente enlazada con contador. El objeto global está en `0x4404e0` y lo construye un
  inicializador estático de la CRT (`0x4028f0` → `0x402900`), antes de `WinMain`.
- **Tope de 5000 mensajes**, fijo en el código del push (`cmp eax,0x1388` en `0x405a46`).
- Cada enqueue hace **dos** `HeapAlloc` con `HEAP_ZERO_MEMORY`: un nodo de `0x204c` bytes (usa
  solo `0x20`) y una copia del paquete. Un mensaje encolado cuesta unos 8 KB de heap.
- Acepta tamaños de 1 a `0x10000`; fuera de eso devuelve 0 sin encolar.
- **Si la cola está llena, el push falla y el enqueue no libera ni el nodo ni la copia**
  (`0x405ba2` salta directo al unlock). El parser ignora el retorno (`0x40307f`). Resultado: el
  mensaje se descarta **en silencio** y se pierden unos 8 KB por cada uno.
- El pop copia `len` bytes al buffer del consumidor **sin comparar contra su tamaño** (0x800). Ver
  [04](04-red.md#dominio-efectivo-del-tamaño) para qué tamaños desbordan.
- El consumidor recorta `count` a 4999 y loguea `error : Loop MAX` si lo supera, pero como saca uno
  por vuelta el recorte no cambia nada.

Hay **dos colas más que nadie usa**, construidas con el mismo constructor:

| Objeto | Dónde se crea | Uso |
| --- | --- | --- |
| `0x4404b0` | inicializador estático `0x402930` | ninguno: no hay push ni pop sobre ella |
| `ctx + 0x4068`, una por conexión | `0x403341`, al crear el contexto | ninguno: solo se vacía al liberar el contexto (`0x403542`) |

La reconstrucción `mfc-vc6/` llama "temporizador" al objeto de `ctx + 0x4068`; el assembly muestra
que es una cola (ver [09](09-discrepancias.md)). *Inferido:* son restos de una cola de envío del
`giocp.cpp` compartido con el GameServer.

## Throughput y latencia

**Análisis.** El consumidor despacha como mucho un mensaje por vuelta, y cada vuelta duerme
`Sleep(1)`. Si `t_s` es lo que dura de verdad ese `Sleep(1)` y `t_h` lo que tarda el handler
(dominado por SQL), la tasa máxima de servicio es

```text
μ = 1 / (t_s + t_h)    mensajes por segundo, para TODO el servidor
```

`Sleep(1)` no duerme 1 ms: duerme hasta el próximo tick del reloj del sistema. En Windows Server
2003 el tick por defecto es 15,625 ms y la resolución es global: si **otro** proceso de la máquina
llamó a `timeBeginPeriod(1)`, el tick baja a ~1 ms para todos. El DataServer no la llama (no
importa `WINMM`).

| Escenario | `t_s` | `μ` con `t_h` ≈ 0 | `μ` con `t_h` = 5 ms |
| --- | --- | --- | --- |
| reloj por defecto | ~15,6 ms | ~64 msg/s | ~48 msg/s |
| otro proceso subió la resolución | ~1–2 ms | ~500–1000 msg/s | ~150–200 msg/s |

*Inferido:* qué escenario corre en el laboratorio depende de qué más hay en la VM; se confirma
midiendo con `timeGetDevCaps`/`NtQueryTimerResolution` o cronometrando una ráfaga de paquetes.

Consecuencias:

- **Ráfagas.** Si llegan λ mensajes/s con λ > μ, la cola crece a razón de λ − μ. Con μ = 64 y una
  ráfaga de login de 300 jugadores (un `0x01` y un `0x06` cada uno, unos 600 mensajes), el último
  espera unos 9 s. La cola se llena recién en 5000, así que una ráfaga normal no pierde mensajes,
  pero sí agrega latencia que el GS percibe como lag.
- **SQL bloquea toda la red.** El consumidor tiene el lock global durante el handler entero. Mientras
  una consulta tarda, ningún worker puede procesar una completion, el accept no puede registrar
  conexiones y nadie más puede enviar. No se pierden datos (el kernel sigue recibiendo en el buffer
  ya posteado y en la ventana TCP), pero la latencia de **todos** los GS pasa a ser la del SQL más
  lento. Un `08S01` con reconexión ([06](06-sql-y-base.md)) congela el servidor entero mientras dure.

## Defectos de concurrencia

Numerados para poder citarlos desde [08](08-quirks-y-bugs.md) y desde los tests.

### C-1. Inversión del orden de locks entre gremios y global

- **Verificado:** el hilo de sync (`0x403d10`) toma el lock de gremios (`0x403d51`) y, con él
  tomado, manda cada gremio (`0x40ca60` → `0x405980` → `0x403140`, que toma el global). Orden:
  **gremios → global**.
- **Verificado:** el handler `0x31` corre con el global (lo tomó el consumidor) y llama a
  `0x4039b0`, que toma el de gremios. Orden: **global → gremios**.
- **Análisis:** dos órdenes opuestos sobre el mismo par de locks es la condición clásica de
  deadlock:

```text
  hilo de sync                         consumidor (handler 0x31)
  ─────────────                        ──────────────────────────
  toma GREMIOS                         toma GLOBAL
  manda gremio i → pide GLOBAL ...     llama 0x4039b0 → pide GREMIOS ...
         (espera al consumidor)               (espera al hilo de sync)
                    ══ ninguno avanza; workers y accept también se quedan sin el global ══
```

- **Hoy es inalcanzable.** Con la lista de gremios vacía el bucle del hilo de sync no ejecuta
  ninguna iteración, así que nunca toma el lock de gremios con un envío adentro. La lista está
  siempre vacía porque las `CQuery` de gremios nunca conectan
  ([Q-01](08-quirks-y-bugs.md#q-01-los-gremios-nunca-funcionan)). Si alguien "arregla" Q-01 en el
  original, el deadlock pasa a ser posible.
- **Cómo se confirmaría:** con Q-01 parcheado en la VM, mandar `0x40` y `0x31` a la vez en bucle.

### C-2. `pendingOps` no cuenta los envíos que quedan pendientes

- **Verificado:** `0x403140` incrementa `pendingOps` (`ctx + 0x406c`) solo si `WSASend` termina en
  el acto (`0x403251`). Si devuelve `WSA_IO_PENDING` (`0x3e5`) salta el incremento (`0x40322d` →
  `0x403257`). Pero las dos formas generan una completion, y el worker **siempre** decrementa
  (`0x402f08`). El `WSARecv` del worker sí incrementa en los dos casos (`0x402fb0` → `0x402fcb`).
- **Verificado:** la liberación del contexto (`0x4034a0`) exige `pendingOps == 0` con comparación
  **sin signo** (`jbe` en `0x4034bb`).
- **Análisis:** cada envío que queda pendiente deja el contador una unidad por debajo de la
  realidad. Dos efectos posibles:
  - Contador "negativo" (`0xFFFFFFFF`) al cerrar: el contexto **nunca se libera** (unos 16 KB más su
    cola). Es una fuga.
  - Contador en 0 con un `WSARecv` todavía en vuelo: si en ese momento el servidor cierra la
    conexión (por ejemplo, porque falló un envío), el contexto se libera con el recv pendiente.
    Cuando ese recv termina con error, el worker lee `ctx->connIdx` de memoria liberada
    (`0x403600`). Es un **use-after-free**.
- **Cuándo pasa:** un `WSASend` queda pendiente solo si el buffer de envío del socket está lleno,
  o sea, si el GS dejó de leer. Con mensajes chicos y un GS sano casi nunca pasa.
- **Cómo se confirmaría:** un cliente de prueba que conecta, pide respuestas grandes (`0x08`,
  `0x06`) y no lee; luego cierra con RST. Mirar el heap del proceso (fuga) o correr la
  reconstrucción con Application Verifier (UAF).

### C-3. Un solo OVERLAPPED de envío, reusado, apuntando a la pila

- **Verificado:** cada contexto tiene un único bloque de envío (`ctx + 0x2038`) y `0x403140` lo
  reescribe sin mirar si hay un envío en curso. El `WSABUF` apunta directo al buffer del que
  llama (`[eax+0x18] = buf` en `0x4031e2`), y los handlers pasan buffers **de pila** (por ejemplo
  `lea edx,[esp+0x18]` antes del envío de `0x06` en `0x40c3cd`). El buffer de envío de 0x2000 bytes
  del propio contexto no se usa nunca.
- **Análisis:** la documentación de Winsock exige que el `OVERLAPPED` y los datos sigan válidos
  hasta la completion. Si un envío queda pendiente:
  - el handler retorna y la pila se reusa: el kernel puede mandar bytes de otro mensaje;
  - un segundo envío a la misma conexión pisa el `OVERLAPPED` en uso, que es comportamiento
    indefinido.
- **Cuándo pasa:** la misma condición que C-2 (buffer de envío del socket lleno). El handler `0x40`
  manda varios paquetes seguidos (cabecera, uno por gremio y cierre), así que es el más expuesto.

### C-4. Un cierre con error filtra el contexto y una completion vieja puede cerrar al slot reusado

- **Verificado:** si `GetQueuedCompletionStatus` falla con un contexto (RST, red caída), el worker
  cierra **sin decrementar** `pendingOps` (`0x402ead` → `0x402ed4`). El cierre (`0x403610`) busca el
  contexto por **índice de slot** (`g_users[idx].ctx`), no usa el contexto que completó, y libera
  el slot aunque el contexto no se haya podido liberar.
- **Análisis:**
  - Todo cierre por error deja `pendingOps` en 1, así que **cada desconexión abrupta filtra un
    contexto** (unos 16 KB). Con 100 slots y pocas desconexiones no se nota; con un GS que se cae
    en bucle, sí.
  - El slot queda libre y lo puede tomar una conexión nueva. Si después llega otra completion del
    contexto viejo (por ejemplo, un envío pendiente que falla), el worker cierra por índice y
    **cierra la conexión nueva**. La ventana es chica: entre el cierre y el fallo de las
    operaciones pendientes.
- **Cómo se confirmaría:** cortar la conexión de un GS con RST en bucle y mirar el heap; para la
  segunda parte, un envío pendiente más una reconexión inmediata.

### C-5. El log no tiene lock

- **Verificado:** ver "Lo que no tiene lock".
- **Análisis:** dos hilos que loguean a la vez pueden escribir en la misma línea del ring buffer o
  saltear una. No hay escritura fuera de rango: el índice se recorta en un registro antes de usarse
  (`0x4061f8`–`0x406209`). El efecto es solo cosmético: líneas mezcladas en pantalla.
- `vsprintf` a 0x200 bytes de pila no tiene tope; ningún formato del binario llega a ese largo con
  los datos que recibe, pero no hay garantía.

### C-6. El cierre desde la UI no toma el lock global

- **Verificado:** `0x404c20` llama a `0x405670` por cada slot sin tomar `0x440510`.
- **Análisis:** corre en paralelo con los workers, pero el proceso termina enseguida, así que no
  tiene efecto observable. Se anota porque la reconstrucción sí toma el lock (ver
  [09](09-discrepancias.md)).

### C-7. Un hilo de sync de gremios por cada `0x40`, sin tope

- **Verificado:** `0x40ca30` crea un hilo por pedido y le pasa el índice de conexión.
- **Análisis:** dos `0x40` seguidos al mismo GS generan dos hilos que intercalan sus paquetes en la
  misma conexión (cabecera, gremios y cierre mezclados). Si la conexión se cae y el slot se reusa
  durante el sync, el resto de la lista le llega al GS nuevo. Con Q-01 la lista está vacía y solo
  se intercalan las cabeceras y los cierres.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Un lock global para red, cola y lógica | la red solo corta paquetes y encola; un **hilo de lógica** único es dueño del estado y no necesita locks | 4 |
| Consumidor con `Sleep(1)` y un mensaje por vuelta | pop bloqueante con `std::condition_variable`; drenar todo lo disponible por vuelta | 4 |
| Cola de 5000 que descarta en silencio y filtra memoria | cola acotada con política explícita: dejar de leer de esa conexión (backpressure) o cerrarla, siempre con log | 4 |
| I-1, I-2, I-3 implícitos | se conservan a propósito: un solo hilo de lógica da orden total y exclusión; se documentan como invariantes y se testean | 4 |
| SQL dentro del lock global | el SQL sigue en el hilo de lógica (conserva I-2), pero la red ya no espera a la lógica | 4, 6 |
| `pendingOps` a mano y con fallas | la vida del contexto la maneja un contador que sube por **cada** operación posteada (o `shared_ptr`) y se libera solo con todo completado | 3 |
| Un `OVERLAPPED` de envío reusado, datos en la pila | cola de envío por conexión, un solo envío en vuelo, buffers propiedad de la conexión | 3 |
| Cierre por índice de slot | identificador de conexión con número de generación, para que una completion vieja no toque al slot reusado | 2, 3 |
| Hilo nuevo por cada `0x40`, lock de gremios con envío adentro | el sync de gremios corre en el hilo de lógica; nunca se manda con un lock tomado | 7 |
| Log sin lock | logger seguro entre hilos | 2 |

Tests que fija:

- Orden por conexión y orden total con varios clientes falsos (ola 4).
- Cola llena: no hay fuga y la política elegida se cumple (ola 4).
- Cerrar con operaciones pendientes, con cierre limpio y con RST: sin fugas (LSan) y sin UAF (ASan)
  (ola 3).
- Un slot reusado no recibe cierres ni datos de la conexión anterior (ola 3).
- Un job de CI con ThreadSanitizer sobre los tests de red y cola (ola 4).
