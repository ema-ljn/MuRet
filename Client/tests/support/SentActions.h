#pragma once

#include "Actors/ActionSend.h"

#include <cstdint>
#include <ostream>
#include <vector>

// The C1 18 an Actors::ActionSender sends in a test: the direction and the action of each, in order. A struct and not a
// std::pair: building a pair of bytes from int literals goes through the forwarding constructor, and MSVC warns about
// the conversion inside the STL (C4242).
namespace Client::Tests
{
struct SentAction
{
    std::uint8_t direction = 0;
    std::uint8_t action = 0;

    friend bool operator==(const SentAction&, const SentAction&) = default;
};

inline void PrintTo(const SentAction& sent, std::ostream* out)
{
    *out << "{dir " << int{sent.direction} << ", acción " << int{sent.action} << "}";
}

// An ActionSender that records each C1 18 in `sent`.
inline Actors::ActionSender RecordingActions(std::vector<SentAction>& sent)
{
    return [&sent](std::uint8_t direction, std::uint8_t action) { sent.push_back(SentAction{direction, action}); };
}
} // namespace Client::Tests
