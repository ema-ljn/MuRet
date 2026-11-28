#include "Hud/GameHud.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Math/RoundedMath.h"
#include "Render/ScreenScale.h"
#include "Hud/MapBanner.h"
#include "CastleSiege/SiegeBattleUi.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Models/LogicalAccess.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbleDraw.h"
#include "CastleSiege/CastleWindows.h"
#include "Events/Crywolf/CrywolfInterfaceDrawing.h"
#include "Events/EventMapObject.h"
#include "Events/EventChipWindow.h"
#include "Events/EventDrawing.h"
#include "Events/EventNpcWindows.h"
#include "Events/EventPackets.h"
#include "Hud/GameCharacterHud.h"
#include "Hud/GameCommandsWindow.h"
#include "Tooltips/GameHelps.h"
#include "ChaosMix/ChaosBoxDrawing.h"
#include "Hud/GameInventoryHud.h"
#include "Hud/GameMoveWindow.h"
#include "Hud/GamePanels.h"
#include "Combat/Attackable.h"
#include "Party/GamePartyWindow.h"
#include "Pets/GamePetWindow.h"
#include "Social/GameSocialDrawing.h"
#include "Tooltips/GameTooltips.h"
#include "Hud/HealthBars.h"
#include "Items/HudItems.h"
#include "Npc/NpcWindowDrawing.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "PetTrainer/PetTrainerDrawing.h"
#include "Guild/GuildWindow.h"
#include "Guild/SecondGuildWindow.h"
#include "Quests/QuestWindow.h"
#include "PersonalShop/ShopSigns.h"
#include "Social/SocialState.h"
#include "Tooltips/TooltipLines.h"
#include "Items/UiItemDrawing.h"
#include "Warehouse/WarehouseKeypad.h"
#include "Guild/GuildDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "CastleSiege/CastleBattleState.h"
#include "Chat/ChatInput.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatLogDraw.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"
#include "Messenger/LetterWindowsDraw.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Ui/SlideBanner.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"
#include "Movement/GameMovementState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Hud
{
namespace
{
using Render::BlendMode;
using Render::Color;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// The unusable skill's tint (0x5a5860), and the color of its bar (0x5a9f4e) and its numbers (0x5a61ea).
constexpr Color SkillTint{1.0f, 0.5f, 0.5f, 1.0f};
constexpr Color SkillColor{1.0f, 0.9f, 0.8f, 1.0f};
// The AG and the numbers of life, mana and the slots (0x5a8d2a, 0x5aa780).
constexpr Color NumberColor{1.0f, 0.95f, 0.75f, 1.0f};

// Text.bmd texts by index: (the address − 0x76bd1dc) / 300.
using Maps::CastleSiegeMap;
using Maps::CrywolfMap;
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
using Maps::TarkanMap;
constexpr std::size_t ExperienceHelpText = 201;
constexpr std::size_t AgHelpText = 214;
constexpr std::size_t ArrowsText = 351;
constexpr std::size_t BoltsText = 352;
constexpr std::size_t DinorantText = 354;
constexpr std::size_t UniriaText = 355;
constexpr std::size_t SummonText = 356;
constexpr std::size_t LifeHelpText = 358;
constexpr std::size_t ManaHelpText = 359;
constexpr std::size_t PartyHelpText = 361;
constexpr std::size_t CharacterHelpText = 362;
constexpr std::size_t InventoryHelpText = 363;
constexpr std::size_t GuildHelpText = 364;
constexpr std::size_t CommandsHelpText = 939;
constexpr std::size_t DarkHorseText = 1187;
constexpr std::size_t FenrirText = 1916;

// The pets' models: the Item.bmd type + 0x212.
constexpr std::int16_t UniriaModel = 0x1c14;
constexpr std::int16_t DinorantModel = 0x1c15;
constexpr std::int16_t DarkHorseModel = 0x1c16;
constexpr std::int16_t FenrirModel = 0x1c37;

// 0x5ab810
constexpr std::uint16_t ArrowsType = 0x80f;
constexpr std::uint16_t BoltsType = 0x807;
constexpr std::size_t HelperSlot = 8;

// 0x6b8940: the icons' texel.
constexpr double Texel = 0.00390625;

// 0x5a736b..0x5a7452: the macro brake, its text (0x6c9310) at y = 0x188 and its bar at y = 404, 50 × 2.
constexpr std::string_view MacroTimeText = "Macro Time";
constexpr int MacroTimeTextTop = 0x188;
constexpr float MacroTimeBarTop = 404.0f;
constexpr float MacroTimeBarWidth = 50.0f;
constexpr float MacroTimeBarHeight = 2.0f;
// 0x5aa9a0: the chat input's box (slots 0xf8 and 0xf9, dialogue1 and dialogue2), the F3 indicator (0xef,
// dialogue2_1) and the whisper names of the other places, every 15 from (0x178, 0x1a6).
constexpr std::uint16_t ChatBoxLeftSlot = 0xf8;
constexpr std::uint16_t ChatBoxRightSlot = 0xf9;
constexpr std::uint16_t WhisperModeSlot = 0xef;
constexpr int WhisperNamesLeft = 0x178;
constexpr int WhisperNamesTop = 0x1a6;
constexpr int WhisperNamesStep = 0xf;
// 0x5aaa77 and 0x5aaa7f: the colors it leaves for the names.
constexpr Render::Text::TextColors WhisperNamesColors{.text = 0xffc8c8c8u, .background = 0x64000000u};
// 0x4691e0: the EDITs' text color (the vtable + 0x40) and the whisper one's background (+ 0x44), with the bytes in
// TextColors' order. The two EDITs' corner (0x45a9d0 in 0x4691e0).

using Math::F32;
using Math::Truncate;

struct Frame
{
    Ui::GameHudContext& context;
    Kernel::WorldRuntime& runtime;
    Kernel::OwnCharacterRecord& own;
    Kernel::ActorState& hero;
    Ui::PanelRegistry& panels;
};

// An access to the own record ([0x7681eb4]): opens without spending and, if it leaves it at 0, closes with 3848
// rand().
Models::LogicalAccess OwnAccess(const Frame& frame)
{
    return Models::LogicalAccess(frame.context.ownRecordDepth, frame.context.random, Models::OwnRecordCloseRandoms);
}

// An access to Skill.bmd entry `skill` (0x76bd1d8 + 0x3c · skill): 60 rand() when closing it at 0 (0x40f600).
Models::LogicalAccess SkillAccess(const Frame& frame, std::uint32_t skill)
{
    return Models::LogicalAccess(frame.runtime.skillDepths[skill], frame.context.random,
                                 static_cast<std::uint16_t>(Tables::SkillRecordSize));
}

Core::Expected<std::string_view, std::string> TextOf(const Frame& frame, std::size_t index, std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(frame.context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<std::span<const std::byte, Tables::SkillRecordSize>, std::string>
SkillRecordOf(const Frame& frame, std::uint32_t skill, std::string_view where)
{
    if (frame.context.skills == nullptr)
        return Core::Unexpected(std::format("{}: sin Skill.bmd", where));
    const auto record = frame.context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    return *record;
}

Core::Expected<std::span<const std::byte, Tables::ItemRecordSize>, std::string>
ItemRecordOf(const Frame& frame, std::int32_t type, std::string_view where)
{
    if (frame.context.items == nullptr)
        return Core::Unexpected(std::format("{}: sin Item.bmd", where));
    const auto record = type < 0 ? std::nullopt : frame.context.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("{}: Item.bmd no tiene el tipo {}", where, type));
    return *record;
}

std::uint8_t ByteAt(std::span<const std::byte, Tables::SkillRecordSize> record, std::size_t at)
{
    return std::to_integer<std::uint8_t>(record[at]);
}

std::uint32_t LittleEndianAt(std::span<const std::byte, Tables::SkillRecordSize> record, std::size_t at,
                             std::size_t bytes)
{
    std::uint32_t value = 0;
    for (std::size_t i = bytes; i-- > 0;)
        value = (value << 8u) | ByteAt(record, at + i);
    return value;
}

// 0x465950 with the normal font (0x5763554) and the colors of 0x6c8e5c and 0x6c8e64: mode 1, which starts at x, or
// 4, which ends at x (0x467060, Render::Text::EndingAt).
void DrawHudText(Frame& frame, int x, int y, std::string_view text, bool endingAtX)
{
    const Render::Text::TextStyle style{.font = Render::Text::FontKind::Normal, .colors = frame.context.hud.textColors};
    int at = x;
    if (endingAtX)
        at = Render::Text::EndingAt(x, frame.context.text.Measure(Render::Text::FontKind::Normal, text),
                                    frame.context.sprites.Screen());
    frame.context.text.Draw(at, y, text, style);
}

// 0x554340 leaves the text white on a transparent background (0x6c8e5c at −1 and 0x6c8e64 at 0).
void DrawHelp(Frame& frame, int x, int y, std::string_view help)
{
    Ui::DrawHelp(frame.context.sprites, frame.context.text, x, y, help);
    frame.context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
}

bool MouseIn(const Frame& frame, int left, int top, int right, int bottom)
{
    const Ui::Point mouse = frame.context.mouse;
    return left <= mouse.x && mouse.x < right && top <= mouse.y && mouse.y < bottom;
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): scaled in size and position, without alpha.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
          float v, float du, float dv)
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
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

// 0x65dd70: 0x65dcd0 runs the fade with timeGetTime() and, if the banner has not gone out, 0x620a00(1), glColor4f(1,
// 1, 1, alpha), 0x621b60(0x66a, x, y, 166, 90, 0, 0, 0.6484375, 0.703125, 0, 0, 0) without scaling size or position
// (MapBannerQuadOf), glColor4f(1, 1, 1, 1) and 0x620980.
void DrawTheMapBanner(Frame& frame)
{
    Ui::GameHudContext& context = frame.context;
    MapBanner& banner = context.hud.mapBanner;
    StepTheMapBanner(banner, context.tickCount);
    if (banner.state == MapBannerState::Off)
        return;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(Color{1.0f, 1.0f, 1.0f, banner.alpha});
    const Core::PixelSize screen = sprites.Screen();
    const MapBannerQuad at =
        MapBannerQuadOf(frame.panels.WorldWidth(), Render::ScaleX(1.0f, screen), Render::ScaleY(1.0f, screen));
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{MapBannerSlot},
                                      .x = at.x,
                                      .y = at.y,
                                      .width = MapBannerWidth,
                                      .height = MapBannerHeight,
                                      .u = 0.0f,
                                      .v = 0.0f,
                                      .du = MapBannerDu,
                                      .dv = MapBannerDv,
                                      .scaleSize = false,
                                      .scalePosition = false,
                                      .alpha = 0.0f});
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
}

