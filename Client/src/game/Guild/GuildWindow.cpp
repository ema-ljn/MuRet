#include "Guild/GuildWindow.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Tables/TextTable.h"
#include "Math/X87.h"
#include "Protocol/Guild/Guild.h"
#include "Render/OriginalQuad.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Chat/GameMessages.h"
#include "Hud/GameUi.h"
#include "Hud/GameWindowDrawing.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Guild/GuildWindowTabs.h"
#include "Render/Frame/ITextDrawer.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/Dialogs.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelDialog.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"

#include <algorithm>
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

namespace Client::Guild
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;
using Ui::WindowDrawing::DrawText;
using Ui::WindowDrawing::SelectFont;

constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};
// 0x65202d and 0x6520fe: the tab that is not chosen, at 0.6.
constexpr Render::Color DimTab{.red = 0.6f, .green = 0.6f, .blue = 0.6f, .alpha = 1.0f};
// 0x651f22: glColor4ub(0x92, 0x90, 0x8d, 0xff), the lines under the tabs that are not chosen.
constexpr Render::Color TabLine{
    .red = 146.0f / 255.0f, .green = 144.0f / 255.0f, .blue = 141.0f / 255.0f, .alpha = 1.0f};

// 0x651840: the UI's mouse rectangle, 0xbe (+0x58) × 0x1b1, with mode 1.
constexpr std::int32_t MouseHeight = 0x1b1;
constexpr std::int32_t TopLeftRect = 1;
// 0x651895..0x6518e3: the close button, what it leaves in 0x6c8dcc and its sound (CL10).
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
constexpr Audio::SoundId ClickSound = Sound::ButtonClickSound;
// The hero's +0x368 without a guild (0x511290).
constexpr std::uint8_t NoGuildStatus = 0xff;
// The tabs: the step checks them every 0x34 from x + 0xf (0x651913..0x65199d) and the draw places them every 0x36.
constexpr std::int32_t TabCount = 3;
constexpr std::int32_t MembersTab = 1;
constexpr std::int32_t UnionTab = 2;
constexpr std::array<std::int32_t, TabCount> TabMouseLefts{0xf, 0x43, 0x77};
constexpr std::int32_t TabMouseTop = 0x4b;
constexpr std::int32_t TabMouseWidth = 0x34;
constexpr std::int32_t TabMouseHeight = 0x10;
constexpr std::int32_t TabLeft = 0xf;
constexpr std::int32_t TabStep = 0x36;
constexpr std::int32_t TabTop = 0x4b;
constexpr std::int32_t TabLineTop = 0x5b;
constexpr float TabLineWidth = 54.0f;
constexpr float TabLineHeight = 1.0f;
// 0x652092: Interface\..., 0x646, 52 × (16 + 2 the chosen one), du 0.75 and dv 0.9375, the chosen one 2 higher.
constexpr std::uint16_t TabSlot = 0x646;
constexpr float TabWidth = 52.0f;
constexpr float TabHeight = 16.0f;
constexpr float TabDu = 0.75f;
constexpr float TabDv = 0.9375f;
constexpr std::int32_t ChosenTabRise = 2;
// 0x6520c8: the tab's text at y + 4, the chosen one's one higher, centered in _ftol(rx · 52) (0x6ba060).
constexpr std::int32_t TabTextBelow = 4;
constexpr double TabTextBox = 52.0;
constexpr std::array<std::size_t, TabCount> TabTexts{946, 1330, 1352};
// 0x651cd2..0x651d5d and 0x651d73..0x651dd6: the title and the guild with the score, in bold on background 0.
constexpr std::size_t TitleText = 180;
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
constexpr std::int32_t ScoreTop = 0x35;
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0};
constexpr std::uint32_t ScoreColor = 0xff64ffc8u;
// 0x651dfa..0x651e4f: without a guild, texts 185, 186 and 187 in the normal font, every 0xf from (x + 0x14, y + 0x32).
constexpr std::array<std::size_t, 3> NoGuildTexts{185, 186, 187};
constexpr std::int32_t NoGuildLeft = 0x14;
constexpr std::int32_t NoGuildTop = 0x32;
constexpr std::int32_t NoGuildStep = 0xf;
constexpr std::uint32_t NoGuildColor = 0xffe6e6e6u;
// 0x651ee6: the close button's help.
constexpr std::size_t CloseHelpText = 220;
// 0x650350: PgUp and PgDn (0x553520) and the master (+0x368 at 0x80) with its two positions, 0x40 and 0x20.
constexpr std::uint8_t PageUpKey = 0x21;
constexpr std::uint8_t PageDownKey = 0x22;
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t AssistantStatus = 0x40;
constexpr std::uint8_t BattleMasterStatus = 0x20;
// 0x650594..0x650716: row 0 with the name (sprintf with 1367) and row 1 (strcpy of 1368 or 1369), 0x40 each, with
// Confirm and Cancel (4).
constexpr std::size_t ChosenNameText = 1367;
constexpr std::size_t DismissText = 1368;
constexpr std::size_t KickText = 1369;
// 0x46d3a0: the list's row, 0xd high from the row's y − 3, with width _ftol(width − thumb + 1).
constexpr std::int32_t MemberRowAbove = 3;
constexpr std::int32_t MemberRowHeight = 0xd;
// 0x46cab0: the scroll to the end.
constexpr std::int32_t ScrollToTheEnd = -10000;
// 0x64fc00: the union master (1270, OK), the dialog to disband the guild (1363 to 1366, 0x32 each, with Confirm) and
// the game's dialog 0x7d that asks for the number (0x64fdb0) with the Tab count (0x6c8e6c) at 1.
constexpr std::size_t UnionMasterText = 1270;
constexpr std::array<std::size_t, 4> DisbandTexts{1363, 1364, 1365, 1366};
// 0x64fce7..0x64fd8f: the four rows are contiguous 0x32-byte slots on the stack (ebp−0xfc, −0xca, −0x98 and −0x66).
constexpr std::size_t DisbandSlot = 0x32;
constexpr std::int32_t NumberDialog = 0x7d;
constexpr std::int32_t NumberDialogFields = 1;
// 0x650c10: the union master (1271), C1 07 E5 01 02 (break the union) with notice 1387 of type 1, and the dialog to
// kick a guild out of it (1423 with the name and 1369).
constexpr std::size_t UnionMasterLeaveText = 1271;
constexpr std::uint8_t UnionRelation = 1;
constexpr std::uint8_t BreakRequest = 2;
constexpr std::size_t UnionLeftText = 1387;
constexpr std::int32_t UnionLeftType = 1;
constexpr std::size_t UnionKickText = 1423;
// 0x64e600: the UI's mouse over 0xd5 × 0xfa from the corner, the two 0xaa × 0x14 boxes at x + 0x14 (y + 0x13 and
// y + 0x29) and the E1's type: 1 without a position, 2 with one.
constexpr std::int32_t PositionsMouseWidth = 0xd5;
constexpr std::int32_t PositionsMouseHeight = 0xfa;
constexpr std::int32_t PositionsChoiceLeft = 0x14;
constexpr std::int32_t AssistantChoiceTop = 0x13;
constexpr std::int32_t BattleMasterChoiceTop = 0x29;
constexpr std::int32_t PositionsChoiceWidth = 0xaa;
constexpr std::int32_t PositionsChoiceHeight = 0x14;
constexpr std::uint8_t AppointType = 1;
constexpr std::uint8_t ChangeType = 2;

