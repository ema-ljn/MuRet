#pragma once

#include "Kernel/SceneId.h"

#include <algorithm>
#include <array>

// The original's scene transitions: the binary's eight `mov` to DAT_006d5dc8, all with a fixed number (02 § Las cinco
// escenas y sus transiciones; read again in CL5.4, *static*). The SceneManager changes scene only by one of these;
// another is an error in the log and changes nothing.
namespace Client::Scenes
{
// In the .data of c32bbd5f, DAT_006d5dc8 is 1: the game starts in the loading.
inline constexpr Kernel::SceneId FirstScene = Kernel::SceneId::Loading;

struct SceneTransition
{
    Kernel::SceneId from = Kernel::SceneId::Loading;
    Kernel::SceneId to = Kernel::SceneId::Loading;
};

// By origin scene, and where the original writes it. The bottom four are from 0x4c2710, the F1 02's encrypted reply:
// with pkt[4] at 1 it goes to 4 (0x4c363f) and with 2, to 2 (0x4c2f1a). The original does not look at which scene it
// is in (only whether it is 5, to clean the game): it would also change it in the login or the world loading. MuRet
// keeps the ones that make sense, from the game and from the selection (Q-C79).
inline constexpr std::array<SceneTransition, 8> OriginalTransitions = {{
    {Kernel::SceneId::Loading, Kernel::SceneId::Login},         // 0x622f93, at the end of 0x622e40
    {Kernel::SceneId::Login, Kernel::SceneId::CharacterSelect}, // 0x634dce, in 0x634c60: the login accepted
    {Kernel::SceneId::CharacterSelect,
     Kernel::SceneId::WorldLoading},                           // 0x633f05 (0x633c10), 0x634470 and 0x634638 (0x6342c0)
    {Kernel::SceneId::WorldLoading, Kernel::SceneId::Game},    // 0x635a91, in 0x6356f0
    {Kernel::SceneId::Game, Kernel::SceneId::CharacterSelect}, // 0x4c363f
    {Kernel::SceneId::CharacterSelect, Kernel::SceneId::CharacterSelect},
    {Kernel::SceneId::Game, Kernel::SceneId::Login}, // 0x4c2f1a
    {Kernel::SceneId::CharacterSelect, Kernel::SceneId::Login},
}};

[[nodiscard]] constexpr bool IsOriginalTransition(Kernel::SceneId from, Kernel::SceneId to)
{
    return std::any_of(OriginalTransitions.begin(), OriginalTransitions.end(),
                       [from, to](const SceneTransition& transition)
                       { return transition.from == from && transition.to == to; });
}
} // namespace Client::Scenes
