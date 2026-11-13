#include "Scenes/SecondGuildWindow.h"

#include "Core/Expected.h"
#include "Protocol/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/OriginalQuad.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/GuildWindow.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLogWindow.h"
#include "UI/Dialogs.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/NameChecks.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindows.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"
#include "World/OriginalFtol.h"

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

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;
using WindowDrawing::DrawText;
using WindowDrawing::SelectFont;

constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};

// 0x656df0: el rectángulo del mouse de la UI, 0xbe (+0x58) × 0x1b1, con el modo 1.
constexpr std::int32_t MouseHeight = 0x1b1;
constexpr std::int32_t TopLeftRect = 1;
// Las páginas (0x7d536ec) y lo que arman (0x7d536e4).
constexpr std::int32_t QuestionPage = 0;
constexpr std::int32_t NamePage = 1;
constexpr std::int32_t MarkPage = 2;
constexpr std::int32_t ConfirmPage = 3;
constexpr std::int32_t NewGuild = 1;
constexpr std::int32_t NewMark = 2;
// 0x655d70 y 0x6552b0: C1 04 54 01 (crear) y 54 00.
constexpr std::uint8_t CreateAnswer = 1;
constexpr std::uint8_t ClosedAnswer = 0;
// 0x6560c3..0x6560e9: el botón de cerrar, 0x18 × 0x18 en (0x19, 0x18b) de la esquina, y lo que deja en 0x6c8dcc.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x41ed10(0x19, 0, 0): el clic del botón de cerrar y de la paleta (CL10).
constexpr std::int32_t ClickSound = 0x19;
// 0x6236f0: los avisos de 0x654f80 y 0x655c10.
constexpr std::int32_t FilteredNameDialog = 0x72;
constexpr std::int32_t ForbiddenSymbolDialog = 0x7a;
constexpr std::int32_t ShortNameDialog = 0x79;
constexpr std::int32_t EmptyMarkDialog = 0x7e;
// 0x654e70: un nombre de menos de 4 bytes es corto.
constexpr std::size_t ShortestName = 4;
// 0x6550cf, 0x6550e0..0x655120: el héroe sin gremio (+0x1f2 en 0xffff) y los diez bytes de 0x7798bf8 (dos DWORD y un
// WORD) en 0.
constexpr std::uint16_t NoGuildWord = 0xffff;
constexpr std::size_t ClearedMessageBytes = 10;
// 0x468260: el EDIT a la vista (0) y oculto (3).
constexpr std::int32_t ShownField = 0;
constexpr std::int32_t HiddenField = 3;
// 0x655202: EM_LIMITTEXT del nombre.
constexpr std::size_t GuildNameLimit = 8;
// 0x6535d1: el ángulo que 0x6535c0 le pone al héroe (+0x24) en cada paso del editor.
constexpr float EditorHeroAngle = 112.5f;
// 0x6535c0 y 0x653750: la grilla de 8 × 8 de 15 en (x + 0x32, y + 0x64) y la paleta de 2 × 8 de 20 en
// (x + 0xf, y + 0x104).
constexpr std::int32_t GridSide = 8;
constexpr std::int32_t GridStep = 15;
constexpr std::int32_t GridLeft = 0x32;
constexpr std::int32_t GridTop = 0x64;
constexpr std::int32_t PaletteRows = 2;
constexpr std::int32_t PaletteStep = 20;
constexpr std::int32_t PaletteLeft = 0xf;
constexpr std::int32_t PaletteTop = 0x104;
// 0x653750: los cuadrados de la grilla en (x + 50 + 1, y + 100 + 1) de 13, los de la paleta en (x + 15 + 1,
// y + 260 + 1) de 18 y el elegido en (x + 15 + 1, y + 230 + 1) de 23, las sumas en la x87; los dos renglones en
// (x + 0x32, y + 0xe6) y (x + 0x32, y + 0xf5).
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
// 0x5f1bb0: el marco del cuadrado (Interface\..., el 0x116) 1 afuera y 2 más grande, con du = w · 0,03125 y
// dv = h · 0,033333335 (0x6b8880 y 0x6ba29c), y el cuadrado con du = dv = 1.
constexpr std::uint16_t SwatchFrameSlot = 0x116;
constexpr double SwatchFrameOut = 1.0;
constexpr double SwatchFrameGrow = 2.0;
constexpr float SwatchDuStep = 0.03125f;
constexpr float SwatchDvStep = 0.033333335f;
// 0x655ac0: los dos marcos (140 × 20 en (x + 0x19, y + 0x41) y 53 × 53 en (x + 0x45, y + 0x68)), el texto en
// (x + 0x19, y + 0x47) y la marca de 48 en (x + 0x48, y + 0x6b).
constexpr float NameBoxWidth = 140.0f;
constexpr float NameBoxHeight = 20.0f;
constexpr float MarkBoxSide = 53.0f;
constexpr float MarkSide = 48.0f;
// 0x655130: la caja del nombre (el 0x10f de 110 × 18 en (x + 0x3a, y + 0x38), du 0,859375 y dv 0,5625) y el EDIT de
// 0x46 × 0xe en (x + 0x3f, y + 0x3c).
constexpr std::uint16_t NameFieldSlot = 0x10f;
constexpr float NameFieldWidth = 110.0f;
constexpr float NameFieldHeight = 18.0f;
constexpr float NameFieldDu = 0.859375f;
constexpr float NameFieldDv = 0.5625f;
// 0x6563e0..0x656504: el texto de la página en (x + 0x19, y + 0x3c) y los dos botones en x + 0x2d, 0x1e y 0x36 más
// abajo.
constexpr std::int32_t PageLeft = 0x19;
constexpr std::int32_t PageTop = 0x3c;
constexpr std::int32_t ButtonsLeft = 0x2d;
constexpr std::int32_t FirstButtonBelow = 0x1e;
constexpr std::int32_t SecondButtonBelow = 0x18;
// 0x656e70: el título en (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
// 0x6563e0, 0x655c70 y 0x655ac0: 0x5542f0(…, _ftol(rx · 140), 2, 0), el texto centrado en una caja de 140 · rx.
constexpr double TextBox = 140.0;
// Los colores de 0x6c8e5c y 0x6c8e64 del título (0x656f2e) y de las páginas (0x6563f6).
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr Render::Text::TextColors PageColors{.text = 0xffe6e6e6u, .background = 0};
// 0x6551e8..0x655223: el EDIT sin filtro (+0x4c en 0), el fondo en 0 (+0x44(0, 0, 0, 0)), de 0x46 × 0xe en
// (x + 0x3f, y + 0x3c).
constexpr std::int32_t FieldWidth = 0x46;
constexpr std::int32_t FieldHeight = 0xe;
constexpr std::int32_t FieldLeft = 0x26;
// El héroe sin gremio en +0x368.
constexpr std::uint8_t NoGuildStatus = 0xff;

