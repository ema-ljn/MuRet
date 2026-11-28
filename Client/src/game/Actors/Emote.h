#pragma once

#include "Core/Expected.h"
#include "Actors/ActionSend.h"

#include <cstdint>
#include <string>

namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// A character's emote (06 § El chat, "El emote"): 0x576a60 and what each case of 0x576eb0 does with what
// Chat::FindTheEmote found. Neither draws a rand(), and a C1 does not either. *Static*.
namespace Client::Actors
{

// A case of 0x576eb0: the animation and action it passes to 0x576a60, and the action of its own C1 18.
struct EmoteCase
{
    std::uint8_t animation = 0;
    std::uint8_t action = 0;

    friend bool operator==(const EmoteCase&, const EmoteCase&) = default;
};

struct EmoteContext
{
    // [0x743a170], the hero: the direction of every C1 18 comes from its angle (+0x24), whatever the character.
    Kernel::ActorState& hero;
    // The models: 0x4ea030 looks at the actions of the model of the character's type (+2; 0x5769ad8 + type · 0xe0,
    // its +0x26, in 0x4ea036..0x4ea054), Player.bmd with a player and another with a transformed one (0x45).
    const Models::IModelLibrary& models;
    ActionSender send;
};

// 0x576a60(character, character, animation, action): with the character's current action (+0x111) from 1 to 12 or
// at 0x8a (the standing ones of 0x4f2250, before the walking ones, 0x0d..0x21), sets the animation with 0x4ea030
// (with an elf, +0x1d4 & 7 == 2, the next one, except from 0xb7 to 0xc0) and sends the action's C1 18; otherwise,
// nothing. Returns whether it acted. Its callers (0x576eb0 and the guild war: 0x4d8260 and 0x4d8870) pass the same
// character twice, the class's and the action's; MuRet receives it once.
[[nodiscard]] Core::Expected<bool, std::string> PlayTheEmote(Kernel::ActorState& actor, EmoteCase emote,
                                                             const EmoteContext& context);

// A case of 0x576eb0 (0x57ca85 and the others): 0x576a60 with the hero and then, whether it acted or not, its own
// C1 18 with the case's action. With the hero standing two equal C1 18 go out (Q-C776); walking, striking or in
// another action, only the case's, and the hero is not animated.
[[nodiscard]] Core::Expected<void, std::string> DoTheEmote(EmoteCase emote, const EmoteContext& context);
} // namespace Client::Actors
