#pragma once

#include "Scenes/SceneId.h"

#include <array>
#include <cstdint>
#include <optional>

// El mundo de cada escena como dato (CL5.4): el mapa que pone el original al entrar, que la escena
// le pasa al pase 3D (IWorldView). Desde la CL7, el pase 3D dibuja cualquier mapa con él, con su
// carpeta de Data::WorldFolder: ninguna escena nombra un mundo en su código.
namespace Client::Scenes
{
struct SceneWorld
{
    std::uint8_t map = 0;
};

struct SceneWorldEntry
{
    SceneId scene = SceneId::Login;
    SceneWorld world;
};

// El login, el 55 (0x6349b2, en 0x6349a0: World56) y la selección, el 54 (0x633f44, en 0x633f20:
// World55). Las demás no tienen uno fijo: la carga no dibuja un mundo, y el de la carga del mundo
// y el del juego lo manda el servidor.
inline constexpr std::array<SceneWorldEntry, 2> SceneWorlds = {{
    {.scene = SceneId::Login, .world = {.map = 55}},
    {.scene = SceneId::CharacterSelect, .world = {.map = 54}},
}};

// El mundo de `scene` en la tabla, o vacío.
[[nodiscard]] constexpr std::optional<SceneWorld> WorldOf(SceneId scene)
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
