#include "Party/GamePartyWindow.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Protocol/Party/Party.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Scenes/GameProjection.h"
#include "Ui/GameUiContext.h"
#include "Hud/GameWindowDrawing.h"
#include "Maps/MapNames.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Party
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5e3cb5..0x5e3ce8: the mouse over the window, 0xbe × 0x1b1 from the corner.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x5e40d4..0x5e40f2: the close button, 0x18 × 0x18 at (0x19, 0x18b) from the corner.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x5e410d..0x5e4113.
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x5e3d83..0x5e3dc5: the kick button, 0x18 × 0x18 at (0x8c, 0x37 + 0x23 · slot) from the corner.
constexpr std::int32_t KickLeft = 0x8c;
constexpr std::int32_t KickTop = 0x37;
constexpr std::int32_t KickStep = 0x23;
constexpr std::int32_t KickSize = 0x18;
// 0x5e3ddc.
constexpr Audio::SoundId KickSound = Sound::ButtonClickSound;

// The Text.bmd texts (the address − 0x76bd1dc, over 300): the title, the empty party's seven lines
// (0x76cb1b0..0x76cb8b8) and the close button's help (0x76cd4d8).
constexpr std::size_t TitleText = 190;
constexpr std::size_t FirstEmptyText = 191;
constexpr std::size_t CloseHelpText = 221;
// 0x5f0d3f..0x5f0dc3: the lines at x + 0x14 and at y + these.
constexpr std::array<std::int32_t, 7> EmptyLineTops{0x32, 0x41, 0x50, 0x64, 0x73, 0x82, 0x91};
constexpr std::int32_t EmptyLineLeft = 0x14;
// 0x5f0cf3..0x5f0cfb: the title at (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;

// 0x6c8e5c and 0x6c8e64 of the title (0x5f0cbe), of the lines (0x5f0d14) and of the closed party (0x5f12eb).
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr Render::Text::TextColors LineColors{.text = 0xffe6e6e6u, .background = 0};
constexpr Render::Text::TextColors ClosedColors{.text = 0xffffffffu, .background = 0};

// The party's GL colors, with the PUSHes' bits.
constexpr float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}
constexpr Render::Color LifeBack{Bits(0x3e48c8c9u), Bits(0x3d20a0a1u), 0.0f, 1.0f};
constexpr Render::Color LifeTenth{Bits(0x3f7afafbu), Bits(0x3d20a0a1u), 0.0f, 1.0f};

// 0x5a89b0: the bar over the head (+0x138 + z + 100, 0x6b86b4), 19 to the left (0x5a8a3c), 42 × 5 with its shadow
// (0, 0, 0, 0.5) one point down and right, the border (0.2, 0, 0) added, the 38 × 1 background and the 3 × 2 tenths
// every 4. The "HP" with the mouse in [x, x + 38) × [y − 2, y + 6).
constexpr double AboveTheHead = 100.0;
constexpr std::int32_t LifeLeft = 0x13;
constexpr float LifeWidth = 42.0f;
constexpr float LifeHeight = 5.0f;
constexpr float LifeInnerWidth = 38.0f;
constexpr float LifeInnerHeight = 1.0f;
constexpr float TenthWidth = 3.0f;
constexpr float TenthHeight = 2.0f;
constexpr std::int32_t TenthStep = 4;
constexpr std::uint8_t MaxTenths = 10;
constexpr double LifeHoverWidth = 38.0;
constexpr std::int32_t LifeHoverAbove = 2;
constexpr std::int32_t LifeHoverBelow = 6;
constexpr std::int32_t LifeTextAbove = 6;
constexpr Render::Color LifeShadow{0.0f, 0.0f, 0.0f, 0.5f};
constexpr Render::Color LifeEdge{0.2f, 0.0f, 0.0f, 1.0f};