// Text.bmd (la dirección − 0x76bd1dc, sobre 300): el título de cada página (0x76ca4cc, 0x771c8d0, 0x771c9fc y
// 0x76cdd0c), la pregunta (0x76ca5f8), "nombre" (0x76ca724), los dos renglones del editor (0x76ca850 y 0x76ca97c),
// la ayuda del botón de cerrar (0x7706814), el aviso del botón de la marca (0x771a0f8) y los textos de los botones
// (0x45b170 en 0x654900).
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
// 0x622c70: los textos 457 y 458.
constexpr std::size_t OperationText = 457;
constexpr std::size_t ServerText = 458;

constexpr auto CreatingIndex = static_cast<std::size_t>(CreatingGuildIndex);

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

template <std::size_t Size> std::string_view NameOf(const std::array<char, Size>& name)
{
    const auto end = std::ranges::find(name, '\0');
    return {name.data(), static_cast<std::size_t>(end - name.begin())};
}

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
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

UI::OldButton& ButtonOf(GuildState& guild, std::size_t button)
{
    return guild.secondWindow.buttons[button];
}

// 0x654f00: la página que sigue (1: 0 → 1 → 3; 2: 0 → 2 → 3).
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

// 0x654eb0: la de antes (1: 3 → 1, y de 1 a la pregunta con el 0x7d536e4 en 0; 2: 3 → 2, y de 2 a la pregunta).
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

