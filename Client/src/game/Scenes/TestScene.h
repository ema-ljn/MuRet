#pragma once

#include "Scenes/ILoopClient.h"
#include "Input/InputState.h"

#include <cstdint>
#include <functional>

// The test scene: each step advances DAT_07d2d360's 32-step cycle, (n + 1) mod 32 (02, "Qué corre por paso"), and the
// background goes from a dark blue to a light one and back each cycle, 1.28 s. It sees neither SDL nor GL: whoever
// builds it passes how to paint the background and present (main, with the window's GL), and the tests, a recorder.
namespace Client::Scenes
{
class TestScene final : public Scenes::ILoopClient
{
public:
    // 0 to 1 per channel.
    struct Color
    {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
    };

    // `present` paints the whole background that color and presents (the swap goes inside Render, ILoopClient); it
    // cannot be empty. It is a function and not an interface, like MuMain's callbacks (Render/RmlUi), because the scene
    // asks for a single thing.
    explicit TestScene(std::function<void(const Color&)> present);

    // Empty: the test scene has no network.
    void Poll() override;
    // The test scene does not read the input.
    void Step(const Input::InputState& input) override;
    void Render(float alpha) override;
    // Never: it does not connect.
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // Never: it is a single scene.
    [[nodiscard]] bool TakeTheSceneChange() override;
    // The background between the current step and the next: `alpha` in [0, 1), like Render's.
    [[nodiscard]] Color Background(float alpha) const;

private:
    std::function<void(const Color&)> m_present;
    std::uint32_t m_cycle = 0;
};
} // namespace Client::Scenes
