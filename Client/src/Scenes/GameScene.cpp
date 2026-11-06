#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "World/GameCamera.h"

#include <optional>
#include <string>

namespace Client::Scenes
{
GameScene::GameScene(const SceneContext& context, App::ServerSession* session) : m_context(context), m_session(session)
{
}

Core::Expected<void, std::string> GameScene::Enter()
{
    m_firstStep = true;
    return {};
}

// 0x636290: el primer paso de la escena 5 manda el F3 03 con el elegido.
void GameScene::Step(const Input::InputState& /*input*/)
{
    // 0x636838: solo el primer paso de la escena 5 escribe el yaw, sin reiniciar los otros globales.
    if (m_firstStep)
    {
        if (m_context.camera != nullptr)
            m_context.camera->yaw = -45.0f;
        m_firstStep = false;
    }
    if (m_context.cloth != nullptr)
    {
        Render::Models::StepTheClothWorld(*m_context.cloth);
    }
    if (m_sent || m_session == nullptr)
    {
        return;
    }
    m_sent = true;
    m_session->EnterWorld();
}

void GameScene::Render(SceneCanvas& /*canvas*/, float /*alpha*/) {}

void GameScene::Leave() {}

// Q-C70: sin conexión, el juego sigue; el texto 3 del área de mensajes es de la CL9.
std::optional<SceneId> GameScene::Next() const
{
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