// 0x654e90: algún nibble de la marca nueva distinto de 0.
bool MarkHasColor(const GuildState& guild)
{
    return std::ranges::any_of(guild.table[CreatingIndex].mark, [](std::uint8_t nibble) { return nibble != 0; });
}

// 0x6535c0(x, y): el ángulo del héroe; la grilla de la marca (el izquierdo abajo pinta con el color elegido, el
// derecho borra: los dos, borra); la paleta, con el flanco del clic, elige el color (fila · 8 + columna) y suena.
void StepTheMarkEditor(WorldState& world, std::int32_t x, std::int32_t y, std::int32_t mouseX, std::int32_t mouseY)
{
    WorldRuntime& runtime = world.Runtime();
    GuildState& guild = runtime.guild;
    world.Actors()[runtime.hero].angles.z = EditorHeroAngle;
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
            // 0x65364c: el byte de abajo de 0x78aba44.
            if (runtime.movement.leftHeld)
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
                !runtime.movement.pendingClick)
                continue;
            runtime.movement.pendingClick = false;
            runtime.lastSoundId = ClickSound; // 0x41ed10 (CL10)
            guild.markColor = row * GridSide + column;
        }
    }
}

// 0x6236f0 con el aviso.
void Notice(WorldRuntime& runtime, std::int32_t dialog)
{
    UI::PushDialog(runtime.dialogs, dialog);
}

// 0x655087..0x6550b4 y 0x6550d8..0x655100: con el EDIT (0x6c7b8c en 1), SetText(0), 0x467360 (el modo del IME, que
// MuRet no tiene) y oculto.
void HideTheNameField(UI::ChatInputState& input)
{
    input.dialogField.text.SetText({});
    UI::ShowTheDialogField(input, HiddenField);
}

// 0x655d70, la página 0: Crear (la ventana arma un gremio: 0x7d536e4 y 0x7d536ec en 1, 0x77f3b68 en 1 y C1 04 54 01);
// Cambiar la marca (el diálogo de los paneles con el 1269, Aceptar, sin función, a +0x2a0); el flanco sobre el botón
// de cerrar lo apaga, 0x77f3b10 en 0 y 0x6c8dcc en 6, manda C1 04 54 00, suena el 0x19 y cierra la ventana con la
// vtable + 0x2c (0x6570b0, que manda otro 54 00: Q-C1289).
Core::Expected<void, std::string> StepTheQuestion(const SecondGuildStepContext& context,
                                                  const UI::OldWindowGlobals& globals)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    SecondGuildWindowState& window = guild.secondWindow;
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildCreateButton), globals, context.mouseX, context.mouseY))
    {
        guild.createKind = NewGuild;
        guild.createStep = NamePage;
        runtime.chat.input.dialogFieldOn = true;
        if (auto sent = Send(context.send, Protocol::Guild::WriteMasterAnswer(CreateAnswer), "0x655dcb");
            !sent.HasValue())
            return sent;
    }
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildMarkButton), globals, context.mouseX, context.mouseY))
    {
        auto text = TextOf(context.texts, NotEnabledText, "0x65607a");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (context.measure == nullptr)
            return Core::Unexpected(std::string("0x6560b3: el diálogo de los paneles (0x6600d0) sin medidor"));
        // 0x6560b3: lo que da, a +0x2a0; con otro abierto (el mismo, con un segundo clic: 0x657930 da el paso del
        // diálogo y después el de la ventana), el aviso de 0x6d69a8 y 0.
        const std::array<std::string, 1> lines{std::string(text.Value())};
        auto opened = PopUpThePanelDialog(
            runtime,
            UI::PanelDialogRequest{
                .lines = lines, .flags = UI::PanelDialogOk, .owner = UI::PanelDialogOwner::None, .placed = 0},
            *context.measure, context.screen, context.now);
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        window.dialog2a0 = opened.Value();
    }
    const UI::OldWindowBase& base = window.base;
    if (!runtime.movement.pendingClick || !UI::MouseInRect(context.mouseX, context.mouseY, base.x + CloseLeft,
                                                           base.y + CloseTop, CloseSize, CloseSize, TopLeftRect))
        return {};
    runtime.movement.pendingClick = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
    if (auto sent = Send(context.send, Protocol::Guild::WriteMasterAnswer(ClosedAnswer), "0x65610a"); !sent.HasValue())
        return sent;
    runtime.lastSoundId = ClickSound; // 0x41ed10 (CL10)
    return CloseTheSecondGuildWindow(context.world, context.send);
}

