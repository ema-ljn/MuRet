#include "Scenes/ActorName.h"

#include "Protocol/Character/Opcodes.h"
#include "Scenes/ActorState.h"
#include "UI/NameChecks.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace Client::Scenes
{
bool ActorNameContains(const ActorState& actor, std::string_view word)
{
    std::array<char, Protocol::Character::NameSize + 1> name{};
    for (std::size_t index = 0; index < actor.name.size(); ++index)
        name[index] = static_cast<char>(actor.name[index]);
    name[actor.name.size()] = static_cast<char>(actor.byte1e3);
    const auto end = std::ranges::find(name, '\0');
    return UI::ContainsLikeTheOriginal(std::string_view(name.data(), static_cast<std::size_t>(end - name.begin())),
                                       word);
}
} // namespace Client::Scenes
