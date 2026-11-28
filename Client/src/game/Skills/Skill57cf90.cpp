#include "Skills/Skill57cf90.h"

#include "Movement/GameMovementState.h"

#include <cstdint>

namespace Client::Skills
{
namespace
{
constexpr std::uint8_t LastMagicKey = 0x32;
constexpr std::uint8_t FirstMagicKey = 1;
} // namespace

std::uint8_t NextMagicKey(Movement::GameMovementState& state, std::uint8_t& key)
{
    ++state.magicKeyCounter;
    if (state.magicKeyCounter > LastMagicKey)
        state.magicKeyCounter = FirstMagicKey;
    key = state.magicKeyCounter;
    return key;
}
} // namespace Client::Skills