// The index of 0x5763338, the registry's resolution (0 to 4: 640, 800, 1024, 1280 and 1600 wide). MuRet does not
// read it from the registry: it takes the largest of the five that fits in the screen width (08, a CL8 decision).
int ResolutionIndex(Core::PixelSize screen)
{
    constexpr std::array<int, 4> Widths{800, 1024, 1280, 1600};
    int index = 0;
    for (const int width : Widths)
    {
        if (screen.width >= width)
            ++index;
    }
    return index;
}

// ---------------------------------------------------------------------------------------------------------------
// 1. The sands (0x5a7520).

// 0x622000(slot, 0, 0, 640, 435, ((int) T % 100000) · speed, 0, du, dv): fild of the remainder times the speed in
// float, stored in float.
void DrawSand(Frame& frame, std::uint16_t slot, float speed, float du, float dv)
{
    const std::int32_t remainder = Truncate(frame.context.worldClock) % 100000;
    frame.context.sprites.SkewedQuad(
        Render::OriginalSkewedQuad{.slot = Render::TextureSlot{slot},
                                   .x = 0.0f,
                                   .y = 0.0f,
                                   .width = 640.0f,
                                   .height = 435.0f,
                                   .u = F32(static_cast<double>(remainder) * static_cast<double>(speed)),
                                   .v = 0.0f,
                                   .du = du,
                                   .dv = dv});
}

constexpr std::uint16_t SlowSand = 0x5c2;
constexpr std::uint16_t FastSand = 0x5c3;

// 0x435930 and 0x42b100: the Crywolf and siege smoke, with its color: first the fast one with the normal blend and
// then the slow one, additive.
void DrawSmoke(Frame& frame, Color color)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(color);
    DrawSand(frame, FastSand, 0.0005f, 3.0f, 2.0f);
    sprites.SetBlend(BlendMode::Additive);
    DrawSand(frame, SlowSand, 0.0002f, 0.3f, 0.3f);
}

void DrawTheSands(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const std::int32_t map = frame.runtime.map;
    if (map == TarkanMap)
    {
        // 0x5a7539..0x5a7604: the white with alpha 0.5 of the normal blend draws nothing; the additive blend draws
        // both.
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(Color{1.0f, 1.0f, 1.0f, 0.5f});
        sprites.SetBlend(BlendMode::Additive);
        sprites.SetColor(Color{0.3f, 0.3f, 0.25f, 1.0f});
        DrawSand(frame, SlowSand, 0.0002f, 0.3f, 0.3f);
        DrawSand(frame, FastSand, 0.001f, 3.0f, 2.0f);
    }
    else if (map == CrywolfMap)
    {
        // 0x5a760e: only once per 0x576353c, which turns it off.
        if (frame.context.crywolfSandOverlay == 1)
        {
            DrawSmoke(frame, Color{0.4f, 0.4f, 0.45f, 1.0f});
            frame.context.crywolfSandOverlay = 0;
        }
    }
    else if (map == CastleSiegeMap && frame.runtime.castleFlags.siegeActive)
    {
        // 0x42b100, with 0x4294b0.
        DrawSmoke(frame, Color{0.3f, 0.3f, 0.25f, 1.0f});
    }
    sprites.SetColor(White);
}

// ---------------------------------------------------------------------------------------------------------------
// 6. The status icons (0x42a660 and 0x5a63d0).

// 0x5a63d0(icon, x, y, w, h): the icon in its 8-column atlas of 32 × 36 texels: those above 0xca in StatusIcon
// (0x68e), 0x78 to 0x7c in command (300), above 0x38 in Skill2 (299) and the rest in Skill (0x12a), from 1.
void DrawStatusIcon(Frame& frame, std::int32_t icon, int x, int y, int width, int height)
{
    std::uint16_t slot = 0x12a;
    std::int32_t index = icon - 1;
    if (icon > 0xca)
    {
        slot = 0x68e;
        index = icon - 0xcb;
    }
    else if (icon >= 0x78 && icon <= 0x7c)
    {
        slot = 300;
        index = icon - 0x78;
    }
    else if (icon > 0x38)
    {
        slot = 299;
        index = icon - 0x39;
    }
    Quad(frame.context.sprites, slot, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
         static_cast<float>(height), F32(static_cast<double>((index % 8) * 32) * Texel),
         F32(static_cast<double>((index / 8) * 36) * Texel), 0.125f, 0.13671875f);
}