using Math::F32;
using Math::Truncate;

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x5542f0(x, y, text, _ftol(rx · 52), 2, 0): the text centered in the box (0x467060 in mode 2), without measuring.
void DrawInTheTabBox(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    const auto rx = static_cast<double>(Render::ScaleX(1.0f, context.sprites.Screen()));
    const std::int32_t box = Truncate(rx * TabTextBox);
    if (text.empty() && box == 0)
        return;
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.hud.dcFont, text, box);
    context.text.DrawColumns(x, y, text,
                             Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                             -placed.textColumn, placed.width);
}

// 0x6519ad..0x651ba4: the first time the union tab is chosen, with a union (+4 of the hero's guild entry, without
// checking −1: Q-C1281), C1 03 E9 in the clear through the socket (0x6c82a0) and +0x84 at 1.
Core::Expected<void, std::string> AskForTheUnionList(const GuildWindowStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (guild.window.unionRequested != 0)
        return {};
    const auto index = static_cast<std::int16_t>(context.world.Hero().word1f2);
    const auto unionByte = UnionByteAt(guild, index);
    if (!unionByte)
        return Core::Unexpected(std::format("0x6519cc: el gremio {} del héroe cae fuera de la tabla", index));
    if (*unionByte == '\0')
        return {};
    if (!context.send)
        return Core::Unexpected(std::string("0x651ac8: el pedido de la alianza (E9) sin el cable"));
    context.send(Protocol::Guild::WriteUnionListRequest());
    guild.window.unionRequested = 1;
    return {};
}

// 0x46d3a0, the vtable + 0x88 of the member list (0x46c3d0 the notice one's and 0x46e1e0 the union one's, the same
// except for the element size and the union row's y, which is its + 0xa0): the mouse over the row with the click
// chooses it (scroll + row + 1, the + 0x58), flips +0 of its element and spends the click. Always 1.
template <typename Row>
std::int32_t HoverTheRow(const GuildWindowStepContext& context, Ui::OldList& list, std::vector<Row>& rows,
                         std::int32_t row, std::size_t at)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::size_t size = rows.size();
    const std::int32_t width =
        Truncate(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0);
    if (Ui::MouseInRect(context.mouseX, context.mouseY, list.base.x, Ui::ListRowY(list, size, row) - MemberRowAbove,
                        width, MemberRowHeight, TopLeftRect) &&
        runtime.mouse.pendingClick)
    {
        Ui::SelectInTheList(list, size, list.scroll + row + 1);
        Row& element = rows[at];
        element.toggled = (element.toggled + 1) % 2;
        runtime.mouse.pendingClick = false;
    }
    return 1;
}