// 0x654f80, la página 1: el editor; Próximo lee el EDIT al nombre del gremio nuevo (0x78a597d) y al buffer del mensaje
// (0x7798bf8) y mira, en orden, el nombre filtrado (0x622c70 sobre el buffer, el aviso 0x72), un byte malo (0x447ca0,
// 0x7a), menos de 4 bytes (0x79) y la marca vacía (0x7e); sin aviso, el EDIT vacío y oculto y la página que sigue. Con
// aviso o sin él, el EDIT vacío y oculto otra vez. Regreso: el héroe sin gremio, el EDIT vacío y oculto, los diez bytes
// del buffer y su largo en 0 (0x6c8e78 en 10, el tope de la entrada sin EDIT, MuRet no lo guarda) y la página de antes.
Core::Expected<void, std::string> StepTheName(const SecondGuildStepContext& context,
                                              const UI::OldWindowGlobals& globals)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    UI::ChatInputState& input = runtime.chat.input;
    const UI::OldWindowBase& base = guild.secondWindow.base;
    StepTheMarkEditor(context.world, base.x, base.y, context.mouseX, context.mouseY);
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildNextButton), globals, context.mouseX, context.mouseY))
    {
        // 0x654fb3..0x654fda: GetText(0x78a597d, 0xff) y GetText(0x7798bf8, 0xff). El nombre tiene 9 bytes hasta la
        // marca (+0x16): el EDIT, con EM_LIMITTEXT 8, no pasa de ahí.
        std::array<char, GuildTableEntry{}.guildName.size()>& name = guild.table[CreatingIndex].guildName;
        const std::string_view typed = input.dialogField.text.Value();
        if (typed.size() >= name.size())
            return Core::Unexpected(std::format("0x654fc5: GetText del nombre del gremio ({} bytes) pisaría la marca "
                                                "nueva (0x78a5986)",
                                                typed.size()));
        std::ranges::copy(typed, name.begin());
        name[typed.size()] = '\0';
        UI::ReadTheDialogField(input, input.messageBuffer);
        auto operation = TextOf(context.texts, OperationText, "0x622c70");
        if (!operation.HasValue())
            return Core::Unexpected(std::move(operation).Error());
        auto server = TextOf(context.texts, ServerText, "0x622c70");
        if (!server.HasValue())
            return Core::Unexpected(std::move(server).Error());
        const UI::NameFilters filters{.filterName = context.filterName,
                                      .filter = context.filter,
                                      .operation = operation.Value(),
                                      .server = server.Value()};
        const std::string_view message = UI::CStringOf(input.messageBuffer);
        if (UI::IsFilteredName(message, filters))
            Notice(runtime, FilteredNameDialog);
        else if (UI::HasForbiddenSymbol(std::string_view(input.messageBuffer.data(), input.messageBuffer.size())))
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
    if (!UI::ClickTheButton(ButtonOf(guild, SecondGuildBackButton), globals, context.mouseX, context.mouseY))
        return {};
    ActorState& hero = context.world.Actors()[runtime.hero];
    hero.word1f2 = NoGuildWord;
    HideTheNameField(input);
    std::fill_n(input.messageBuffer.begin(), ClearedMessageBytes, '\0');
    input.messageLength = 0;
    PreviousPage(guild);
    return {};
}

