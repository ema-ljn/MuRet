#include "Events/EventNpcWindows.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Chat/GameChat.h"
#include "Ui/GameHudContext.h"
#include "Ui/GameUiContext.h"
#include "Hud/GameWindowDrawing.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Crt/OriginalFormat.h"
#include "Kernel/CharacterClass.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Ui/GameUiState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Events
{
namespace
{
using Result = Core::Expected<void, std::string>;
using Render::Text::FontKind;

// The corner of the right-hand NPC windows (0x78aba28 and 0x78aba2c).
constexpr std::int32_t WindowLeft = 0x104;
constexpr std::int32_t WindowTop = 0;
// The buttons: Interface\... 0xf0, 140 wide (0x6b8ca8) from x + 25 (0x6b8808), with du = 0.83203125.
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr double ButtonLeft = 25.0;
constexpr float ButtonWidth = 140.0f;
constexpr float ButtonDu = 0.83203125f;
// Devil Square: from y + 187 (0x6ba638), in steps of 40 (0x6b8bf4), 35 high (0x6b8974); six.
constexpr double SquareTop = 187.0;
constexpr double SquareStep = 40.0;
constexpr float SquareHeight = 35.0f;
constexpr std::size_t SquareCount = 6;
// Blood Castle: from y + 170 (0x6b899c), in steps of 35, 30 high (0x41f00000 and 0x6b8858); seven.
constexpr double BridgeTop = 170.0;
constexpr double BridgeStep = 35.0;
constexpr float BridgeHeight = 30.0f;
constexpr std::size_t BridgeCount = 7;
// 0x5e598c and 0x5e6082: the step checks x between 285 (0x6ba458) and 425 (0x6ba634), without the corner.
constexpr double StepLeft = 285.0;
constexpr double StepRight = 425.0;

// 0x6ba4c8 and 0x6ba4f8: {minimum, maximum} of the six squares, and those of the Magic Gladiator and the Dark Lord.
using LevelRange = std::array<std::int32_t, 2>;
constexpr std::array<LevelRange, SquareCount> SquareLevels{
    {{15, 130}, {131, 180}, {181, 230}, {231, 400}, {281, 330}, {331, 400}}};
constexpr std::array<LevelRange, SquareCount> SquareLevelsOfTheSecondClasses{
    {{15, 110}, {111, 160}, {161, 210}, {211, 400}, {261, 310}, {311, 400}}};
// 0x6ba528: the seven bridges and, seven later, those of the Magic Gladiator and the Dark Lord.
constexpr std::array<LevelRange, BridgeCount * 2> BridgeLevels{{{15, 80},
                                                                {81, 130},
                                                                {131, 180},
                                                                {181, 230},
                                                                {231, 280},
                                                                {281, 330},
                                                                {331, 400},
                                                                {10, 60},
                                                                {61, 110},
                                                                {111, 160},
                                                                {161, 210},
                                                                {211, 260},
                                                                {261, 310},
                                                                {311, 400}}};
// The class & 7 of the Magic Gladiator and the Dark Lord.
// 0x5f5c50 and 0x5f63a9: with a maximum over 10000, the text without a maximum.
constexpr std::int32_t OpenMaximum = 0x2710;

constexpr std::size_t SquareText = 645;
constexpr std::size_t OpenSquareText = 646;
constexpr std::size_t DevilSquareTitle = 39;
constexpr std::size_t FirstSquareStory = 670;
constexpr std::size_t BridgeText = 847;
constexpr std::size_t OpenBridgeText = 848;
constexpr std::size_t BloodCastleTitle = 846;
constexpr std::size_t BloodCastleStory = 832;
// 0x5e5ab2, 0x5e5ae5 and 0x5e5b1d: too high, too low and no invitation; 0x5e6138: no cloak.
constexpr std::size_t TooHighText = 686;
constexpr std::size_t TooLowText = 687;
constexpr std::size_t NoInvitationText = 677;
constexpr std::size_t NoCloakText = 854;
// [ebp-0x68] and [ebp-0xa8]: 64 bytes for the button's line.
constexpr std::size_t RowBuffer = 0x40;
// 0x5f64d2: 832 in at most 7 lines of under 26 bytes.
constexpr std::size_t StoryLines = 7;
constexpr std::size_t StoryLineBytes = 0x1a;

constexpr Render::Text::TextColors DevilSquareColors{.text = 0xc41432ffu, .background = 0x101000u};
constexpr Render::Text::TextColors BloodCastleColors{.text = 0xdcffdcbeu, .background = 0x101000u};
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr Render::Text::TextColors WhiteText{.text = 0xffffffffu, .background = 0};

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Render::Color SquareColor{0.9f, 0.8f, 0.7f, 1.0f};
constexpr Render::Color BridgeColor{0.7f, 0.7f, 0.9f, 1.0f};
constexpr Render::Color ClosedBridgeColor{0.4f, 0.4f, 0.5f, 1.0f};

// The 0x554180 boxes in 640 × 480: the Devil Square lines (0x74), the titles and the Blood Castle lines (0x78) and the
// bottom texts (0x82).
constexpr std::uint32_t SquareBox = 0x74;
constexpr std::uint32_t TitleBox = 0x78;
constexpr std::uint32_t StoryBox = 0x82;

// Devil Square's invitation (14/19) and Blood Castle's cloak (13/18).
constexpr std::uint16_t DevilsInvitation = 0x1c13;
constexpr std::uint16_t CloakOfInvisibility = 0x1a12;
// What 0x5e5c8f and 0x5e62d9 add to the slot's place.
constexpr std::int32_t SquarePlaceBias = 0x18;
constexpr std::int32_t BridgePlaceBias = 0xc;
// 0x5e611a: bridge 7 also accepts the level 6 cloak.
constexpr std::int32_t LastBridge = 7;
constexpr std::int32_t LastBridgeSecondCloak = 6;

using Math::F32;
using Math::Truncate;

bool SecondClass(std::uint8_t characterClass)
{
    const Kernel::CharacterClass kind = Kernel::BaseClassOf(characterClass);
    return kind == Kernel::CharacterClass::MagicGladiator || kind == Kernel::CharacterClass::DarkLord;
}

// left ≤ mouse < left + width and top ≤ mouse < top + height in the x87, without an item on the cursor (0x7879fbc).
bool Hovered(const Ui::GameHudContext& context, float left, float top, float height)
{
    const auto x = static_cast<double>(context.mouse.x);
    const auto y = static_cast<double>(context.mouse.y);
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(ButtonWidth) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(height) &&
           context.world.Runtime().cursorItemCount == 0;
}

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// sprintf (0x69fd9b) of a Text.bmd text into the 64 stack bytes.
Core::Expected<std::string, std::string> RowText(const Ui::GameHudContext& context, std::size_t index,
                                                 std::span<const Crt::FormatArgument> arguments, std::string_view where)
{
    auto format = TextOf(context.texts, index, where);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    auto text = Crt::FormatOriginal(Crt::FormatFunction::Sprintf, format.Value(), arguments, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (text.Value().size() >= RowBuffer)
        return Core::Unexpected(std::format("{}: \"{}\" ocupa {} bytes con su '\\0' y el buffer de la pila tiene {}",
                                            where, text.Value(), text.Value().size() + 1, RowBuffer));
    return text;
}

// A button's line: 646 (or 848) without the maximum if it is over 10000, or 645 (847) with both.
Core::Expected<std::string, std::string> ButtonText(const Ui::GameHudContext& context, std::size_t text,
                                                    std::size_t openText, std::int32_t number, const LevelRange& levels,
                                                    std::string_view where)
{
    if (levels[1] > OpenMaximum)
    {
        const std::array<Crt::FormatArgument, 2> arguments{number, levels[0]};
        return RowText(context, openText, arguments, where);
    }
    const std::array<Crt::FormatArgument, 3> arguments{number, levels[0], levels[1]};
    return RowText(context, text, arguments, where);
}

// 0x5f5cf9..0x5f5d52 and 0x5f6444..0x5f649d: glColor3f(1, 1, 1), the title at (x + 0x23, y + 0xc) in 0xffdcdcdc over
// 0xff141414, the normal font, white text over 0 and the normal blend.
Result DrawTheTitle(Ui::GameHudContext& context, std::size_t index, std::string_view where)
{
    context.sprites.SetColor(White);
    context.hud.textColors = TitleColors;
    auto title = TextOf(context.texts, index, where);
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    Ui::WindowDrawing::DrawInBox(context, WindowLeft + 0x23, WindowTop + 0xc, title.Value(), TitleBox);
    Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = WhiteText;
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    return {};
}

// 0x5f5916..0x5f5de6.
Result DrawTheDevilSquareWindow(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const bool write = !context.projecting;
    if (write)
    {
        runtime.ui.npcWindowX = WindowLeft;
        runtime.ui.npcWindowY = WindowTop;
    }
    Render::SpriteBatch& sprites = context.sprites;
    Ui::WindowDrawing::DrawTheWindowBack(sprites, WindowLeft, WindowTop);
    context.hud.textColors = DevilSquareColors;
    const float left = F32(static_cast<double>(WindowLeft) + ButtonLeft);
    Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
    float top = F32(static_cast<double>(WindowTop) + SquareTop);
    for (std::size_t square = 0; square < SquareCount; ++square)
    {
        sprites.SetColor(Hovered(context, left, top, SquareHeight) ? White : SquareColor);
        Ui::WindowDrawing::Quad(sprites, ButtonSlot, left, top, ButtonWidth, SquareHeight, ButtonDu, 1.0f);
        // 0x5f5a3f..0x5f5b2a: the table's levels and, with the own record open, those of class 3 or 4.
        LevelRange levels = SquareLevels[square];
        {
            const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
            if (SecondClass(context.world.OwnCharacter().characterClass))
                levels = SquareLevelsOfTheSecondClasses[square];
        }
        if (write)
            runtime.events.devilSquareLevels[square] = levels;
        auto text =
            ButtonText(context, SquareText, OpenSquareText, static_cast<std::int32_t>(square) + 1, levels, "0x5f5c8a");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::WindowDrawing::DrawInBox(context, Truncate(left) + 0xc, Truncate(top) + 0xc, text.Value(), SquareBox);
        top = F32(static_cast<double>(top) + SquareStep);
    }
    if (auto title = DrawTheTitle(context, DevilSquareTitle, "0x5f5d52"); !title.HasValue())
        return title;
    // 0x5f5d8a..0x5f5dd9: 670 to 675 at (x + 0x1e, y + 0x41), in steps of 20.
    for (std::size_t line = 0; line < SquareCount; ++line)
    {
        auto story = TextOf(context.texts, FirstSquareStory + line, "0x5f5dc2");
        if (!story.HasValue())
            return Core::Unexpected(std::move(story).Error());
        Ui::WindowDrawing::DrawInBox(context, WindowLeft + 0x1e,
                                      WindowTop + 0x41 + static_cast<std::int32_t>(line) * 0x14, story.Value(),
                                      StoryBox);
    }
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

// 0x5f5e01..0x5f653f.
Result DrawTheBloodCastleWindow(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!context.projecting)
    {
        runtime.ui.npcWindowX = WindowLeft;
        runtime.ui.npcWindowY = WindowTop;
    }
    Render::SpriteBatch& sprites = context.sprites;
    Ui::WindowDrawing::DrawTheWindowBack(sprites, WindowLeft, WindowTop);
    context.hud.textColors = BloodCastleColors;
    const float left = F32(static_cast<double>(WindowLeft) + ButtonLeft);
    float top = F32(static_cast<double>(WindowTop) + BridgeTop);
    Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
    for (std::size_t bridge = 0; bridge < BridgeCount; ++bridge)
    {
        // 0x5f5e68..0x5f5e98: the table by the hero's class (+0x1d4), and the own record's level (+0xe).
        const Kernel::ActorState& hero = context.world.Hero();
        const std::size_t row = bridge + (SecondClass(hero.characterClass) ? BridgeCount : 0);
        const LevelRange& levels = BridgeLevels[row];
        bool open = true;
        {
            const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
            const std::int32_t level = context.world.OwnCharacter().level;
            if (level > levels[1] || level < levels[0])
                open = false;
        }
        const Render::Color color = !open                                       ? ClosedBridgeColor
                                    : Hovered(context, left, top, BridgeHeight) ? White
                                                                                : BridgeColor;
        sprites.SetColor(color);
        Ui::WindowDrawing::Quad(sprites, ButtonSlot, left, top, ButtonWidth, BridgeHeight, ButtonDu, 1.0f);
        auto text =
            ButtonText(context, BridgeText, OpenBridgeText, static_cast<std::int32_t>(bridge) + 1, levels, "0x5f63e5");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::WindowDrawing::DrawInBox(context, Truncate(left) + 0xa, Truncate(top) + 0xc, text.Value(), TitleBox);
        top = F32(static_cast<double>(top) + BridgeStep);
    }
    if (auto title = DrawTheTitle(context, BloodCastleTitle, "0x5f649d"); !title.HasValue())
        return title;
    auto story = TextOf(context.texts, BloodCastleStory, "0x5f64e2");
    if (!story.HasValue())
        return Core::Unexpected(std::move(story).Error());
    const std::vector<std::string> lines = Ui::SplitIntoLines(story.Value(), StoryLines, StoryLineBytes);
    for (std::size_t line = 0; line < lines.size(); ++line)
        Ui::WindowDrawing::DrawInBox(context, WindowLeft + 0x1e,
                                      WindowTop + 0x40 + static_cast<std::int32_t>(line) * 0x14, lines[line], StoryBox);
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

// 0x632e30 with a Text.bmd text.
Result MessageBoxOf(const Ui::GameUiContext& context, std::size_t index, std::string_view where)
{
    auto text = TextOf(context.texts, index, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, text.Value());
    return {};
}

Result Send(const Ui::GameUiContext& context, Protocol::Outgoing packet, std::string_view where)
{
    if (context.chat == nullptr)
        return Core::Unexpected(std::format("{}: el send() sin el contexto de la sesión", where));
    if (context.chat->send)
        context.chat->send(std::move(packet));
    return {};
}

bool InRow(const Ui::GameUiContext& context, double top, float height)
{
    const auto x = static_cast<double>(static_cast<float>(context.mouseX));
    const auto y = static_cast<double>(context.mouseY);
    return StepLeft <= x && x < StepRight && top <= y && y < top + static_cast<double>(height);
}

// 0x5e5978..0x5e5f2c.
Result StepTheDevilSquareWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.mouse.pendingClick)
        return {};
    std::optional<std::size_t> chosen;
    double top = static_cast<double>(F32(SquareTop));
    for (std::size_t square = 0; square < SquareCount && !chosen; ++square)
    {
        if (InRow(context, top, SquareHeight))
            chosen = square;
        top += SquareStep;
    }
    if (!chosen)
        return {};
    runtime.mouse.pendingClick = false;
    if (runtime.cursorItemCount != 0)
        return {};
    const std::size_t square = *chosen;
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const std::int32_t level = context.world.OwnCharacter().level;
    const LevelRange& levels = runtime.events.devilSquareLevels[square];
    // 0x5e5a98..0x5e5af2: 0x65cae0(0x4000000, 0), 0x5be150 (without an item on the cursor, already checked, it does
    // nothing) and the notice.
    if (level > levels[1] || level < levels[0])
    {
        if (auto closed = panels.Close(Ui::InventoryPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return MessageBoxOf(context, level > levels[1] ? TooHighText : TooLowText,
                            level > levels[1] ? "0x5e5ab7" : "0x5e5aea");
    }
    std::int32_t place = FindInventoryItem(context.world.Inventory(), DevilsInvitation, 0);
    if (place == -1)
        place = FindInventoryItem(context.world.Inventory(), DevilsInvitation, static_cast<std::int32_t>(square) + 1);
    if (place == -1)
        return MessageBoxOf(context, NoInvitationText, "0x5e5b22");
    return Send(context,
                Protocol::Events::WriteDevilSquareEntry(static_cast<std::uint8_t>(square),
                                                        static_cast<std::uint8_t>(place + SquarePlaceBias)),
                "0x5e5d7e");
}

// 0x5e5f2e..0x5e650d.
Result StepTheBloodCastleWindow(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.mouse.pendingClick)
        return {};
    std::optional<std::int32_t> chosen;
    float top = F32(BridgeTop);
    for (std::size_t bridge = 0; bridge < BridgeCount; ++bridge)
    {
        // 0x5e5f61..0x5e6080: each bridge opens and closes the own record for its level before checking the mouse.
        const Kernel::ActorState& hero = context.world.Hero();
        const LevelRange& levels = BridgeLevels[bridge + (SecondClass(hero.characterClass) ? BridgeCount : 0)];
        bool open = true;
        {
            const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
            const std::int32_t level = context.world.OwnCharacter().level;
            if (level > levels[1] || level < levels[0])
                open = false;
        }
        if (open && InRow(context, static_cast<double>(top), BridgeHeight))
        {
            chosen = static_cast<std::int32_t>(bridge) + 1;
            break;
        }
        top = F32(static_cast<double>(top) + BridgeStep);
    }
    if (!chosen)
        return {};
    runtime.mouse.pendingClick = false;
    if (runtime.cursorItemCount != 0)
        return {};
    std::int32_t place = FindInventoryItem(context.world.Inventory(), CloakOfInvisibility, *chosen);
    if (*chosen == LastBridge && place == -1)
        place = FindInventoryItem(context.world.Inventory(), CloakOfInvisibility, LastBridgeSecondCloak);
    if (place == -1)
        return MessageBoxOf(context, NoCloakText, "0x5e613d");
    return Send(context,
                Protocol::Events::WriteBloodCastleEntry(static_cast<std::uint8_t>(*chosen),
                                                        static_cast<std::uint8_t>(place + BridgePlaceBias)),
                "0x5e62cb");
}
} // namespace

std::int32_t CountInventoryItems(const Kernel::OwnInventory& inventory, std::int32_t type, std::int32_t level)
{
    constexpr std::int32_t AnyItem = -1;
    std::int32_t count = 0;
    for (const Kernel::ItemState& item : inventory.cells)
    {
        if (static_cast<std::int32_t>(static_cast<std::int16_t>(item.id)) != type)
            continue;
        if (type != AnyItem && static_cast<std::int32_t>(item.occupied) <= 0)
            continue;
        if (level == AnyItem || static_cast<std::int32_t>((item.scalar1Low >> 3) & 0xfu) == level)
            ++count;
    }
    return count;
}

std::int32_t FindInventoryItem(const Kernel::OwnInventory& inventory, std::uint16_t type, std::int32_t level)
{
    constexpr std::int32_t Side = 8;
    for (std::int32_t column = Side - 1; column >= 0; --column)
    {
        for (std::int32_t row = Side - 1; row >= 0; --row)
        {
            const std::int32_t index = row * Side + column;
            const Kernel::ItemState& item = inventory.cells[static_cast<std::size_t>(index)];
            if (item.id != type || static_cast<std::int32_t>(item.occupied) <= 0)
                continue;
            if (level == -1 || static_cast<std::int32_t>((item.scalar1Low >> 3) & 0xfu) == level)
                return index;
        }
    }
    return -1;
}

Result DrawTheEventNpcWindow(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (panels.IsOpen(Ui::EventNpcPanel))
        return DrawTheDevilSquareWindow(context);
    if (panels.IsOpen(Ui::SecondEventNpcPanel))
        return DrawTheBloodCastleWindow(context);
    return {};
}

Result StepTheEventNpcWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (panels.IsOpen(Ui::EventNpcPanel))
    {
        runtime.ui.npcWindowX = WindowLeft;
        runtime.ui.npcWindowY = WindowTop;
        return StepTheDevilSquareWindow(context, panels);
    }
    if (!panels.IsOpen(Ui::SecondEventNpcPanel))
        return {};
    runtime.ui.npcWindowX = WindowLeft;
    runtime.ui.npcWindowY = WindowTop;
    return StepTheBloodCastleWindow(context);
}
} // namespace Client::Events
