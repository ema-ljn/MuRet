#include "CastleSiege/CastleWindowDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/TextLayout.h"
#include "CastleSiege/CastleWindowSupport.h"
#include "Hud/GameHud.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::CastleSiege::CastleDraw
{
namespace
{
Ui::OldWindowDrawContext Drawing(Hud::GameHudContext& context)
{
    return {.sprites = context.sprites,
            .text = context.text,
            .dcFont = context.hud.dcFont,
            .textColors = context.hud.textColors};
}

Core::Expected<void, std::string> Head(Hud::GameHudContext& context, Ui::OldList& list, std::size_t count,
                                       bool registered)
{
    auto& sprites = context.sprites;
    const auto& base = list.base;
    sprites.SetBlend(Render::BlendMode::Alpha);
    Ui::OldDraw::SetThePalette(sprites, 7, 0.4f);
    sprites.ColorQuad(static_cast<float>(base.x - 1), static_cast<float>(base.y - base.height - 1),
                      static_cast<float>(base.width + 1), static_cast<float>(base.height + 2));
    Ui::OldDraw::WhiteWithTexture(sprites);
    Ui::OldDraw::DrawTheListBar(sprites, list, count,
                                {.mouseX = context.mouse.x,
                                 .mouseY = context.mouse.y,
                                 .leftClick = context.world.Runtime().movement.pendingClick});
    constexpr std::array<std::uint32_t, 4> SiegeHeaders{0x76ca724, 0x772d07c, 0x772d1a8, 0x772d2d4};
    constexpr std::array<std::uint32_t, 3> RegisteredHeaders{0x76ca724, 0x7732860, 0x773298c};
    constexpr std::array<std::int32_t, 4> SiegeLeft{5, 63, 98, 123};
    constexpr std::array<std::int32_t, 3> RegisteredLeft{5, 80, 120};
    const std::size_t columns = registered ? RegisteredHeaders.size() : SiegeHeaders.size();
    for (std::size_t i = 0; i < columns; ++i)
    {
        auto label = CastleUi::Text(context.texts, registered ? RegisteredHeaders[i] : SiegeHeaders[i]);
        if (!label.HasValue())
            return Core::Unexpected(std::move(label).Error());
        static_cast<void>(Ui::OldDraw::DrawText(Drawing(context),
                                                base.x + (registered ? RegisteredLeft[i] : SiegeLeft[i]),
                                                base.y - base.height - 12, label.Value()));
    }
    return {};
}

Core::Expected<std::int32_t, std::string> Row(Hud::GameHudContext& context,
                                              CastleSiege::SiegeRegistrationWindow& window, bool registered,
                                              std::int32_t visual, std::size_t at)
{
    auto& list = registered ? window.registeredGuildList : window.siegeGuildList;
    const auto& item = (registered ? window.registeredGuilds : window.siegeGuilds)[at];
    const std::string_view name(item.name.data());
    if (!registered)
    {
        auto names = CastleUi::GuildNames(context.world);
        if (!names.HasValue())
            return Core::Unexpected(std::move(names).Error());
        if (name != names.Value().first && name != names.Value().second && window.state <= 3)
            return 0;
    }
    const std::int32_t y =
        Ui::ListRowY(list, registered ? window.registeredGuilds.size() : window.siegeGuilds.size(), visual);
    const bool highlighted = registered ? item.first == 1 : list.selection == list.scroll + visual + 1;
    if (highlighted)
    {
        context.sprites.SetBlend(Render::BlendMode::Alpha);
        context.sprites.SetColor({1, 1, 1, registered ? 0.2f : 0.5f});
        context.sprites.ColorQuad(static_cast<float>(list.base.x), static_cast<float>(y - 3),
                                  static_cast<float>(list.base.width) - list.thumbWidth + 1, 13);
        Ui::OldDraw::WhiteWithTexture(context.sprites);
        if (!registered)
            context.hud.textColors.text = 0xff000000u;
    }
    else
        context.hud.textColors.text = 0xffc8dce6u;
    context.hud.textColors.background = 0;
    static_cast<void>(Ui::OldDraw::DrawText(Drawing(context), list.base.x + 6, y, name));
    // 0x47057f/0x4705de/0x470627 y 0x471404/0x471465: modo4 de 0x465950, ancho0.
    // El nombre anterior usa modo1; las demás columnas terminan en el ancla, incluso con rx fraccionario.
    const auto drawEndingAt = [&](std::int32_t x, std::string_view text)
    {
        const auto left = Render::Text::EndingAt(list.base.x + x, context.text.Measure(context.hud.dcFont, text),
                                                 context.sprites.Screen());
        static_cast<void>(Ui::OldDraw::DrawText(Drawing(context), left, y, text));
    };
    auto first = CastleUi::Text(context.texts, registered ? (item.first == 1 ? 0x7732be4 : 0x7732ab8)
                                                          : (item.first == 0 ? 0x772d52c : 0x772d400));
    if (!first.HasValue())
        return Core::Unexpected(std::move(first).Error());
    if (registered)
    {
        drawEndingAt(104, first.Value());
        auto second = CastleUi::Text(context.texts, item.second == 1 ? 0x7732d10 : 0x7732e3c);
        if (!second.HasValue())
            return Core::Unexpected(std::move(second).Error());
        drawEndingAt(141, second.Value());
    }
    else
    {
        drawEndingAt(89, std::to_string(std::bit_cast<std::int32_t>(item.marks)));
        drawEndingAt(124, first.Value());
        drawEndingAt(144, std::to_string(item.second));
    }
    return 1;
}
} // namespace

Core::Expected<void, std::string> GuildList(Hud::GameHudContext& context, CastleSiege::SiegeRegistrationWindow& window,
                                            bool registered)
{
    auto& list = registered ? window.registeredGuildList : window.siegeGuildList;
    const auto count = registered ? window.registeredGuilds.size() : window.siegeGuilds.size();
    std::optional<std::string> failed;
    Ui::OldDraw::DrawTheOldList(Drawing(context), list, count,
                                {.head =
                                     [&]()
                                 {
                                     if (auto head = Head(context, list, count, registered); !head.HasValue())
                                         failed = std::move(head).Error();
                                 },
                                 .row =
                                     [&](std::int32_t row, std::size_t at)
                                 {
                                     if (failed)
                                         return 1;
                                     auto drawn = Row(context, window, registered, row, at);
                                     if (!drawn.HasValue())
                                     {
                                         failed = std::move(drawn).Error();
                                         return 1;
                                     }
                                     return drawn.Value();
                                 },
                                 .tail = {}});
    if (failed)
        return Core::Unexpected(std::move(*failed));
    return {};
}
} // namespace Client::CastleSiege::CastleDraw
