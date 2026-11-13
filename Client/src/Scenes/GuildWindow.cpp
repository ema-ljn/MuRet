#include "Scenes/GuildWindow.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Guild.h"
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
#include "Scenes/GameMessages.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/GuildWindowTabs.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/ChatLogWindow.h"
#include "UI/Dialogs.h"
#include "UI/OldControls.h"
#include "UI/OldWindows.h"
#include "UI/PanelDialog.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"
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
#include <vector>

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;
using WindowDrawing::DrawText;
using WindowDrawing::SelectFont;

constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};
// 0x65202d y 0x6520fe: la pestaña que no está elegida, en 0,6.
constexpr Render::Color DimTab{.red = 0.6f, .green = 0.6f, .blue = 0.6f, .alpha = 1.0f};
// 0x651f22: glColor4ub(0x92, 0x90, 0x8d, 0xff), las rayas bajo las pestañas que no están elegidas.
constexpr Render::Color TabLine{
    .red = 146.0f / 255.0f, .green = 144.0f / 255.0f, .blue = 141.0f / 255.0f, .alpha = 1.0f};

// 0x651840: el rectángulo del mouse de la UI, 0xbe (+0x58) × 0x1b1, con el modo 1.
constexpr std::int32_t MouseHeight = 0x1b1;
constexpr std::int32_t TopLeftRect = 1;
// 0x651895..0x6518e3: el botón de cerrar, lo que deja en 0x6c8dcc y su sonido (CL10).
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
constexpr std::int32_t ClickSound = 0x19;
// +0x368 del héroe sin gremio (0x511290).
constexpr std::uint8_t NoGuildStatus = 0xff;
// Las pestañas: el paso las mira de a 0x34 desde x + 0xf (0x651913..0x65199d) y el dibujo las pone de a 0x36.
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
// 0x652092: Interface\..., el 0x646 de 52 × (16 + 2 la elegida), du 0,75 y dv 0,9375, 2 más arriba la elegida.
constexpr std::uint16_t TabSlot = 0x646;
constexpr float TabWidth = 52.0f;
constexpr float TabHeight = 16.0f;
constexpr float TabDu = 0.75f;
constexpr float TabDv = 0.9375f;
constexpr std::int32_t ChosenTabRise = 2;
// 0x6520c8: el texto de la pestaña en y + 4, uno más arriba la elegida, centrado en _ftol(rx · 52) (0x6ba060).
constexpr std::int32_t TabTextBelow = 4;
constexpr double TabTextBox = 52.0;
constexpr std::array<std::size_t, TabCount> TabTexts{946, 1330, 1352};
// 0x651cd2..0x651d5d y 0x651d73..0x651dd6: el título y el gremio con el puntaje, en negrita sobre el fondo 0.
constexpr std::size_t TitleText = 180;
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
constexpr std::int32_t ScoreTop = 0x35;
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0};
constexpr std::uint32_t ScoreColor = 0xff64ffc8u;
// 0x651dfa..0x651e4f: sin gremio, los textos 185, 186 y 187 en la normal, de a 0xf desde (x + 0x14, y + 0x32).
constexpr std::array<std::size_t, 3> NoGuildTexts{185, 186, 187};
constexpr std::int32_t NoGuildLeft = 0x14;
constexpr std::int32_t NoGuildTop = 0x32;
constexpr std::int32_t NoGuildStep = 0xf;
constexpr std::uint32_t NoGuildColor = 0xffe6e6e6u;
// 0x651ee6: la ayuda del botón de cerrar.
constexpr std::size_t CloseHelpText = 220;
// 0x650350: PgUp y PgDn (0x553520) y el maestro (+0x368 en 0x80) con sus dos cargos, 0x40 y 0x20.
constexpr std::uint8_t PageUpKey = 0x21;
constexpr std::uint8_t PageDownKey = 0x22;
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t AssistantStatus = 0x40;
constexpr std::uint8_t BattleMasterStatus = 0x20;
// 0x650594..0x650716: el renglón 0 con el nombre (sprintf con el 1367) y el 1 (strcpy del 1368 o del 1369), de a 0x40,
// con Confirmar y Cancelar (4).
constexpr std::size_t ChosenNameText = 1367;
constexpr std::size_t DismissText = 1368;
constexpr std::size_t KickText = 1369;
// 0x46d3a0: el renglón de la lista, de 0xd desde la y del renglón − 3, con el ancho _ftol(ancho − cursor + 1).
constexpr std::int32_t MemberRowAbove = 3;
constexpr std::int32_t MemberRowHeight = 0xd;
// 0x46cab0: el corrimiento hasta el final.
constexpr std::int32_t ScrollToTheEnd = -10000;
// 0x64fc00: el maestro de la alianza (1270, OK), el diálogo de disolver el gremio (1363 a 1366, de a 0x32, con
// Confirmar) y el diálogo 0x7d del juego que pide el número (0x64fdb0) con la cuenta de Tab (0x6c8e6c) en 1.
constexpr std::size_t UnionMasterText = 1270;
constexpr std::array<std::size_t, 4> DisbandTexts{1363, 1364, 1365, 1366};
constexpr std::int32_t NumberDialog = 0x7d;
constexpr std::int32_t NumberDialogFields = 1;
// 0x650c10: el maestro de la alianza (1271), C1 07 E5 01 02 (romper la alianza) con el aviso 1387 del tipo 1, y el
// diálogo de echar a un gremio de ella (1423 con el nombre y 1369).
constexpr std::size_t UnionMasterLeaveText = 1271;
constexpr std::uint8_t UnionRelation = 1;
constexpr std::uint8_t BreakRequest = 2;
constexpr std::size_t UnionLeftText = 1387;
constexpr std::int32_t UnionLeftType = 1;
constexpr std::size_t UnionKickText = 1423;
// 0x64e600: el mouse de la UI sobre 0xd5 × 0xfa desde la esquina, las dos cajas de 0xaa × 0x14 en x + 0x14 (y + 0x13
// y y + 0x29) y el tipo del E1: 1 sin cargo, 2 con uno.
constexpr std::int32_t PositionsMouseWidth = 0xd5;
constexpr std::int32_t PositionsMouseHeight = 0xfa;
constexpr std::int32_t PositionsChoiceLeft = 0x14;
constexpr std::int32_t AssistantChoiceTop = 0x13;
constexpr std::int32_t BattleMasterChoiceTop = 0x29;
constexpr std::int32_t PositionsChoiceWidth = 0xaa;
constexpr std::int32_t PositionsChoiceHeight = 0x14;
constexpr std::uint8_t AppointType = 1;
constexpr std::uint8_t ChangeType = 2;

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x5542f0(x, y, texto, _ftol(rx · 52), 2, 0): el texto centrado en la caja (0x467060 en el modo 2), sin medir.
void DrawInTheTabBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
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

