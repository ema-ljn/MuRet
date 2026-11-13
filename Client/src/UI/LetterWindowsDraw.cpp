#include "UI/LetterWindowsDraw.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"
#include "UI/ChatLog.h"
#include "UI/FriendWindowsDraw.h"
#include "UI/LetterModel.h"
#include "UI/LetterWindows.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"
#include "UI/SprintfFormat.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
using OldDraw::DrawText;
using OldDraw::DrawTheButton;
using OldDraw::RatioX;
using OldDraw::SetThePalette;
using OldDraw::ToFloat;
using OldDraw::Truncate;
using OldDraw::WhiteWithTexture;

// Los índices de 0x491960.
constexpr std::int32_t EdgeIndex = 1;
constexpr std::int32_t LineIndex = 2;
constexpr std::int32_t BlackIndex = 7;
// 0x460550: el renglón `row` en y − 13 · row − 0x10 (su y, el borde de abajo de la lista), el nombre en x + 8 y el
// texto después del ancho del nombre.
constexpr std::int32_t ChatRowHeight = 13;
constexpr std::int32_t ChatRowDrop = 0x10;
constexpr std::int32_t ChatNameX = 8;
constexpr std::string_view NameEnd = ": ";
// 0x460631 y 0x46089b: con 0x6c7a88 en 1, un fondo de 0 en el color (sin mirar el alfa) queda en 0.
constexpr std::uint32_t BackgroundColorMask = 0xffffffu;
// 0x493fc0: los miembros se ven con más de 2 (su vtable + 0x54, 0x4720e0) o con la lista de invitar (+0xb0 en 2); sus
// rayas, 0xa0 y 0x50 antes del borde derecho, 16 (0x6b8f00) más cortas que el área de adentro; la raya del 2, 16 sobre
// el borde de abajo, y el negro, los 15 (0x6b87bc y 0x41700000) de abajo.
constexpr std::int32_t MembersShown = 2;
constexpr std::int32_t WithInvites = 2;
constexpr std::int32_t InvitesSeparatorX = 0xa0;
constexpr std::int32_t MembersSeparatorX = 0x50;
constexpr double SeparatorCut = 16.0;
constexpr double BottomLineRise = 16.0;
constexpr double BottomBarRise = 15.0;
constexpr float BottomBarHeight = 15.0f;
// 0x491ba0: los filos del 2 en x y x + 4 (0x6b8a44), y el medio del 1 de 3 de ancho (0x40400000) en x + 1
// (0x6b87d4).
constexpr double SeparatorFarEdge = 4.0;
constexpr double SeparatorMiddleX = 1.0;
constexpr float SeparatorMiddleWidth = 3.0f;

// 0x465770: el renglón del cuerpo en x + 10, en 0xffc8dce6 sin fondo.
constexpr std::int32_t BodyTextX = 10;
constexpr std::uint32_t BodyTextColor = 0xffc8dce6u;
// 0x499e44..0x49a0f4: el retrato ocupa los 0x78 (0x6b86c8, 120) de la derecha; abajo, la raya del 2 a 20 (0x6b86d0)
// del borde y el negro de 18 (0x41900000) desde 19 (0x6b8870), y la raya del costado de 19 (0x41980000); arriba, la
// raya del 2 en y + 0xe y el negro de 14 (0x41600000).
constexpr std::int32_t ReadPortraitShown = 2;
constexpr std::int32_t PortraitX = 0x78;
constexpr double PortraitWidth = 120.0;
constexpr double ButtonsLineRise = 20.0;
constexpr double ButtonsBarRise = 19.0;
constexpr float ButtonsBarHeight = 18.0f;
constexpr float PortraitLineHeight = 19.0f;
constexpr std::int32_t HeaderLineY = 0xe;
constexpr float HeaderBarHeight = 14.0f;
// 0x49a10e..0x49a164: el texto 1014 en (x + 3, y + 3).
constexpr std::size_t ReadHeaderText = 1014;
constexpr std::int32_t ReadHeaderX = 3;
constexpr std::int32_t ReadHeaderY = 3;

