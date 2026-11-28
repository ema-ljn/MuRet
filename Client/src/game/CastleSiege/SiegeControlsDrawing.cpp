#include "CastleSiege/SiegeBattleUi.h"

#include "Core/Expected.h"
#include "Tables/SkillTable.h"
#include "Models/LogicalAccess.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Hud/GameWindowDrawing.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Tooltips/SkillTooltip.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"
#include "CastleSiege/SiegeBattleUiState.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Movement/GameMovementState.h"
#include "Ui/PanelRegistry.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::CastleSiege
{
namespace
{
constexpr Render::Color White{};
constexpr Render::Color ButtonHighlight{1.0f, 0.8f, 0.6f, 1.0f};
constexpr std::uint32_t OpenGateFlag = 0x20000;
constexpr std::uint32_t LightGray = 0xffdcdcdc;

void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u = 0.0f,
          float v = 0.0f, float du = 1.0f, float dv = 1.0f)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = u,
                                      .v = v,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true});
}
Core::Expected<std::string_view, std::string> Text(const Hud::GameHudContext& context, std::size_t index)
{
    auto text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("Castle Siege: falta el texto {} de Text.bmd", index));
    return *text;
}
Core::Expected<void, std::string> Center(Hud::GameHudContext& context, std::int32_t x, std::int32_t y,
                                         std::size_t index)
{
    auto text = Text(context, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Hud::WindowDrawing::DrawCenteredText(context, x, y, text.Value());
    return {};
}
Core::Expected<void, std::string> Title(Hud::GameHudContext& context, std::size_t index)
{
    auto text = Text(context, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Hud::WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    context.hud.textColors.text = LightGray;
    context.hud.textColors.background = 0;
    Hud::WindowDrawing::DrawTitle(context, 485, 12, text.Value());
    return {};
}
bool Inside(const Hud::GameHudContext& context, float left, float top, float width, float height, bool inclusive)
{
    const float x = static_cast<float>(context.mouse.x);
    const float y = static_cast<float>(context.mouse.y);
    return left <= x && (inclusive ? x <= left + width : x < left + width) && top <= y &&
           (inclusive ? y <= top + height : y < top + height);
}
Core::Expected<void, std::string> CloseHelp(Hud::GameHudContext& context)
{
    Quad(context.sprites, 0x118, 470.0f, 395.0f, 24.0f, 24.0f, 0.0f, 0.0f, 0.75f, 0.75f);
    if (!Inside(context, 470.0f, 395.0f, 24.0f, 24.0f, false))
        return {};
    auto text = Text(context, 1002);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Hud::WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors.text = 0xffffffffu;
    context.hud.textColors.background = 0xff000000u;
    Ui::DrawHelp(context.sprites, context.text, 470, 382, text.Value());
    context.hud.textColors.background = 0;
    return {};
}
void DrawBack(Hud::GameHudContext& context)
{
    context.sprites.SetColor(White);
    Hud::WindowDrawing::DrawTheWindowBack(context.sprites, 450, 0);
}
void DrawSeparators(Hud::GameHudContext& context)
{
    Quad(context.sprites, 0x117, 450.0f, 110.0f, 190.0f, 10.0f, 0.0f, 0.0f, 0.7421875f, 0.625f);
    Quad(context.sprites, 0x117, 450.0f, 290.0f, 190.0f, 10.0f, 0.0f, 0.0f, 0.7421875f, 0.625f);
}
void DrawWeaponChoice(Hud::GameHudContext& context, std::int32_t index, float x, float y, float width, float height)
{
    Render::SpriteBatch& sprites = context.sprites;
    const bool selected = index == context.world.Runtime().castleSiege.siegeBattleUi.weaponChoice - 1;
    sprites.SetColor(ButtonHighlight);
    sprites.ColorQuad(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f);
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (!Inside(context, x, y, width, height, true) && !selected)
        sprites.SetColor(White);
    Quad(sprites, selected ? 0x116 : 0x115, x, y, width, height);
}
Core::Expected<void, std::string> DrawWeapon(Hud::GameHudContext& context)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::uint8_t mode = runtime.panels.delegated.siegeWeapon;
    if (mode == 0)
        return {};
    DrawBack(context);
    DrawSeparators(context);
    if (mode == 1 || mode == 2)
    {
        if (auto title = Title(context, mode == 1 ? 1400 : 1401); !title.HasValue())
            return title;
        const std::int32_t count = mode == 1 ? 3 : 2;
        const std::int32_t stride = mode == 1 ? 58 : 95;
        for (std::int32_t index = 0; index < count; ++index)
        {
            DrawWeaponChoice(context, index, static_cast<float>(470 + index * stride), 140.0f,
                             mode == 1 ? 32.0f : 54.0f, mode == 1 ? 32.0f : 42.0f);
            if (auto label = Center(context, (mode == 1 ? 485 : 496) + index * stride, mode == 1 ? 152 : 156,
                                    static_cast<std::size_t>((mode == 1 ? 1402 : 1406) + index));
                !label.HasValue())
                return label;
        }
        DrawWeaponChoice(context, count, 500.0f, 200.0f, 90.0f, 64.0f);
        if (auto label = Center(context, 545, 226, static_cast<std::size_t>((mode == 1 ? 1402 : 1406) + count));
            !label.HasValue())
            return label;
    }
    context.sprites.SetColor(White);
    for (std::int32_t line = 0; line < 3; ++line)
    {
        if (auto label = Center(context, 545, 60 + line * 15, static_cast<std::size_t>(1409 + line)); !label.HasValue())
            return label;
    }
    Hud::WindowDrawing::SelectFont(context, Render::Text::FontKind::BigBold);
    if (runtime.castleSiege.siegeBattleUi.weaponChoice == 0)
        context.sprites.SetColor({0.5f, 0.5f, 0.5f, 1.0f});
    else
    {
        context.sprites.SetColor(White);
        context.hud.textColors.text = 0xffffffffu;
    }
    Quad(context.sprites, 0xf0, 470.0f, 325.0f, 150.0f, 48.0f, 0.0f, 0.0f, 0.83203125f, 1.0f);
    if (auto action = Center(context, 545, 340, 1499); !action.HasValue())
        return action;
    context.sprites.SetColor(White);
    return CloseHelp(context);
}
Core::Expected<void, std::string> DrawGate(Hud::GameHudContext& context)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.panels.delegated.siegeGate == 0)
        return {};
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    DrawBack(context);
    if (auto title = Title(context, 1475); !title.HasValue())
        return title;
    DrawSeparators(context);
    if (auto label = Center(context, 545, 50, 1476); !label.HasValue())
        return label;
    if (auto label = Center(context, 545, 67, 1477); !label.HasValue())
        return label;
    context.hud.textColors.background = 0xff0000a0u;
    if (auto label = Center(context, 545, 84, 1478); !label.HasValue())
        return label;
    context.hud.textColors.background = 0;
    const bool opened = runtime.castleSiege.siegeBattleUi.gateState == OpenGateFlag;
    Quad(context.sprites, opened ? 0x666 : 0x665, 467.0f, 120.0f, 155.0f, 168.0f, 0.0f, 0.0f, 155.0f / 256.0f,
         168.0f / 256.0f);
    Quad(context.sprites, 0xf0, 470.0f, 325.0f, 150.0f, 48.0f, 0.0f, 0.0f, 0.83203125f, 1.0f);
    context.hud.textColors.text = LightGray;
    if (Inside(context, 470.0f, 325.0f, 213.0f, 64.0f, false))
    {
        context.sprites.SetColor({0.8f, 0.6f, 0.4f, 1.0f});
        context.sprites.SetBlend(Render::BlendMode::Additive);
        Quad(context.sprites, 0xf0, 470.0f, 325.0f, 150.0f, 48.0f, 0.0f, 0.0f, 0.83203125f, 1.0f);
        context.sprites.SetColor(White);
        context.sprites.SetBlend(Render::BlendMode::Opaque);
        context.sprites.SetBlend(Render::BlendMode::Alpha);
        context.hud.textColors.text = 0xffffffffu;
    }
    Hud::WindowDrawing::SelectFont(context, Render::Text::FontKind::BigBold);
    if (auto label = Center(context, 545, 335, opened ? 1002 : 1479); !label.HasValue())
        return label;
    context.sprites.SetColor(White);
    return CloseHelp(context);
}
bool HasRole(const Kernel::ActorState& hero)
{
    return hero.byte360 == 6 || hero.byte360 == 7 || hero.byte360 == 8 || hero.byte360 == 12;
}
} // namespace

