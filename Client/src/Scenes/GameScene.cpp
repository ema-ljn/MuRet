#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"

#include <optional>
#include <string>

namespace Client::Scenes
{
GameScene::GameScene(const SceneContext& context, App::ServerSession* session) : m_context(context), m_session(session)
{
}

Core::Expected<void, std::string> GameScene::Enter()
{
    return {};
}

// 0x636290: el primer paso de la escena 5 manda el F3 03 con el elegido.
void GameScene::Step(const Input::InputState& /*input*/)
{
    if (m_sent || m_session == nullptr)
    {
        return;
    }
    m_sent = true;
    m_session->EnterWorld();
}

void GameScene::Render(SceneCanvas& /*canvas*/, float /*alpha*/) {}

void GameScene::Leave() {}

std::optional<SceneId> GameScene::Next() const
{
    if (m_session != nullptr && m_session->State() == App::SessionState::Failed)
    {
        return SceneId::Login;
    }
    return std::nullopt;
}

bool GameScene::WantsToQuit() const
{
    return false;
}

bool GameScene::WantsTextInput() const
{
    return false;
}

bool GameScene::HoldsTheLastFrame() const
{
    return false;
}
} // namespace Client::Scenes
