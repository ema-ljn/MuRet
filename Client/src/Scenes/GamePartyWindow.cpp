#include "Scenes/GamePartyWindow.h"

#include "Core/Expected.h"
#include "Protocol/Party.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

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

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5e3cb5..0x5e3ce8: el mouse sobre la ventana, de 0xbe × 0x1b1 desde la esquina.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x5e40d4..0x5e40f2: el botón de cerrar, de 0x18 × 0x18 en (0x19, 0x18b) de la esquina.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x5e410d..0x5e4113.
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x5e3d83..0x5e3dc5: el botón de echar, de 0x18 × 0x18 en (0x8c, 0x37 + 0x23 · lugar) de la esquina.
constexpr std::int32_t KickLeft = 0x8c;
constexpr std::int32_t KickTop = 0x37;
constexpr std::int32_t KickStep = 0x23;
constexpr std::int32_t KickSize = 0x18;
// 0x5e3ddc.
constexpr std::int32_t KickSound = 0x19;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300): el título, los siete renglones de la party vacía
// (0x76cb1b0..0x76cb8b8) y la ayuda del botón de cerrar (0x76cd4d8).
constexpr std::size_t TitleText = 190;
constexpr std::size_t FirstEmptyText = 191;
constexpr std::size_t CloseHelpText = 221;
// 0x5f0d3f..0x5f0dc3: los renglones en x + 0x14 y en y + estas.
constexpr std::array<std::int32_t, 7> EmptyLineTops{0x32, 0x41, 0x50, 0x64, 0x73, 0x82, 0x91};
constexpr std::int32_t EmptyLineLeft = 0x14;
// 0x5f0cf3..0x5f0cfb: el título en (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;

// 0x6c8e5c y 0x6c8e64 del título (0x5f0cbe), de los renglones (0x5f0d14) y de la party cerrada (0x5f12eb).
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr Render::Text::TextColors LineColors{.text = 0xffe6e6e6u, .background = 0};
constexpr Render::Text::TextColors ClosedColors{.text = 0xffffffffu, .background = 0};

// Los colores de GL de la party, con los bits de los PUSH.
constexpr float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}
constexpr Render::Color LifeBack{Bits(0x3e48c8c9u), Bits(0x3d20a0a1u), 0.0f, 1.0f};
constexpr Render::Color LifeTenth{Bits(0x3f7afafbu), Bits(0x3d20a0a1u), 0.0f, 1.0f};

// 0x5a89b0: la barra sobre la cabeza (+0x138 + z + 100, 0x6b86b4), 19 a la izquierda (0x5a8a3c), de 42 × 5 con su
// sombra (0, 0, 0, 0,5) un punto abajo a la derecha, el borde (0,2; 0; 0) sumado, el fondo de 38 × 1 y los décimos
// de 3 × 2 cada 4. El "HP" con el mouse en [x, x + 38) × [y − 2, y + 6).
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

// 0x5f130a..0x5f168a: la lista de la derecha, desde el ancho del mundo − 50 (0x6b86b0) y y 5, de a 19 (0x6b8870).
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
// 0x5f1583..0x5f1665: Message_box2 (0xfc) arriba y abajo, de 54 × 3 desde x − 5, y los costados de 1 en x − 4 y
// x + 47 (0x6ba644), de 19 por miembro desde y 5.
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

// 0x5f0df0..0x5f11ad: cada miembro en la ventana abierta, de a 0x23.
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
// 0x5f108f..0x5f11a1: el botón de echar, exit_01.jpg (0x118) o exit_02.jpg (0x119) con el izquierdo abajo, de 24 × 24
// en (x + 140, y + 55 + 0x23 · lugar), con su ayuda (texto 189, 0x76caf58) 13 arriba.
constexpr std::uint16_t KickSlot = 0x118;
constexpr std::uint16_t KickPressedSlot = 0x119;
constexpr double KickDrawnLeft = 140.0;
constexpr double KickDrawnTop = 55.0;
constexpr float KickDrawnSize = 24.0f;
constexpr float KickUv = 0.75f;
constexpr std::int32_t KickHelpAbove = 0xd;
constexpr std::size_t KickHelpText = 189;
constexpr Render::Text::TextColors KickHelpColors{.text = 0xffffffffu, .background = 0xff000000u};

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}
// Un nombre de diez bytes hasta su primer 0 (strcmp entre el +0x1d9 del héroe y el lugar de 0x77f3cd0; *inferido*
// con diez letras, donde strcmp sigue en +0x1e3 del héroe contra el 0 de +0xa del lugar).
std::string_view NameView(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}

float F32(double value)
{
    return static_cast<float>(value);
}

// _ftol: hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