// A Skill.bmd record's name as strcpy copies it (0x551e0c): up to its '\0', inside the record.
std::string SkillRecordName(std::span<const std::byte, Tables::SkillRecordSize> record)
{
    std::string name;
    for (const std::byte value : record)
    {
        if (value == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return name;
}

// 0x5a6600(x, y, icon), from 0x42a735 with the mouse over the icon: its help in the tooltip box (0x5af090, with mode
// 2 and the frame). With the own record open around it, the name of Skill.bmd entry `icon` (0x551c70, with the
// entry's access: 60 rand() when closing it at 0) goes in blue and bold on line 1, between two "\n", and the icon's
// text (the table at 0x5a69fc), in white on line 3; 0xcc has three, lines 3 to 5. It does not clear the lines: an
// icon without text leaves the line 3 that was there, and 0xcc, the colors and bolds of lines 3 and 4 (Q-C597).
Core::Expected<void, std::string> DrawTheStatusHelp(Frame& frame, std::int32_t x, std::int32_t y, std::int32_t icon)
{
    struct HelpText
    {
        std::int32_t icon = 0;
        std::size_t text = 0;
    };
    constexpr std::array<HelpText, 6> Texts{
        {{0x40, 1466}, {0x43, 1468}, {0x45, 1469}, {0x46, 1470}, {0x4b, 1467}, {0xcb, 1471}}};
    // 0x5a6855..0x5a6888: the three of 0xcc.
    constexpr std::int32_t ThreeLinesIcon = 0xcc;
    constexpr std::array<std::size_t, 3> ThreeLines{1638, 1639, 1472};
    Tooltips::TooltipLines& lines = frame.context.hud.tooltip;
    std::size_t last = 3;
    {
        const auto own = OwnAccess(frame);
        const auto skill = static_cast<std::uint32_t>(icon);
        auto record = SkillRecordOf(frame, skill, "0x551c70");
        if (!record.HasValue())
            return Core::Unexpected(record.Error());
        std::string name;
        {
            const auto entry = SkillAccess(frame, skill);
            name = SkillRecordName(record.Value());
        }
        lines.text[0] = "\n";
        lines.text[1] = Crt::Sprintf("%s", std::string_view(name));
        lines.colors[1] = Tooltips::TooltipBlue;
        lines.bold[1] = 1;
        lines.text[2] = "\n";
        const auto write = [&](std::size_t line, std::size_t index) -> Core::Expected<void, std::string>
        {
            const auto text = TextOf(frame, index, "0x5a6888");
            if (!text.HasValue())
                return Core::Unexpected(text.Error());
            lines.text[line] = Crt::Sprintf(text.Value());
            return {};
        };
        if (icon == ThreeLinesIcon)
        {
            for (std::size_t i = 0; i < ThreeLines.size(); ++i)
            {
                if (auto written = write(3 + i, ThreeLines[i]); !written.HasValue())
                    return written;
            }
            last = 5;
        }
        for (const HelpText& help : Texts)
        {
            if (help.icon != icon)
                continue;
            if (auto written = write(3, help.text); !written.HasValue())
                return written;
        }
        lines.colors[last] = Tooltips::TooltipWhite;
        lines.bold[last] = 0;
        lines.text[last + 1] = "\n";
    }
    return Tooltips::DrawTheTooltipBox(frame.context, x, y, static_cast<std::int32_t>(last + 2), 0, 2, 1);
}

Core::Expected<void, std::string> DrawTheStatusIcons(Frame& frame)
{
    struct StatusIcon
    {
        std::uint32_t bit = 0;
        std::int32_t icon = 0;
    };
    constexpr std::array<StatusIcon, 8> Icons{{{0x200, 0x4b},
                                               {0x400, 0x35},
                                               {0x800, 0x40},
                                               {0x1000, 0x43},
                                               {0x2000, 0x45},
                                               {0x4000, 0x46},
                                               {0x8000, 0xcb},
                                               {0x10000, 0xcc}}};
    std::uint8_t shown = 0;
    for (const StatusIcon& icon : Icons)
    {
        if ((frame.hero.flags78 & icon.bit) != icon.bit)
            continue;
        const int x = shown * 25 + 180;
        DrawStatusIcon(frame, icon.icon, x, 1, 20, 24);
        const Ui::Point mouse = frame.context.mouse;
        if (x < mouse.x && mouse.x < x + 20 && 1 < mouse.y && mouse.y < 25)
        {
            if (auto help = DrawTheStatusHelp(frame, x + 10, 0x1d, icon.icon); !help.HasValue())
                return help;
        }
        shown = static_cast<std::uint8_t>(shown + 1);
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 7. The arrows and the bolts (0x5ab810).

// What the party leaves on the right: with someone in the party, 10 with its window open or 60.
float PartyOffset(Frame& frame)
{
    if (frame.context.hud.partyCount <= 0)
        return 0.0f;
    return frame.panels.IsOpen(Ui::PartyPanel) ? 10.0f : 60.0f;
}

// An arrows text: with the normal blend, at (x, 4) in mode 1, and its size in 0x77f31b8 and 0x77f31bc
// (GetTextExtentPoint scaled to 640 × 480 with _ftol). An empty text is neither drawn nor measured.
void DrawAmmoText(Frame& frame, float x, std::string_view text)
{
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    if (text.empty())
        return;
    DrawHudText(frame, Truncate(x), 4, text, false);
    const Render::Text::TextExtent extent = frame.context.text.Measure(Render::Text::FontKind::Normal, text);
    frame.context.hud.arrowsTextWidth = Render::Text::DesignX(extent.width, frame.context.sprites.Screen());
    frame.context.hud.arrowsTextHeight = Render::Text::DesignY(extent.height, frame.context.sprites.Screen());
}

// 10. 0x5a736b..0x5a7452: with the macro brake (0x77f3b74), "Macro Time" centered on the world width (0x5bb8d0, with
// its queries) and the bar (0x5a6af0) with the brake / 2, in green and inside the screen. The text leaves its size
// in 0x77f31b8 and 0x77f31bc.
void DrawTheMacroTime(Frame& frame)
{
    Kernel::HudState& hud = frame.context.hud;
    const std::int32_t time = hud.macroTime;
    if (time <= 0)
        return;
    const std::int32_t worldWidth = frame.panels.WorldWidth();
    const float x = F32((static_cast<double>(worldWidth) - 50.0) * 0.5);
    const std::int32_t fill = time * 50 / 100;
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    DrawHudText(frame, Truncate(x), MacroTimeTextTop, MacroTimeText, false);
    const Render::Text::TextExtent extent = frame.context.text.Measure(Render::Text::FontKind::Normal, MacroTimeText);
    hud.arrowsTextWidth = Render::Text::DesignX(extent.width, frame.context.sprites.Screen());
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, frame.context.sprites.Screen());
    Ui::DrawGauge(frame.context.sprites, x, MacroTimeBarTop, MacroTimeBarWidth, MacroTimeBarHeight,
                  static_cast<float>(fill), false, true);
}

// 0x469350: both EDITs use the same vtable +0x14 (0x468320) as friends, letters and dialogs.
void DrawTheChatEdit(Frame& frame, Ui::OldEdit& edit, bool focused)
{
    Ui::GameHudContext& context = frame.context;
    Ui::OldDraw::DrawTheOldEdit(Ui::OldWindowDrawContext{.sprites = context.sprites,
                                                         .text = context.text,
                                                         .dcFont = context.hud.dcFont,
                                                         .textColors = context.hud.textColors,
                                                         .arrowHeld = context.arrowHeld},
                                edit, focused);
}

// 22. 0x5aa9a0: with the chat field open (or 0x77f3b69), the box without blend, the two EDITs (0x469350), with the
// whisper field chosen the names of the other places (each leaves its size in 0x77f31b8 and 0x77f31bc, the empty ones
// too), and with the whisper mode (F3) its indicator.
void DrawTheChatInput(Frame& frame)
{
    Ui::GameHudContext& context = frame.context;
    Kernel::HudState& hud = context.hud;
    const Ui::ChatInputState& input = frame.runtime.chat.input;
    context.sprites.SetColor(White);
    hud.dcFont = Render::Text::FontKind::Normal;
    if (!frame.runtime.ui.chatOpen && !input.otherInput)
        return;
    context.sprites.SetBlend(BlendMode::Opaque);
    Quad(context.sprites, ChatBoxLeftSlot, 186.0f, 415.0f, 134.0f, 29.0f, 0.0f, 0.0f, 0.51953125f, 0.90625f);
    Quad(context.sprites, ChatBoxRightSlot, 320.0f, 415.0f, 134.0f, 29.0f, 0.0f, 0.0f, 0.51953125f, 0.90625f);
    context.sprites.SetBlend(BlendMode::Alpha);
    if (input.shown)
    {
        Ui::OldEdit messageCopy;
        Ui::OldEdit whisperCopy;
        Ui::OldEdit& message = context.projecting ? (messageCopy = input.message) : frame.runtime.chat.input.message;
        Ui::OldEdit& whisper = context.projecting ? (whisperCopy = input.whisper) : frame.runtime.chat.input.whisper;
        DrawTheChatEdit(frame, message, input.focus == Ui::ChatFocus::Message);
        DrawTheChatEdit(frame, whisper, input.focus == Ui::ChatFocus::Whisper);
    }
    hud.textColors = WhisperNamesColors;
    if (input.field % 2 == Ui::WhisperField)
    {
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(Ui::WhisperNameSlots); ++slot)
        {
            if (slot == input.whisperNameAt)
                continue;
            const std::string_view name = Ui::CStringOf(input.whisperNames[static_cast<std::size_t>(slot)]);
            if (!name.empty())
                DrawHudText(frame, WhisperNamesLeft, (slot - input.whisperNameAt) * WhisperNamesStep + WhisperNamesTop,
                            name, false);
            const Render::Text::TextExtent extent = context.text.Measure(Render::Text::FontKind::Normal, name);
            hud.arrowsTextWidth = Render::Text::DesignX(extent.width, context.sprites.Screen());
            hud.arrowsTextHeight = Render::Text::DesignY(extent.height, context.sprites.Screen());
        }
    }
    if (input.whisperMode)
        Quad(context.sprites, WhisperModeSlot, 432.0f, 420.0f, 15.0f, 16.0f, 0.0f, 0.0f, 0.9375f, 1.0f);
}

Core::Expected<bool, std::string> DrawTheArrows(Frame& frame)
{
    const float partyOffset = PartyOffset(frame);
    constexpr std::array<int, 5> ResolutionOffsets{0x5a, 0x4b, 0x41, 0x3a, 10};
    const int resolutionOffset =
        ResolutionOffsets[static_cast<std::size_t>(ResolutionIndex(frame.context.sprites.Screen()))];
    bool shown = false;
    const auto own = OwnAccess(frame);
    const std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells(frame.context.world.Inventory().cells);
    struct Ammo
    {
        std::size_t slot = 0;
        std::uint16_t type = 0;
        std::size_t text = 0;
        bool countsTheInventory = false;
    };
    // 0x5ab96c: the arrows in the right hand also show the inventory's; 0x5abab3: the bolts in the left, only with
    // their own (the inventory count runs anyway).
    constexpr std::array<Ammo, 2> Kinds{{{0, ArrowsType, ArrowsText, true}, {1, BoltsType, BoltsText, false}}};
    for (const Ammo& ammo : Kinds)
    {
        const Kernel::ItemState& item = frame.own.equipment[ammo.slot];
        if (item.id != ammo.type)
            continue;
        const std::int32_t worldWidth = frame.panels.WorldWidth();
        const std::int32_t count = item.durability;
        const float x = F32(static_cast<double>(worldWidth) - static_cast<double>(partyOffset) -
                            static_cast<double>(resolutionOffset));
        // 0x557d80, with its own nested access to the record.
        std::int32_t total = 0;
        {
            const auto nested = OwnAccess(frame);
            total = Items::CountTheAmmo(frame.own.characterClass, frame.own.equipment[0].id, frame.own.equipment[1].id,
                                        cells);
        }
        if (count <= 0 && (!ammo.countsTheInventory || total <= 0))
            continue;
        auto format = TextOf(frame, ammo.text, "0x5ab9be");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        const std::string first = std::to_string(count);
        const std::string second = std::to_string(total);
        const std::array<std::string_view, 2> arguments{first, second};
        DrawAmmoText(frame, x, Ui::PlaceArguments(format.Value(), arguments));
        shown = true;
    }
    return shown;
}

// ---------------------------------------------------------------------------------------------------------------
// 8. The helper and the summon (0x5ab210).

Core::Expected<std::int32_t, std::string> DrawTheHelper(Frame& frame, bool arrowsShown)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    float partyOffset = 0.0f;
    std::int32_t top = 15;
    if (frame.context.hud.partyCount > 0 && !frame.panels.IsOpen(Ui::PartyPanel))
        partyOffset = 50.0f;
    const auto pet = static_cast<std::int16_t>(frame.hero.pet.model);
    if ((pet > 0x1c11 && pet < 0x1c17) || pet == FenrirModel)
    {
        const std::int32_t worldWidth = frame.panels.WorldWidth();
        const float x = F32(static_cast<double>(worldWidth - 50) - static_cast<double>(partyOffset) - 15.0);
        const float y = arrowsShown ? 24.0f : 4.0f;
        std::uint32_t fill = 0;
        {
            const auto own = OwnAccess(frame);
            fill = (static_cast<std::uint32_t>(frame.own.equipment[HelperSlot].durability) * 50u) / 255u;
        }
        sprites.SetBlend(BlendMode::Alpha);
        std::string name;
        std::optional<std::size_t> text;
        if (pet == UniriaModel)
            text = UniriaText;
        else if (pet == DinorantModel)
            text = DinorantText;
        else if (pet == DarkHorseModel)
            text = DarkHorseText;
        else if (pet == FenrirModel)
            text = FenrirText;
        if (text)
        {
            auto found = TextOf(frame, *text, "0x5ab2f0");
            if (!found.HasValue())
                return Core::Unexpected(found.Error());
            name = std::string(found.Value());
        }
        else
        {
            auto record = ItemRecordOf(frame, pet - static_cast<std::int32_t>(Tables::FirstItemModel), "0x5ab2f0");
            if (!record.HasValue())
                return Core::Unexpected(record.Error());
            name = Items::ItemRecordName(record.Value());
        }
        if (!name.empty())
            DrawHudText(frame, Truncate(x) + 50, Truncate(y), name, true);
        Ui::DrawGauge(sprites, x, F32(static_cast<double>(y) + 12.0), 50.0f, 2.0f, static_cast<float>(fill), false,
                      true);
        top = Truncate(y) + 0x17;
    }
    const std::int32_t summonLife = frame.context.hud.summonLife;
    if (summonLife != 0)
    {
        const std::int32_t worldWidth = frame.panels.WorldWidth();
        const std::int32_t fill = summonLife * 50 / 100;
        const float x = F32(static_cast<double>(worldWidth - 50) - 150.0);
        sprites.SetBlend(BlendMode::Alpha);
        auto text = TextOf(frame, SummonText, "0x5ab5a6");
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        if (!text.Value().empty())
            DrawHudText(frame, Truncate(x) + 50, 4, text.Value(), true);
        Ui::DrawGauge(sprites, x, 16.0f, 50.0f, 2.0f, static_cast<float>(fill), false, true);
    }
    return top;
}

// ---------------------------------------------------------------------------------------------------------------
// 9. The durability warnings (0x5aac20).

bool ShowsNoDurability(std::int16_t type)
{
    return type == static_cast<std::int16_t>(BoltsType) || type == static_cast<std::int16_t>(ArrowsType) ||
           (type >= 0x1a00 && type <= 0x1a03) || type == 0x1a25;
}

Core::Expected<void, std::string> DrawTheDurability(Frame& frame, std::int32_t top)
{
    if (frame.panels.IsOpen(Ui::TradePanel))
        return {};
    Kernel::HudState& hud = frame.context.hud;
    Render::SpriteBatch& sprites = frame.context.sprites;
    const float partyOffset = PartyOffset(frame);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    const std::int32_t worldWidth = frame.panels.WorldWidth();
    const float x = F32(static_cast<double>(worldWidth) - static_cast<double>(partyOffset));
    const auto y = static_cast<float>(top);
    float offset = 0.0f;
    for (std::size_t slot = 0; slot < Kernel::OwnEquipmentSlotCount; ++slot)
    {
        hud.textColors.background = 0x80000000u;
        const auto own = OwnAccess(frame);
        Kernel::ItemState& item = frame.own.equipment[slot];
        if (item.id == Kernel::EmptyOwnItemId)
            continue;
        const auto type = static_cast<std::int16_t>(item.id);
        std::optional<std::uint8_t> warning;
        if (!ShowsNoDurability(type))
        {
            auto record = ItemRecordOf(frame, type, "0x5aaf2a");
            if (!record.HasValue())
                return Core::Unexpected(record.Error());
            std::uint16_t maximum =
                Items::MaxDurability(item, record.Value(), static_cast<int>((item.scalar1Low >> 3) & 0xfu));
            if (item.id == 0x1a0a)
                maximum = 200;
            const std::uint8_t durability = item.durability;
            bool shown = true;
            if (durability == 0)
            {
                warning = std::uint8_t{8};
                hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0x800a0affu};
            }
            else
            {
                // 0x5aaf6d..0x5aafce: fild of the maximum, stored in float, times 0.2, 0.3 and 0.5 in float, against
                // the durability in the x87.
                const auto current = static_cast<double>(durability);
                const auto limit = static_cast<double>(static_cast<float>(maximum));
                if (current <= limit * static_cast<double>(0.2f))
                {
                    warning = std::uint8_t{7};
                    hud.textColors.text = 0xff0a0affu;
                }
                else if (current <= limit * static_cast<double>(0.3f))
                {
                    warning = std::uint8_t{6};
                    hud.textColors.text = 0xff009cffu;
                }
                else if (current <= limit * static_cast<double>(0.5f))
                {
                    warning = std::uint8_t{5};
                    hud.textColors.text = 0xff00e4ffu;
                }
                else
                    shown = false;
            }
            if (shown)
            {
                // 0x6c9304
                const std::string text =
                    std::format("{} ({}/{})", Items::ItemRecordName(record.Value()), durability, maximum);
                hud.durabilityWarning = 1;
                DrawHudText(frame, Truncate(x), Truncate(y) + Truncate(offset), text, true);
                offset = static_cast<float>(Truncate(offset) + 12);
            }
        }
        // 0x5ab04e: without the requirements, warning 3, also on the arrows and the helpers.
        if (static_cast<std::uint16_t>(frame.own.agility * 2) < item.word20 ||
            static_cast<std::uint16_t>(frame.own.energy * 2) < item.word22 ||
            static_cast<std::uint16_t>(frame.own.word2a + frame.own.strength) < item.word1e)
            warning = std::uint8_t{3};
        if (warning && !frame.context.projecting)
            item.warning = *warning;
    }
    hud.textColors.background = 0x80000000u;
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 11. The event object ([0x723c60]): 0x40bab0 and its method +4.

// 0x40bafe and 0x40bb08: the countdown's colors (0x6c8e5c and 0x6c8e64), and 0x40bbd1: where 0x554180 writes it.
constexpr std::uint32_t EventCountdownTextColor = 0xffff8080u;
constexpr std::uint32_t EventCountdownBackground = 0x80000000u;
constexpr int EventCountdownLeft = 10;
constexpr int EventCountdownTop = 0x174;

Core::Expected<void, std::string> DrawTheEventObject(Frame& frame)
{
    if (!frame.runtime.eventObject)
        return {};
    Events::EventMapObject& event = *frame.runtime.eventObject;
    // 0x40bab0: the 30-second countdown with +8 from 1 to 14 (GetTickCount against +0xc and the entry's texts). An
    // intermediate drawing does not turn off the expired countdown: the original's next frame does it again.
    auto countdown =
        Events::EventCountdownText(event, frame.context.tickCount, frame.context.texts, !frame.context.projecting);
    if (!countdown.HasValue())
        return Core::Unexpected(std::move(countdown).Error());
    if (countdown.Value())
    {
        // 0x40baf2..0x40bbd8: 0x620980 and 0x620a00(0), the text in 0xffff8080 on 0x80000000 and 0x554180(10, 0x174).
        frame.context.sprites.SetBlend(BlendMode::Opaque);
        frame.context.sprites.SetBlend(BlendMode::Alpha);
        frame.context.hud.textColors =
            Render::Text::TextColors{.text = EventCountdownTextColor, .background = EventCountdownBackground};
        DrawHudText(frame, EventCountdownLeft, EventCountdownTop, *countdown.Value(), false);
    }
    // Method +4: Blood Castle's (0x40c2d0), Chaos Castle's (0x405090) and Devil Square's (0x40bdb0), a ret. The 9B
    // writes the fields (CL9 milestone 11). An intermediate drawing does not choose the trap's area.
    return Events::DrawTheCastleTimeAndPoints(frame.context, event, !frame.context.projecting);
}

// ---------------------------------------------------------------------------------------------------------------
// 18. The windows (0x5f7440).

// 0x5f7465
constexpr std::int32_t PartyWindowLeft = 0x1c2;

Core::Expected<void, std::string> DrawTheWindows(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Ui::PanelRegistry& panels = frame.panels;
    const Ui::PanelState& state = frame.runtime.panels;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    frame.context.hud.textColors.text = 0xffff8080u;
    // 0x5f7474: 0x5f0be0(0x1c2, 0), which starts with its IsOpen.
    if (auto party = Party::DrawThePartyWindow(frame.context, panels, PartyWindowLeft, 0); !party.HasValue())
        return party;
    // 0x5f7487: 0x5ee2e0 and the close button, with the character window open.
    if (panels.IsOpen(Ui::CharacterPanel))
    {
        if (auto character = DrawTheCharacterWindow(frame.context); !character.HasValue())
            return character;
    }
    // 0x5f2340, which starts with its IsOpen.
    if (auto inventory = DrawTheInventory(frame.context, panels); !inventory.HasValue())
        return inventory;
    // 0x5f3b40, which starts with its IsOpen.
    if (auto trade = DrawTheTradeWindow(frame.context, panels); !trade.HasValue())
        return trade;
    // 0x5f3030, which starts with its IsOpen (CL9 milestone 7).
    if (auto shop = DrawThePersonalShopWindow(frame.context, panels); !shop.HasValue())
        return shop;
    // 0x5f4720, which starts with its IsOpen (the protected byte, 1 rand()).
    if (auto shop = Npc::DrawTheNpcShop(frame.context, panels); !shop.HasValue())
        return shop;
    // 0x5f75b2: 0x5febf0, which starts with its IsOpen.
    if (auto chaos = ChaosMix::DrawTheChaosBox(frame.context, panels); !chaos.HasValue())
        return chaos;
    // 0x5f75b7: 0x44b510, which checks the mode.
    if (auto trainer = PetTrainer::DrawTheTrainer(frame.context); !trainer.HasValue())
        return trainer;
    if (auto siege = CastleSiege::DrawTheSiegeControls(frame.context); !siege.HasValue())
        return siege;
    // 0x5f4e30, which starts with its IsOpen (no rand()).
    if (auto warehouse = Npc::DrawTheWarehouse(frame.context, panels); !warehouse.HasValue())
        return warehouse;
    // 0x5f75cb: 0x5f58f0, the event NPCs (CL9 milestone 11), which starts with their IsOpen.
    if (auto events = Events::DrawTheEventNpcWindow(frame.context, panels); !events.HasValue())
        return events;
    // 0x5f75d0: 0x5f6550, with 0x78abb00 other than 0.
    if (state.field78abb00 != 0)
    {
        if (auto chips = Events::DrawTheEventChipWindow(frame.context); !chips.HasValue())
            return chips;
    }
    // 0x5f75db (CL9 milestone 2): 0x41ca40, the quest window and the character window's button.
    if (auto quest = Quests::DrawTheQuestWindow(frame.context, panels); !quest.HasValue())
        return quest;
    // 0x5f75e7: 0x424160 with the hero, always: the pet's button and window and the raven's sign.
    if (auto pet = Pets::DrawThePetWindow(frame.context, panels); !pet.HasValue())
        return pet;
    if (auto drawn = Npc::DrawThePanel2000(frame.context, panels); !drawn.HasValue())
        return drawn;
    // 0x5f75fa: 0x40a650, which starts with +4 of [0x6e592c].
    if (auto commands = DrawTheCommandsWindow(frame.context); !commands.HasValue())
        return commands;
    // 0x6579e0: [0x7d536f4] through its vtable + 0x14 (0x656e70) first.
    if (panels.IsOpen(Ui::SecondGuildPanel))
    {
        if (auto second = Guild::DrawTheSecondGuildWindow(frame.context); !second.HasValue())
            return second;
    }
    // 0x657a18: [0x7d536f0] through its vtable + 0x14 (0x651c20).
    if (panels.IsOpen(Ui::GuildPanel))
    {
        if (auto guild = Guild::DrawTheGuildWindow(frame.context); !guild.HasValue())
            return guild;
    }
    if (auto castle = CastleSiege::DrawTheCastleWindows(frame.context); !castle.HasValue())
        return castle;
    // 0x6579e0: 0x660830, the panels' dialog, if it is open.
    return Guild::DrawThePanelDialog(frame.context, frame.runtime.panels.delegated.panelDialog);
}

// ---------------------------------------------------------------------------------------------------------------
// 20. The bottom bar (0x5a9290).

void DrawTheBottomBar(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    frame.context.hud.durabilityWarning = 0;
    frame.context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    Quad(sprites, 0xe9, 0.0f, 387.0f, 108.0f, 45.0f, 0.0f, 0.0f, 0.84375f, 0.703125f);
    Quad(sprites, 0xe9, 532.0f, 387.0f, 108.0f, 45.0f, 0.84375f, 0.0f, -0.84375f, 0.703125f);
    sprites.SetBlend(BlendMode::Opaque);
    Quad(sprites, 0xe6, 0.0f, 432.0f, 256.0f, 48.0f, 0.0f, 0.0f, 1.0f, 0.75f);
    {
        // 0x5a93b5..0x5a9455: opens and closes the own record without reading anything that changes the drawing.
        const auto own = OwnAccess(frame);
    }
    Quad(sprites, 0xe7, 256.0f, 432.0f, 128.0f, 48.0f, 0.0f, 0.0f, 1.0f, 0.75f);
    Quad(sprites, 0xe8, 384.0f, 432.0f, 256.0f, 48.0f, 0.0f, 0.0f, 1.0f, 0.75f);
    sprites.SetBlend(BlendMode::Opaque);
}

// ---------------------------------------------------------------------------------------------------------------
// 23. The experience (0x5abcd0).

Core::Expected<void, std::string> DrawTheExperience(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto own = OwnAccess(frame);
    const Kernel::OwnCharacterRecord& record = frame.own;
    if (record.level != 0)
    {
        // 0x5abe3b..0x5abe74: the level's experience, in 32 bits.
        const std::uint32_t steps = static_cast<std::uint32_t>(record.level) - 1u;
        std::uint32_t base = 0;
        if (static_cast<std::int32_t>(steps) > 0)
        {
            base = (steps + 9u) * steps * steps * 10u;
            if (steps > 0xffu)
            {
                const std::uint32_t more = steps - 0xffu;
                base += (more + 9u) * more * more * 1000u;
            }
        }
        // 0x5abe77..0x5abebd: fild qword of the two unsigned differences; the done part is 0 if the experience does not
        // reach the base.
        const auto needed = static_cast<double>(record.nextExperience - base);
        const double done = record.experience < base ? 0.0 : static_cast<double>(record.experience - base);
        float fill = 0.0f;
        if (done > 0.0)
            fill = F32(done / needed * 10.0);
        // 0x5abef0..0x5abf1d: subtracts 1 in the x87 while it is above 1. Infinity (no experience for the next level)
        // does not end.
        double fraction = fill;
        if (fill > 1.0f)
        {
            if (std::isinf(fill))
                return Core::Unexpected(std::string(
                    "0x5abf0a: sin experiencia entre el nivel y el que sigue, la barra del original no termina"));
            fraction = static_cast<double>(fill) - std::ceil(static_cast<double>(fill) - 1.0);
        }
        const float barFraction = F32(fraction);
        sprites.SetColor(Color{0.92f, 0.8f, 0.34f, 1.0f});
        sprites.ColorQuad(221.0f, 439.0f, F32(static_cast<double>(barFraction) * 198.0), 4.0f);
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(Color{0.91f, 0.81f, 0.6f, 1.0f});
        Ui::DrawNumber(sprites, 425.0f, 434.0f, Truncate(fill), 9.0f, 10.0f);
        sprites.SetBlend(BlendMode::Opaque);
        sprites.SetColor(White);
    }
    if (MouseIn(frame, 219, 439, 421, 445))
    {
        auto format = TextOf(frame, ExperienceHelpText, "0x5ac007");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        const std::string experience = std::to_string(record.experience);
        const std::string next = std::to_string(record.nextExperience);
        const std::array<std::string_view, 2> arguments{experience, next};
        DrawHelp(frame, 223, 424, Ui::PlaceArguments(format.Value(), arguments));
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 24. The AG (0x5a8cc0).

Core::Expected<void, std::string> DrawTheAg(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    std::uint16_t maximum = 1;
    std::uint16_t current = 0;
    {
        const auto own = OwnAccess(frame);
        if (frame.own.maxBp != 0)
            maximum = frame.own.maxBp;
        current = frame.own.bp < maximum ? frame.own.bp : maximum;
    }
    const std::int32_t height = static_cast<std::int32_t>(current) * 36 / static_cast<std::int32_t>(maximum);
    const auto fill = static_cast<float>(height);
    sprites.SetColor(NumberColor);
    Quad(sprites, 0x101, 551.0f, F32(473.0 - static_cast<double>(fill)), 15.0f, fill, 0.0f, 0.0f, 0.9375f,
         F32(static_cast<double>(fill) * 0.015625));
    Ui::DrawNumber(sprites, 571.0f, 467.0f, current, 9.0f, 10.0f);
    if (MouseIn(frame, 551, 437, 566, 473))
    {
        auto format = TextOf(frame, AgHelpText, "0x5a8e7e");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        const std::string first = std::to_string(current);
        const std::string second = std::to_string(maximum);
        const std::array<std::string_view, 2> arguments{first, second};
        DrawHelp(frame, 546, 419, Ui::PlaceArguments(format.Value(), arguments));
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 27. The UI's 3D (0x5ac120) and 28. the grids (0x5f7840).

Core::Expected<void, std::string> DrawTheUi3DItems(Frame& frame)
{
    const Ui::PanelState& state = frame.runtime.panels;
    // 0x5ac1c7 (CL9 milestone 2): 0x41b9e0, the quest window's requested items.
    if (auto quest = Quests::DrawTheQuestItems(frame.context); !quest.HasValue())
        return quest;
    // 0x5ac1cc..0x5ac1df: with 0x78abb00 other than 0 and 3, 0x5f70a0 and the jump to 0x5ac269, without the grids, the
    // cursor item or the quick slots.
    if (state.field78abb00 != 0 && state.field78abb00 != Events::SerialNumberWindow)
        return Events::DrawTheEventChipItems(frame.context);
    // 0x5f7610: the open grids' items, in their order, and the trainer's box (0x44ad40(0)).
    if (auto grids = DrawTheGridItems(frame.context, frame.panels); !grids.HasValue())
        return grids;
    // 0x5ac1e9..0x5ac204: the cursor item (0x5f7b10), with 0x7879fbc above 0 (DrawTheCursorItem checks it), without a
    // pending notice (0x78abb14 at 0: 0x633110 draws the selling one in its dialog) and without the own shop's price
    // dialog (0x78aba60 other than 2).
    constexpr std::int32_t ShopPriceDialog = 2;
    const Social::SocialState& social = frame.runtime.social;
    if (social.trade.warningKind == 0 && social.personalShop.dialogMode != ShopPriceDialog)
    {
        if (auto cursor = DrawTheCursorItem(frame.context); !cursor.HasValue())
            return cursor;
    }
    // 0x5ac209..0x5ac267: the three quick slots, each with 0x5e1480((float)(208 + 31i), 454, 20, 20, type, +4, 0, 0,
    // 0).
    const std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells(frame.context.world.Inventory().cells);
    for (int slot = 0; slot < 3; ++slot)
    {
        auto found = Items::FindQuickSlotItem(frame.context.hud.quickSlots, cells, slot,
                                              frame.runtime.movement.gateRequestBlocked != 0);
        if (!found.HasValue())
            return Core::Unexpected(found.Error());
        if (!found.Value())
            continue;
        const Kernel::ItemState& item = cells[*found.Value()];
        const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
        const Items::UiItemBox box{
            .x = static_cast<float>(208 + 31 * slot), .y = 454.0f, .width = 20.0f, .height = 20.0f};
        if (auto drawn = DrawUiItem(frame.context, box, type, item.scalar1Low, 0, 0, false); !drawn.HasValue())
            return drawn;
    }
    return {};
}

// 0x5ac120 closes the 2D and sets up its 1° projection of the whole screen, with the camera at the origin and depth
// on: the items' pass (Items/UiItemDrawing.h), which the first one to draw opens. At the end (0x5ac269..0x5ac2bb) it
// puts back the world camera without its turn in 0x7d366f0 and 0x7d36834 (P-C73) and opens the 2D again.
Core::Expected<void, std::string> DrawTheUi3D(Frame& frame)
{
    auto drawn = DrawTheUi3DItems(frame);
    if (frame.context.uiItems != nullptr)
        Items::CloseUiItemPass(*frame.context.uiItems);
    return drawn;
}

// 0x5f7840.
Core::Expected<void, std::string> DrawTheGrids(Frame& frame)
{
    return DrawTheGridNumbers(frame.context, frame.panels);
}

// ---------------------------------------------------------------------------------------------------------------
// The bar's skill (0x5a5860, 0x40ca20 and 0x5531d0).

bool IsRiding(std::int16_t pet)
{
    return pet == UniriaModel || pet == DinorantModel || pet == FenrirModel;
}

// 0x40ca20(skill, energy, leadership): whether the character falls short of the skill. The energy it asks for is the
// Skill.bmd record's (+0x2c · +0x20 · 4 / 100 + 20), except the table's, and leadership (+0x2e) is only checked when
// the one asked for is above 0.
Core::Expected<bool, std::string> CannotUseTheSkill(Frame& frame, std::uint32_t skill, std::int32_t energy,
                                                    std::int32_t leadership)
{
    auto record = SkillRecordOf(frame, skill, "0x40ca20");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const auto entry = SkillAccess(frame, skill);
    std::uint32_t required = ByteAt(record.Value(), 0x2c) * ByteAt(record.Value(), 0x20) * 4u / 100u + 0x14u;
    switch (skill)
    {
    case 0x11:
    case 0x3e:
    case 0x4c:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0x7b:
        required = 0;
        break;
    case 0x1e:
        required = 30;
        break;
    case 0x1f:
        required = 60;
        break;
    case 0x20:
        required = 90;
        break;
    case 0x21:
        required = 130;
        break;
    case 0x22:
        required = 170;
        break;
    case 0x23:
        required = 210;
        break;
    case 0x24:
        required = 300;
        break;
    case 0x3c:
        required = 15;
        break;
    default:
        break;
    }
    const bool withoutEnergy = (skill >= 0x43 && skill <= 0x48) || (skill > 0x11 && skill < 0x17) ||
                               (skill > 0x28 && skill < 0x2c) || (skill > 0x2e && skill < 0x32) || skill == 0x18 ||
                               skill == 0x33 || skill == 0x34 || skill == 0x37 || skill == 0x38 || skill == 0x2c ||
                               skill == 0x2d || skill == 0x2e || skill == 0x39 || skill == 0x49 || skill == 0x4a;
    if (withoutEnergy)
        required = 0;
    const auto leadershipRequired = static_cast<std::int32_t>(LittleEndianAt(record.Value(), 0x2e, 2));
    if (0 < leadership && leadership < leadershipRequired)
        return true;
    return energy < static_cast<std::int32_t>(required);
}

// 0x5531d0(index, x, y, w, h): the skill's delay (+0x76c of the own record), which covers the icon from the bottom up
// with half red. Nobody writes it until CL9.2.
Core::Expected<void, std::string> DrawTheSkillDelay(Frame& frame, std::uint32_t index, int x, int y, int width,
                                                    int height)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto own = OwnAccess(frame);
    if (index >= frame.own.skillDelays.size())
        return Core::Unexpected(std::format("0x5531f9: la espera {} pasa del equipo del registro propio", index));
    const std::int32_t delay = frame.own.skillDelays[index];
    if (delay <= 0)
        return {};
    const std::optional<std::uint8_t> skill = frame.own.SkillAt(index);
    if (!skill)
        return Core::Unexpected(
            std::format("0x5532a3: la skill {} del registro propio (+0x6c + {}) no está en MuRet", index, index));
    auto record = SkillRecordOf(frame, *skill, "0x5531d0");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    std::int32_t total = 0;
    {
        const auto entry = SkillAccess(frame, *skill);
        total = static_cast<std::int32_t>(LittleEndianAt(record.Value(), 0x28, 4));
    }
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(Color{1.0f, 0.5f, 0.5f, 0.5f});
    const float fill =
        F32(static_cast<double>(delay) / static_cast<double>(total) * static_cast<double>(static_cast<float>(height)));
    sprites.ColorQuad(static_cast<float>(x),
                      F32(static_cast<double>(static_cast<float>(y)) + static_cast<double>(static_cast<float>(height)) -
                          static_cast<double>(fill)),
                      static_cast<float>(width), fill);
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}

// Icon `index` of an 8-column atlas of w × h texels (0x5a6140..0x5a61b4).
void DrawSkillQuad(Frame& frame, std::uint16_t slot, std::int32_t index, int x, int y, int width, int height)
{
    Quad(frame.context.sprites, slot, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
         static_cast<float>(height), F32(static_cast<double>((index % 8) * width) * Texel),
         F32(static_cast<double>((index / 8) * height) * Texel), F32(static_cast<double>(width) * Texel),
         F32((static_cast<double>(height) - 1.0) * Texel));
}

// 0x5a5860(index, x, y, w, h): skill `index` of the own record (+0x6c + index; from 0x78 on, the index is the
// skill), tinted red if it cannot be used, with its delay and its number.
Core::Expected<void, std::string> DrawTheSkillIcon(Frame& frame, std::uint32_t index, int x, int y, int width,
                                                   int height)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    std::uint8_t key = 0xff;
    {
        const auto own = OwnAccess(frame);
        std::uint32_t skill = index;
        if (index <= 0x77)
        {
            const std::optional<std::uint8_t> fromRecord = frame.own.SkillAt(index);
            if (!fromRecord)
                return Core::Unexpected(std::format(
                    "0x5a58c4: la skill {} del registro propio (+0x6c + {}) no está en MuRet", index, index));
            skill = *fromRecord;
        }
        if (skill == 0)
            return {};
        std::uint8_t kind = 0;
        {
            auto record = SkillRecordOf(frame, skill, "0x5a5926");
            if (!record.HasValue())
                return Core::Unexpected(record.Error());
            const auto entry = SkillAccess(frame, skill);
            kind = ByteAt(record.Value(), 0x31);
        }
        const auto pet = static_cast<std::int16_t>(frame.hero.pet.model);
        const std::int32_t map = frame.runtime.map;
        bool tinted = false;
        if ((skill == 0x2c || skill == 0x2d || skill == 0x2e || skill == 0x39 || skill == 0x49 || skill == 0x4a) &&
            (map != CastleSiegeMap || !frame.runtime.castleFlags.siegeActive))
            tinted = true;
        if ((frame.hero.flags78 & 0x200u) != 0x200u && kind == 2)
            tinted = true;
        if (skill == 0x2f)
        {
            if (!IsRiding(pet))
                tinted = true;
        }
        else if (((skill > 0x11 && skill < 0x18) || skill == 0x38) && IsRiding(pet))
            tinted = true;
        const std::int32_t energy = frame.own.word30 + frame.own.energy;
        auto cannot = CannotUseTheSkill(frame, skill, energy, 0);
        if (!cannot.HasValue())
            return Core::Unexpected(cannot.Error());
        if (cannot.Value())
            tinted = true;
        // 0x5a5c43 and 0x5a5de2: the helper without durability, read with a nested access to the record.
        const auto helperWornOut = [&frame]
        {
            const auto nested = OwnAccess(frame);
            return frame.own.equipment[HelperSlot].durability == 0;
        };
        if (skill == 0x3f)
        {
            if (frame.context.hud.partyCount < 1)
                tinted = true;
        }
        else if (skill == 0x3e && helperWornOut())
            tinted = true;
        if (map < FirstChaosCastleMap || map > LastChaosCastleMap)
        {
            if (skill == 0x3e && helperWornOut())
                tinted = true;
        }
        else if (skill == 0x3e || skill == 0x31 || (skill > 0x77 && skill < 0x7c))
            tinted = true;
        cannot = CannotUseTheSkill(frame, skill, energy, frame.own.word36 + frame.own.leadership);
        if (!cannot.HasValue())
            return Core::Unexpected(cannot.Error());
        if (cannot.Value())
            tinted = true;
        if (tinted)
            sprites.SetColor(SkillTint);

        bool numbered = false;
        if (skill >= 0x78 && skill <= 0x7c)
        {
            DrawSkillQuad(frame, 300, static_cast<std::int32_t>(skill) - 0x78, x, y, width, height);
            numbered = true;
        }
        else if (skill == 0x4c)
        {
            Quad(sprites, 300, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                 static_cast<float>(height), F32(static_cast<double>(width * 4) * Texel), 0.0f,
                 F32(static_cast<double>(width) * Texel), F32((static_cast<double>(height) - 1.0) * Texel));
        }
        else
        {
            const bool first = skill < 0x39;
            DrawSkillQuad(frame, first ? 0x12a : 299, static_cast<std::int32_t>(skill) - (first ? 1 : 0x39), x, y,
                          width, height);
            numbered = skill > 0x71;
        }
        if (numbered)
        {
            // 0x5a61e7: the command skill's number, from 1.
            sprites.SetColor(SkillColor);
            Ui::DrawNumber(sprites, F32(static_cast<double>(static_cast<float>(x)) + 4.0),
                           F32(static_cast<double>(static_cast<float>(y)) + 26.0), static_cast<int>(skill) - 0x77, 9.0f,
                           10.0f);
            return {};
        }
        if (auto delay = DrawTheSkillDelay(frame, index, x, y, width, height); !delay.HasValue())
            return delay;
        sprites.SetBlend(BlendMode::Alpha);
        // 0x5a630b..0x5a6325: read the key before the own close; the number goes after (0x5a6374..0x5a63b6).
        const std::size_t place = frame.context.chosenSlot.value_or(0);
        if (place < frame.own.skillKeys.size() && index < Kernel::OwnSkillKeyBytes)
            key = frame.own.skillKeys[place][index];
    }
    if (key != 0xff)
    {
        sprites.SetColor(SkillColor);
        Ui::DrawNumber(sprites, F32(static_cast<double>(x) + 4.0), F32(static_cast<double>(y) + 26.0), key, 9.0f,
                       10.0f);
    }
    return {};
}

// 0x421b90: the four orders only with the raven, with no other class gate. Each icon can change the color.
Core::Expected<void, std::string> DrawTheRavenCommands(Frame& frame)
{
    if (!frame.hero.darkSpirit)
        return {};
    constexpr Color Unselected{0.6f, 0.7f, 0.8f, 1.0f};
    for (std::uint32_t order = 0; order < 4; ++order)
    {
        const std::uint32_t slot = 0x78 + order;
        frame.context.sprites.SetColor(slot == frame.hero.skillIndex ? SkillColor : Unselected);
        if (auto drawn = DrawTheSkillIcon(frame, slot, 256 + static_cast<int>(order) * 32, 330, 32, 36);
            !drawn.HasValue())
            return drawn;
    }
    return {};
}

// 0x5a9f9a..0x5aa356: the 64 slots with the outer own access open. The drawing goes down 46 only once; the native
// hitboxes go down 52 (0x5a083c), so they are kept independent. There is no third row or paging.
Core::Expected<void, std::string> DrawTheSkillList(Frame& frame)
{
    constexpr int RowSize = 20, IconWidth = 32, IconHeight = 36, RowStep = 46;
    constexpr Color Unselected{0.6f, 0.7f, 0.8f, 1.0f};
    const int width = std::min(static_cast<int>(frame.own.byte6a) - static_cast<int>(frame.own.byte6b), RowSize);
    int shown = 0;
    int column = 0;
    int y = 370;
    for (std::uint32_t slot = 0; slot < 64; ++slot)
    {
        const std::uint8_t skill = frame.own.SkillAt(slot).value_or(0);
        if (skill == 0 || (skill >= 0x43 && skill <= 0x48))
            continue;
        auto record = SkillRecordOf(frame, skill, "0x5aa181");
        if (!record.HasValue())
            return Core::Unexpected(record.Error());
        std::uint8_t hidden = 0;
        {
            const auto entry = SkillAccess(frame, skill);
            hidden = ByteAt(record.Value(), 0x31);
        }
        if (hidden == 1)
            continue;
        if (shown == RowSize)
        {
            y -= RowStep;
            column = 0;
        }
        // The operands (bytes and up to 64 columns) keep all these integers exact also in float/ftol.
        const int x = column * IconWidth + 320 - width * 16;
        ++shown;
        ++column;
        frame.context.sprites.SetColor(slot == frame.hero.skillIndex ? SkillColor : Unselected);
        if (auto drawn = DrawTheSkillIcon(frame, slot, x, y, IconWidth, IconHeight); !drawn.HasValue())
            return drawn;
    }
    return DrawTheRavenCommands(frame);
}

// ---------------------------------------------------------------------------------------------------------------
// 29. The main bar (0x5a9630).

// An orb: the slot at (x, 432 + missing · 48, 53, 48 − missing · 48 / 64), with v at missing · 48 / 64. The height
// subtracts the v and not the pixels, like the original (0x5a9840).
void DrawOrb(Frame& frame, std::uint16_t slot, float x, float missing)
{
    const double pixels = static_cast<double>(missing) * 48.0;
    const float v = F32(pixels * 0.015625);
    Quad(frame.context.sprites, slot, x, F32(pixels + 432.0), 53.0f, F32(48.0 - static_cast<double>(v)), 0.0f, v,
         0.828125f, 0.75f);
}

Core::Expected<void, std::string> HelpWithText(Frame& frame, int x, int y, std::size_t index, std::string_view where)
{
    auto text = TextOf(frame, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    DrawHelp(frame, x, y, text.Value());
    return {};
}

Core::Expected<void, std::string> HelpWithValues(Frame& frame, int x, int y, std::size_t index, std::uint32_t first,
                                                 std::uint32_t second, std::string_view where)
{
    auto format = TextOf(frame, index, where);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    const std::string one = std::to_string(first);
    const std::string two = std::to_string(second);
    const std::array<std::string_view, 2> arguments{one, two};
    DrawHelp(frame, x, y, Ui::PlaceArguments(format.Value(), arguments));
    return {};
}

Core::Expected<void, std::string> DrawTheMainBar(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Ui::PanelRegistry& panels = frame.panels;
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    std::uint16_t maxLife = 0;
    std::uint16_t life = 0;
    std::uint16_t maxMana = 0;
    std::uint16_t mana = 0;
    {
        const auto own = OwnAccess(frame);
        maxLife = frame.own.maxLife;
        life = frame.own.life < maxLife ? frame.own.life : maxLife;
        maxMana = frame.own.maxMana;
        mana = frame.own.mana < maxMana ? frame.own.mana : maxMana;
    }
    // 0x5a9796..0x5a97b8: the missing part over the maximum (fidiv: with a maximum of 0, NaN).
    const float missingLife = F32(static_cast<double>(maxLife - life) / static_cast<double>(maxLife));
    const float missingMana = F32(static_cast<double>(maxMana - mana) / static_cast<double>(maxMana));
    if (life != 0 && static_cast<double>(life) / static_cast<double>(maxLife) < static_cast<double>(0.2f))
        frame.context.RequestSound(Audio::SoundId{0x22});
    // 0x5a97fb: the green orb with poison (+0x78 & 1).
    DrawOrb(frame, (frame.hero.flags78 & 1u) == 1u ? 0xf7 : 0xea, 97.0f, missingLife);
    if (MouseIn(frame, 97, 432, 150, 480))
    {
        if (auto help = HelpWithValues(frame, 103, 419, LifeHelpText, life, maxLife, "0x5a9881"); !help.HasValue())
            return help;
    }
    DrawOrb(frame, 0xeb, 489.0f, missingMana);
    if (MouseIn(frame, 489, 432, 542, 480))
    {
        if (auto help = HelpWithValues(frame, 495, 419, ManaHelpText, mana, maxMana, "0x5a9934"); !help.HasValue())
            return help;
    }
    struct Button
    {
        int left = 0;
        std::size_t help = 0;
        Ui::PanelId panel = 0;
        std::uint16_t slot = 0;
    };
    constexpr std::array<Button, 3> Buttons{{{348, PartyHelpText, Ui::PartyPanel, 0xec},
                                             {379, CharacterHelpText, Ui::CharacterPanel, 0xed},
                                             {410, InventoryHelpText, Ui::InventoryPanel, 0xee}}};
    for (const Button& button : Buttons)
    {
        if (MouseIn(frame, button.left, 452, button.left + 24, 476))
        {
            if (auto help = HelpWithText(frame, button.left, 437, button.help, "0x5a99a0"); !help.HasValue())
                return help;
        }
        if (panels.IsOpen(button.panel))
            Quad(sprites, button.slot, static_cast<float>(button.left), 452.0f, 24.0f, 24.0f, 0.0f, 0.0f, 0.75f, 0.75f);
    }
    // 0x5a9c98..0x5a9dd3: with the mouse over the friends button, the three-line help (texts 1043 to 1045) that
    // 0x78793c8 builds and 0x5af090 draws, the tooltip one of milestone 4; it only draws, but the icons that follow
    // come out with the blend and color it leaves (P-C63). 0x5a9ddb: 0x4a5120, the friends and letters icons of the old
    // UI (CL9 milestone 10), with the window list (the projection's copy in an intermediate drawing).
    Messenger::DrawTheFriendBarIcons(
        frame.context.oldWindows != nullptr ? *frame.context.oldWindows : frame.runtime.oldWindows, sprites);
    // 0x5a9de6: 0x40a5b0, the commands button.
    if (MouseIn(frame, 6, 435, 59, 454))
    {
        if (auto help = HelpWithText(frame, 6, 420, CommandsHelpText, "0x40a5f6"); !help.HasValue())
            return help;
    }
    if (frame.runtime.panels.delegated.commands != 0)
        Quad(sprites, 0x65c, 6.0f, 435.0f, 53.0f, 19.0f, 0.0f, 0.0f, 0.828125f, 0.59375f);
    if (MouseIn(frame, 582, 459, 634, 477))
    {
        if (auto help = HelpWithText(frame, 582, 444, GuildHelpText, "0x5a9cb2"); !help.HasValue())
            return help;
    }
    if (panels.IsOpen(Ui::GuildPanel))
        Quad(sprites, 0xfa, 582.0f, 459.0f, 52.0f, 18.0f, 0.0f, 0.0f, 0.8125f, 0.5625f);
    {
        const auto own = OwnAccess(frame);
        if (frame.own.byte6a != 0)
        {
            sprites.SetColor(SkillColor);
            if (auto icon = DrawTheSkillIcon(frame, frame.hero.skillIndex, 304, 444, 32, 36); !icon.HasValue())
                return icon;
            if (frame.runtime.ui.skillListOpen != 0)
            {
                if (auto list = DrawTheSkillList(frame); !list.HasValue())
                    return list;
            }
        }
    }
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(Color{0.6f, 0.6f, 0.6f, 1.0f});
    std::uint8_t cell = 0;
    {
        const Models::LogicalAccess access(frame.context.cellXDepth, frame.context.random,
                                           Models::SmallLogicalCloseRandoms);
        cell = frame.hero.cellX;
    }
    Ui::DrawNumber(sprites, 24.0f, 462.0f, cell, 8.0f, 9.0f);
    {
        const Models::LogicalAccess access(frame.context.cellYDepth, frame.context.random,
                                           Models::SmallLogicalCloseRandoms);
        cell = frame.hero.cellY;
    }
    Ui::DrawNumber(sprites, 48.0f, 462.0f, cell, 8.0f, 9.0f);
    sprites.SetColor(NumberColor);
    {
        const auto own = OwnAccess(frame);
        Ui::DrawNumber(sprites, 100.0f, 467.0f, frame.own.life, 9.0f, 10.0f);
        Ui::DrawNumber(sprites, 540.0f, 467.0f, frame.own.mana, 9.0f, 10.0f);
    }
    const std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells(frame.context.world.Inventory().cells);
    for (int slot = 0; slot < 3; ++slot)
    {
        auto count = Items::CountQuickSlotItems(frame.context.hud.quickSlots, cells, slot);
        if (!count.HasValue())
            return Core::Unexpected(count.Error());
        if (count.Value() > 0)
            Ui::DrawNumber(sprites, F32(static_cast<double>(slot) * 31.0 + 226.0), 447.0f, count.Value(), 9.0f, 10.0f);
    }
    // 0x5aa980: 0x621a90, the pops of the 2D matrices, which MuRet sets up per pass.
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 4. Who holds the castle switches (0x5a6fc0), with the object at 0x5767924 that the B2 20 builds.

// 0x5a71af..0x5a71d9: the hero's zone, X in 150..200 and Y in 180..230 (JL and JG, edges inside).
constexpr std::uint8_t SwitchZoneLeft = 0x96;
constexpr std::uint8_t SwitchZoneRight = 0xc8;
constexpr std::uint8_t SwitchZoneTop = 0xb4;
constexpr std::uint8_t SwitchZoneBottom = 0xe6;
// 0x5a71eb and 0x5a72f3: the first line at y 150 and the next one 15 lower, at x 0.
constexpr int SwitchLinesTop = 0x96;
constexpr int SwitchLineStep = 0xf;
constexpr std::size_t SwitchHolderText = 1981; // 0x774e358
// 0x5a7207..0x5a7295: each half's side (+7) against the bits of the hero's +0x78.
constexpr std::array<std::pair<std::uint32_t, std::uint8_t>, 3> SwitchSides{
    {{std::uint32_t{0x80000}, std::uint8_t{2}},
     {std::uint32_t{0x400000}, std::uint8_t{3}},
     {std::uint32_t{0x800000}, std::uint8_t{4}}}};

// A sprintf %s on the object from `at`: up to the first 0. With none before the end of the 0x36 bytes, the original
// keeps reading the heap that follows (Q-C1403): std::nullopt.
std::optional<std::string> HolderText(const std::array<std::uint8_t, CastleSiege::SwitchHoldersSize>& holders,
                                      std::size_t at)
{
    std::string text;
    for (std::size_t i = at; i < holders.size(); ++i)
    {
        if (holders[i] == 0)
            return text;
        text.push_back(static_cast<char>(holders[i]));
    }
    return std::nullopt;
}

Core::Expected<void, std::string> DrawTheSwitchHolders(Frame& frame)
{
    auto& storage = frame.runtime.castleSiege.castleSiegeInfo;
    if (!storage)
        return {};
    // 0x5a6fdb..0x5a71aa: the hero's X (+0x41c) and Y (+0x420), each with its access to the protection table.
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    {
        const Models::LogicalAccess access(frame.context.cellXDepth, frame.context.random,
                                           Models::SmallLogicalCloseRandoms);
        x = frame.hero.cellX;
    }
    {
        const Models::LogicalAccess access(frame.context.cellYDepth, frame.context.random,
                                           Models::SmallLogicalCloseRandoms);
        y = frame.hero.cellY;
    }
    if (x < SwitchZoneLeft || x > SwitchZoneRight || y < SwitchZoneTop || y > SwitchZoneBottom)
    {
        // 0x5a730d: 0x4e05f0 frees the object. An intermediate MuRet drawing does not free it: the original's next
        // frame does it with its accesses.
        if (!frame.context.projecting)
            storage.reset();
        return {};
    }
    const auto& holders = *storage;
    for (std::size_t half = 0; half < 2; ++half)
    {
        const std::size_t base = half * CastleSiege::SwitchHolderHalf;
        // 0x5a71f0: JBE, the unsigned byte at 0.
        if (holders[base + CastleSiege::HolderShownAt] == 0)
            continue;
        const std::uint8_t side = holders[base + CastleSiege::HolderSideAt];
        bool withName = false;
        for (const auto& [flag, sideOfFlag] : SwitchSides)
            withName = withName || ((frame.hero.flags78 & flag) == flag && side == sideOfFlag);
        auto title = TextOf(frame, SwitchHolderText, "0x5a7227");
        if (!title.HasValue())
            return Core::Unexpected(title.Error());
        const auto guild = HolderText(holders, base + CastleSiege::HolderGuildAt);
        const auto name =
            withName ? HolderText(holders, base + CastleSiege::HolderNameAt) : std::optional<std::string>{};
        if (!guild || (withName && !name))
            return Core::Unexpected(std::format("0x5a6fc0: el %s de la mitad {} del objeto de 0x5767924 no tiene un 0 "
                                                "antes del final de sus 0x36 bytes y sigue leyendo el heap (Q-C1403)",
                                                half));
        const auto number = static_cast<std::int32_t>(half + 1);
        // 0x6c9288, 0x6c9298 and 0x6c92a8 are the same text; 0x6c92b8, without the name.
        const std::string line = withName ? Crt::Sprintf("%s%d / %s / %s", title.Value(), number,
                                                         std::string_view(*guild), std::string_view(*name))
                                          : Crt::Sprintf("%s%d / %s", title.Value(), number, std::string_view(*guild));
        DrawHelp(frame, 0, SwitchLinesTop + (static_cast<int>(half) * SwitchLineStep), line);
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 31. The siege panel (0x4125b0).

} // namespace

Core::Expected<void, std::string> DrawTheHud(Ui::GameHudContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    GamePanels panels(world, context.random);
    Frame frame{.context = context,
                .runtime = runtime,
                .own = world.OwnCharacter(),
                .hero = world.Hero(),
                .panels = panels.Registry()};
    Kernel::HudState& hud = context.hud;
    hud.textColors.text = 0xffffffffu;
    // 1. 0x5a7520.
    DrawTheSands(frame);
    // 2. 0x5a7640..0x5a7cbd: the war and Battle Soccer scoreboards.
    if (auto war = Guild::DrawTheWarScore(context, frame.panels); !war.HasValue())
        return war;
    // 3. 0x5a89b0: the party's bars.
    if (auto life = Party::DrawThePartyLife(context); !life.HasValue())
        return life;
    // 4. 0x5a6fc0: who holds the switches.
    if (auto holders = DrawTheSwitchHolders(frame); !holders.HasValue())
        return holders;
    // 5. 0x42a4d0: the health bars of the 0x7239a8 queue (Hud/HealthBars). An intermediate drawing empties a copy.
    {
        std::deque<Kernel::QueuedHealthBar> scratch;
        if (context.projecting)
            scratch = runtime.healthBars;
        if (auto bars = DrawTheHealthBars(context.projecting ? scratch : runtime.healthBars, context.projection,
                                          context.sprites);
            !bars.HasValue())
            return bars;
    }
    // 6. 0x42a660.
    if (auto icons = DrawTheStatusIcons(frame); !icons.HasValue())
        return icons;
    // 7..9. 0x5ab810, 0x5ab210 and 0x5aac20.
    auto arrows = DrawTheArrows(frame);
    if (!arrows.HasValue())
        return Core::Unexpected(arrows.Error());
    auto top = DrawTheHelper(frame, arrows.Value());
    if (!top.HasValue())
        return Core::Unexpected(top.Error());
    if (auto durability = DrawTheDurability(frame, top.Value()); !durability.HasValue())
        return durability;
    // 10. The macro.
    DrawTheMacroTime(frame);
    // 11.
    context.sprites.SetColor(White);
    if (auto event = DrawTheEventObject(frame); !event.HasValue())
        return event;
    // 12. 0x5a9050: the bubbles and the names over the heads (0x77e1be0, Chat/ChatBubbleDraw). It draws no rand() and
    // writes in each entry where it ended up (+0x278..+0x284), which the right click of 0x557650 reads.
    Chat::ChatBubbleDrawContext bubbles{.world = world,
                                        .projection = context.projection,
                                        .sprites = context.sprites,
                                        .text = context.text,
                                        .dcFont = hud.dcFont,
                                        .textColors = hud.textColors,
                                        .clampBoxes = hud.durabilityWarning,
                                        .boxWidth = hud.arrowsTextWidth,
                                        .boxHeight = hud.arrowsTextHeight,
                                        .mouse = context.mouse,
                                        .blink = hud.messageFrame.areaBlink,
                                        .projecting = context.projecting,
                                        .texts = context.texts};
    if (auto drawn = Chat::DrawTheChatBubbles(bubbles); !drawn.HasValue())
        return drawn;
    // 13. 0x451d10 and 0x451c20: the signs of the singleton at 0x7245c8 (PersonalShop/ShopSigns). With none (+0xc at
    // 0), nothing; with some, 0x5bb8d0 with its rand(), their place (0x451d60), the spread so they do not overlap
    // (0x451e40), the count of +0x10 and each sign (0x452a10).
    PersonalShop::ShopSignDrawContext signs{.world = world,
                                            .panels = frame.panels,
                                            .projection = context.projection,
                                            .sprites = context.sprites,
                                            .text = context.text,
                                            .dcFont = hud.dcFont,
                                            .texts = context.texts,
                                            .projecting = context.projecting};
    if (auto drawn = PersonalShop::DrawTheShopSigns(signs); !drawn.HasValue())
        return drawn;
    // 14. 0x5a6cc0: the duel score, with 0x78abad8, which the AA turns on (CL9 milestone 7).
    Social::DrawTheDuelScore(context, frame.panels);
    // 15. 0x46b090: the Slide.bmd banner ([0x5763528]).
    if (auto slides = Ui::DrawTheSlides(hud.slides, Ui::SlideDrawContext{.sprites = context.sprites,
                                                                         .text = context.text,
                                                                         .dcFont = hud.dcFont,
                                                                         .textColors = hud.textColors,
                                                                         .mouseX = context.mouse.x,
                                                                         .mouseY = context.mouse.y,
                                                                         .slidesOn = runtime.chat.slidesOn});
        !slides.HasValue())
        return slides;
    // 16. 0x413ca0.
    CastleSiege::DrawTheBattleClock(context);
    // 17. [[0x576350c]] + 0x14: the chat log (0x479080).
    Chat::DrawTheChatLog(
        runtime.chat.log, hud.chatFrame, runtime.ui.chatModes,
        Chat::ChatLogDrawContext{
            .sprites = context.sprites, .text = context.text, .dcFont = hud.dcFont, .textColors = hud.textColors},
        Chat::ChatLogDrawView{.mouseX = context.mouse.x,
                              .mouseY = context.mouse.y,
                              .leftDown = context.leftDown,
                              .chatOpen = runtime.ui.chatOpen,
                              .crywolfStage = runtime.crywolf.stage,
                              .texts = context.texts});
    // 18. 0x5f7440.
    if (auto windows = DrawTheWindows(frame); !windows.HasValue())
        return windows;
    // 19. 0x65dd70: the banner with the map's name ([0x5763534]).
    DrawTheMapBanner(frame);
    // 20. 0x5a9290.
    DrawTheBottomBar(frame);
    // 21. 0x4365c0: the Crywolf interface (Events/Crywolf/CrywolfInterfaceDrawing), which always starts with
    // 0x620a00(1); the trace shows it before the experience.
    if (auto crywolf = Events::Crywolf::DrawTheCrywolfInterface(context); !crywolf.HasValue())
        return crywolf;
    // 22. 0x5aa9a0.
    DrawTheChatInput(frame);
    // 23. and 24. 0x5abcd0 and 0x5a8cc0.
    if (auto experience = DrawTheExperience(frame); !experience.HasValue())
        return experience;
    if (auto ag = DrawTheAg(frame); !ag.HasValue())
        return ag;
    // 25. 0x652c90.
    if (auto siege = CastleSiege::DrawTheBattleSkill(context); !siege.HasValue())
        return siege;
    // 26. 0x436240: the text color and the window of the Crywolf contract (Events/Crywolf/CrywolfInterfaceDrawing).
    if (auto contract = Events::Crywolf::DrawTheCrywolfContract(context); !contract.HasValue())
        return contract;
    // 27. and 28. 0x5ac120 and 0x5f7840.
    if (auto ui3D = DrawTheUi3D(frame); !ui3D.HasValue())
        return ui3D;
    if (auto grids = DrawTheGrids(frame); !grids.HasValue())
        return grids;
    // 29. 0x5a9630.
    if (auto bar = DrawTheMainBar(frame); !bar.HasValue())
        return bar;
    // 30. 0x5e9b10, which starts with the keypad's mode (0x78abb20).
    if (auto keypad = Warehouse::DrawTheKeypad(frame.context); !keypad.HasValue())
        return keypad;
    // 31. With no window, 0x4125b0.
    return CastleSiege::DrawTheBattlePanel(context);
}

Core::Expected<void, std::string> DrawTheOverlays(Ui::GameHudContext& context)
{
    Kernel::WorldState& world = context.world;
    const std::int32_t& mode = world.Runtime().panels.overlayMode;
    // 0x5b0559..0x5b06ac: the first F1 help, and the mode again.
    if (mode == Ui::FirstHelpMode)
    {
        if (auto drawn = Tooltips::DrawTheFunctionHelp(context, Ui::FirstHelpMode); !drawn.HasValue())
            return drawn;
    }
    if (mode == Ui::SecondHelpMode)
        return Tooltips::DrawTheFunctionHelp(context, Ui::SecondHelpMode);
    if (mode == Ui::ItemHelpMode)
        return Tooltips::DrawTheItemHelp(context);
    // 0x5b10cf..0x5b10e0: IsOpen(2), the /move window.
    GamePanels panels(world, context.random);
    if (panels.Registry().IsOpen(Ui::MovePanel))
        return DrawTheMoveWindow(context);
    return {};
}

Core::Expected<void, std::string> DrawTheGameCursor(Ui::GameHudContext& context)
{
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    // 0x5ac2ee..0x5ac33a: the talk cursor's frame, ftol(T · 0.01) % 6, in quarters of the texture.
    const std::int32_t frameIndex = Truncate(static_cast<double>(context.worldClock) * static_cast<double>(0.01f)) % 6;
    const float u = frameIndex == 1 || frameIndex == 3 || frameIndex == 5 ? 0.5f : 0.0f;
    const float v = frameIndex == 2 || frameIndex == 3 || frameIndex == 4 ? 0.5f : 0.0f;
    const float x = F32(static_cast<double>(context.mouse.x) - 2.0);
    const float y = F32(static_cast<double>(context.mouse.y) - 2.0);
    const auto cursor = [&](std::uint16_t slot) { Quad(sprites, slot, x, y, 24.0f, 24.0f, 0.0f, 0.0f, 1.0f, 1.0f); };
    if (runtime.ui.field78abb20 != 0 || runtime.dialogs.current != 0)
    {
        cursor(2);
        return {};
    }
    const std::array<std::int32_t, 5>& targets = runtime.selectedTargets;
    if (targets[0] != -1)
    {
        cursor(5);
        return {};
    }
    if (targets[1] != -1)
    {
        Quad(sprites, 6, x, y, 24.0f, 24.0f, u, v, 0.5f, 0.5f);
        return {};
    }
    if (targets[3] != -1)
        return Core::Unexpected(
            std::string("0x5ac3d8: el cursor de los objetos lee su tabla (0x7d34820), de la CL9.1"));
    GamePanels panels(world, context.random);
    const Kernel::ActorState& hero = world.Hero();
    if (hero.safeZone == 0 && targets[2] != -1)
    {
        // 0x5ac53c..0x5ac60b (CL9 milestone 3): attackable (0x558bb0) and the mouse outside the UI (0x775857c), the
        // attack one (4, or 0x68d in 30); otherwise, the commands one with IsOpen(0x200) or the normal one (0x5ac818).
        const auto attackable = Combat::IsAttackable(world, context.controlDown);
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        if (attackable.Value() && runtime.ui.mouseOnUi == 0)
        {
            cursor(runtime.map == CastleSiegeMap ? 0x68d : 4);
            return {};
        }
        cursor(panels.Registry().IsOpen(Ui::CommandsPanel) ? 0x65a : 2);
        return {};
    }
    if (panels.Registry().IsOpen(Ui::CommandsPanel))
    {
        cursor(0x65a);
        return {};
    }
    const std::int32_t repairMode = runtime.panels.repairMode;
    if (repairMode == 1)
    {
        cursor(7);
        return {};
    }
    if (repairMode == 2)
    {
        // 0x5ac6d4: fsin(T · 0.02) against 0; the sign is the same on any CPU.
        const double turn = Math::RoundedSin(static_cast<double>(context.worldClock) * static_cast<double>(0.02f));
        if (turn <= 0.0)
            sprites.RotatedQuad(Render::TextureSlot{7}, F32(static_cast<double>(context.mouse.x) + 5.0),
                                F32(static_cast<double>(context.mouse.y) + 18.0), 24.0f, 24.0f, 45.0f);
        else
            sprites.RotatedQuad(Render::TextureSlot{7}, F32(static_cast<double>(context.mouse.x) + 10.0),
                                F32(static_cast<double>(context.mouse.y) + 10.0), 24.0f, 24.0f, 0.0f);
        return {};
    }
    if (context.leftDown)
    {
        cursor(context.hud.pushCursor == 0 ? 3 : 0xa);
        return {};
    }
    cursor(2);
    return {};
}
} // namespace Client::Hud
