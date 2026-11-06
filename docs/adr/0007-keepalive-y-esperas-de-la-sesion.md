# 0007. La sesión: keepalive con el GameServer conectado y esperas de respuesta

- **Estado:** aceptado. Las dos son de Claude: la recomendación de la pregunta 6 del plan de la
  CL4, que Emanuel no cambió, y el tope, que resolvió en la CL4.5 (2026-11-02). Los estados que
  esperan al jugador son de la CL5.5 (2026-11-03).
- **Filas:** Q-C53 y Q-C56 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
- **Revisión del 2026-11-06:** Emanuel pide conservar la espera en el formulario del original.
  Se sustituye la política de keepalive de CL4.5; el tope independiente de respuestas se conserva.
- **Código:** `Client::App::ServerSession`, con `KeepalivePeriod` y `ReplyTimeout`
  (`Client/src/App/ServerSession.h`).

## Contexto

Las dos filas son el reloj de la sesión: lo que la `ServerSession` manda y cuánto espera según el
tiempo, con el mismo reloj, decididas juntas en la CL4.5. Por eso van en un ADR.

- **Q-C53, el keepalive.** El original lo manda desde dos lugares que no se conocen. Un timer de
  20 s (`SetTimer` en `0x4bad89`), puesto al arrancar y nunca detenido, manda uno si hay conexión
  al GameServer, sin mirar si hubo login. Y `0x4ad100` manda otro al aceptar el login, sin
  reiniciar el timer: el segundo llega a menos de 20 s del primero, a los 7,5 s en la sesión real.
  Está *verificado* en el cable: en 2 de las 8 conexiones al GS del `muproxy.log`, el primer C3 es
  un keepalive antes del login, y una conexión sin login manda 34.
- **Q-C56, las esperas.** El research no le vio un tope a ninguna espera de la sesión: el saludo
  del ConnectServer, la lista, la dirección del GameServer, el saludo del GS y el resultado del
  login. Con lo leído en el 02 y el 04, un servidor que acepta la conexión y no contesta deja al
  cliente esperando, sin aviso (*a confirmar*).

## Decisión

- **El timer periódico empieza al construir la sesión**, antes de abrir conexiones. Se consulta
  también sin GameServer para consumir los vencimientos. Cada 20 s envía si la conexión al GS está
  abierta: antes del saludo, en el formulario, durante avisos o rechazos, esperando resultado y
  después de autenticar. No envía al CS ni por una conexión cerrada o pendiente.
- **Aceptar el login suma un keepalive inmediato sin reiniciar el timer.** Cambiar de estado,
  cancelar o reconectar tampoco reinicia su fase. Los envíos previos usan el mismo serial C3 de
  la conexión que el login. Sin personaje, velocidades 0/0 sin ajuste.
- **Se conserva `IntervalTimer` y el Poll del bucle**, también minimizado. Un frame largo consume
  un vencimiento, sin ráfaga. Es la política portable de coalescencia existente: no demuestra
  igualdad del orden de mensajes o del scheduler de `WM_TIMER`. La fase empieza en la construcción
  de la sesión, no en un instante absoluto sincronizado con otra ejecución del original.
- **Cada espera de un servidor tiene un tope de 30 s** (`ReplyTimeout`), contado desde que
  empezó. Si pasa sin la respuesta, la sesión cierra y queda en `Failed` con `NoReply`, y el log
  dice qué servidor y qué se esperaba. Se mira después de leer lo que llegó, así una respuesta que
  llegó durante un frame largo se lee y no cuenta como tarde. Son los estados
  `WaitingConnectHello`, `WaitingServerList`, `WaitingServerAddress`, `WaitingGameHello` y
  `WaitingLoginResult`.
- **Lo que espera al jugador no tiene tope:** `ChoosingServer` (elegir un servidor de la lista) y
  `WaitingCredentials` (la cuenta y la clave), desde la CL5.5. `LoggedIn` tampoco: el GS puede no
  mandar nada por un rato. Los `Connecting*` siguen con el `ConnectTimeout` de la conexión.

## Consecuencias

- **Se recupera:** el tráfico prelogin verificado en el original y el segundo keepalive a menos
  de 20 s del inmediato de aceptación. La política anterior omitía ese tráfico por decisión de
  CL4.5. Sus tests usaban un socket que no caducaba por inactividad; no comprobaban una espera
  humana prolongada contra un servidor que sí cierra por falta de tráfico.
- **Se conserva una diferencia:** un servidor real que tardara más de 30 s en contestar haría
  fallar a MuRet donde el original esperaría (Q-C56). Ese plazo no se aplica al formulario.
- **Límite de la comprobación:** el log de MuRet registra cierre remoto con el formulario abierto,
  pero no demuestra el motivo del GS. La prueba de integración usa una expiración controlada del
  doble, no un plazo medido del original. Queda comprobar el cliente corregido frente al mismo GS
  real, dejándolo en el formulario y autenticando después. Un keepalive tampoco impide un cierre
  remoto, un error de transporte ni una orden `F1 02/0`.
- **Integración futura del personaje:** `SetKeepaliveSpeeds` aún no tiene llamador productivo y
  conserva por contrato sus valores al llamar a `Start`. Los ceros del personaje inicial no fijan
  qué hace el original al volver del mundo al login. Esa transición se debe contrastar cuando se
  conecten las velocidades del personaje; no se introduce un reset sin evidencia en este arreglo.
- **Lo fijan:**
  - Los tests de `ServerSession` cubren envíos prelogin, fase independiente de la autenticación y
    reconexión, seriales, exclusión del CS y frames largos; `FakeServers` acepta keepalives antes
    del login y comprueba una espera prolongada frente a expiración controlada y su caso sin tráfico.
  - `ServerSessionTest.EachWaitCountsFromItsStart`, `AConnectServerThatDoesNotGreetFails`,
    `AConnectServerThatDoesNotGiveTheAddressFails`, `AGameServerThatDoesNotGreetFails`,
    `AGameServerThatDoesNotAnswerTheLoginFails` y `AReplyThatArrivedDuringALongFrameIsRead`.
  - `ServerSessionTest.WaitsForTheChoiceAfterTheList` y `WaitsForTheCredentialsAfterTheGameHello`:
    60 s esperando al jugador no cierran la sesión; con el GS abierto salen keepalives.

El contrato y los pasos están en la sub-ola CL4.5 del
[plan de la CL4](../superpowers/plans/2026-11-02-ola-cl4-red-del-cliente.md), y los estados que
esperan al jugador, en la CL5.5 del
[plan de la CL5](../superpowers/plans/2026-11-02-ola-cl5-render-2d-y-login.md).