// 0x5f0b40(mapa): el texto de Text.bmd con su nombre.
std::size_t MapNameText(std::uint8_t map)
{
    constexpr std::size_t FirstMapText = 30;
    switch (map)
    {
    case 0x22:
        return 1851;
    case 0x21:
        return 1850;
    case 0x1e:
        return 669;
    case 0x1f:
        return 59;
    case 0x20:
        return 39;
    case 0x0a:
        return 55;
    case 0x24:
        return 58;
    default:
        break;
    }
    if (map > 0x11 && map < 0x18)
        return 57;
    if (map > 0x17 && map < 0x1e)
        return 58;
    if (map > 0x0a && map < 0x12)
        return 56;
    return FirstMapText + map;
}

Core::Expected<std::size_t, std::string> Members(std::int32_t count, std::string_view where)
{
    if (count > static_cast<std::int32_t>(PartySlots))
        return Core::Unexpected(
            std::format("{}: {} miembros pasan los cinco lugares de 0x77f3cd0 (Q-C960)", where, count));
    return static_cast<std::size_t>(std::max(count, 0));
}

// Los décimos de la vida (+0x18, del 44), hasta 10.
std::int32_t TenthsOf(const PartySlot& slot)
{
    return std::min(slot.tenths, MaxTenths);
}

// 0x5f130a..0x5f168a.
Core::Expected<void, std::string> DrawThePartyList(GameHudContext& context, std::int32_t width)
{
    HudState& hud = context.hud;
    auto counted = Members(hud.partyCount, "0x5f130a");
    if (!counted.HasValue())
        return Core::Unexpected(std::move(counted).Error());
    Render::SpriteBatch& sprites = context.sprites;
    const PartyState& party = context.world.Runtime().social.party;
    const float left = F32(static_cast<double>(width) - ListFromTheRight);
    const auto mouseX = static_cast<double>(context.mouse.x);
    const auto mouseY = static_cast<double>(context.mouse.y);
    float top = ListTop;
    const float barLeft = F32(static_cast<double>(left) - ListBarLeft);
    const float backLeft = F32(static_cast<double>(left) - ListBackLeft);
    for (std::size_t at = 0; at < counted.Value(); ++at)
    {
        const PartySlot& slot = party.slots[at];
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
        WindowDrawing::SelectFont(context, FontKind::Normal);
        WindowDrawing::DrawText(context, Truncate(static_cast<double>(left)),
                                Truncate(static_cast<double>(top)) + ListNameTop, NameView(slot.name));
        top = F32(static_cast<double>(top) + ListRow);
    }
    const float edgeLeft = F32(static_cast<double>(left) - ListEdgeLeft);
    WindowDrawing::Quad(sprites, ListEdgeSlot, edgeLeft, ListEdgeTop, ListEdgeWidth, ListEdgeHeight, ListEdgeDu,
                        ListEdgeDv);
    WindowDrawing::Quad(sprites, ListEdgeSlot, edgeLeft, top, ListEdgeWidth, ListEdgeHeight, ListEdgeDu, ListEdgeDv);
    sprites.SetColor(ListSideColor);
    const auto sides = static_cast<float>(hud.partyCount * ListSideStep);
    sprites.ColorQuad(F32(static_cast<double>(left) - ListLeftSide), ListSideTop, ListSideWidth, sides);
    sprites.ColorQuad(F32(static_cast<double>(left) + ListRightSide), ListSideTop, ListSideWidth, sides);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    return {};
}