// 0x650378, 0x64fc14 and 0x650c3c: 0x5762eb4 with the list's id, its vtable + 0x18 (0x45aa80(0): the queue, empty
// because nobody sends it messages, the mouse and 0x47b0f0 or its equivalents 0x480e00 and 0x4824d0) and 0x5762eb4 at
// 0.
template <typename Row>
void StepTheGuildList(const GuildWindowStepContext& context, Ui::OldList& list, std::vector<Row>& rows)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.ui.capture = list.base.id;
    if (PassesTheGuildWindowGate(list.base, runtime.ui, context.mouseX, context.mouseY))
    {
        const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
        const Ui::OldListGlobals listGlobals{.leftClick = globals.leftClick,
                                             .wheel = globals.wheel,
                                             .capture = globals.capture,
                                             .focusedList = globals.focusedList,
                                             .mouseOnUi = globals.mouseOnUi,
                                             .sound = globals.sound,
                                             .drawn = runtime.oldWindows.drawn == 1,
                                             .modal = runtime.oldWindows.modal,
                                             .soundRequest = globals.soundRequest};
        const Ui::OldListView view{
            .mouseX = context.mouseX, .mouseY = context.mouseY, .keyDown = [&context](std::uint8_t key) {
                return context.keyDown && context.keyDown(key);
            }};
        static_cast<void>(Ui::StepTheListMouse(list, rows.size(), listGlobals, view,
                                               [&context, &list, &rows](std::int32_t row, std::size_t at)
                                               { return HoverTheRow(context, list, rows, row, at); }));
    }
    runtime.ui.capture = 0;
}

// 0x46c930, 0x46bca0 and 0x46d900 after building the element: at the front, with more than +0xfc it drops from the
// end (0x47b7e0 and its equivalents 0x4814f0 and 0x482bc0), chooses 0, one more of scroll with more than the rows,
// clamps it, 1 if it is the only one, and at the end the last (the count) with the scroll to the end (−10000).
template <typename Row> void PutFirst(Ui::OldList& list, std::vector<Row>& rows, Row row)
{
    rows.insert(rows.begin(), std::move(row));
    while (std::cmp_greater(rows.size(), list.maxEntries))
        rows.pop_back();
    const std::size_t size = rows.size();
    Ui::SelectInTheList(list, size, 0);
    if (Ui::ListCount(list, size) > list.rows)
        ++list.scroll;
    if (const std::int32_t count = Ui::ListCount(list, size); count >= list.rows && count - list.scroll < list.rows)
        list.scroll = count - list.rows;
    if (size == 1)
        Ui::SelectInTheList(list, size, 1);
    if (size != 0)
    {
        Ui::SelectInTheList(list, size, Ui::ListCount(list, size));
        Ui::ScrollTheList(list, size, ScrollToTheEnd);
    }
}

// 0x47a410 and its equivalents 0x480260 and 0x4818b0: empties it, chooses 0 and puts the scroll back to 0.
template <typename Row> void ClearTheGuildList(Ui::OldList& list, std::vector<Row>& rows)
{
    rows.clear();
    Ui::SelectInTheList(list, 0, 0);
    list.scroll = 0;
}

// 0x64faa0: the slot of `name` among the 0x78aba38 members of 0x787a5e8 (strcmp), or −1.
Core::Expected<std::int32_t, std::string> FindTheMember(const GuildState& guild, std::string_view name)
{
    for (std::int32_t index = 0; index < guild.memberCount; ++index)
    {
        if (static_cast<std::size_t>(index) >= guild.members.size())
            return Core::Unexpected(std::string("0x64fac0: más de 80 miembros leen la tabla de los gremios"));
        if (GuildTextOf(guild.members[static_cast<std::size_t>(index)].name) == name)
            return index;
    }
    return -1;
}

// 0x6505cb..0x65062c, 0x6504a9 and 0x6513a0: strcpy of the name to 0x7d535a8 (12 bytes up to 0x7d535b4): the name
// and its 0; the rest stays from the previous name, and the E1 (10 bytes) and the EB (8) send it (Q-C1292).
void ChooseTheMember(GuildState& guild, std::string_view name)
{
    std::size_t index = 0;
    for (; index < name.size() && index + 1 < guild.chosenName.size(); ++index)
        guild.chosenName[index] = name[index];
    guild.chosenName[index] = '\0';
}

// 0x6600d0(rows, count, step, flags, function, 0) with what it gives in +0x428. With another one open (0x657930 gives
// the dialog's step and then the window's: a click on another master button still arrives), the notice of 0x6d69a8
// and +0x428 at 0.
Core::Expected<void, std::string> OpenTheGuildDialog(const GuildWindowStepContext& context,
                                                     std::span<const std::string> lines, std::uint32_t flags,
                                                     Ui::PanelDialogOwner owner, std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el diálogo de los paneles (0x6600d0) sin medidor", where));
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    auto opened = PopUpThePanelDialog(
        runtime, Ui::PanelDialogRequest{.lines = lines, .flags = flags, .owner = owner, .placed = 0}, *context.measure,
        context.screen, context.now);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    runtime.guild.window.dialog = opened.Value();
    return {};
}

