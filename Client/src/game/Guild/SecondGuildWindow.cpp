#include "Guild/SecondGuildWindow.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Outgoing.h"
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
#include "Hud/GameUi.h"
#include "Hud/GameWindowDrawing.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Guild/GuildWindow.h"
#include "Render/Frame/ITextDrawer.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/Dialogs.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Guild
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;
using Ui::WindowDrawing::DrawText;
using Ui::WindowDrawing::SelectFont;

constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};

// 0x656df0: the UI's mouse rectangle, 0xbe (+0x58) × 0x1b1, with mode 1.
constexpr std::int32_t MouseHeight = 0x1b1;
constexpr std::int32_t TopLeftRect = 1;
// The pages (0x7d536ec) and what they build (0x7d536e4).
constexpr std::int32_t QuestionPage = 0;
constexpr std::int32_t NamePage = 1;
constexpr std::int32_t MarkPage = 2;
constexpr std::int32_t ConfirmPage = 3;
constexpr std::int32_t NewGuild = 1;
constexpr std::int32_t NewMark = 2;
// 0x655d70 and 0x6552b0: C1 04 54 01 (create) and 54 00.
constexpr std::uint8_t CreateAnswer = 1;
constexpr std::uint8_t ClosedAnswer = 0;
// 0x6560c3..0x6560e9: the close button, 0x18 × 0x18 at (0x19, 0x18b) from the corner, and what it leaves in 0x6c8dcc.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x41ed10(0x19, 0, 0): the click of the close button and of the palette (CL10).
constexpr Audio::SoundId ClickSound = Sound::ButtonClickSound;
// 0x6236f0: the notices of 0x654f80 and 0x655c10.
constexpr std::int32_t FilteredNameDialog = 0x72;
constexpr std::int32_t ForbiddenSymbolDialog = 0x7a;
constexpr std::int32_t ShortNameDialog = 0x79;
constexpr std::int32_t EmptyMarkDialog = 0x7e;
// 0x654e70: a name of fewer than 4 bytes is short.
constexpr std::size_t ShortestName = 4;
// 0x6550cf, 0x6550e0..0x655120: the hero without a guild (+0x1f2 at 0xffff) and the ten bytes of 0x7798bf8 (two
// DWORDs and a WORD) at 0.
constexpr std::uint16_t NoGuildWord = 0xffff;
constexpr std::size_t ClearedMessageBytes = 10;
// 0x468260: the EDIT shown (0) and hidden (3).
constexpr std::int32_t ShownField = 0;
constexpr std::int32_t HiddenField = 3;
// 0x655202: the name's EM_LIMITTEXT.
constexpr std::size_t GuildNameLimit = 8;
// 0x6535d1: the angle 0x6535c0 gives the hero (+0x24) on every editor step.
constexpr float EditorHeroAngle = 112.5f;
// 0x6535c0 and 0x653750: the 8 × 8 grid of 15 at (x + 0x32, y + 0x64) and the 2 × 8 palette of 20 at
// (x + 0xf, y + 0x104).
constexpr std::int32_t GridSide = 8;
constexpr std::int32_t GridStep = 15;
constexpr std::int32_t GridLeft = 0x32;
constexpr std::int32_t GridTop = 0x64;
constexpr std::int32_t PaletteRows = 2;
constexpr std::int32_t PaletteStep = 20;
constexpr std::int32_t PaletteLeft = 0xf;
constexpr std::int32_t PaletteTop = 0x104;
// 0x653750: the grid squares at (x + 50 + 1, y + 100 + 1) of 13, the palette ones at (x + 15 + 1, y + 260 + 1) of 18
// and the chosen one at (x + 15 + 1, y + 230 + 1) of 23, the sums on the x87; the two rows at (x + 0x32, y + 0xe6)
// and (x + 0x32, y + 0xf5).
constexpr double GridDrawLeft = 50.0;
constexpr double GridDrawTop = 100.0;
constexpr double PaletteDrawLeft = 15.0;
constexpr double PaletteDrawTop = 260.0;
constexpr double ChosenTop = 230.0;
constexpr double SwatchInset = 1.0;
constexpr std::int32_t GridSwatch = 13;
constexpr std::int32_t PaletteSwatch = 18;
constexpr std::int32_t ChosenSwatch = 23;
constexpr std::int32_t HelpLeft = 0x32;
constexpr std::int32_t FirstHelpTop = 0xe6;
constexpr std::int32_t SecondHelpTop = 0xf5;
// 0x5f1bb0: the square's frame (Interface\..., 0x116) 1 outside and 2 bigger, with du = w · 0.03125 and
// dv = h · 0.033333335 (0x6b8880 and 0x6ba29c), and the square with du = dv = 1.
constexpr std::uint16_t SwatchFrameSlot = 0x116;
constexpr double SwatchFrameOut = 1.0;
constexpr double SwatchFrameGrow = 2.0;
constexpr float SwatchDuStep = 0.03125f;
constexpr float SwatchDvStep = 0.033333335f;
// 0x655ac0: the two frames (140 × 20 at (x + 0x19, y + 0x41) and 53 × 53 at (x + 0x45, y + 0x68)), the text at
// (x + 0x19, y + 0x47) and the 48 mark at (x + 0x48, y + 0x6b).
constexpr float NameBoxWidth = 140.0f;
constexpr float NameBoxHeight = 20.0f;
constexpr float MarkBoxSide = 53.0f;
constexpr float MarkSide = 48.0f;
// 0x655130: the name box (0x10f, 110 × 18 at (x + 0x3a, y + 0x38), du 0.859375 and dv 0.5625) and the 0x46 × 0xe EDIT
// at (x + 0x3f, y + 0x3c).
constexpr std::uint16_t NameFieldSlot = 0x10f;
constexpr float NameFieldWidth = 110.0f;
constexpr float NameFieldHeight = 18.0f;
constexpr float NameFieldDu = 0.859375f;
constexpr float NameFieldDv = 0.5625f;
// 0x6563e0..0x656504: the page's text at (x + 0x19, y + 0x3c) and the two buttons at x + 0x2d, 0x1e and 0x36 lower.
constexpr std::int32_t PageLeft = 0x19;
constexpr std::int32_t PageTop = 0x3c;
constexpr std::int32_t ButtonsLeft = 0x2d;
constexpr std::int32_t FirstButtonBelow = 0x1e;
constexpr std::int32_t SecondButtonBelow = 0x18;
// 0x656e70: the title at (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
// 0x6563e0, 0x655c70 and 0x655ac0: 0x5542f0(…, _ftol(rx · 140), 2, 0), the text centered in a 140 · rx box.
constexpr double TextBox = 140.0;
// The colors of 0x6c8e5c and 0x6c8e64 for the title (0x656f2e) and the pages (0x6563f6).
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr Render::Text::TextColors PageColors{.text = 0xffe6e6e6u, .background = 0};
// 0x6551e8..0x655223: the EDIT without a filter (+0x4c at 0), the background at 0 (+0x44(0, 0, 0, 0)), 0x46 × 0xe at
// (x + 0x3f, y + 0x3c).
constexpr std::int32_t FieldWidth = 0x46;
constexpr std::int32_t FieldHeight = 0xe;
constexpr std::int32_t FieldLeft = 0x26;
// The hero's +0x368 without a guild.
constexpr std::uint8_t NoGuildStatus = 0xff;

