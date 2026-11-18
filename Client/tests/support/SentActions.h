#pragma once

#include "Scenes/ActionSend.h"

#include <cstdint>
#include <ostream>
#include <vector>

// Los C1 18 que manda un Scenes::ActionSender en un test (CL9 hito 1): la dirección y la acción de cada uno, en orden.
// Un struct y no un std::pair: armar un par de bytes con literales int pasa por el constructor que reenvía, y MSVC
// avisa la conversión adentro de la STL (C4242).
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

// Un ActionSender que anota cada C1 18 en `sent`.
inline Scenes::ActionSender RecordingActions(std::vector<SentAction>& sent)
{
    return [&sent](std::uint8_t direction, std::uint8_t action) { sent.push_back(SentAction{direction, action}); };
}
} // namespace Client::Tests
