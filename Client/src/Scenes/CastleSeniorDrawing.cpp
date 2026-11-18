#include "Render/SpriteVertex.h"
#include "Scenes/CastleWindowDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Scenes/CastleWindowSupport.h"
#include "Scenes/CastleWindows.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleSeniorWindow.h"
#include "UI/CastleSiegeState.h"
#include "UI/ChatLogWindow.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/SprintfFormat.h"
#include "UI/Widget.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Scenes::CastleDraw
{
namespace
{
constexpr std::int32_t Top = 65;

std::int32_t Signed(const UI::CastleData& data, std::uint32_t at)
{
    return std::bit_cast<std::int32_t>(data.U32(at));
}

std::int32_t Defense(std::uint32_t type, std::int32_t level)
{
    constexpr std::array<std::int32_t, 4> Gate{100, 180, 300, 520};
    constexpr std::array<std::int32_t, 4> Statue{80, 180, 340, 550};
    if (type != UI::GateType && type != UI::StatueType)
        return -1;
    const std::size_t tier = level >= 0 && level <= 2 ? static_cast<std::size_t>(level) : 3;
    return (type == UI::GateType ? Gate : Statue)[tier];
}

std::int32_t Recovery(std::uint32_t type, std::int32_t level)
{
    if (type == UI::GateType)
        return 0;
    if (type != UI::StatueType)
        return -1;
    return level >= 0 && level <= 2 ? level : 3;
}

std::int32_t LifeDelta(std::uint32_t type, std::int32_t maxLife)
{
    if (type == UI::GateType)
        return maxLife == 500000 ? 300000 : maxLife == 800000 ? 700000 : maxLife == 1500000 ? 1500000 : 0;
    if (type == UI::StatueType)
        return maxLife == 400000 ? 350000 : maxLife == 750000 ? 550000 : maxLife == 1300000 ? 1200000 : 0;
    return -1;
}

std::int32_t DefenseDelta(std::uint32_t type, std::int32_t level)
{
    constexpr std::array<std::int32_t, 3> Gate{80, 120, 220};
    constexpr std::array<std::int32_t, 3> Statue{100, 160, 210};
    if (type != UI::GateType && type != UI::StatueType)
        return -1;
    if (level < 0 || level > 2)
        return 0;
    return (type == UI::GateType ? Gate : Statue)[static_cast<std::size_t>(level)];
}

float TierWidth(std::int32_t tier)
{
    return static_cast<float>(std::bit_cast<std::int32_t>((static_cast<std::uint32_t>(tier) * 3u + 3u) * 2u));
}

void NpcIcon(GameHudContext& context, const UI::CastleData& data, std::uint32_t at, std::int32_t x, std::int32_t y)
{
    const auto type = data.U32(at + UI::RecordType);
    if (type != UI::GateType && type != UI::StatueType)
        return;
    const bool statue = type == UI::StatueType;
    auto& sprites = context.sprites;
    sprites.SetColor({1, 1, 1, 1});
    if (data.bytes.at(at + UI::RecordAlive - UI::CastleDataBase) != 0)
    {
        const auto maxLife = Signed(data, at + UI::RecordMaxLife);
        const float width = TierWidth(UI::LifeLevelOf(type, maxLife));
        const float left = static_cast<float>(x);
        const float top = static_cast<float>(y - (statue ? 15 : 10));
        sprites.SetBlend(Render::BlendMode::Opaque);
        sprites.ColorQuad(left, top, width, 3);
        sprites.ColorQuad(left, static_cast<float>(y - (statue ? 10 : 5)), 24, 3);
        if (statue)
            sprites.ColorQuad(left, static_cast<float>(y - 5), 24, 3);
        const double ratio = static_cast<double>(static_cast<float>(Signed(data, at + UI::RecordLife))) /
                             static_cast<double>(static_cast<float>(maxLife));
        sprites.SetColor({1, 0, 0, 1});
        sprites.ColorQuad(left, top, static_cast<float>(static_cast<double>(width) * ratio), 3);
        if (!statue)
        {
            sprites.SetColor({0, 0, 0, 1});
            sprites.ColorQuad(left, top, 24, 1);
            sprites.ColorQuad(left, static_cast<float>(y - 7), 24, 1);
            sprites.ColorQuad(left, top, 1, 3);
            sprites.ColorQuad(left + 24, top, 1, 3);
        }
        sprites.SetColor({0, 1, 0, 1});
        sprites.ColorQuad(left, static_cast<float>(y - (statue ? 10 : 5)),
                          TierWidth(Signed(data, at + UI::RecordDefense)), 3);
        if (statue)
        {
            sprites.SetColor({1, 1, 0, 1});
            sprites.ColorQuad(left, static_cast<float>(y - 5), TierWidth(Signed(data, at + UI::RecordRecovery)), 3);
        }
        UI::OldDraw::WhiteWithTexture(sprites);
    }
    const auto selected = data.U32(statue ? UI::SelectedStatueAt : UI::SelectedGateAt);
    const bool chosen = data.U32(at + UI::RecordIndex) == selected + 1u;
    const std::uint16_t slot = statue ? (chosen ? 0x664 : 0x663) : (chosen ? 0x662 : 0x661);
    WindowDrawing::Quad(sprites, slot, static_cast<float>(x), static_cast<float>(y), 24, 24, 0.75f, 0.75f);
}

Core::Expected<void, std::string> UpgradeText(GameHudContext& context, std::int32_t x, std::int32_t y,
                                              std::uint32_t text, std::int32_t value)
{
    auto format = CastleUi::Text(context.texts, text);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    WindowDrawing::DrawText(context, x, y, UI::Sprintf(format.Value(), value));
    return {};
}

Core::Expected<void, std::string> NpcDetails(GameHudContext& context, UI::CastleSeniorWindow& window,
                                             UI::CastleData& data, std::int32_t x, bool statue)
{
    const auto selected = data.U32(statue ? UI::SelectedStatueAt : UI::SelectedGateAt);
    const auto at = (statue ? UI::FirstStatueAt : UI::FirstGateAt) + selected * UI::NpcRecordBytes;
    if (!UI::CastleData::Holds(at, UI::NpcRecordBytes))
        return Core::Unexpected(std::string("el dibujo de Senior lee un registro fuera de 0x7d5c504..0x7d5c658"));
    const bool alive = data.bytes.at(at + UI::RecordAlive - UI::CastleDataBase) != 0;
    auto& buttons = window.buttons;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    if (!alive)
        return Button(context, buttons[0], x + 70, Top + 188, 0x772f3a4, 0);
    const auto life = Signed(data, at + UI::RecordLife), maximum = Signed(data, at + UI::RecordMaxLife);
    const auto type = data.U32(at + UI::RecordType);
    const auto defense = CastleUi::DefenseLevel(data, at), recovery = CastleUi::RecoveryLevel(data, at);
    const auto tier = UI::LifeLevelOf(type, maximum);
    if (auto button =
            Button(context, buttons[0], x + 70, Top + (statue ? 231 : 216), 0x772f4d0, life == maximum ? 6 : 0);
        !button.HasValue())
        return button;
    if (auto button = Button(context, buttons[1], x + 115, Top + 280, 0x772ea44, tier == 3 ? 6 : 0); !button.HasValue())
        return button;
    if (auto button = Button(context, buttons[2], x + 115, Top + 298, 0x772ea44, defense == 3 ? 6 : 0);
        !button.HasValue())
        return button;
    if (statue)
        if (auto button = Button(context, buttons[3], x + 115, Top + 316, 0x772ea44, recovery == 3 ? 6 : 0);
            !button.HasValue())
            return button;
    context.hud.textColors = {.text = 0xffffffffu, .background = 0};
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto line = Formatted(context, x, Top + 188, 0x772f5fc, life, maximum); !line.HasValue())
        return line;
    if (auto line = Formatted(context, x, Top + 202, 0x772f728, Defense(type, defense)); !line.HasValue())
        return line;
    if (statue)
        if (auto line = Formatted(context, x, Top + 216, 0x772f854, Recovery(type, recovery)); !line.HasValue())
            return line;
    WindowDrawing::Quad(context.sprites, 0x117, static_cast<float>(x), static_cast<float>(Top + (statue ? 252 : 240)),
                        190, 10, 0.7421875f, 0.625f);
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    if (auto line = Text(context, x, Top + (statue ? 267 : 259), 0x772ea44); !line.HasValue())
        return line;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    if (auto line = UpgradeText(context, x + 30, Top + (statue ? 284 : 283), 0x772f980, LifeDelta(type, maximum));
        !line.HasValue())
        return line;
    if (auto line = UpgradeText(context, x + 30, Top + 301, 0x772faac, DefenseDelta(type, defense)); !line.HasValue())
        return line;
    if (statue)
        return UpgradeText(context, x + 30, Top + 318, 0x772fbd8, recovery >= 0 && recovery <= 2 ? 1 : 0);
    return {};
}

Core::Expected<void, std::string> Npcs(GameHudContext& context, UI::CastleSeniorWindow& window, UI::CastleData& data,
                                       std::int32_t x, bool statue)
{
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    if (auto title = Text(context, x, Top + 6, 0x772f278); !title.HasValue())
        return title;
    WindowDrawing::Quad(context.sprites, 0x660, static_cast<float>(x + 15), static_cast<float>(Top + 18), 160, 165,
                        0.625f, 0.64453125f);
    constexpr std::array<UI::Point, 6> Gates{{{82, 53}, {64, 101}, {100, 101}, {48, 153}, {82, 153}, {116, 153}}};
    constexpr std::array<UI::Point, 4> Statues{{{82, 38}, {82, 83}, {64, 128}, {100, 128}}};
    const auto count = statue ? Statues.size() : Gates.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& p = statue ? Statues[i] : Gates[i];
        NpcIcon(context, data,
                (statue ? UI::FirstStatueAt : UI::FirstGateAt) + static_cast<std::uint32_t>(i) * UI::NpcRecordBytes,
                x + p.x, Top + p.y);
    }
    return NpcDetails(context, window, data, x, statue);
}