// 0x498537..0x4986e8: arriba, el negro de 29 (0x41e80000) con las rayas del 2 en y + 0xe y en y + 0x1d; abajo, el negro
// de 19 (0x41980000) desde 19 (0x6b8870) sobre el borde y su raya.
constexpr float WriteHeaderHeight = 29.0f;
constexpr std::int32_t WriteToLineY = 0xe;
constexpr std::int32_t WriteSubjectLineY = 0x1d;
constexpr double WriteBottomRise = 19.0;
constexpr float WriteBottomHeight = 19.0f;
// 0x4986f5..0x49876d: el 1000 y el 1005 en x + 3 y en y + 3 y y + 0x12, en 0xffc8dce6, en la caja del ancho del 1000.
constexpr std::uint32_t WriteTextColor = 0xffc8dce6u;
constexpr std::size_t ToText = 1000;
constexpr std::size_t SubjectText = 1005;
constexpr std::int32_t WriteLabelX = 3;
constexpr std::int32_t ToLabelY = 3;
constexpr std::int32_t SubjectLabelY = 0x12;
// 0x4987c7: los botones +0xb54 y +0xbdc, con +0xb0 en 1.
constexpr std::int32_t WithThePortrait = 1;
// +0xa44, +0xacc, +0xb54 y +0xbdc (0x48f4ee).
constexpr std::size_t WriteButtonCount = 4;
// 0x4670ba..0x4670d0: un texto vacío mide el "0" de 0x6c7ae4.
constexpr std::string_view EmptyTextMeasure = "0";

// 0x4a5490 y 0x4a5750: los índices de 0x491960 del fondo (la sombra, con su 179), del borde, del elegido y de los que
// esperan; el margen del renglón (0x6b8a44), su franja (1 más abajo, de 3 más, 0x6b86b8, desde x + 1 y 2 menos ancha)
// y el texto en x + 2 y 3 más abajo; el alfa del texto, ftol(parpadeo · 255) (0x6b9f98), sobre 0xc8dce6 o sobre negro.
// 0x4a5120: los íconos de la barra. win_letter.jpg (0x64d) de 15 × 9 con 0,9375 × 0,5625; win_man.jpg (0x64e) de
// 25 × 14 con 0,78125 × 0,4375 (con la de amigos, desde v = 0,4375); win_push.jpg (0x64f) con w · 1/64 y h · 1/32
// (0x6ba064 y 0x6b8880).
constexpr std::uint16_t LetterSlot = 0x64d;
constexpr std::uint16_t ManSlot = 0x64e;
constexpr std::uint16_t PushSlot = 0x64f;
constexpr float LetterWidth = 15.0f;
constexpr float LetterHeight = 9.0f;
constexpr float LetterDu = 0.9375f;
constexpr float LetterDv = 0.5625f;
constexpr float ManWidth = 25.0f;
constexpr float ManHeight = 14.0f;
constexpr float ManDu = 0.78125f;
constexpr float ManDv = 0.4375f;
constexpr float PushDu = 0.015625f;
constexpr float PushDv = 0.03125f;
constexpr std::int32_t BarBlinkPeriod = 0x18;
constexpr std::int32_t BarBlinkLit = 0xc;
constexpr std::int32_t LetterBlinkStep = 0xb;
constexpr std::int32_t LetterBlinks = 5;
constexpr std::int32_t ListBackIndex = 3;
constexpr std::int32_t ListEdgeIndex = 15;
constexpr std::int32_t ChosenChatIndex = 0x10;
constexpr std::int32_t WaitingChatIndex = 0x11;
constexpr double ChatRowMargin = 4.0;
constexpr double ChatBandDrop = 1.0;
constexpr double ChatBandExtra = 3.0;
constexpr std::int32_t ChatBandX = 1;
constexpr std::int32_t ChatBandCut = 2;
constexpr std::int32_t ChatTextX = 2;
constexpr double ChatTextDrop = 3.0;
constexpr double TextAlphaScale = 255.0;
constexpr std::uint32_t ChatRowColor = 0xc8dce6u;
constexpr std::uint32_t AlphaShift = 24;
constexpr std::uint32_t ByteMask = 0xffu;
// 0x4a5943..0x4a5a68: strncpy de 0x100 del título, strtok con "," (0x6c7b88) y el resto en w − 8, un renglón.
constexpr std::size_t ChatTitleCopy = 0x100;
constexpr char TitleDelimiter = ',';
constexpr std::int32_t ChatTextCut = 8;
// 0x4a5ac2..0x4a5b5a: con más de dos miembros, win_mark.tga de 4 × 6 (0, 0, 0,5, 0,75) en x + w − 7 (0x6b8878) y 5
// más abajo (0x6b87f8) que el renglón, con glColor3f(255, 0, 0) y después glColor3f(255, 255, 255).
constexpr std::int32_t ManyMembers = 2;
constexpr std::uint16_t MarkSlot = 0x64c;
constexpr double MarkInset = 7.0;
constexpr double MarkDrop = 5.0;
constexpr float MarkWidth = 4.0f;
constexpr float MarkHeight = 6.0f;
constexpr float MarkDu = 0.5f;
constexpr float MarkDv = 0.75f;
constexpr float FullColor = 255.0f;

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x46057d..0x460631 con 0x5763510 + 8 en 0 (el de este main.exe, como el registro del chat de 0x45cbb0): los colores
// del nombre por el tipo (+0x110); 0x460550 no tiene caso para 6 o más y quedan los de antes.
void ChatNameColors(std::int32_t kind, Render::Text::TextColors& colors)
{
    switch (kind)
    {
    case 0:
        colors = {.text = 0xff000000u, .background = 0x32c8ffu};
        break;
    case 1:
        colors = {.text = 0xffff9664u, .background = 0};
        break;
    case 2:
        colors = {.text = 0xff001effu, .background = 0};
        break;
    case 3:
        colors = {.text = 0xffefdccdu, .background = 0};
        break;
    case 4:
        colors = {.text = 0xff000000u, .background = 0xffc800u};
        break;
    case 5:
        colors = {.text = 0xff000000u, .background = 0x96ff00u};
        break;
    default:
        break;
    }
}