// 0x5f130a..0x5f168a: the right-hand list, from the world's width − 50 (0x6b86b0) and y 5, every 19 (0x6b8870).
constexpr double ListFromTheRight = 50.0;
constexpr float ListTop = 5.0f;
constexpr double ListRow = 19.0;
constexpr double ListBackLeft = 3.0;
constexpr float ListBackWidth = 50.0f;
constexpr float ListBackHeight = 19.0f;
constexpr double ListBarLeft = 2.0;
constexpr double ListBarTop = 12.0;
constexpr float ListBarWidth = 43.0f;
constexpr float ListBarHeight = 4.0f;
constexpr double ListTenthTop = 13.0;
constexpr std::int32_t ListNameTop = 2;
constexpr Render::Color ListBackColor{0.1f, 0.1f, 0.1f, 0.7f};
constexpr Render::Color ListHoverColor{0.5f, 0.5f, 0.5f, 0.8f};
constexpr Render::Color ListMissingColor{0.3f, 0.0f, 0.0f, 0.5f};
constexpr Render::Color ListFlaggedColor{0.2f, 1.0f, 0.2f, 0.2f};
constexpr std::uint32_t ListFlag = 8;
// 0x5f1583..0x5f1665: Message_box2 (0xfc) at top and bottom, 54 × 3 from x − 5, and the 1-wide sides at x − 4 and
// x + 47 (0x6ba644), 19 per member from y 5.
constexpr std::uint16_t ListEdgeSlot = 0xfc;
constexpr double ListEdgeLeft = 5.0;
constexpr float ListEdgeTop = 2.0f;
constexpr float ListEdgeWidth = 54.0f;
constexpr float ListEdgeHeight = 3.0f;
constexpr float ListEdgeDu = 0.83203125f;
constexpr float ListEdgeDv = 0.625f;
constexpr double ListLeftSide = 4.0;
constexpr double ListRightSide = 47.0;
constexpr float ListSideTop = 5.0f;
constexpr float ListSideWidth = 1.0f;
constexpr std::int32_t ListSideStep = 19;
constexpr Render::Color ListSideColor{Bits(0x3f38b8b9u), Bits(0x3f179798u), Bits(0x3ec2c2c3u), 0.5f};

// 0x5f0df0..0x5f11ad: each member in the open window, every 0x23.
constexpr std::int32_t MemberStep = 0x23;
constexpr std::int32_t MemberLeft = 0x14;
constexpr std::int32_t MemberNameTop = 0x34;
constexpr std::int32_t MemberMapTop = 0x3c;
constexpr std::int32_t MemberCellLeft = 0x5a;
constexpr std::int32_t MemberBarLeft = 0x13;
constexpr std::int32_t MemberBarTop = 0x45;
constexpr float MemberBarWidth = 92.0f;
constexpr float MemberBarHeight = 5.0f;
constexpr std::int32_t MemberLifeTop = 0x46;
constexpr double MemberLifeWidth = 90.0;
constexpr float MemberLifeHeight = 3.0f;
constexpr std::int32_t MemberNumbersLeft = 0x38;
constexpr std::int32_t MemberNumbersTop = 0x4b;
constexpr Render::Color MemberBarColor{0.2f, 0.0f, 0.0f, 1.0f};
constexpr Render::Color MemberLifeColor{Bits(0x3f61e1e2u), Bits(0x3d70f0f1u), 0.0f, 1.0f};
constexpr Render::Text::TextColors LeaderColors{.text = 0xffffffffu, .background = 0xff962828u};
constexpr Render::Text::TextColors MemberColors{.text = 0xffffe6d2u, .background = 0};
constexpr Render::Text::TextColors MemberDataColors{.text = 0xffd2e6ffu, .background = 0};
// 0x5f108f..0x5f11a1: the kick button, exit_01.jpg (0x118) or exit_02.jpg (0x119) with the left button down, 24 × 24
// at (x + 140, y + 55 + 0x23 · slot), with its help (text 189, 0x76caf58) 13 higher.
constexpr std::uint16_t KickSlot = 0x118;
constexpr std::uint16_t KickPressedSlot = 0x119;
constexpr double KickDrawnLeft = 140.0;
constexpr double KickDrawnTop = 55.0;
constexpr float KickDrawnSize = 24.0f;
constexpr float KickUv = 0.75f;
constexpr std::int32_t KickHelpAbove = 0xd;
constexpr std::size_t KickHelpText = 189;
constexpr Render::Text::TextColors KickHelpColors{.text = 0xffffffffu, .background = 0xff000000u};