void TaxArrow(GameHudContext& context, UI::CastleData& data, std::uint32_t address, std::int32_t x, std::int32_t y,
              bool increment, bool& released)
{
    const bool hit = UI::MouseInRect(context.mouse.x, context.mouse.y, x, y, 13, 13, 1);
    const bool pressed = context.world.Runtime().movement.pendingClick && hit;
    UI::OldDraw::Quad(context.sprites, 0x649, static_cast<float>(x), static_cast<float>(y), 13, 13,
                      pressed ? 0.8125f : 0, increment ? (pressed ? 0.90625f : 0.09375f) : 0.5f,
                      pressed ? -0.8125f : 0.8125f, pressed ? -0.40625f : 0.40625f);
    if (!released || !hit)
        return;
    auto value = std::bit_cast<std::int32_t>(data.U32(address) + (increment ? 1u : 0xffffffffu));
    if (increment && value > 3)
        value = 3;
    if (!increment && value < 0)
        value = 0;
    data.SetU32(address, static_cast<std::uint32_t>(value));
    released = false;
}

Core::Expected<void, std::string> Taxes(GameHudContext& context, UI::CastleSeniorWindow& window, UI::CastleData& data,
                                        std::int32_t x)
{
    context.hud.textColors = {.text = 0xffffffffu, .background = 0};
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    if (auto title = Text(context, x + 30, Top + 7, 0x773040c); !title.HasValue())
        return title;
    context.sprites.SetColor({0.5f, 0.6f, 0.5f, 0.5f});
    context.sprites.ColorQuad(static_cast<float>(x + 20), Top + 22, 140, 24);
    context.sprites.ColorQuad(static_cast<float>(x + 20), Top + 47, 140, 24);
    UI::OldDraw::WhiteWithTexture(context.sprites);
    bool projectedRelease = context.world.Runtime().movement.releasedClick;
    bool& release = context.projecting ? projectedRelease : context.world.Runtime().movement.releasedClick;
    TaxArrow(context, data, UI::ChaosTaxAt, x + 157, Top + 22, true, release);
    TaxArrow(context, data, UI::ChaosTaxAt, x + 157, Top + 34, false, release);
    TaxArrow(context, data, UI::StoreTaxAt, x + 157, Top + 47, true, release);
    TaxArrow(context, data, UI::StoreTaxAt, x + 157, Top + 59, false, release);
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    if (auto line = FormattedWidth(context, x, Top + 30, 175, 0x7730538, data.U32(UI::ServerChaosTaxAt),
                                   data.U32(UI::ChaosTaxAt));
        !line.HasValue())
        return line;
    if (auto line = FormattedWidth(context, x, Top + 55, 175, 0x7730664, data.U32(UI::ServerStoreTaxAt),
                                   data.U32(UI::StoreTaxAt));
        !line.HasValue())
        return line;
    constexpr std::array<std::uint32_t, 3> Labels{0x7730790, 0x77308bc, 0x77309e8};
    for (std::size_t i = 0; i < Labels.size(); ++i)
    {
        auto line = CastleUi::Text(context.texts, Labels[i]);
        if (!line.HasValue())
            return Core::Unexpected(std::move(line).Error());
        WindowDrawing::DrawText(context, x + (i == 0 ? 20 : 26), Top + 75 + static_cast<std::int32_t>(i) * 13,
                                line.Value());
    }
    if (auto button = Button(context, window.buttons[4], x + 70, Top + 116, 0x770e1f4, window.buttons[4].base.state);
        !button.HasValue())
        return button;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    context.hud.textColors.text = 0xff947bbbu;
    for (std::int32_t i = 0; i < 6; ++i)
        if (auto line = Text(context, x + 15, Top + 136 + 12 * i, 0x7730b14 + static_cast<std::uint32_t>(i) * 300, 160);
            !line.HasValue())
            return line;
    WindowDrawing::Quad(context.sprites, 0x117, static_cast<float>(x), Top + 216, 190, 10, 0.7421875f, 0.625f);
    context.hud.textColors.text = 0xffffffffu;
    const auto money = data.U64(UI::CastleMoneyAt);
    if (auto line = Formatted(context, x, Top + 236, 0x773121c, static_cast<std::uint32_t>(money),
                              static_cast<std::uint32_t>(money >> 32u));
        !line.HasValue())
        return line;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    if (auto button = Button(context, window.buttons[5], x + 70, Top + 256, 0x76ce66c, window.buttons[5].base.state);
        !button.HasValue())
        return button;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    context.hud.textColors.text = 0xff947bbbu;
    for (std::int32_t i = 0; i < 3; ++i)
        if (auto line = Text(context, x + 15, Top + 282 + 12 * i, 0x7731348 + static_cast<std::uint32_t>(i) * 300, 160);
            !line.HasValue())
            return line;
    return {};
}