// Text.bmd (the address − 0x76bd1dc, over 300): each page's title (0x76ca4cc, 0x771c8d0, 0x771c9fc and 0x76cdd0c),
// the question (0x76ca5f8), "name" (0x76ca724), the editor's two rows (0x76ca850 and 0x76ca97c), the close button's
// help (0x7706814), the mark button's notice (0x771a0f8) and the buttons' texts (0x45b170 in 0x654900).
constexpr std::size_t GuildTitleText = 180;
constexpr std::size_t QuestionText = 181;
constexpr std::size_t NameText = 182;
constexpr std::size_t FirstEditorText = 183;
constexpr std::size_t SecondEditorText = 184;
constexpr std::size_t ConfirmTitleText = 228;
constexpr std::size_t CloseHelpText = 1002;
constexpr std::size_t NotEnabledText = 1269;
constexpr std::size_t CreateText = 1303;
constexpr std::size_t MarkText = 1304;
constexpr std::array<std::size_t, SecondGuildButtonCount> ButtonTexts{1303, 1304, 1305, 1306};
// 0x622c70: texts 457 and 458.
constexpr std::size_t OperationText = 457;
constexpr std::size_t ServerText = 458;

constexpr auto CreatingIndex = static_cast<std::size_t>(CreatingGuildIndex);

using Math::F32;
using Math::Truncate;

template <std::size_t Size> std::string_view NameOf(const std::array<char, Size>& name)
{
    const auto end = std::ranges::find(name, '\0');
    return {name.data(), static_cast<std::size_t>(end - name.begin())};
}

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<void, std::string> Send(const GuildSender& send, Protocol::Outgoing packet, std::string_view where)
{
    if (!send)
        return Core::Unexpected(std::string(where) + ": el paquete de la ventana 2 del gremio sin el cable");
    send(std::move(packet));
    return {};
}

Ui::OldButton& ButtonOf(GuildState& guild, std::size_t button)
{
    return guild.secondWindow.buttons[button];
}