// 0x460819..0x46089b: los del texto, con el 1 y el 3 más claros.
void ChatTextColors(std::int32_t kind, Render::Text::TextColors& colors)
{
    if (kind == 1)
        colors = {.text = 0xff46a5d2u, .background = 0};
    else if (kind == 3)
        colors = {.text = 0xffc8dce6u, .background = 0};
    else
        ChatNameColors(kind, colors);
}

// 0x460550, la vtable + 0x80 de los renglones del chat (0x45f240, vtable 0x6b9030) con 0x5763510 + 8 en 0: con la
// mezcla normal, el nombre con ": " (sprintf "%s: ", 0x6c7aa8) y su ancho en 640 × 480 (GetTextExtentPointA con el
// DC, ftol(cx / rx)), y el texto (sprintf "%s") después, con 0x465950 directo. Da 1.
std::int32_t DrawTheChatRow(const OldWindowDrawContext& context, const OldList& list, const ChatLine& line,
                            std::int32_t row, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    const std::int32_t y = list.base.y - row * ChatRowHeight - ChatRowDrop;
    std::int32_t nameWidth = 0;
    if (!line.name.empty())
    {
        ChatNameColors(line.kind, context.textColors);
        const std::string name = line.name + std::string(NameEnd);
        static_cast<void>(DrawText(context, list.base.x + ChatNameX, y, name));
        nameWidth =
            Truncate(static_cast<double>(context.text.Measure(context.dcFont, name).width) / RatioX(sprites.Screen()));
    }
    ChatTextColors(line.kind, context.textColors);
    if (view.chatPlacement == 1 && (context.textColors.background & BackgroundColorMask) == 0)
        context.textColors.background = 0;
    static_cast<void>(DrawText(context, list.base.x + nameWidth + ChatNameX, y, line.text));
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x45f9c0, la vtable + 0x14 de los renglones (0x47a810 con los elementos de 0x11c del registro del chat): la barra
// (0x45fac0, igual a 0x461890) y los renglones. Con +0xc8 en 1 (0x45f240) la lista muestra su segundo deque (+0xcc),
// que 0x460130 llena con push_front: el primero es el último pedazo del renglón más nuevo, partido en el ancho de la
// lista menos 0x1e (0x459c70). El iterador corre hasta el final de ese deque (+0xe8, 0x45f9fd).
void DrawTheChatLines(const OldWindowDrawContext& context, OldChatPart& chat, const OldWindowDrawView& view)
{
    OldList& list = chat.lines;
    const std::size_t size = chat.wrappedRows.size();
    OldDraw::DrawTheOldList(
        context, list, size,
        OldDraw::OldListDrawing{.head = [&] { OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                .row = [&](std::int32_t row, std::size_t at)
                                { return DrawTheChatRow(context, list, chat.wrappedRows[at], row, view); },
                                .tail = {}});
}

// 0x491ba0(x, y, h): la raya entre dos listas, sin cambiar la mezcla: los filos del 2 y el medio del 1.
void DrawTheListSeparator(Render::SpriteBatch& sprites, float x, float y, float height)
{
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(x, y, 1.0f, height);
    sprites.ColorQuad(F32(static_cast<double>(x) + SeparatorFarEdge), y, 1.0f, height);
    SetThePalette(sprites, EdgeIndex, 1.0f);
    sprites.ColorQuad(F32(static_cast<double>(x) + SeparatorMiddleX), y, SeparatorMiddleWidth, height);
}

// La raya a `fromRight` del borde derecho del área de adentro: FILD de x + w − fromRight y de y, y h − 16 en float.
void DrawTheMembersSeparator(Render::SpriteBatch& sprites, const OldWindow& chat, std::int32_t fromRight)
{
    DrawTheListSeparator(sprites, ToFloat(Steps::ClientX(chat, 0) + Steps::ClientWidth(chat) - fromRight),
                         ToFloat(Steps::ClientY(chat, 0)),
                         F32(static_cast<double>(Steps::ClientHeight(chat)) - SeparatorCut));
}

// La y de un quad desde el borde de abajo: FILD h, FADD de y en float y FSUB.
float FromTheBottom(const OldWindow& window, double rise)
{
    return F32(static_cast<double>(Steps::ClientHeight(window)) +
               static_cast<double>(ToFloat(Steps::ClientY(window, 0))) - rise);
}

std::string TextAt(const OldWindowDrawView& view, std::size_t index)
{
    return view.texts != nullptr ? view.texts->At(index) : std::string();
}

// La ventana `id` de la lista minimizada (0x490a00 y 0x45a9c0 con 5). Una que no está, que el original no ve (lee la
// de 0), no.
bool IsMinimized(OldWindowManager& manager, std::int32_t id)
{
    const OldWindow* window = FindTheOldWindow(manager, id);
    return window != nullptr && window->base.state == MinimizedWindow;
}

// 0x4a5943..0x4a5a68: el texto del renglón de un chat, del título de su ventana (+0x98, strncpy de 0x100): strtok(","),
// el primer pedazo sin las comas de adelante; si es más largo que el 994, lo que sigue al 994 (y al 995 si empieza
// con él) cortado por 0x459c70 en w − 8 y un renglón (de 0x40 bytes: los títulos de MuRet no llegan); si no, el 995.
// Sin pedazo (un título de comas, que el original no ve: strlen de NULL) o pasando el final del pedazo, MuRet no lee
// nada (*inferido*).
Core::Expected<std::string, std::string> TextOfTheChatRow(const OldWindow* chat, const OldWindow& window,
                                                          const OldWindowDrawContext& context,
                                                          const OldWindowDrawView& view)
{
    std::string title = chat != nullptr ? chat->frame.title.value_or(std::string()) : std::string();
    title = title.substr(0, std::min(title.find('\0'), ChatTitleCopy));
    const std::size_t from = std::min(title.find_first_not_of(TitleDelimiter), title.size());
    const std::string_view token = std::string_view(title).substr(from, title.find(TitleDelimiter, from) - from);
    const std::string chatting = TextAt(view, ChatTitleText);
    const std::string closed = TextAt(view, ChatClosedText);
    if (token.size() <= chatting.size())
        return closed;
    const std::size_t skip = token.starts_with(closed) ? closed.size() + chatting.size() : chatting.size();
    auto wrapped = WrapByWidth(token.substr(std::min(skip, token.size())), context.text, context.dcFont,
                               context.sprites.Screen(), window.base.width - ChatTextCut, 1, 0);
    if (!wrapped.HasValue())
        return Core::Unexpected(std::move(wrapped).Error());
    return wrapped.Value().lines.empty() ? std::string() : wrapped.Value().lines.front();
}

// 0x4a5750, desde 0x4a5490 con el blanco con textura: los renglones de los chats de abajo hacia arriba, el n de
// (renglón + 4) · n sobre +0x104 (uno minimizado no cuenta).
Core::Expected<void, std::string> DrawTheChatRowsOfTheList(OldWindowManager& manager, const OldWindow& window,
                                                           const OldWindowDrawContext& context,
                                                           const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OldWindowListPart& list = window.list;
    const OldWindowBase& base = window.base;
    const double step = static_cast<double>(list.rowHeight) + ChatRowMargin;
    const double bottom = static_cast<double>(list.bottom);
    std::int32_t row = 1;
    for (std::size_t at = 0; at < list.chats.size(); ++at, ++row)
    {
        const std::int32_t id = list.chats[at];
        if (IsMinimized(manager, id))
        {
            --row;
            continue;
        }
        const auto alpha =
            static_cast<std::uint32_t>(Truncate(static_cast<double>(list.blink) * TextAlphaScale)) & ByteMask;
        bool waiting = false;
        for (const std::int32_t other : list.waiting)
            if (other == id)
                waiting = true;
        const bool chosen = list.cursor == at;
        const double rows = static_cast<double>(row);
        std::uint32_t color = (alpha << AlphaShift) + ChatRowColor;
        if (chosen || waiting)
        {
            sprites.SetBlend(Render::BlendMode::Alpha);
            SetThePalette(sprites, chosen ? ChosenChatIndex : WaitingChatIndex, list.blink);
            sprites.ColorQuad(ToFloat(base.x + ChatBandX), F32(bottom - step * rows + ChatBandDrop),
                              ToFloat(base.width - ChatBandCut),
                              F32(static_cast<double>(list.rowHeight) + ChatBandExtra));
            WhiteWithTexture(sprites);
            color = alpha << AlphaShift;
        }
        context.textColors = Render::Text::TextColors{.text = color, .background = 0};
        const OldWindow* chat = FindTheOldWindow(manager, id);
        auto text = TextOfTheChatRow(chat, window, context, view);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        // FILD de n y FSTP en float: el mismo n.
        static_cast<void>(DrawText(
            context, base.x + ChatTextX,
            Truncate(bottom - step * static_cast<double>(static_cast<float>(row)) + ChatTextDrop), text.Value()));
        if (chat == nullptr || ListCount(chat->chat.users, chat->chat.members.size()) <= ManyMembers)
            continue;
        sprites.SetColor(Render::Color{.red = FullColor, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f});
        OldDraw::Quad(sprites, MarkSlot, F32(static_cast<double>(base.width + base.x) - MarkInset),
                      F32(bottom - step * rows + MarkDrop), MarkWidth, MarkHeight, 0.0f, 0.0f, MarkDu, MarkDv);
        sprites.SetColor(Render::Color{.red = FullColor, .green = FullColor, .blue = FullColor, .alpha = 1.0f});
    }
    return {};
}

// 0x5542f0(x, y, texto, caja, 3, 0) → 0x465950 → 0x467060 con el modo 3 (0x467134..0x467144): TextOutA pone el texto
// en la columna caja − cx del DIB, que tiene la caja de ancho: alineado a la derecha de la caja, cortado a la izquierda
// si es más ancho. Sin texto y sin caja no dibuja nada (0x5542f8..0x55430f); vacío y con caja, el fondo solo.
void DrawRightAligned(const OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                      std::int32_t box)
{
    if (text.empty() && box == 0)
        return;
    const std::int32_t width = context.text.Measure(context.dcFont, text.empty() ? EmptyTextMeasure : text).width;
    context.text.DrawColumns(x, y, text, Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors},
                             width - box, box);
}

// 0x465770, la vtable + 0x80 de la lista del cuerpo (0x464620, vtable 0x6b93d4): con la mezcla normal, el renglón
// (sprintf "%s", 0x6c7084) en x + 10 y en su y (0x465710, igual a 0x461c70), con 0x465950 directo. Con 0x6c7a88 en 1
// vuelve a poner el fondo en 0, que ya está en 0 (0x4657a5..0x4657be). Da 1.
std::int32_t DrawTheBodyRow(const OldWindowDrawContext& context, const OldList& list, std::size_t size,
                            const std::string& line, std::int32_t row)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.textColors = Render::Text::TextColors{.text = BodyTextColor, .background = 0};
    static_cast<void>(DrawText(context, list.base.x + BodyTextX, ListRowY(list, size, row), line));
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x464d90, la vtable + 0x14 del cuerpo (0x47a810 con los elementos de 0x3f0): la barra (0x464e90, igual a 0x461890) y
// los renglones. Como la de los renglones del chat, muestra su segundo deque, que 0x465450 llena con push_front y
// partido.
void DrawTheBodyLines(const OldWindowDrawContext& context, OldLetterPart& letter, const OldWindowDrawView& view)
{
    OldList& list = letter.lines;
    const std::size_t size = letter.wrappedRows.size();
    OldDraw::DrawTheOldList(
        context, list, size,
        OldDraw::OldListDrawing{.head = [&] { OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                .row = [&](std::int32_t row, std::size_t at)
                                { return DrawTheBodyRow(context, list, size, letter.wrappedRows[at], row); },
                                .tail = {}});
}

// Lo que mide el 9 de los renglones al volver a partirlos durante el dibujo: GetTextExtentPointA sobre el DC, con la
// fuente que tenga en ese momento.
OldWindowMeasure MeasureOfTheDraw(const OldWindowDrawContext& context)
{
    return OldWindowMeasure{.measure = context.text, .dcFont = context.dcFont, .screen = context.sprites.Screen()};
}
} // namespace

Core::Expected<void, std::string> DrawTheChatWindow(OldWindowManager& manager, OldWindow& chat,
                                                    const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto settled = SettleTheLetterKindsForTheDraw(manager, chat, MeasureOfTheDraw(context)); !settled.HasValue())
        return settled;
    OldChatPart& part = chat.chat;
    DrawTheChatLines(context, part, view);
    if (ListCount(part.users, part.members.size()) > MembersShown || part.mode >= WithInvites)
    {
        DrawTheFriendList(context, part.users, part.members, view);
        if (part.mode >= WithInvites)
        {
            DrawTheFriendList(context, part.invite, part.invitable, view);
            sprites.SetBlend(Render::BlendMode::Alpha);
            DrawTheMembersSeparator(sprites, chat, InvitesSeparatorX);
        }
    }
    if (ListCount(part.users, part.members.size()) > MembersShown || part.mode >= WithInvites)
        DrawTheMembersSeparator(sprites, chat, MembersSeparatorX);
    sprites.SetBlend(Render::BlendMode::Alpha);
    const float left = ToFloat(Steps::ClientX(chat, 0));
    const float width = ToFloat(Steps::ClientWidth(chat));
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(chat, BottomLineRise), width, 1.0f);
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(chat, BottomBarRise), width, BottomBarHeight);
    WhiteWithTexture(sprites);
    DrawTheButton(context, part.inviteButton, view);
    OldEdit& edit = part.edit;
    OldDraw::DrawTheOldEdit(context, edit, view.focusedEdit != 0 && view.focusedEdit == edit.base.id, view);
    if (part.mode >= WithInvites)
        DrawTheButton(context, part.sendButton, view);
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

