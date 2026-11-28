#pragma once

#include "Render/Frame/SceneWorld.h"
#include "Kernel/SceneId.h"

#include <array>
#include <optional>

// Each scene's world as data: the map the original sets on entering, which the scene passes to the 3D pass
// (IWorldView). The 3D pass draws any map with it, with its Maps::WorldFolder folder: no scene names a world in its
// code.
namespace Client::Scenes
{
struct SceneWorldEntry
{
    Kernel::SceneId scene = Kernel::SceneId::Login;
    Render::Frame::SceneWorld world;
};

// The login, the 55 (0x6349b2, in 0x6349a0: World56) and the selection, the 54 (0x633f44, in 0x633f20: World55). The
// others have no fixed one: the loading draws no world, and the server sends the world loading's and the game's.
inline constexpr std::array<SceneWorldEntry, 2> SceneWorlds = {{
    {.scene = Kernel::SceneId::Login, .world = {.map = 55}},
    {.scene = Kernel::SceneId::CharacterSelect, .world = {.map = 54}},
}};

[[nodiscard]] constexpr std::optional<Render::Frame::SceneWorld> WorldOf(Kernel::SceneId scene)
{
    for (const SceneWorldEntry& entry : SceneWorlds)
    {
        if (entry.scene == scene)
        {
            return entry.world;
        }
    }
    return std::nullopt;
}
} // namespace Client::Scenes