// 0x654f00: the next page (1: 0 → 1 → 3; 2: 0 → 2 → 3).
void NextPage(GuildState& guild)
{
    if (guild.createKind == NewGuild)
    {
        if (guild.createStep == QuestionPage)
            guild.createStep = NamePage;
        else if (guild.createStep == NamePage)
            guild.createStep = ConfirmPage;
    }
    else if (guild.createKind == NewMark)
    {
        if (guild.createStep == QuestionPage)
            guild.createStep = MarkPage;
        else if (guild.createStep == MarkPage)
            guild.createStep = ConfirmPage;
    }
}

// 0x654eb0: the previous one (1: 3 → 1, and from 1 to the question with 0x7d536e4 at 0; 2: 3 → 2, and from 2 to the
// question).
void PreviousPage(GuildState& guild)
{
    if (guild.createKind == NewGuild)
    {
        if (guild.createStep == NamePage)
        {
            guild.createKind = 0;
            guild.createStep = QuestionPage;
        }
        else if (guild.createStep == ConfirmPage)
            guild.createStep = NamePage;
    }
    else if (guild.createKind == NewMark)
    {
        if (guild.createStep == MarkPage)
        {
            guild.createKind = 0;
            guild.createStep = QuestionPage;
        }
        else if (guild.createStep == ConfirmPage)
            guild.createStep = MarkPage;
    }
}

// 0x654e90: some nibble of the new mark other than 0.
bool MarkHasColor(const GuildState& guild)
{
    return std::ranges::any_of(guild.table[CreatingIndex].mark, [](std::uint8_t nibble) { return nibble != 0; });
}

// 0x6535c0(x, y): the hero's angle; the mark grid (the left button down paints with the chosen color, the right one
// erases: both, erases); the palette, with the click's edge, chooses the color (row · 8 + column) and plays the sound.
void StepTheMarkEditor(Kernel::WorldState& world, std::int32_t x, std::int32_t y, std::int32_t mouseX,
                       std::int32_t mouseY)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    GuildState& guild = runtime.guild;
    world.Hero().angles.z = EditorHeroAngle;
    auto& mark = guild.table[CreatingIndex].mark;
    for (std::int32_t row = 0; row < GridSide; ++row)
    {
        const std::int32_t top = y + GridTop + row * GridStep;
        for (std::int32_t column = 0; column < GridSide; ++column)
        {
            const std::int32_t left = x + GridLeft + column * GridStep;
            if (mouseX < left || mouseX >= left + GridStep || mouseY < top || mouseY >= top + GridStep)
                continue;
            const auto cell = static_cast<std::size_t>(row * GridSide + column);
            // 0x65364c: the low byte of 0x78aba44.
            if (runtime.mouse.leftHeld)
                mark[cell] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(guild.markColor) & 0xffu);
            if (runtime.ui.rightDown)
                mark[cell] = 0;
        }
    }
    for (std::int32_t row = 0; row < PaletteRows; ++row)
    {
        const std::int32_t top = y + PaletteTop + row * PaletteStep;
        for (std::int32_t column = 0; column < GridSide; ++column)
        {
            const std::int32_t left = x + PaletteLeft + column * PaletteStep;
            if (mouseX < left || mouseX >= left + PaletteStep || mouseY < top || mouseY >= top + PaletteStep ||
                !runtime.mouse.pendingClick)
                continue;
            runtime.mouse.pendingClick = false;
            runtime.RequestSound(ClickSound); // 0x41ed10 (CL10)
            guild.markColor = row * GridSide + column;
        }
    }
}

// 0x6236f0 with the notice.
void Notice(Kernel::WorldRuntime& runtime, std::int32_t dialog)
{
    Ui::PushDialog(runtime.dialogs, dialog);
}

// 0x655087..0x6550b4 and 0x6550d8..0x655100: with the EDIT (0x6c7b8c at 1), SetText(0), 0x467360 (the IME mode, which
// MuRet does not have) and hidden.
void HideTheNameField(Ui::ChatInputState& input)
{
    input.dialogEdit.text.SetText({});
    Ui::ShowTheDialogField(input, HiddenField);
}