Core::Expected<void, std::string> DrawTheSiegeControls(Hud::GameHudContext& context)
{
    if (auto weapon = DrawWeapon(context); !weapon.HasValue())
        return weapon;
    return DrawGate(context);
}

void DrawTheBattleClock(Hud::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.castleSiege.siegeActive || runtime.map != 30)
        return;
    auto copy = runtime.castleSiege.siegeBattleUi.clock;
    auto& clock = context.projecting ? copy : runtime.castleSiege.siegeBattleUi.clock;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(White);
    sprites.SetBlend(Render::BlendMode::Alpha);
    Quad(sprites, 0x6a3, static_cast<float>(clock.x - 5), static_cast<float>(clock.y + 5), 154.0f, 30.0f, 0.0f, 0.0f,
         154.0f / 256.0f, 0.71875f);
    if (1000.0 < static_cast<double>(context.worldClock) - static_cast<double>(clock.blinkTime))
    {
        clock.blinkTime = context.worldClock;
        clock.blink ^= 1;
    }
    const Render::Color Orange{1.0f, 0.6f, 0.3f, 1.0f};
    if (clock.blink != 0)
    {
        sprites.SetColor(Orange);
        Quad(sprites, 0x669, static_cast<float>(clock.x + 71), static_cast<float>(clock.y + 16), 3.0f, 9.0f, 0.0f, 0.0f,
             0.75f, 0.5625f);
    }
    std::int32_t hours = std::bit_cast<std::int32_t>(clock.hours);
    std::int32_t minutes = std::bit_cast<std::int32_t>(clock.minutes - ((context.tickCount - clock.tick) / 60000u));
    if (minutes < 0)
    {
        minutes += 60;
        hours = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(hours) - 1u);
    }
    sprites.SetColor({0.0f, 0.0f, 0.0f, 1.0f});
    Ui::DrawNumber(sprites, static_cast<float>(clock.x + 63), static_cast<float>(clock.y + 15), hours, 14.0f, 14.0f);
    Ui::DrawNumber(sprites, static_cast<float>(clock.x + 90), static_cast<float>(clock.y + 15), minutes, 14.0f, 14.0f);
    sprites.SetColor(Orange);
    Ui::DrawNumber(sprites, static_cast<float>(clock.x + 62), static_cast<float>(clock.y + 14), hours, 14.0f, 14.0f);
    Ui::DrawNumber(sprites, static_cast<float>(clock.x + 89), static_cast<float>(clock.y + 14), minutes, 14.0f, 14.0f);
}

