#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"

#include <optional>
#include <string>

// Scene 3, the world loading (0x6356f0): sets the wait counter to 9999999 and raises the scene's flag (0x7d3a13e),
// loads LSBg01 to 04, draws one frame with the four in a 2 × 2 mosaic in 800 × 600 and asks for the game. That frame
// stays on screen while the game waits for the F3 03 reply (GameScene). The original does everything in the same
// call; here the game goes in the step after the first frame, one later. 0x553570(1), which at the end empties the
// UI's text fields (0x7798bf8), belongs to the UI (CL8).
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "La carga del mundo (escena 3)" and "La entrada al
// mundo", step 2.
namespace Client::Scenes
{
class WorldLoadingScene final : public IScene
{
public:
    // `context.world` and `context.cloth` (the rand() the counter's close spends) must be there.
    explicit WorldLoadingScene(const SceneContext& context);

    // With the flag at 0: the counter, the flag and the four textures, in 0x6356f0's order. With the flag already at 1,
    // the error: it is a branch the original does not reach (06, "La carga del mundo").
    Core::Expected<void, std::string> Enter() override;
    // After the first frame, the game.
    void Step(const Input::InputState& input) override;
    // The four, with the scale W / 800 and H / 600, like the loading's UI.
    void Render(Render::Frame::SceneCanvas& canvas, float alpha) override;
    void Leave() override;
    [[nodiscard]] std::optional<Kernel::SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    SceneContext m_context;
    Assets::SceneTextures m_textures;
    bool m_drawn = false;
    bool m_done = false;
};
} // namespace Client::Scenes
