#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"

#include <optional>
#include <string>

// Una escena que no hace nada: el lugar de la selección en los tests que solo miran cómo se llega a
// ella o qué hace la sesión mientras tanto, como hacía la selección vacía de la CL5. La de verdad
// (CL6.3) pide la lista al entrar.
namespace Client::Tests
{
class IdleScene final : public Scenes::IScene
{
public:
    Core::Expected<void, std::string> Enter() override
    {
        return {};
    }
    void Step(const Input::InputState& /*input*/) override {}
    void Render(Scenes::SceneCanvas& /*canvas*/, float /*alpha*/) override {}
    void Leave() override {}
    [[nodiscard]] std::optional<Scenes::SceneId> Next() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] bool WantsToQuit() const override
    {
        return false;
    }
    [[nodiscard]] bool WantsTextInput() const override
    {
        return false;
    }
    [[nodiscard]] bool HoldsTheLastFrame() const override
    {
        return false;
    }
};
} // namespace Client::Tests
