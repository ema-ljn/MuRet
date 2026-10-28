# Front-ends del DataServer: CLI en Linux y GUI moderna en Windows (futuro)

> **Documento de trabajo a futuro, 2026-10-26.** No es parte de las olas 0–8 ni cambia sus planes.
> Es una propuesta para **después** de la ola 8 (o en paralelo, si Emanuel quiere cambiar de tema
> un rato): cómo se maneja el DataServer de MuRet desde afuera. Todo es **opcional** y se decide
> cuando se arranque, con su ADR.
>
> Contexto: el roadmap ([D-7](2026-10-25-dataserver-port-design.md#decisiones-de-diseño)) ya decidió
> que la ventana MFC del original se reemplaza por **consola + log a archivo**. Eso no cambia: el
> DataServer sigue siendo headless en los dos sistemas. Este documento propone qué se construye
> encima: en Linux, consola y acceso remoto tomando como referencia a OpenMU y a los emuladores de
> WoW; en Windows, una GUI como la del original pero con estándares actuales (decisión de Emanuel).
> Todo sin ensuciar el núcleo.
>
> Desde el [camino de aprendizaje](2026-10-28-camino-de-aprendizaje-design.md) (L-12) es la
> **ola F**: opcional y sin lugar fijo después de la ola 8, porque los números del 9 en adelante
> quedan para sistemas distribuidos.
> Autor: Emanuel (ema-ljn), con Claude de tutor.

## Qué hace el original

El DataServer 1.02 es una app MFC con una sola ventana (ver
[02-arranque](../../../Server/DataServer/docs/research/02-arranque.md)):

- Toolbar, status bar y una vista de log que se repinta cada 1 s (timer 1001) desde un ring buffer
  de 30 líneas de 0x50 bytes (`0x064437cc`, ver
  [01-binario-y-metodo](../../../Server/DataServer/docs/research/01-binario-y-metodo.md)).
- Título `DataServer  Puerto:%u  Ver : 4.4 Oct 21 2003`; el "modo" de la línea de comandos solo
  decide en qué mitad de la pantalla se pone la ventana.
- Los errores de arranque salen en `MessageBox` (bloquean hasta que alguien hace clic).
- Cerrar la ventana es la única forma de apagarlo: pide confirmación, loguea los GS conectados y
  termina sin apagado ordenado (Q-09).
- No hay ningún comando: no se puede ver la cola, echar un GS ni recargar los filtros sin reiniciar.

O sea: la UI y el servidor son **el mismo hilo** (el arranque entero corre en el hilo de la UI, ver
[03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md)). Eso es justo lo que no
queremos repetir.

## Qué hace OpenMU (referencia)

[MUnique/OpenMU](https://github.com/MUnique/OpenMU) es la referencia más cercana (otro servidor de
MU, en C#). Mirando `src/Startup/Program.cs`:

- **Un solo proceso, headless por defecto.** Arranca todos los subsistemas (connect, login, game,
  chat) y queda en una consola.
- **Argumentos con forma `-nombre` o `-nombre:valor`:** `-autostart`, `-demo` (base en memoria),
  `-reinit`, `-version:<v>`, `-adminpanel:disabled`, `-resolveIP:<...>`. Los números se validan
  (`DetermineUshort` chequea el rango 0–65535, a diferencia del `sscanf` del DataServer original).
- **REPL mínimo en stdin:** `help`, `exit`, `gc`, `pid`. Lo que no conoce responde
  `Unknown command` y lista los comandos.
- **Ctrl+C con confirmación:** el primer Ctrl+C pregunta `Confirm shutdown? (y/N)`, el segundo (o
  `y`) cancela un `CancellationToken` y todo se apaga ordenado. También escucha `ProcessExit`
  (lo que manda `docker stop` o systemd).
- **Config en archivos** (`appsettings.json` + uno por entorno) y **log** con Serilog a consola y
  archivo, y OpenTelemetry opcional para métricas y trazas.
- **La "GUI" es web:** un admin panel en Blazor dentro del mismo proceso, que se apaga con
  `-adminpanel:disabled`. En Docker (`deploy/all-in-one`) corre todo en un contenedor y el panel
  se abre en `http://localhost/`.

Lo que nos llevamos de OpenMU: **el servidor nunca depende de la UI**. La consola es lo mínimo, el
panel es un extra que se enciende o se apaga, y los dos hablan con el mismo núcleo.

## Qué hacen los emuladores de WoW (referencia)

TrinityCore, AzerothCore y CMaNGOS son servidores en C++ con años de producción en Linux (ver
[referencias de WoW](../../learning/referencias-emuladores-wow.md)). Los tres resuelven la consola
igual, y es el modelo que mejor encaja con nuestro hilo de lógica único. Mirando
`src/server/worldserver/` de TrinityCore (AzerothCore y CMaNGOS son de la misma familia):

- **Argumentos chicos, el resto en el archivo de config.** `Main.cpp` usa
  `boost::program_options` con `--help/-h`, `--version/-v`, `--config/-c <archivo>`,
  `--config-dir`, `--update-databases-only`, y en Windows `--service/-s install|uninstall` para
  registrarse como Windows Service. Todo lo demás vive en `worldserver.conf`, y cualquier clave se
  puede pisar con una variable de entorno (útil en Docker).
- **La consola es un hilo que solo lee y encola.** `CommandLine/CliRunnable.cpp` lee con GNU
  readline en Linux (historial y autocompletado con Tab) y con la API de consola en Windows, con
  un prompt (`TC> `, `AC> `, `mangos>`). Cada línea se envuelve en un `CliCommandHolder` y se
  manda a `World::QueueCliCommand`. El hilo del mundo, en cada tick, llama a
  `ProcessCliCommands()` y la ejecuta. **La consola nunca toca el estado del mundo**: es
  exactamente la regla que necesitamos para no romper los invariantes de la ola 4. El `CliCommandHolder` lleva además dos callbacks (uno para imprimir y otro para avisar
  que terminó): así la respuesta vuelve a quien la pidió, aunque se ejecute en otro hilo.
- **Un solo sistema de comandos para todos los canales.** Los comandos de consola son los mismos que
  los de GM dentro del juego; la consola es un "jugador" con nivel de seguridad de consola.
- **Acceso remoto opcional, apagado por defecto** (`worldserver.conf`):
  - `Console.Enable = 1`: la consola local (se apaga sola en modo servicio).
  - `Ra.Enable = 0`, `Ra.Port = 3443`, `Ra.MinLevel = 3`: *Remote Access*, un telnet de texto que
    pide usuario y clave de una cuenta con nivel de admin y encola los comandos igual que la
    consola (`RemoteAccess/RASession.cpp`).
  - `SOAP.Enabled = 0`, `SOAP.IP = "127.0.0.1"`, `SOAP.Port = 7878`: el mismo comando por HTTP/SOAP,
    para que webs o bots lo llamen.
- **Señales con Asio:** `SIGINT`, `SIGTERM` (y `SIGBREAK` en Windows) se esperan con un
  `signal_set` y disparan el apagado ordenado.
- **No tienen GUI propia.** Las herramientas gráficas (paneles web, Keira3 para editar la base) son
  proyectos aparte que hablan por SOAP o directo a la base.

Lo que nos llevamos de ellos: **la consola es un productor más de la cola**, el acceso remoto es el
mismo vocabulario por otro transporte, y todo lo remoto arranca apagado, solo en `127.0.0.1` y con
auth.

### OpenMU vs emuladores de WoW, en una tabla

| | OpenMU | TrinityCore / AzerothCore / CMaNGOS | Para MuRet |
| --- | --- | --- | --- |
| Lenguaje | C# | C++ | C++, así que el código de WoW es la referencia directa |
| Argumentos | `-nombre:valor`, varios | pocos (`-c`, `-h`, `-v`, `-s`), el resto en `.conf` | pocos + config (D-9) |
| Consola | REPL mínimo (`help`, `exit`, `gc`, `pid`) | readline + todo el sistema de comandos | REPL con el vocabulario de abajo |
| Cómo ejecuta un comando | en el hilo de la consola | encolado al hilo del mundo | **encolado al hilo de lógica** (como WoW) |
| Remoto | panel web Blazor en el mismo proceso | RA (telnet) y SOAP, apagados por defecto | canal de control local (L-B) |
| Servicio | Docker / `ProcessExit` | systemd / Windows Service (`-s install`) | los dos |

## Principio: el núcleo no sabe que existe una UI

La regla de capas del roadmap (Network no sabe de SQL, Handlers no tocan sockets…) se extiende con
una capa más:

```
  ┌──────────────┐  ┌───────────────────┐  ┌──────────────┐
  │ consola      │  │ GUI Windows (Qt)  │  │ muretctl     │   front-ends (delgados)
  └──────┬───────┘  └─────────┬─────────┘  └──────┬───────┘
         │ en proceso         │ en proceso        │ canal de control (opcional, L-B)
┌────────▼────────────────────▼───────────────────▼───────┐
│ Admin   IServerControl · LogSink · MetricsSnapshot      │   DataServer::Admin
└────────┬────────────────────────────────────────────────┘
         │ comandos encolados / snapshots copiados
┌────────▼────────────────────────────────────────────────┐
│ Network · Dispatch · Handlers · Database (olas 1–8)     │
└─────────────────────────────────────────────────────────┘
```

- **`DataServer::Admin`** es una interfaz chica que el núcleo implementa. Ejemplo de forma (no es
  código para copiar, es para discutir):
  - `Status()` → puerto, uptime, versión, estado de la base.
  - `Sessions()` → los GS conectados (lo que el original loguea al cerrar).
  - `Metrics()` → la foto de las métricas de la sub-ola 8.4 (conexiones, largo de la cola, latencia
    por head, errores de SQL).
  - `Kick(sessionId)`, `ReloadFilters()` (los `badsyntax`/`Monster`/`Skill` de
    [07](../../../Server/DataServer/docs/research/07-datos-auxiliares.md)), `SetLogLevel(level)`,
    `Shutdown()` (el apagado limpio de la 8.4).
  - `Subscribe(LogSink&)` → quien quiera ver el log en vivo (la consola, la GUI) se suscribe; el
    núcleo no sabe quién es.
- **Hilos:** un front-end **nunca** toca el estado del hilo de lógica directamente. Los comandos que
  cambian algo (`Kick`, `ReloadFilters`) se **encolan** y los ejecuta el hilo de lógica, como el
  `QueueCliCommand` de TrinityCore, para no romper los invariantes I-1/I-2/I-3 de la ola 4.
  Ojo con un detalle que TrinityCore no tiene: allá el hilo del mundo da vueltas con un tick fijo y
  vacía la cola de comandos en cada vuelta, pero nuestro hilo de lógica (4.2) está **dormido en el
  `Pop` de la `MessageQueue`**. Si los comandos van en una cola aparte, nadie lo despierta. Hay dos
  salidas, a decidir en la F.1: que la `MessageQueue` lleve `std::variant<Frame, AdminCommand>`
  (con los comandos fuera del tope de back-pressure, para que un `shutdown` nunca se descarte), o
  una segunda cola que comparta el mismo `condition_variable`. Cada comando lleva su callback de
  respuesta, como el `CliCommandHolder`.
- **Lecturas:** lo que solo lee (`Status`, `Metrics`) devuelve una **copia** (snapshot) tomada bajo
  lock o con atómicos, sin pasar por la cola.
- **Log:** el logger seguro entre hilos (C-5, ola 2) escribe a archivo siempre, y además a los
  `LogSink` suscritos. Un ring buffer de N líneas para el que se conecta tarde es la versión
  moderna del de 30 × 0x50 del original. Al `LogSink` lo llaman **los hilos del servidor** (los de
  I/O y el de lógica), así que tiene que ser barato y no bloquear: copia la línea y vuelve. Nunca
  espera a la UI ni vuelve a loguear adentro (con el lock del logger tomado, eso es un deadlock).
- **Un ejecutable headless en los dos sistemas.** `DataServer` (el de `main.cpp`) sigue siendo de
  consola en Windows y en Linux. La GUI de Windows es **otro ejecutable** sobre el mismo núcleo, que
  se suma, no un reemplazo.

Esto además prepara el terreno para cuando entren ConnectServer o JoinServer: el mismo canal y la
misma herramienta sirven para todos (ahí la interfaz pasa a `Server/Common/`).

## Comandos comunes

Las dos plataformas comparten el mismo vocabulario, así se aprende una sola vez:

| Comando | Qué hace | Depende de |
| --- | --- | --- |
| `help` / `?` | lista los comandos | — |
| `status` | puerto, uptime, versión, base conectada o no | ola 2, 6 |
| `sessions` | GS conectados: id, generación, IP, desde cuándo | 2.4 |
| `queue` | largo de la cola y si hay back-pressure | 4.1, 4.3 |
| `metrics` | la foto de métricas de la 8.4 | 8.4 |
| `kick <id>` | cierra esa sesión (ordenado, con log) | 3.3 |
| `reload filters` | relee los archivos de filtros de nombres sin reiniciar | 5 |
| `loglevel <nivel>` | cambia el nivel de log en caliente | 2 |
| `shutdown` / `exit` | apagado limpio, con confirmación como el original y OpenMU | 8.4 |

## Linux: sin interfaz gráfica

En un servidor Linux no hay escritorio: se maneja por argumentos, archivos de config, log y
comandos, como OpenMU y los emuladores de WoW. Dos alternativas, de menor a mayor.

### L-A. Consola interactiva en el mismo proceso (estilo TrinityCore / OpenMU)

```
$ ./DataServer --config /etc/muret/dataserver.toml --port 55960
[2026-10-26 04:50:01] info  DataServer Run (puerto 55960)
DS> sessions
  #3 gen 1  127.0.0.1:51022  conectado hace 00:12:40
DS> shutdown
¿Apagar? (s/N) s
```

- **Pocos argumentos, el resto en la config** (como TrinityCore): `--help`, `--version`,
  `--config <archivo>`, `--port` (validado 1–65535, pisa a la config), `--no-console` (no leer
  stdin). Se mantiene el uso viejo `DataServer 55960 [modo]` posicional para que los scripts del lab
  sigan andando; el `modo` (la mitad de la pantalla donde iba la ventana) se acepta y se ignora con
  un aviso en el log. Las claves de la config se pueden pisar con variables de entorno (útil en
  Docker).
- **Config** en el archivo que decida la ola 2 (TOML o JSON), con `Console.Enable` y compañía.
- **Consola = un productor más de la cola**: un hilo lee la línea, la parsea y la **encola** con su
  callback de respuesta; el hilo de lógica la ejecuta y el callback imprime el resultado.
- **Librería de línea: replxx.** GNU readline es GPL y no anda en la consola de Windows (por eso
  TrinityCore tiene dos caminos); linenoise es solo POSIX. replxx es BSD, anda en Linux y en
  Windows, tiene historial y autocompletado, y resuelve el problema clásico de esta consola: el log
  escribe líneas mientras uno está tipeando. Su `print()` se puede llamar desde otro hilo mientras
  `input()` espera; replxx encola el texto y lo imprime sin romper la línea que se está editando.
  El `LogSink` de la consola escribe por ahí.
- **Señales:** `SIGINT` (Ctrl+C) pide confirmación como OpenMU; `SIGTERM` apaga sin preguntar (es
  lo que manda `systemctl stop` o `docker stop`). Ya está en la sub-ola 8.4.
- **Como servicio:** una unidad de systemd (`Type=simple`, log a stdout que va a `journalctl`,
  `--no-console`). Si se quiere la consola, se corre dentro de `tmux`/`screen` y uno se "adjunta".
- **Pros:** es casi lo que ya está planeado (D-7 + ola 2 + 8.4), costo bajo, se aprende stdin,
  señales y el ciclo de vida de un proceso.
- **Contras:** como servicio no tiene stdin, así que para mandar un comando hay que adjuntarse; no
  se puede automatizar desde otro script.

### L-B. Daemon + acceso remoto (`muretctl`, estilo RA de TrinityCore)

```
$ systemctl start muret-dataserver
$ muretctl status
DataServer 55960  up 03:12:09  db OK  sesiones 2  cola 0/5000
$ muretctl kick 3
$ muretctl shutdown
```

- El servidor corre siempre headless y abre un **canal de control**, **apagado por defecto** como
  el RA y el SOAP de TrinityCore (`Control.Enable = 0`): un Unix domain socket
  (`/run/muret/dataserver.sock`, los permisos del archivo son la seguridad) o TCP en `127.0.0.1`
  con usuario/clave o token de la config, como `Ra.MinLevel`.
- **Protocolo:** una línea por comando y una respuesta, en texto (como RA, se prueba con `nc`) o
  JSON (a decidir). El mismo vocabulario de la tabla de arriba, y **los comandos se encolan igual
  que los de la consola**: un solo camino de ejecución.
- `muretctl` es un ejecutable chico en `tools/muretctl/` que se conecta, manda y muestra, como
  `systemctl` o `redis-cli`.
- **Pros:** headless de verdad, scriptable (cron, monitoreo, CI), y el mismo canal puede usarlo la
  GUI de Windows cuando el server corre como servicio. Es un buen ejercicio de sistemas
  distribuidos: diseñar un protocolo, IPC, autenticación, qué pasa si el cliente se cuelga.
- **Contras:** otra superficie de ataque (por eso apagado, solo local y con auth), y un protocolo
  más para mantener y testear.

L-B **no reemplaza** a L-A: igual que en TrinityCore, consola local y acceso remoto conviven y
comparten los comandos.

*Variante opcional:* una TUI tipo `htop` (por ejemplo con FTXUI) que muestra sesiones, cola y log en
vivo en la terminal. Es un front-end más sobre el mismo canal; solo vale la pena si L-B ya existe.

## Windows: GUI como la del original, con estándares actuales

**Decisión de Emanuel (2026-10-26):** en Windows queda una **GUI como la del original** (una ventana
con el log, el estado y el título con el puerto), pero hecha con estándares actuales, no con MFC.
En Windows el DataServer headless de consola sigue existiendo (L-A anda igual; Ctrl+C con
`SetConsoleCtrlHandler`), y la GUI es la forma "de escritorio" de usarlo.

### Qué conserva del original y qué moderniza

| Original (MFC, 2003) | GUI de MuRet |
| --- | --- |
| Vista de log repintada cada 1 s desde un ring buffer de 30 líneas | log en vivo, con niveles, filtro, búsqueda, colores, copiar y pausar; buffer de miles de líneas |
| Status bar y título `Puerto:%u  Ver : 4.4` | panel de estado: puerto, versión, uptime, base OK/caída, sesiones, largo de la cola (métricas 8.4) |
| Errores de arranque en `MessageBox` que bloquean | errores en la ventana y en el log, sin bloquear; un aviso no modal |
| Cerrar la ventana = apagar sin orden (Q-09) | botón "Apagar" con confirmación y apagado limpio (8.4); cerrar la ventana pregunta si apagar o minimizar a la bandeja |
| Sin comandos | tabla de GS conectados con `kick`, recargar filtros, cambiar nivel de log (los comandos comunes) |
| La UI es el hilo del servidor | la UI corre en su hilo y habla **solo** con `IServerControl`; si se cuelga, el servidor sigue |
| Ventana fija en media pantalla según el "modo" | redimensionable, recuerda posición y tamaño |
| ANSI, DPI fijo, tema clásico | Unicode, **DPI por monitor** (Per-Monitor v2), tema claro/oscuro del sistema, accesible por teclado |

### Dónde vive la GUI

| Opción | Cómo es | Pros | Contras |
| --- | --- | --- | --- |
| **W-1. En el mismo proceso** (como el original; recomendado para empezar) | `DataServerGui.exe` linkea el núcleo y abre una ventana; la UI en su hilo, el núcleo en los suyos, unidos por `IServerControl` y un `LogSink` | se usa como siempre (doble clic y anda); no hace falta protocolo | si la GUI crashea, se cae el proceso; no sirve contra un server que corre como servicio |
| **W-2. App aparte** | `MuRetAdmin.exe` se conecta al canal de control de L-B | el servidor sigue headless; sirve contra un DataServer en Linux o en la VM | necesita L-B primero |

Como la GUI solo habla con `IServerControl`, pasar de W-1 a W-2 después es cambiar la
implementación de esa interfaz (en memoria → por el canal), no reescribir la ventana.

### Con qué se dibuja

| Opción | Qué es | Pros | Contras |
| --- | --- | --- | --- |
| **G-1. Qt 6 Widgets** (estilo `windows11`) | framework completo en C++, multiplataforma | desde Qt 6.7, en Windows 11 los Widgets usan por defecto el estilo `windows11` (diseño Fluent, con modo oscuro), sin escribir QML; DPI alto siempre activo y *Per-Monitor DPI Aware V2* por defecto en Windows; model/view para la tabla de sesiones y el log; signals/slots entre hilos (justo lo que hace falta entre el núcleo y la UI); también anda en Linux con escritorio | pesado, build largo por vcpkg, MOC; licencia LGPLv3 (linkear dinámico); con escalas no enteras (175 %) los Widgets pueden tener detalles gráficos, que se arreglan con la política de redondeo `Round` |
| **G-1b. Qt 6 Quick/QML** (estilo `FluentWinUI3`) | la misma Qt, pero la UI en QML | el estilo `FluentWinUI3` (Qt 6.8+) imita WinUI 3, con temas claro y oscuro; animaciones y layouts más modernos | hay que aprender QML (y algo de JavaScript) además de C++; el estilo **no es nativo**: se dibuja con imágenes y se ve igual en todos los sistemas |
| **G-2. Dear ImGui** (+ DirectX 11/Win32) | UI "modo inmediato" en C++, muy usada en herramientas de juegos | chica, C++ puro, entra por vcpkg, se aprende rápido; ideal para un prototipo o un panel de debug | look de "herramienta", no de app de Windows; DPI a mano (avisarle a Windows con `ImGui_ImplWin32_EnableDpiAwareness()` o un manifest, escalar fuentes con `style.FontScaleDpi` y el resto con `style.ScaleAllSizes()`); accesibilidad casi nula |
| **G-3. WinUI 3** (Windows App SDK, C++/WinRT) | la UI nativa de Windows 11 (Fluent) | el look más "Windows actual" posible | solo Windows; C++/WinRT es difícil; empaquetado MSIX; en C# es más fácil pero es otro lenguaje |
| **G-4. Panel web** (estilo OpenMU) | HTTP chico en el núcleo y una página que consume JSON | desde cualquier navegador y sistema | HTTP, auth web y front-end aparte; no es "una GUI como la de antes" |

## Recomendación

1. **Linux: L-A primero**, el modelo de TrinityCore: pocos argumentos, config, y una consola que
   **encola** comandos al hilo de lógica. Es casi gratis: la ola 2 ya pide argumentos validados y la
   8.4 señales y apagado limpio. Lo nuevo es `IServerControl` y la consola.
2. **Después L-B** (`muretctl` + canal de control apagado por defecto, como RA). Es el que más
   enseña de sistemas distribuidos.
3. **Windows: W-1 + G-1**, una ventana como la del original, en el mismo proceso, hecha con
   **Qt 6 Widgets**: todo en C++, sin QML; en Windows 11 se ve Fluent con tema oscuro sin hacer
   nada (estilo `windows11`), y sus signals/slots resuelven el cruce de hilos núcleo → UI. QML
   (G-1b) queda para cuando se quiera algo más vistoso. Si Qt resulta muy pesado, Dear ImGui
   (G-2) es el plan B, más rápido de arrancar y más feo. Con L-B hecho, la misma ventana puede
   pasar a W-2.
4. WinUI (G-3) y el panel web (G-4) quedan anotados, sin plan.

En todos los casos el núcleo es el mismo y no sabe qué front-end lo está usando.

## Cómo encajaría en olas (borrador)

Una "ola F" opcional, con el mismo ritmo de siempre (una sub-ola = un concepto = un PR chico que
escribe Emanuel). Requiere las olas 4 (hilos y cola) y 8.4 (métricas y apagado).

| Sub-ola | Qué | Concepto | Terminada cuando |
| --- | --- | --- | --- |
| F.1 | `DataServer::Admin`: `IServerControl`, `LogSink`, snapshots; comandos encolados al hilo de lógica (como `QueueCliCommand`) | interfaces, observer, snapshots thread-safe | tests en memoria: un `kick` encolado no rompe I-1/I-2/I-3 (TSan limpio) |
| F.2 | consola L-A (historial, `help`, confirmación) + argumentos + unidad de systemd | parseo de comandos, stdin, servicios | `status`, `sessions`, `shutdown` andan; `systemctl stop` apaga limpio |
| F.3 | GUI de Windows en Qt 6 Widgets en el mismo proceso: log en vivo + panel de estado (solo lectura) | event loop de la UI; cruzar hilos con signals/slots (una señal emitida desde otro hilo llega como *queued connection* y el slot corre en el hilo de la UI; los tipos propios se registran con `qRegisterMetaType`; nunca `BlockingQueuedConnection`, que al apagar se traba); el núcleo no conoce `QObject`: un adaptador de la GUI implementa `LogSink` y emite la señal | la ventana muestra log y métricas mientras 100 clientes de prueba pegan, sin trabarse |
| F.4 | GUI con comandos (el callback de respuesta vuelve a la UI con `QMetaObject::invokeMethod` y `Qt::QueuedConnection`): tabla de GS con `kick`, recargar filtros, apagar con confirmación; bandeja | model/view, comandos asíncronos con respuesta | los comandos de la tabla común andan desde la GUI y desde la consola |
| F.5 | *opcional:* canal de control L-B + `tools/muretctl` (y la GUI como cliente, W-2) | IPC, protocolo, auth, timeouts | `muretctl status` contra un server en Linux y otro en Windows |
| F.6 | *opcional:* Windows Service (`--service install`, como TrinityCore) | SCM, servicio sin consola | el server arranca con Windows y la GUI (W-2) se conecta |

Antes de arrancarla se escribe su plan en `docs/superpowers/plans/` y su guía en `docs/learning/`,
como las demás.

## Estructura en el repo (propuesta)

```
Server/DataServer/src/Admin/       IServerControl, LogSink, CliCommand        DataServer::Admin
Server/DataServer/src/main.cpp     headless: arma el núcleo + consola
Server/DataServer/gui/             DataServerGui.exe (Qt 6): mismo núcleo + ventana
tools/muretctl/                    (F.5) cliente del canal de control
packaging/systemd/                 muret-dataserver.service
```

Para que `main.cpp` y la GUI compartan el núcleo, el CMake del DataServer pasa a tener una
biblioteca (`DataServerCore`) y dos ejecutables encima. Qt entra por vcpkg solo si se pide la GUI:
una *feature* `gui` en `vcpkg.json` (con `qtbase`), y en el `CMakeLists.txt` raíz una opción
`MURET_BUILD_GUI` (apagada por defecto) que hace `list(APPEND VCPKG_MANIFEST_FEATURES "gui")`
**antes** del `project()`, como dice la doc de integración de vcpkg con CMake. Así el CI de Linux no
compila Qt. `replxx` (también tiene port en vcpkg) lo linkea solo el ejecutable de consola, no
`DataServerCore`: el núcleo no depende de ningún front-end. Si entra otro servidor antes, `Admin/`
se muda a `Server/Common/`.

## Preguntas abiertas (para cuando se arranque)

- Qt Widgets (recomendado) o QML con `FluentWinUI3`; se decide con un prototipo chico de cada uno,
  con su ADR (el siguiente libre después de las 0001–0005 de los planes).
- ¿El log de la GUI lee del `LogSink` en vivo, o del archivo de log? (en vivo, con un ring buffer
  para lo anterior, parece lo natural).
- Comandos y frames en la misma `MessageQueue` (`std::variant`) o en una cola aparte con el mismo
  `condition_variable` (ver el principio de arriba).
- Para L-B: transporte (Unix socket / named pipe o TCP local), protocolo (texto o JSON) y auth.
- ¿Cerrar la ventana apaga el servidor o lo manda a la bandeja?
