#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"

#include <optional>
#include <string>

// La escena 3, la carga del mundo (0x6356f0): sube LSBg01 a 04, dibuja un frame con las cuatro en un
// mosaico de 2 × 2 en 800 × 600 y pide el juego. El original hace todo en la misma llamada; acá el
// juego va en el paso que sigue al primer frame, uno más tarde.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La carga del mundo (escena 3)".
namespace Client::Scenes
{
class WorldLoadingScene final : public IScene
{
public:
    explicit WorldLoadingScene(const SceneContext& context);

    Core::Expected<void, std::string> Enter() override;
    // Después del primer frame, el juego.
    void Step(const Input::InputState& input) override;
    // Las cuatro, con la escala W / 800 y H / 600, como la UI de la carga.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta las cuatro.
    void Leave() override;
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    SceneContext m_context;
    SceneTextures m_textures;
    bool m_drawn = false;
    bool m_done = false;
    bool m_drawMosaic = true;
};
} // namespace Client::Scenes