// 0x6519ad..0x651ba4: la primera vez que se elige la pestaña de la alianza, con alianza (+4 de la entrada del gremio
// del héroe, sin mirar el −1: Q-C1281), C1 03 E9 en claro por el socket (0x6c82a0) y +0x84 en 1.
Core::Expected<void, std::string> AskForTheUnionList(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (guild.window.unionRequested != 0)
        return {};
    const auto index = static_cast<std::int16_t>(context.world.Actors()[runtime.hero].word1f2);
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

// 0x46d3a0, la vtable + 0x88 de la lista de los miembros (0x46c3d0 la del aviso y 0x46e1e0 la de la alianza, iguales
// salvo el tamaño del elemento y la y del renglón de la alianza, que es su + 0xa0): el mouse sobre el renglón con el
// clic lo elige (corrimiento + renglón + 1, la + 0x58), invierte +0 de su elemento y gasta el clic. Siempre 1.
template <typename Row>
std::int32_t HoverTheRow(const GuildWindowStepContext& context, UI::OldList& list, std::vector<Row>& rows,
                         std::int32_t row, std::size_t at)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::size_t size = rows.size();
    const std::int32_t width =
        Truncate(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0);
    if (UI::MouseInRect(context.mouseX, context.mouseY, list.base.x, UI::ListRowY(list, size, row) - MemberRowAbove,
                        width, MemberRowHeight, TopLeftRect) &&
        runtime.movement.pendingClick)
    {
        UI::SelectInTheList(list, size, list.scroll + row + 1);
        Row& element = rows[at];
        element.toggled = (element.toggled + 1) % 2;
        runtime.movement.pendingClick = false;
    }
    return 1;
}

