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

// La escena 1 (CL5.4): la carga de 0x622e40, con su UI de 0x65eeb0 (cinco imágenes y la barra, en
// 800 × 600) y el frame de 0x65f200. El original dibuja sola, fuera del bucle: once frames, uno por
// avance de 0x61ccb0 (06 § La carga). MuRet no carga nada en ella todavía (Text.bmd lo lee main.cpp
// antes, como WinMain), así que avanza uno por paso y al paso que sigue al último pide el login.
namespace Client::Scenes
{
// El total de 0x640580(actual, 10): la barra va de 0 a 10.
inline constexpr int LoadingAdvances = 10;

class LoadingScene final : public IScene
{
public:
    explicit LoadingScene(const SceneContext& context);

    // Sube su juego (LoadingTextureSet).
    Core::Expected<void, std::string> Enter() override;
    // Un avance, hasta LoadingAdvances; el paso siguiente termina la carga.
    void Step(const Input::InputState& input) override;
    // La mezcla normal (0x620a00(1), que 0x622e40 deja antes de 0x61ccb0), las cinco imágenes en
    // el orden de 0x65f200 y la barra con el avance.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta sus seis slots, como 0x63c150 al final de 0x622e40.
    void Leave() override;
    // El login, después del último avance (0x622f93).
    [[nodiscard]] std::optional<SceneId> Next() const override;
    // La carga no tiene ventanas: nunca, las dos.
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;

    // El `actual` de 0x640580: de 0 a LoadingAdvances.
    [[nodiscard]] int Progress() const;

private:
    SceneContext m_context;
    SceneTextures m_textures;
    int m_progress = 0;
    bool m_done = false;
};
} // namespace Client::Scenes
