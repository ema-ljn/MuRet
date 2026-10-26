# 02. Arranque y configuración

En el laboratorio se lanza como `c:\muserver\dataserver1\dataserver.exe 55960`; la segunda
instancia usa `55962`. El GameServer se conecta como cliente
(`gameserver.exe 127.0.0.1 55970 127.0.0.1 55960 55901`). Todo el arranque es **secuencial en el
hilo de la UI** hasta que se crea el hilo de accept.

## Secuencia

Las direcciones de la columna "Dónde" son los `call` dentro de cada función, en el orden en que se
ejecutan (verificado en `disasm/Dataserver.asm`).

| # | Paso | Dónde | Si falla |
| --- | --- | --- | --- |
| 0 | Constructores estáticos de la CRT: `EventDBSet` **conecta a ODBC** antes de `WinMain` | ctor `0x40b0e0` (tabla CRT `0x403fc0`) | MessageBox `EventDBSet ODBC Connect Fail`; **sigue** |
| 0' | Otros constructores estáticos: la cola de mensajes `0x4404e0` y una segunda cola sin uso `0x4404b0`, ambas con tope 5000 ([03](03-hilos-y-concurrencia.md#la-cola)) | `0x4028f0`, `0x402930` | — |
| 1 | `WSAStartup(2.2)` | `0x406700`, desde `InitInstance 0x402530` | MessageBox; **sigue** (el retorno se ignora) |
| 2 | `SetRegistryKey("Mu Data Server")` | `0x402530` | — |
| 3 | Línea de comandos: `sscanf("%d %d")` → puerto, modo | `0x4025e0` | sin validación (ver abajo) |
| 4 | `CMainFrame::OnCreate`: toolbar, status bar, vista de log | `0x4042f0` | — |
| 5 | Auto-update: `WZ_UM.DLL` / `AU_CHECKUPDATE` | `call 0x40d900` en `0x404424` | puede **terminar el proceso** si hay update |
| 6 | Habilita el log en pantalla y en `log\YYYYMMDD.log` | `0x405f70(1)` | sin archivo, solo pantalla |
| 7 | Tabla de items desde `..\data\lang\kor\item(kor).txt` | `call 0x408080` en `0x4047e6` | no fatal |
| 8 | Nombres reservados: items, `..\data\Monster.txt`, `Skill(<idioma>).txt` | `0x404817`, `0x40484c`, … | MessageBox; no fatal |
| 9 | `..\data\badsyntax.txt` | `call 0x401210` en `0x4048b4` | MessageBox; no fatal |
| 10 | Título de ventana `DataServer  Puerto:%u  Ver : 4.4 Oct 21 2003` + sufijo de idioma | `0x40491c` | — |
| 11 | `OnStart` | `call 0x404ad0` en `0x40494d` | `OnCreate` devuelve -1 |
| 11.1 | Pone en cero la tabla de conexiones (100 × `0x5C`) y su contador | `call 0x4055c0` en `0x404adc` | — |
| 11.2 | **7 conexiones ODBC**, en orden: serial, cuenta, personaje, preview, baúl, `CItemExt`, opciones | desde `call 0x40a0a0` en `0x404ae6` | **aborta en la primera que falle** |
| 11.3 | `SELECT ItemCount, Number from GameServerInfo where Number=0` → `0x00440978` | `call 0x40a0e0` en `0x404b5f` | guarda -103 (exec falló) o -101 (sin fila) **y sigue** |
| 11.4 | Hilo watchdog StateMode | `call 0x401140` en `0x404bb8` | — |
| 11.5 | Log `DataServer Run` | — | — |
| 11.6 | Red: hilo de accept en el puerto P | `call 0x402970` en `0x404bd8` | se loguea; **el retorno se ignora** |
| 11.7 | Listener "port check" en P+1 | `call 0x405870` en `0x404c05` | se loguea; no fatal |

Después de esto el hilo de la UI queda en el bucle de mensajes de MFC (repinta el log cada 1 s
con el timer 1001 y rota el archivo de log con el timer 1000, cada 100 s).

## Línea de comandos

`FUN_004025e0` (formato en `0x43a1f4`):

