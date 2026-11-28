#include "Hud/GameHover.h"

#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextStyle.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Items/FloorItemNames.h"
#include "Kernel/DrawTailState.h"
#include "Hud/GamePanels.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "Social/SocialState.h"
#include "Hud/HudText.h"
#include "Render/Frame/ITextDrawer.h" // IWYU pragma: keep (ITextDrawer as ITextMeasure)
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Hud
{
namespace
{
// 0x5bbbd0..0x5bbbd8: Chaos Castle.
using Maps::IsChaosCastle;
// 0x6c9ccc.
constexpr std::string_view WebzenWord = "webzen";
// A monster's +0x88.
constexpr std::uint8_t MonsterKind = 2;
// 0x5bbcdd..0x5bbce7
constexpr Render::Text::TextColors MonsterNameColors{.text = 0xffc8e6ffu, .background = 0xff000064u};
// 0x5bbcdb
constexpr std::int32_t MonsterNameTop = 10;
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::int32_t NoTarget = -1;

// 0x557170(+0x1d9, 0x78abb3c or 0x78abb40, character, 0, −1): the name alone, with the empty .bss text.
Core::Expected<void, std::string> NameBubble(GameHoverContext& context, std::int32_t index)
{
    if (context.projecting)
        return {};
    const Kernel::ActorState& actor = context.world.Actors()[static_cast<std::size_t>(index)];
    return Chat::AddChatBubble(
        context.world,
        Chat::ChatBubbleMeasure{.measure = context.text, .screen = context.sprites.Screen(), .texts = context.texts},
        Actors::ChatNameOf(actor), "", static_cast<std::size_t>(index), Chat::ChatBubbleMode::Chat);
}

// 0x5bbc9e..0x5bbd2f: the chosen character.
Core::Expected<void, std::string> CharacterName(GameHoverContext& context, std::int32_t index)
{
    const Kernel::ActorState& actor = context.world.Actors()[static_cast<std::size_t>(index)];
    if (actor.byte88 != MonsterKind)
    {
        // 0x5bbd06..0x5bbd15: with its shop's sign in view (0x451770), nothing; otherwise, the name bubble.
        if (PersonalShop::ShopTitleShown(context.world.Runtime().social.personalShop, static_cast<std::size_t>(index)))
            return {};
        return NameBubble(context, index);
    }
    // 0x5bbcbb..0x5bbcd4: GetTextExtentPointA of the name, which is not used.
    context.hud.textColors = MonsterNameColors;
    // 0x5bbcf1..0x5bbcf9: 0x5bb8d0, with its queries, and the half with CDQ and SAR, toward 0.
    const std::int32_t worldWidth = GamePanels(context.world, context.random).Registry().WorldWidth();
    DrawHudCentered(context.text, context.sprites.Screen(), context.hud, worldWidth / 2, MonsterNameTop,
                    Actors::ChatNameOf(actor));
    return {};
}

// 0x5ba790 on entry `index` of 0x77f46c8.
Core::Expected<void, std::string> FloorItemName(GameHoverContext& context, std::int32_t index, bool atTheItem,
                                                std::string_view where)
{
    const auto& items = context.tail.floorItems;
    if (index < 0 || static_cast<std::size_t>(index) >= items.size())
        return Core::Unexpected(std::format("{}: el ítem del piso {} está fuera de la tabla 0x77f46c8", where, index));
    if (context.names == nullptr)
        return Core::Unexpected(
            std::format("{}: el nombre del ítem {} del piso (0x5ba790) sin sus tablas", where, index));
    return Items::DrawTheFloorItemName(*context.names, items[static_cast<std::size_t>(index)], atTheItem);
}

// 0x5bbd6a..0x5bbde7: Alt.
Core::Expected<void, std::string> FloorItemNames(GameHoverContext& context)
{
    if (Ui::LatchTheKey(context.altLatch, context.altDown))
        context.altNames = context.altNames == 0 ? 1 : 0;
    if (context.altNames == 0 && !context.altDown)
        return {};
    const auto& items = context.tail.floorItems;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        // 0x5bbda7..0x5bbdbc: alive (+0x44 of the 0x220-byte entry at 0x77f46d0), in view (+0x1b0) and not the chosen
        // one.
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
    Kernel::WorldState& world = context.world;
    const Kernel::WorldRuntime& runtime = world.Runtime();
    const std::int32_t floorItem = context.targets[0];
    const std::int32_t npc = context.targets[1];
    const std::int32_t character = context.targets[2];
    const std::int32_t pickedOther = context.targets[4];
    // 0x5bbbc6..0x5bbc0a.
    const Kernel::ActorState& hero = world.Hero();
    if (IsChaosCastle(runtime.map) && !Actors::ActorNameContains(hero, WebzenWord) &&
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
        // 0x5bbd31..0x5bbd62: at the mouse.
        if (auto named = FloorItemName(context, floorItem, false, "0x5bbd62"); !named.HasValue())
            return named;
    }
    return FloorItemNames(context);
}
} // namespace Client::Hud