Core::Expected<std::string_view, std::string> TextOf(const Ui::GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}
// A ten-byte name up to its first 0 (strcmp between the hero's +0x1d9 and 0x77f3cd0's slot; *inferred* with ten
// letters, where strcmp goes on into the hero's +0x1e3 against the 0 at the slot's +0xa).
std::string_view NameView(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}

using Math::F32;

// _ftol: toward 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

Core::Expected<std::size_t, std::string> Members(std::int32_t count, std::string_view where)
{
    if (count > static_cast<std::int32_t>(Social::PartySlots))
        return Core::Unexpected(
            std::format("{}: {} miembros pasan los cinco lugares de 0x77f3cd0 (Q-C960)", where, count));
    return static_cast<std::size_t>(std::max(count, 0));
}

// The life's tenths (+0x18, from the 44), up to 10.
std::int32_t TenthsOf(const Social::PartySlot& slot)
{
    return std::min(slot.tenths, MaxTenths);
}

// 0x5f130a..0x5f168a.
Core::Expected<void, std::string> DrawThePartyList(Ui::GameHudContext& context, std::int32_t width)
{
    Kernel::HudState& hud = context.hud;
    auto counted = Members(hud.partyCount, "0x5f130a");
    if (!counted.HasValue())
        return Core::Unexpected(std::move(counted).Error());
    Render::SpriteBatch& sprites = context.sprites;
    const Social::PartyState& party = context.world.Runtime().social.party;
    const float left = F32(static_cast<double>(width) - ListFromTheRight);
    const auto mouseX = static_cast<double>(context.mouse.x);
    const auto mouseY = static_cast<double>(context.mouse.y);
    float top = ListTop;
    const float barLeft = F32(static_cast<double>(left) - ListBarLeft);
    const float backLeft = F32(static_cast<double>(left) - ListBackLeft);
    for (std::size_t at = 0; at < counted.Value(); ++at)
    {
        const Social::PartySlot& slot = party.slots[at];
        sprites.SetBlend(BlendMode::Alpha);
        if (slot.actorIndex != -1)
        {
            const bool hover = mouseX >= static_cast<double>(backLeft) &&
                               mouseX <= static_cast<double>(left) + ListFromTheRight &&
                               mouseY >= static_cast<double>(top) && mouseY <= static_cast<double>(top) + ListRow;
            sprites.SetColor(hover ? ListHoverColor : ListBackColor);
            sprites.ColorQuad(backLeft, top, ListBackWidth, ListBackHeight);
        }
        sprites.SetColor(LifeBack);
        sprites.ColorQuad(barLeft, F32(static_cast<double>(top) + ListBarTop), ListBarWidth, ListBarHeight);
        sprites.SetColor(LifeTenth);
        const float tenthTop = F32(static_cast<double>(top) + ListTenthTop);
        for (std::int32_t tenth = 0; tenth < TenthsOf(slot); ++tenth)
            sprites.ColorQuad(F32(static_cast<double>(tenth * TenthStep) + static_cast<double>(left)), tenthTop,
                              TenthWidth, TenthHeight);
        if (slot.actorIndex == -1)
        {
            sprites.SetColor(ListMissingColor);
            sprites.ColorQuad(backLeft, top, ListBackWidth, ListBackHeight);
        }
        else if ((slot.actorFlags & ListFlag) == ListFlag)
        {
            sprites.SetColor(ListFlaggedColor);
            sprites.ColorQuad(backLeft, top, ListBackWidth, ListBackHeight);
        }
        sprites.SetColor(White);
        sprites.SetBlend(BlendMode::Alpha);
        Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
        Ui::WindowDrawing::DrawText(context, Truncate(static_cast<double>(left)),
                                     Truncate(static_cast<double>(top)) + ListNameTop, NameView(slot.name));
        top = F32(static_cast<double>(top) + ListRow);
    }
    const float edgeLeft = F32(static_cast<double>(left) - ListEdgeLeft);
    Ui::WindowDrawing::Quad(sprites, ListEdgeSlot, edgeLeft, ListEdgeTop, ListEdgeWidth, ListEdgeHeight, ListEdgeDu,
                             ListEdgeDv);
    Ui::WindowDrawing::Quad(sprites, ListEdgeSlot, edgeLeft, top, ListEdgeWidth, ListEdgeHeight, ListEdgeDu,
                             ListEdgeDv);
    sprites.SetColor(ListSideColor);
    const auto sides = static_cast<float>(hud.partyCount * ListSideStep);
    sprites.ColorQuad(F32(static_cast<double>(left) - ListLeftSide), ListSideTop, ListSideWidth, sides);
    sprites.ColorQuad(F32(static_cast<double>(left) + ListRightSide), ListSideTop, ListSideWidth, sides);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    return {};
}

