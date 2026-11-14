#include "UI/FriendWindowsDraw.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"
#include "UI/FriendModel.h"
#include "UI/FriendWindows.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"
#include "UI/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
using OldDraw::DrawText;
using OldDraw::DrawTheButton;
using OldDraw::RatioX;
using OldDraw::RatioY;
using OldDraw::SetThePalette;
using OldDraw::ToFloat;
using OldDraw::Truncate;
using OldDraw::WhiteWithTexture;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300): las columnas de la lista de amigos (0x7707e58 y
// 0x7707f84) y la de la lista de ventanas (0x7708434), el botón del estado (0x7708ec0) y el lugar de un amigo: fuera de
// línea con 0xff, 0xfe y 0xfc (0x7709370), 0xfd (0x77095c8) o el servidor + 1 (0x77096f4, un formato).
constexpr std::size_t NameColumnText = 1021;
constexpr std::size_t PlaceColumnText = 1022;
constexpr std::size_t WindowColumnText = 1026;
constexpr std::size_t StateButtonText = 1035;
constexpr std::size_t OfflinePlaceText = 1039;
constexpr std::size_t FdPlaceText = 1041;
constexpr std::size_t ServerPlaceText = 1042;
constexpr std::uint8_t FdState = 0xfd;
constexpr std::uint8_t FeState = 0xfe;
constexpr std::uint8_t FfState = 0xff;
// Los colores de 0x6c8e5c: el blanco del encabezado a la vista o bajo el mouse y el de los demás.
constexpr std::uint32_t LitColor = 0xffffffffu;
constexpr std::uint32_t DimColor = 0xffc8dce6u;
// Los índices de 0x491960.
constexpr std::int32_t BlackIndex = 7;
constexpr std::int32_t LineIndex = 2;
constexpr std::int32_t ColumnBarIndex = 5;
constexpr std::int32_t ColumnTickIndex = 14;
// 0x4a2620: la franja de 20 de alto (0x41a00000) arriba del área de adentro, los tres encabezados de 0x35 × 0x13 en
// y + 2 y la raya del 2 desde x + 0x9f, 0x12 debajo de ellos.
constexpr float HeaderBarHeight = 20.0f;
constexpr std::int32_t HeaderY = 2;
constexpr std::int32_t HeaderWidth = 0x35;
constexpr std::int32_t HeaderHeight = 0x13;
constexpr std::int32_t HeaderLineX = 0x9f;
constexpr std::int32_t HeaderLineBelow = 0x12;
// Los títulos, centrados con ((52 − cx / rx) + 0,5) · 0,5 (0x6ba060) y ((24 − cy / ry) + 0,5) · 0,5 (0x6b8800) en
// x + 0, x + 0x36 y x + 0x6b: un punto a la derecha de su encabezado, salvo el primero.
constexpr std::array<std::int32_t, FriendsTabCount> HeaderTextX{0, 0x36, 0x6b};
constexpr double HeaderTextWidth = 52.0;
constexpr double HeaderTextHeight = 24.0;
constexpr double Half = 0.5;
// El texto del botón del estado, 2 antes del borde derecho (0x6b8818), y la casilla 14 (0x6b881c) y 1 (0x6b87d4) más a
// la izquierda y 1 más arriba.
constexpr double StateTextInset = 2.0;
constexpr double CheckboxGap = 14.0;
constexpr double One = 1.0;
// 0x49cc20 y 0x49e7f0: el negro debajo de la lista desde 0x12 bajo su alto (con 18 menos, 0x6b886c); la franja de las
// columnas de 16 (0x41800000) con su raya debajo; la raya sobre los botones, 0x11 bajo el alto de la lista.
constexpr std::int32_t BelowTheList = 0x12;
constexpr double BelowTheListCut = 18.0;
constexpr float ColumnBarHeight = 16.0f;
constexpr std::int32_t ColumnLineY = 0x10;
constexpr std::int32_t ListLineBelow = 0x11;
// 0x49cc20: la raya entre las columnas desde y + 0x11 con 22 y 17 menos de alto (0x6b8820 y 0x6b8868), y su marca en
// la franja, de 10 (0x41200000) desde y + 3.
constexpr std::int32_t SeparatorY = 0x11;
constexpr double SeparatorCut = 22.0;
constexpr double SeparatorCutAgain = 17.0;
constexpr std::int32_t TickY = 3;
constexpr float TickHeight = 10.0f;
// Los títulos de las columnas en (x + 4, y + 3) y (x + 8, y + 3); el mouse las toma de 0x13 de alto.
constexpr std::int32_t ColumnTextX = 4;
constexpr std::int32_t WindowColumnTextX = 8;
constexpr std::int32_t ColumnTextY = 3;
constexpr std::int32_t ColumnHeight = 0x13;
// 0x461cd0 y 0x462a60: el nombre en x + 3 y el lugar 4 después de la columna; el título de una ventana en x + 8.
constexpr std::int32_t FriendNameX = 3;
constexpr std::int32_t PlaceGap = 4;
constexpr std::int32_t WindowTitleX = 8;

std::string TextAt(const OldWindowDrawView& view, std::size_t index)
{
    return view.texts != nullptr ? view.texts->At(index) : std::string();
}

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x461e31..0x461ebd: sprintf del texto del lugar en el buffer del renglón.
std::string PlaceOf(const OldWindowDrawView& view, std::uint8_t state)
{
    if (state == FfState || state == FeState || state == AllOfflineState)
        return Sprintf(TextAt(view, OfflinePlaceText));
    if (state == FdState)
        return Sprintf(TextAt(view, FdPlaceText));
    return Sprintf(TextAt(view, ServerPlaceText), static_cast<std::int32_t>(state) + 1);
}

// 0x461cd0, la vtable + 0x80 de la lista de amigos: el renglón (DrawTheChosenRow), el nombre (sprintf "%s",
// 0x6c7084) y, con +0x130 en 1, el lugar. Da 1.
std::int32_t DrawTheFriendRow(const OldWindowDrawContext& context, const OldWindow& tab, std::int32_t row,
                              std::size_t at, const OldWindowDrawView& view)
{
    const OldList& list = tab.tab.list;
    const std::size_t size = tab.tab.friends.size();
    OldDraw::DrawTheChosenRow(context, list, size, row, view.focusedList);
    const FriendEntry& entry = tab.tab.friends[at];
    const std::int32_t x = list.base.x + FriendNameX;
    const std::int32_t y = ListRowY(list, size, row);
    // 0x465950 directo, sin el corte de 0x5542f0 para el texto vacío: vacío, tampoco dibuja nada.
    static_cast<void>(DrawText(context, x, y, entry.name));
    if (list.headers == 1)
        static_cast<void>(DrawText(context, list.nameWidth + x + PlaceGap, y, PlaceOf(view, entry.state)));
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x462a60, la vtable + 0x80 de la lista de ventanas: el renglón y el título (sprintf "%s"). Da 1.
std::int32_t DrawTheWindowRow(const OldWindowDrawContext& context, const OldWindow& tab, std::int32_t row,
                              std::size_t at, const OldWindowDrawView& view)
{
    const OldList& list = tab.tab.list;
    const std::size_t size = tab.tab.windows.size();
    OldDraw::DrawTheChosenRow(context, list, size, row, view.focusedList);
    static_cast<void>(
        DrawText(context, list.base.x + WindowTitleX, ListRowY(list, size, row), tab.tab.windows[at].title));
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// El principio de 0x49cc20 y 0x49e7f0: con la mezcla normal, el negro debajo de la lista, hasta el borde de abajo;
// después, el blanco con textura.
void DrawTheTabBottom(Render::SpriteBatch& sprites, const OldWindow& tab)
{
    sprites.SetBlend(Render::BlendMode::Alpha);
    SetThePalette(sprites, BlackIndex, 1.0f);
    const std::int32_t listHeight = tab.tab.list.base.height;
    sprites.ColorQuad(
        ToFloat(Steps::ClientX(tab, 0)), ToFloat(Steps::ClientY(tab, listHeight + BelowTheList)),
        ToFloat(Steps::ClientWidth(tab)),
        F32(static_cast<double>(Steps::ClientHeight(tab)) - static_cast<double>(listHeight) - BelowTheListCut));
    WhiteWithTexture(sprites);
}

// La lista de la pestaña (su vtable + 0x14) con la barra (+0x7c) y sus renglones (+0x80), y sus botones (0x45b2e0).
template <typename RowDraw>
void DrawTheTabList(const OldWindowDrawContext& context, OldWindow& tab, std::size_t size,
                    const OldWindowDrawView& view, const RowDraw& rowDraw)
{
    OldList& list = tab.tab.list;
    OldDraw::DrawTheOldList(
        context, list, size,
        OldDraw::OldListDrawing{.head = [&] { OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                .row = [&](std::int32_t row, std::size_t at)
                                { return rowDraw(context, tab, row, at, view); },
                                .tail = {}});
    for (const OldButton& button : tab.tab.buttons)
        DrawTheButton(context, button, view);
}

// Con la mezcla normal: la raya del 2 en y + 16, la franja de las columnas (el 5, de 16) y la raya sobre los botones.
// La pestaña de amigos pone la primera con ClientY(0x10); la de ventanas, con ClientY(0) + 16 en float: lo mismo.
void DrawTheColumnBar(Render::SpriteBatch& sprites, const OldWindow& tab)
{
    sprites.SetBlend(Render::BlendMode::Alpha);
    const float left = ToFloat(Steps::ClientX(tab, 0));
    const float width = ToFloat(Steps::ClientWidth(tab));
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(tab, ColumnLineY)), width, 1.0f);
    SetThePalette(sprites, ColumnBarIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(tab, 0)), width, ColumnBarHeight);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(tab, tab.tab.list.base.height + ListLineBelow)), width, 1.0f);
}