// 0x5f0df0..0x5f11ad: los miembros en la ventana abierta en (x, y).
Core::Expected<void, std::string> DrawTheMembers(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    HudState& hud = context.hud;
    auto counted = Members(hud.partyCount, "0x5f0df0");
    if (!counted.HasValue())
        return Core::Unexpected(std::move(counted).Error());
    Render::SpriteBatch& sprites = context.sprites;
    WorldState& world = context.world;
    const PartyState& party = world.Runtime().social.party;
    const std::string_view hero = NameView(world.Actors()[world.Runtime().hero].name);
    const bool leader = NameView(party.slots[0].name) == hero;
    for (std::size_t at = 0; at < counted.Value(); ++at)
    {
        const PartySlot& slot = party.slots[at];
        const std::int32_t row = static_cast<std::int32_t>(at) * MemberStep;
        hud.textColors = at == 0 ? LeaderColors : MemberColors;
        WindowDrawing::SelectFont(context, FontKind::Bold);
        WindowDrawing::DrawText(context, x + MemberLeft, row + y + MemberNameTop, NameView(slot.name));
        hud.textColors = MemberDataColors;
        WindowDrawing::SelectFont(context, FontKind::Normal);
        auto map = TextOf(context, MapNameText(slot.map), "0x5f0ec8");
        if (!map.HasValue())
            return Core::Unexpected(std::move(map).Error());
        WindowDrawing::DrawText(context, x + MemberLeft, row + y + MemberMapTop, map.Value());
        WindowDrawing::DrawText(context, x + MemberCellLeft, row + y + MemberMapTop,
                                std::format("{},{}", slot.x, slot.y));
        sprites.SetColor(MemberBarColor);
        sprites.ColorQuad(static_cast<float>(x + MemberBarLeft), static_cast<float>(row + y + MemberBarTop),
                          MemberBarWidth, MemberBarHeight);
        sprites.SetColor(MemberLifeColor);
        // 0x5f0f71..0x5f0fbb: la menor con signo, FIDIV por la máxima (con 0, lo que dé la x87) y · 90 (0x6b8948).
        const auto life = static_cast<std::int32_t>(slot.life);
        const auto maxLife = static_cast<std::int32_t>(slot.maxLife);
        const std::int32_t shown = life < maxLife ? life : maxLife;
        const float lifeWidth = F32(static_cast<double>(shown) / static_cast<double>(maxLife) * MemberLifeWidth);
        sprites.ColorQuad(static_cast<float>(x + MemberLeft), static_cast<float>(row + y + MemberLifeTop), lifeWidth,
                          MemberLifeHeight);
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(White);
        WindowDrawing::DrawText(context, x + MemberNumbersLeft, row + y + MemberNumbersTop,
                                std::format("( {} / {} )", life, maxLife));
        // 0x5f1013..0x5f1089: el botón con el héroe de jefe (el primer lugar) o en este lugar.
        if (!leader && NameView(slot.name) != hero)
            continue;
        const float left = F32(static_cast<double>(hud.partyX) + KickDrawnLeft);
        const float top = F32(static_cast<double>(row) + static_cast<double>(hud.partyY) + KickDrawnTop);
        WindowDrawing::Quad(sprites, KickSlot, left, top, KickDrawnSize, KickDrawnSize, KickUv, KickUv);
        const auto mouseX = static_cast<double>(context.mouse.x);
        const auto mouseY = static_cast<double>(context.mouse.y);
        if (mouseX < static_cast<double>(left) || mouseX >= static_cast<double>(left) + KickDrawnSize ||
            mouseY < static_cast<double>(top) || mouseY >= static_cast<double>(top) + KickDrawnSize)
            continue;
        if (context.leftDown)
            WindowDrawing::Quad(sprites, KickPressedSlot, left, top, KickDrawnSize, KickDrawnSize, KickUv, KickUv);
        auto help = TextOf(context, KickHelpText, "0x5f118f");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        hud.textColors = KickHelpColors;
        WindowDrawing::DrawText(context, Truncate(static_cast<double>(left)),
                                Truncate(static_cast<double>(top)) - KickHelpAbove, help.Value());
    }
    return {};
}