// 0x655d70, page 0: Create (the window builds a guild: 0x7d536e4 and 0x7d536ec at 1, 0x77f3b68 at 1 and C1 04 54
// 01); Change the mark (the panels' dialog with 1269, Accept, no function, to +0x2a0); the edge over the close button
// turns it off, 0x77f3b10 at 0 and 0x6c8dcc at 6, sends C1 04 54 00, plays 0x19 and closes the window with the vtable
// + 0x2c (0x6570b0, which sends another 54 00: Q-C1289).
Core::Expected<void, std::string> StepTheQuestion(const SecondGuildStepContext& context,
                                                  const Ui::OldWindowGlobals& globals)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    SecondGuildWindowState& window = guild.secondWindow;
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildCreateButton), globals, context.mouseX, context.mouseY))
    {
        guild.createKind = NewGuild;
        guild.createStep = NamePage;
        runtime.chatInput.dialogFieldOn = true;
        if (auto sent = Send(context.send, Protocol::Guild::WriteMasterAnswer(CreateAnswer), "0x655dcb");
            !sent.HasValue())
            return sent;
    }
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildMarkButton), globals, context.mouseX, context.mouseY))
    {
        auto text = TextOf(context.texts, NotEnabledText, "0x65607a");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (context.measure == nullptr)
            return Core::Unexpected(std::string("0x6560b3: el diálogo de los paneles (0x6600d0) sin medidor"));
        // 0x6560b3: what it gives, to +0x2a0; with another one open (the same one, with a second click: 0x657930 gives
        // the dialog's step and then the window's), the notice of 0x6d69a8 and 0.
        const std::array<std::string, 1> lines{std::string(text.Value())};
        auto opened = PopUpThePanelDialog(
            runtime,
            Ui::PanelDialogRequest{
                .lines = lines, .flags = Ui::PanelDialogOk, .owner = Ui::PanelDialogOwner::None, .placed = 0},
            *context.measure, context.screen, context.now);
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        window.dialog2a0 = opened.Value();
    }
    const Ui::OldWindowBase& base = window.base;
    if (!runtime.mouse.pendingClick || !Ui::MouseInRect(context.mouseX, context.mouseY, base.x + CloseLeft,
                                                        base.y + CloseTop, CloseSize, CloseSize, TopLeftRect))
        return {};
    runtime.mouse.pendingClick = false;
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = CloseMouseUpdateTimeMax;
    if (auto sent = Send(context.send, Protocol::Guild::WriteMasterAnswer(ClosedAnswer), "0x65610a"); !sent.HasValue())
        return sent;
    runtime.RequestSound(ClickSound); // 0x41ed10 (CL10)
    return CloseTheSecondGuildWindow(context.world, context.send);
}

// 0x654f80, page 1: the editor; Next reads the EDIT into the new guild's name (0x78a597d) and the message buffer
// (0x7798bf8) and checks, in order, the filtered name (0x622c70 on the buffer, notice 0x72), a bad byte (0x447ca0,
// 0x7a), fewer than 4 bytes (0x79) and the empty mark (0x7e); without a notice, the EDIT emptied and hidden and the
// next page. With a notice or without, the EDIT emptied and hidden again. Back: the hero without a guild, the EDIT
// emptied and hidden, the buffer's ten bytes and its length at 0 (0x6c8e78 at 10, the limit of the input without an
// EDIT, MuRet does not store it) and the previous page.
Core::Expected<void, std::string> StepTheName(const SecondGuildStepContext& context,
                                              const Ui::OldWindowGlobals& globals)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    Ui::ChatInputState& input = runtime.chatInput;
    const Ui::OldWindowBase& base = guild.secondWindow.base;
    StepTheMarkEditor(context.world, base.x, base.y, context.mouseX, context.mouseY);
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildNextButton), globals, context.mouseX, context.mouseY))
    {
        // 0x654fb3..0x654fda: GetText(0x78a597d, 0xff) and GetText(0x7798bf8, 0xff). The name has 9 bytes up to the
        // mark
        // (+0x16): the EDIT, with EM_LIMITTEXT 8, does not go past it.
        std::array<char, GuildTableEntry{}.guildName.size()>& name = guild.table[CreatingIndex].guildName;
        const std::string_view typed = input.dialogEdit.text.Value();
        if (typed.size() >= name.size())
            return Core::Unexpected(std::format("0x654fc5: GetText del nombre del gremio ({} bytes) pisaría la marca "
                                                "nueva (0x78a5986)",
                                                typed.size()));
        std::ranges::copy(typed, name.begin());
        name[typed.size()] = '\0';
        Ui::ReadTheDialogField(input, input.messageBuffer);
        auto operation = TextOf(context.texts, OperationText, "0x622c70");
        if (!operation.HasValue())
            return Core::Unexpected(std::move(operation).Error());
        auto server = TextOf(context.texts, ServerText, "0x622c70");
        if (!server.HasValue())
            return Core::Unexpected(std::move(server).Error());
        const Ui::NameFilters filters{.filterName = context.filterName,
                                      .filter = context.filter,
                                      .operation = operation.Value(),
                                      .server = server.Value()};
        const std::string_view message = Ui::CStringOf(input.messageBuffer);
        if (Ui::IsFilteredName(message, filters))
            Notice(runtime, FilteredNameDialog);
        else if (Ui::HasForbiddenSymbol(std::string_view(input.messageBuffer.data(), input.messageBuffer.size())))
            Notice(runtime, ForbiddenSymbolDialog);
        else if (message.size() < ShortestName)
            Notice(runtime, ShortNameDialog);
        else if (!MarkHasColor(guild))
            Notice(runtime, EmptyMarkDialog);
        else
        {
            HideTheNameField(input);
            NextPage(guild);
        }
        HideTheNameField(input);
    }
    if (!Ui::ClickTheButton(ButtonOf(guild, SecondGuildBackButton), globals, context.mouseX, context.mouseY))
        return {};
    Kernel::ActorState& hero = context.world.Hero();
    hero.word1f2 = NoGuildWord;
    HideTheNameField(input);
    std::fill_n(input.messageBuffer.begin(), ClearedMessageBytes, '\0');
    input.messageLength = 0;
    PreviousPage(guild);
    return {};
}

