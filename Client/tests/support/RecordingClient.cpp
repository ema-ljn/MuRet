#include "support/RecordingClient.h"

#include "Input/InputState.h"
#include "support/FakeClock.h"

#include <ostream>

namespace Client::Tests
{
void PrintTo(LoopCall call, std::ostream* out)
{
    switch (call)
    {
    case LoopCall::Poll:
        *out << "Poll";
        return;
    case LoopCall::Step:
        *out << "Step";
        return;
    case LoopCall::Render:
        *out << "Render";
        return;
    }
    *out << "LoopCall fuera del enum";
}

RecordingClient::RecordingClient(FakeClock& clock) : m_clock(clock) {}

void RecordingClient::Poll()
{
    ++polls;
    calls.push_back(LoopCall::Poll);
    m_clock.Advance(pollCost);
}

void RecordingClient::Step(const Input::InputState& input)
{
    ++steps;
    calls.push_back(LoopCall::Step);
    sawPress.push_back(input.WasPressed(watchedKey));
    if (holdsAfterAStep)
    {
        holdsTheLastFrame = true;
    }
    if (changesTheSceneInAStep)
    {
        m_sceneChanged = true;
    }
    m_clock.Advance(stepCost);
}

void RecordingClient::Render(float alpha)
{
    calls.push_back(LoopCall::Render);
    alphas.push_back(alpha);
    m_clock.Advance(renderCost);
}

bool RecordingClient::HoldsTheLastFrame() const
{
    return holdsTheLastFrame;
}

void RecordingClient::FinishTheLoop()
{
    finishedLoops.push_back(calls.size());
}

bool RecordingClient::TakeTheSceneChange()
{
    const bool changed = m_sceneChanged;
    m_sceneChanged = false;
    return changed;
}
} // namespace Client::Tests
