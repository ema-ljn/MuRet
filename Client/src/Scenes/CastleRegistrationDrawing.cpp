#include "Scenes/CastleWindowDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Scenes/CastleWindowSupport.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleSiegeState.h"
#include "UI/OldWindowDrawParts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes::CastleDraw
{
namespace
{
constexpr std::int32_t Left = 450;
constexpr std::int32_t ContentTop = 105;

Core::Expected<void, std::string> Date(GameHudContext& context, std::int32_t y, std::uint32_t address,
                                       const UI::SiegeDate& date)
{
    return Formatted(context, Left, y, address, static_cast<std::uint32_t>(date.year),
                     static_cast<std::uint32_t>(date.rest[0]), static_cast<std::uint32_t>(date.rest[1]),
                     static_cast<std::uint32_t>(date.rest[2]), static_cast<std::uint32_t>(date.rest[3]));
}

Core::Expected<void, std::string> StatePage(GameHudContext& context, const UI::SiegeRegistrationWindow& window)
{
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    if (auto date = Date(context, ContentTop + 20, 0x772d658, window.dates[0]); !date.HasValue())
        return date;
    if (auto date = Date(context, ContentTop + 34, 0x772d784, window.dates[1]); !date.HasValue())
        return date;
    constexpr std::array<std::uint32_t, 9> States{0x772d9dc, 0x772db08, 0x772dc34, 0x772dd60, 0x772de8c,
                                                  0x772dfb8, 0x772e0e4, 0x772e210, 0x772e33c};
    std::uint32_t state = 0;
    if (window.state >= 1 && window.state <= 9)
        state = States[static_cast<std::size_t>(window.state - 1)];
    else if (window.state == -1 || window.state == 0)
        state = 0x772d8b0;
    if (state != 0)
    {
        if (auto text = Text(context, Left, ContentTop + 48, state); !text.HasValue())
            return text;
    }
    if (window.state >= 7)
        return {};
    if (auto text = Text(context, Left, ContentTop + 83, 0x772e468); !text.HasValue())
        return text;
    if (auto date = Date(context, ContentTop + 97, 0x772e594, window.dates[2]); !date.HasValue())
        return date;
    return Formatted(context, Left, ContentTop + 132, 0x7725318, window.remaining / 3600,
                     (window.remaining % 3600) / 60, window.remaining % 60);
}

Core::Expected<void, std::string> RegisterPage(GameHudContext& context, UI::SiegeRegistrationWindow& window)
{
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    const auto post = context.world.Actors()[context.world.Runtime().hero].guildStatus;
    if (window.state == 1)
    {
        if (post != CastleUi::Master)
            return Text(context, Left, ContentTop + 20, 0x771dcbc);
        if (window.registered != 0)
            return Text(context, Left, ContentTop + 20, 0x772e6c0);
        auto names = CastleUi::GuildNames(context.world);
        if (!names.HasValue())
            return Core::Unexpected(std::move(names).Error());
        const std::string_view owner(reinterpret_cast<const char*>(window.ownerGuild.data()));
        const bool owns = !names.Value().first.empty() && !owner.empty() && names.Value().first == owner;
        return Button(context, window.button, Left + 70, ContentTop + 10, 0x7726380, owns ? CastleUi::Disabled : 0);
    }
    if (window.state == 3)
    {
        if (window.registered != 1)
            return Text(context, Left, ContentTop + 20, 0x772c014);
        if (auto text = Text(context, Left, ContentTop + 20, 0x77264ac); !text.HasValue())
            return text;
        const auto count = CastleUi::CountMarks(context.world, context.random, context.ownRecordDepth);
        if (auto text = Formatted(context, Left, ContentTop + 50, 0x77265d8, count); !text.HasValue())
            return text;
        if (auto text = Formatted(context, Left, ContentTop + 64, 0x7726704, window.marks); !text.HasValue())
            return text;
        return Button(context, window.button, Left + 70, ContentTop + 94, 0x7726830,
                      count < 1 ? CastleUi::Disabled : 0);
    }
    if (window.state == 4)
    {
        WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
        if (auto text = Text(context, Left, ContentTop + 20, 0x772695c); !text.HasValue())
            return text;
        return Text(context, Left, ContentTop + 34, 0x7726a88);
    }
    std::uint32_t text = 0;
    switch (window.state)
    {
    case -1:
    case 0:
        text = 0x772d8b0;
        break;
    case 2:
        text = 0x772e7ec;
        break;
    case 5:
        text = 0x772de8c;
        break;
    case 6:
        text = 0x772dfb8;
        break;
    case 7:
        text = 0x772e0e4;
        break;
    case 8:
        text = 0x7726bb4;
        WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
        break;
    case 9:
        text = 0x772e33c;
        break;
    default:
        return {};
    }
    return Text(context, Left, ContentTop + 20, text);
}

Core::Expected<void, std::string> ListPage(GameHudContext& context, UI::SiegeRegistrationWindow& window)
{
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    if (window.state == 1 || window.state == 3)
    {
        if (auto list = GuildList(context, window, false); !list.HasValue())
            return list;
    }
    else if (window.state == 5 || window.state == 6)
    {
        if (auto list = GuildList(context, window, true); !list.HasValue())
            return list;
    }
    else if (window.state == 8)
    {
        if (auto text = Text(context, Left, ContentTop + 20, 0x7726bb4); !text.HasValue())
            return text;
    }
    const auto post = context.world.Actors()[context.world.Runtime().hero].guildStatus;
    const bool enabled = window.registered == 1 && window.state > 0 && window.state < 4 && post == CastleUi::Master;
    return Button(context, window.button, Left + 70, ContentTop + 262, 0x772e918, enabled ? 0 : CastleUi::Disabled);
}

Core::Expected<void, std::string> RegistrationTabs(GameHudContext& context, UI::SiegeRegistrationWindow& window)
{
    constexpr std::array<std::uint32_t, 3> Labels{0x77272bc, 0x7726380, 0x77273e8};
    for (std::int32_t tab = 0; tab < 3; ++tab)
    {
        const bool selected = window.tab == tab;
        const float gray = selected ? 1.0f : 0.6f;
        context.sprites.SetColor({gray, gray, gray, 1});
        WindowDrawing::Quad(context.sprites, 0x646, static_cast<float>(Left + 15 + tab * 54), selected ? 88.0f : 90.0f,
                            54, selected ? 18.0f : 16.0f, 0.75f, 0.9375f);
        const auto label = tab == 1 && window.state != 1 ? 0x7726830u : Labels[static_cast<std::size_t>(tab)];
        if (auto text = Text(context, Left + 15 + tab * 54, selected ? 93 : 94, label, 54); !text.HasValue())
            return text;
    }
    context.sprites.SetColor({146.0f / 255, 144.0f / 255, 141.0f / 255, 1});
    for (std::int32_t tab = 0; tab < 3; ++tab)
        if (window.tab != tab)
            context.sprites.ColorQuad(static_cast<float>(Left + 15 + tab * 54), 105, 54, 1);
    UI::OldDraw::WhiteWithTexture(context.sprites); // 0x64dd27: 0x459ec0 después de los subrayados.
    return {};
}
} // namespace

Core::Expected<void, std::string> Registration(GameHudContext& context, UI::SiegeRegistrationWindow& window)
{
    window.x = Left;
    window.y = 0;
    window.right = 770;
    window.bottom = 300;
    if (auto back = Back(context, Left, 0x7726f38); !back.HasValue())
        return back;
    std::string unknown;
    if (window.ownerMaster[0] == 0 || window.ownerGuild[0] == 0)
    {
        auto text = CastleUi::Text(context.texts, 0x7720cc8);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        unknown = std::move(text).Value();
    }
    const std::string_view master = window.ownerMaster[0] == 0
                                        ? std::string_view(unknown)
                                        : std::string_view(reinterpret_cast<const char*>(window.ownerMaster.data()));
    const std::string_view guild = window.ownerGuild[0] == 0
                                       ? std::string_view(unknown)
                                       : std::string_view(reinterpret_cast<const char*>(window.ownerGuild.data()));
    if (auto text = Formatted(context, Left, 56, 0x7727064, master); !text.HasValue())
        return text;
    if (auto text = Formatted(context, Left, 71, 0x7727190, guild); !text.HasValue())
        return text;
    if (auto tabs = RegistrationTabs(context, window); !tabs.HasValue())
        return tabs;
    Core::Expected<void, std::string> page;
    if (window.tab == 0)
        page = StatePage(context, window);
    else if (window.tab == 1)
        page = RegisterPage(context, window);
    else if (window.tab == 2)
        page = ListPage(context, window);
    if (!page.HasValue())
        return page;
    return Close(context, Left);
}
} // namespace Client::Scenes::CastleDraw
