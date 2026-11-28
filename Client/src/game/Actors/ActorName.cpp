#include "Actors/ActorName.h"

#include "Protocol/FrontEnd/CharacterName.h"
#include "Kernel/ActorState.h"
#include "FrontEnd/NameChecks.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace Client::Actors
{
bool ActorNameContains(const Kernel::ActorState& actor, std::string_view word)
{
    std::array<char, Protocol::FrontEnd::NameSize + 1> name{};
    for (std::size_t index = 0; index < actor.name.size(); ++index)
        name[index] = static_cast<char>(actor.name[index]);
    name[actor.name.size()] = static_cast<char>(actor.byte1e3);
    const auto end = std::ranges::find(name, '\0');
    return UI::ContainsLikeTheOriginal(std::string_view(name.data(), static_cast<std::size_t>(end - name.begin())),
                                       word);
}

std::string_view HeroNameOf(const Kernel::ActorState& hero)
{
    std::size_t size = 0;
    while (size < hero.name.size() && hero.name[size] != std::byte{0})
        ++size;
    return {reinterpret_cast<const char*>(hero.name.data()), size};
}

std::string ChatNameOf(const Kernel::ActorState& actor)
{
    std::string name(HeroNameOf(actor));
    if (name.size() == actor.name.size() && actor.byte1e3 != 0)
        name.push_back(static_cast<char>(actor.byte1e3));
    return name;
}
} // namespace Client::Actors
