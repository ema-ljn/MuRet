#pragma once

#include <cstdint>

// En qué está la ServerSession (App/ServerSession.h) y por qué falló, aparte de la sesión: así los
// PrintTo de los tests (tests/support/AppPrinters.h) no traen la Connection ni el Dispatcher.
// Contrato: el plan de la CL4 (CL4.3), "Interfaces" y el diagrama de estados del paso 1.
namespace Client::App
{
enum class SessionState : std::uint8_t
{
    Idle,                    // sin Start
    ConnectingConnectServer, // Start abrió el CS
    WaitingConnectHello,     // el A9 salió; se espera C1 04 00 01
    WaitingServerList,       // F4 02 salió
    WaitingServerAddress,    // F4 03 salió
    ConnectingGameServer,    // el CS se cerró y se abrió el GS; lo que sigue es de la CL4.4
    Failed,
};

enum class SessionFailure : std::uint8_t
{
    ConnectServerLost, // la conexión con el CS se cerró: el motivo va al log y queda en la conexión
    EmptyServerList,   // el CS no tiene ningún servidor
    GameServerLost,    // la conexión con el GS se cerró
};
} // namespace Client::App
