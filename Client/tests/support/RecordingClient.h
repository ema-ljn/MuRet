#pragma once

#include "Scenes/ILoopClient.h"
#include "Core/IClock.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "support/FakeClock.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

// The loop tests' test client (CL3.2, CL3.3 and CL4.1).
namespace Client::Tests
{
// Each call the loop makes to the client, to check the order.
enum class LoopCall : std::uint8_t
{
    Poll,
    Step,
    Render,
};

// Without this, GoogleTest shows a vector of LoopCall as loose bytes.
void PrintTo(LoopCall call, std::ostream* out);

// Counts polls, steps and draws, records the order they arrive in, keeps each alpha, and makes each one "take" what
// it is told by moving the FakeClock: so a test builds a 130 ms frame, a 10 s breakpoint or a network that takes
// 20 ms on each poll.
class RecordingClient final : public Scenes::ILoopClient
{
public:
    explicit RecordingClient(FakeClock& clock);
    void Poll() override;
    void Step(const Input::InputState& input) override;
    void Render(float alpha) override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    [[nodiscard]] bool TakeTheSceneChange() override;
    void FinishTheLoop() override;

    Core::Duration pollCost{};
    Core::Duration stepCost{};
    Core::Duration renderCost{};
    std::uint64_t polls = 0;
    std::uint64_t steps = 0;
    std::vector<LoopCall> calls;
    // One per draw.
    std::vector<float> alphas;
    // One per FinishTheLoop: how many `calls` there were, to see it comes after the draw.
    std::vector<std::size_t> finishedLoops;
    // What HoldsTheLastFrame answers.
    bool holdsTheLastFrame = false;
    // Whether each step turns it on, like the one that enters the login and connects: so a test sees that the loop
    // looks at it after the steps.
    bool holdsAfterAStep = false;
    // Whether each step changes the scene, like the loading's last: TakeTheSceneChange says so once.
    bool changesTheSceneInAStep = false;
    // The key each step watches; Count, which is never pressed, if the test watches none.
    Input::Key watchedKey = Input::Key::Count;
    // One per step: whether that step saw watchedKey's press (WasPressed).
    std::vector<bool> sawPress;

private:
    FakeClock& m_clock;
    bool m_sceneChanged = false;
};
} // namespace Client::Tests