// 0x650378, 0x64fc14 y 0x650c3c: 0x5762eb4 con el id de la lista, su vtable + 0x18 (0x45aa80(0): la cola, vacía
// porque nadie le manda mensajes, el mouse y 0x47b0f0 o sus iguales 0x480e00 y 0x4824d0) y 0x5762eb4 en 0.
template <typename Row>
void StepTheGuildList(const GuildWindowStepContext& context, UI::OldList& list, std::vector<Row>& rows)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.ui.capture = list.base.id;
    if (PassesTheGuildWindowGate(list.base, runtime.ui, context.mouseX, context.mouseY))
    {
        const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
        const UI::OldListGlobals listGlobals{.leftClick = globals.leftClick,
                                             .wheel = globals.wheel,
                                             .capture = globals.capture,
                                             .focusedList = globals.focusedList,
                                             .mouseOnUi = globals.mouseOnUi,
                                             .sound = globals.sound,
                                             .drawn = runtime.oldWindows.drawn == 1,
                                             .modal = runtime.oldWindows.modal};
        const UI::OldListView view{
            .mouseX = context.mouseX, .mouseY = context.mouseY, .keyDown = [&context](std::uint8_t key) {
                return context.keyDown && context.keyDown(key);
            }};
        static_cast<void>(UI::StepTheListMouse(list, rows.size(), listGlobals, view,
                                               [&context, &list, &rows](std::int32_t row, std::size_t at)
                                               { return HoverTheRow(context, list, rows, row, at); }));
    }
    runtime.ui.capture = 0;
}

// 0x46c930, 0x46bca0 y 0x46d900 después de armar el elemento: adelante, con más de +0xfc saca del final (0x47b7e0 y sus
// iguales 0x4814f0 y 0x482bc0), elige 0, uno más de corrimiento con más que los renglones, lo acota, 1 si es el único,
// y al final el último (la cuenta) con el corrimiento hasta el final (−10.000).
template <typename Row> void PutFirst(UI::OldList& list, std::vector<Row>& rows, Row row)
{
    rows.insert(rows.begin(), std::move(row));
    while (std::cmp_greater(rows.size(), list.maxEntries))
        rows.pop_back();
    const std::size_t size = rows.size();
    UI::SelectInTheList(list, size, 0);
    if (UI::ListCount(list, size) > list.rows)
        ++list.scroll;
    if (const std::int32_t count = UI::ListCount(list, size); count >= list.rows && count - list.scroll < list.rows)
        list.scroll = count - list.rows;
    if (size == 1)
        UI::SelectInTheList(list, size, 1);
    if (size != 0)
    {
        UI::SelectInTheList(list, size, UI::ListCount(list, size));
        UI::ScrollTheList(list, size, ScrollToTheEnd);
    }
}

// 0x47a410 y sus iguales 0x480260 y 0x4818b0: la vacía, elige 0 y vuelve el corrimiento a 0.
template <typename Row> void ClearTheGuildList(UI::OldList& list, std::vector<Row>& rows)
{
    rows.clear();
    UI::SelectInTheList(list, 0, 0);
    list.scroll = 0;
}