// 0x655c10, la página 2: el editor; Próximo con la marca vacía, el aviso 0x7e, y si no, la página que sigue; Regreso,
// la de antes.
void StepTheMark(const SecondGuildStepContext& context, const UI::OldWindowGlobals& globals)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    const UI::OldWindowBase& base = guild.secondWindow.base;
    StepTheMarkEditor(context.world, base.x, base.y, context.mouseX, context.mouseY);
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildNextButton), globals, context.mouseX, context.mouseY))
    {
        if (MarkHasColor(guild))
            NextPage(guild);
        else
            Notice(runtime, EmptyMarkDialog);
    }
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildBackButton), globals, context.mouseX, context.mouseY))
        PreviousPage(guild);
}

// 0x6552b0, la página 3: Próximo junta los 64 nibbles de a dos (el par primero arriba, 0x6552f1..0x655335); armando
// un gremio, manda el C1 55 con el nombre (los 8 bytes de 0x78a597d) y la marca; siempre manda C1 04 54 00 y cierra la
// ventana (0x6570b0, otro 54 00: Q-C1289). La marca sola (2) no manda nada más (Q-C1288). Regreso, la página de antes.
Core::Expected<void, std::string> StepTheConfirmation(const SecondGuildStepContext& context,
                                                      const UI::OldWindowGlobals& globals)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildNextButton), globals, context.mouseX, context.mouseY))
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
    if (UI::ClickTheButton(ButtonOf(guild, SecondGuildBackButton), globals, context.mouseX, context.mouseY))
        PreviousPage(guild);
    return {};
}

// 0x621b60 con u y v.
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

// 0x5f1bb0(x, y, w, h, color): el marco 0x116 y el cuadrado del color.
Core::Expected<void, std::string> DrawTheSwatch(GameHudContext& context, float x, float y, std::int32_t width,
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
    WindowDrawing::Quad(sprites, slot.Value().value, x, y, fw, fh, 1.0f, 1.0f);
    return {};
}

// 0x653750(x, y): la grilla con la marca nueva, la paleta y el color elegido, y los dos renglones con la fuente normal.
Core::Expected<void, std::string> DrawTheMarkEditor(GameHudContext& context, const GuildState& guild, std::int32_t x,
                                                    std::int32_t y)
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

// 0x5542f0(x, y, texto, caja, 2, 0): el texto centrado en la caja (0x467060 en el modo 2), sin medir.
void DrawInTheBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
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

// El gremio del héroe (+0x1f2) en la tabla, para "%s ( Score:%d )" (0x6d67cc).
Core::Expected<std::string, std::string> HeroGuildLine(GameHudContext& context, std::string_view where)
{
    WorldRuntime& runtime = context.world.Runtime();
    const auto index = static_cast<std::int16_t>(context.world.Actors()[runtime.hero].word1f2);
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(std::format("{}: el gremio {} del héroe cae fuera de la tabla", where, index));
    return std::format("{} ( Score:{} )", NameOf(runtime.guild.table[static_cast<std::size_t>(index)].guildName),
                       runtime.guild.score);
}

