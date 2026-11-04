#pragma once

#include <cstdint>

// En qué está la ServerSession (App/ServerSession.h) y por qué falló, aparte de la sesión: así los
// PrintTo de los tests (tests/support/AppPrinters.h) no traen la Connection ni el Dispatcher.
// Contrato: el plan de la CL4, "Interfaces" y el diagrama de estados de la CL4.3, paso 1, y la
// CL4.4, "Contrato"; los dos estados que esperan a la UI, el de la CL5 (CL5.5); y los de la
// selección de personaje, el de la CL6 (CL6.3, pregunta 9). Un servidor ocupado y un login
// rechazado no son fallas desde la CL5.5: la sesión vuelve a esperar. La orden de cerrar el juego
// tampoco es una falla: no tiene aviso (Q-C106).
namespace Client::App
{
enum class SessionState : std::uint8_t
{
    Idle,                    // sin Start
    ConnectingConnectServer, // Start abrió el CS
    WaitingConnectHello,     // el A9 salió; se espera C1 04 00 01
    WaitingServerList,       // F4 02 salió
    ChoosingServer,          // la lista llegó; se espera ChooseServer (CL5.5)
    WaitingServerAddress,    // F4 03 salió
    ConnectingGameServer,    // el CS se cerró y se abrió el GS
    WaitingGameHello,        // el GS abrió; se espera su saludo, F1 00
    WaitingCredentials,      // el GS saludó; se espera SubmitCredentials (CL5.5)
    WaitingLoginResult,      // el login, F1 01, salió cifrado
    LoggedIn,                // el GS aceptó el login: sale el keepalive (CL4.5)
    WaitingCharacterList,    // F3 00 salió (el 0x32 del original)
    ChoosingCharacter,       // la lista llegó (0x33); se espera EnterWorld (CL6.3)
    EnteringWorld,           // F3 03 salió (0x3c); su respuesta es de la CL7: hasta ahí, sin tope
    QuitOrdered,             // el GS ordenó cerrar el juego (F1 02 con 0): la conexión se cerró (Q-C106)
    Failed,
};

enum class SessionFailure : std::uint8_t
{
    ConnectServerLost,  // la conexión con el CS se cerró: el motivo va al log y queda en la conexión
    EmptyServerList,    // el CS no tiene ningún servidor
    GameServerLost,     // la conexión con el GS se cerró
    HelloRejected,      // el saludo del GS trae otro resultado que HelloAccepted
    VersionMismatch,    // el saludo aceptado trae otra versión que ClientVersion
    CredentialsTooLong, // la cuenta o la clave no entran en el login (WriteLoginRequest)
    NoReply,            // un estado Waiting* pasó ReplyTimeout sin su respuesta (CL4.5)
};
} // namespace Client::App
