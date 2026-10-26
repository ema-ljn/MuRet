# DataServer en MuRet: diseño del port

- **Fecha:** 2026-10-25 (decisiones cerradas el 2026-10-26)
- **Estado:** aprobado, en ejecución por olas
- **Autor:** Emanuel (ema-ljn), con Claude como tutor
- **Fuente de verdad del comportamiento:** [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i), carpeta `dataserver/`
- **Documento vivo original:** [Claude Doc "DataServer en MuRet — spec y plan"](https://claude.ai/artifact/98nqEqURDgipsG86LcGepx)

Vamos a reescribir el DataServer en MuRet como un servidor C++20 moderno, multiplataforma (Windows y Linux), que habla el mismo protocolo y ejecuta el mismo SQL que el original, organizado y documentado como MuMain. Lo escribís vos, etapa por etapa; yo hago de tutor.

## Objetivo y cómo trabajamos

**Qué queremos al final:** un `DataServer` que el GameServer 1.02 del laboratorio no pueda distinguir del original. Criterio de aceptación: mismo comportamiento en el wire (puerto 55960, framing C1/C2, los 22 heads) y en la base `MuOnline` (mismas queries y procedimientos `MX_*`/`SP_*`).

**Qué NO es el objetivo:** copiar la reconstrucción `mfc-vc6` línea por línea. Esa reconstrucción es la *referencia de comportamiento* (el contrato); MuRet es una *implementación nueva* con C++ moderno, RAII, capas separadas y tests.

**Cómo es cada subola (ritmo lento a propósito):**

1. Te explico el concepto nuevo (por ejemplo, qué es un completion port o por qué un `std::span`), con un ejemplo chico fuera del proyecto.
2. Te paso una mini-spec de la etapa: qué archivos, qué interfaz pública, qué test tiene que pasar.
3. Lo escribís vos y abrís un PR (o me pegás el código).
4. Lo reviso como reviewer: bugs, estilo según `CODING_RULES.md`, y te hago preguntas en vez de darte la respuesta.
5. Cuando está verde y lo entendés, cerramos la etapa con una entrada corta en `docs/` y pasamos a la siguiente.

Yo puedo escribir sin problema el andamiaje aburrido (CMake, CI, plantillas de docs) si me lo pedís. La lógica del servidor la escribís vos.

## Qué es el DataServer (lo que ya sabés por la reconstrucción)

El DataServer es el único proceso que toca la base: los GameServers le piden cuentas, personajes, baúl, guilds y seriales de items por TCP, y él traduce cada pedido a SQL. Todo esto sale de `dataserver/src/reconstructed/shared/docs/CONTRACT.md` en tu repo de research.

**Las piezas del original, en el orden en que viaja un paquete:**

1. **Accept + IOCP** (`ds_net.cpp`): un hilo acepta conexiones (máx. 100 usuarios, tabla de stride 0x5C) y `2 × CPUs` workers esperan completions de `WSARecv`/`WSASend`.
2. **Parser de framing** (`ds_parser.cpp`): corta el stream TCP en paquetes C1 (`C1 size head ...`) y C2 (`C2 sizeHi sizeLo head ...`), con buffer de 0x2000 y compactación del resto incompleto.
3. **Cola** (`ds_queue.cpp`): los workers encolan; capacidad 5000 nodos, con su propia sección crítica.
4. **Consumidor único + dispatcher** (`ds_dispatch.cpp`): un solo hilo saca de la cola y llama al handler según el head, bajo un lock global. Esto hace que toda la lógica sea secuencial.
5. **Handlers** (`ds_handlers.cpp`): uno por head, cada uno arma SQL contra `MuOnline` vía ODBC (`CQuery.cpp`) y responde con `ds_SendPacket`.

**Los 22 heads agrupados por dominio** (así los vamos a portar):

| Dominio | Heads | Qué hacen |
| --- | --- | --- |
| Sesión del GS | 0x00, 0x40, 0x02 | login del GameServer, sync de gremios (llamado heartbeat en docs viejos), broadcast a todos |
| Cuenta y personajes | 0x01, 0x04, 0x05, 0x06, 0x07, 0x11, 0xCF | lista, crear (`MX_CreateCharacter`), borrar, cargar (`MX_SelectCharacter`), guardar stats e inventario, transferir (\`SP\_CHARACTER\_TRANSFER\`) |
| Baúl | 0x08, 0x09, 0x12 | abrir, cerrar y guardar dinero (`MX_*warehouse`, `MX_*money`) |
| Guilds | 0x30, 0x31, 0x32, 0x33, 0x37, 0x38 | crear, borrar, miembros, score, aviso |
| Varios | 0x20, 0x52, 0x60 | contador de items del GS, serial anti-dupe (`WZ_GetItemSerial`), opciones |

**Quirks del original:** el contrato documenta bugs reales (paquetes > 0x800 desbordan el buffer del consumidor, fuga cuando la cola está llena, re-encolado con `size == 0`). En MuRet los vamos a **corregir**, pero cada uno queda anotado en `docs/` con un test que muestra el comportamiento nuevo, así aprendés por qué eran bugs.

La especificación completa del comportamiento, con offsets, SQL, respuestas, quirks y evidencia, está en la [Parte 2](#parte-2-comportamiento-del-dataserver-contrato) de este documento.

## Decisiones de diseño

La regla guía: el **contrato** (wire + SQL) se respeta al byte; la **implementación** es moderna y portable. Cada decisión de esta tabla va a tener su ADR en `docs/adr/`, como hace MuMain.

| Tema | Original (VC6) | MuRet (propuesta) | Por qué / qué aprendés |
| --- | --- | --- | --- |
| Lenguaje | C++98 + MFC | C++20, sin MFC | RAII, `std::span`, `std::byte`, `enum class`, smart pointers |
| Build | `.dsp` de VC6 | CMake 3.25+ con presets (`windows-x64`, `linux-x64`) y vcpkg, igual que MuMain | un solo build para Windows y Linux |
| Red | IOCP crudo de Win32 | IOCP (Windows) y epoll (Linux) escritos a mano detrás de una interfaz `Network::IoBackend`, siguiendo el modelo del original; Asio solo como comparación opcional | `OVERLAPPED`, completion ports, proactor vs reactor, pool de workers: lo que hace una librería de red por dentro |
| Concurrencia | N workers + cola + 1 consumidor + lock global | N hilos de I/O + una cola acotada + 1 hilo de lógica (mismo modelo, sin lock global) | productor/consumidor, `std::mutex`, `std::condition_variable`, back-pressure |
| Base de datos | ODBC directo (`CQuery`) | interfaz `IAccountRepository` etc. con dos implementaciones: en memoria (tests) y ODBC crudo con RAII contra el SQL Server de la VM2003 | inyección de dependencias, testear sin base, prepared statements |
| Protocolo | structs empaquetados leídos con punteros | structs `#pragma pack(1)` + funciones `Read`/`Write` sobre `std::span`, con validación de tamaño | endianness, layout de memoria, por qué no confiar en el tamaño que manda el cliente |
| UI | ventana MFC con log | consola + log a archivo (spdlog o propio) | separar presentación de lógica |
| Tests | smoke test en host | GoogleTest + CTest desde la ola 0 | TDD, tests de contrato con bytes capturados del original |
| Config | `.ini` + rutas fijas | archivo de config (TOML o JSON) + argumentos de línea de comandos | nada hardcodeado (regla 3 de MuMain) |

**Compatibilidad SQL:** los textos SQL y llamadas `MX_*` se copian *verbatim* de `ds_sql.h` a un solo archivo de constantes, así hay un único lugar para comparar con el original.

```
            GameServer(s) 1.02
                   │ TCP 55960, C1/C2
┌──────────────────▼──────────────────────────────────────┐
│ Network   IoBackend (IOCP | epoll) · Session · FrameParser│  N hilos de I/O
└──────────────────┬──────────────────────────────────────┘
                   │ paquete completo + índice de sesión
┌──────────────────▼──────────────────────────────────────┐
│ Dispatch  MessageQueue (acotada) → Dispatcher (head→fn)  │  1 hilo de lógica
└──────────────────┬──────────────────────────────────────┘
┌──────────────────▼──────────────────────────────────────┐
│ Handlers  Account · Character · Warehouse · Guild · Misc │──► respuesta por Session
└──────────────────┬──────────────────────────────────────┘
                   │ interfaces de repositorio
┌──────────────────▼──────────────────────────────────────┐
│ Database  Memory (tests) | Odbc (SQL Server, VM2003)     │
└─────────────────────────────────────────────────────────┘
```

El paquete entra por Network, pasa por la cola al único hilo de lógica, el handler consulta la base a través de una interfaz, y la respuesta vuelve por la Session de esa conexión.

## Estructura del repo MuRet

Copiamos la forma de MuMain: un `AGENTS.md` como entrada, reglas en `docs/CODING_RULES.md`, ADRs numerados, specs de superpowers con fecha, y el código dividido por capas con un namespace `<Capa>::<Tema>`.

El repo crece de a componentes: el DataServer vive en `Server/DataServer/` y después se suman otros servidores al lado (por ejemplo `Server/ConnectServer/` o `Server/JoinServer/`). Lo que comparten varios va a `Server/Common/` recién cuando aparezca el segundo que lo necesite.

```
MuRet/
  README.md                  qué es el repo, componentes y estado de cada uno
  AGENTS.md / CLAUDE.md      entrada para humanos y agentes (apunta a CODING_RULES)
  CMakeLists.txt             raíz: opciones comunes + add_subdirectory(Server/DataServer)
  CMakePresets.json          windows-x64, linux-x64, debug/release
  vcpkg.json                 dependencias de todo el repo (gtest; el resto cuando haga falta)
  cmake/                     módulos CMake compartidos (warnings, sanitizers)
  docs/                      lo que vale para todo el repo
    CODING_RULES.md          adaptado de MuMain
    adr/                     decisiones globales (0001 contrato wire y SQL, 0002 IOCP/epoll a mano...)
    superpowers/specs/       specs con fecha, como este documento
  Server/
    Common/                  (más adelante) código compartido entre servidores
    DataServer/
      CMakeLists.txt         target DataServer + DataServerTests
      README.md              cómo compilar, correr y configurar este servidor
      docs/
        protocol/            un .md por dominio: framing, cuenta, personajes, baúl, gremios
        learning/            tus notas por etapa
      src/
        main.cpp             solo arma las piezas y arranca
        Core/                Config, Log, errores           namespace DataServer::Core
        Network/             Server, Session, FrameParser   namespace DataServer::Network
        Protocol/            Heads, structs                 namespace DataServer::Protocol
        Dispatch/            MessageQueue, Dispatcher       namespace DataServer::Dispatch
        Handlers/            Account, Character, Warehouse, Guild, Misc
        Database/            interfaces + Memory/ + Odbc/   namespace DataServer::Database
      tests/
        protocol/  dispatch/  handlers/  database/  integration/
  tools/
    packet-replay/           reproduce capturas del GameServer real contra MuRet
```

La regla de capas (regla 5 de MuMain) aplicada acá: **Network** no sabe nada de SQL, **Handlers** no tocan sockets, **Database** no conoce paquetes. Solo `Server/DataServer/src/main.cpp` conoce a todos. Los namespaces llevan el componente adelante (`DataServer::Network`) para que no choquen cuando entren otros servidores.

## Plan por olas

El plan está dividido en 9 olas por tema, y cada ola en subolas. **Cada subola es un concepto nuevo y un PR chico** que compila, tiene tests de GoogleTest en verde y se puede mostrar. Vamos de lo más rudimentario a lo más escalable: primero bytes, después sockets a mano, después IOCP/epoll como el original, y recién al final el sistema distribuido. Cada ola tiene su plan detallado en `docs/superpowers/plans/` antes de arrancarla.

| Ola | Tema | Subolas (un PR cada una) | Conceptos que aprendés | Terminada cuando |
| --- | --- | --- | --- | --- |
| 0 | Fundaciones | 0.1 CMake raíz + `Server/DataServer` + presets Windows/Linux + esta spec y el plan de la ola 0 · 0.2 vcpkg + GoogleTest + CTest + CI en GitHub Actions (Windows y Linux) · 0.3 docs base: `AGENTS.md`, `CODING_RULES.md`, ADR 0001 y 0002 | unidades de compilación, headers, linker, targets de CMake, vcpkg | `cmake --build` y `ctest` pasan en los dos sistemas |
| 1 | Protocolo sin red | 1.1 helpers de bytes: `std::byte`, `std::span`, big/little-endian · 1.2 `FrameParser` C1/C2 · 1.3 structs de paquetes con `static_assert(sizeof)` y `Read`/`Write` (0x00, marcadores de 0x40) | tipos, clases, RAII básico, layout en memoria, TDD | tests con los casos del contrato: partido, varios por recv, header inválido, `size == 0` |
| 2 | Sockets a mano | 2.1 capa mínima de portabilidad Winsock/BSD sockets · 2.2 servidor TCP bloqueante de un cliente · 2.3 un hilo por conexión y por qué no escala · 2.4 tabla de 100 sesiones | sockets, TCP como stream, `#ifdef _WIN32`, `std::thread`, ciclo de vida | un cliente de prueba manda paquetes y el log los muestra |
| 3 | I/O asíncrono como el original | 3.1 IOCP en Windows: `CreateIoCompletionPort`, `WSARecv`/`WSASend` overlapped, `GetQueuedCompletionStatus`, contexto por conexión · 3.2 epoll en Linux detrás de la misma interfaz `Network::IoBackend` · 3.3 envío con cola propia por sesión y cierre ordenado (contador de pendientes, despertar workers al apagar) · 3.4 *opcional:* el mismo server con Asio para comparar | modelo proactor (IOCP) vs reactor (epoll), OVERLAPPED, pool de workers, los bugs 19 y del WSABUF del original | 100 clientes simultáneos sin crash ni leaks (ASan) en Windows y Linux |
| 4 | Concurrencia | 4.1 `MessageQueue` acotada con `std::mutex` + `std::condition_variable` · 4.2 hilo de lógica + `Dispatcher` head → handler · 4.3 estrés, back-pressure y TSan | data races, productor/consumidor, por qué el original serializa todo | test de estrés con TSan limpio |
| 5 | Handlers en memoria | 5.1 interfaces de repositorio + implementación en memoria · 5.2 heads 0x00, 0x02, 0x40 · 5.3 cuenta y personajes contra memoria | interfaces (virtual puro), inyección de dependencias, `unique_ptr` | integración: login + broadcast entre 2 clientes |
| 6 | Base de datos real | 6.1 ODBC crudo envuelto en RAII (env, dbc, stmt, `SQLGetDiagRec`) · 6.2 prepared statements y blobs · 6.3 conexión al SQL Server de la VM2003 · 6.4 repositorios ODBC | handles de C con RAII, errores, SQL injection | los tests de repositorio pasan contra la base `MuOnline` de la VM |
| 7 | Resto de heads | 7.1 cuenta y personajes · 7.2 baúl y varios · 7.3 gremios | blobs, transacciones, atomicidad del serial | todos los heads del contrato implementados |
| 8 | Sistema distribuido | 8.1 el GameServer real de la VM contra MuRet · 8.2 `packet-replay` y paridad byte a byte · 8.3 timeouts, reconexión, idempotencia, fallas parciales · 8.4 apagado limpio, métricas, health checks | sistemas distribuidos en serio, observabilidad | un GS 1.02 entra, crea personaje y juega contra MuRet |

**Base de datos durante el desarrollo:** el SQL Server vive en la VM2003, así que hasta la ola 5 todo se prueba contra el repositorio en memoria, sin base. Desde la ola 6, los tests de integración se conectan por ODBC al SQL Server de la VM (puerto 1433) y se marcan aparte para que el CI no los corra.

## Decisiones tomadas (2026-10-26)

Estas eran las preguntas abiertas; quedan así:

- **Red:** IOCP en Windows y epoll en Linux escritos a mano, siguiendo el modelo del original, porque enseñan el nivel rudimentario. Asio queda como subola opcional 3.4 para comparar.
- **Tests:** GoogleTest con CTest.
- **Forma de trabajo:** por olas y subolas; cada subola es un concepto y un PR chico que vos escribís y yo reviso.
- **Superpowers:** la spec va al repo como `docs/superpowers/specs/2026-10-25-dataserver-port-design.md` en la ola 0, y cada ola tiene su plan en `docs/superpowers/plans/`.
- **Base de datos:** el SQL Server de la VM2003, como se describe arriba.

---

# Parte 2: Comportamiento del DataServer (contrato)

Esta parte es la especificación de comportamiento que MuRet tiene que cumplir: qué hace el `Dataserver.exe` original (VC6, oct. 2003, SHA-256 `58daf2c8…47bff`) según el research de `EmaNLJN/mu-server-repack-research-102i`, con la evidencia y el nivel de confianza de cada afirmación. Offsets en hexa, relativos al byte 0 del paquete (`0xC1`/`0xC2`), enteros little-endian salvo el size de C2.

## Evidencia y método

Toda afirmación de esta parte sale de cinco fuentes, y cada una se valida contra el assembly; el pseudocódigo de Ghidra nunca es la última palabra.

| Fuente | Qué aporta | Dónde está |
| --- | --- | --- |
| Inspección del binario | PE32, base 0x400000, EP 0xFC3E, MFC 4.2 estática, 1572 funciones; **ni empacado ni virtualizado** (`.text` del proceso vivo pid 624 = disco, 0 bytes distintos de 196 KB) | `docs/03_dataserver_updater.md` |
| Decompilación total | 1571 de 1572 funciones (99,94%) con Ghidra 12.1.4 headless y límites del decompilador subidos (`FixDecomp.java`); la única falla, `FUN_0040b7dd`, se reconstruye a mano desde asm | `ghidra/out/all/dataserver_all.c` (52.132 líneas) |
| Assembly | fuente de verdad para offsets, tamaños, signedness y convenciones de llamada | `disasm/Dataserver.asm` + volcados `.rdata`/`.data` |
| Validación dinámica (VM2003) | procesos vivos (DS en 55960, GS en 55970, CustomDB, SQL Server 10.50), schema `MuOnline`, ejecución real de la query de arranque (devuelve `226 0`), DSN ODBC del registro, dump de `.data` vivo | `docs/DYNAMIC_VALIDATION.md`, `vm_evidence/` |
| Revisión adversarial | 8+ rondas independientes (Opus 5.5 + astra) código contra asm, con correcciones aplicadas (por ejemplo: capacidad 100 y no 256, head 0x02 es broadcast y no ACK) | `src/reconstructed/docs/reviews/` |

**Niveles de confianza que uso abajo:**

- **Verificado**: confirmado en asm y, cuando aplica, en la base viva o en una corrida real.
- **Inferido**: deducción razonable sin confirmación directa (se marca con *inferido*).
- **Seam**: comportamiento conocido en estructura pero sin bytes exactos todavía; necesita una captura de wire real para cerrarse.

**Receta para verificar cualquier dato:** tomar la dirección citada (`0xADDR`), buscarla en `Dataserver.asm`, contrastar registros, tamaños y signedness, y anotar el veredicto. MuRet hereda esta receta para sus tests de contrato.

## Arranque

El servidor arranca en este orden (`OnStart`, `0x00404ad0`) y aborta en la primera conexión ODBC que falle. Se lanza como `dataserver.exe 55960`; la instancia 2 del lab usa 55962.

1. **Auto-update** (`0x0040d900`): copia `.\Temp\{WZ_AutoUpdater.exe, WZ_UM.DLL}` a `.\`, lee `[UPDATE_DATA] SkipUpdate` de `.\wz_update.ini`; si no se salta, llama `AU_CHECKUPDATE` de `WZ_UM.DLL` y, si hay update, lanza el updater y sale del proceso. *En MuRet no se porta.*
2. **Cola y cache de gremios**: inicializa la sección crítica de la cola (`0x004055c0`) y el gestor de gremios en memoria.
3. **Tablas desde archivos** (contenedor `0x004048b4`): tabla de items de `..\data\lang\kor\item(kor).txt` (512 registros de 0x58 bytes, siempre `kor`) y listas de nombres prohibidos (`badsyntax.txt` + nombres de items, monstruos y skills). No es fatal si faltan.
4. **8 conexiones ODBC**, todas `Connect(3, "MuOnline", "Admin", "BobStudio")` con handles separados: `EventDBSet` (en un constructor estático, antes de `OnStart`), y en `OnStart` serial, cuenta, personaje, preview, bodega, `CItemExt` y opciones. `CItemExt` y `EventDBSet` no las usa ningún handler (código muerto). Los dos objetos de SQL de gremios **nunca se conectan** (ver Quirks).
5. **Query de arranque** sobre la conexión serial: `SELECT ItemCount, Number from GameServerInfo where Number=0`. Guarda el valor tal cual en `DAT_00440978` (o -103 si falló el exec, -101 si no hubo fila). Se lo devuelve a cada GameServer en el login (head 0x00).
6. **Watchdog StateMode** (`0x00401140`): un hilo revisa cada 1 s; si pasaron más de 30.000 ms sin paquetes, loguea `StateMode Change 0`. Solo afecta el color del banner de la UI.
7. Log `DataServer Run` y **red**: hilo de accept en el puerto P y un segundo listener "port check" en **P+1** (`0x004067a0`/`0x00405870`, `listen(8)` + `WSAAsyncSelect(FD_ACCEPT)`), que solo sirve de sonda TCP.

**Autenticación real** (verificado en el registro de la VM): el DSN `MuOnline` tiene `Trusted_Connection=Yes`, así que `Admin`/`BobStudio` se ignoran y conecta con la cuenta de Windows del servicio. El login SQL `Admin` ni siquiera existe. En MuRet las credenciales salen de la config.

## Modelo de hilos

```
accept (WSAAccept loop) ──► tabla de 100 slots ──► asocia socket al IOCP
                                                   │
2×CPU workers: GetQueuedCompletionStatus ◄─────────┘
   │ recv completado → parser C1/C2 (0x00403010)
   ▼
cola FIFO (cap. 5000, CS propia) ──► 1 consumidor (lock global) ──► dispatcher 0x0040b190
                                                                     │ handler + SQL (ODBC)
                                                                     ▼
                                                     WSASend directo (sin cola de envío)
aparte: head 0x40 → CreateThread(0x00403d10) · watchdog StateMode · listener sonda P+1
```

Muchos hilos reciben, pero uno solo ejecuta (salvo el head 0x40, que lanza su propio hilo): los workers cortan el stream en paquetes y los encolan, y el consumidor los procesa de a uno, con el SQL incluido. La respuesta no pasa por la cola: el handler llama a `WSASend` directo. Aparte corren el watchdog StateMode y el listener de sonda en P+1.

## Red: framing, cola, sesiones y envío

### Framing (parser `0x00403010`, verificado)

| Tipo | Layout | Size | Head |
| --- | --- | --- | --- |
| C1 | `C1 [size:u8] [head:u8] [payload]` | `byte[1]`, longitud **total** con cabecera | `byte[2]` |
| C2 | `C2 [sizeHi:u8] [sizeLo:u8] [head:u8] [payload]` | `byte[1]<<8 \| byte[2]`, big-endian, total | `byte[3]` |

Reglas, en el orden en que las aplica el parser sobre el buffer acumulado (0x2000 bytes por conexión):

1. Byte 0 que no es `0xC1`/`0xC2`: log `error-L1 (Header error)` y descarta todo el acumulado (única rama que lo pone a 0).
2. `size == 0`: log y retorna **sin** limpiar ni compactar (quirk de re-encolado).
3. Paquete incompleto (`accum < size`, comparación con signo): si ya se procesó alguno en esta pasada y `0 < accum < 0x2000`, mueve el resto al principio del buffer y espera; si no, espera sin tocar nada.
4. Paquete completo: lo encola entero (desde el `0xC1`/`0xC2`) con su head y el índice de conexión, y sigue con el siguiente. Un `recv` puede traer varios paquetes.

No valida que `size <= 0x800` ni que la cabecera entera esté dentro de lo recibido.

### Cola y consumidor (`0x00405b00` / `0x00405c00` / `0x00402da0`, verificado)

- Cola FIFO con su propia sección crítica, distinta del lock global. Nodo de 0x204C bytes + payload aparte en el heap.
- Enqueue acepta `0 < size <= 0x10000`; con más de 4999 nodos rechaza **y pierde la memoria** del nodo.
- **Un solo hilo consumidor**: saca un mensaje, llama al dispatcher bajo el lock global, loguea `RecvQ_Count : %d` si hay más de 30 pendientes, y hace `Sleep(1)`. El pop copia el payload a un buffer de pila de **0x800** bytes.
- Consecuencia de diseño: toda la lógica y todo el SQL corren **de a un paquete por vez, con una excepción: el head 0x40 lanza su propio hilo (ver Protocolo)**. No hay carreras entre handlers, pero un query lento frena a todos los GameServers.

### Hilos de I/O (`0x00402a90` / `0x00402e80`, verificado)

- `2 × CPUs` workers sobre un IOCP, más el hilo de accept (`WSASocket` → `bind INADDR_ANY` → `listen(5)` → loop `WSAAccept`).
- El worker distingue recv de send por un campo del contexto. Con 0 bytes o error de `GetQueuedCompletionStatus` cierra la conexión. En recv: suma bytes, llama al parser y re-postea `WSARecv` en `buffer + accum` con `0x2000 - accum` libres.
- Contador de operaciones pendientes por conexión (`ctx+0x406C`); el contexto solo se libera cuando llega a 0.

### Tabla de conexiones (`0x004055e0`, verificado)

- **100 slots** (índices 0..99), búsqueda lineal del primer libre; lleno → `Error : Max User Index`.
- Entrada de 0x5C bytes: `active`@+0x00, IP@+0x04 (cabe en 0x14 bytes), puerto del GS u16@+0x18, clase@+0x1A, nombre del GS@+0x1B, serverId u16@+0x4E, serverId % 20 @+0x50, socket@+0x54, contexto@+0x58. `active = 1` es conectado; el login del GS (head 0x00) lo pasa a **2** y guarda puerto, clase, nombre (0x30) y serverId; un segundo login en el mismo slot falla.
- Contexto por conexión de 0x4078 bytes: dos bloques per-IO (recv y send) con su OVERLAPPED, WSABUF y buffer de 0x2000.

### Envío (`0x00405980` → `0x00403140`, verificado)

- Bajo el lock global; rechaza `len > 0x2000` (`Error (Max msg)`), slots inactivos y contextos nulos.
- Un solo `WSASend` por llamada, sin cola de envío; el WSABUF apunta **al buffer del llamador** (no copia). Si el send queda pendiente y el llamador reutiliza su buffer, se envían datos corruptos.
- **Broadcast** = recorrer los 100 slots y enviar a cada `active >= 1`.

**Límites en una tabla**

| Constante | Valor | Evidencia |
| --- | --- | --- |
| Conexiones | 100 | `(0x6443748 - 0x6441358) / 0x5C` |
| Buffer recv / send | 0x2000 | contexto 0x4078 |
| Buffer del consumidor | 0x800 | `auStack_800` en `0x00402da0` |
| Paquete encolable | 1..0x10000 | `0x00405b00` |
| Nodos en cola | 5000 (rechaza si ya hay más de 4999) | `0x00405a30` |
| Aviso de cola | > 30 | `RecvQ_Count` |
| Timeout StateMode | 30.000 ms | `0x004010b0` |

## Protocolo: los 22 heads

El dispatcher (`0x0040b190`) usa una tabla índice de 0xD0 bytes (`0x0040B3BC`) y un jump table de 23 entradas (`0x0040B360`), ambos extraídos del binario. Un head sin entrada es un no-op silencioso. Para todo `head != 0` marca actividad en StateMode. Los handlers reciben el paquete completo y el índice de conexión; **nunca la longitud**, así que leen en offsets fijos sin chequear que el paquete sea tan largo. Strings de longitud fija sin `\0` garantizado: cuenta 10, personaje 10, gremio 8.

### Sesión del GameServer

| Head | Request | Efecto | Respuesta |
| --- | --- | --- | --- |
| 0x00 Login del GS | clase@+3, puerto u16@+4, nombre@+6 (0x30), serverId u16@+0x38 | marca el slot como GS (`active=2`), guarda los datos; sin SQL | al que pidió: `C1 08 00`, result@+3 (1 ok / 0 `Client_Join_Fail`), ItemCount u32@+4 |
| 0x02 Broadcast | paquete C1 cualquiera | ninguno | **reenvía el mismo paquete** a todos los slots activos, con len = `byte[1]` (si llega un C2, el len sale mal) |
| 0x40 Sync de gremios | nada | recorre la cache de gremios en RAM | al que pidió: `C1 08 41` sub=0 + count; un `C1 40 40` por gremio (number@+4, G\_Name@+6, master@+0xF, mark@+0x1A (32), score@+0x3C); `C1 08 41` sub=1. Corre en un hilo propio (CreateThread de 0x00403d10), fuera del consumidor, con Sleep(1) entre gremios. Como la cache está siempre vacía, en la práctica son solo los dos marcadores |

### Cuenta y personajes

| Head | Request | Efecto | Respuesta |
| --- | --- | --- | --- |
| 0x01 Lista de personajes | cuenta@+3, id u16@+0xE | si la cuenta no existe la crea (`INSERT AccountCharacter`); lee GameID1..5 (`WHERE` mayúsculas, comparación de Id sensible a mayúsculas); por cada nombre válido lee `vCharacterPreview` y su `Inventory`; si falla, **saltea** ese personaje | `C2 01`: id@+4, count@+6, flag@+0xC (1 si algún nivel >= 220), cuenta@+0xD; registros de 0x4E desde +0x18: slot, nombre (10), cLevel u16@+0xC, Class@+0xE, CtlCode@+0xF, preview de 12 items desde +0x10 (formato según DbVersion 0 o 1), DbVersion@+0x4C. *Bytes exactos: seam* |
| 0x04 Crear personaje | id u16@+0xC, cuenta@+0xE, nombre@+0x18, clase@+0x22 (nibble alto = clase 0..3) | busca el primer GameID vacío; valida el nombre (sin `'` ni espacio, badsyntax, reservados); `EXECUTE MX_CreateCharacter` con stats de la clase, nivel 1, mapa 0, X=182, Y=128; si sale bien, escribe Inventory (0x2F8), MagicList (0x3C) y Quest (0x32) y `UPDATE AccountCharacter SET GameIDn` | `C1 34 04`: result@+3 (1 ok, 0 nombre inválido o fallo del SP, 2 sin slot), id@+4, cuenta@+6, nombre@+0x10, slot@+0x1A (0xFF si no hay), clase@+0x1B, preview de 12 pares desde +0x1C |
| 0x05 Borrar personaje | id u16@+4, cuenta@+6, nombre@+0x10, guildFlag@+0x1A, G\_Name@+0x1B | `DELETE FROM Character`, limpia el GameID; después de responder, si guildFlag=1 ejecuta DeleteGuild y si es 2 RemoveGuildMember | `C1 10 05`: result@+3 (0 **solo** si cuenta o nombre vacíos; un DELETE fallido igual responde 1), id@+4, cuenta@+6 |
| 0x06 Cargar personaje (entrar al juego) | cuenta@+3, nombre **cifrado** con XOR `FC CF AB`@+0xE, userIdx u16@+0x1A | descifra; rechaza nombres con espacio; `EXECUTE MX_SelectCharacter`; verifica que `AccountID` coincida; lee los 3 blobs (sin fila → relleno 0xFF; error → falla) y `Leaderpoint`; si sale bien, `UPDATE AccountCharacter SET GameIDC` | **siempre** `C2 03BC 06`: status@+4 (1/0), userIdx@+6, cuenta@+8, nombre@+0x12, Class@+0x1D, cLevel@+0x1E, LevelUpPoint@+0x20, Exp@+0x24, Money@+0x2C, Str/Dex/Vit/Ene@+0x30, Life/MaxLife/Mana/MaxMana como u16 ×10@+0x38, Inventory@+0x40, MagicList@+0x338, mapa/X/Y/dir@+0x374, Pk×3@+0x378, CtlCode@+0x384, DbVersion@+0x385, Quest@+0x387 (con Leaderpoint pisando sus 2 últimos bytes). Si ok y hay fila de opciones: además `C1 24 60` |
| 0x07 Guardar personaje | nombre@+4, cLevel@+0x10, Class@+0x12, LevelUpPoint@+0x14, Exp@+0x18, Money@+0x20, stats u16@+0x24..+0x2A, vida/maná u16 ×0,1@+0x2C..+0x32, Inventory@+0x34, MagicList@+0x32C, mapa/X/Y/dir@+0x368, Pk×3@+0x36C, Quest@+0x378 | `UPDATE Character SET … DbVersion=1`; los 3 blobs **solo** si el UPDATE salió bien. Nombre vacío → `error-L2 : Name is zero` | ninguna |
| 0x11 Guardar inventario | nombre@+4, Inventory@+0xE (0x2F8) | verifica con `SELECT Inventory` y hace `UPDATE … Inventory=?` | ninguna |
| 0xCF Transferir personaje | nombre@+3, eco u16@+0xE | si el nombre tiene más de 1 carácter: `SP_CHARACTER_TRANSFER '%s'` (sin `EXECUTE`); result = primera columna, o 1 si falla | `C1 12 CF`: nombre@+3, eco@+0xE, result@+0x10. El SP no existe en la base del lab: siempre 1 |

### Baúl

| Head | Request | Efecto | Respuesta |
| --- | --- | --- | --- |
| 0x08 Abrir baúl | cuenta@+3, id u16@+0xE | si no hay fila en `warehouse`: `INSERT` (Money 0) | baúl nuevo: `C1 10 10` (¡head 0x10!) con cuenta@+3 e id@+0xE |
|  |  | si existe: `EXECUTE MX_SELECTmoney` (Money, DbVersion, pw) y `EXECUTE MX_OPENwarehouse` (Items) | `C2 04C8 08`: cuenta@+4, id@+0xE, Money@+0x10, Items@+0x14 (1200 bytes, 0xFF si vacío), DbVersion@+0x4C4, pw u16@+0x4C6 |
| 0x09 Cerrar baúl | cuenta@+4, Money@+0x10, Items@+0x14 (1200), pw u16@+0x4C6 | `EXECUTE MX_UPDATEmoney money,pw,'cuenta'` y `EXECUTE MX_CLOSEwarehouse ?,'cuenta'` con Items como parámetro binario | ninguna |
| 0x12 Guardar dinero del baúl | cuenta@+3, Money@+0x10 | `UPDATE warehouse SET Money` | ninguna |

### Gremios (todos rotos en el original, ver Quirks)

| Head | Request | Efecto si la conexión existiera | Respuesta real del original |
| --- | --- | --- | --- |
| 0x30 Crear | G\_Name@+3, master@+0xC, G\_Mark@+0x17 (32), ecos@+0x37 | valida nombre (silencio si es inválido); `INSERT Guild`, mark, `SELECT Number`, `INSERT GuildMember` con G\_Level=1 (rollback si falla), alta en cache, broadcast | `C1 44 30` con result=3 solo al que pidió |
| 0x31 Borrar | G\_Name@+5, master@+0xD, ecos@+3 | `DELETE GuildMember` y luego `DELETE Guild` | `C1 1B 31` con result=4, **broadcast** a todos (flag@+4 = 1 para el que pidió) |
| 0x32 Agregar miembro | G\_Name@+3, miembro@+0xC, ecos@+0x17 | `INSERT GuildMember`, alta en cache, broadcast | `C1 1E 32` con result=4 solo al que pidió |
| 0x33 Quitar miembro | ecos@+3, G\_Name@+5, nombre@+0xD | `DELETE GuildMember`, baja en cache, broadcast | `C1 1B 33` con result=3 solo al que pidió |
| 0x37 Score | G\_Name@+3, score@+0xC | `UPDATE Guild SET G_Score` | ninguna (builder `0x0040d520` sin reconstruir) |
| 0x38 Aviso | G\_Name@+3, aviso@+0xC (60) | `UPDATE Guild SET G_Notice` y broadcast `C1 48 38` | ninguna |

### Varios

| Head | Request | Efecto | Respuesta |
| --- | --- | --- | --- |
| 0x20 Contador de items | u32@+4 | `UPDATE GameServerInfo SET ItemCount = %d where Number=0` | ninguna |
| 0x52 Serial de item | campos de eco en +3..+0x15 | ejecuta `WZ_GetItemSerial` y lee la primera columna (un serial 0 se envía igual) | `C1 1C 52`: ecos, serial u32@+8. Sin fila: no responde |
| 0x60 Guardar opciones | nombre@+3, SkillKey@+0xD (10), GameOption/Q/W/E/Chat@+0x17..+0x1B | si no existe: `INSERT OptionData`; si existe: `UPDATE` + `UPDATE … SkillKey=?` | ninguna |

Todas las strings SQL están *verbatim* en `shared/include/ds_sql.h` (mayúsculas incluidas, por ejemplo `MX_SELECTmoney`). Todas se arman con `sprintf` y `'%s'`, sin escapar.

## Datos auxiliares

El DataServer no es solo SQL: crear y cargar personajes depende de tablas y constantes que hay que portar como datos, no como código.

**Stats iniciales por clase** (tabla `DAT_0643f974`, stride 0x65C). No está en el archivo: se llena en runtime, y se recuperó con un dump de `.data` del proceso vivo (`vm_evidence/ds_data_statregion_2026-10-14.bin`). Verificado; coincide con los valores canónicos de MU.

| Clase | Str | Dex | Vit | Ene | Vida / Máx | Maná / Máx | Kit inicial (grupo, índice) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 Dark Wizard | 18 | 18 | 15 | 30 | 60 | 60 | ninguno; primer skill 0x11 en MagicList |
| 1 Dark Knight | 28 | 20 | 25 | 10 | 110 | 20 | Small Axe (1,0) |
| 2 Fairy Elf | 22 | 25 | 20 | 15 | 80 | 30 | Arrow (4,15) + Short Bow (4,0) |
| 3 Magic Gladiator | 26 | 26 | 26 | 26 | 110 | 60 | Short Sword (0,1) + Small Shield (6,0) |

Una clase > 3 se fuerza a Dark Knight con el log `error-L3 : Class over value`.

**Formatos de blob**

| Blob | Tamaño | Formato |
| --- | --- | --- |
| Inventory | 0x2F8 (760) | 76 items de 10 bytes: code (`grupo*32 + índice`), byte1, durabilidad, …; 0xFF = vacío |
| MagicList | 0x3C (60) | 20 entradas de 3 bytes; 0xFF = vacío |
| Quest | 0x32 (50) | bytes en 0 al crear |
| Items del baúl | 1200 | mismo formato de 10 bytes por item |
| SkillKey | 10 | teclas rápidas |
| G\_Mark | 32 | escudo del gremio |

**Tabla de items**: 512 registros de 0x58 bytes, cargada desde `item(kor).txt` (`0x00408080`). El DataServer solo usa la durabilidad (`rec+0x30`) para el kit inicial; el cálculo de precio que también está en el binario es código muerto.

**Nombres prohibidos**: `badsyntax.txt` (rechaza si una palabra es *subcadena* del nombre) y reservados (nombres de items, monstruos y skills, rechaza si coincide *exacto*). Ambos sensibles a mayúsculas. Se aplican a personajes (0x04) y gremios (0x30).

**Otras constantes**

- Cifrado del nombre en 0x06: XOR con la clave de 3 bytes `FC CF AB` (`0x43ac04`), simétrico.
- Vida y maná viajan como u16 × 10 (factor 10.0f, *inferido* de `0x431a98`) y se guardan como float × 0,1.
- Posición inicial al crear: mapa 0, X=182, Y=128, dir 0.
- Cache de gremios en RAM: lista doblemente enlazada de entradas de 0x25C bytes, máx. 30 miembros (el slot 0 es el master), status 0xFFFF = offline. Nunca se llena desde la base (no hay loader).

## Quirks y bugs del original

La reconstrucción los replica a propósito; MuRet los corrige, pero cada corrección lleva un test que documenta el cambio y, si afecta al wire, un ADR. La última columna es la propuesta; se discute en la etapa correspondiente.

| Quirk | Efecto en el original | En MuRet |
| --- | --- | --- |
| Gremios sin conexión: los 2 objetos SQL de gremios nunca hacen `Connect()` | crear, borrar, agregar y quitar miembro siempre fallan; score y aviso nunca se guardan; la cache queda vacía | implementarlos de verdad, detrás de un flag `guilds.compat_broken` por si el GameServer depende de la respuesta rota |
| SQL armado con `sprintf` y `'%s'` | SQL injection con cualquier nombre que tenga `'` (la validación de nombre lo evita solo en algunos heads) | prepared statements en todos los queries |
| Paquetes de 0x801 a 0x2000 bytes | desbordan el buffer de 0x800 del consumidor | buffers del tamaño del paquete (`std::vector`), máximo validado |
| Los handlers no reciben la longitud | leen más allá del paquete si el GS manda uno corto | cada handler valida el tamaño mínimo de su struct |
| `size == 0` en el framing | re-encola paquetes ya despachados | cerrar la conexión por protocolo inválido |
| Cola llena (> 4999) | descarta y pierde la memoria del nodo | cola acotada con back-pressure y métrica |
| WSABUF apunta al buffer del llamador | envío corrupto si el send queda pendiente | cola de envío por sesión con buffers propios |
| Contexto: escribe antes de chequear si `HeapAlloc` devolvió NULL | crash con memoria agotada | RAII, sin `new` crudo |
| 0x02 usa `byte[1]` como len | un C2 se reenvía con largo incorrecto | len real del paquete |
| 0x05 responde 1 aunque el DELETE falle | el GS cree que borró | result refleja el SQL (*cambio de wire*, ADR) |
| Basura de pila en bytes de relleno de varias respuestas | bytes no deterministas | siempre 0 |
| Credenciales ODBC hardcodeadas | ignoradas por `Trusted_Connection` | config |
| `SP_CHARACTER_TRANSFER` no existe en la base | 0xCF siempre responde 1 | igual (el SP es responsabilidad de la base) |
| Columna `Skillkey` en la base vs `SkillKey` en el binario | inocuo (SQL no distingue mayúsculas) | igual |

Dos quirks más, encontrados al revisar contra el pseudocódigo (2026-10-26):

- **0x00 copia el nombre del GS con `strcpy` sin límite** (`0x004056b0`) al offset +0x1B de la entrada; un nombre de 0x30 bytes sin `\0` pisa serverId (+0x4E) y lo que sigue. La reconstrucción usa `strncpy(0x30)`. En MuRet: copia acotada.
- **0x40 corre en un hilo aparte** (`0x0040ca30` → `CreateThread(0x00403d10)`): es el único handler que no queda serializado por el consumidor; usa la sección crítica de la cache de gremios y `Sleep(1)` por gremio. La reconstrucción lo modela síncrono (mismo wire). En MuRet: tarea en el hilo de lógica, sin hilo extra.

## Discrepancias entre los documentos del research

Revisé los docs del repo de research entre sí y contra el código reconstruido, el pseudocódigo (`dataserver_all.c`) y las strings del binario (`bin/Dataserver.exe`). Las columnas "Correcto" dicen qué confirma la fuente primaria; esta parte ya usa la versión correcta.

| # | Tema | Dice | Dónde | Correcto (evidencia) |
| --- | --- | --- | --- | --- |
| 1 | Respuesta de 0x00 | `C1 08 01` | `CONTRACT.md` §6, `docs/03` | `C1 08 00 01` + ItemCount: head 0x00, result 1 en +3 (`0x0040b490`) |
| 2 | Head 0x02 | "ACK simple" | `docs/03` | broadcast del paquete a todos (`0x0040b8c0`; ya corregido en `CONTRACT.md`) |
| 3 | Head 0x40 | "keep-alive, sin query" | `CONTRACT.md` §6, `docs/03` | sync de gremios en hilo propio (`0x0040ca30` → `0x00403d10`, paquetes `C1 41`/`C1 40`) |
| 4 | Cantidad de heads | 21 | `README.md` del DataServer | 22 (falta 0xCF) |
| 5 | Hilos de red | "IOCP, 2 hilos" | `docs/03` | `2 × CPUs` workers + 1 consumidor + accept (`0x00402a90`) |
| 6 | Formato de `MX_UPDATEmoney` | `'%d' '%d' '%s'` | `docs/03` | ` %d,%d,'%s'  ` (string en 0x3bc04 del binario) |
| 7 | SQL de 0x05 | `DELETE Character/AccountCharacter/GuildMember/Guild` | `docs/03`, `CONTRACT.md` §6 | solo `DELETE FROM Character` + limpiar el GameID; no existe ningún `DELETE … AccountCharacter` en el binario; los DELETE de gremio vienen de la cascada a 0x31/0x33 |
| 8 | Blobs de 0x04 | `INSERT Inventory/MagicList/Quest` | `docs/03`, `CONTRACT.md` §6 | `UPDATE Character SET Inventory=?` (y MagicList, Quest) con parámetro binario |
| 9 | Maná inicial por clase | DK 110, Elf 80, MG 110 | `DYNAMIC_VALIDATION.md`, `shared/include/ds_classdata.h` | DK 20, Elf 30, MG 60: el float en +0x14 es MaxLife, el maná está en +0x1C/+0x20 (corregido solo en `mfc-vc6/ds_classdata.h`) |
| 10 | Layout de la tabla de usuarios | IP de \~0x50 bytes en +0x04 | `CONTRACT.md` §4, `ds_types.h` | el login del GS escribe en +0x18, +0x1A, +0x1B, +0x4E y +0x50 (`0x004056b0`); la IP cabe en 0x14 bytes |
| 11 | Offsets de 0x30 | master@+3, G\_Name@+0xC | `COVERAGE.md` | G\_Name@+3 (8 bytes), master@+0xC (10 bytes) (`0x0040c690`) |
| 12 | Offset de 0x31 | G\_Name@+3 | `COVERAGE.md` | ecos@+3/+4, G\_Name@+5, master@+0xD (`0x0040cd10`) |
| 13 | Modelo de accept | `WSAAsyncSelect(FD_ACCEPT)` → IOCP, "no loop WSAAccept" | `CHECKPOINT.md` | loop `WSAAccept` en un hilo; `WSAAsyncSelect` es el listener de sonda en P+1 (`ds_net.cpp`, revisión Tanda 3) |
| 14 | Estructura de carpetas | `legacy-vc6/` con `src/`, `realbuild/`, smoke test | `src/reconstructed/README.md`, rutas de `COVERAGE.md` | `legacy-vc6/` fue retirada; el código vigente es `mfc-vc6/` (`CHECKPOINT.md`) |
| 15 | Estado de cobertura | arranque y updater ⬜; cascada de 0x05 y stats de 0x06 "seam" | `COVERAGE.md` | implementados en `Server.cpp`, `Updater.cpp` y `ds_handlers.cpp` |
| 16 | INSERT de 0x32 | "G\_Level=1" | comentario en `ds_handlers.cpp` | el código usa `INSERT GuildMember (Name, G_Name)` sin nivel, como la string 0x3c06c del binario; el comentario está mal |
| 17 | Cantidad de funciones | 1910 | `docs/03` (último párrafo) | 1572; las 1910 de ReVa incluían \~340 duplicados (el mismo doc lo explica más abajo) |

Uno menor, el 18: `COVERAGE.md` dice `EXECUTE SP_CHARACTER_TRANSFER` para 0xCF, pero el binario no lleva `EXECUTE` (string 0x43b5e4). El punto 19 está en la tabla de abajo. Las correcciones al repo de research quedan pendientes.

### Contraste con la documentación oficial (Context7, 2026-10-26)

Chequeamos contra la doc de Microsoft (Win32 y windows-docs-rs) y la de Asio las afirmaciones del contrato que dependen de una API externa.

| Afirmación | Resultado | Fuente |
| --- | --- | --- |
| El error ODBC se lee con `SQLGetDiagRec` porque el primer argumento es el tipo de handle (2 = `SQL_HANDLE_DBC`) | **Confirmado**: la firma es `(HandleType, Handle, RecNumber, SqlState, NativeError, MessageText, BufferLength, TextLength)` | [SQLGetDiagRec](https://microsoft.github.io/windows-docs-rs/doc/windows/Win32/System/Search/fn.SQLGetDiagRec.html) |
| `WSA_IO_PENDING` (0x3E5) = la operación quedó en curso y se completa después | **Confirmado** | [LPFN\_TRANSMITPACKETS](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_transmitpackets) |
| Quirk del envío: los datos tienen que seguir válidos hasta que el transporte los consuma | **Confirmado**: el proveedor copia el arreglo de WSABUF, no los datos; la completion llega cuando los buffers fueron consumidos. Como los handlers envían desde buffers de pila y retornan enseguida, un send pendiente puede mandar basura | [WSARecvMsg, Remarks](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_wsarecvmsg) |
| El worker cierra la conexión cuando `GetQueuedCompletionStatus` falla | **Bug nuevo (19)**: si falla con `lpOverlapped == NULL` no se desencoló nada y la completion key es *indeterminada*. El worker (`0x00402e80`) llama igual a `FUN_00403600(key)` sin mirar el OVERLAPPED, así que cierra la conexión de la vuelta anterior. Tampoco decrementa el contador de pendientes en esa rama, al contrario de lo que dice `CONTRACT.md` §3 | [GetQueuedCompletionStatus](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus) |
| Apagado: `PostQueuedCompletionStatus` con key NULL para despertar y terminar workers | **Confirmado** como patrón válido; en Windows Server 2003 cerrar el puerto *no* despierta a los workers, así que el post es necesario | mismas páginas |
| Plan de MuRet: Asio usa IOCP en Windows y epoll en Linux | **Confirmado**. En Windows la demultiplexación corre en todos los hilos que llaman `io_context::run()`; en Linux, en uno de ellos | [Asio, implementation notes](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/implementation.html) |

Context7 no tiene la referencia de la API ODBC clásica de Microsoft (solo el driver para Linux/macOS); para ODBC se usó la proyección de windows-docs-rs, igual que en el research.

## Lo que todavía no se sabe

Casi todo lo que falta es de un solo tipo: bytes exactos de respuestas que dependen de datos de gameplay. Se cierra con una captura "golden" del wire real: un hook dentro del proceso en `ds_SendPacket` (`0x00405980`), nunca con un debugger, mientras un cliente real entra al juego en la VM.

| Pendiente | Tipo | Cómo se cierra |
| --- | --- | --- |
| Bytes exactos de las respuestas 0x01, 0x04, 0x06 y 0x07 (registros de preview, relleno) | seam | golden de respuestas |
| Builder de la respuesta de 0x37 (`0x0040d520`) | seam | RE del builder (inalcanzable bajo el quirk, baja prioridad) |
| Nombre de la columna `pw` de `MX_SELECTmoney` | inferido | leer la definición del SP en la base del lab |
| Columnas `Life`/`Mana` y `Qkey`/`Wkey`/`Ekey` | inferido | schema vivo (las de `Character` ya están confirmadas por el golden SQL) |
| Factor 10.0f de vida y maná | inferido | golden de 0x06 |
| Ordinales exactos de ODBC32/WS2\_32 | pendiente | tabla de exports de la VM (no afecta a MuRet) |
| Por qué el EP está al final de `.text` | abierto | no afecta al comportamiento |

Para MuRet, lo único bloqueante es el golden de respuestas, y recién en la ola 7. Hasta entonces alcanza con el contrato de arriba.

## Cómo usamos este contrato en MuRet

Cada fila de esta parte se convierte en un test antes de escribir el código que la cumple. Así el contrato deja de ser un documento y pasa a ser algo que `ctest` verifica.

| Sección del contrato | Ola | Tipo de test (GoogleTest) |
| --- | --- | --- |
| Framing C1/C2 | 1.2 | unitario con bytes literales: partido, varios por recv, header inválido, `size == 0`, C2 big-endian |
| Layout de requests y respuestas | 1.3 | `static_assert(sizeof)` + round-trip contra bytes de referencia |
| Tabla de 100 conexiones, envío, broadcast | 2.4 y 3 | integración con clientes de prueba, en IOCP y en epoll |
| Bugs de red del original (WSABUF, GQCS sin OVERLAPPED) | 3.3 | test que reproduce el escenario y verifica la versión corregida |
| Cola de un solo consumidor | 4 | estrés con ThreadSanitizer: orden FIFO, límite y back-pressure |
| Heads 0x00, 0x02, 0x40 | 5.2 | integración con repositorio en memoria |
| SQL *verbatim* | 6–7 | comparar las strings de MuRet contra `ds_sql.h` |
| Heads de cuenta, personajes, baúl, gremios | 5.3 y 7 | repositorio en memoria + SQL Server de la VM2003 |
| Paridad con el original | 8.2 | `packet-replay`: mismo request a ambos servidores, comparar respuestas byte a byte |

Cuando un test de paridad difiere, hay tres respuestas posibles: bug de MuRet, quirk corregido a propósito (tiene que estar en la tabla de quirks), o un dato que este contrato todavía no tenía (se agrega acá con su evidencia).
