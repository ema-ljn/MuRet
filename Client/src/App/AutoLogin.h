#pragma once

#include "App/Credentials.h"
#include "App/ServerSession.h"
#include "Core/Log.h"

#include <cstdint>

// Lo que la ServerSession de la CL4 hacía sin UI, aparte (CL5.5; pregunta 12 del plan de la CL5):
// con --auto-login, Main lo arma y lo consulta en el pollNetwork del SceneManager, después de la
// sesión. En ChoosingServer elige el primero de la lista, en el orden en que llegó (pregunta 9 del
// plan de la CL4), y en WaitingCredentials manda la cuenta y la clave del entorno (pregunta 7).
// Sirve para la prueba a mano y para depurar la red sin la UI.
//
// Una vez cada una por vuelta (ServerSession::StartCount): si en la misma vuelta la sesión vuelve
// a esperar, por un servidor ocupado (Q-C71) o un login rechazado, la elección o la cuenta quedan
// para la UI, con un aviso; elegir el mismo o mandar la misma clave daría lo mismo otra vez. La
// LoginScene no lo conoce: como elige y manda en el mismo pollNetwork que trae la lista o el
// saludo, la escena no ve la sesión esperando y no abre sus ventanas, salvo en esos dos casos.
namespace Client::App
{
class AutoLogin
{
public:
    // `session` y `log` tienen que vivir más que esto. La cuenta y la clave no van al log.
    AutoLogin(ServerSession& session, Credentials credentials, Core::Log& log);

    AutoLogin(const AutoLogin&) = delete;
    AutoLogin& operator=(const AutoLogin&) = delete;
    ~AutoLogin() = default;

    // Una vez por frame, después de ServerSession::Poll.
    void Poll();

private:
    // Lo hecho en una vuelta de la sesión.
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
