# Referencias: emuladores de WoW

Los emuladores de World of Warcraft más conocidos resuelven en C++ los mismos problemas que tiene
MuRet: aceptar conexiones, reensamblar paquetes, repartir trabajo entre hilos y hablar con la base
sin trabar el server. Este documento junta **qué usan** y **qué patrones conviene mirar** en cada ola.

**No los adoptamos como dependencia.** No son frameworks reutilizables sino aplicaciones enteras.
Su capa de base es para MySQL (MuRet usa ODBC contra SQL Server) y usan Boost.Asio desde el día
uno, que es justo lo que MuRet escribe a mano en las olas 2 y 3 para aprenderlo (decisión D-3 del
[roadmap](../superpowers/specs/2026-10-25-dataserver-port-design.md)). Se leen como referencia, igual
que MuMain: se entiende el patrón y se escribe la versión propia.

> Rutas verificadas el 2026-10-26 contra la rama `master` de cada repo. Si un link se rompe, el
> archivo se movió: buscalo por nombre.

## Qué usa cada uno

| Proyecto | Red | Base | Logging / formato | Threading | Build |
| --- | --- | --- | --- | --- | --- |
| [TrinityCore](https://github.com/TrinityCore/TrinityCore) | Boost.Asio | cliente C de MySQL/MariaDB | `Log` propio con appenders + fmt (vendorizada en `dep/fmt`) | `std::thread` + colas propias | CMake |
| [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) | Boost.Asio (mismo diseño que TrinityCore) | MySQL | igual que TrinityCore | igual que TrinityCore | CMake |
| [CMaNGOS](https://github.com/cmangos/mangos-wotlk) | Boost.Asio (antes ACE) | MySQL o PostgreSQL detrás de una capa propia | `Log` propio | hilo de "SQL diferido" + `std::thread` | CMake |

Las tres comparten OpenSSL para la criptografía del login, que el DataServer no necesita.

## Patrones para copiar, por ola

Cada patrón dice qué problema resuelve, dónde mirarlo y qué preguntarte antes de escribir el tuyo.
Leelo **después** de la guía de la ola, no antes: primero intentá resolverlo vos.

### Ola 1 y 2: buffer de reensamblado de paquetes

- **Problema:** TCP es un stream; un `recv` puede traer medio paquete o dos y medio.
- **Dónde:** TrinityCore [`src/common/Utilities/MessageBuffer.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/Utilities/MessageBuffer.h).
  Un `std::vector<uint8>` con dos posiciones: hasta dónde se escribió y hasta dónde se leyó. Cuando
  se consume lo leído, compacta moviendo lo que queda al principio.
- **Preguntate:** ¿cuándo conviene compactar y cuándo crecer? ¿Qué pasa si el header C1/C2 declara
  un tamaño mayor al máximo permitido?

### Ola 3: un hilo de red por núcleo, sesiones repartidas

- **Problema:** muchas conexiones y pocos hilos, sin que una sesión se procese en dos hilos a la vez.
- **Dónde:** TrinityCore `src/common/network/`:
  [`AsyncAcceptor.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/network/AsyncAcceptor.h)
  acepta,
  [`SocketMgr.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/network/SocketMgr.h)
  elige el `NetworkThread` con menos conexiones,
  [`NetworkThread.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/network/NetworkThread.h)
  tiene su propio `io_context` y su lista de sockets, y
  [`Socket.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/network/Socket.h)
  es un template CRTP (`Socket<Derived>`) con la cola de escritura y el cierre ordenado.
  En AzerothCore los mismos archivos están en `src/server/shared/Network/`.
- **Preguntate:** el original del DataServer usa un solo completion port con N workers. ¿Qué
  cambia si cada hilo tiene su propio backend? ¿Quién es dueño del socket cuando se cierra mientras
  hay una escritura pendiente? (son los bugs C-2 y C-4 del
  [research 08](../../Server/DataServer/docs/research/); el C-3 es la cola de envío).
- **Para la 3.4 opcional (Asio):** esta es la implementación de referencia para comparar.

### Ola 4: cola productor/consumidor

- **Problema:** pasar trabajo de los hilos de red al hilo de lógica sin carreras.
- **Dónde:** TrinityCore [`src/common/Threading/ProducerConsumerQueue.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/Threading/ProducerConsumerQueue.h).
  `std::mutex` + `std::condition_variable` + `std::queue`, con un `Cancel()` para despertar a todos
  al apagar.
- **Preguntate:** ¿cómo terminás el consumidor sin perder los mensajes que quedan en la cola?

### Ola 6: base de datos

- **Sentencias preparadas por enum.** Cada base declara un enum con todas sus queries y las
  registra al arrancar. Ver
  [`Implementation/CharacterDatabase.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/server/database/Database/Implementation/CharacterDatabase.h)
  y [`PreparedStatement.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/server/database/Database/PreparedStatement.h).
  Encaja con "todo el SQL parametrizado" del roadmap y con los `MX_*`/`SP_*` del research 06.
- **Pool de conexiones con workers.** [`DatabaseWorkerPool.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/server/database/Database/DatabaseWorkerPool.h)
  separa conexiones síncronas (para el arranque) de asíncronas (cada una con su hilo y su cola).
- **Query asíncrona con callback.** [`QueryCallback.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/server/database/Database/QueryCallback.h):
  la query devuelve un objeto que el hilo de lógica revisa en cada tick y, cuando el resultado está
  listo, ejecuta el callback en *su* hilo, no en el de la base.
- **La versión simple de CMaNGOS.** [`src/shared/Database/SqlDelayThread.h`](https://github.com/cmangos/mangos-wotlk/blob/master/src/shared/Database/SqlDelayThread.h):
  un único hilo que ejecuta las escrituras diferidas. Es un buen primer paso antes del pool.
- **Preguntate:** ¿el GameServer espera la respuesta en el mismo orden en que mandó los pedidos de
  un personaje? Si hay varias conexiones asíncronas, ¿qué garantiza ese orden?

### Logging (cuando lo necesites)

- **Dónde:** TrinityCore [`src/common/Logging/Log.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/Logging/Log.h)
  y [`Appender.h`](https://github.com/TrinityCore/TrinityCore/blob/master/src/common/Logging/Appender.h):
  loggers por categoría (`"network"`, `"sql"`) que escriben en uno o más appenders (consola, archivo).
- **Para MuRet:** el logger tiene que ser seguro entre hilos (el bug C-5 del original es un log
  sin lock). Al principio alcanza con `std::format` de C++20 y una función propia. Si el
  logging crece, spdlog (está en vcpkg) da lo mismo sin escribirlo.

## Qué no copiar

- **Boost entero.** Para el DataServer solo interesaría Asio, y existe standalone (sin Boost).
- **El cliente de MySQL y sus tipos.** El patrón sirve; la API no, porque MuRet usa ODBC.
- **La escala.** Son proyectos de cientos de miles de líneas con scripting, mapas y pathfinding.
  Mirá un archivo por vez, el que corresponde a la ola en curso.
