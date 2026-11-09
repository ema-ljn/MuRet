#pragma once

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"

#include <cstdint>
#include <optional>
#include <string>

// La escena 5: en su primer paso manda el F3 03 (0x636290, ServerSession::EnterWorld) y después espera
// el mundo. Mientras el contador de la espera pasa de 30, el dibujo (0x6371a0) no dibuja ni presenta y
// queda en pantalla el mosaico de la escena 3; el paso corre con lo que dejó la selección y MoveHero
// baja el contador (0x59af01). La respuesta del F3 03 lo pone en 30 (T7). Lo que dibuja el juego y el
// resto de su paso son de la T8.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La entrada al mundo", pasos 3, 4 y 6, "El
// paso del juego" y "El dibujo del juego".
namespace Client::Scenes
{
class GameScene final : public IScene
{
public:
    // `session` tiene que vivir más; nullptr sin servidor.
    GameScene(const SceneContext& context, App::ServerSession* session);

    // Sin `context.world` o `context.cloth`, el error: la espera vive en el primero y sus cierres
    // gastan el rand() del segundo.
    Core::Expected<void, std::string> Enter() override;
    void Step(const Input::InputState& input) override;
    // El pase del juego, desde 0x63723a, todavía no está (T8): con el frame no retenido no dibuja nada.
    void Render(SceneCanvas& canvas, float alpha) override;
    // 0x6371a6..0x63722e: sin el mundo cargado retiene sin leer; con él, lee el contador y retiene
    // con más de 30.
    void PrepareFrame() override;
    void Leave() override;
    // Ninguna: sin conexión, el juego sigue (Q-C70).
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    // Lo que decidió el último PrepareFrame; antes del primero, retiene.
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    // 0x6362b2..0x63693a: solo el primer paso de la escena.
    void StepEntry();
    // 0x636da3..0x63717f: el paso con el mundo cargado.
    void StepWorld();
    // 0x59ae8d..0x59afd6, lo de MoveHero con el contador: lo baja y lo vuelve a leer.
    [[nodiscard]] std::int32_t StepWaitCounter();

    SceneContext m_context;
    App::ServerSession* m_session;
    bool m_sent = false;
    bool m_firstStep = true;
    bool m_holdFrame = true;
};
} // namespace Client::Scenes
