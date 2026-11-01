# 01. Binario y método

## Identidad del binario

| Dato | Valor | Evidencia |
| --- | --- | --- |
| Archivo | `Dataserver.exe`, 286.720 bytes; `Dataserver2.exe` es idéntico | `bin/`, mismo SHA-256 |
| SHA-256 | `58daf2c8438e58799c5954755f6d98b1bc58af06f7b4e13eab749aaff4d47bff` | `docs/03_dataserver_updater.md` |
| Formato | PE32 x86, GUI, ImageBase `0x400000`, entry point `0xFC3E` | inspección PE |
| `SizeOfImage` | ~101 MB (`.rsrc` en VA `0x6454000`); las tablas estáticas grandes viven en `0x063xxxxx`–`0x0644xxxx` | inspección PE |
| Compilador | Visual C++ 6, MFC 4.2 **enlazada estáticamente** (RTTI `.?AVCObject@@`, `.?AVCUserException@@`; sin `MFC42.DLL`) | strings + imports |
| Fecha | título de ventana `Ver : 4.4 Oct 21 2003`; recurso de versión 0.4.2.0 Webzen | `.rdata`, recurso |
| Fuentes originales | `F:\Backup\MuProject_20031004\DataServer\giocp.cpp` (red) y `...\protocol.cpp` (handlers), según los `__FILE__` de los logs | `.data` `0x43a3b4` (offset `0x3a3b4` del archivo), log de `0x40ba58` |
| Imports | `ODBC32`, `WS2_32`, `COMCTL32`, `GDI32`, `WINSPOOL.DRV`, `SHELL32`, `ADVAPI32`, `KERNEL32`, `USER32`. `WZ_UM.DLL` y `comdlg32.dll` no están en la tabla, solo como strings: se cargan en ejecución. `WZ_UM.DLL` la carga el auto-update `0x40d900` ([02](02-arranque.md)); `comdlg32.dll`, *inferido* (`KERNEL32` trae `LoadLibraryA` y `GetProcAddress`) | tabla de imports (`objdump -p`), `strings -a` |
| Funciones | 1572, todas en `.text` `0x401000`–`0x430EF0`; la mayoría es MFC y CRT | Ghidra headless |

**No está empacado ni virtualizado.** El `.text` del proceso vivo (pid 624 en la VM) es idéntico al
del disco (0 bytes distintos de 196 KB). Las diferencias en `.rdata` (1397 de 36.864 bytes) son
relocaciones normales del loader.

## Cómo se analizó

1. **Inspección estática:** secciones, imports, entropía, strings. Ninguna firma de packer.
2. **Decompilación total** con Ghidra 12.1.4 headless y un script propio (`ghidra/FixDecomp.java`)
   que sube los límites del decompilador (`setMaxInstructions`, timeout 300 s). Sin eso, las
   funciones con frames SEH grandes salían truncadas a ~51 líneas. Resultado: 1571 de 1572
   funciones (99,94 %). La única que falla, `FUN_0040b7dd`, y el builder de la lista de personajes
   (`0x40b64b`) se reconstruyeron a mano desde el assembly.
3. **Assembly como fuente de verdad** (`disasm/Dataserver.asm`, sintaxis Intel, VAs reales) para
   offsets, tamaños, signo de comparaciones y convención de llamada.
4. **Validación dinámica en la VM2003:** procesos vivos, esquema de la base `MuOnline`, ejecución de
   la query de arranque, DSN ODBC leído del registro, volcado de `.data` del proceso vivo y una
   captura parcial del wire.
5. **Revisión adversarial:** más de 8 rondas independientes (Opus 5.5 y astra) comparando el código
   reconstruido contra assembly y pseudocódigo, con las correcciones aplicadas
   (`src/reconstructed/docs/reviews/`).
6. **Reconstrucción compilable:** `src/reconstructed/mfc-vc6/` compila con VC6 en la VM, conecta a
   SQL Server, ejecuta la query de arranque y escucha en 55960. Es la referencia ejecutable de este
   research, pero no reemplaza al assembly.

### Una trampa del método: funciones plegadas por el linker

**Verificado.** El linker de VC6 pliega funciones idénticas (identical COMDAT folding). Ejemplo:
`0x4010a0` es `mov eax,[ecx+4]; ret`. Ghidra lo nombra como el getter de "StateMode activo", pero
el hilo de sync de gremios (`0x403d29`) hace `mov ecx,0x6411e40; call 0x4010a0` para leer la cabeza
de la lista de gremios. Es el mismo cuerpo sirviendo a dos clases distintas. Cuando un nombre de
función no cuadra con el contexto, hay que mirar qué hay en `ecx` antes del `call`.

## Mapa de memoria (globales relevantes)

