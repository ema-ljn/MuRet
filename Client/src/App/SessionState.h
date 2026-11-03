#pragma once

#include <cstdint>

// En qué está la ServerSession (App/ServerSession.h) y por qué falló, aparte de la sesión: así los
// PrintTo de los tests (tests/support/AppPrinters.h) no traen la Connection ni el Dispatcher.
// Contrato: el plan de la CL4, "Interfaces" y el diagrama de estados de la CL4.3, paso 1, y la
// CL4.4, "Contrato".
namespace Client::App
{
enum class SessionState : std::uint8_t
{
    Idle,                    // sin Start
    ConnectingConnectServer, // Start abrió el CS
    WaitingConnectHello,     // el A9 salió; se espera C1 04 00 01
    WaitingServerList,       // F4 02 salió
    WaitingServerAddress,    // F4 03 salió
    ConnectingGameServer,    // el CS se cerró y se abrió el GS
    WaitingGameHello,        // el GS abrió; se espera su saludo, F1 00
    WaitingLoginResult,      // el login, F1 01, salió cifrado
    LoggedIn,                // el GS aceptó el login: sale el keepalive (CL4.5)
    Failed,
};

enum class SessionFailure : std::uint8_t
{
    ConnectServerLost,  // la conexión con el CS se cerró: el motivo va al log y queda en la conexión
    EmptyServerList,    // el CS no tiene ningún servidor
    GameServerLost,     // la conexión con el GS se cerró
    HelloRejected,      // el saludo del GS trae otro resultado que HelloAccepted
    VersionMismatch,    // el saludo aceptado trae otra versión que ClientVersion
    LoginRejected,      // el resultado del login no es LoginAccepted: queda en LoginResult()
    CredentialsTooLong, // la cuenta o la clave no entran en el login (WriteLoginRequest)
    NoReply,            // un estado Waiting* pasó ReplyTimeout sin su respuesta (CL4.5)
};
} // namespace Client::App