// 0x49cf3c..0x49d099: el título de una columna, en blanco con el mouse sobre ella o con el orden de la columna (+0 del
// modelo, 0x5763518), y si no, con el color que haya (el de los botones de la pestaña).
void DrawTheColumnTitle(const OldWindowDrawContext& context, const OldWindowDrawView& view, std::int32_t hitX,
                        std::int32_t hitY, std::int32_t hitWidth, bool lit, std::int32_t textX, std::int32_t textY,
                        std::string_view text)
{
    if (!OldDraw::MouseIn(view, hitX, hitY, hitWidth, ColumnHeight) && !lit)
    {
        static_cast<void>(DrawText(context, textX, textY, text));
        return;
    }
    context.textColors.text = LitColor;
    static_cast<void>(DrawText(context, textX, textY, text));
    context.textColors.text = DimColor;
}

// 0x49cc20, la vtable + 0x14 de la pestaña de amigos (la ubicación del principio está en
// SettleTheFriendsForTheDraw).
void DrawTheFriendTab(const OldWindowManager& manager, OldWindow& tab, const OldWindowDrawContext& context,
                      const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheTabBottom(sprites, tab);
    sprites.SetBlend(Render::BlendMode::Opaque);
    DrawTheTabList(context, tab, tab.tab.friends.size(), view, DrawTheFriendRow);
    DrawTheColumnBar(sprites, tab);
    const OldList& list = tab.tab.list;
    const std::int32_t left = Steps::ClientX(tab, 0);
    const float separatorX = F32(static_cast<double>(ToFloat(left)) + static_cast<double>(list.nameWidth));
    sprites.ColorQuad(separatorX, ToFloat(Steps::ClientY(tab, SeparatorY)), 1.0f,
                      F32(static_cast<double>(Steps::ClientHeight(tab)) - SeparatorCut - SeparatorCutAgain));
    SetThePalette(sprites, ColumnTickIndex, 1.0f);
    sprites.ColorQuad(separatorX, ToFloat(Steps::ClientY(tab, TickY)), 1.0f, TickHeight);
    WhiteWithTexture(sprites);
    context.textColors.background = 0;
    const std::int32_t top = Steps::ClientY(tab, 0);
    const std::int32_t textY = Steps::ClientY(tab, ColumnTextY);
    const std::int32_t sort = manager.friendModel.sortMode;
    DrawTheColumnTitle(context, view, left, top, list.nameWidth, sort == ByNameSort, Steps::ClientX(tab, ColumnTextX),
                       textY, TextAt(view, NameColumnText));
    DrawTheColumnTitle(context, view, left + list.nameWidth, top, list.placeWidth, sort == ByStateSort,
                       Steps::ClientX(tab, ColumnTextX) + list.nameWidth, textY, TextAt(view, PlaceColumnText));
    sprites.SetBlend(Render::BlendMode::Opaque);
}