Core::Expected<void, std::string> DrawTheReadWindow(OldWindowManager& manager, OldWindow& read,
                                                    const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto settled = SettleTheLetterKindsForTheDraw(manager, read, MeasureOfTheDraw(context)); !settled.HasValue())
        return settled;
    OldLetterPart& letter = read.letter;
    DrawTheBodyLines(context, letter, view);
    const float left = ToFloat(Steps::ClientX(read, 0));
    const std::int32_t width = Steps::ClientWidth(read);
    if (letter.portraitMode >= ReadPortraitShown)
    {
        const float toThePortrait = F32(static_cast<double>(width) - PortraitWidth);
        SetThePalette(sprites, LineIndex, 1.0f);
        sprites.ColorQuad(ToFloat(Steps::ClientX(read, 0) + width - PortraitX), FromTheBottom(read, ButtonsBarRise),
                          1.0f, PortraitLineHeight);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsLineRise), toThePortrait, 1.0f);
        SetThePalette(sprites, BlackIndex, 1.0f);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsBarRise), toThePortrait, ButtonsBarHeight);
    }
    else
    {
        SetThePalette(sprites, LineIndex, 1.0f);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsLineRise), ToFloat(width), 1.0f);
        SetThePalette(sprites, BlackIndex, 1.0f);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsBarRise), ToFloat(width), ButtonsBarHeight);
    }
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(read, HeaderLineY)), ToFloat(width), 1.0f);
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(read, 0)), ToFloat(width), HeaderBarHeight);
    WhiteWithTexture(sprites);
    // sprintf(1014, +0x780, +0x88b, +0x89b) con los colores que haya: los del último renglón del cuerpo.
    const LetterEntry& entry = letter.entry;
    static_cast<void>(DrawText(context, Steps::ClientX(read, ReadHeaderX), Steps::ClientY(read, ReadHeaderY),
                               Sprintf(TextAt(view, ReadHeaderText), entry.name, entry.date, entry.time)));
    for (const OldButton& button : letter.buttons)
        DrawTheButton(context, button, view);
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

