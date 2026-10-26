# Research del DataServer original

> **Borrador, 2026-10-26.** Escrito a partir de
> [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i)
> @ `ff076087` (carpeta `dataserver/`): binario `bin/Dataserver.exe`, desensamblado
> `disasm/Dataserver.asm`, decompilación `ghidra/out/all/dataserver_all.c`, reconstrucción
> `src/reconstructed/mfc-vc6/`, contrato `src/reconstructed/shared/docs/CONTRACT.md` y evidencia
> de la VM en `vm_evidence/`. Las direcciones son VAs del binario (base `0x400000`).
>
> Este research describe **qué hace el original**, no cómo lo hace MuRet. Las decisiones de MuRet
> están en el [roadmap](../../../../docs/superpowers/specs/2026-10-25-dataserver-port-design.md).
> Cada tema termina con lo que implica para MuRet y qué test lo fija.

## Para qué sirve

El DataServer es el único proceso del servidor de MU Online 1.02 que toca la base de datos. Los
GameServers se conectan a él por TCP y le piden cuentas, personajes, baúl, gremios y seriales de
items; él lo traduce a SQL contra SQL Server y responde. MuRet lo reescribe con el mismo
comportamiento observable, así que antes de escribir código hace falta saber con precisión qué
hace el original, incluidos sus errores.

## Índice

| # | Documento | Qué responde | Olas que lo usan |
| --- | --- | --- | --- |
| 01 | [Binario y método](01-binario-y-metodo.md) | qué binario es, cómo se analizó, cómo verificar cualquier dato | todas |
| 02 | [Arranque y configuración](02-arranque.md) | qué pasa desde que se lanza el `.exe` hasta que escucha | 0, 8 |
| 03 | [Hilos y concurrencia](03-hilos-y-concurrencia.md) | qué hilos hay, qué lock protege qué, qué puede salir mal | 3, 4 |
| 04 | [Red: framing, conexiones y envío](04-red.md) | gramática C1/C2, parser, tabla de conexiones, IOCP, envío | 1, 2, 3 |
| 05 | [Protocolo: los 22 heads](05-protocolo.md) | cada request y respuesta byte a byte, con su flujo | 1, 5, 7 |
| 06 | [SQL y base de datos](06-sql-y-base.md) | cada sentencia verbatim, el esquema, la capa `CQuery` | 6, 7 |
| 07 | [Datos auxiliares](07-datos-auxiliares.md) | stats por clase, blobs, tabla de items, filtros de nombres | 5, 7 |
| 08 | [Quirks y bugs](08-quirks-y-bugs.md) | catálogo numerado de defectos del original y qué hace MuRet | todas |
| 09 | [Discrepancias entre documentos](09-discrepancias.md) | dónde se contradicen los docs del research y cuál es el dato bueno | — |
| 10 | [Lo que falta saber](10-pendientes.md) | huecos de conocimiento y cómo cerrarlos | 7, 8 |

Orden de lectura sugerido para aprender: 01 → 04 → 03 → 05 → 06, y el resto como referencia.

## Niveles de confianza

Cada afirmación lleva, explícito o por la sección en la que está, uno de estos niveles:

| Nivel | Significa | Cómo se marca |
| --- | --- | --- |
| **Verificado** | confirmado en el assembly y, cuando aplica, en la VM (base viva, proceso vivo, captura) | sin marca, con dirección citada |
| **Inferido** | deducción razonable que no se confirmó directamente | *inferido* |
| **Análisis** | consecuencia derivada en este documento a partir de datos verificados (por ejemplo, una carrera posible) | **Análisis** |
| **Seam** | comportamiento conocido en estructura pero sin los bytes exactos; necesita una captura del wire | *seam* |

El nivel **Análisis** es nuevo de este documento: son razonamientos sobre el código del original
(cotas, carreras, terminación) que el research no escribió. Hay que tratarlos como hipótesis
fuertes, y cada uno dice cómo se confirmaría.

## Receta para verificar cualquier dato

1. Tomar la dirección citada (por ejemplo `0x00403010`).
2. Buscarla en `disasm/Dataserver.asm` (formato `  403010:`) y, si hace falta, el bloque
   `/* ==== 0x00403010 ... ==== */` en `ghidra/out/all/dataserver_all.c`.
3. Contrastar registros, tamaños, signo de las comparaciones (`jg` vs `ja`) y convención de llamada
   (`thiscall` usa `ecx` como `this`). El pseudocódigo de Ghidra nunca es la última palabra.
4. Para un dato de runtime (tablas que se llenan al arrancar, esquema de la base), buscar la
   evidencia en `vm_evidence/`.

MuRet hereda esta receta: cada test de contrato cita la sección de este research que fija.

## Glosario mínimo

| Término | Qué es |
| --- | --- |
| GS | GameServer: el cliente de este servidor. Hay uno o varios conectados a la vez. |
| DS | DataServer: este servidor. Escucha en el puerto 55960. |
| head | byte de operación de un paquete (`0x00`…`0xCF`); elige el handler. |
| C1 / C2 | dos formatos de cabecera de paquete de MU, con tamaño de 1 o 2 bytes. |
| IOCP | I/O Completion Port de Windows: el kernel avisa en una cola cuando terminó una operación de red. |
| OVERLAPPED | estructura que identifica una operación asíncrona en curso; el kernel la usa hasta que termina. |
| GQCS | `GetQueuedCompletionStatus`: la llamada con la que un hilo espera la próxima operación terminada. |
| CS | `CRITICAL_SECTION`: mutex recursivo de Win32. |
| `CQuery` | clase propia del original que envuelve ODBC (una conexión, un statement). |
| `MX_*` | procedimientos almacenados del CustomDB del repack que el DS invoca. |
| blob | columna binaria (`Inventory`, `MagicList`, `Quest`, `Items`, `G_Mark`, `SkillKey`). |