// 0x655c10, page 2: the editor; Next with the empty mark, notice 0x7e, and otherwise the next page; Back, the previous
// one.
void StepTheMark(const SecondGuildStepContext& context, const Ui::OldWindowGlobals& globals)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    const Ui::OldWindowBase& base = guild.secondWindow.base;
    StepTheMarkEditor(context.world, base.x, base.y, context.mouseX, context.mouseY);
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildNextButton), globals, context.mouseX, context.mouseY))
    {
        if (MarkHasColor(guild))
            NextPage(guild);
        else
            Notice(runtime, EmptyMarkDialog);
    }
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildBackButton), globals, context.mouseX, context.mouseY))
        PreviousPage(guild);
}

// 0x6552b0, page 3: Next packs the 64 nibbles two by two (the first of the pair high, 0x6552f1..0x655335); building a
// guild, it sends the C1 55 with the name (the 8 bytes of 0x78a597d) and the mark; it always sends C1 04 54 00 and
// closes the window (0x6570b0, another 54 00: Q-C1289). The mark alone (2) sends nothing more (Q-C1288). Back, the
// previous page.
Core::Expected<void, std::string> StepTheConfirmation(const SecondGuildStepContext& context,
                                                      const Ui::OldWindowGlobals& globals)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildNextButton), globals, context.mouseX, context.mouseY))
    {
        const GuildTableEntry& entry = guild.table[CreatingIndex];
        Protocol::Guild::GuildMark packed{};
        for (std::size_t nibble = 0; nibble < entry.mark.size(); nibble += 2)
            packed[nibble / 2] = static_cast<std::uint8_t>((entry.mark[nibble] << 4) + entry.mark[nibble + 1]);
        if (guild.createKind == NewGuild)
        {
            Protocol::Guild::GuildName name{};
            for (std::size_t index = 0; index < name.size(); ++index)
                name[index] = static_cast<std::byte>(entry.guildName[index]);
            if (auto sent = Send(context.send, Protocol::Guild::WriteCreate(name, packed), "0x655712");
                !sent.HasValue())
                return sent;
        }
        if (auto sent = Send(context.send, Protocol::Guild::WriteMasterAnswer(ClosedAnswer), "0x6559bb");
            !sent.HasValue())
            return sent;
        if (auto closed = CloseTheSecondGuildWindow(context.world, context.send); !closed.HasValue())
            return closed;
    }
    if (Ui::ClickTheButton(ButtonOf(guild, SecondGuildBackButton), globals, context.mouseX, context.mouseY))
        PreviousPage(guild);
    return {};
}

