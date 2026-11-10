#include "Scenes/GameCharacters.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Scenes/ActorState.h"
#include "World/TerrainWalls.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x510e57..0x510e6d: 0x447a0000 en +0x13c y 0x3f800000 en los otros tres.
constexpr float UndrawnHeroMark = 1000.0f;
} // namespace

Core::Expected<void, std::string> DrawGameCharacters(std::span<ActorState> actors, std::size_t hero,
                                                     Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    for (std::size_t slot = 0; slot < actors.size(); ++slot)
    {
        ActorState& actor = actors[slot];
        // 0x510e40: el héroe con +0x1d8 & 4 en la escena 5.
        if (slot == hero && (actor.controlCode & UndrawnHeroControlBit) != 0)
        {
            actor.field13c = UndrawnHeroMark;
            actor.field148 = 1.0f;
            actor.field158 = 1.0f;
            actor.field168 = 1.0f;
            continue;
        }
        // 0x510e75 y 0x510e7a.
        if (!actor.alive || !actor.seen)
        {
            continue;
        }
        return Core::Unexpected(std::format("0x510ea1: el personaje {} está vivo y visto, y 0x50ba30 en el juego "
                                            "(la luz de 0x50cc55 y la rama del héroe de 0x50cca9) no está portado",
                                            slot));
    }
    World::OpenTheGates(walls);
    return {};
}
} // namespace Client::Scenes
