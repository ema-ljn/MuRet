#pragma once

#include "App/Credentials.h"
#include "App/ObfuscationMode.h"
#include "App/SessionState.h"

#include <ostream>

// Cómo imprime GoogleTest los tipos de Client::App en un mensaje, como NetworkPrinters.h. Las
// Credentials salen con el largo de cada campo y nunca con el texto (pregunta 7 del plan de la
// CL4): sin este PrintTo, GoogleTest vuelca los bytes del struct, y con SSO la clave va ahí.
namespace Client::App
{
inline void PrintTo(const Credentials& credentials, std::ostream* out)
{
    *out << "Credentials{cuenta de " << credentials.account.size() << " bytes, clave de " << credentials.password.size()
         << " bytes}";
}

inline void PrintTo(ObfuscationMode mode, std::ostream* out)
{
    switch (mode)
    {
    case ObfuscationMode::FoxAnti:
        *out << "FoxAnti";
        return;
    case ObfuscationMode::None:
        *out << "None";
        return;
    }
    *out << "ObfuscationMode fuera del enum";
}

inline void PrintTo(SessionState state, std::ostream* out)
{
    switch (state)
    {
    case SessionState::Idle:
        *out << "Idle";
        return;
    case SessionState::ConnectingConnectServer:
        *out << "ConnectingConnectServer";
        return;
    case SessionState::WaitingConnectHello:
        *out << "WaitingConnectHello";
        return;
    case SessionState::WaitingServerList:
        *out << "WaitingServerList";
        return;
    case SessionState::WaitingServerAddress:
        *out << "WaitingServerAddress";
        return;
    case SessionState::ConnectingGameServer:
        *out << "ConnectingGameServer";
        return;
    case SessionState::WaitingGameHello:
        *out << "WaitingGameHello";
        return;
    case SessionState::WaitingLoginResult:
        *out << "WaitingLoginResult";
        return;
    case SessionState::LoggedIn:
        *out << "LoggedIn";
        return;
    case SessionState::Failed:
        *out << "Failed";
        return;
    }
    *out << "SessionState fuera del enum";
}

inline void PrintTo(SessionFailure failure, std::ostream* out)
{
    switch (failure)
    {
    case SessionFailure::ConnectServerLost:
        *out << "ConnectServerLost";
        return;
    case SessionFailure::EmptyServerList:
        *out << "EmptyServerList";
        return;
    case SessionFailure::GameServerLost:
        *out << "GameServerLost";
        return;
    case SessionFailure::HelloRejected:
        *out << "HelloRejected";
        return;
    case SessionFailure::VersionMismatch:
        *out << "VersionMismatch";
        return;
    case SessionFailure::LoginRejected:
        *out << "LoginRejected";
        return;
    case SessionFailure::CredentialsTooLong:
        *out << "CredentialsTooLong";
        return;
    }
    *out << "SessionFailure fuera del enum";
}
} // namespace Client::App
