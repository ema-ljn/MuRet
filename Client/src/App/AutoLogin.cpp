#include "App/AutoLogin.h"

#include "App/Credentials.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Log.h"

#include <utility>

namespace Client::App
{
AutoLogin::AutoLogin(ServerSession& session, Credentials credentials, Core::Log& log)
    : m_session(session), m_credentials(std::move(credentials)), m_log(log)
{
}

void AutoLogin::Poll()
{
    if (m_session.StartCount() != m_round.start)
    {
        m_round = Round{.start = m_session.StartCount()};
    }
    switch (m_session.State())
    {
    case SessionState::ChoosingServer:
        Choose();
        return;
    case SessionState::WaitingCredentials:
        Submit();
        return;
    case SessionState::Idle:
    case SessionState::ConnectingConnectServer:
    case SessionState::WaitingConnectHello:
    case SessionState::WaitingServerList:
    case SessionState::WaitingServerAddress:
    case SessionState::ConnectingGameServer:
    case SessionState::WaitingGameHello:
    case SessionState::WaitingLoginResult:
    case SessionState::LoggedIn:
    case SessionState::QuitOrdered:
    case SessionState::Failed:
        return;
    }
}

void AutoLogin::Choose()
{
    if (!m_round.chose)
    {
        // Una lista vacía no llega a ChoosingServer: la sesión falla con EmptyServerList.
        if (m_session.ServerList().empty())
        {
            return;
        }
        m_log.Info("--auto-login elige el primero de la lista");
        m_round.chose = true;
        m_session.ChooseServer(m_session.ServerList().front().code);
        return;
    }
    if (!m_round.warnedChoice)
    {
        m_log.Warning("--auto-login ya eligió un servidor y estaba ocupado: la elección queda para la lista");
        m_round.warnedChoice = true;
    }
}

void AutoLogin::Submit()
{
    if (!m_round.submitted)
    {
        m_log.Info("--auto-login manda la cuenta y la clave del entorno");
        m_round.submitted = true;
        m_session.SubmitCredentials(m_credentials);
        return;
    }
    if (!m_round.warnedSubmit)
    {
        m_log.Warning("--auto-login ya mandó la cuenta y la clave, y el login se rechazó: la cuenta queda para "
                      "la ventana");
        m_round.warnedSubmit = true;
    }
}
} // namespace Client::App
