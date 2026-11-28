#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/SceneId.h"

#include <optional>
#include <string>

// A scene that does nothing: the selection's place in the tests that only look at how it is reached or what the
// session does meanwhile. The real one (CL6.3) asks for the list on entering.
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
    void Render(Render::Frame::SceneCanvas& /*canvas*/, float /*alpha*/) override {}
    void Leave() override {}
    [[nodiscard]] std::optional<Kernel::SceneId> Next() const override
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