Core::Expected<void, std::string> DrawTheWriteWindow(OldWindowManager& manager, OldWindow& write,
                                                     const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto settled = SettleTheLetterKindsForTheDraw(manager, write, MeasureOfTheDraw(context)); !settled.HasValue())
        return settled;
    OldLetterPart& letter = write.letter;
    const float left = ToFloat(Steps::ClientX(write, 0));
    const float width = ToFloat(Steps::ClientWidth(write));
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(write, 0)), width, WriteHeaderHeight);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(write, WriteToLineY)), width, 1.0f);
    sprites.ColorQuad(left, ToFloat(Steps::ClientY(write, WriteSubjectLineY)), width, 1.0f);
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(write, WriteBottomRise), width, WriteBottomHeight);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(write, WriteBottomRise), width, 1.0f);
    WhiteWithTexture(sprites);
    // Solo el color del texto (0x6c8e5c): el fondo queda el que haya.
    context.textColors.text = WriteTextColor;
    // GetTextExtentPointA del 1000 con el DC (0x498702..0x49871b): la caja de los dos.
    const std::string to = TextAt(view, ToText);
    const std::int32_t box = context.text.Measure(context.dcFont, to).width;
    DrawRightAligned(context, Steps::ClientX(write, WriteLabelX), Steps::ClientY(write, ToLabelY), to, box);
    DrawRightAligned(context, Steps::ClientX(write, WriteLabelX), Steps::ClientY(write, SubjectLabelY),
                     TextAt(view, SubjectText), box);
    // 0x498772..0x4987a1: la vtable + 0x14 (0x468320) del nombre, del asunto y del cuerpo, con la mezcla normal.
    const auto drawTheEdit = [&](OldEdit& edit)
    { OldDraw::DrawTheOldEdit(context, edit, view.focusedEdit != 0 && view.focusedEdit == edit.base.id, view); };
    drawTheEdit(letter.to);
    drawTheEdit(letter.subject);
    drawTheEdit(letter.text);
    sprites.SetBlend(Render::BlendMode::Opaque);
    if (letter.buttons.size() != WriteButtonCount)
        return {};
    DrawTheButton(context, letter.buttons[0], view);
    DrawTheButton(context, letter.buttons[1], view);
    if (letter.withPortrait != WithThePortrait)
        return {};
    DrawTheButton(context, letter.buttons[2], view);
    DrawTheButton(context, letter.buttons[3], view);
    return {};
}