Core::Expected<void, std::string> Craft(GameHudContext& context, UI::CastleSeniorWindow& window, std::int32_t x)
{
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    context.hud.textColors = {.text = 0xff947bbbu, .background = 0};
    if (!HeroCanCraftAtTheCastle(context.world))
        return Text(context, x, Top + 7, 0x7735614);
    for (std::int32_t i = 0; i < 3; ++i)
        if (auto line = Text(context, x, Top + 7 + 12 * i, 0x7736550 + static_cast<std::uint32_t>(i) * 300);
            !line.HasValue())
            return line;
    DrawTheGridBoxes(context.sprites, static_cast<float>(x + 15), Top + 47, context.world.Runtime().npcWindows.chaosBox,
                     8, 4);
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    for (std::int32_t i = 0; i < 6; ++i)
        if (auto line = Text(context, x, Top + 137 + 12 * i, 0x773586c + static_cast<std::uint32_t>(i) * 300);
            !line.HasValue())
            return line;
    return Button(context, window.buttons[6], x + 70, Top + 216, 0x76e8670, window.buttons[6].base.state);
}

Core::Expected<void, std::string> Tabs(GameHudContext& context, std::int32_t x, std::int32_t tab)
{
    constexpr std::array<std::uint32_t, 4> Labels{0x77317f8, 0x7731924, 0x7731a50, 0x77353bc};
    auto taxes = CastleUi::HeroCanEditTaxes(context.world);
    if (!taxes.HasValue())
        return Core::Unexpected(std::move(taxes).Error());
    for (std::int32_t i = 0; i < 4; ++i)
    {
        const bool selected = tab == i;
        const float gray = selected ? 1.0f : 0.6f;
        context.sprites.SetColor(i == 2 && !taxes.Value() ? Render::Color{1, 0, 0, 1}
                                                          : Render::Color{gray, gray, gray, 1});
        // 0x6698d0: el alto del cuarto tab consulta tab==2, mientras su y consulta tab==3.
        const bool tall = i == 3 ? tab == 2 : selected;
        WindowDrawing::Quad(context.sprites, 0x646, static_cast<float>(x + 15 + 40 * i), selected ? 48.0f : 50.0f, 40,
                            tall ? 18.0f : 16.0f, 0.75f, 0.9375f);
        if (auto label = Text(context, x + 15 + 40 * i, selected ? 53 : 54, Labels[static_cast<std::size_t>(i)], 40);
            !label.HasValue())
            return label;
    }
    context.sprites.SetColor({146.0f / 255, 144.0f / 255, 141.0f / 255, 1});
    for (std::int32_t i = 0; i < 4; ++i)
        if (tab != i)
            context.sprites.ColorQuad(static_cast<float>(x + 15 + 40 * i), 65, 40, 1);
    UI::OldDraw::WhiteWithTexture(context.sprites);
    return {};
}
} // namespace

Core::Expected<void, std::string> Senior(GameHudContext& context, UI::CastleSeniorWindow& window, UI::CastleData& data,
                                         std::int32_t tab)
{
    const std::int32_t x = tab == 3 ? 257 : 450;
    window.x = x;
    window.y = 0;
    window.right = x + 320;
    window.bottom = 300;
    if (auto back = Back(context, x, 0x77316cc); !back.HasValue())
        return back;
    if (auto tabs = Tabs(context, x, tab); !tabs.HasValue())
        return tabs;
    Core::Expected<void, std::string> body;
    if (tab == 0 || tab == 1)
        body = Npcs(context, window, data, x, tab == 1);
    else if (tab == 2)
        body = Taxes(context, window, data, x);
    else if (tab == 3)
        body = Craft(context, window, x);
    if (!body.HasValue())
        return body;
    return Close(context, x);
}
} // namespace Client::Scenes::CastleDraw
