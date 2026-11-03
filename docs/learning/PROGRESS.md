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
- **CL4.2 (el `Dispatcher`), resuelta por Claude** el 2026-11-02: una tabla opcode → handler, con
  un segundo nivel por subcódigo (el byte que sigue al opcode, `pkt[3]` en un C1 y `pkt[4]` en un
  C2), en vez del `switch` de 10,5 KB de `0x4e2b10`. Lo que no tiene handler da `Unknown` y una
  línea de aviso la primera vez de cada opcode, o de cada opcode y subcódigo, sin los bytes; un
  C3 o un opcode con subcódigos que termina en el opcode dan `Malformed` (el original lee el byte
  que no llegó, Q-C42). Registrar dos veces da `false` y no pisa. Tu paso 1, la tabla de qué lee
  cada uno, lo resolvió Claude: está en el plan.
- **La CL4.3 la escribe Claude**, en dos PRs. El primero trae la `Connection`: el camino de un
  paquete en claro por una conexión, en el orden del original. Al recibir, el socket, el corte con
  la máscara de la ofuscación, `AfterCut` y `BeforeDispatch`; al mandar, `BeforeSend` y la
  `SendQueue`, sin adelantarse nunca a lo pendiente. Lo que pasa en un frame sale como eventos, en
  orden (`Opened`, los paquetes y al final `Closed` con su motivo), porque Network no despacha. Un
  error del corte cierra, porque se perdió dónde empieza el siguiente, y un paquete que la
  ofuscación no traduce se descarta y la conexión sigue. Volver a abrir vacía el corte y la cola:
  nada del ConnectServer llega al GameServer. Si resolver el host tarda más de 100 ms, va al log.
  El cifrado llega en la CL4.4.
- Documentación lista: el [roadmap](../superpowers/specs/2026-10-25-dataserver-port-design.md) (qué hace MuRet, las olas 0 a 8 y la tabla de las 9 a 14), el [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md) (cómo se aprende), el [research del original](../../Server/DataServer/docs/research/) (01..10, qué hace el binario byte a byte), los [planes por ola](../superpowers/plans/) y una [guía de estudio por ola](README.md)
- De la 0.3 ya están `docs/CODING_RULES.md`, `AGENTS.md` y `.clang-format`, adaptados de MuMain; quedan los ADR 0001 (tuyo) y 0002

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
| E/S no bloqueante detrás de una interfaz chica (`ISocket`, con `SystemSocket` y `FakeSocket`): el connect como máquina de estados que guarda su resultado porque `SO_ERROR` se borra al leerlo, "probá más tarde" contra un cierre (`recv` en 0, `ECONNRESET`), `SIGPIPE` y `MSG_NOSIGNAL`, y un `#ifdef _WIN32` en un solo archivo (`SocketApi`) | CL4.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una cola de envío con cursor sobre un buffer fijo (`SendQueue`): por qué restar lo que salió dos veces repite el principio y pierde el final (Q-C11), compactar solo cuando lo nuevo no entra, y un tope que cierra en vez de crecer | CL4.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la red consultada una vez por frame en el mismo hilo que la lógica (`ILoopClient::Poll`), y dónde va en el modo fiel: fuera de la medición, donde lo que tarda se pierde de a ticks enteros, como los pasos (`FaithfulDoesNotMeasureThePoll`) | CL4.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una tabla de despacho en vez de un `switch`: opcode → handler con un segundo nivel por subcódigo, `std::function` como handler, registrar dos veces como un error que se devuelve, y un aviso que sale una sola vez por opcode (`Dispatcher`, `std::bitset`) | CL4.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una conexión consultada por frame que devuelve eventos en orden (`Connection`): el `Opened` antes de lo que llegó con él, un cierre que pasa en un `Send` y sale en el próximo `Poll`, el tope del connect mirado después del `PollConnect`, y qué error corta (el del corte) y cuál se descarta (el de la ofuscación) | CL4.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |

## Preguntas para olas futuras

- Cliente (2026-10-30): nació `Client/` con su [research](../../Client/docs/research/README.md) y su [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) (olas CL0 a CL10, SDL3 + OpenGL 3.3, tick fijo y FPS desacoplados sin perder fidelidad). ~~¿Cuándo arranca la CL1 respecto de la ola 1 del servidor?~~ Decidido el 2026-11-01: el cliente va directo (pregunta 3 del roadmap del cliente)