- Si la línea tiene longitud ≤ 1 → puerto `55960` (`0xDA98`), modo `0`.
- Si no → `sscanf(cmdline, "%d %d", &port, &mode)` y guarda los **16 bits bajos** de cada uno.
  No valida rango: `"70000 0"` da puerto `4464`; `"0 0"` da puerto 0 (el sistema elige uno).
- El **modo** solo decide en qué mitad de la pantalla se ubica la ventana (izquierda si 0).

## Configuración leída de archivos

| Archivo | Clave | Uso |
| --- | --- | --- |
| `..\data\commonserver.cfg` | `[GameServerInfo] Language` (0 kor, 1 eng, 2 jpn, 3 chs, 4 tai, 5 tha) | sufijo del título y ruta de `Skill(<x>).txt`. En el lab vale 3. |
| `.\wz_update.ini` | `[UPDATE_DATA] SkipUpdate`, `Version` | auto-update. El DS **vuelve a escribir `SkipUpdate=0`** cada vez que lo lee, y lo crea si falta. |
| `..\data\lang\kor\item(kor).txt` | — | tabla de items; **siempre** la versión coreana, sea cual sea `Language` |
| `..\data\Monster.txt`, `..\data\lang\<x>\Skill(<x>).txt`, `..\data\badsyntax.txt` | — | filtros de nombres ([07](07-datos-auxiliares.md)) |

Las rutas son relativas al directorio de trabajo (`C:\Muserver\DataServer1` → `..\data` es
`C:\Muserver\data`).

## Conexión a la base

Las 8 conexiones usan `CQuery::Connect(3, "MuOnline", "Admin", "BobStudio")`, es decir
`SQLConnect` contra el DSN `MuOnline`, cada una con sus propios handles ODBC.

**Verificado en la VM:** el DSN `MuOnline` (`HKCU\SOFTWARE\ODBC\ODBC.INI\MuOnline`) tiene
`Driver=SQLSRV32.dll`, `Server=(local)`, `Database=MuOnline` y `Trusted_Connection=Yes`. Con eso el
driver **ignora** el usuario y la clave y entra con la cuenta de Windows del proceso (sysadmin). El
login SQL `Admin` ni siquiera existe en el servidor. Las credenciales hardcodeadas son un vestigio
del código de WebZen.

Además de esas 8, hay **dos objetos `CQuery` de gremios que nunca llaman a `Connect`** (ver
[Q-01](08-quirks-y-bugs.md#q-01-los-gremios-nunca-funcionan)).

## Auto-update

`FUN_0040d900` copia `.\WZ_AutoUpdater.exe` y `WZ_UM.DLL` a `.\Temp\`, carga `WZ_UM.DLL` y llama al
export `AU_CHECKUPDATE` (ABI `__cdecl`) con IP `10.1.1.80`, puerto `2004` y usuario `DSUSER`. Si hay
versión nueva, lanza `WZ_AutoUpdater.exe <%s> <%d> <%s> <%d> <%s> <%s>` y termina. En el
laboratorio `SkipUpdate=1` lo evita. **En MuRet no se porta.**

## Apagado

Al cerrar la ventana (`0x404c20`) loguea cada GameServer conectado, pide confirmación siempre, y
libera los slots de la tabla sin cerrar los sockets. El proceso termina enseguida con
`CFrameWnd::OnClose`. No hay apagado ordenado de la red ni de la cola: los mensajes encolados se
pierden. `WSACleanup` corre en el destructor estático del objeto de red.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Puerto y modo por `sscanf` sin validar | argumentos de línea de comandos validados (rango 1–65535) + archivo de config | 2 |
| `Language` de `commonserver.cfg`, rutas relativas fijas | rutas en la config, sin depender del directorio de trabajo | 5 |
| 8 conexiones con credenciales hardcodeadas, abortar en la primera | cadena de conexión en la config; un pool chico; fallar al arrancar con un mensaje claro | 6 |
| La query de arranque guarda -103/-101 en `ItemCount` y sigue | **compatibilidad:** el valor viaja en la respuesta de 0x00, así que se preserva la semántica, pero se loguea como error | 5–6 |
| Sin apagado ordenado | cerrar el listener, drenar la cola, esperar los envíos pendientes, cerrar | 8 |
| Auto-update y UI MFC | no se portan; consola + log a archivo | — |

Tests que fija: parseo de argumentos (válidos, fuera de rango, vacíos), y que el servidor no
arranca si la base no responde.