Core::Expected<void, std::string> StepTheKickButtons(const GameUiContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const std::int32_t count = runtime.hud.partyCount;
    if (count <= 0)
        return {};
    if (count > static_cast<std::int32_t>(PartySlots))
        return Core::Unexpected(
            std::format("0x5e3d03: {} miembros pasan los cinco lugares de 0x77f3cd0 (Q-C960)", count));
    const PartyState& party = runtime.social.party;
    const std::string_view hero = NameView(world.Actors()[runtime.hero].name);
    const std::string_view leader = NameView(party.slots[0].name);
    for (std::int32_t at = 0; at < count; ++at)
    {
        const PartySlot& slot = party.slots[static_cast<std::size_t>(at)];
        if (hero != leader && hero != NameView(slot.name))
            continue;
        const std::int32_t left = runtime.hud.partyX + KickLeft;
        const std::int32_t top = runtime.hud.partyY + KickTop + at * KickStep;
        if (context.mouseX < left || context.mouseX >= left + KickSize || context.mouseY < top ||
            context.mouseY >= top + KickSize || !runtime.movement.pendingClick)
            continue;
        runtime.movement.pendingClick = false;
        runtime.lastSoundId = KickSound;
        if (context.requests == nullptr)
            return Core::Unexpected(std::string("0x5e3ded: echar de la party (C1 04 43) sin el cable"));
        context.requests->SendGamePacket(Protocol::Party::WritePartyLeave(slot.number));
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepThePartyWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::PartyPanel))
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t x = runtime.hud.partyX;
    const std::int32_t y = runtime.hud.partyY;
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    if (mouseX >= x && mouseX < x + WindowWidth && mouseY >= y && mouseY < y + WindowHeight)
        runtime.ui.mouseOnUi = 1;
    // 0x5e3cf0..0x5e40b6: con miembros, el botón de echar de cada uno, si el héroe es el primero (el jefe) o ese
    // miembro: el clic lo gasta, suena y manda el C1 04 43 con su número, en claro. La vuelta sigue con el clic
    // gastado.
    if (auto kicked = StepTheKickButtons(context); !kicked.HasValue())
        return kicked;
    if (mouseX < x + CloseLeft || mouseX >= x + CloseLeft + CloseSize || mouseY < y + CloseTop ||
        mouseY >= y + CloseTop + CloseSize || !runtime.movement.pendingClick)
        return {};
    runtime.movement.pendingClick = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
    auto closed = panels.Close(UI::PartyPanel);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> DrawThePartyWindow(GameHudContext& context, UI::PanelRegistry& panels, std::int32_t x,
                                                     std::int32_t y)
{
    HudState& hud = context.hud;
    if (!panels.IsOpen(UI::PartyPanel))
    {
        // 0x5f12d0..0x5f1304: el ancho del mundo (la x de la lista, menos 50) y los colores, antes de mirar la cuenta.
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
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    auto title = TextOf(context, TitleText, "0x5f0cf6");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    hud.textColors = TitleColors;
    WindowDrawing::SelectFont(context, FontKind::Bold);
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    WindowDrawing::SelectFont(context, FontKind::Normal);
    hud.textColors = LineColors;
    if (hud.partyCount == 0)
    {
        for (std::size_t line = 0; line < EmptyLineTops.size(); ++line)
        {
            auto text = TextOf(context, FirstEmptyText + line, "0x5f0d45");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            WindowDrawing::DrawText(context, x + EmptyLineLeft, y + EmptyLineTops[line], text.Value());
        }
    }
    // 0x5f0dde..0x5f0dea: con miembros, sus nombres, su vida y su botón de echar; con la cuenta negativa, nada.
    if (auto members = DrawTheMembers(context, x, y); !members.HasValue())
        return members;
    // 0x5f11c2: la esquina de 0x78a7068 y 0x78a706c, la que acaba de poner.
    if (!WindowDrawing::DrawTheCloseButton(context, hud.partyX, hud.partyY))
        return {};
    auto help = TextOf(context, CloseHelpText, "0x5f12aa");
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    WindowDrawing::DrawTheCloseHelp(context, hud.partyX, hud.partyY, help.Value());
    return {};
}

Core::Expected<void, std::string> DrawThePartyLife(GameHudContext& context)
{
    HudState& hud = context.hud;
    if (hud.partyCount <= 0)
        return {};
    auto counted = Members(hud.partyCount, "0x5a89e5");
    if (!counted.HasValue())
        return Core::Unexpected(std::move(counted).Error());
    Render::SpriteBatch& sprites = context.sprites;
    WorldState& world = context.world;
    const auto actors = world.Actors();
    const PartyState& party = world.Runtime().social.party;
    for (std::size_t at = 0; at < counted.Value(); ++at)
    {
        const PartySlot& slot = party.slots[at];
        if (slot.actorIndex <= -1)
            continue;
        const auto index = static_cast<std::size_t>(slot.actorIndex);
        if (index >= actors.size())
            return Core::Unexpected(std::format("0x5a89f1: el personaje {} de la party pasa el pool", index));
        if (context.projection == nullptr)
            return Core::Unexpected(std::string("0x5a8a2e: la barra de la party no tiene la proyección del pase del "
                                                "mundo"));
        const ActorState& actor = actors[index];
        // 0x5a8a00..0x5a8a2b: FLD +0x138, FADD +0x18 y FADD 100 en la x87, y FSTP en float.
        const glm::vec3 above{
            actor.position.x, actor.position.y,
            F32(static_cast<double>(actor.boundsTop) + static_cast<double>(actor.position.z) + AboveTheHead)};
        const auto projected = ProjectGamePoint(*context.projection, above);
        if (!projected.HasValue())
            return Core::Unexpected(projected.Error());
        const std::int32_t x = projected.Value().x - LifeLeft;
        const std::int32_t y = projected.Value().y;
        // 0x5a8a42..0x5a8b3e: el mouse sobre la barra. 0x6c8e60 queda en 0xffd2e6ff: solo lo lee 0x553e50 en las
        // columnas del prefijo de un globo, que 0x555b00 pone antes (MuRet no lo guarda). El texto sale con la fuente
        // del DC y los colores que haya (0x465950 con el modo 1) y deja su tamaño en 0x77f31b8 y 0x77f31bc.
        const UI::Point mouse = context.mouse;
        if (mouse.x >= x && static_cast<double>(x) + LifeHoverWidth > static_cast<double>(mouse.x) &&
            mouse.y >= y - LifeHoverAbove && mouse.y < y + LifeHoverBelow)
            WindowDrawing::DrawText(context, x, y - LifeTextAbove, std::format("HP : {}0%", slot.tenths));
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
} // namespace Client::Scenes