void DrawTheFriendBarIcons(OldWindowManager& manager, Render::SpriteBatch& sprites)
{
    OldWindow& windowList = manager.windowList;
    OldWindowListPart& list = windowList.list;
    // 0x4a512f..0x4a5146: IDIV con signo y SETL.
    const bool lit = list.barBlink % BarBlinkPeriod < BarBlinkLit;
    // El += de +0x148 en todas las salidas; en 32 bits, como el original.
    const auto nextDraw = [&list]
    { list.barBlink = static_cast<std::int32_t>(static_cast<std::uint32_t>(list.barBlink) + 1u); };
    const std::int32_t x = windowList.base.x;
    const std::int32_t y = list.bottom;
    const bool waiting = !list.waiting.empty();
    const auto unread = [&manager]
    {
        return std::ranges::any_of(manager.letterModel.letters,
                                   [](const LetterEntry& letter) { return letter.type == 0; });
    };
    std::int32_t letterX = 0;
    std::int32_t letterY = 0;
    if (FindTheOldWindow(manager, manager.friends) != nullptr)
    {
        // FILD y FMUL de los float: w / 64 y h / 32, exactos.
        const float width = ToFloat(windowList.base.width);
        const float height = ToFloat(list.rowStep);
        OldDraw::Quad(sprites, PushSlot, ToFloat(x), ToFloat(y), width, height, 0.0f, 0.0f, width * PushDu,
                      height * PushDv);
        if (waiting && lit)
            OldDraw::Quad(sprites, ManSlot, ToFloat(x + 7), ToFloat(y + 3), ManWidth, ManHeight, 0.0f, ManDv, ManDu,
                          ManDv);
        letterX = x + 0x24;
        letterY = y + 7;
        if (list.newLetter == 0)
        {
            if (unread())
                OldDraw::Quad(sprites, LetterSlot, ToFloat(letterX), ToFloat(letterY), LetterWidth, LetterHeight, 0.0f,
                              0.0f, LetterDu, LetterDv);
            nextDraw();
            return;
        }
        // 0x4a5230: sin ninguna sin leer, 0x4a6420(0); dibuja igual.
        if (!unread())
            list.newLetter = 0;
    }
    else
    {
        if (waiting && lit)
            OldDraw::Quad(sprites, ManSlot, ToFloat(x + 4), ToFloat(y + 2), ManWidth, ManHeight, 0.0f, 0.0f, ManDu,
                          ManDv);
        letterX = x + 0x21;
        letterY = y + 5;
        if (list.newLetter == 0)
        {
            if (unread())
                OldDraw::Quad(sprites, LetterSlot, ToFloat(letterX), ToFloat(letterY), LetterWidth, LetterHeight, 0.0f,
                              0.0f, LetterDu, LetterDv);
            nextDraw();
            return;
        }
    }
    if (!lit)
    {
        nextDraw();
        return;
    }
    OldDraw::Quad(sprites, LetterSlot, ToFloat(letterX), ToFloat(letterY), LetterWidth, LetterHeight, 0.0f, 0.0f,
                  LetterDu, LetterDv);
    if (list.barBlink % BarBlinkPeriod == LetterBlinkStep)
        ++list.letterBlinks;
    if (list.letterBlinks > LetterBlinks)
    {
        list.letterBlinks = 0;
        list.newLetter = 0;
    }
    nextDraw();
}

