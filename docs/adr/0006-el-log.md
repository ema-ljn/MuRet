# 0006. El log: agrega, se vacía al pasar 1 MiB, va en claro y avisa si deja de escribir

- **Estado:** aceptado. Lo resolvió Claude en el plan de la CL3 (CL3.4, 2026-11-02); los arreglos
  de AUD-C01 y AUD-C02 salen de la auditoría y de su plan de seguimiento (2026-11-03).
- **Fila:** Q-C48 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
- **Código:** `Client::Core::FileSink` y `MaxLogFileBytes` (`Client/src/Core/FileSink.h`), detrás
  del `TeeSink` de `main.cpp`, que manda cada línea también a stderr.

## Contexto

El original escribe `MuError.log` (`0x41f7a0`). Lo abre con `OPEN_ALWAYS` (`0x41f7e8`), compartido
solo para leer, y se para al final para escribir (`SetFilePointer`, `0x41f802`). Entre las dos
cosas, `0x41f830` lo recorta: lee hasta `0x1ffff` bytes, y se queda con lo que va desde el byte
`0x7fff` o, en un archivo más chico, con las cuatro últimas sesiones (las marcas
`###### Log Begin ######`). Después lo borra y lo reescribe. Lo escribe cifrado con un XOR de 16
bytes. Eso es *estático*, verificado con objdump
(Q-C48). Un segundo cliente abierto a la vez no puede abrirlo para escribir, y se queda sin log: el
`CreateFileA` no se mira. Eso es *inferido*, sin probar.

En MU es normal tener dos clientes abiertos. MuRet loguea más que el original por corrida (el
arranque, los avisos del bucle, SDL). La
[auditoría del 2026-11-03](../audits/2026-11-03-cliente-cpp20.md) encontró dos defectos en la
primera versión: AUD-C01, el sink que vaciaba el archivo podía pisar líneas de otro cliente, y
AUD-C02, una escritura que fallaba después de abrir no avisaba.

## Decisión

- **Agrega al final**, como el original, en `muret-client.log` (o `[Log] Path`): el log de una
  caída sigue ahí al volver a abrir. Cada línea es una sola escritura con su flush.
- **Dos clientes escriben los dos.** En modo agregar, cada escritura va al final, y las líneas
  quedan intercaladas pero enteras. Es una diferencia con el original, que deja al segundo sin log
  (*inferido*).
- **Al abrir, si pasa de 1 MiB (`MaxLogFileBytes`), se vacía entero** en lugar de cortar el
  principio. Es más simple, deja varias corridas, y se mira solo al abrir: una corrida no pierde
  lo suyo a la mitad por su propio tope. Después de vaciarlo, el archivo se vuelve a abrir para
  agregar, así no pisa lo que escribe otro cliente (AUD-C01).
- **Va en claro**, sin el XOR ni la marca por sesión: se lee con cualquier editor.
- **Si una escritura falla** (el disco se llenó, se fue la unidad), `FileSink` deja un aviso en
  stderr que nombra la ruta, una sola vez, cierra el archivo y no reintenta (AUD-C02). Las líneas
  siguen en stderr por el `TeeSink`. El aviso no pasa por el `Log`, que es quien llamó.

## Consecuencias

- **Se gana:** un log que se lee sin herramientas, que no se pierde con dos clientes y que no
  crece sin fin. Un fallo de escritura se ve una vez, en vez de perder el log en silencio.
- **Se pierde:** si otro cliente abre el archivo cuando ya pasó el tope, lo vacía, y la corrida
  que ya estaba pierde lo que había escrito antes; lo que escribe después queda. Es un límite
  aceptado: cambiar el vaciado pide otra mini-spec (T4 del
  [plan de seguimiento](../superpowers/plans/2026-11-03-seguimiento-auditoria-cliente.md)). Un error
  que el sistema da recién al cerrar no se ve, porque el `ofstream` lo traga en su destructor.
  Ningún test prueba dos procesos que escriben a la vez: lo cubren dos sinks independientes.
- **Lo fijan:**
  - `FileSinkTest.AppendsToThePreviousRun`: agrega a lo que había.
  - `FileSinkTest.EmptiesAFileOverTheCap` y `KeepsAFileExactlyAtTheCap`: el vaciado, y que justo
    en el tope no se vacía.
  - `FileSinkTest.AppendsFromIndependentSinksAfterTheCap`: dos sinks abiertos como dos clientes,
    después de que el segundo vació el archivo. Fija también el límite aceptado.
  - `FileSinkTest.KeepsLinesWholeFromTwoThreads`: dos hilos del mismo sink.
  - `FileSinkTest.AWriteFailureCanBeObserved`, `ReportsAWriteFailureOnce` y
    `WritesNothingAfterAFailure`: el fallo de escritura, con `/dev/full` y con un tope de tamaño
    (no corren en Windows).

El contrato está en la sub-ola CL3.4 del
[plan de la CL3](../superpowers/plans/2026-11-02-ola-cl3-plataforma-y-bucle.md), y los arreglos,
en T1 y T2 del plan de seguimiento.
