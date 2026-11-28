#include "App/AutoLogin.h"

#include "Session/Credentials.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
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
    case Session::SessionState::ChoosingServer:
        Choose();
        return;
    case Session::SessionState::WaitingCredentials:
        Submit();
        return;
    case Session::SessionState::Idle:
    case Session::SessionState::ConnectingConnectServer:
    case Session::SessionState::WaitingConnectHello:
    case Session::SessionState::WaitingServerList:
    case Session::SessionState::WaitingServerAddress:
    case Session::SessionState::ConnectingGameServer:
    case Session::SessionState::WaitingGameHello:
    case Session::SessionState::WaitingLoginResult:
    case Session::SessionState::LoggedIn:
    case Session::SessionState::WaitingCharacterList:
    case Session::SessionState::ChoosingCharacter:
    case Session::SessionState::WaitingCreateResult:
    case Session::SessionState::WaitingDeleteResult:
    case Session::SessionState::EnteringWorld:
    case Session::SessionState::InWorld:
    case Session::SessionState::QuitOrdered:
    case Session::SessionState::Failed:
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