Core::Expected<void, std::string> DrawTheBattleSkill(Hud::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::ActorState& hero = context.world.Hero();
    if (!runtime.castleSiege.siegeActive || !HasRole(hero) || static_cast<std::int16_t>(hero.word1f2) < 0 ||
        hero.guildStatus == 0)
        return {};
    std::uint8_t cost = 0;
    {
        const Models::LogicalAccess access(runtime.movement.skillDepths[hero.byte371], context.random, 60);
        if (context.skills == nullptr)
            return Core::Unexpected(std::string("0x652f83: sin Skill.bmd para la barra del asedio"));
        const auto record = context.skills->Record(hero.byte371);
        if (!record)
            return Core::Unexpected(std::string("0x652f83: no está la entrada de la skill del asedio"));
        cost = std::to_integer<std::uint8_t>((*record)[0x33]);
    }
    Render::SpriteBatch& sprites = context.sprites;
    const bool enough = hero.byte372 >= cost;
    const Render::Color color{1.0f, enough ? 1.0f : 0.0f, enough ? 1.0f : 0.0f, 1.0f};
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(color);
    Quad(sprites, 0x65f, 0.0f, 0.0f, 128.0f, 32.0f);
    const std::int32_t icon = static_cast<std::int32_t>(hero.byte371) - 0x39;
    Quad(sprites, 299, 25.0f, 4.0f, 18.0f, 24.0f, static_cast<float>((icon % 8) * 32 + 7) / 256.0f,
         static_cast<float>((icon / 8) * 39 + 3) / 256.0f, 0.0703125f, 0.08984375f);
    Ui::DrawNumber(sprites, 65.0f, 10.0f, hero.byte372, 9.0f, 10.0f);
    Ui::DrawNumber(sprites, 85.0f, 10.0f, cost, 9.0f, 10.0f);
    const std::uint8_t pressed = runtime.castleSiege.siegeBattleUi.skillPressed;
    if (pressed <= 2)
    {
        if (pressed == 1)
            sprites.SetColor({0.5f, 0.5f, 0.5f, 1.0f});
        else if (pressed == 0)
            sprites.SetColor(color);
        Quad(sprites, 0x649, 43.0f, 4.0f, 13.0f, 11.0f, 0.0f, 0.125f, 0.8125f, 0.34375f);
        sprites.SetColor(pressed == 2 ? Render::Color{0.5f, 0.5f, 0.5f, 1.0f} : color);
        Quad(sprites, 0x649, 43.0f, 17.0f, 13.0f, 12.0f, 0.0f, 0.5f, 0.8125f, 0.375f);
        if (pressed == 2)
            sprites.SetColor(color);
        if (!context.projecting && pressed != 0)
            runtime.castleSiege.siegeBattleUi.skillPressed = 0;
    }
    if (Inside(context, 25.0f, 4.0f, 18.0f, 24.0f, false))
    {
        std::int32_t index = 0;
        {
            const Models::LogicalAccess access(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
            for (std::size_t slot = 0; slot < 64; ++slot)
                if (context.world.OwnCharacter().SkillAt(slot) == hero.byte371)
                {
                    index = static_cast<std::int32_t>(slot);
                    break;
                }
        }
        if (auto tooltip = Tooltips::DrawTheSkillTooltip(context, 55, 69, index, hero.byte371); !tooltip.HasValue())
            return tooltip;
    }
    if (Inside(context, 0.0f, 10.0f, 128.0f, 32.0f, false))
    {
        auto text = Text(context, 1474);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::DrawHelp(sprites, context.text, 3, 77, text.Value());
        context.hud.textColors.text = 0xffffffffu;
        context.hud.textColors.background = 0;
        context.hud.dcFont = Render::Text::FontKind::Normal;
    }
    return {};
}
} // namespace Client::CastleSiege
