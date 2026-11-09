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

// La escena 3, la carga del mundo (0x6356f0): arma el contador de la espera en 9.999.999 y sube la
// bandera de la escena (0x7d3a13e), sube LSBg01 a 04, dibuja un frame con las cuatro en un mosaico de
// 2 × 2 en 800 × 600 y pide el juego. Ese frame queda en pantalla mientras el juego espera la respuesta
// del F3 03 (GameScene). El original hace todo en la misma llamada; acá el juego va en el paso que
// sigue al primer frame, uno más tarde. 0x553570(1), que al final vacía los campos de texto de la UI
// (0x7798bf8), es de la UI (CL8).
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La carga del mundo (escena 3)" y "La
// entrada al mundo", paso 2.
namespace Client::Scenes
{
class WorldLoadingScene final : public IScene
{
public:
    // `context.world` y `context.cloth` (el rand() que gasta el cierre del contador) tienen que estar.
    explicit WorldLoadingScene(const SceneContext& context);

    // Con la bandera en 0: el contador, la bandera y las cuatro texturas, en el orden de 0x6356f0. Con
    // la bandera ya en 1, el error: es una rama que el original no alcanza (06, "La carga del mundo").
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
};
} // namespace Client::Scenes
