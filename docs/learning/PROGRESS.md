# Progreso de aprendizaje

Lo leen y actualizan el estilo `Tutor MuRet` y el subagente `muret-reviewer`. El comando `/tutor`
solo lo lee.

## Dónde estamos

- Ola: 0 (Fundaciones)
- Sub-ola: **0.0 resuelta como ejemplo trabajado** (del código al programa), el 2026-11-01. A
  pedido tuyo la resolvió Claude: la misión (`Labs::HeadOffset` en `MuRetLabs`), el jefe (la ODR) y
  el carril R, con las salidas y las respuestas en [`labs/0.0-enlace/README.md`](../../labs/0.0-enlace/README.md).
  Para cerrarla falta lo tuyo: contar con tus palabras las preguntas de la
  [guía](ola-0.0-del-codigo-al-programa.md#preguntas-para-chequearte) y completar su sección final
- La 0.1 está hecha (andamiaje CMake + `Server/DataServer/`); falta compilar en Windows y responder [las preguntas](ola-0.1-cmake.md#te-toca), que se cierran junto con la 0.0
- Próxima del servidor: la 0.2, ahora más chica: vcpkg, GoogleTest, los test presets y el CI ya
  llegaron con la CL0.2 y la CL0.3 del cliente; le quedan los tests del DataServer
- **Cliente (tu foco): CL0.2 y CL0.3 resueltas como ejemplo trabajado** el 2026-11-01, después de
  que eligieras ir directo al cliente: `ClientLib` con la resolución de rutas de `Data\`,
  los tests con la carpeta golden, la ventana SDL3 con OpenGL 3.3 core y el CI en Windows y
  Linux. Para cerrarla: las [preguntas de la CL0](../superpowers/plans/2026-10-30-ola-cl0-fundaciones-cliente.md#para-cerrar-la-cl0-emanuel)
  y correr `Main.exe` en tu Windows. Conceptos para explicar: targets y qué se linkea a quién,
  vcpkg en modo manifiesto y su baseline, loader de GL, tests salteables. Próxima: la CL1
  (formatos sin ventana), con su [plan](../superpowers/plans/2026-11-01-ola-cl1-formatos-sin-ventana.md)
  y la [guía de la CL1.0](ola-cl1.0-bytes-y-archivos.md). El 2026-11-01 pediste seguir con el
  cliente: Claude escribe la CL1 como ejemplo trabajado. Ya están el `Expected` propio
  (`Client/src/Core/Expected.h`) y la CL1.1: `ByteReader`, XOR3, el checksum de las tablas y
  `Text.bmd`, con tests contra el archivo real y contra la memoria del original. También la
  CL1.2: `Gate`, `Item` y `Skill.bmd` sobre una misma `RecordTable<RecordSize>`; `Item` informa su
  checksum y carga igual (Q-C01), `Skill` lo exige, y un archivo corto es un error (Q-C24). La
  parte de `labs/` de la CL1.0 (la `ByteView`, el jefe Q-C07), las actividades 1 a 6 de la guía y
  el checksum de dos dwords a mano (actividad 12) quedan para después, igual que el test opcional
  del comportamiento viejo de Q-C24 (paso 7 de la CL1.2). La CL1.3 (terreno) va en dos PRs; el
  primero (#19) trae K16, `EncTerrain.map` y `EncTerrain.att`, comparados con la memoria del
  original. El segundo, `EncTerrain.obj` y las alturas de `TerrainHeight.OZB`, leídas donde las
  lee el original y no donde dice el BMP (Q-C02). La CL1.4 (texturas) va en tres PRs: los
  volcados de memoria por captura, el OZT, y el OZJ con libjpeg-turbo, que da los mismos bytes que
  la libjpeg 6b del original en los 2.380 JPEG del laboratorio. Fuera de ellos difiere en casos
  que quedan escritos (Q-C33), y el plan de un decodificador propio compatible con la 6b queda en
  pausa por si turbo no alcanza ([su plan](../superpowers/plans/2026-11-01-cl1.4-decodificador-jpeg.md)).
  Con el OZJ entra también la luz del terreno, `TerrainLight.OZJ`, igual a la que tenía en memoria
  el original en Lorencia y Devias. La CL1.5 (`message.wtf`) salió de la ola: es del launcher, y
  `main.exe` no lo lee
  ([05](../../Client/docs/research/05-formatos-de-datos.md#el-pipeline-de-las-dlls)). Con eso el
  código de la CL1 está entero; para cerrarla falta lo tuyo (la parte de `labs/`, las actividades
  de la guía y explicar los conceptos de abajo). Próxima: la CL2 (protocolo sin red), con su
  [plan](../superpowers/plans/2026-11-02-ola-cl2-protocolo-sin-red.md) y la
  [guía de la CL2.0](ola-cl2.0-flujos-y-bloques.md). El 2026-11-02 pediste que Claude la escriba
  entera, también la CL2.0 en `labs/` y los pasos `TODO(human)`, hasta que digas que participás:
  lo que resuelva Claude queda marcado acá como resuelto por Claude. **La CL2.0 la resolvió
  Claude** el 2026-11-02: el `FrameCutter` de a un byte, el `BitWriter` y el `BitReader` MSB
  primero, el inverso modular con Euclides extendido y el jefe, un modelo del corte del original
  que lee bytes viejos (Q-C03), con las respuestas de las 14 actividades en
  [`labs/cl2.0-flujos/README.md`](../../labs/cl2.0-flujos/README.md). Con ella llegó GoogleTest a
  `labs/`. Para cerrarla falta lo tuyo: las preguntas y la sección final de la guía. **La CL2.1
  también la escribió Claude**: el `ByteReader` pasó a `Core`, con `ReadU16Be`, y llegó el corte
  de `Network`. `Packet` valida el tipo y el tamaño al armarse, y `PacketReader` espera los bytes
  que faltan donde el original lee bytes viejos (Q-C03), y se para con un error ante un tipo
  inválido, un tamaño que no llega al opcode o uno de más de `0x2000` (Q-C38, Q-C39 y Q-C40). Se
  prueba contra el cable del laboratorio: los 689 paquetes que mandó el cliente y los 144 que
  contestó el ConnectServer salen enteros y sin bytes de más. **La CL2.2, también**:
  `PacketWriter` arma un C1 o un C2 campo por campo y rechaza lo que no entra en vez de truncarlo
  (Q-C06), y el XOR32 del motor de envío va aparte, con el opcode en claro también en un C2, como
  en el binario y no como en `mu-client`.
  **La CL2.3, también**: los bloques del SimpleModulus y los archivos de clave, con una clave de
  cifrar y una de descifrar que son tipos distintos, y un módulo que no sirve rechazado al cargar
  (Q-C41). Contra las claves reales, cada par del cliente y del servidor es inverso. Encima,
  `EncryptPacket` y `DecryptPacket` arman y rearman los C3/C4 con sus seriales: el serial que no
  coincide se resincroniza (Q-C04), el C4 más grande se descifra entero (Q-C05), y el paquete
  rearmado declara lo que mide (Q-C10). Se descifran los 183 C3 que mandó el cliente original y
  los 25 C3/C4 que contestó el GameServer, con el serial en orden en cada conexión, y
  `EncryptPacket` vuelve a cifrar los 183 como el original salvo el último bloque, donde el
  original deja bytes de su pila.
  **La CL2.4, también**: la capa de paquetes de FoxAnti como una interfaz, `IObfuscation`, con dos
  implementaciones: la que no toca nada y la del servidor del laboratorio. El `PacketReader`
  desenmascara el `0xAA` por paquete (Q-C18), la lista y el `F3 11` del laboratorio pasan a la
  forma de `main.exe`, y los remapeos y la escalera del código de servidor dan byte por byte lo
  mismo que la FoxAnti mínima, también donde el original pierde los códigos desde 21 (Q-C17). Lo
  que el original no mide es `InvalidPacket` (Q-C09). Todo lo que contestaron los dos servidores en
  el cable se corta entero, 3.033 paquetes, y lo del GameServer llega hasta el dispatcher.
  **La CL2.5, también**: los paquetes del login, leídos y escritos campo por campo con
  `ByteReader` y `PacketWriter`, sin `reinterpret_cast` ni `#pragma pack`. Tu paso 1, decodificar
  a mano el saludo `F1 00`, lo resolvió Claude: la respuesta está en el comentario de
  `HelloTest.ReadsTheHelloDecodedByHand`. La versión y el serial del build están en un solo lugar,
  `Protocol/BuildConstants.h`, y el XOR3 pasó de `Data::Tables` a `Core`, porque lo usa también la
  cuenta del login. Contra el cable salen los 7 login y los 156 keepalive que mandó el cliente, y
  los 8 saludos y los 7 login aceptados del GameServer. Los dos WORD del keepalive, que el plan
  llamaba `x` e `y`, son la velocidad de ataque y la de magia: así los compara el GS.
  Próxima: la CL3 (plataforma y bucle), con su
  [plan](../superpowers/plans/2026-11-02-ola-cl3-plataforma-y-bucle.md) y la [guía de la
  CL3.0](ola-cl3.0-el-bucle-de-juego.md). **La CL3.0 la resolvió Claude** el 2026-11-02, como la
  CL2.0: el `FixedStep` en ms enteros (y uno con `std::chrono::nanoseconds` para comparar), la
  `TickGrid` que lee como `GetTickCount` y el jefe, un modelo del bucle de `0x637700` que da 64
  frames cada 3 s y 75 pasos cada 64 frames, y que con pasos lentos se frena en vez de espiralar. El
  carril R encontró que el acumulador del binario arranca en 40, y el jefe arranca ahí. Las
  respuestas de las 13 actividades están en
  [`labs/cl3.0-bucle/README.md`](../../labs/cl3.0-bucle/README.md). Para cerrarla falta lo tuyo: las
  preguntas y la sección final de la guía.
  **La CL3.R**, el carril R antes de desacoplar, la escribió Claude: el bucle de `0x637700` pasó
  al 02 (con signo, sin tope, medido desde `0x63852b`, y en 40 al arrancar), y una tabla dice qué
  corre por paso, qué por frame y qué por reloj. El teclado se lee casi todo en el paso y pierde
  un apretón corto (Q-C45), y sin foco el original no corre el frame (Q-C46); "7 pasos cada 6
  frames" pasó a 75 cada 64, con 11 de 2. **La CL3.1 también la escribió Claude**: `Platform`,
  en su propio target (`ClientPlatform`), es lo único que ve SDL3. `SdlContext` y `Window` son
  RAII sobre la API de C, con el contexto OpenGL 3.3 core que muere antes que la ventana; el
  loader de GL y el intervalo de swap salieron de `main.cpp` y se prueban sin ventana, en los dos
  sistemas. `App::WindowState` lleva cerrar, minimizar o tapar, el foco y el tamaño, con un
  `Apply` puro que ignora el 0 × 0 que manda Windows al minimizar. Tu paso 1, la tabla de
  `Apply`, lo resolvió Claude: está en el plan.
  **La CL3.2, también**, en dos PRs. El primero trae el bucle de MuRet: `GameLoop` con el reloj
  inyectado (`Core::IClock`), el de SDL (`SdlClock`) en `Main` y un `FakeClock` en los tests,
  donde un frame tarda lo que dice el test. La lógica corre a 25 pasos por segundo a 1, 25, 60,
  144 y 240 FPS, un frame de 130 ms se pone al día con 3 pasos, y el dibujo se interpola con un
  alfa que nunca llega a 1. Un frame de más de 1 s (un breakpoint) cuenta 1 s, con un aviso en
  el log (Q-C44); un reloj que vuelve no corre pasos, y minimizada la lógica sigue y el bucle
  duerme hasta el próximo paso. El segundo trae el modo fiel, `FaithfulPacer`: el bucle del jefe
  de la CL3.0 sobre el reloj del cliente, con el acumulador en 40, sin tope y midiendo desde
  después de los pasos. Da 64 frames cada 3 s y 75 pasos cada 64 frames, y con pasos de 10 ms, 71
  pasos y no 74. `Main` elige con
  `--pacing`, `--vsync` y `--stats`. Tu paso 1, los 7 frames del modo fiel a mano, lo resolvió
  Claude; la prueba a mano en Windows (paso 7) queda para vos.
  **La CL3.3, también**: `Input`, sin SDL, arma el teclado y el mouse con los eventos. "Está
  abajo" (`IsDown`) se consulta y "se apretó" (`WasPressed`) es un flanco, que vale hasta el
  próximo `ConsumeEdges`: `GameLoop` lo llama después del primer paso del frame, así un apretón
  lo ve un solo paso, también uno apretado y soltado en el mismo frame (el que el original pierde,
  Q-C45), y en un frame sin pasos espera al siguiente. Shift, Ctrl y Alt son lógicas, como el
  `VK_SHIFT` del original: están abajo si lo está cualquiera de sus dos lados. Perder el foco
  suelta todo, y `Platform` traduce los scancodes y descarta el auto-repeat. Tu paso 1, la tabla
  de flancos, lo resolvió Claude: está en el plan.
  **La CL3.4, también**, en dos PRs. El primero: `muret-client.ini` (o `--config`) y la línea de
  comandos, `connect /u<ip> /p<puerto>` (Q-C36 y Q-C37) y `--data`, se juntan en `Resolve` y se
  validan antes de abrir nada: un puerto malo o una carpeta de datos que no existe terminan con
  un error que los nombra, sin ventana (Q-C35), y sin config ni argumentos `Main --frames 60`
  sigue andando. El parser del INI es propio y no `inih`, con la razón en el header: el puerto de
  vcpkg fija el largo de la línea en 200 al compilar, y un `Host` de 255 bytes no entra.
  Tu paso 2, la tabla de la línea de comandos, lo resolvió Claude. El segundo: `Core::Log`,
  propio y no spdlog, sin globales, con cuatro niveles y `std::format`, escribe con un `TeeSink` a
  `stderr` y a `muret-client.log`. El mutex va en cada salida y no en el `Log`, porque SDL loguea
  desde sus hilos, y el `TeeSink` tiene el suyo para que las dos reciban las líneas en el mismo
  orden. El archivo agrega al final, como el `MuError.log` del original, y al abrir se vacía si
  pasa de 1 MiB (el original corta el principio, Q-C48). `SdlLog` le pasa los mensajes de SDL,
  con un RAII que devuelve la salida de SDL antes de que muera el `Log`, y `Main` arma todo y
  avisa cuando un frame pasa de 1 s.
  Con eso el código de la CL3 está entero; para cerrarla falta lo tuyo: correr `Main` en tu
  Windows ([README del cliente](../../Client/README.md#la-prueba-a-mano-en-windows)), las
  preguntas y la sección final de la [guía de la CL3.0](ola-cl3.0-el-bucle-de-juego.md), y
  explicar los conceptos de abajo. Próxima: la CL4 (red del cliente), con su
  [plan](../superpowers/plans/2026-11-02-ola-cl4-red-del-cliente.md).
  **La CL4.1 la escribió Claude**, en tres PRs. El primero trae el socket: `ISocket`, con
  `SystemSocket` sobre `SocketApi`, que tiene el único `#ifdef _WIN32` de `ClientLib` (en el
  `.cpp`) y en su header la razón de §10 (SDL3_net traería SDL3 a `ClientLib`, y asio cambia el
  modelo a handlers), y un `FakeSocket` que sigue un guion. El connect no espera y guarda su
  resultado, porque `SO_ERROR` se borra al leerlo; un `recv` en 0 y un RST son un cierre del otro
  lado, y en Linux un `send` a una conexión cerrada no mata el proceso (`MSG_NOSIGNAL`). Se prueba
  con sockets de verdad en `127.0.0.1` contra un `LoopbackServer`, en un solo hilo. Tu paso 2, la
  tabla de lo que da cada llamada en Linux y en Windows, lo resolvió Claude: está en el plan.
  El segundo trae la `SendQueue`: lo que el socket no tomó, en un buffer fijo de `0x2000` como el
  del original, con un cursor, así un envío partido en `{2, 2, -1}` no repite ni pierde bytes
  (Q-C11) y lo nuevo espera detrás de lo pendiente; lo que no entra es `Full`, y la conexión va a
  cerrar como el original.
  El tercero trae el `Poll` del bucle: `ILoopClient` suma `Poll()`, que `GameLoop::RunFrame` llama
  una vez por frame, antes de los pasos y también minimizada. En el modo fiel va antes de
  `BeginFrame`, fuera de la medición, como el bucle de mensajes del original: con polls de 20 ms el
  juego se frena a 48 frames y 56 pasos en 3 s (`GameLoopTest.FaithfulDoesNotMeasureThePoll`). La
  escena de prueba tiene un `Poll()` vacío. Al arrastrar la ventana en Windows, el original sigue
  con el keepalive y MuRet no consulta la red hasta soltarla (Q-C49).
  **La CL4.R** la escribió Claude, en el binario: la conexión del original pasó al research (03, 04,
  02). El `A9` es el pedido de la información de una mascota, no del anticheat, y el del CS se
  reproduce en la CL4.3; el `connect` espera, rechaza `127.0.0.1` (Q-C50) y toma como conectado un
  nombre que no resuelve (Q-C51); cada cierre corta con un RST (Q-C54); el envío en línea se
  adelanta a lo encolado (Q-C52) y Q-C11 tiene sus bytes (basta un parcial, como halló el jefe de la
  CL4.0); el timer de 20 s manda sin mirar el login (Q-C53, visto en el cable), y sin personaje las
  velocidades salen en 0. P-C11 quedó cerrado, y P-C04 pasa a la CL5.R.
- **CL4.2 (el `Dispatcher`), resuelta por Claude** el 2026-11-02: una tabla opcode → handler, con
  un segundo nivel por subcódigo (el byte que sigue al opcode, `pkt[3]` en un C1 y `pkt[4]` en un
  C2), en vez del `switch` de 10,5 KB de `0x4e2b10`. Lo que no tiene handler da `Unknown` y una
  línea de aviso la primera vez de cada opcode, o de cada opcode y subcódigo, sin los bytes; un
  C3 o un opcode con subcódigos que termina en el opcode dan `Malformed` (el original lee el byte
  que no llegó, Q-C42). Registrar dos veces da `false` y no pisa. Tu paso 1, la tabla de qué lee
  cada uno, lo resolvió Claude: está en el plan.
- **CL4.0 (sockets no bloqueantes), resuelta por Claude** el 2026-11-02, como la CL3.0: el
  `LabSocket` con RAII y todos los `#ifdef _WIN32` en un archivo, el `LoopbackPair` que conecta en
  `127.0.0.1` sin bloquear y en un solo hilo, y la `DrainQueue`, la cola con cursor. El puerto
  cerrado mostró que `SO_ERROR` se borra al leerlo: un segundo `CheckConnect` después del rechazo da
  conectado. El jefe es el drenaje de `FD_WRITE` de `0x4e87b0` (Q-C11): con `{2, 2, WSAEWOULDBLOCK}`
  repite `00..03` y pierde `06..09`, como dice el plan, y además basta un parcial, y después de uno
  ese `FD_WRITE` ya no llega a 0 pendiente. Las respuestas de las 12 actividades están en
  [`labs/cl4.0-sockets/README.md`](../../labs/cl4.0-sockets/README.md), y la comparación con hilos,
  IOCP, `select` y `poll`, en [`labs/README.md`](../../labs/README.md). Para cerrarla falta lo tuyo:
  las preguntas y la sección final de la [guía de la CL4.0](ola-cl4.0-sockets-no-bloqueantes.md).
- **La CL4.3 la escribe Claude**, en dos PRs. El primero trae la `Connection`: el camino de un
  paquete en claro por una conexión, en el orden del original. Al recibir, el socket, el corte con
  la máscara de la ofuscación, `AfterCut` y `BeforeDispatch`; al mandar, `BeforeSend` y la
  `SendQueue`, sin adelantarse nunca a lo pendiente. Lo que pasa en un frame sale como eventos, en
  orden (`Opened`, los paquetes y al final `Closed` con su motivo), porque Network no despacha. Un
  error del corte cierra, porque se perdió dónde empieza el siguiente, y un paquete que la
  ofuscación no traduce se descarta y la conexión sigue. Volver a abrir vacía el corte y la cola:
  nada del ConnectServer llega al GameServer. Si resolver el host tarda más de 100 ms, va al log.
  El cifrado llega en la CL4.4.
  El segundo trae la `ServerSession` con el ConnectServer: al abrirlo manda el `A9`, que la CL4.R
  identificó como el pedido de la información de una mascota (`WritePetInfoRequest`); con el
  saludo pide la lista, elige el primer servidor y, con la dirección, cierra el CS y conecta al
  GameServer. Cada estado espera un paquete, y uno fuera de turno se ignora con un aviso; un
  cierre es `Failed`. Hay un `Dispatcher` por servidor, porque el `00` es el saludo en uno y el
  chat en el otro, y lo que quedaba del lote del CS no se despacha. La config suma
  `[Server] Obfuscation` (`foxanti` o `none`). Tu paso 1, el diagrama de estados, lo resolvió
  Claude: está en el plan.
- **La CL4.4 la escribe Claude**, en tres PRs. El primero trae el cifrado en la `Connection`: las
  claves son de cada conexión (`Open` las recibe; la del ConnectServer no tiene) y los dos
  seriales vuelven a 0 en cada `Open`. Lo que va `Encrypted` sale como C3/C4 con el serial que
  sigue y un byte de relleno de un `std::mt19937` con semilla, después del `BeforeEncrypt` de la
  ofuscación; si no se puede cifrar, no sale y no gasta serial. Un C3/C4 que llega se descifra
  después de `AfterCut` y antes de `BeforeDispatch`; si no descifra, o si la conexión no tiene
  claves, sale como `Undecryptable` y se espera el serial siguiente. Un serial desfasado se anota
  y se sigue desde el que trajo. Contra el cable del laboratorio, con las claves reales: los C3/C4
  del GameServer salen descifrados, y los del cliente, mandados otra vez, salen con sus bytes
  menos el relleno. Tu paso 1, los seriales a mano, lo resolvió Claude: está en el plan.
  El segundo lleva la `ServerSession` hasta `LoggedIn`: abre el GameServer con sus claves y espera
  su saludo; con el resultado y la versión bien, manda el login cifrado con la cuenta, la clave y
  el tick del reloj, y con el resultado del login queda en `LoggedIn` o falla. Un saludo
  rechazado, otra versión (las dos van al log) o un login rechazado cierran el GS. Un C3/C4 con el
  checksum malo o que no mide bloques enteros se contesta con el `F1 03`. La cuenta y la clave
  salen de `MURET_CLIENT_ACCOUNT` y `MURET_CLIENT_PASSWORD` (`ReadCredentials`), y las claves de
  `Enc1.dat` y `Dec2.dat` de la carpeta de datos (`LoadConnectionKeys`): `Main` las lee solo con
  un host, antes de la ventana, y cada error dice qué falta sin traer un valor. La sesión entra
  al bucle en la CL4.5.
  El tercero cierra la CL4.4: un ConnectServer y un GameServer falsos (`FakeServers`), con sockets
  de verdad en `127.0.0.1` y los bytes del cable del 04, y un test que corre `SystemSocket`,
  `Connection` con FoxAnti y la sesión desde el `Start` hasta `LoggedIn`, en un hilo y con un tope
  de 5 s reales. Lo que no está en el guion, el servidor falso lo dice en palabras y corta.
- **CL4.5 (keepalive y desconexión), resuelta por Claude** el 2026-11-02, en un PR. Un
  `IntervalTimer` en `Core`, por reloj y no por pasos: el siguiente vence a un periodo del
  anterior, sin deriva, y con un frame largo vence una sola vez, sin ráfaga (mirado un periodo
  tarde o más, el siguiente cuenta desde ahí; mirado menos tarde, sigue en su grilla). La
  sesión manda el primer keepalive en el mismo `Poll` que lee el login aceptado y después cada
  20 s, solo en `LoggedIn` y también minimizada; sin personaje, las velocidades van en 0 y 0, sin
  ajuste. Un cierre, una falla o un `Start` lo detienen, y la sesión no se reconecta sola. Cada
  `Waiting*` tiene un tope de 30 s (`ReplyTimeout`, falla `NoReply`), mirado después de leer lo
  que llegó: un servidor que acepta y no contesta ya no deja la sesión colgada. Con un host,
  `Main` arma el socket, la conexión con la semilla de `std::random_device` y la sesión, y el
  bucle la consulta antes que la escena (`SessionClient`); el log dice cada estado y cada
  keepalive. Sin host no cambia nada. Tu paso 1, los instantes de los keepalives con y sin
  frames, lo resolvió Claude: está en el plan. El paso 4, el login contra el laboratorio, está
  escrito en el README y falta correrlo.
- Documentación lista: el [roadmap](../superpowers/specs/2026-10-25-dataserver-port-design.md) (qué hace MuRet, las olas 0 a 8 y la tabla de las 9 a 14), el [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md) (cómo se aprende), el [research del original](../../Server/DataServer/docs/research/) (01..10, qué hace el binario byte a byte), los [planes por ola](../superpowers/plans/) y una [guía de estudio por ola](README.md)
- De la 0.3 ya están `docs/CODING_RULES.md`, `AGENTS.md` y `.clang-format`, adaptados de MuMain; quedan los ADR 0001 (tuyo) y 0002
- **Cliente, la CL5 (render 2D y login), al lado de la CL4:** su
  [plan](../superpowers/plans/2026-11-02-ola-cl5-render-2d-y-login.md) y la [guía de la
  CL5.0](ola-cl5.0-el-pipeline-grafico.md). **La CL5.0 la resolvió Claude** el 2026-11-02, como la
  CL3.0: el quad de `0x621b60` con la y dada vuelta (`OriginalQuad`), que escala multiplicando por
  la inversa en float, como el binario, y saca el borde de abajo de la resta sin redondear; la
  matriz de `gluOrtho2D` a mano, igual bit a bit a `glm::ortho` (`Ortho2D`); de fan a triángulos
  (`FanToTriangles`); el jefe, el NDC del original y el viewport de `0x620db0`; y en `gl/` el quad
  dibujado en un framebuffer de 64 × 64 con una ventana escondida, la mezcla y
  `GL_UNPACK_ALIGNMENT`, que en CI corren en Xvfb con `MURET_CLIENT_REQUIRE_GL=1`. Con ella
  llegaron glm a `vcpkg.json` y ese paso del CI. Las respuestas, en
  [`labs/cl5.0-pipeline/README.md`](../../labs/cl5.0-pipeline/README.md). Para cerrarla falta lo
  tuyo: las preguntas y la sección final de la guía, y explicar los conceptos de abajo.
  **La CL5.R** la escribió Claude, en el binario: la carga y el login pasaron al research
  (06, 02, 05). Las escenas las escriben ocho `mov` con números fijos, y la tabla de transiciones es
  la que prueba la CL5.4; la carga se diseña en 800 × 600 y dibuja 11 frames mientras carga; cada
  vuelta al login carga todo otra vez. Las siete ventanas de la UI nueva quedaron identificadas,
  salvo tres; la lista ubica los grupos por una tabla y se corta en el primer lugar vacío (Q-C62);
  los campos de la cuenta y la clave son controles `EDIT` de Windows, de 10 caracteres, y la ventana
  sale con el GS ya conectado; cada resultado del `F1 01` tiene su aviso y su texto por índice, y el
  OK del aviso de la conexión que falló cierra el juego (Q-C69). Los mapas van del 0 al 255 sin
  mirar el rango, y el 32 deja el juego en el 9 (Q-C58). Quirks nuevos de Q-C57 a Q-C70; P-C04 quedó
  cerrado, y P-C13 espera la traza y las capturas de estas dos escenas.
  **La CL5.1** la resuelve Claude en dos PRs. El A (2026-11-03): la tabla de GL es una X-macro de
  46 funciones (`Platform/GlFunctionList.h`), y `LoadGlFunctions` mira la versión y el perfil antes
  de pedir el resto. `ClientRender`, la única biblioteca con GL, trae el shader del 2D, `GlTexture`
  (el OZJ y el OZT de la CL1 con su filtro y su wrap, con `GL_UNPACK_ALIGNMENT` en 1 y vuelto a como
  estaba), un framebuffer para leer lo dibujado y `GlRenderer`, que ya dibuja triángulos con la y
  para arriba. Los tests de `render_gl/` abren una ventana escondida (`GlTestContext`) y en CI
  corren en Xvfb. El lab de la CL5.0 dejó sus copias y pide con el loader del cliente. `GL_CLAMP` no
  existe en core: va `GL_CLAMP_TO_EDGE`, y la diferencia en el borde es Q-C73. El B: un juego de
  texturas es un dato (`Scenes/TextureSets.h`: las 18 del login y las 6 de la carga, con su slot y
  su muestreo), y `LoadTextureSet` lo lee del disco como el original, cortando el nombre en el
  primer punto y poniéndole la extensión del cargador (Q-C74, sin punto). El golden lee las 24 del
  `Data\` del laboratorio. `Gl::TextureTable` es la tabla de la escena, por el slot del original,
  detrás de `ITextureStore`; y `Core::Expected` tiene su `void`.
  **La CL5.2** la resolvió Claude (2026-11-03), con el paso 1, la tabla de los quads del login
  calculada a mano, **resuelto por Claude**. Las cuentas del original están en `ClientLib`, sin GL:
  `ScreenScale`, las esquinas de `0x621b60` y de la arena (`0x622000`) y la imagen de la UI nueva
  (`UiImage`, la de la carga y las ventanas, con la y desde arriba y las UV a medio téxel). El
  `SpriteBatch` las junta en triángulos y tramos, sin reordenar, y guarda el estado de GL como lo
  deja el original: el color, la mezcla, la textura prendida y el alpha test, que el quad sin
  textura apaga hasta la próxima mezcla (Q-C75, nuevo). `GlSpriteRenderer` sube los vértices una
  vez por frame y dibuja cada tramo con su mezcla, su textura o ninguna, y el alpha test como un
  `discard`; un slot que falta se avisa una vez. El lab de la CL5.0 usa ahora este código.
  **La CL5.3** la resuelve Claude en dos PRs, con el paso 2, la tabla de `ParseInlineColors`,
  **resuelto por Claude**. El A (2026-11-03): el texto que no ve GL, en `Render/Text/`. Las cuatro
  fuentes de `WinMain`, con el paso de la letra por escalones de ancho (15 desde 1280, Q-C59); los
  colores `0x02` de `0x4667a0`, que anota hasta nueve tramos pero saca todos los pares; dónde va un
  texto del login, con la división sin signo del original y la y con el alto (Q-C60), y cómo se
  corta en cajas de 256 columnas; y `FreeTypeRasterizer`, FreeType en un bit con el intérprete v35
  y la celda de la OS/2, detrás de `IGlyphRasterizer`. La fuente sale de `[Video] FontFile` (y de
  `BoldFontFile`), y en los tests, de `MURET_CLIENT_FONT_FILE`. `Text.bmd` es de un byte por letra y
  se lee como Latin-1 (Q-C67); lo que se aparta de GDI es Q-C76, nuevo. Con la revisión, una letra
  sin área ya no hace fallar `Open` con la negrita simulada, y un texto que empieza con un salto de
  línea queda con su fondo solo, como en `0x467060`. El B (2026-11-03): la caché y el dibujo.
  `TextCache` rasteriza un texto una vez por fuente y colores, sube sus cajas de 256 × 32 a los
  slots del texto, 256 después de los 1.845 del original en la `TextureTable`, y saca el que hace
  más tiempo que no se dibuja cuando no hay lugar, salvo uno que ya está en el lote del frame; los
  quads son los de `TextPieces`, con el slot de cada caja. La fuente sin `[Video] FontFile`:
  `App::ChooseFontFiles` y, en Windows, `Platform::FindSystemFontFile` con la lista del registro.
  Dos bugs nuevos del original, corregidos: `0x4667a0` reescribe el texto en el buffer del que llama
  (Q-C77), y la caja que pasa de la columna 1.280 del DIB lee la fila de abajo (Q-C78).
  **La CL5.4** la resuelve Claude en dos PRs, con el paso 1, la tabla de transiciones y la de
  `WorldFolderNumber`, **resuelto por Claude**. El A (2026-11-03): la carpeta de cada mapa es un
  dato (`Data::WorldFolder`, con `OriginalMaps`, los 39 mapas que llegan a `0x61a560`), con los
  tres rangos de evento, el 32 en la carpeta del 9 y el 255 en `World256`; el golden ve que los 20
  mundos del laboratorio son la carpeta de algún mapa y que solo falta la del 35. Las escenas son
  objetos (`IScene`: entrar, paso, dibujo, salir y a cuál ir), y el `SceneManager` cambia solo por
  una de las ocho transiciones del original (`SceneTransitions.h`): otra es un error en el log, una
  vez mientras se pida seguido. Sale la de antes y entra la nueva, que la fábrica arma de cero; un
  `Enter` que falla deja el error para que `main.cpp` cierre, y un segundo `Start` no pisa la escena
  de ahora (lo encontró la revisión). La red corre en cada `Poll`, también minimizada. Q-C79,
  nuevo: el `F1 02` cambia de escena sin mirar en cuál está. El B (2026-11-03): las escenas de
  verdad y `main.cpp`. Con `--data`, `Main` lee `Text.bmd` y elige la fuente antes de la ventana, y
  corre la carga (`LoadingScene`, su UI de 800 × 600 y la barra, un avance por paso), el login
  (`LoginScene`: entra con sus 18 texturas y la sesión; dibuja los fondos, el fin del 2D, el pase
  3D, que no dibuja nada hasta la CL7, la arena con el reloj del frame y los tres textos, con la
  versión de MuRet) y una selección vacía, adonde lleva el login aceptado. Cada escena lo pide
  todo por un `SceneCanvas` (el lote, el texto, el mundo y el fin del 2D), y lo da el
  `Gl::GlFrameTarget`, o el `RecordingFrameTarget` de los tests, que anota cada quad, texto y pase
  en orden. Las fuentes siguen el ancho de la pantalla (`SceneText`). El `SessionClient` se fue:
  la sesión la corre el `SceneManager`. Q-C80, nuevo: los textos del login salen con la mezcla
  aditiva de la arena.
  **La CL5.5** la resuelve Claude en dos PRs, con el paso 2, la tabla del `TextField`, **resuelto
  por Claude**. El A (2026-11-03): la UI del login, sin red. El texto tecleado llega aparte de las
  teclas (`SDL_EVENT_TEXT_INPUT`, `InputState::Text()`), y el mouse de la UI va en píxeles del
  framebuffer, acotado a la ventana como en `0x640940` (Q-C47, decidido). Las ventanas son las de la
  UI nueva del original, en píxeles y centradas (D-C11, corregido): el `TextField` de 10 bytes de
  ASCII imprimible, con la clave en asteriscos; el `Button`, con sus cuadros, su captura y su
  rótulo; la lista de servidores, con los 21 grupos en el orden de `0x4c1860` y 20 botones de
  servidor (Q-C62 y Q-C63); la cuenta, con el foco, Tab, Enter y Esc; y el aviso, con los textos
  de 06 por índice. Todo se atiende en el paso y no en el frame (Q-C81, nuevo), y un clic en un
  grupo no vuelve a pedir la lista (Q-C82, nuevo). La cuenta y la clave no salen en ningún
  `PrintTo` ni en el log. El B (2026-11-03): el login entero. La `ServerSession` espera al jugador
  en dos estados nuevos, `ChoosingServer` y `WaitingCredentials`, sin tope ni keepalive, e ignora
  con un aviso lo que llega fuera de turno; un servidor ocupado vuelve a pedir la lista (Q-C71), y
  un login rechazado vuelve a la cuenta por la misma conexión, con su resultado para el aviso. La
  `LoginScene` mira el estado en cada paso, antes y después de atender la ventana de arriba, y
  abre la lista, la cuenta o el aviso cuando cambia, y pide el texto tecleado de SDL solo con la
  cuenta abierta; el OK de los avisos de una conexión que falló cierra el juego, como el original
  (Q-C69). Lo que la CL4 hacía sola quedó en `--auto-login` (`AutoLogin`), una vez por vuelta para
  no repetir un rechazo. Sin la VM del laboratorio, el login se miró en Xvfb contra un CS y un GS
  falsos, con capturas fuera de Git; contra el laboratorio queda el paso 7.
- **Cliente, la CL6 (modelos y selección):** su
  [plan](../superpowers/plans/2026-11-03-ola-cl6-modelos-y-seleccion.md) y la [guía de la
  CL6.0](ola-cl6.0-huesos-y-cuaterniones.md). **La CL6.0 la resolvió Claude** el 2026-11-03, como la
  CL5.0: `AngleQuaternion` (`0x5fcd20`) y `QuaternionMatrix` (`0x5fcdb0`) contra glm, con los senos
  redondeados a float y los cosenos no, como los deja la x87; el slerp de `0x5fce90`, con la vuelta
  de q, la primera comparación con el cos de la pila y la segunda con el redondeado, y la rama de
  los opuestos, que con cuaterniones de largo 1 no corre (sí con un NaN o un infinito); la cadena
  de huesos con `Concat` (`0x5fca90`), `AngleMatrix` en grados (`0x5fc8f0`) y el skinning rígido;
  y el jefe: el slerp contra los ángulos interpolados y el lerp normalizado, y en qué key está el
  original (la tabla del 06 va una key atrás). Son 23 tests, sin GL. Las respuestas, en
  [`labs/cl6.0-huesos/README.md`](../../labs/cl6.0-huesos/README.md), y la comparación, en
  [`labs/README.md`](../../labs/README.md). Para cerrarla falta lo tuyo: las preguntas y la sección
  final de la guía, y explicar los conceptos de abajo.
  **La CL6.R** la escribió Claude, en el binario y con dos lectores en paralelo: los modelos, la
  selección y el logo del login pasaron al research (05, 06, 04, 02, 07, 08 y 09). El lector de
  `.bmd` es `0x4f13d0` (`0x4f1ac0` escribe), y un script que lee como él recorre los 1.962 modelos
  del laboratorio sin bytes de sobra; un modelo que falta cierra el juego solo si se llama
  `Monster`, `Player` o `PlayerTest`, así que `Monster50`, que falta, cierra el original cuando
  aparece (Q-C23). La malla la dibuja `0x4ecc30` (`0x4ed890` es código muerto), y cuántos pases de
  brillo lleva una parte lo decide su nivel (`0x60b3d0`). El asignador de los slots de textura,
  simulado con los modelos del laboratorio, da uno por uno los 750 de `lor_slots.bin`; busca por el
  nombre sin la carpeta (Q-C90), y `Logo03` va detrás del mundo 55 (Q-C92). La selección quedó leída
  entera: el picking con un segmento contra una caja corrida, el doble clic con un reloj propio, la
  lista y el equipo campo por campo, y los `F3` 0 a 3 por largo, tipo y subcódigo. Quirks nuevos de
  Q-C86 a Q-C105, y cambiaron Q-C21, Q-C23, Q-C29 y Q-C74; P-C01 y P-C06 quedaron cerrados, y P-C14
  a P-C16 esperan capturas.
  **La CL6.1 A** (2026-11-03, resuelta por Claude): el `.bmd` de un modelo como datos, en
  `Data/Models/`. `LoadBmd` lee como `0x4f13d0`: no mira el `"BMD"`, descifra con K16 el bloque de
  `+8` solo en la versión `0x0C` y lee en claro las otras, y a diferencia del original corta con
  `FileTooShort` lo que no entra y con `InvalidData` una cantidad negativa. `ModelFileName` arma el
  nombre como `0x60da30` (`"0-2"` con -2), `IsRequiredModel` compara byte a byte los tres que cierran
  el juego, y `LoadModelFile` busca sin mayúsculas, lee con un tope de 8 MiB y no deja pasar un
  `Player.bmd` con mallas (Q-C88). Los golden recorren los 1.962 modelos del laboratorio sin un
  byte de sobra, y encontraron lo que el plan no esperaba: en `NPC\BloodCastle01.bmd` dos huesos
  cuelgan de uno que va después, y la pose del original los arma con una matriz vieja (Q-C108,
  nuevo). 28 tests y 14 mutantes a mano. La revisión adversarial encontró que un `.bmd` chico
  podía pedir gigas de memoria, porque cada hueso lleva algo por acción aunque la acción no ocupe
  bytes: ahora `LoadBmd` tiene un tope de huesos por acciones, con dos tests más. **La CL6.1 B**
  (2026-11-03, resuelta por Claude): los modelos que piden el personaje (213) y los ítems (243), las
  181 velocidades de las acciones de `Player.bmd`, el cargador, la piel y el script de la textura de
  una malla, y los slots que les reparte el original (`ModelTextureSlots`, en `Render/Models/`).
  Las tablas no se copiaron a mano del desensamblado: salen de emular los dos cargadores del
  `main.exe` (`Client/docs/research/scripts/emulate_model_loaders.py`), con la dirección de cada
  llamada. El golden corrigió al research: 17 mallas del personaje tienen script. 36 tests más y 13
  mutantes. La revisión adversarial encontró un `ModelTextureSlots` que se podía copiar con las
  rutas colgando, la letra del cargador sin punto mal leída (Q-C107) y dos variantes de mutantes de
  la búsqueda que seguían vivas: dos tests más, que fallaron primero.
  **La CL6.2 A** (2026-11-03, resuelta por Claude): la matemática de Half-Life con los tipos de glm
  (`Render/Models/HalfLifeMath`, a mano el slerp de `0x5fce90` y la igualdad de `0x5fc700`), el
  esqueleto con un cuaternión por key, la pose de `0x4eb860` y el skinning de `0x4ebc80`, en la CPU
  y sin GL. El cuerpo del personaje lleva la escala y la posición en cada vértice, no en la raíz
  (`0x50ba30` → `0x6001b0` posa, y `0x60cf70` lleva cada parte con `0x4ebc80` en 1). Contra las dos trazas de GL del laboratorio
  (`tests/support/GlTrace`, con la variable nueva `MURET_CLIENT_GL_TRACE_DIR`), cada vértice del
  personaje de la selección y del guardia de Lorencia cae a 0,0014 o menos del original en los cinco
  frames: lo que redondea un float. Los frames que lo dan corrigieron la tabla del 06. 36 tests y 17
  mutantes a mano, dos equivalentes. **La CL6.2 B** (2026-11-03, resuelta por Claude): el avance de
  `0x4ec200` y el cambio de acción de `0x4ea030` (`Render/Models/Animation`), la mascota y las alas
  por tiempo (Q-C20 corregido) y la copia que posa el render, y `MakePlayerSkeleton` con las
  velocidades, las que no repiten y la cabeza. Los saltos de la traza, con los frames corregidos,
  son pasos justos de 0,26 y 0,28. 24 tests y 16 mutantes. **La CL6.2 C** (2026-11-03, resuelta por
  Claude): las UV de los tres pases de brillo de `0x4ecc30` (`Render/Models/Shine`), con el reloj
  entero desde que arranca el cliente (Q-C22). Contra la traza de la selección caen a 0,00003 de las
  del original, con las normales sin la escala del objeto, que el PR A había decidido leyendo el
  binario y la traza confirmó. El reloj de cada swap, que la traza deja ver entero, quedó en el 06.
  6 tests y 10 mutantes. Después, a fondo: el personaje y el guardia de Lorencia, con la cabeza, a
  0,0014 de la traza, y el giro de la cabeza de `0x505de0` (un 20 % por paso) verificado con ella.

## Conceptos

**Niveles:** 1 **usar** (lo escribiste con tests) · 2 **explicar** (lo contás sin mirar) ·
3 **romper** (predecís la falla y la provocás) · 4 **transferir** (lo aplicás en otro contexto).
Una sub-ola se cierra en el nivel 3; el 4 es ejercicio extra.

**Estado:** un concepto queda **dominado** cuando lo respondés bien en dos sesiones distintas. Si
la explicación tiene huecos, queda **flojo** y entra primero en el repaso del arranque de cada
sesión, junto con los de repaso más viejo.

| Concepto | Ola | Nivel | Estado | Último repaso |
| --- | --- | --- | --- | --- |
| unidad de traducción, archivo objeto y símbolos (`nm`) | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| enlace externo e interno, `inline` y la ODR | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| bibliotecas estáticas y dinámicas, imports del PE | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| errores como valor: `Expected`, `std::variant` y `[[nodiscard]]` | CL1.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| little-endian y enteros armados con desplazamientos (`ByteReader`) | CL1.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| XOR que se deshace solo y aritmética de 32 bits que da la vuelta (XOR3, checksum) | CL1.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| registros de largo fijo sin `\0`, `std::string_view` y su lifetime (Q-C07) | CL1.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| plantillas con un parámetro que no es un tipo (`RecordTable<RecordSize>`) y `std::span` de largo fijo | CL1.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| quirk de contrato o bug interno: un checksum que se informa (Q-C01), otro que se exige, y un archivo corto (Q-C24) | CL1.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| un cifrado con estado (K16): cada byte depende del cifrado anterior y de su posición en el archivo, y qué hace falta para descifrar un pedazo suelto | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| una grilla 2D guardada fila por fila (`CellIndex`), y un tipo que no deja salirse de ella | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| floats que no son lo que parecen: `b * (1.0f / 255)` no es `b / 255.0f`, y `std::bit_cast` para ver sus bits | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| un registro con floats: se leen 4 bytes little-endian y `std::bit_cast` los pasa a float sin tocar un bit, también un NaN | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| leer un formato como lo lee el original y no como dice su cabecera (`bfOffBits`, Q-C02), y por qué eso deja afuera a una biblioteca | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| una imagen dentro de una textura de lados potencia de 2, y el orden de las filas (abajo hacia arriba en el TGA) y de los canales (BGRA) | CL1.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| una biblioteca de C que avisa sus errores con `setjmp`/`longjmp`, y por qué entre los dos no puede quedar vivo un objeto con destructor (`JpegImage.cpp`) | CL1.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| probar que una biblioteca hace lo mismo que la del original: los mismos bytes en los datos reales y en vectores chicos que corren en CI (`CODING_RULES.md` §10) | CL1.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| TCP entrega un flujo y no mensajes: el corte como máquina de estados que recibe de a un byte (`FrameCutter`), y sus invariantes | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un cifrado encadenado (XOR32) y uno por bloques (`SimpleModulus`), y por qué con la clave en el binario los dos son ofuscación | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| aritmética modular y el inverso con Euclides extendido (`ModularInverse`), y por qué la clave de cifrado del cliente es la inversa de la de descifrado del servidor | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| campos de bits MSB primero (`BitWriter`, `BitReader`), y por qué un carril del `SimpleModulus` no es un campo de 18 bits | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la aritmética sin signo que da la vuelta, el desborde con signo que es comportamiento indefinido, y `std::uint16_t * std::uint16_t`, que se multiplica como `int` | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un test de propiedad con `std::mt19937` y semilla fija, tomando la salida del motor y no una distribución | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un bug que lee bytes viejos de un buffer que se reusa (Q-C03), modelado para verlo fallar | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un lector que recibe lecturas enteras y saca paquetes enteros, sin leer un byte que no llegó (`PacketReader`), y un error que queda hasta que se cierra la conexión | CL2.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un tipo que se valida al armarse (`Packet::FromBytes`) y deja cambiar el cuerpo pero no la cabecera (`MutableBody`) | CL2.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| probar contra una captura del cable sin mostrar lo que lleva: contar paquetes y decir dónde difieren, nunca qué valen (`SameBytesWithoutValues`) | CL2.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| constructores con nombre (`PacketWriter::C1`, `C2`) y métodos que devuelven `*this` para encadenarse, con un `Finish()` `const` que no cambia el escritor | CL2.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un cifrado encadenado que se deshace en el lugar yendo de atrás para adelante (`Xor32DecodeBytes`), y separarlo del armado para probar cada etapa sola | CL2.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| vectores de test armados por un modelo escrito de nuevo desde el research, y no por el código que prueban (`make_vectors.py`) | CL2.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una plantilla con un parámetro `enum` para que dos claves del mismo formato sean tipos distintos (`SimpleModulusKey<KeyRole>`), y una fábrica que valida antes de construir (`FromLanes`) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un checksum que cubre el claro y no el largo útil: qué cambios detecta y cuáles no (`SimpleModulusTest.TheChecksumDoesNotCoverTheUsefulSize`) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| vectores que fijan lo que un ida y vuelta no ve: el orden de los bits 16 y 17 de un carril, y el producto de 32 bits que da la vuelta | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un contador por conexión que detecta un paquete perdido o repetido, y resincronizar en vez de arrastrar el desfase (`PacketSerials`, Q-C04) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| varios errores en un paquete: cuál gana y por qué (el checksum malo sobre el largo útil imposible, porque es el que lleva el aviso) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una interfaz con dos implementaciones elegidas por configuración (`IObfuscation`), y por qué una clase base polimórfica no se copia en público (el slicing) | CL2.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un estado que sigue entre dos lecturas: la máscara que eligió el primer byte de un paquete partido, y no desenmascarar nunca un byte del paquete siguiente | CL2.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un XOR encadenado que se deshace, se cambia un byte y se rehace: por qué cambian también los que siguen (los remapeos de FoxAnti) | CL2.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una ida y vuelta que no cierra entre dos tablas (Q-C17), y fijarla con la tabla entera de la referencia en vez de arreglarla | CL2.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un struct del protocolo leído y escrito campo por campo, con el offset de cada campo fijado en un test, en vez de `reinterpret_cast` o `#pragma pack` (`LoginTest.PutsEachFieldAtItsOffset`) | CL2.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un número en big-endian dentro de un protocolo little-endian: cómo se ve en el binario (`pkt[5] * 0x100 + pkt[6]`) y cómo se prueba (`00 05` es 5 y no 1280) | CL2.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un texto de largo fijo que puede no tener `\0` (Q-C07), leído como una vista sin copiar (`ByteReader::ReadBytes`) y cortado en el primer `\0` (`TextField`) | CL2.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una resta que da la vuelta en 16 bits a propósito, y dónde va el `static_cast` para que el compilador no la marque (`KeepaliveSpeed`) | CL2.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el paquete y cómo sale al cable en un solo valor (`Outgoing` y `Delivery`), para que el que manda no tenga que acordarse de cifrar el login | CL2.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un golden que elige paquetes del cable sin mirar sus datos: deshacer el XOR32 para leer el subcódigo, y comparar solo lo fijo con `SameBytesWithoutValues` | CL2.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| las convenciones del código nuevo de MuMain: `m_` para el estado del objeto y el nombre natural para el parámetro, `I` para una interfaz, y un ejecutable por archivo de test juntado con un glob | estilo | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el bucle de juego con paso fijo y acumulador (`FixedStep`): por qué un paso variable depende de los FPS, y el resto que pasa al frame siguiente | CL3.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| relojes de pared y monótonos, unidad contra resolución, y el tick de 15,625 ms que hace durar 46,875 ms una espera de 40 con `GetTickCount` (`TickGrid`) | CL3.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| `std::chrono`: duraciones enteras y de punto flotante, `duration_cast` que trunca hacia cero, y lo que se pierde truncando cada frame a ms (`ChronoFixedStep`) | CL3.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la interpolación con `alfa = acumulador / paso`, que no cambia la lógica, y VSync, que ata los FPS al monitor | CL3.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la espiral de la muerte y el tope, y por qué el original no espirala: mide desde después de los pasos (el jefe, `OriginalLoop`) | CL3.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un acumulador con signo y sin tope leído en el binario (`JL`, `SUB`, `ADD`), y su valor inicial en `.data`, 40 (carril R) | CL3.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| qué corre por paso, por frame o por reloj en el original (P-C03), leído antes de desacoplar: interpolar solo es fiel si lo que va por paso sigue por paso | CL3.R | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| RAII sobre una API de C (`SdlContext`, y `Window` con sus `std::unique_ptr` y deleters), una capa que encierra SDL3 (`ClientPlatform`), y el contexto GL como un recurso que vive dentro de la ventana | CL3.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el paso fijo con acumulador e interpolación con el reloj inyectado (`IClock`): el mismo `GameLoop` con el reloj de SDL en `Main` y con `FakeClock` en los tests, donde un frame tarda lo que el test dice | CL3.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el ritmo del original sin las reglas del paso fijo: ms enteros sin tope, medir desde después de los pasos y dormir de tick en tick sobre la grilla de 15,625 ms (`FaithfulPacer`, `TickCountMs`) | CL3.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| estado contra eventos: "está abajo" se consulta y "se apretó" es un flanco (`InputState`), un flanco leído por frame que llega a exactamente un paso (`ConsumeEdges` después del primer paso), y una tecla lógica que es el OR de sus dos lados (`Shift`, como `VK_SHIFT`) | CL3.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| de dónde sale lo que depende del lugar (la carpeta de datos, el servidor, el video), validado antes de abrir nada (`Resolve`), y un log que se lee en un test (`ILogSink`) | CL3.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un log sin globales, con la salida inyectada (`ILogSink`), un `std::mutex` en cada salida porque SDL loguea desde sus hilos y otro en el `TeeSink` para el orden, un archivo que agrega con un tope de 1 MiB al abrir (`FileSink`), y un RAII que devuelve la salida de SDL antes de que muera el `Log` (`SdlLog`) | CL3.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un limitador de frames con meta absoluta contra un sueño relativo: la deriva de lo que corre entre frames, qué valor toma la meta cuando un frame se pasa (donde terminó, para no ponerse al día ni volver a sumar el hueco), y por qué con VSync no se usa (`SleepRestOfFrame`, el `FpsCap` de MuMain) | CL3.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un socket con RAII sobre el descriptor del sistema: el destructor cierra, se mueve con `std::exchange` y no se copia, con un handle que entra en Windows y en POSIX y el `#ifdef _WIN32` en un solo archivo (`LabSocket`) | CL4.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el `connect` no bloqueante como una máquina de estados (en curso, conectado, rechazado): escribible no quiere decir conectado, y `SO_ERROR`, que dice cómo terminó, se borra al leerlo (`CheckConnect`) | CL4.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| "probá más tarde" (`EWOULDBLOCK`, `EAGAIN`, `WSAEWOULDBLOCK`, `EINTR`) contra un error y contra el `recv` en 0, que es el FIN del otro lado; `SIGPIPE` y `MSG_NOSIGNAL`, y un `recv` con buffer vacío que no se llama | CL4.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el `send` que toma menos de lo pedido, y la cola con cursor, de tamaño fijo, que manda el resto sin repetir ni perder (`DrainQueue`) | CL4.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el drenaje de `FD_WRITE` de `0x4e87b0`, que resta dos veces lo mandado y no corre el buffer: con un parcial repite los primeros bytes y pierde los últimos (el jefe, Q-C11), leído en el desensamblado (carril R) | CL4.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un socket consultado una vez por frame contra un hilo por conexión o IOCP y epoll, y `select` contra `poll` y `WSAPoll` para saber si un `connect` terminó (la comparación de `labs/README.md`) | CL4.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| E/S no bloqueante detrás de una interfaz chica (`ISocket`, con `SystemSocket` y `FakeSocket`): el connect como máquina de estados que guarda su resultado porque `SO_ERROR` se borra al leerlo, "probá más tarde" contra un cierre (`recv` en 0, `ECONNRESET`), `SIGPIPE` y `MSG_NOSIGNAL`, y un `#ifdef _WIN32` en un solo archivo (`SocketApi`) | CL4.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una cola de envío con cursor sobre un buffer fijo (`SendQueue`): por qué restar lo que salió dos veces repite el principio y pierde el final (Q-C11), compactar solo cuando lo nuevo no entra, y un tope que cierra en vez de crecer | CL4.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la red consultada una vez por frame en el mismo hilo que la lógica (`ILoopClient::Poll`), y dónde va en el modo fiel: fuera de la medición, donde lo que tarda se pierde de a ticks enteros, como los pasos (`FaithfulDoesNotMeasureThePoll`) | CL4.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una tabla de despacho en vez de un `switch`: opcode → handler con un segundo nivel por subcódigo, `std::function` como handler, registrar dos veces como un error que se devuelve, y un aviso que sale una sola vez por opcode (`Dispatcher`, `std::bitset`) | CL4.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una conexión consultada por frame que devuelve eventos en orden (`Connection`): el `Opened` antes de lo que llegó con él, un cierre que pasa en un `Send` y sale en el próximo `Poll`, el tope del connect mirado después del `PollConnect`, y qué error corta (el del corte) y cuál se descarta (el de la ofuscación) | CL4.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un protocolo con turnos como máquina de estados (`ServerSession`): qué espera cada estado y qué hace con lo que llega fuera de turno, un dispatcher por servidor, y dejar de recorrer un lote de eventos cuando un handler cambia o cierra la conexión (un contador que cambia con cada `Open`), sin perder el `Closed` que cerró el lote | CL4.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| el cifrado como una etapa más del camino de un paquete (`Connection`): dónde va entre las de la ofuscación, claves y seriales por conexión que vuelven a 0 en cada `Open`, un serial que solo se gasta si el paquete sale, un relleno con semilla para que el test sea repetible, y un paquete que no descifra como un evento y no como un cierre | CL4.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| secretos fuera de todo lo que se imprime: la cuenta y la clave del entorno y no de la config ni de la línea de comandos, errores que nombran la variable sin su valor, un `PrintTo` que da los largos, un log que no las trae (lo mira un test), y el login comparado con `SameBytesWithoutValues`; y un dato del servidor que va al log limpio (la versión, con `?` en lo que no se ve) | CL4.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un test de integración en un hilo con sockets de verdad: servidores falsos con un guion que contestan lo que les llegó (así el orden del sistema no cambia el resultado), un tope de reloj real y no de vueltas, y un servidor que, fuera del guion, se queja en palabras y corta para que el test falle enseguida | CL4.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un temporizador por reloj y no por pasos (`IntervalTimer`): vencer contra una meta absoluta para no sumar deriva, qué hacer con un frame largo (uno solo, y seguir desde ahí, como un `WM_TIMER` que no se junta), y el reloj falso que lo prueba sin esperar | CL4.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| qué hace un cliente cuando el otro lado calla o corta: un tope por estado que se mira después de leer lo que llegó, un timer que se detiene al salir del estado que lo necesita, y no reconectarse solo (`ReplyTimeout`, `SetState`) | CL4.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| de la función fija al pipeline programable: los vértices en un VBO con su formato en un VAO, un vertex shader que hace lo de la pila de matrices, un fragment shader que hace lo de `glTexEnv`, y `discard` en vez de `glAlphaFunc` | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| las coordenadas de un vértice: del clip al NDC dividiendo por w, y del NDC al píxel con el viewport; la y para arriba de `gluOrtho2D` y la que el original da vuelta en cada vértice (`OriginalQuad`, el jefe) | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una matriz en column-major (`Ortho2D`, `glm::ortho` y `glm::value_ptr`), qué espera `glUniformMatrix4fv` con `GL_FALSE`, y qué se ve con la transpuesta | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un fan pasado a triángulos con índices (`FanToTriangles`), el sentido de giro y el culling, y por qué dar vuelta la y dos veces deja los triángulos de espaldas | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| las cuentas de float del binario: escalar multiplicando por una inversa que no es exacta (1/640 en `0x6ba070`), y una resta que la x87 hace sin redondear (`FST` y `FLD ST0`) y el decompilado no muestra | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| subir una textura: `GL_UNPACK_ALIGNMENT` con filas de 6 bytes, el filtro, el wrap y los lados potencia de 2, con el largo del buffer chequeado antes de llamar a GL (`RequiredUnpackBytes`) | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la mezcla normal y la aditiva, y por qué con la normal un lote no puede cambiar el orden de dibujo | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| RAII sobre un nombre de GL (`GlName`), que se mueve con `std::exchange`, no se copia y necesita el contexto vivo; una X-macro para la tabla de funciones (`LabGl`); y un test de GL que se saltea sin pantalla salvo con `MURET_CLIENT_REQUIRE_GL=1` | CL5.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| leer una UI en el binario: una clase por ventana con su vtable sobre una base común, un administrador con el lugar de cada una, el `switch` de un id de aviso a un índice de `Text.bmd`, y uno compilado como tabla de bytes y tabla de saltos (`0x4e5f04` y `0x4e5ebc`) | CL5.R | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un loader de GL propio como X-macro: una lista que arma el struct, el pedido y los nombres que prueba un test, y por qué se pregunta la versión y el perfil con dos funciones de la 1.0 antes de pedir las de la 3.3 | CL5.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una textura de GL desde un buffer en memoria: el formato interno con tamaño (`GL_RGB8`) contra el del buffer (`GL_RGB`), `GL_UNPACK_ALIGNMENT` en 1 y vuelto a como estaba, por qué sin mipmaps el filtro de minificación por defecto deja la textura incompleta (negra), y `GL_CLAMP` contra `GL_CLAMP_TO_EDGE` con `GL_LINEAR` (Q-C73) | CL5.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| probar GL con píxeles: un framebuffer con una textura de color, `glReadPixels` que da las filas de abajo para arriba, un fixture que se saltea sin pantalla y falla con `MURET_CLIENT_REQUIRE_GL=1`, y un shader que no compila o no enlaza con el log del driver en el error | CL5.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un juego de texturas como dato y no como código: la ruta del original, su slot y su muestreo en una tabla `constexpr`; la primera lectura del disco de la capa de datos, con un error por tipo (falta, no se lee, no es de ningún cargador) que dice la ruta; una tabla por escena indexada por el slot del original detrás de una interfaz sin GL; y `Expected<void, E>` para lo que solo puede fallar | CL5.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| coordenadas lógicas contra físicas: la UI escalada desde 640 × 480 cada eje por su lado, la que va en píxeles y la de la UI nueva en su propio espacio (800 × 600 en la carga), con la y desde arriba que el original da vuelta en cada esquina | CL5.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un lote de sprites: muchos quads en un VBO por frame y un `glDrawArrays` por tramo de la misma textura, mezcla y alpha test, sin reordenar nunca; y el estado de la función fija (color, mezcla, `GL_TEXTURE_2D`, alpha test) guardado como lo deja el original, que pasa de un quad al otro (Q-C57, Q-C75) | CL5.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| la mezcla y el alpha test en el pipeline programable: `glBlendFunc` por tramo, la textura apagada y el alpha test como banderas del fragment shader (`glUniform1i`), y por qué 0,25 justo no pasa `GL_GREATER` | CL5.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| rasterizar una fuente en un bit: el contorno, el hinting de TrueType (el intérprete v35 contra el v40), un bitmap de un bit por píxel con su `pitch`, y por qué el alto de `CreateFontA` es la celda (el ascenso más el descenso) y no el em | CL5.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| medir como `GetTextExtentPointA` (los avances con hinting, sumados sin kerning) y ubicar con la aritmética del original: una división entera sin signo que trunca y un producto que da la vuelta en 32 bits | CL5.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| colores dentro de un texto: un parser que saca pares de bytes y anota tramos sobre el texto limpio, con un tope que corta los tramos y no la limpieza (`0x4667a0`); y las codificaciones de un byte (Latin-1) contra GB2312 y UTF-8, decididas contando bytes sin imprimir el texto | CL5.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una caché con tope y la regla de sacar el menos usado (LRU): una lista que se reordena con `splice` sin mover sus nodos, un índice que apunta a ellos, una clave con todo lo que cambia el resultado (texto, fuente y colores), y los recursos (slots) que vuelven al soltar | CL5.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una máquina de estados con transiciones explícitas: una tabla de pares que se prueba entera (las 25 de cinco escenas), un pedido que no está que se rechaza sin cambiar nada, y el ciclo de vida de un estado (entrar, paso, dibujo, salir) con sus recursos soltados antes de que entre el siguiente | CL5.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| el mundo como dato: una función que da la carpeta de cualquier mapa desde una tabla de rangos, leída de los `cmp` y los saltos con signo del desensamblado, y un golden que recorre todos los mundos que hay en vez de uno | CL5.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un doble que anota en orden para probar un dibujo: el lienzo como interfaz (el lote, el texto, el mundo y el fin del 2D), el `RecordingFrameTarget` que pasa el lote a una lista de quads, textos y pases, y por qué el orden y el estado de cada quad dicen más que los píxeles | CL5.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un recurso que vive lo que su dueño: el juego de texturas que sube el `Enter` y suelta el `Leave`, la caché del texto que suelta sus slots en su destructor, y el orden de las declaraciones de `main.cpp`, que decide quién muere antes | CL5.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| el texto tecleado contra las teclas: un evento de texto en UTF-8 que se copia porque es de SDL, el IME que compone sin mandar texto, y un campo que guarda bytes de ASCII imprimible hasta un tope y muestra la clave en asteriscos (`TextField`) | CL5.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una UI retenida mínima: ventanas que guardan su estado entre pasos, el foco del teclado, la captura del mouse de un botón (un clic es soltar encima del que se apretó), y una ventana que devuelve lo que pasó en vez de llamar a la red | CL5.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| ventanas en píxeles del framebuffer, centradas, con sus textos en 640 × 480: la densidad de la pantalla, el mouse acotado como en `0x640940`, y pasar de un espacio al otro truncando como la x87 (`DesignX`) | CL5.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una máquina de estados de red que espera al jugador: estados sin tope ni keepalive, lo que llega fuera de turno ignorado con un aviso, y un resultado guardado para que la UI elija el aviso; y un modo sin UI (`AutoLogin`) que hace lo del jugador una vez por vuelta, para no quedar en un lazo | CL5.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| la UI que sigue al modelo: mirar el estado en cada paso y actuar solo cuando cambia, también después de lo que pidió el jugador; una sola ventana atendida por paso, la de arriba, para que el Enter que cierra una no le llegue a la que se abre; y una escena que pide cerrar el juego en vez de cerrarlo | CL5.5 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un giro como ángulos de Euler: el orden de los ejes, el gimbal lock (con el pitch en 90° solo cuenta x − z), y por qué interpolar los ángulos puede dar la vuelta larga | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un cuaternión unitario: los medios ángulos de `AngleQuaternion`, su matriz (`QuaternionMatrix`), q y −q como el mismo giro, y el orden x, y, z, w contra el constructor de cuatro de glm, que toma la w primero (`glm::quat::wxyz`) | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| el slerp: dar vuelta q para ir por el camino corto, el arco con senos y el paso a lineal cerca de cos = 1; las dos comparaciones de `0x5fce90`, una con el cos de la pila y otra con el redondeado, y una rama que con cuaterniones de largo 1 no corre (sí con un NaN o un infinito) | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una cadena de huesos: la 3 × 4 por filas de Half-Life contra la de glm por columnas, cada hueso por la matriz de su padre (el padre antes), la raíz por la del objeto con su escala, y el skinning rígido: el punto con la traslación, la normal solo con el giro | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| el frame fraccionario: la pose entre dos keys con la parte fraccionaria, la key anterior que guarda el original (`0x4ec200` y `0x4eb860`), y por qué la tabla del 06 va una key atrás | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| slerp, lerp normalizado y ángulos interpolados: cuánto se aparta cada uno del slerp, que con el lerp normalizado crece con el cubo del ángulo entre las keys, y cuándo se ve en una traza (la comparación de `labs/README.md`) | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| la x87 con la precisión en 53 bits (`_controlfp` en `0x6a4b73`): qué queda en la pila como double y qué se redondea a float en cada store, y probar unos tests con mutantes a mano, uno equivalente incluido | CL6.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| leer un formato y un cargador en el binario y probarlos contra datos reales: un script que lee los `.bmd` como `0x4f13d0`, el asignador de slots simulado contra un volcado de la memoria, y qué queda *estático*, *inferido* o verificado | CL6.R | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| un formato con conteos que dicen cuánto leer de cada parte: no pedir memoria antes de saber que entra, un cifrado que vale solo para un bloque (`0x0C` desde `+8`), y un golden que encuentra lo que el plan no esperaba (Q-C108) | CL6.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una tabla del original sacada del binario y no copiada a mano: emular un cargador, qué registros dependen de lo de antes (el `ebx` de `0x60ff50`), y un slot que se busca por nombre desde el 0 hasta el próximo libre | CL6.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| la pose de un esqueleto: dos keys y la parte fraccionaria del frame, el slerp y su vuelta, la cadena de padres, filas contra columnas, y un golden que busca el frame en vez de tolerar el error | CL6.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| una animación por paso y otra por tiempo: el módulo, el tope y la key anterior, un reloj falso a varios FPS, y la copia interpolada que cruza keys como el paso | CL6.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |
| UV que salen de la normal en el mundo y de un reloj: por qué un float de ms deja de contar, y sacar de la traza el valor que no se ve (T entero desde T módulo 10.000 y un seno y un coseno) | CL6.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-03 |

## Preguntas para olas futuras

- Cliente (2026-10-30): nació `Client/` con su [research](../../Client/docs/research/README.md) y su [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) (olas CL0 a CL10, SDL3 + OpenGL 3.3, tick fijo y FPS desacoplados sin perder fidelidad). ~~¿Cuándo arranca la CL1 respecto de la ola 1 del servidor?~~ Decidido el 2026-11-01: el cliente va directo (pregunta 3 del roadmap del cliente)
