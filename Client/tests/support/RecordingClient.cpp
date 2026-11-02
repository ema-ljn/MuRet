#include "support/RecordingClient.h"

namespace Client::Tests
{
RecordingClient::RecordingClient(FakeClock& clock) : m_clock(clock) {}

void RecordingClient::Step(const Input::InputState& input)
{
    ++steps;
    sawPress.push_back(input.WasPressed(watchedKey));
    m_clock.Advance(stepCost);
}

void RecordingClient::Render(float alpha)
{
    alphas.push_back(alpha);
    m_clock.Advance(renderCost);
}
} // namespace Client::Tests