// A one-row dialog with OK and no owner (0x6600d0(text, 1, 0x32, 1, 0, 0)).
Core::Expected<void, std::string> TellTheMaster(const GuildWindowStepContext& context, std::size_t text,
                                                std::string_view where)
{
    auto line = TextOf(context.texts, text, where);
    if (!line.HasValue())
        return Core::Unexpected(std::move(line).Error());
    const std::array<std::string, 1> lines{std::string(line.Value())};
    return OpenTheGuildDialog(context, lines, Ui::PanelDialogOk, Ui::PanelDialogOwner::None, where);
}

// 0x650648 and 0x6507ad: 0x6600d0(rows, 2, 0x40, 4, function, 0), with what it gives in +0x428.
Core::Expected<void, std::string> AskAboutTheMember(const GuildWindowStepContext& context, std::string_view name,
                                                    std::size_t text, Ui::PanelDialogOwner owner)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x650648: el diálogo de los paneles (0x6600d0) sin medidor"));
    auto format = TextOf(context.texts, ChosenNameText, "0x650594");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    auto second = TextOf(context.texts, text, "0x65059f");
    if (!second.HasValue())
        return Core::Unexpected(std::move(second).Error());
    const std::array<std::string_view, 1> arguments{name};
    const std::array<std::string, 2> lines{Ui::PlaceArguments(format.Value(), arguments), std::string(second.Value())};
    return OpenTheGuildDialog(context, lines, Ui::PanelDialogConfirm, owner, "0x650648");
}

// 0x6503bd..0x6507bb: the master's three buttons, one after the other.
Core::Expected<void, std::string> StepTheMasterButtons(const GuildWindowStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
    const auto chosen = [&]() -> std::optional<GuildMemberSlot>
    {
        const auto at = Ui::SelectedInTheList(window.memberList, window.members.size());
        if (!at)
            return std::nullopt;
        return window.members[*at].slot;
    };
    if (Ui::ClickTheButton(window.memberButtons[GuildAssignButton], globals, context.mouseX, context.mouseY))
    {
        if (const auto member = chosen(); member && member->status != MasterStatus)
        {
            guild.chosenStatus = member->status;
            ChooseTheMember(guild, GuildTextOf(member->name));
            // 0x6504b6: 0x6600d0 without rows or buttons (0xd5 × 0x2b) with what it gives in +0x428, and 0x6602b0 with
            // the positions window's step and draw. With another one open, 0x6600d0 gives 0 (with the notice) and
            // 0x6504d4 still sets the hooks on the open one: its step and draw become the positions', and its answer
            // stays (Q-C1306).
            if (auto opened = OpenTheGuildDialog(context, {}, 0, Ui::PanelDialogOwner::None, "0x6504b6");
                !opened.HasValue())
                return opened;
            Ui::HookThePanelDialog(runtime.panels.delegated.panelDialog, Ui::PanelDialogHooks::GuildPositions);
        }
    }
    if (Ui::ClickTheButton(window.memberButtons[GuildDismissButton], globals, context.mouseX, context.mouseY))
    {
        if (const auto member = chosen();
            member && (member->status == AssistantStatus || member->status == BattleMasterStatus))
        {
            const std::string_view name = GuildTextOf(member->name);
            ChooseTheMember(guild, name);
            if (auto asked = AskAboutTheMember(context, name, DismissText, Ui::PanelDialogOwner::GuildDismiss);
                !asked.HasValue())
                return asked;
        }
    }
    if (Ui::ClickTheButton(window.memberButtons[GuildKickButton], globals, context.mouseX, context.mouseY))
    {
        if (const auto member = chosen(); member && member->status != MasterStatus)
        {
            const std::string_view name = GuildTextOf(member->name);
            auto index = FindTheMember(guild, name);
            if (!index.HasValue())
                return Core::Unexpected(std::move(index).Error());
            guild.kickIndex = index.Value();
            if (auto asked = AskAboutTheMember(context, name, KickText, Ui::PanelDialogOwner::GuildKick);
                !asked.HasValue())
                return asked;
        }
    }
    return {};
}

// 0x650350: the members tab.
Core::Expected<void, std::string> StepTheMembersTab(const GuildWindowStepContext& context)
{
    if (!context.keyEdge)
        return Core::Unexpected(std::string("0x650387: PgUp y PgDn de la lista de los miembros sin el teclado"));
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    Ui::OldList& list = window.memberList;
    StepTheGuildList(context, list, window.members);
    if (context.keyEdge(PageUpKey))
        Ui::ScrollTheList(list, window.members.size(), -list.rows);
    if (context.keyEdge(PageDownKey))
        Ui::ScrollTheList(list, window.members.size(), list.rows);
    if (context.world.Hero().guildStatus != MasterStatus)
        return {};
    return StepTheMasterButtons(context);
}