// 0x5f0df0..0x5f11ad: the members in the open window at (x, y).
Core::Expected<void, std::string> DrawTheMembers(Ui::GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Kernel::HudState& hud = context.hud;
    auto counted = Members(hud.partyCount, "0x5f0df0");
    if (!counted.HasValue())
        return Core::Unexpected(std::move(counted).Error());
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::WorldState& world = context.world;
    const Social::PartyState& party = world.Runtime().social.party;
    const std::string_view hero = NameView(world.Hero().name);
    const bool leader = NameView(party.slots[0].name) == hero;
    for (std::size_t at = 0; at < counted.Value(); ++at)
    {
        const Social::PartySlot& slot = party.slots[at];
        const std::int32_t row = static_cast<std::int32_t>(at) * MemberStep;
        hud.textColors = at == 0 ? LeaderColors : MemberColors;
        Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
        Ui::WindowDrawing::DrawText(context, x + MemberLeft, row + y + MemberNameTop, NameView(slot.name));
        hud.textColors = MemberDataColors;
        Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
        auto map = TextOf(context, Maps::MapNameText(slot.map), "0x5f0ec8");
        if (!map.HasValue())
            return Core::Unexpected(std::move(map).Error());
        Ui::WindowDrawing::DrawText(context, x + MemberLeft, row + y + MemberMapTop, map.Value());
        Ui::WindowDrawing::DrawText(context, x + MemberCellLeft, row + y + MemberMapTop,
                                     std::format("{},{}", slot.x, slot.y));
        sprites.SetColor(MemberBarColor);
        sprites.ColorQuad(static_cast<float>(x + MemberBarLeft), static_cast<float>(row + y + MemberBarTop),
                          MemberBarWidth, MemberBarHeight);
        sprites.SetColor(MemberLifeColor);
        // 0x5f0f71..0x5f0fbb: the lesser, signed, FIDIV by the maximum (with 0, whatever the x87 gives) and · 90
        // (0x6b8948).
        const auto life = static_cast<std::int32_t>(slot.life);
        const auto maxLife = static_cast<std::int32_t>(slot.maxLife);
        const std::int32_t shown = life < maxLife ? life : maxLife;
        const float lifeWidth = F32(static_cast<double>(shown) / static_cast<double>(maxLife) * MemberLifeWidth);
        sprites.ColorQuad(static_cast<float>(x + MemberLeft), static_cast<float>(row + y + MemberLifeTop), lifeWidth,
                          MemberLifeHeight);
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(White);
        Ui::WindowDrawing::DrawText(context, x + MemberNumbersLeft, row + y + MemberNumbersTop,
                                     std::format("( {} / {} )", life, maxLife));
        // 0x5f1013..0x5f1089: the button with the hero as leader (the first slot) or in this slot.
        if (!leader && NameView(slot.name) != hero)
            continue;
        const float left = F32(static_cast<double>(hud.partyX) + KickDrawnLeft);
        const float top = F32(static_cast<double>(row) + static_cast<double>(hud.partyY) + KickDrawnTop);
        Ui::WindowDrawing::Quad(sprites, KickSlot, left, top, KickDrawnSize, KickDrawnSize, KickUv, KickUv);
        const auto mouseX = static_cast<double>(context.mouse.x);
        const auto mouseY = static_cast<double>(context.mouse.y);
        if (mouseX < static_cast<double>(left) || mouseX >= static_cast<double>(left) + KickDrawnSize ||
            mouseY < static_cast<double>(top) || mouseY >= static_cast<double>(top) + KickDrawnSize)
            continue;
        if (context.leftDown)
            Ui::WindowDrawing::Quad(sprites, KickPressedSlot, left, top, KickDrawnSize, KickDrawnSize, KickUv, KickUv);
        auto help = TextOf(context, KickHelpText, "0x5f118f");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        hud.textColors = KickHelpColors;
        Ui::WindowDrawing::DrawText(context, Truncate(static_cast<double>(left)),
                                     Truncate(static_cast<double>(top)) - KickHelpAbove, help.Value());
    }
    return {};
}