// 0x49e7f0, la vtable + 0x14 de la pestaña de las ventanas.
void DrawTheWindowsTab(OldWindow& tab, const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheTabBottom(sprites, tab);
    DrawTheTabList(context, tab, tab.tab.windows.size(), view, DrawTheWindowRow);
    DrawTheColumnBar(sprites, tab);
    WhiteWithTexture(sprites);
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.textColors = Render::Text::TextColors{.text = DimColor, .background = 0};
    static_cast<void>(DrawText(context, Steps::ClientX(tab, WindowColumnTextX), Steps::ClientY(tab, ColumnTextY),
                               TextAt(view, WindowColumnText)));
    sprites.SetBlend(Render::BlendMode::Opaque);
}

// La y de un texto de la franja de los encabezados: ClientY(0) + ((24 − cy / ry) + 0,5) · 0,5, sin truncar.
double HeaderTextTop(const OldWindow& window, const Render::Text::TextExtent& extent, App::PixelSize screen)
{
    return static_cast<double>(Steps::ClientY(window, 0)) +
           ((HeaderTextHeight - static_cast<double>(extent.height) / RatioY(screen)) + Half) * Half;
}

// 0x4a2827..0x4a2a98: los títulos de las tres pestañas, si tienen, en blanco el de la vista o el del mouse (+0xb4).
void DrawTheHeaderTitles(const OldWindow& window, const OldWindowDrawContext& context)
{
    const App::PixelSize screen = context.sprites.Screen();
    for (std::size_t at = 0; at < FriendsTabCount; ++at)
    {
        const std::optional<std::string>& title = window.tabs[at].frame.title;
        if (!title)
            continue;
        const auto number = static_cast<std::int32_t>(at);
        const bool lit = window.friends.currentTab == number || window.friends.hoverTab == number;
        context.textColors.text = lit ? LitColor : DimColor;
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, *title);
        const std::int32_t y = Truncate(HeaderTextTop(window, extent, screen));
        const std::int32_t x =
            Truncate(static_cast<double>(Steps::ClientX(window, HeaderTextX[at])) +
                     ((HeaderTextWidth - static_cast<double>(extent.width) / RatioX(screen)) + Half) * Half);
        static_cast<void>(DrawText(context, x, y, *title));
    }
}

