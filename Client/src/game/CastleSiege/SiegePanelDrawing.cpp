#include "CastleSiege/SiegeBattleUi.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"
#include "Math/RoundedMath.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Hud/GamePanels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Ui/HudPrimitives.h"
#include "Ui/PanelRegistry.h"
#include "CastleSiege/SiegePanelState.h"
#include "Math/Ftol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::CastleSiege
{
namespace
{
constexpr std::int32_t MapSize = 128;
constexpr std::int32_t MapInsetX = 23;
constexpr std::int32_t MapInsetY = 45;
constexpr std::uint16_t MapSlot = 0x69d;
constexpr std::uint16_t FrameSlot = 0x69f;
constexpr std::uint16_t FirstCommandSlot = 0x6a0;

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
std::int32_t ReadCell(Kernel::ActorState& actor, Crt::CrtRandom& random, bool x, std::uint8_t& depth)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return x ? actor.cellX : actor.cellY;
}
std::uint32_t SideOf(std::uint32_t flags, std::int32_t offsetY)
{
    for (const std::uint32_t side : {0x80000u, 0x400000u, 0x800000u, 0x40000u})
        if ((flags & side) == side)
            return side;
    // 0x40fcc3: sin esos cuatro bits, usa el DWORD de offsetY que dejó en [ebp-8] 0x40fc4b.
    return static_cast<std::uint32_t>(offsetY);
}

bool InMap(float x, float y, float left, float top)
{
    return left <= x && x <= left + MapSize && top <= y && y <= top + MapSize;
}

// 0x40f850: los accesos de cada actor ocurren aunque su punto termine fuera del rectángulo.
Core::Expected<void, std::string> DrawBase(Hud::GameHudContext& context, CastleSiege::SiegePanelState& panel,
                                           std::uint8_t kind)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::ActorState& hero = context.world.Hero();
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor({1.0f, 1.0f, 1.0f, panel.alpha});
    const std::int32_t heroX = ReadCell(hero, context.random, true, context.cellXDepth) / panel.page;
    const std::int32_t heroY = (256 - ReadCell(hero, context.random, false, context.cellYDepth)) / panel.page;
    panel.offsetX = std::max(heroX - panel.page * 64, 0);
    panel.offsetY = std::clamp(heroY - panel.page * 64, 0, 128);
    const float left = static_cast<float>(panel.x + MapInsetX);
    const float top = static_cast<float>(panel.y + MapInsetY);
    const float scale = 256.0f / static_cast<float>(panel.page);
    Quad(sprites, MapSlot, left, top, 128.0f, 128.0f, static_cast<float>(panel.offsetX) / scale,
         static_cast<float>(panel.offsetY) / scale, static_cast<float>(panel.page) * 0.5f,
         static_cast<float>(panel.page) * 0.5f);
    sprites.SetBlend(Render::BlendMode::Alpha);
    Quad(sprites, FrameSlot, static_cast<float>(panel.x), static_cast<float>(panel.y), 151.0f, 173.0f, 0.0f, 0.0f,
         151.0f / 256.0f, 173.0f / 256.0f);
    sprites.SetBlend(Render::BlendMode::Opaque);
    const std::uint32_t side = SideOf(hero.flags78, panel.offsetY);
    for (std::size_t index = 0; index + 1 < Kernel::ActorPoolSize; ++index)
    {
        Kernel::ActorState& actor = context.world.Actors()[index];
        if (!actor.alive || index == runtime.hero || (actor.byte88 != 1 && actor.byte88 != 2 && actor.byte88 != 4))
            continue;
        const bool friendly = (actor.flags78 & side) == side;
        const bool npc = actor.byte88 == 4 || (actor.byte88 == 2 && actor.type == 0x1a8);
        if (kind == 0)
            sprites.SetColor({0.8f, 0.8f, 0.8f, 1.0f});
        else
        {
            if (kind == 2 && friendly)
                continue;
            sprites.SetColor(npc        ? Render::Color{1.0f, 0.0f, 1.0f, 1.0f}
                             : friendly ? Render::Color{0.0f, 1.0f, 0.0f, 1.0f}
                                        : Render::Color{0.8f, 0.0f, 0.0f, 1.0f});
        }
        std::uint8_t xDepth = context.projecting ? actor.cellXAccesses : 0;
        std::uint8_t yDepth = context.projecting ? actor.cellYAccesses : 0;
        const auto x = ReadCell(actor, context.random, true, context.projecting ? xDepth : actor.cellXAccesses);
        const auto y = ReadCell(actor, context.random, false, context.projecting ? yDepth : actor.cellYAccesses);
        sprites.ColorQuad(left + static_cast<float>(x / panel.page - panel.offsetX),
                          top + static_cast<float>((256 - y) / panel.page - panel.offsetY), 3.0f, 3.0f);
    }
    sprites.SetColor({1.0f, 1.0f, 0.0f, 1.0f});
    sprites.ColorQuad(left + static_cast<float>(heroX - panel.offsetX), top + static_cast<float>(heroY - panel.offsetY),
                      3.0f, 3.0f);
    sprites.SetColor({1.0f, 1.0f, 1.0f, panel.alpha});
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (panel.page == 1)
        Quad(sprites, 0x6a7, static_cast<float>(panel.x + 127), static_cast<float>(panel.y + 13), 24.0f, 22.0f, 0.0f,
             0.0f, 0.75f, 0.6875f);
    // 410261..410274: _ftol devuelve el dword bajo de un int64, también el0 deNaN (Q-C186).
    constexpr double PercentScale = 100.0;
    const auto percentage = Math::OriginalFtolLow(static_cast<double>(panel.alpha) * PercentScale);
    if (!percentage)
        return Core::Unexpected(std::string("0x410274: el porcentaje del minimapa excede el contrato medido de _ftol"));
    Ui::DrawNumber(sprites, static_cast<float>(panel.x + 25), static_cast<float>(panel.y + 32), *percentage, 8.0f,
                   8.0f);
    return {};
}

