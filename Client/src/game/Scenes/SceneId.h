#pragma once

#include <cstdint>
#include <string_view>

// Las cinco escenas del original, con los números de DAT_006d5dc8, la variable que mira el bucle
// de dibujo (0x637700) y la lógica de cada paso para elegir qué correr (02 § Las cinco escenas y
// sus transiciones). En MuRet cada una es una clase (IScene.h), y el número queda para el log y
// las trazas.
namespace Client::Scenes
{
enum class SceneId : std::uint8_t
{
    Loading = 1,         // la carga, 0x622e40
    Login = 2,           // 0x6349a0 al entrar, 0x6350b0 el dibujo
    WorldLoading = 3,    // la carga del mundo, 0x6356f0
    CharacterSelect = 4, // la selección de personaje, 0x633f20 al entrar
    Game = 5,
};

// El nombre del enum, para el log: "Login". Un valor fuera del enum da "?".
[[nodiscard]] constexpr std::string_view SceneName(SceneId scene)
{
    switch (scene)
    {
    case SceneId::Loading:
        return "Loading";
    case SceneId::Login:
        return "Login";
    case SceneId::WorldLoading:
        return "WorldLoading";
    case SceneId::CharacterSelect:
        return "CharacterSelect";
    case SceneId::Game:
        return "Game";
    }
    return "?";
}
} // namespace Client::Scenes