| Dirección | Qué es | Usado por |
| --- | --- | --- |
| `0x0043a214` | socket de escucha principal | listen `0x4029c0` |
| `0x0043a218` | puerto (u16) | `0x402970` |
| `0x0043f960` | tabla `badsyntax` (100 × 25 bytes, count en +0x9c8) | filtro de nombres |
| `0x00440470` | handles de los hilos worker | `0x402a90` |
| `0x004404b0` | segunda cola, construida y destruida pero **sin uso** | — |
| `0x004404e0` | cola de mensajes (count +0x00, tope +0x04, head +0x08, tail +0x0C, CS +0x14 = `0x4404f4`) | parser, consumidor |
| `0x00440510` | **lock global** (CS) de red, cola y lógica | todo |
| `0x0044052c` / `0x00440530` | cabeza / cola de la lista de contextos de conexión | `0x403440` |
| `0x00440534` | cantidad de workers (= CPUs × 2) | `0x402a90` |
| `0x00440538` | handle del IOCP | workers |
| `0x00440540` | objeto StateMode (activo +4, último tick +8, CS +0x14) | dispatcher, watchdog |
| `0x00440978` | `ItemCount` leído al arrancar | head 0x00 |
| `0x0044098c` | socket del listener "port check" (puerto + 1) | `0x405870` |
| `0x063df720` | tabla de nombres reservados (1000 × 20, count en +0x4e24) | filtro de nombres |
| `0x063e4548` | `CQuery` de opciones | 0x06, 0x60 |
| `0x063ebec8` | `CQuery` de `CItemExt` (conecta, nadie la usa) | — |
| `0x063f3848` | `CQuery` de miembros de gremio (**nunca conecta**) | 0x30–0x33 |
| `0x063fb1c0` | `CQuery` de baúl | 0x08, 0x09, 0x12 |
| `0x06402b40` | `CQuery` de `EventDBSet` (conecta en constructor estático, nadie la usa) | — |
| `0x0640a4c0` | `CQuery` de preview de personajes | 0x01 |
| `0x06411e40` | gestor de gremios en RAM (count +0x14 = `0x6411e54`, CS +0x18 = `0x6411e58`) | 0x30–0x40 |
| `0x06411e78` | `CQuery` de la tabla `Guild` (**nunca conecta**) | 0x30, 0x31, 0x37, 0x38 |
| `0x06428ae0` | `CQuery` de personaje | 0x04, 0x05, 0x06, 0x07, 0x11, 0xCF |
| `0x06430460` | `CQuery` de cuenta | 0x01, 0x04, 0x05, 0x06 |
| `0x06437de0` | `CQuery` "serial" (GameServerInfo, seriales) | 0x20, 0x52, arranque |
| `0x0643f974` | tabla de stats por clase (stride `0x65c`), se llena en runtime | 0x04 |
| `0x06441358` | **tabla de conexiones**: 100 entradas de `0x5C` | red, broadcast |
| `0x06443748` | contador de conexiones (justo después de la tabla) | red |
| `0x06443764` | hook de log de SQL | `CQuery` |
| `0x0644376c` | callback de log general | todo |
| `0x064437cc` | ring buffer del log en pantalla (30 × 0x50) | UI |
| `0x06444238` | tabla de items (512 × `0x58`) | 0x04 |

## Mapa de funciones clave

| Dirección | Rol | Doc |
| --- | --- | --- |
| `0x402530` | `CDataServerApp::InitInstance` (WSAStartup, línea de comandos) | [02](02-arranque.md) |
| `0x4042f0` | `CMainFrame::OnCreate`: driver del arranque | [02](02-arranque.md) |
| `0x404ad0` | `OnStart`: ODBC, query de arranque, red | [02](02-arranque.md) |
| `0x40d900` / `0x40dde0` | auto-update (WZ_UM / `wz_update.ini`) | [02](02-arranque.md) |
| `0x402970` | crea el hilo de accept | [04](04-red.md) |
| `0x402a90` | hilo de accept: IOCP, workers, consumidor, bucle `WSAAccept` | [03](03-hilos-y-concurrencia.md) |
| `0x4029c0` | `WSASocket` + `bind` + `listen(5)` | [04](04-red.md) |
| `0x402e80` | worker IOCP | [04](04-red.md) |
| `0x403010` | parser de framing C1/C2 | [04](04-red.md) |
| `0x402da0` | consumidor de la cola | [03](03-hilos-y-concurrencia.md) |
| `0x4059a0` | constructor de cola (tope, CS con spin 4000) | [03](03-hilos-y-concurrencia.md#la-cola) |
| `0x405b00` / `0x405c00` / `0x405a10` / `0x405a30` / `0x405ab0` | cola: enqueue, pop, count, push, pop de lista | [03](03-hilos-y-concurrencia.md) |
| `0x4055c0` | pone en cero la tabla de conexiones | [02](02-arranque.md) |
| `0x4055e0` | asigna slot en la tabla de conexiones | [04](04-red.md) |
| `0x403270` / `0x4032f0` | crea contexto de conexión y lo asocia al IOCP | [04](04-red.md) |
| `0x403600` / `0x403610` | cierre de conexión | [04](04-red.md) |
| `0x405980` → `0x403140` | envío (`ds_SendPacket` → `WSASend`) | [04](04-red.md) |
| `0x405870` / `0x404a50` | listener "port check" en puerto + 1 | [04](04-red.md) |
| `0x40b190` | dispatcher head → handler | [05](05-protocolo.md) |
| `0x40b490`…`0x40d840` | los 22 handlers | [05](05-protocolo.md) |
| `0x401a80` | `CQuery::Connect` | [06](06-sql-y-base.md) |
| `0x401d10` | `CQuery::ExecQuery` | [06](06-sql-y-base.md) |
| `0x402120` | `CQuery::Diag` (reconexión en `08S01`) | [06](06-sql-y-base.md) |
| `0x401140` / `0x401070` | watchdog StateMode / marcar actividad | [03](03-hilos-y-concurrencia.md) |
| `0x403d10` | hilo de sync de gremios (head 0x40) | [03](03-hilos-y-concurrencia.md) |
| `0x408080` | carga de la tabla de items | [07](07-datos-auxiliares.md) |
| `0x401580` / `0x404db0` | filtros de nombres (badsyntax / reservados) | [07](07-datos-auxiliares.md) |

## Qué implica para MuRet

- La **compatibilidad** se define contra el comportamiento observable: bytes en el puerto 55960 y
  texto SQL hacia `MuOnline`. No contra el layout interno ni el PE.
- Las tablas de este documento son la referencia para los tests de contrato: cada test cita la
  dirección del original que fija.
- La reconstrucción `mfc-vc6/` se puede compilar en la VM y usar como **oráculo** para tests de
  paridad (ola 8), junto con el original.
