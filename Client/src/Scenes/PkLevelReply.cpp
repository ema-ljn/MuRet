#include "Scenes/PkLevelReply.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Character/PkLevel.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameMessages.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "Crt/SprintfFormat.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x4d3b89: desde el 6, +0x1d6 en 1 (JC: sin signo).
constexpr std::uint8_t MurdererLevel = 6;
// 0x76e0c90, 0x76e0dbc, 0x76e0ee8, 0x76e1014 y 0x76e1140.
constexpr std::size_t HeroText = 487;
constexpr std::size_t CommonerText = 488;
constexpr std::size_t WarnedText = 489;
constexpr std::size_t MurdererText = 490;
constexpr std::size_t MurdererTailText = 491;
// 0x6c843c y 0x6c8444, de la .data: los dos son "%s %d%s".
constexpr std::string_view MurdererFormat = "%s %d%s";
// [EBP − 0x20]: el buffer de la pila del sprintf del 5 y del 6.
constexpr std::size_t MurdererBuffer = 0x20;

Core::Expected<std::string_view, std::string> TextOf(const WorldEntryResources& resources, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(resources.texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x4d3b50: falta el texto {} de Text.bmd", index));
    return *text;
}

// 0x4d3c04..0x4d3c19 y 0x4d3c55..0x4d3c6a: sprintf(buffer, "%s %d%s", el 490, 1 o 2, el 491).
Core::Expected<std::string, std::string> MurdererLine(const WorldEntryResources& resources, std::int32_t count,
                                                      std::string_view where)
{
    const auto head = TextOf(resources, MurdererText);
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    const auto tail = TextOf(resources, MurdererTailText);
    if (!tail.HasValue())
        return Core::Unexpected(tail.Error());
    std::string line = Crt::Sprintf(MurdererFormat, head.Value(), count, tail.Value());
    if (line.size() >= MurdererBuffer)
        return Core::Unexpected(std::format("{}: \"{}\" ocupa {} bytes con su '\\0' y el buffer de la pila tiene 0x20: "
                                            "lo que no entra la pisa",
                                            where, line, line.size() + 1));
    return line;
}
} // namespace

Core::Expected<void, std::string> ApplyPkLevel(const Protocol::Character::PkLevel& packet,
                                               const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    // 0x4d3b6c..0x4d3ba7: 0x5110d0(clave) sin mirar el resultado.
    ActorState& actor = world.Actors()[ActorWithKey(world, packet.key)];
    actor.pk = packet.level;
    actor.level = packet.level < MurdererLevel ? 0 : 1;
    // 0x4d3ba7..0x4d3bb6: la tabla 0x4d3ca8 del 1 al 6; el 0 y desde el 7, nada más.
    std::string text;
    std::int32_t type = UI::SystemMessage;
    switch (packet.level)
    {
    case 1:
    case 2:
    case 3:
    {
        const auto read = TextOf(resources, packet.level == 3 ? CommonerText : HeroText);
        if (!read.HasValue())
            return Core::Unexpected(read.Error());
        text = read.Value();
        break;
    }
    case 4:
    {
        const auto read = TextOf(resources, WarnedText);
        if (!read.HasValue())
            return Core::Unexpected(read.Error());
        text = read.Value();
        type = UI::ErrorMessage;
        break;
    }
    case 5:
    case MurdererLevel:
    {
        auto line = MurdererLine(resources, packet.level == 5 ? 1 : 2, packet.level == 5 ? "0x4d3c19" : "0x4d3c6a");
        if (!line.HasValue())
            return Core::Unexpected(std::move(line).Error());
        text = std::move(line).Value();
        type = UI::ErrorMessage;
        break;
    }
    default:
        return {};
    }
    if (resources.textMeasure == nullptr)
        return Core::Unexpected(std::string("0x4d3bf6: 0x555190 sin el medidor de los textos"));
    // 0x4d3bd7..0x4d3bf6: otra vez 0x5110d0(clave), y 0x555190(+0x1d9 de ese personaje, texto, tipo).
    const std::string name = ChatNameOf(world.Actors()[ActorWithKey(world, packet.key)]);
    const Core::PixelSize screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480};
    return AddGameMessage(world.Runtime(), *resources.textMeasure, screen, name, text, type);
}
} // namespace Client::Scenes