// 0x64faa0: el lugar de `name` entre los 0x78aba38 miembros de 0x787a5e8 (strcmp), o −1.
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

// 0x6505cb..0x65062c, 0x6504a9 y 0x6513a0: strcpy del nombre a 0x7d535a8 (12 bytes hasta 0x7d535b4): el nombre y su
// 0; lo que sigue queda del nombre de antes, y el E1 (10 bytes) y el EB (8) lo mandan (Q-C1292).
void ChooseTheMember(GuildState& guild, std::string_view name)
{
    std::size_t index = 0;
    for (; index < name.size() && index + 1 < guild.chosenName.size(); ++index)
        guild.chosenName[index] = name[index];
    guild.chosenName[index] = '\0';
}

// 0x6600d0(renglones, cuántos, paso, banderas, función, 0) con lo que da en +0x428.
Core::Expected<void, std::string> OpenTheGuildDialog(const GuildWindowStepContext& context,
                                                     std::span<const std::string> lines, std::uint32_t flags,
                                                     UI::PanelDialogOwner owner, std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el diálogo de los paneles (0x6600d0) sin medidor", where));
    WorldRuntime& runtime = context.world.Runtime();
    UI::PanelDialogState& dialog = runtime.panels.delegated.panelDialog;
    const auto opened = UI::OpenThePanelDialog(
        dialog, UI::PanelDialogRequest{.lines = lines, .flags = flags, .owner = owner, .placed = 0}, *context.measure,
        runtime.hud.dcFont, context.now);
    if (opened == UI::PanelDialogOpening::Busy)
        return Core::Unexpected(std::string("0x6600f9: el aviso del diálogo ocupado (0x6d69a8, en EUC-KR) no está en "
                                            "Text.bmd"));
    runtime.guild.window.dialog = opened == UI::PanelDialogOpening::Opened ? UI::PanelDialogHandle(dialog) : 0U;
    return {};
}

// Un diálogo de un renglón con OK y sin dueño (0x6600d0(texto, 1, 0x32, 1, 0, 0)).
Core::Expected<void, std::string> TellTheMaster(const GuildWindowStepContext& context, std::size_t text,
                                                std::string_view where)
{
    auto line = TextOf(context.texts, text, where);
    if (!line.HasValue())
        return Core::Unexpected(std::move(line).Error());
    const std::array<std::string, 1> lines{std::string(line.Value())};
    return OpenTheGuildDialog(context, lines, UI::PanelDialogOk, UI::PanelDialogOwner::None, where);
}

// 0x650648 y 0x6507ad: 0x6600d0(renglones, 2, 0x40, 4, función, 0), con lo que da en +0x428.
Core::Expected<void, std::string> AskAboutTheMember(const GuildWindowStepContext& context, std::string_view name,
                                                    std::size_t text, UI::PanelDialogOwner owner)
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
    const std::array<std::string, 2> lines{UI::PlaceArguments(format.Value(), arguments), std::string(second.Value())};
    return OpenTheGuildDialog(context, lines, UI::PanelDialogConfirm, owner, "0x650648");
}

