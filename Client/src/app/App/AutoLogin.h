#pragma once

#include "Session/Credentials.h"
#include "Session/ServerSession.h"
#include "Core/Log.h"

#include <cstdint>

// What CL4's ServerSession did without a UI, split out (CL5.5; question 12 of the CL5 plan): with --auto-login, Main
// builds it and queries it in the SceneManager's pollNetwork, after the session. In ChoosingServer it picks the first
// of the list, in the order it arrived (question 9 of the CL4 plan), and in WaitingCredentials it sends the account
// and password from the environment (question 7). It serves for manual testing and for debugging the network without
// the UI.
//
// Once each per round (ServerSession::StartCount): if in the same round the session waits again, for a busy server
// (Q-C71) or a rejected login, the choice or the account are left to the UI, with a warning; choosing the same one or
// sending the same password would give the same again. The LoginScene does not know it: since it picks and sends in
// the same pollNetwork that brings the list or the hello, the scene does not see the session waiting and does not
// open its windows, except in those two cases.
namespace Client::App
{
class AutoLogin
{
public:
    // `session` and `log` must outlive this. The account and password do not go to the log.
    AutoLogin(ServerSession& session, Credentials credentials, Core::Log& log);

    AutoLogin(const AutoLogin&) = delete;
    AutoLogin& operator=(const AutoLogin&) = delete;
    ~AutoLogin() = default;

    // Once per frame, after ServerSession::Poll.
    void Poll();

private:
    // What was done in one round of the session.
    struct Round
    {
        std::uint32_t start = 0;
        bool chose = false;
        bool submitted = false;
        bool warnedChoice = false;
        bool warnedSubmit = false;
    };

    void Choose();
    void Submit();

    ServerSession& m_session;
    Credentials m_credentials;
    Core::Log& m_log;
    Round m_round;
};
} // namespace Client::App
