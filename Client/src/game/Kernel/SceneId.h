#pragma once

#include <cstdint>
#include <string_view>

// The original's five scenes, with the numbers of DAT_006d5dc8, the variable the draw loop (0x637700) and each step's
// logic look at to choose what to run (02 § Las cinco escenas y sus transiciones). In MuRet each one is a class
// (IScene.h), and the number stays for the log and the traces.
namespace Client::Kernel
{
enum class SceneId : std::uint8_t
{
    Loading = 1,         // 0x622e40
    Login = 2,           // 0x6349a0 on entering, 0x6350b0 the draw
    WorldLoading = 3,    // 0x6356f0
    CharacterSelect = 4, // 0x633f20 on entering
    Game = 5,
};

// For the log: "Login". A value outside the enum gives "?".
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
} // namespace Client::Kernel