Core::Expected<void, std::string> StepTheKickButtons(const Ui::GameUiContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const std::int32_t count = runtime.hud.partyCount;
    if (count <= 0)
        return {};
    if (count > static_cast<std::int32_t>(Social::PartySlots))
        return Core::Unexpected(
            std::format("0x5e3d03: {} miembros pasan los cinco lugares de 0x77f3cd0 (Q-C960)", count));
    const Social::PartyState& party = runtime.social.party;
    const std::string_view hero = NameView(world.Hero().name);
    const std::string_view leader = NameView(party.slots[0].name);
    for (std::int32_t at = 0; at < count; ++at)
    {
        const Social::PartySlot& slot = party.slots[static_cast<std::size_t>(at)];
        if (hero != leader && hero != NameView(slot.name))
            continue;
        const std::int32_t left = runtime.hud.partyX + KickLeft;
        const std::int32_t top = runtime.hud.partyY + KickTop + at * KickStep;
        if (context.mouseX < left || context.mouseX >= left + KickSize || context.mouseY < top ||
            context.mouseY >= top + KickSize || !runtime.mouse.pendingClick)
            continue;
        runtime.mouse.pendingClick = false;
        runtime.RequestSound(KickSound);
        if (context.requests == nullptr)
            return Core::Unexpected(std::string("0x5e3ded: echar de la party (C1 04 43) sin el cable"));
        context.requests->SendGamePacket(Protocol::Party::WritePartyLeave(slot.number));
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepThePartyWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::PartyPanel))
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t x = runtime.hud.partyX;
    const std::int32_t y = runtime.hud.partyY;
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    if (mouseX >= x && mouseX < x + WindowWidth && mouseY >= y && mouseY < y + WindowHeight)
        runtime.ui.mouseOnUi = 1;
    // 0x5e3cf0..0x5e40b6: with members, each one's kick button, if the hero is the first (the leader) or that member:
    // it spends the click, plays the sound and sends the C1 04 43 with its number, with the XOR32 and without C3. The
    // loop goes on with the click spent.
    if (auto kicked = StepTheKickButtons(context); !kicked.HasValue())
        return kicked;
    if (mouseX < x + CloseLeft || mouseX >= x + CloseLeft + CloseSize || mouseY < y + CloseTop ||
        mouseY >= y + CloseTop + CloseSize || !runtime.mouse.pendingClick)
        return {};
    runtime.mouse.pendingClick = false;
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = CloseMouseUpdateTimeMax;
    auto closed = panels.Close(Ui::PartyPanel);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> DrawThePartyWindow(Ui::GameHudContext& context, Ui::PanelRegistry& panels,
                                                     std::int32_t x, std::int32_t y)
{
    Kernel::HudState& hud = context.hud;
    if (!panels.IsOpen(Ui::PartyPanel))
    {
        // 0x5f12d0..0x5f1304: the world's width (the list's x, minus 50) and the colors, before looking at the count.
        const std::int32_t width = panels.WorldWidth();
        hud.textColors = ClosedColors;
        if (hud.partyCount == 0)
            return {};
        return DrawThePartyList(context, width);
    }
    Render::SpriteBatch& sprites = context.sprites;
    hud.partyX = x;
    hud.partyY = y;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    Ui::WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    auto title = TextOf(context, TitleText, "0x5f0cf6");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    hud.textColors = TitleColors;
    Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
    Ui::WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
    hud.textColors = LineColors;
    if (hud.partyCount == 0)
    {
        for (std::size_t line = 0; line < EmptyLineTops.size(); ++line)
        {
            auto text = TextOf(context, FirstEmptyText + line, "0x5f0d45");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            Ui::WindowDrawing::DrawText(context, x + EmptyLineLeft, y + EmptyLineTops[line], text.Value());
        }
    }
    // 0x5f0dde..0x5f0dea: with members, their names, their life and their kick button; with a negative count, nothing.
    if (auto members = DrawTheMembers(context, x, y); !members.HasValue())
        return members;
    // 0x5f11c2: the corner of 0x78a7068 and 0x78a706c, the one it just set.
    if (!Ui::WindowDrawing::DrawTheCloseButton(context, hud.partyX, hud.partyY))
        return {};
    auto help = TextOf(context, CloseHelpText, "0x5f12aa");
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    Ui::WindowDrawing::DrawTheCloseHelp(context, hud.partyX, hud.partyY, help.Value());
    return {};
}