// The guild (+0xd) and the union (+4) of the hero's entry (+0x1f2), without checking −1 (Q-C1281).
struct HeroGuildNames
{
    std::string_view guild;
    std::string_view alliance;
};

Core::Expected<HeroGuildNames, std::string> HeroGuildNamesOf(Kernel::WorldState& world, std::string_view where)
{
    const auto index = static_cast<std::int16_t>(world.Hero().word1f2);
    const GuildState& guild = world.Runtime().guild;
    const auto name = GuildNameAt(guild, index);
    const auto alliance = UnionNameAt(guild, index);
    if (!name || !alliance)
        return Core::Unexpected(std::format("{}: el gremio {} del héroe cae fuera de la tabla", where, index));
    return HeroGuildNames{.guild = *name, .alliance = *alliance};
}

// 0x64fdb0..0x64fddf and 0x64de99..0x64ded4: the game's dialog 0x7d (0x7d3a10c), 0x553570(0), the chat field closed
// (0x6c8e68) and a single field for Tab (0x6c8e6c). 0x6c8e78 (the limit of the input without an EDIT, from
// [0x7d3a0a4]) and 0x77f31c0 (its marks) belong to the input without an EDIT, which MuRet does not have.
void OpenTheNumberDialog(Kernel::WorldRuntime& runtime)
{
    runtime.dialogs.current = NumberDialog;
    Ui::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.ui.chatOpen = false;
    runtime.chat.input.fieldCount = NumberDialogFields;
}

// 0x64fce7..0x64fd8f: strcpy of 1363 to 1366 into their 0x32-byte slots, one after the other. A text of 0x32 bytes or
// more runs into the slot below and the next strcpy overwrites its tail with its 0: the row is left without its 0 and
// 0x6600d0 copies it up to the 0 of the one below (1364 in Spanish: Q-C1366). Past the fourth slot it would overwrite
// other locals: it stops.
Core::Expected<std::array<std::string, DisbandTexts.size()>, std::string> DisbandLines(const Tables::TextTable* texts)
{
    std::array<char, DisbandSlot * DisbandTexts.size()> stack{};
    for (std::size_t line = 0; line < DisbandTexts.size(); ++line)
    {
        auto text = TextOf(texts, DisbandTexts[line], "0x64fce7");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        const std::size_t start = line * DisbandSlot;
        if (start + text.Value().size() >= stack.size())
            return Core::Unexpected(std::format("0x64fd8f: el texto {} pasa de los cuatro renglones de la pila (0x32 "
                                                "bytes cada uno, Q-C1366)",
                                                DisbandTexts[line]));
        std::ranges::copy(text.Value(), stack.begin() + static_cast<std::ptrdiff_t>(start));
        stack[start + text.Value().size()] = '\0';
    }
    std::array<std::string, DisbandTexts.size()> lines{};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        const auto first = stack.begin() + static_cast<std::ptrdiff_t>(line * DisbandSlot);
        lines[line] = std::string(first, std::find(first, stack.end(), '\0'));
    }
    return lines;
}

// 0x64fc00: tab 0.
Core::Expected<void, std::string> StepTheNoticeTab(const GuildWindowStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    StepTheGuildList(context, window.noticeList, window.notice);
    const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
    if (!Ui::ClickTheButton(window.tabButtons[GuildLeaveButton], globals, context.mouseX, context.mouseY))
        return {};
    const Kernel::ActorState& hero = context.world.Hero();
    if (hero.guildStatus == MasterStatus)
    {
        auto names = HeroGuildNamesOf(context.world, "0x64fc55");
        if (!names.HasValue())
            return Core::Unexpected(std::move(names).Error());
        if (names.Value().guild == names.Value().alliance)
            return TellTheMaster(context, UnionMasterText, "0x64fcd5");
        auto lines = DisbandLines(context.texts);
        if (!lines.HasValue())
            return Core::Unexpected(std::move(lines).Error());
        return OpenTheGuildDialog(context, lines.Value(), Ui::PanelDialogConfirm, Ui::PanelDialogOwner::GuildDisband,
                                  "0x64fcd5");
    }
    // 0x64fd9c: 0x64faa0 with the hero's name (+0x1d9).
    auto index = FindTheMember(guild, Actors::HeroNameOf(hero));
    if (!index.HasValue())
        return Core::Unexpected(std::move(index).Error());
    guild.kickMember = index.Value();
    OpenTheNumberDialog(runtime);
    return {};
}