// 0x5f16a0(1999, 1) sin quad: sube la marca nueva y la pisa el primer cuadrado del editor (Q-C1290).
Core::Expected<void, std::string> UploadTheNewMark(GameHudContext& context, const GuildState& guild)
{
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x5f16a0: la marca nueva sin la tabla de las marcas"));
    auto slot = context.guildMarks->Upload(guild, CreatingGuildIndex, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    return {};
}

void DrawTheButton(GameHudContext& context, const UI::OldButton& button)
{
    const UI::OldWindowDrawContext buttons{.sprites = context.sprites,
                                           .text = context.text,
                                           .dcFont = context.hud.dcFont,
                                           .textColors = context.hud.textColors};
    UI::DrawTheOldButton(buttons, button, UI::OldWindowDrawView{.mouseX = context.mouse.x, .mouseY = context.mouse.y});
}

// 0x6563e0, la página 0: sin gremio (+0x368 en 0xff), la pregunta (el 181) con Crear a la vista y Cambiar la marca
// apagado; con gremio, su nombre y el puntaje (0x78aba3c) centrados, Crear apagado y la marca a la vista. Los dos
// botones debajo, y el botón de cerrar con su ayuda (el 1002) con el mouse encima.
Core::Expected<void, std::string> DrawTheQuestion(GameHudContext& context, GuildState& guild, std::int32_t x,
                                                  std::int32_t y)
{
    WorldRuntime& runtime = context.world.Runtime();
    context.hud.textColors = PageColors;
    UI::OldButton& create = ButtonOf(guild, SecondGuildCreateButton);
    UI::OldButton& mark = ButtonOf(guild, SecondGuildMarkButton);
    const std::int32_t left = x + PageLeft;
    std::int32_t top = y + PageTop;
    if (context.world.Actors()[runtime.hero].guildStatus == NoGuildStatus)
    {
        auto question = TextOf(context.texts, QuestionText, "0x656497");
        if (!question.HasValue())
            return Core::Unexpected(std::move(question).Error());
        DrawText(context, left, top, question.Value());
        create.base.state = UI::ShownWindow;
        mark.base.state = UI::DisabledWindow;
    }
    else
    {
        auto line = HeroGuildLine(context, "0x65641e");
        if (!line.HasValue())
            return Core::Unexpected(std::move(line).Error());
        DrawInTheBox(context, left, top, line.Value());
        create.base.state = UI::DisabledWindow;
        mark.base.state = UI::ShownWindow;
    }
    top += FirstButtonBelow;
    create.base.x = x + ButtonsLeft;
    create.base.y = top;
    DrawTheButton(context, create);
    mark.base.x = x + ButtonsLeft;
    mark.base.y = top + SecondButtonBelow;
    DrawTheButton(context, mark);
    if (!WindowDrawing::DrawTheCloseButton(context, x, y))
        return {};
    auto help = TextOf(context.texts, CloseHelpText, "0x6565ba");
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    return {};
}

// 0x655130, la página 1: la caja del nombre, "nombre" en negrita, el EDIT (a la vista, el fondo en 0, EM_LIMITTEXT 8,
// en (x + 0x3f, y + 0x3c), el foco sin cambiar lo elegido y su dibujo), la marca nueva (sin quad), el editor y
// Regreso y Próximo.
Core::Expected<void, std::string> DrawTheName(GameHudContext& context, GuildState& guild, std::int32_t x,
                                              std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    WindowDrawing::Quad(sprites, NameFieldSlot, static_cast<float>(x + PageLeft + 0x21),
                        static_cast<float>(y + PageTop - 4), NameFieldWidth, NameFieldHeight, NameFieldDu, NameFieldDv);
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = PageColors;
    auto name = TextOf(context.texts, NameText, "0x6551b7");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    DrawText(context, x + PageLeft, y + PageTop, name.Value());
    UI::ChatInputState& input = context.world.Runtime().chat.input;
    UI::OldEdit& field = input.dialogField;
    UI::ShowTheDialogField(input, ShownField);
    field.filter = 0;
    field.backgroundColor = 0;
    field.text.Limit(GuildNameLimit);
    field.base.width = FieldWidth;
    field.base.height = FieldHeight;
    field.base.x = x + PageLeft + FieldLeft;
    field.base.y = y + PageTop;
    UI::FocusTheDialogField(input);
    // 0x65523d: la vtable + 0x14 del EDIT (0x468320), con el cursor si tiene el foco.
    UI::OldDraw::DrawTheOldEdit(UI::OldWindowDrawContext{.sprites = context.sprites,
                                                         .text = context.text,
                                                         .dcFont = context.hud.dcFont,
                                                         .textColors = context.hud.textColors,
                                                         .arrowHeld = context.arrowHeld},
                                field, input.focus == UI::ChatFocus::DialogField);
    if (auto uploaded = UploadTheNewMark(context, guild); !uploaded.HasValue())
        return uploaded;
    if (auto editor = DrawTheMarkEditor(context, guild, x, y); !editor.HasValue())
        return editor;
    DrawTheButton(context, ButtonOf(guild, SecondGuildBackButton));
    DrawTheButton(context, ButtonOf(guild, SecondGuildNextButton));
    return {};
}

// 0x655c70, la página 2: "nombre" en negrita y, en el mismo lugar, el gremio del héroe con el puntaje centrado
// (Q-C1288); la fuente normal, la marca nueva (sin quad), el editor y los dos botones.
Core::Expected<void, std::string> DrawTheMark(GameHudContext& context, GuildState& guild, std::int32_t x,
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

// 0x655ac0, la página 3: la fuente normal, el fondo de los textos en 0, el marco del nombre con "nombre : <nombre>"
// centrado, el marco de la marca con la marca nueva de 48 y los dos botones.
Core::Expected<void, std::string> DrawTheConfirmation(GameHudContext& context, GuildState& guild, std::int32_t x,
                                                      std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    SelectFont(context, FontKind::Normal);
    context.hud.textColors.background = 0;
    WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + PageLeft), static_cast<float>(y + 0x41), NameBoxWidth,
                                NameBoxHeight, false);
    auto name = TextOf(context.texts, NameText, "0x655b2a");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    DrawInTheBox(context, x + PageLeft, y + 0x47,
                 std::format("{} : {}", name.Value(), NameOf(guild.table[CreatingIndex].guildName)));
    WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + 0x45), static_cast<float>(y + 0x68), MarkBoxSide,
                                MarkBoxSide, false);
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x655b98: la marca nueva sin la tabla de las marcas"));
    auto slot = context.guildMarks->Upload(guild, CreatingGuildIndex, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    WindowDrawing::Quad(sprites, slot.Value().value, static_cast<float>(x + 0x48), static_cast<float>(y + 0x6b),
                        MarkSide, MarkSide, 1.0f, 1.0f);
    DrawTheButton(context, ButtonOf(guild, SecondGuildBackButton));
    DrawTheButton(context, ButtonOf(guild, SecondGuildNextButton));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheSecondGuildWindow(const SecondGuildStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    const UI::OldWindowBase& base = guild.secondWindow.base;
    if (!PassesTheGuildWindowGate(base, runtime.ui, context.mouseX, context.mouseY))
        return {};
    if (UI::MouseInRect(context.mouseX, context.mouseY, base.x, base.y, base.width, MouseHeight, TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
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

void LendTheSecondGuildStepTo(UI::PanelContext& panels, const SecondGuildStepContext& context)
{
    panels.secondGuildStep = [context]() { return StepTheSecondGuildWindow(context); };
}

Core::Expected<void, std::string> DrawTheSecondGuildWindow(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    SecondGuildWindowState& window = guild.secondWindow;
    for (std::size_t button = 0; button < window.buttons.size(); ++button)
    {
        // 0x654900 los copia al armarse (0x45b170), después de Text.bmd; MuRet, la primera vez que dibuja.
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
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
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
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, text.Value());
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
} // namespace Client::Scenes