Core::Expected<void, std::string> DrawStatus(Hud::GameHudContext& context, CastleSiege::SiegePanelState& panel,
                                             float* lastWidth = nullptr, float* lastHeight = nullptr)
{
    Render::SpriteBatch& sprites = context.sprites;
    const float left = static_cast<float>(panel.x + MapInsetX);
    const float top = static_cast<float>(panel.y + MapInsetY);
    // Son locales de una llamada, no de cada fila. Los saltos sin caso conservan sus valores anteriores.
    struct Metrics
    {
        float width;
        float height;
        float atlasWidth;
    };
    std::optional<Metrics> metrics;
    for (auto& status : panel.status)
    {
        if (status[3] == 3 || status[0] == 0 || status[0] >= 8)
            continue;
        const float x = left + static_cast<float>(status[1] / panel.page - panel.offsetX);
        const float y = top + static_cast<float>((256 - status[2]) / panel.page - panel.offsetY);
        if (!InMap(x, y, left, top))
            continue;
        if (status[3] == 0)
            metrics = Metrics{13.0f, 13.0f, 16.0f};
        else if (status[3] == 1)
            metrics = Metrics{18.0f, 15.0f, 32.0f};
        else if (status[3] == 2)
            metrics = Metrics{11.0f, 12.0f, 16.0f};
        if (metrics && lastWidth != nullptr)
            *lastWidth = metrics->width;
        if (metrics && lastHeight != nullptr)
            *lastHeight = metrics->height;
        float flash = 1.0f;
        if (status[4] != 0)
        {
            flash =
                static_cast<float>(Math::RoundedSin(static_cast<double>(status[4]) * static_cast<double>(0.2f)) + 1.0);
            --status[4];
        }
        sprites.SetColor({1.0f, flash, flash, 1.0f});
        Ui::DrawNumber(sprites, x - 10.0f, y - 6.0f, status[0], 8.0f, 8.0f);
        if (!metrics)
            return Core::Unexpected(std::string("0x410330/0x411600: la primera marca sin caso usa métricas del stack "
                                                "sin inicializar; falta el estado original para reconstruirlas"));
        Quad(sprites, static_cast<std::uint16_t>(FirstCommandSlot + status[3]), x - 8.0f, y - 8.0f, 11.0f, 11.0f, 0.0f,
             0.0f, (metrics->width - 1.0f) / metrics->atlasWidth, (metrics->height - 1.0f) / 16.0f);
    }
    return {};
}

bool Inside(float x, float y, float left, float top, float width, float height)
{
    return left <= x && x <= left + width && top <= y && y <= top + height;
}

void DrawMemberPoints(Hud::GameHudContext& context, const CastleSiege::SiegePanelState& panel)
{
    if (panel.membersHidden != 0)
        return;
    const float left = static_cast<float>(panel.x + MapInsetX);
    const float top = static_cast<float>(panel.y + MapInsetY);
    for (const auto& member : panel.members)
    {
        if (member[0] == 0)
            context.sprites.SetColor({0.0f, 1.0f, 0.0f, 1.0f});
        else if (member[0] == 1)
            context.sprites.SetColor({0.0f, 1.0f, 1.0f, 1.0f});
        else if (member[0] == 2)
            context.sprites.SetColor({1.0f, 1.0f, 0.0f, 1.0f});
        const float x = left + static_cast<float>(member[1] / panel.page - panel.offsetX);
        const float y = top + static_cast<float>((256 - member[2]) / panel.page - panel.offsetY);
        if (InMap(x, y, left, top))
            context.sprites.ColorQuad(x, y, 3.0f, 3.0f);
    }
}

