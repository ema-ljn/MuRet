#include "Scenes/Skill57cf90.h"

#include "Scenes/GameMovementState.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t LastMagicKey = 0x32;
constexpr std::uint8_t FirstMagicKey = 1;
} // namespace

std::uint8_t NextMagicKey(GameMovementState& state, std::uint8_t& key)
{
    ++state.magicKeyCounter;
    if (state.magicKeyCounter > LastMagicKey)
        state.magicKeyCounter = FirstMagicKey;
    key = state.magicKeyCounter;
    return key;
}
} // namespace Client::Scenes
