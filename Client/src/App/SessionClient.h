#pragma once

#include "App/ILoopClient.h"
#include "App/ServerSession.h"
#include "Input/InputState.h"

// El cliente del bucle de Main con un servidor (el plan de la CL4, CL4.5): en cada Poll, la sesión
// y después la escena, así lo que la sesión leyó en el frame ya está cuando la escena lo mira; los
// pasos y el dibujo son de la escena. Sin servidor no se arma, y el bucle recibe la escena sola.
// Queda hasta que la LoginScene de la CL5.4 tenga la sesión.
namespace Client::App
{
class SessionClient final : public ILoopClient
{
public:
    // La sesión y la escena son de quien lo arma y tienen que vivir más que él.
    SessionClient(ServerSession& session, ILoopClient& scene);

    void Poll() override;
    void Step(const Input::InputState& input) override;
    void Render(float alpha) override;

private:
    ServerSession& m_session;
    ILoopClient& m_scene;
};
} // namespace Client::App
