#include "App/SessionClient.h"

namespace Client::App
{
SessionClient::SessionClient(ServerSession& session, ILoopClient& scene) : m_session(session), m_scene(scene) {}

void SessionClient::Poll()
{
    m_session.Poll();
    m_scene.Poll();
}

void SessionClient::Step(const Input::InputState& input)
{
    m_scene.Step(input);
}

void SessionClient::Render(float alpha)
{
    m_scene.Render(alpha);
}
} // namespace Client::App