// 0x650c10: the union tab.
Core::Expected<void, std::string> StepTheUnionTab(const GuildWindowStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    StepTheGuildList(context, window.unionList, window.unions);
    const Kernel::ActorState& hero = context.world.Hero();
    if (hero.guildStatus != MasterStatus)
        return {};
    auto names = HeroGuildNamesOf(context.world, "0x650c74");
    if (!names.HasValue())
        return Core::Unexpected(std::move(names).Error());
    if (names.Value().alliance.empty())
        return {};
    const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
    if (Ui::ClickTheButton(window.tabButtons[GuildUnionLeaveButton], globals, context.mouseX, context.mouseY))
    {
        if (names.Value().guild == names.Value().alliance)
        {
            if (auto told = TellTheMaster(context, UnionMasterLeaveText, "0x650d22"); !told.HasValue())
                return told;
        }
        else
        {
            if (!context.send)
                return Core::Unexpected(std::string("0x650d35: el C1 07 E5 de salir de la alianza sin el cable"));
            context.send(Protocol::Guild::WriteRelationshipRequest(UnionRelation, BreakRequest, hero.key));
            // 0x651277..0x651290: sprintf with 1387 without arguments (Q-C1367) and 0x555190("", text, 1).
            auto format = TextOf(context.texts, UnionLeftText, "0x65127a");
            if (!format.HasValue())
                return Core::Unexpected(std::move(format).Error());
            if (context.measure == nullptr)
                return Core::Unexpected(std::string("0x651290: el aviso de salir de la alianza sin medidor"));
            if (auto added = Chat::AddGameMessage(runtime, *context.measure, context.screen, {},
                                                  Crt::Sprintf(format.Value()), UnionLeftType);
                !added.HasValue())
                return added;
        }
    }
    if (!Ui::ClickTheButton(window.tabButtons[GuildUnionKickButton], globals, context.mouseX, context.mouseY))
        return {};
    const auto at = Ui::SelectedInTheList(window.unionList, window.unions.size());
    if (!at)
        return {};
    const std::string_view name = GuildTextOf(window.unions[*at].name);
    if (name == names.Value().guild)
        return {};
    auto format = TextOf(context.texts, UnionKickText, "0x651367");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    auto second = TextOf(context.texts, KickText, "0x651372");
    if (!second.HasValue())
        return Core::Unexpected(std::move(second).Error());
    const std::array<std::string_view, 1> arguments{name};
    const std::array<std::string, 2> lines{Ui::PlaceArguments(format.Value(), arguments), std::string(second.Value())};
    ChooseTheMember(guild, name);
    return OpenTheGuildDialog(context, lines, Ui::PanelDialogConfirm, Ui::PanelDialogOwner::UnionKick, "0x6513d8");
}

// 0x651bbf..0x651c05: the tab's step.
Core::Expected<void, std::string> StepTheTab(const GuildWindowStepContext& context, std::int32_t tab)
{
    switch (tab)
    {
    case 0:
        return StepTheNoticeTab(context);
    case MembersTab:
        return StepTheMembersTab(context);
    case UnionTab:
        return StepTheUnionTab(context);
    default:
        return {};
    }
}

// 0x651f1c..0x652275: 0x621aa0's lines under the tabs that are not chosen, 0x459ec0, bold in white on 0 and each tab:
// the chosen one white (2 taller and higher) and the others at 0.6, with their text.
Core::Expected<void, std::string> DrawTheTabs(Ui::GameHudContext& context, std::int32_t tab, std::int32_t x,
                                              std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(TabLine);
    for (std::int32_t index = 0; index < TabCount; ++index)
    {
        if (index == tab)
            continue;
        sprites.ColorQuad(static_cast<float>(x + TabLeft + index * TabStep), static_cast<float>(y + TabLineTop),
                          TabLineWidth, TabLineHeight);
    }
    // 0x459ec0: glColor4f(1, 1, 1, 1) and the texture.
    sprites.SetColor(White);
    sprites.EnableTexture();
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    for (std::int32_t index = 0; index < TabCount; ++index)
    {
        const bool chosen = index == tab;
        const std::int32_t left = x + TabLeft + index * TabStep;
        const std::int32_t top = y + TabTop;
        const std::int32_t rise = chosen ? ChosenTabRise : 0;
        sprites.SetColor(chosen ? White : DimTab);
        sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{TabSlot},
                                          .x = static_cast<float>(left),
                                          .y = static_cast<float>(top - rise),
                                          .width = TabWidth,
                                          .height = F32(static_cast<double>(rise) + static_cast<double>(TabHeight)),
                                          .u = 0.0f,
                                          .v = 0.0f,
                                          .du = TabDu,
                                          .dv = TabDv,
                                          .scaleSize = true,
                                          .scalePosition = true,
                                          .alpha = 0.0f});
        auto text = TextOf(context.texts, TabTexts[static_cast<std::size_t>(index)], "0x6520cd");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawInTheTabBox(context, left, top - (chosen ? 1 : 0) + TabTextBelow, text.Value());
    }
    sprites.SetColor(White);
    return {};
}
} // namespace

bool PassesTheGuildWindowGate(const Ui::OldWindowBase& base, Ui::GameUiState& ui, std::int32_t mouseX,
                              std::int32_t mouseY)
{
    if (!Ui::MouseInRect(mouseX, mouseY, base.x, base.y, base.width, base.height, base.rectMode))
        return true;
    if (ui.hovered == 0)
    {
        if (ui.capture != 0 && ui.capture != base.id)
            return false;
        ui.hovered = base.id;
    }
    return base.state != Ui::ShownWindow || ui.hovered == base.id || ui.capture == base.id;
}