// 0x4a2a9b..0x4a2bc6: el texto del botón del estado contra el borde derecho y su casilla a la izquierda.
void DrawTheStateButton(const OldWindowManager& manager, const OldWindow& window, const OldWindowDrawContext& context,
                        const OldWindowDrawView& view)
{
    const App::PixelSize screen = context.sprites.Screen();
    const std::string label = TextAt(view, StateButtonText);
    context.textColors.text = DimColor;
    const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, label);
    const double right = static_cast<double>(Steps::ClientX(window, 0) + Steps::ClientWidth(window));
    const double textLeft = right - static_cast<double>(extent.width) / RatioX(screen) - StateTextInset;
    const double top = HeaderTextTop(window, extent, screen);
    static_cast<void>(DrawText(context, Truncate(textLeft), Truncate(top), label));
    // FSTP en float de la x de la casilla antes de restarle 1.
    const float boxLeft = F32(textLeft - CheckboxGap);
    OldDraw::DrawTheCheckbox(context.sprites, Truncate(static_cast<double>(boxLeft) - One), Truncate(top - One),
                             manager.offlineSent == 1);
}
} // namespace

Core::Expected<void, std::string> DrawTheFriendsWindow(OldWindowManager& manager, OldWindow& window,
                                                       const OldWindowDrawContext& context,
                                                       const OldWindowDrawView& view)
{
    SettleTheFriendsForTheDraw(manager, window);
    const std::int32_t current = window.friends.currentTab;
    if (current == FriendTabNumber)
        DrawTheFriendTab(manager, window.tabs[FriendTabNumber], context, view);
    else if (current == WindowsTabNumber)
        DrawTheWindowsTab(window.tabs[WindowsTabNumber], context, view);
    else if (current == LettersTabNumber)
        return Core::Unexpected(
            std::string("0x4a2620: el dibujo de la pestaña de las cartas (0x4a0a30) es del hito 10"));
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    SetThePalette(sprites, BlackIndex, 1.0f);
    const std::int32_t width = Steps::ClientWidth(window);
    sprites.ColorQuad(ToFloat(Steps::ClientX(window, 0)), ToFloat(Steps::ClientY(window, 0)), ToFloat(width),
                      HeaderBarHeight);
    OldDraw::DrawTheTabHeaders(sprites, Steps::ClientX(window, 0), Steps::ClientY(window, HeaderY), HeaderWidth,
                               HeaderHeight, static_cast<std::int32_t>(FriendsTabCount), current);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(ToFloat(Steps::ClientX(window, HeaderLineX)),
                      ToFloat(Steps::ClientY(window, HeaderY) + HeaderLineBelow), ToFloat(width - HeaderLineX), 1.0f);
    WhiteWithTexture(sprites);
    context.textColors.background = 0;
    DrawTheHeaderTitles(window, context);
    DrawTheStateButton(manager, window, context, view);
    return {};
}

void DrawTheAddFriendBox(OldWindowManager& manager, OldWindow& box, const OldWindowDrawContext& context,
                         const OldWindowDrawView& view)
{
    SettleTheFriendsForTheDraw(manager, box);
    for (const OldButton& button : box.add.buttons)
        DrawTheButton(context, button, view);
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    OldEdit& edit = box.add.edit;
    OldDraw::DrawTheOldEdit(context, edit, view.focusedEdit != 0 && view.focusedEdit == edit.base.id);
    context.sprites.SetBlend(Render::BlendMode::Opaque);
}
} // namespace Client::UI