Core::Expected<void, std::string> DrawTheChatsInTheList(OldWindowManager& manager, const OldWindow& window,
                                                        const OldWindowDrawContext& context,
                                                        const OldWindowDrawView& view)
{
    const OldWindowListPart& list = window.list;
    if (list.chats.empty() || window.base.state != ShownWindow)
        return {};
    std::int32_t count = 0;
    for (const std::int32_t id : list.chats)
        if (!IsMinimized(manager, id))
            ++count;
    if (count == 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    // ftol((renglón + 4) · cuántos), y el alto en float (FILD y FSTP).
    const std::int32_t height =
        Truncate((static_cast<double>(list.rowHeight) + ChatRowMargin) * static_cast<double>(count));
    const float tall = ToFloat(height);
    const OldWindowBase& base = window.base;
    const float x = ToFloat(base.x);
    const float top = ToFloat(list.bottom - height);
    const float width = ToFloat(base.width);
    SetThePalette(sprites, ListBackIndex, list.blink);
    sprites.ColorQuad(x, top, width, tall);
    SetThePalette(sprites, ListEdgeIndex, list.blink);
    sprites.ColorQuad(x, top, width, 1.0f);
    sprites.ColorQuad(x, top, 1.0f, tall);
    sprites.ColorQuad(ToFloat(base.width + base.x - 1), top, 1.0f, tall);
    sprites.ColorQuad(x, ToFloat(list.bottom), width, 1.0f);
    WhiteWithTexture(sprites);
    return DrawTheChatRowsOfTheList(manager, window, context, view);
}

Core::Expected<void, std::string> DrawTheLetterPortrait(OldWindow& window, const OldWindowDrawView& view)
{
    std::string_view where;
    if (window.kind == OldWindowKind::LetterReadWindow && window.letter.portraitMode >= ReadPortraitShown)
        where = "0x49a1d0";
    else if (window.kind == OldWindowKind::LetterWriteWindow && window.letter.withPortrait == 1)
        where = "0x498800";
    else
        return {};
    if (!view.portrait)
        return Core::Unexpected(std::format("{}: el dibujo del retrato de las cartas (su vtable + 0x14, 0x497c10) sin "
                                            "el juego",
                                            where));
    return view.portrait(window.letter.portrait);
}
} // namespace Client::UI
