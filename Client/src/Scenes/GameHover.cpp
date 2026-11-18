#include "Scenes/GameHover.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorName.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/FloorItemNames.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GamePanels.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialState.h"
#include "Scenes/HudText.h"
#include "Scenes/ITextDrawer.h" // IWYU pragma: keep (ITextDrawer como ITextMeasure)
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x5bbbd0..0x5bbbd8: Chaos Castle.
constexpr std::int32_t FirstChaosCastleMap = 0x12;
constexpr std::int32_t LastChaosCastleMap = 0x17;
// 0x6c9ccc.
constexpr std::string_view WebzenWord = "webzen";
// +0x88 de un monstruo.
constexpr std::uint8_t MonsterKind = 2;
// 0x5bbcdd..0x5bbce7: el fondo y el texto del nombre de un monstruo.
constexpr Render::Text::TextColors MonsterNameColors{.text = 0xffc8e6ffu, .background = 0xff000064u};
// 0x5bbcdb: la y del nombre de un monstruo.
constexpr std::int32_t MonsterNameTop = 10;
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::int32_t NoTarget = -1;

bool InChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

// 0x557170(+0x1d9, 0x78abb3c o 0x78abb40, personaje, 0, −1): el nombre solo, con el texto vacío de la .bss.
Core::Expected<void, std::string> NameBubble(GameHoverContext& context, std::int32_t index)
{
    if (context.projecting)
        return {};
    const ActorState& actor = context.world.Actors()[static_cast<std::size_t>(index)];
    return AddChatBubble(
        context.world,
        ChatBubbleMeasure{.measure = context.text, .screen = context.sprites.Screen(), .texts = context.texts},
        ChatNameOf(actor), "", static_cast<std::size_t>(index), ChatBubbleMode::Chat);
}

// 0x5bbc9e..0x5bbd2f: el personaje elegido.
Core::Expected<void, std::string> CharacterName(GameHoverContext& context, std::int32_t index)
{
    const ActorState& actor = context.world.Actors()[static_cast<std::size_t>(index)];
    if (actor.byte88 != MonsterKind)
    {
        // 0x5bbd06..0x5bbd15: con el cartel de su tienda a la vista (0x451770), nada; si no, el globo del nombre.
        if (ShopTitleShown(context.world.Runtime().social.personalShop, static_cast<std::size_t>(index)))
            return {};
        return NameBubble(context, index);
    }
    // 0x5bbcbb..0x5bbcd4: GetTextExtentPointA del nombre, que no se usa.
    context.hud.textColors = MonsterNameColors;
    // 0x5bbcf1..0x5bbcf9: 0x5bb8d0, con sus consultas, y la mitad con CDQ y SAR, hacia el 0.
    const std::int32_t worldWidth = GamePanels(context.world, context.random).Registry().WorldWidth();
    DrawHudCentered(context.text, context.sprites.Screen(), context.hud, worldWidth / 2, MonsterNameTop,
                    ChatNameOf(actor));
    return {};
}

// 0x5ba790 sobre la entrada `index` de 0x77f46c8.
Core::Expected<void, std::string> FloorItemName(GameHoverContext& context, std::int32_t index, bool atTheItem,
                                                std::string_view where)
{
    const auto& items = context.tail.floorItems;
    if (index < 0 || static_cast<std::size_t>(index) >= items.size())
        return Core::Unexpected(std::format("{}: el ítem del piso {} está fuera de la tabla 0x77f46c8", where, index));
    if (context.names == nullptr)
        return Core::Unexpected(
            std::format("{}: el nombre del ítem {} del piso (0x5ba790) sin sus tablas", where, index));
    return DrawTheFloorItemName(*context.names, items[static_cast<std::size_t>(index)], atTheItem);
}

// 0x5bbd6a..0x5bbde7: Alt.
Core::Expected<void, std::string> FloorItemNames(GameHoverContext& context)
{
    if (UI::LatchTheKey(context.altLatch, context.altDown))
        context.altNames = context.altNames == 0 ? 1 : 0;
    if (context.altNames == 0 && !context.altDown)
        return {};
    const auto& items = context.tail.floorItems;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        // 0x5bbda7..0x5bbdbc: vivo (+0x44 de la entrada de 0x220 bytes en 0x77f46d0), a la vista (+0x1b0) y no el
        // elegido.
        if (!items[index].alive || !items[index].seen || static_cast<std::int32_t>(index) == context.targets[0])
            continue;
        if (auto named = FloorItemName(context, static_cast<std::int32_t>(index), true, "0x5bbdd2"); !named.HasValue())
            return named;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheHover(GameHoverContext& context)
{
    WorldState& world = context.world;
    const WorldRuntime& runtime = world.Runtime();
    const std::int32_t floorItem = context.targets[0];
    const std::int32_t npc = context.targets[1];
    const std::int32_t character = context.targets[2];
    const std::int32_t pickedOther = context.targets[4];
    // 0x5bbbc6..0x5bbc0a.
    const ActorState& hero = world.Actors()[runtime.hero];
    if (InChaosCastle(runtime.map) && !ActorNameContains(hero, WebzenWord) &&
        (npc != NoTarget || character != NoTarget))
        return {};
    // 0x5bbc10..0x5bbc2c.
    context.hud.durabilityWarning = 1;
    context.sprites.SetColor(White);
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    if (npc != NoTarget)
    {
        if (auto named = NameBubble(context, npc); !named.HasValue())
            return named;
    }
    else if (character != NoTarget && pickedOther == NoTarget)
    {
        if (auto named = CharacterName(context, character); !named.HasValue())
            return named;
    }
    else if (floorItem != NoTarget)
    {
        // 0x5bbd31..0x5bbd62: en el mouse.
        if (auto named = FloorItemName(context, floorItem, false, "0x5bbd62"); !named.HasValue())
            return named;
    }
    return FloorItemNames(context);
}
} // namespace Client::Scenes