Core::Expected<void, std::string> StepTheGuildWindow(const GuildWindowStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    const Ui::OldWindowBase& base = window.base;
    if (!PassesTheGuildWindowGate(base, runtime.ui, context.mouseX, context.mouseY))
        return {};
    if (Ui::MouseInRect(context.mouseX, context.mouseY, base.x, base.y, base.width, MouseHeight, TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    if (runtime.mouse.pendingClick && Ui::MouseInRect(context.mouseX, context.mouseY, base.x + CloseLeft,
                                                      base.y + CloseTop, CloseSize, CloseSize, TopLeftRect))
    {
        runtime.mouse.pendingClick = false;
        runtime.mouse.updateTime = 0;
        runtime.mouse.updateTimeMax = CloseMouseUpdateTimeMax;
        runtime.RequestSound(ClickSound); // 0x41ed10 (CL10)
        // 0x6518e3: the vtable + 0x2c, 0x6522f0. The step goes on with the window closed.
        if (auto closed = CloseTheGuildWindow(context.world, context.send); !closed.HasValue())
            return closed;
    }
    if (context.world.Hero().guildStatus == NoGuildStatus)
        return {};
    if (runtime.mouse.leftHeld)
    {
        const auto inTab = [&](std::int32_t tab)
        {
            return window.tab != tab &&
                   Ui::MouseInRect(context.mouseX, context.mouseY,
                                   base.x + TabMouseLefts[static_cast<std::size_t>(tab)], base.y + TabMouseTop,
                                   TabMouseWidth, TabMouseHeight, TopLeftRect);
        };
        std::optional<std::int32_t> chosen;
        if (inTab(0))
            chosen = 0;
        else if (inTab(MembersTab))
            chosen = MembersTab;
        else if (inTab(UnionTab))
        {
            if (auto asked = AskForTheUnionList(context); !asked.HasValue())
                return asked;
            chosen = UnionTab;
        }
        if (chosen)
        {
            if (auto cancelled = CancelTheGuildWindowDialog(context.world, context.send); !cancelled.HasValue())
                return cancelled;
            window.tab = *chosen;
        }
    }
    return StepTheTab(context, window.tab);
}

void AddTheGuildMember(Kernel::WorldState& world, const GuildMemberSlot& slot)
{
    if (slot.name[0] == '\0')
        return;
    Kernel::WorldRuntime& runtime = world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    Ui::OldList& list = window.memberList;
    const std::string_view name = GuildTextOf(slot.name);
    if (Ui::ListCount(list, window.members.size()) == 0)
    {
        // 0x46c955: strcmp with the hero's name (+0x1d9, ten bytes and what follows: *inferred*, up to the first 0).
        const auto& heroName = world.Hero().name;
        std::string hero;
        for (const std::byte letter : heroName)
        {
            if (letter == std::byte{0})
                break;
            hero.push_back(std::to_integer<char>(letter));
        }
        window.heroFirst = name == hero ? 1 : 0;
    }
    GuildMemberRow row{};
    std::copy(name.begin(), name.end(), row.slot.name.begin());
    row.slot.number = slot.number;
    row.slot.server = slot.server;
    row.slot.status = slot.status;
    PutFirst(list, window.members, row);
}

void ClearTheGuildMembers(GuildWindowState& window)
{
    ClearTheGuildList(window.memberList, window.members);
}

Core::Expected<void, std::string> AddTheGuildNotice(GuildWindowState& window, const Render::Text::ITextMeasure& measure,
                                                    Render::Text::FontKind font, Core::PixelSize screen,
                                                    std::string_view text)
{
    constexpr int NoticeWidth = 0x6e;
    constexpr int NoticeLines = 5;
    auto wrapped = Ui::WrapByWidth(text, measure, font, screen, NoticeWidth, NoticeLines, 0);
    if (!wrapped.HasValue())
        return Core::Unexpected(std::move(wrapped).Error());
    Ui::OldList& list = window.noticeList;
    for (const std::string& line : wrapped.Value().lines)
    {
        // 0x46bca0: with no text or an empty one, nothing; strcpy to 0x575d234 (nobody writes +0 of 0x575d230: 0).
        if (line.empty())
            continue;
        PutFirst(list, window.notice, GuildNoticeRow{.toggled = 0, .text = line});
    }
    const std::size_t size = window.notice.size();
    Ui::ScrollTheList(list, size, Ui::ListCount(list, size) - list.rows);
    return {};
}

void ClearTheGuildNotice(GuildWindowState& window)
{
    ClearTheGuildList(window.noticeList, window.notice);
}

void AddTheUnion(GuildWindowState& window, const std::array<std::uint8_t, GuildMarkNibbles>& mark,
                 std::string_view name, std::uint8_t number)
{
    if (name.empty() || name.front() == '\0')
        return;
    GuildUnionRow row{};
    row.mark = mark;
    // 0x46d93d: strncpy(+0x44, name, 8) and +0x4c at 0.
    constexpr std::size_t NameCopy = 8;
    for (std::size_t index = 0; index < NameCopy && index < name.size() && name[index] != '\0'; ++index)
        row.name[index] = name[index];
    row.byte0 = number;
    PutFirst(window.unionList, window.unions, row);
}

void ClearTheUnions(GuildWindowState& window)
{
    ClearTheGuildList(window.unionList, window.unions);
}

Core::Expected<void, std::string> StepTheGuildPositions(Kernel::WorldState& world, const GuildSender& send,
                                                        std::int32_t mouseX, std::int32_t mouseY,
                                                        const Ui::PanelDialogAnswer& answer)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    GuildState& guild = runtime.guild;
    GuildPositionsState& positions = guild.positions;
    const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
    if (Ui::MouseInRect(mouseX, mouseY, positions.x, positions.y, PositionsMouseWidth, PositionsMouseHeight,
                        TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    // 0x64e64e: with the click's edge (0x7d366d4, which it does not spend), the two boxes choose the position.
    if (runtime.mouse.pendingClick)
    {
        if (Ui::MouseInRect(mouseX, mouseY, positions.x + PositionsChoiceLeft, positions.y + AssistantChoiceTop,
                            PositionsChoiceWidth, PositionsChoiceHeight, TopLeftRect))
            positions.choice = AssistantChoice;
        if (Ui::MouseInRect(mouseX, mouseY, positions.x + PositionsChoiceLeft, positions.y + BattleMasterChoiceTop,
                            PositionsChoiceWidth, PositionsChoiceHeight, TopLeftRect))
            positions.choice = BattleMasterChoice;
    }
    Ui::PanelDialogState& dialog = runtime.panels.delegated.panelDialog;
    if (Ui::ClickTheButton(positions.buttons[GuildPositionsOkButton], globals, mouseX, mouseY))
    {
        // 0x64e6cc: without a position (0x7d535b4 at 0), C1 0F E1 01; with one, 02; with the chosen one and the 10
        // bytes of 0x7d535a8, through the socket (0x6c82a0). Then C1 03 52 (0x64efca) and 0x6603c0.
        if (!send)
            return Core::Unexpected(std::string("0x64e6cc: el E1 de la ventana de los cargos sin el cable"));
        const std::uint8_t type = guild.chosenStatus == 0 ? AppointType : ChangeType;
        Protocol::FrontEnd::NameBytes name{};
        for (std::size_t index = 0; index < name.size(); ++index)
            name[index] = static_cast<std::byte>(guild.chosenName[index]);
        send(Protocol::Guild::WriteAssignStatus(type, static_cast<std::uint8_t>(positions.choice), name));
        send(Protocol::Guild::WriteMemberListRequest());
        if (auto cancelled = Ui::CancelThePanelDialog(dialog, answer); !cancelled.HasValue())
            return cancelled;
    }
    // 0x64f196: the other button cancels (closed, 0x6603c0 does nothing).
    if (Ui::ClickTheButton(positions.buttons[GuildPositionsCancelButton], globals, mouseX, mouseY))
        return Ui::CancelThePanelDialog(dialog, answer);
    return {};
}

void LendTheGuildWindowStepTo(Ui::PanelContext& panels, const GuildWindowStepContext& context)
{
    panels.guildStep = [context]() { return StepTheGuildWindow(context); };
}

Core::Expected<void, std::string> DrawTheGuildWindow(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const GuildState& guild = runtime.guild;
    const GuildWindowState& window = guild.window;
    const std::int32_t x = window.base.x;
    const std::int32_t y = window.base.y;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    Ui::WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    auto title = TextOf(context.texts, TitleText, "0x651cd5");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    context.hud.textColors = TitleColors;
    SelectFont(context, FontKind::Bold);
    Ui::WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    const Kernel::ActorState& hero = context.world.Hero();
    const bool inAGuild = hero.guildStatus != NoGuildStatus;
    if (inAGuild)
    {
        const auto index = static_cast<std::int16_t>(hero.word1f2);
        const auto name = GuildNameAt(guild, index);
        if (!name)
            return Core::Unexpected(std::format("0x651da0: el gremio {} del héroe cae fuera de la tabla", index));
        context.hud.textColors.text = ScoreColor;
        Ui::WindowDrawing::DrawTitle(context, x + TitleLeft, y + ScoreTop,
                                     std::format("{} ( Score:{} )", *name, guild.score));
    }
    else
    {
        SelectFont(context, FontKind::Normal);
        context.hud.textColors.text = NoGuildColor;
        std::int32_t top = y + NoGuildTop;
        for (const std::size_t index : NoGuildTexts)
        {
            auto text = TextOf(context.texts, index, "0x651e25");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            DrawText(context, x + NoGuildLeft, top, text.Value());
            top += NoGuildStep;
        }
    }
    if (Ui::WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto help = TextOf(context.texts, CloseHelpText, "0x651ee6");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        Ui::WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    }
    if (!inAGuild)
        return {};
    if (auto tabs = DrawTheTabs(context, window.tab, x, y); !tabs.HasValue())
        return tabs;
    // 0x652291..0x6522c0: the tab's draw.
    return DrawTheGuildTab(context, window.tab);
}
} // namespace Client::Guild