Core::Expected<void, std::string> DrawThePartyLife(Ui::GameHudContext& context)
{
    Kernel::HudState& hud = context.hud;
    if (hud.partyCount <= 0)
        return {};
    auto counted = Members(hud.partyCount, "0x5a89e5");
    if (!counted.HasValue())
        return Core::Unexpected(std::move(counted).Error());
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::WorldState& world = context.world;
    const auto actors = world.Actors();
    const Social::PartyState& party = world.Runtime().social.party;
    for (std::size_t at = 0; at < counted.Value(); ++at)
    {
        const Social::PartySlot& slot = party.slots[at];
        if (slot.actorIndex <= -1)
            continue;
        const auto index = static_cast<std::size_t>(slot.actorIndex);
        if (index >= actors.size())
            return Core::Unexpected(std::format("0x5a89f1: el personaje {} de la party pasa el pool", index));
        if (context.projection == nullptr)
            return Core::Unexpected(std::string("0x5a8a2e: la barra de la party no tiene la proyección del pase del "
                                                "mundo"));
        const Kernel::ActorState& actor = actors[index];
        // 0x5a8a00..0x5a8a2b: FLD +0x138, FADD +0x18 and FADD 100 on the x87, and FSTP to float.
        const glm::vec3 above{
            actor.position.x, actor.position.y,
            F32(static_cast<double>(actor.boundsTop) + static_cast<double>(actor.position.z) + AboveTheHead)};
        const auto projected = Scenes::ProjectGamePoint(*context.projection, above);
        if (!projected.HasValue())
            return Core::Unexpected(projected.Error());
        const std::int32_t x = projected.Value().x - LifeLeft;
        const std::int32_t y = projected.Value().y;
        // 0x5a8a42..0x5a8b3e: the mouse over the bar. 0x6c8e60 stays at 0xffd2e6ff: only 0x553e50 reads it, in the
        // columns of a balloon's prefix, which 0x555b00 sets first (MuRet does not keep it). The text goes out in the
        // DC's font with whatever colors are set (0x465950 with mode 1) and leaves its size in 0x77f31b8 and 0x77f31bc.
        const Ui::Point mouse = context.mouse;
        if (mouse.x >= x && static_cast<double>(x) + LifeHoverWidth > static_cast<double>(mouse.x) &&
            mouse.y >= y - LifeHoverAbove && mouse.y < y + LifeHoverBelow)
            Ui::WindowDrawing::DrawText(context, x, y - LifeTextAbove, std::format("HP : {}0%", slot.tenths));
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(LifeShadow);
        sprites.ColorQuad(static_cast<float>(x + 1), static_cast<float>(y + 1), LifeWidth, LifeHeight);
        sprites.SetBlend(BlendMode::Additive);
        sprites.SetColor(LifeEdge);
        sprites.ColorQuad(static_cast<float>(x), static_cast<float>(y), LifeWidth, LifeHeight);
        sprites.SetColor(LifeBack);
        sprites.ColorQuad(static_cast<float>(x + 2), static_cast<float>(y + 2), LifeInnerWidth, LifeInnerHeight);
        sprites.SetColor(LifeTenth);
        for (std::int32_t tenth = 0; tenth < TenthsOf(slot); ++tenth)
            sprites.ColorQuad(static_cast<float>(x + tenth * TenthStep + 2), static_cast<float>(y + 2), TenthWidth,
                              TenthHeight);
        sprites.SetBlend(BlendMode::Opaque);
    }
    // 0x5a8c81..0x5a8c95.
    sprites.SetBlend(BlendMode::Opaque);
    sprites.SetColor(White);
    return {};
}
} // namespace Client::Party