// 0x6503bd..0x6507bb: los tres botones del maestro, uno detrás del otro.
Core::Expected<void, std::string> StepTheMasterButtons(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    const auto chosen = [&]() -> std::optional<GuildMemberSlot>
    {
        const auto at = UI::SelectedInTheList(window.memberList, window.members.size());
        if (!at)
            return std::nullopt;
        return window.members[*at].slot;
    };
    if (UI::ClickTheButton(window.memberButtons[GuildAssignButton], globals, context.mouseX, context.mouseY))
    {
        if (const auto member = chosen(); member && member->status != MasterStatus)
        {
            guild.chosenStatus = member->status;
            ChooseTheMember(guild, GuildTextOf(member->name));
            // 0x6504b6: 0x6600d0 sin renglones ni botones (0xd5 × 0x2b) con lo que da en +0x428, y 0x6602b0 con el paso
            // y el dibujo de la ventana de los cargos. Con otro abierto, 0x6600f9 corta antes (el original le pondría
            // los ganchos al abierto).
            if (auto opened = OpenTheGuildDialog(context, {}, 0, UI::PanelDialogOwner::None, "0x6504b6");
                !opened.HasValue())
                return opened;
            UI::HookThePanelDialog(runtime.panels.delegated.panelDialog, UI::PanelDialogHooks::GuildPositions);
        }
    }
    if (UI::ClickTheButton(window.memberButtons[GuildDismissButton], globals, context.mouseX, context.mouseY))
    {
        if (const auto member = chosen();
            member && (member->status == AssistantStatus || member->status == BattleMasterStatus))
        {
            const std::string_view name = GuildTextOf(member->name);
            ChooseTheMember(guild, name);
            if (auto asked = AskAboutTheMember(context, name, DismissText, UI::PanelDialogOwner::GuildDismiss);
                !asked.HasValue())
                return asked;
        }
    }
    if (UI::ClickTheButton(window.memberButtons[GuildKickButton], globals, context.mouseX, context.mouseY))
    {
        if (const auto member = chosen(); member && member->status != MasterStatus)
        {
            const std::string_view name = GuildTextOf(member->name);
            auto index = FindTheMember(guild, name);
            if (!index.HasValue())
                return Core::Unexpected(std::move(index).Error());
            guild.kickIndex = index.Value();
            if (auto asked = AskAboutTheMember(context, name, KickText, UI::PanelDialogOwner::GuildKick);
                !asked.HasValue())
                return asked;
        }
    }
    return {};
}

// 0x650350: la pestaña de los miembros.
Core::Expected<void, std::string> StepTheMembersTab(const GuildWindowStepContext& context)
{
    if (!context.keyEdge)
        return Core::Unexpected(std::string("0x650387: PgUp y PgDn de la lista de los miembros sin el teclado"));
    WorldRuntime& runtime = context.world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    UI::OldList& list = window.memberList;
    StepTheGuildList(context, list, window.members);
    if (context.keyEdge(PageUpKey))
        UI::ScrollTheList(list, window.members.size(), -list.rows);
    if (context.keyEdge(PageDownKey))
        UI::ScrollTheList(list, window.members.size(), list.rows);
    if (context.world.Actors()[runtime.hero].guildStatus != MasterStatus)
        return {};
    return StepTheMasterButtons(context);
}

// El gremio (+0xd) y la alianza (+4) de la entrada del héroe (+0x1f2), sin mirar el −1 (Q-C1281).
struct HeroGuildNames
{
    std::string_view guild;
    std::string_view alliance;
};

Core::Expected<HeroGuildNames, std::string> HeroGuildNamesOf(WorldState& world, std::string_view where)
{
    const auto index = static_cast<std::int16_t>(world.Actors()[world.Runtime().hero].word1f2);
    const GuildState& guild = world.Runtime().guild;
    const auto name = GuildNameAt(guild, index);
    const auto alliance = UnionNameAt(guild, index);
    if (!name || !alliance)
        return Core::Unexpected(std::format("{}: el gremio {} del héroe cae fuera de la tabla", where, index));
    return HeroGuildNames{.guild = *name, .alliance = *alliance};
}

// 0x64fdb0..0x64fddf y 0x64de99..0x64ded4: el diálogo 0x7d del juego (0x7d3a10c), 0x553570(0), el campo del chat
// cerrado (0x6c8e68) y un solo campo para Tab (0x6c8e6c). 0x6c8e78 (el tope de la entrada sin EDIT, de [0x7d3a0a4]) y
// 0x77f31c0 (sus marcas) son de la entrada sin EDIT, que MuRet no tiene.
void OpenTheNumberDialog(WorldRuntime& runtime)
{
    runtime.dialogs.current = NumberDialog;
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.ui.chatOpen = false;
    runtime.chat.input.fieldCount = NumberDialogFields;
}

