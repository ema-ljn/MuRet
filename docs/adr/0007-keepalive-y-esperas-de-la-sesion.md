# 0007. La sesión: el keepalive desde el login aceptado, y las esperas con tope de 30 s

- **Estado:** aceptado. Las dos son de Claude: la recomendación de la pregunta 6 del plan de la
  CL4, que Emanuel no cambió, y el tope, que resolvió en la CL4.5 (2026-11-02). Los estados que
  esperan al jugador son de la CL5.5 (2026-11-03).
- **Filas:** Q-C53 y Q-C56 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
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

- **El keepalive sale solo con el login aceptado.** El primero, al aceptarlo, como el original.
  Los siguientes, cada 20 s contados desde ese, mientras la sesión esté en `LoggedIn`, también con
  la ventana minimizada. Antes del login no sale ninguno, tampoco mientras se espera la cuenta y
  la clave. Si un frame tarda más (un breakpoint de 60 s), sale uno solo, y el siguiente a los
  20 s. Sin personaje, las velocidades van en 0 sin ajuste, como en el original.
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

- **Se gana:** un keepalive a un ritmo fijo, que es lo que pide el test del 02 ("cada 20 s de
  reloj simulado y no antes"), y que no sale antes de que el GameServer acepte el login. Un
  servidor que no contesta termina en un error con nombre, en lugar de una espera sin fin.
- **Se pierde:** el GameServer no ve el keepalive temprano que a veces manda el original antes del
  login, ni el segundo a menos de 20 s del primero. Nada indica que lo necesite: en el
  `muproxy.log`, solo 2 de los 7 logins salen después de un keepalive (04). Falta probarlo: el
  login de MuRet contra el GS del laboratorio, el paso 4 de la CL4.5, todavía no se corrió. Y un
  servidor real que tardara más de 30 s en contestar haría fallar a MuRet donde el original
  esperaría.
- **Lo fijan:**
  - `ServerSessionTest.NoKeepaliveBeforeTheLogin`, `KeepaliveEveryTwentySecondsAndNotBefore`,
    `ASixtySecondFrameSendsOneKeepalive` y `KeepsTheKeepaliveWhileMinimized`.
  - `ServerSessionTest.EachWaitCountsFromItsStart`, `AConnectServerThatDoesNotGreetFails`,
    `AConnectServerThatDoesNotGiveTheAddressFails`, `AGameServerThatDoesNotGreetFails`,
    `AGameServerThatDoesNotAnswerTheLoginFails` y `AReplyThatArrivedDuringALongFrameIsRead`.
  - `ServerSessionTest.WaitsForTheChoiceAfterTheList` y `WaitsForTheCredentialsAfterTheGameHello`:
    60 s esperando al jugador no cierran la sesión ni mandan un keepalive.

El contrato y los pasos están en la sub-ola CL4.5 del
[plan de la CL4](../superpowers/plans/2026-11-02-ola-cl4-red-del-cliente.md), y los estados que
esperan al jugador, en la CL5.5 del
[plan de la CL5](../superpowers/plans/2026-11-02-ola-cl5-render-2d-y-login.md).