// 0x621b60 with u and v.
void QuadAt(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
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

// 0x5f1bb0(x, y, w, h, color): the 0x116 frame and the color's square.
Core::Expected<void, std::string> DrawTheSwatch(Ui::GameHudContext& context, float x, float y, std::int32_t width,
                                                std::int32_t height, std::int32_t color)
{
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x5f1bb0: el color del editor de la marca sin la tabla de las marcas"));
    Render::SpriteBatch& sprites = context.sprites;
    const auto fw = static_cast<float>(width);
    const auto fh = static_cast<float>(height);
    QuadAt(sprites, SwatchFrameSlot, F32(static_cast<double>(x) - SwatchFrameOut),
           F32(static_cast<double>(y) - SwatchFrameOut), F32(static_cast<double>(fw) + SwatchFrameGrow),
           F32(static_cast<double>(fh) + SwatchFrameGrow), 0.0f, 0.0f,
           F32(static_cast<double>(fw) * static_cast<double>(SwatchDuStep)),
           F32(static_cast<double>(fh) * static_cast<double>(SwatchDvStep)));
    auto slot = context.guildMarks->UploadSwatch(color);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    Ui::WindowDrawing::Quad(sprites, slot.Value().value, x, y, fw, fh, 1.0f, 1.0f);
    return {};
}

// 0x653750(x, y): the grid with the new mark, the palette and the chosen color, and the two rows in the normal font.
Core::Expected<void, std::string> DrawTheMarkEditor(Ui::GameHudContext& context, const GuildState& guild,
                                                    std::int32_t x, std::int32_t y)
{
    const auto fx = static_cast<double>(static_cast<float>(x));
    const auto fy = static_cast<double>(static_cast<float>(y));
    const auto& mark = guild.table[CreatingIndex].mark;
    for (std::int32_t row = 0; row < GridSide; ++row)
    {
        const float top = F32(static_cast<double>(row * GridStep) + fy + GridDrawTop + SwatchInset);
        for (std::int32_t column = 0; column < GridSide; ++column)
        {
            const float left = F32(static_cast<double>(column * GridStep) + fx + GridDrawLeft + SwatchInset);
            const std::uint8_t nibble = mark[static_cast<std::size_t>(row * GridSide + column)];
            if (auto drawn = DrawTheSwatch(context, left, top, GridSwatch, GridSwatch, nibble); !drawn.HasValue())
                return drawn;
        }
    }
    std::int32_t color = 0;
    for (std::int32_t row = 0; row < PaletteRows; ++row)
    {
        const float top = F32(static_cast<double>(row * PaletteStep) + fy + PaletteDrawTop + SwatchInset);
        for (std::int32_t column = 0; column < GridSide; ++column)
        {
            const float left = F32(static_cast<double>(column * PaletteStep) + fx + PaletteDrawLeft + SwatchInset);
            if (auto drawn = DrawTheSwatch(context, left, top, PaletteSwatch, PaletteSwatch, color); !drawn.HasValue())
                return drawn;
            ++color;
        }
    }
    if (auto drawn = DrawTheSwatch(context, F32(fx + PaletteDrawLeft + SwatchInset), F32(fy + ChosenTop + SwatchInset),
                                   ChosenSwatch, ChosenSwatch, guild.markColor);
        !drawn.HasValue())
        return drawn;
    SelectFont(context, FontKind::Normal);
    context.hud.textColors = PageColors;
    auto first = TextOf(context.texts, FirstEditorText, "0x6538ab");
    if (!first.HasValue())
        return Core::Unexpected(std::move(first).Error());
    DrawText(context, x + HelpLeft, y + FirstHelpTop, first.Value());
    auto second = TextOf(context.texts, SecondEditorText, "0x6538c3");
    if (!second.HasValue())
        return Core::Unexpected(std::move(second).Error());
    DrawText(context, x + HelpLeft, y + SecondHelpTop, second.Value());
    return {};
}

// 0x5542f0(x, y, text, box, 2, 0): the text centered in the box (0x467060 in mode 2), without measuring.
void DrawInTheBox(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    const auto rx = static_cast<double>(Render::ScaleX(1.0f, context.sprites.Screen()));
    const std::int32_t box = Truncate(rx * TextBox);
    if (text.empty() && box == 0)
        return;
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.hud.dcFont, text, box);
    context.text.DrawColumns(x, y, text,
                             Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                             -placed.textColumn, placed.width);
}

// The hero's guild (+0x1f2) in the table, for "%s ( Score:%d )" (0x6d67cc).
Core::Expected<std::string, std::string> HeroGuildLine(Ui::GameHudContext& context, std::string_view where)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const auto index = static_cast<std::int16_t>(context.world.Hero().word1f2);
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(std::format("{}: el gremio {} del héroe cae fuera de la tabla", where, index));
    return std::format("{} ( Score:{} )", NameOf(runtime.guild.table[static_cast<std::size_t>(index)].guildName),
                       runtime.guild.score);
}

