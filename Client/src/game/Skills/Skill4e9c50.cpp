#include "Skills/Skill4e9c50.h"

#include "Kernel/ActorState.h"

#include <cstdint>

namespace Client::Skills
{
namespace
{
constexpr std::uint8_t BothHandsAction = 0x63;
constexpr std::uint8_t RightHandAction = 0x64;
constexpr std::uint8_t LeftHandAction = 0x65;
constexpr std::uint8_t NoHandAction = 0x62;
} // namespace

std::uint8_t ActionByTheHands(const Kernel::ActorState& by)
{
    // 0x4e9c56..0x4e9cbd: the two WORDs against −1.
    const bool right = by.hands[0].model != Kernel::NoItemModel;
    const bool left = by.hands[1].model != Kernel::NoItemModel;
    if (right && left)
        return BothHandsAction;
    if (right)
        return RightHandAction;
    if (left)
        return LeftHandAction;
    return NoHandAction;
}
} // namespace Client::Skills
