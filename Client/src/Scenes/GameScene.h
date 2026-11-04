#pragma once

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"

#include <optional>
#include <string>

// La escena 5 mínima de la CL6.3: en su primer paso manda el F3 03 (0x636290,
// ServerSession::EnterWorld). No dibuja nada hasta la CL7, que lee la respuesta (0x4c3940) y pone el
// mundo y el personaje.
namespace Client::Scenes
{
class GameScene final : public IScene
{
public:
    // `session` tiene que vivir más; nullptr sin servidor.
    GameScene(const SceneContext& context, App::ServerSession* session);

    Core::Expected<void, std::string> Enter() override;
    void Step(const Input::InputState& input) override;
    void Render(SceneCanvas& canvas, float alpha) override;
    void Leave() override;
    // Ninguna: sin conexión, el juego sigue (Q-C70).
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    SceneContext m_context;
    App::ServerSession* m_session;
    bool m_sent = false;
};
} // namespace Client::Scenes
