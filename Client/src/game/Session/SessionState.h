#pragma once

#include <cstdint>

// What the ServerSession (game/Session/ServerSession.h) is doing and why it failed, apart from the session: so the
// tests' PrintTo (tests/support/AppPrinters.h) do not bring in the Connection or the Dispatcher. Contract: the CL4
// plan, "Interfaces" and the CL4.3 state diagram, step 1, and CL4.4, "Contrato"; the two states that wait for the
// UI, CL5's (CL5.5); the character selection ones, CL6's (CL6.3, question 9); and the world one, CL7's (CL7.L1, the
// 0x3d of 06 § La entrada al mundo). A busy server and a rejected login are not failures since CL5.5: the session
// waits again. The order to close the game is not a failure either: it has no notice (Q-C106).
namespace Client::Session
{
enum class SessionState : std::uint8_t
{
    Idle,                    // no active connection; also F1/02=2 before entering the login
    ConnectingConnectServer, // Start opened the CS
    WaitingConnectHello,     // the A9 went out; C1 04 00 01 expected
    WaitingServerList,       // F4 02 went out
    ChoosingServer,          // the list arrived; ChooseServer expected (CL5.5)
    WaitingServerAddress,    // F4 03 went out
    ConnectingGameServer,    // the CS closed and the GS opened
    WaitingGameHello,        // the GS opened; its hello, F1 00, expected
    WaitingCredentials,      // the GS said hello; SubmitCredentials expected (CL5.5)
    WaitingLoginResult,      // the login, F1 01, went out encrypted
    LoggedIn,                // the GS accepted the login: the keepalive goes out (CL4.5)
    WaitingCharacterList,    // F3 00 went out (the original's 0x32)
    ChoosingCharacter,       // the list arrived (0x33); EnterWorld expected (CL6.3)
    WaitingCreateResult,     // F3 01 went out (0x34); its reply goes back to ChoosingCharacter (CL6.5)
    WaitingDeleteResult,     // F3 02 went out (0x38); its reply goes back to ChoosingCharacter (CL6.5)
    EnteringWorld,           // F3 03 went out (0x3c); no timeout: the applied reply moves to InWorld
    InWorld,                 // the F3 03 reply published the 0x3d (0x4c4854); no timeout
    QuitOrdered,             // the GS ordered the game closed (F1 02 with 0): the connection closed (Q-C106)
    Failed,
};

enum class SessionFailure : std::uint8_t
{
    ConnectServerLost,  // the CS connection closed: the reason goes to the log and stays in the connection
    EmptyServerList,    // the CS has no server
    GameServerLost,     // the GS connection closed
    HelloRejected,      // the GS hello carries a result other than HelloAccepted
    VersionMismatch,    // the accepted hello carries a version other than ClientVersion
    CredentialsTooLong, // the account or the password does not fit in the login (WriteLoginRequest)
    ApplicationFailed,  // the synchronous consumer could not apply a receive
    NoReply,            // a Waiting* state passed ReplyTimeout without its reply (CL4.5)
};
} // namespace Client::Session