// 0x64fc00: la pestaña 0.
Core::Expected<void, std::string> StepTheNoticeTab(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    StepTheGuildList(context, window.noticeList, window.notice);
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    if (!UI::ClickTheButton(window.tabButtons[GuildLeaveButton], globals, context.mouseX, context.mouseY))
        return {};
    const ActorState& hero = context.world.Actors()[runtime.hero];
    if (hero.guildStatus == MasterStatus)
    {
        auto names = HeroGuildNamesOf(context.world, "0x64fc55");
        if (!names.HasValue())
            return Core::Unexpected(std::move(names).Error());
        if (names.Value().guild == names.Value().alliance)
            return TellTheMaster(context, UnionMasterText, "0x64fcd5");
        std::array<std::string, DisbandTexts.size()> lines{};
        for (std::size_t line = 0; line < lines.size(); ++line)
        {
            auto text = TextOf(context.texts, DisbandTexts[line], "0x64fce7");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            lines[line] = std::string(text.Value());
        }
        return OpenTheGuildDialog(context, lines, UI::PanelDialogConfirm, UI::PanelDialogOwner::GuildDisband,
                                  "0x64fcd5");
    }
    // 0x64fd9c: 0x64faa0 con el nombre del héroe (+0x1d9).
    auto index = FindTheMember(guild, HeroNameOf(hero));
    if (!index.HasValue())
        return Core::Unexpected(std::move(index).Error());
    guild.kickMember = index.Value();
    OpenTheNumberDialog(runtime);
    return {};
}

// 0x650c10: la pestaña de la alianza.
Core::Expected<void, std::string> StepTheUnionTab(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    StepTheGuildList(context, window.unionList, window.unions);
    const ActorState& hero = context.world.Actors()[runtime.hero];
    if (hero.guildStatus != MasterStatus)
        return {};
    auto names = HeroGuildNamesOf(context.world, "0x650c74");
    if (!names.HasValue())
        return Core::Unexpected(std::move(names).Error());
    if (names.Value().alliance.empty())
        return {};
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    if (UI::ClickTheButton(window.tabButtons[GuildUnionLeaveButton], globals, context.mouseX, context.mouseY))
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
            // 0x651277..0x651290: sprintf con el 1387 sin argumentos y 0x555190("", texto, 1).
            auto format = TextOf(context.texts, UnionLeftText, "0x65127a");
            if (!format.HasValue())
                return Core::Unexpected(std::move(format).Error());
            if (context.measure == nullptr)
                return Core::Unexpected(std::string("0x651290: el aviso de salir de la alianza sin medidor"));
            if (auto added = AddGameMessage(runtime, *context.measure, context.screen, {}, UI::Sprintf(format.Value()),
                                            UnionLeftType);
                !added.HasValue())
                return added;
        }
    }
    if (!UI::ClickTheButton(window.tabButtons[GuildUnionKickButton], globals, context.mouseX, context.mouseY))
        return {};
    const auto at = UI::SelectedInTheList(window.unionList, window.unions.size());
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
    const std::array<std::string, 2> lines{UI::PlaceArguments(format.Value(), arguments), std::string(second.Value())};
    ChooseTheMember(guild, name);
    return OpenTheGuildDialog(context, lines, UI::PanelDialogConfirm, UI::PanelDialogOwner::UnionKick, "0x6513d8");
}

// 0x651bbf..0x651c05: el paso de la pestaña.
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

// 0x651f1c..0x652275: las rayas de 0x621aa0 bajo las pestañas que no están elegidas, 0x459ec0, la negrita en blanco
// sobre 0 y cada pestaña: blanca la elegida (2 más alta y más arriba) y en 0,6 las otras, con su texto.
Core::Expected<void, std::string> DrawTheTabs(GameHudContext& context, std::int32_t tab, std::int32_t x, std::int32_t y)
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
    // 0x459ec0: glColor4f(1, 1, 1, 1) y la textura.
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

