#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"

#include <optional>
#include <string>

// La escena 4 de la CL5: adonde va el login aceptado, para que la máquina de escenas tenga la
// transición entera. No dibuja nada y no manda el F3 00: la selección de verdad (0x6342c0, con su
// mundo, el 54 de SceneWorlds.h, y la lista de personajes) es de la CL6.3 (P-C06).
namespace Client::Scenes
{
class CharacterSelectScene final : public IScene
{
public:
    Core::Expected<void, std::string> Enter() override;
    void Step(const Input::InputState& input) override;
    void Render(SceneCanvas& canvas, float alpha) override;
    void Leave() override;
    [[nodiscard]] std::optional<SceneId> Next() const override;
    // Sin sus avisos ni sus campos todavía (CL6): nunca, las dos.
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
};
} // namespace Client::Scenes
