#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Assets/GameModels.h"
#include "Scenes/IScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"

#include <optional>
#include <string>

// Scene 1: the loading of 0x622e40, with its UI from 0x65eeb0 and the frame of 0x65f200. The original draws it alone,
// outside the loop: eleven frames, one per advance of 0x61ccb0 (06 § La carga). MuRet advances one per step, reads the
// character and item models at advance 1 (main.cpp reads Text.bmd before, like WinMain) and then asks for the login.
namespace Client::Scenes
{
// The total of 0x640580(actual, 10): the bar goes from 0 to 10.
inline constexpr int LoadingAdvances = 10;

class LoadingScene final : public IScene
{
public:
    // `models` must outlive the scene; with nullptr, the loading reads no models (the CL5 tests).
    explicit LoadingScene(const SceneContext& context, Assets::GameModels* models = nullptr);

    // Loads its set (LoadingTextureSet).
    Core::Expected<void, std::string> Enter() override;
    // One advance, up to LoadingAdvances; the next step ends the loading.
    void Step(const Input::InputState& input) override;
    // The normal blend (0x620a00(1), which 0x622e40 leaves before 0x61ccb0), the five images in 0x65f200's order and
    // the bar with the advance.
    void Render(Render::Frame::SceneCanvas& canvas, float alpha) override;
    // Releases its six slots, like 0x63c150 at the end of 0x622e40.
    void Leave() override;
    // The login, after the last advance (0x622f93).
    [[nodiscard]] std::optional<Kernel::SceneId> Next() const override;
    // If the models could not be read (Player.bmd missing): the error is already in the log.
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

    // The `actual` of 0x640580: from 0 to LoadingAdvances.
    [[nodiscard]] int Progress() const;

private:
    [[nodiscard]] Core::Expected<void, std::string> LoadModels();
    SceneContext m_context;
    Assets::GameModels* m_models;
    Assets::SceneTextures m_textures;
    bool m_failed = false;
    int m_progress = 0;
    bool m_done = false;
};
} // namespace Client::Scenes
