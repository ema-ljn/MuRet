#include "Scenes/Skill4e9c50.h"

#include "Scenes/ActorState.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t BothHandsAction = 0x63;
constexpr std::uint8_t RightHandAction = 0x64;
constexpr std::uint8_t LeftHandAction = 0x65;
constexpr std::uint8_t NoHandAction = 0x62;
} // namespace

std::uint8_t ActionByTheHands(const ActorState& by)
{
    // 0x4e9c56..0x4e9cbd: los dos WORD contra −1.
    const bool right = by.hands[0].model != NoItemModel;
    const bool left = by.hands[1].model != NoItemModel;
    if (right && left)
        return BothHandsAction;
    if (right)
        return RightHandAction;
    if (left)
        return LeftHandAction;
    return NoHandAction;
}
} // namespace Client::Scenes