bool PassesTheGuildWindowGate(const UI::OldWindowBase& base, UI::GameUiState& ui, std::int32_t mouseX,
                              std::int32_t mouseY)
{
    if (!UI::MouseInRect(mouseX, mouseY, base.x, base.y, base.width, base.height, base.rectMode))
        return true;
    if (ui.hovered == 0)
    {
        if (ui.capture != 0 && ui.capture != base.id)
            return false;
        ui.hovered = base.id;
    }
    return base.state != UI::ShownWindow || ui.hovered == base.id || ui.capture == base.id;
}

Core::Expected<void, std::string> StepTheGuildWindow(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    const UI::OldWindowBase& base = window.base;
    if (!PassesTheGuildWindowGate(base, runtime.ui, context.mouseX, context.mouseY))
        return {};
    if (UI::MouseInRect(context.mouseX, context.mouseY, base.x, base.y, base.width, MouseHeight, TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    if (runtime.movement.pendingClick && UI::MouseInRect(context.mouseX, context.mouseY, base.x + CloseLeft,
                                                         base.y + CloseTop, CloseSize, CloseSize, TopLeftRect))
    {
        runtime.movement.pendingClick = false;
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
        runtime.lastSoundId = ClickSound; // 0x41ed10 (CL10)
        // 0x6518e3: la vtable + 0x2c, 0x6522f0. El paso sigue con la ventana cerrada.
        if (auto closed = CloseTheGuildWindow(context.world, context.send); !closed.HasValue())
            return closed;
    }
    if (context.world.Actors()[runtime.hero].guildStatus == NoGuildStatus)
        return {};
    if (runtime.movement.leftHeld)
    {
        const auto inTab = [&](std::int32_t tab)
        {
            return window.tab != tab &&
                   UI::MouseInRect(context.mouseX, context.mouseY,
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

void AddTheGuildMember(WorldState& world, const GuildMemberSlot& slot)
{
    if (slot.name[0] == '\0')
        return;
    WorldRuntime& runtime = world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    UI::OldList& list = window.memberList;
    const std::string_view name = GuildTextOf(slot.name);
    if (UI::ListCount(list, window.members.size()) == 0)
    {
        // 0x46c955: strcmp con el nombre del héroe (+0x1d9, diez bytes y lo que sigue: *inferido*, hasta el primer 0).
        const auto& heroName = world.Actors()[runtime.hero].name;
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
                                                    Render::Text::FontKind font, App::PixelSize screen,
                                                    std::string_view text)
{
    constexpr int NoticeWidth = 0x6e;
    constexpr int NoticeLines = 5;
    auto wrapped = UI::WrapByWidth(text, measure, font, screen, NoticeWidth, NoticeLines, 0);
    if (!wrapped.HasValue())
        return Core::Unexpected(std::move(wrapped).Error());
    UI::OldList& list = window.noticeList;
    for (const std::string& line : wrapped.Value().lines)
    {
        // 0x46bca0: sin texto o vacío, nada; strcpy a 0x575d234 (+0 de 0x575d230 nadie lo escribe: 0).
        if (line.empty())
            continue;
        PutFirst(list, window.notice, GuildNoticeRow{.toggled = 0, .text = line});
    }
    const std::size_t size = window.notice.size();
    UI::ScrollTheList(list, size, UI::ListCount(list, size) - list.rows);
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
    // 0x46d93d: strncpy(+0x44, nombre, 8) y +0x4c en 0.
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

Core::Expected<void, std::string> StepTheGuildPositions(WorldState& world, const GuildSender& send, std::int32_t mouseX,
                                                        std::int32_t mouseY, const UI::PanelDialogAnswer& answer)
{
    WorldRuntime& runtime = world.Runtime();
    GuildState& guild = runtime.guild;
    GuildPositionsState& positions = guild.positions;
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    if (UI::MouseInRect(mouseX, mouseY, positions.x, positions.y, PositionsMouseWidth, PositionsMouseHeight,
                        TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    // 0x64e64e: con el flanco del clic (0x7d366d4, que no gasta), las dos cajas eligen el cargo.
    if (runtime.movement.pendingClick)
    {
        if (UI::MouseInRect(mouseX, mouseY, positions.x + PositionsChoiceLeft, positions.y + AssistantChoiceTop,
                            PositionsChoiceWidth, PositionsChoiceHeight, TopLeftRect))
            positions.choice = AssistantChoice;
        if (UI::MouseInRect(mouseX, mouseY, positions.x + PositionsChoiceLeft, positions.y + BattleMasterChoiceTop,
                            PositionsChoiceWidth, PositionsChoiceHeight, TopLeftRect))
            positions.choice = BattleMasterChoice;
    }
    UI::PanelDialogState& dialog = runtime.panels.delegated.panelDialog;
    if (UI::ClickTheButton(positions.buttons[GuildPositionsOkButton], globals, mouseX, mouseY))
    {
        // 0x64e6cc: sin cargo (0x7d535b4 en 0), C1 0F E1 01; con uno, 02; con el elegido y los 10 bytes de 0x7d535a8,
        // por el socket (0x6c82a0). Después C1 03 52 (0x64efca) y 0x6603c0.
        if (!send)
            return Core::Unexpected(std::string("0x64e6cc: el E1 de la ventana de los cargos sin el cable"));
        const std::uint8_t type = guild.chosenStatus == 0 ? AppointType : ChangeType;
        Protocol::Guild::CharacterName name{};
        for (std::size_t index = 0; index < name.size(); ++index)
            name[index] = static_cast<std::byte>(guild.chosenName[index]);
        send(Protocol::Guild::WriteAssignStatus(type, static_cast<std::uint8_t>(positions.choice), name));
        send(Protocol::Guild::WriteMemberListRequest());
        if (auto cancelled = UI::CancelThePanelDialog(dialog, answer); !cancelled.HasValue())
            return cancelled;
    }
    // 0x64f196: el otro botón cancela (cerrado, 0x6603c0 no hace nada).
    if (UI::ClickTheButton(positions.buttons[GuildPositionsCancelButton], globals, mouseX, mouseY))
        return UI::CancelThePanelDialog(dialog, answer);
    return {};
}

void LendTheGuildWindowStepTo(UI::PanelContext& panels, const GuildWindowStepContext& context)
{
    panels.guildStep = [context]() { return StepTheGuildWindow(context); };
}

Core::Expected<void, std::string> DrawTheGuildWindow(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const GuildState& guild = runtime.guild;
    const GuildWindowState& window = guild.window;
    const std::int32_t x = window.base.x;
    const std::int32_t y = window.base.y;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    auto title = TextOf(context.texts, TitleText, "0x651cd5");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    context.hud.textColors = TitleColors;
    SelectFont(context, FontKind::Bold);
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const bool inAGuild = hero.guildStatus != NoGuildStatus;
    if (inAGuild)
    {
        const auto index = static_cast<std::int16_t>(hero.word1f2);
        const auto name = GuildNameAt(guild, index);
        if (!name)
            return Core::Unexpected(std::format("0x651da0: el gremio {} del héroe cae fuera de la tabla", index));
        context.hud.textColors.text = ScoreColor;
        WindowDrawing::DrawTitle(context, x + TitleLeft, y + ScoreTop,
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
    if (WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto help = TextOf(context.texts, CloseHelpText, "0x651ee6");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    }
    if (!inAGuild)
        return {};
    if (auto tabs = DrawTheTabs(context, window.tab, x, y); !tabs.HasValue())
        return tabs;
    // 0x652291..0x6522c0: el dibujo de la pestaña.
    return DrawTheGuildTab(context, window.tab);
}
} // namespace Client::Scenes