// 0x5f16a0(1999, 1) without a quad: uploads the new mark and the editor's first square overwrites it (Q-C1290).
Core::Expected<void, std::string> UploadTheNewMark(Ui::GameHudContext& context, const GuildState& guild)
{
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x5f16a0: la marca nueva sin la tabla de las marcas"));
    auto slot = context.guildMarks->Upload(guild, CreatingGuildIndex, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    return {};
}

void DrawTheButton(Ui::GameHudContext& context, const Ui::OldButton& button)
{
    const Ui::OldWindowDrawContext buttons{.sprites = context.sprites,
                                           .text = context.text,
                                           .dcFont = context.hud.dcFont,
                                           .textColors = context.hud.textColors};
    Ui::DrawTheOldButton(buttons, button, Ui::OldWindowDrawView{.mouseX = context.mouse.x, .mouseY = context.mouse.y});
}

// 0x6563e0, page 0: without a guild (+0x368 at 0xff), the question (181) with Create shown and Change the mark off;
// with a guild, its name and the score (0x78aba3c) centered, Create off and the mark shown. The two buttons below,
// and the close button with its help (1002) with the mouse over it.
Core::Expected<void, std::string> DrawTheQuestion(Ui::GameHudContext& context, GuildState& guild, std::int32_t x,
                                                  std::int32_t y)
{
    context.hud.textColors = PageColors;
    Ui::OldButton& create = ButtonOf(guild, SecondGuildCreateButton);
    Ui::OldButton& mark = ButtonOf(guild, SecondGuildMarkButton);
    const std::int32_t left = x + PageLeft;
    std::int32_t top = y + PageTop;
    if (context.world.Hero().guildStatus == NoGuildStatus)
    {
        auto question = TextOf(context.texts, QuestionText, "0x656497");
        if (!question.HasValue())
            return Core::Unexpected(std::move(question).Error());
        DrawText(context, left, top, question.Value());
        create.base.state = Ui::ShownWindow;
        mark.base.state = Ui::DisabledWindow;
    }
    else
    {
        auto line = HeroGuildLine(context, "0x65641e");
        if (!line.HasValue())
            return Core::Unexpected(std::move(line).Error());
        DrawInTheBox(context, left, top, line.Value());
        create.base.state = Ui::DisabledWindow;
        mark.base.state = Ui::ShownWindow;
    }
    top += FirstButtonBelow;
    create.base.x = x + ButtonsLeft;
    create.base.y = top;
    DrawTheButton(context, create);
    mark.base.x = x + ButtonsLeft;
    mark.base.y = top + SecondButtonBelow;
    DrawTheButton(context, mark);
    if (!Ui::WindowDrawing::DrawTheCloseButton(context, x, y))
        return {};
    auto help = TextOf(context.texts, CloseHelpText, "0x6565ba");
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    Ui::WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    return {};
}

// 0x655130, page 1: the name box, "name" in bold, the EDIT (shown, the background at 0, EM_LIMITTEXT 8, at
// (x + 0x3f, y + 0x3c), the focus without changing the selection and its draw), the new mark (without a quad), the
// editor and Back and Next.
Core::Expected<void, std::string> DrawTheName(Ui::GameHudContext& context, GuildState& guild, std::int32_t x,
                                              std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    Ui::WindowDrawing::Quad(sprites, NameFieldSlot, static_cast<float>(x + PageLeft + 0x21),
                            static_cast<float>(y + PageTop - 4), NameFieldWidth, NameFieldHeight, NameFieldDu,
                            NameFieldDv);
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = PageColors;
    auto name = TextOf(context.texts, NameText, "0x6551b7");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    DrawText(context, x + PageLeft, y + PageTop, name.Value());
    Ui::ChatInputState& input = context.world.Runtime().chatInput;
    Ui::OldEdit& field = input.dialogEdit;
    Ui::ShowTheDialogField(input, ShownField);
    field.filter = 0;
    field.backgroundColor = 0;
    Ui::LimitTheEdit(field, GuildNameLimit, context.arrowHeld);
    field.base.width = FieldWidth;
    field.base.height = FieldHeight;
    field.base.x = x + PageLeft + FieldLeft;
    field.base.y = y + PageTop;
    // 0x655232: the vtable + 0x30 (0x4682a0), the focus and the EDIT's id (7) in 0x5762ec0 (the list that takes the
    // arrows).
    Ui::FocusTheDialogField(input);
    context.world.Runtime().ui.focusedList = field.base.id;
    // 0x65523d: the EDIT's vtable + 0x14 (0x468320), with the cursor if it has the focus.
    Ui::OldDraw::DrawTheOldEdit(Ui::OldWindowDrawContext{.sprites = context.sprites,
                                                         .text = context.text,
                                                         .dcFont = context.hud.dcFont,
                                                         .textColors = context.hud.textColors,
                                                         .arrowHeld = context.arrowHeld},
                                field, input.focus == Ui::ChatFocus::Dialog);
    if (auto uploaded = UploadTheNewMark(context, guild); !uploaded.HasValue())
        return uploaded;
    if (auto editor = DrawTheMarkEditor(context, guild, x, y); !editor.HasValue())
        return editor;
    DrawTheButton(context, ButtonOf(guild, SecondGuildBackButton));
    DrawTheButton(context, ButtonOf(guild, SecondGuildNextButton));
    return {};
}

// 0x655c70, page 2: "name" in bold and, in the same place, the hero's guild with the score centered (Q-C1288); the
// normal font, the new mark (without a quad), the editor and the two buttons.
Core::Expected<void, std::string> DrawTheMark(Ui::GameHudContext& context, GuildState& guild, std::int32_t x,
                                              std::int32_t y)
{
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = PageColors;
    auto name = TextOf(context.texts, NameText, "0x655cb4");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    DrawText(context, x + PageLeft, y + PageTop, name.Value());
    auto line = HeroGuildLine(context, "0x655cce");
    if (!line.HasValue())
        return Core::Unexpected(std::move(line).Error());
    DrawInTheBox(context, x + PageLeft, y + PageTop, line.Value());
    SelectFont(context, FontKind::Normal);
    if (auto uploaded = UploadTheNewMark(context, guild); !uploaded.HasValue())
        return uploaded;
    if (auto editor = DrawTheMarkEditor(context, guild, x, y); !editor.HasValue())
        return editor;
    DrawTheButton(context, ButtonOf(guild, SecondGuildBackButton));
    DrawTheButton(context, ButtonOf(guild, SecondGuildNextButton));
    return {};
}

// 0x655ac0, page 3: the normal font, the text background at 0, the name frame with "name : <name>" centered, the mark
// frame with the new 48 mark and the two buttons.
Core::Expected<void, std::string> DrawTheConfirmation(Ui::GameHudContext& context, GuildState& guild, std::int32_t x,
                                                      std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    SelectFont(context, FontKind::Normal);
    context.hud.textColors.background = 0;
    Ui::WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + PageLeft), static_cast<float>(y + 0x41),
                                    NameBoxWidth, NameBoxHeight, false);
    auto name = TextOf(context.texts, NameText, "0x655b2a");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    DrawInTheBox(context, x + PageLeft, y + 0x47,
                 std::format("{} : {}", name.Value(), NameOf(guild.table[CreatingIndex].guildName)));
    Ui::WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + 0x45), static_cast<float>(y + 0x68), MarkBoxSide,
                                    MarkBoxSide, false);
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x655b98: la marca nueva sin la tabla de las marcas"));
    auto slot = context.guildMarks->Upload(guild, CreatingGuildIndex, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    Ui::WindowDrawing::Quad(sprites, slot.Value().value, static_cast<float>(x + 0x48), static_cast<float>(y + 0x6b),
                            MarkSide, MarkSide, 1.0f, 1.0f);
    DrawTheButton(context, ButtonOf(guild, SecondGuildBackButton));
    DrawTheButton(context, ButtonOf(guild, SecondGuildNextButton));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheSecondGuildWindow(const SecondGuildStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    const Ui::OldWindowBase& base = guild.secondWindow.base;
    if (!PassesTheGuildWindowGate(base, runtime.ui, context.mouseX, context.mouseY))
        return {};
    if (Ui::MouseInRect(context.mouseX, context.mouseY, base.x, base.y, base.width, MouseHeight, TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
    switch (guild.createStep)
    {
    case QuestionPage:
        return StepTheQuestion(context, globals);
    case NamePage:
        return StepTheName(context, globals);
    case MarkPage:
        StepTheMark(context, globals);
        return {};
    case ConfirmPage:
        return StepTheConfirmation(context, globals);
    default:
        return {};
    }
}

void LendTheSecondGuildStepTo(Ui::PanelContext& panels, const SecondGuildStepContext& context)
{
    panels.secondGuildStep = [context]() { return StepTheSecondGuildWindow(context); };
}

Core::Expected<void, std::string> DrawTheSecondGuildWindow(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    SecondGuildWindowState& window = guild.secondWindow;
    for (std::size_t button = 0; button < window.buttons.size(); ++button)
    {
        // 0x654900 copies them when it is built (0x45b170), after Text.bmd; MuRet, the first time it draws.
        if (window.buttons[button].label)
            continue;
        auto label = TextOf(context.texts, ButtonTexts[button], "0x6549a7");
        if (!label.HasValue())
            return Core::Unexpected(std::move(label).Error());
        window.buttons[button].label = std::string(label.Value());
    }
    const std::int32_t x = window.base.x;
    const std::int32_t y = window.base.y;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    Ui::WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    context.hud.textColors = TitleColors;
    SelectFont(context, FontKind::Bold);
    std::size_t title = GuildTitleText;
    if (guild.createStep == NamePage)
        title = CreateText;
    else if (guild.createStep == MarkPage)
        title = MarkText;
    else if (guild.createStep == ConfirmPage)
        title = ConfirmTitleText;
    auto text = TextOf(context.texts, title, "0x656f89");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Ui::WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, text.Value());
    switch (guild.createStep)
    {
    case QuestionPage:
        return DrawTheQuestion(context, guild, x, y);
    case NamePage:
        return DrawTheName(context, guild, x, y);
    case MarkPage:
        return DrawTheMark(context, guild, x, y);
    case ConfirmPage:
        return DrawTheConfirmation(context, guild, x, y);
    default:
        return {};
    }
}
} // namespace Client::Guild