Core::Expected<void, std::string> DrawCommands(Hud::GameHudContext& context, CastleSiege::SiegePanelState& panel)
{
    Render::SpriteBatch& sprites = context.sprites;
    float iconWidth = 19.0f;
    float iconHeight = 18.0f;
    const float left = static_cast<float>(panel.x);
    const float top = static_cast<float>(panel.y);
    const float menuX = left + 22.0f;
    const float mapTop = top + MapInsetY;
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor({1.0f, 1.0f, 1.0f, panel.alpha});
    const auto mouse = context.mouse;
    if (left <= static_cast<float>(mouse.x) && static_cast<float>(mouse.x) <= left + 151.0f &&
        mapTop <= static_cast<float>(mouse.y) && static_cast<float>(mouse.y) <= mapTop + MapSize &&
        panel.selected != 0 && panel.command != 3)
    {
        float atlasWidth = 16.0f;
        if (panel.command == 0)
            iconWidth = iconHeight = 13.0f;
        else if (panel.command == 1)
        {
            iconWidth = 18.0f;
            iconHeight = 15.0f;
            atlasWidth = 32.0f;
        }
        else if (panel.command == 2)
        {
            iconWidth = 11.0f;
            iconHeight = 12.0f;
        }
        Ui::DrawNumber(sprites, static_cast<float>(mouse.x - 10), static_cast<float>(mouse.y - 6), panel.selected, 8.0f,
                       10.0f);
        Quad(sprites, static_cast<std::uint16_t>(FirstCommandSlot + panel.command), static_cast<float>(mouse.x - 8),
             static_cast<float>(mouse.y - 8), iconWidth, iconHeight, 0.0f, 0.0f, (iconWidth - 1.0f) / atlasWidth,
             (iconHeight - 1.0f) / 16.0f);
    }
    if (auto status = DrawStatus(context, panel, &iconWidth, &iconHeight); !status.HasValue())
        return status;
    float rowY = top + 54.0f;
    for (std::uint8_t row = 1; row <= 7; ++row)
    {
        if (panel.selected == row)
        {
            sprites.SetColor({1.0f, 1.0f, 1.0f, panel.alpha});
            float menuY = rowY - 5.0f;
            Quad(sprites, 0x6a6, left + 3.0f, menuY, iconWidth, iconHeight, 0.0f, 0.0f, iconWidth / 32.0f,
                 iconHeight / 32.0f);
            if (panel.command == 3)
            {
                iconWidth = 30.0f;
                iconHeight = 18.0f;
                if (row == 6)
                    menuY -= 18.0f;
                else if (row == 7)
                    menuY -= 36.0f;
                constexpr std::array<float, 3> Widths{13.0f, 18.0f, 11.0f};
                constexpr std::array<float, 3> Heights{13.0f, 15.0f, 12.0f};
                constexpr std::array<float, 3> Offsets{8.0f, 7.0f, 10.0f};
                for (std::uint16_t command = 0; command < 3; ++command)
                {
                    const float y = menuY + command * 18.0f;
                    const bool hover =
                        Inside(static_cast<float>(mouse.x), static_cast<float>(mouse.y), menuX, y, 30.0f, 18.0f);
                    // 411c20/411cf3/411dc6: los tres fondos miden30x18, igual que las filas del paso410773.
                    Quad(sprites, hover ? 0x6a5 : 0x6a4, menuX, y, 30.0f, 18.0f, 0.0f, 0.0f, 0.9375f, 0.5625f);
                    Quad(sprites, static_cast<std::uint16_t>(FirstCommandSlot + command), menuX + Offsets[command],
                         y + (command == 2 ? 3.0f : 2.0f), Widths[command], Heights[command], 0.0f, 0.0f,
                         Widths[command] / (command == 1 ? 32.0f : 16.0f), Heights[command] / 16.0f);
                }
            }
        }
        else
            sprites.SetColor({1.0f, 0.6f, 0.3f, panel.alpha});
        Ui::DrawNumber(sprites, left + 12.0f, rowY, row, 8.0f, 8.0f);
        rowY += 17.0f;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheBattlePanel(Hud::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Hud::GamePanels panels(context.world, context.random);
    if (panels.Registry().IsOpen(Ui::AnyPanel) || !runtime.castleSiege.siegePanelKind ||
        runtime.castleSiege.siegePanel.visible == 0)
        return {};
    // Los cambios que hace el original al dibujar duran hasta el frame siguiente; un frame proyectado usa su copia.
    CastleSiege::SiegePanelState copy;
    CastleSiege::SiegePanelState& panel =
        context.projecting ? (copy = runtime.castleSiege.siegePanel) : runtime.castleSiege.siegePanel;
    if (panel.page <= 0)
        return Core::Unexpected(std::string("0x40fba8: la escala del minimapa dividiría por cero o sería negativa"));
    const std::uint8_t kind = *runtime.castleSiege.siegePanelKind;
    if (auto base = DrawBase(context, panel, kind); !base.HasValue())
        return base;
    if (kind == 1)
        return DrawStatus(context, panel);
    else if (kind == 2)
    {
        DrawMemberPoints(context, panel);
        return DrawCommands(context, panel);
    }
    return {};
}
} // namespace Client::CastleSiege
