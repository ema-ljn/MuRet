#include "UI/LetterWindowsDraw.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
// que 0x460130 llena con push_front: el primero es el último renglón, partido en el ancho de la lista menos 0x1e
// (0x459c70). MuRet no lo parte (P-C218) y lo lee de atrás para adelante.
void DrawTheChatLines(const OldWindowDrawContext& context, OldChatPart& chat, const OldWindowDrawView& view)
{
    OldList& list = chat.lines;
    const std::size_t size = chat.lineRows.size();
    OldDraw::DrawTheOldList(
        context, list, size,
        OldDraw::OldListDrawing{.head = [&] { OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                .row = [&](std::int32_t row, std::size_t at)
                                { return DrawTheChatRow(context, list, chat.lineRows[size - 1 - at], row, view); },
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
// partido (P-C218): MuRet lo lee de atrás para adelante.
void DrawTheBodyLines(const OldWindowDrawContext& context, OldLetterPart& letter, const OldWindowDrawView& view)
{
    OldList& list = letter.lines;
    const std::size_t size = letter.lineRows.size();
    OldDraw::DrawTheOldList(
        context, list, size,
        OldDraw::OldListDrawing{.head = [&] { OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                .row = [&](std::int32_t row, std::size_t at)
                                { return DrawTheBodyRow(context, list, size, letter.lineRows[size - 1 - at], row); },
                                .tail = {}});
}
} // namespace

void DrawTheChatWindow(OldWindowManager& manager, OldWindow& chat, const OldWindowDrawContext& context,
                       const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    SettleTheLetterKindsForTheDraw(manager, chat);
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
}

void DrawTheReadWindow(OldWindowManager& manager, OldWindow& read, const OldWindowDrawContext& context,
                       const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    SettleTheLetterKindsForTheDraw(manager, read);
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
}

void DrawTheWriteWindow(OldWindowManager& manager, OldWindow& write, const OldWindowDrawContext& context,
                        const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    SettleTheLetterKindsForTheDraw(manager, write);
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
        return;
    DrawTheButton(context, letter.buttons[0], view);
    DrawTheButton(context, letter.buttons[1], view);
    if (letter.withPortrait != WithThePortrait)
        return;
    DrawTheButton(context, letter.buttons[2], view);
    DrawTheButton(context, letter.buttons[3], view);
}

Core::Expected<void, std::string> DrawTheLetterPortrait(const OldWindow& window)
{
    if (window.kind == OldWindowKind::LetterReadWindow && window.letter.portraitMode >= ReadPortraitShown)
        return Core::Unexpected(
            std::string("0x49a1d0: el dibujo del retrato de las cartas (su vtable + 0x14, 0x497c10) "
                        "es de Scenes (P-C212)"));
    if (window.kind == OldWindowKind::LetterWriteWindow && window.letter.withPortrait == 1)
        return Core::Unexpected(
            std::string("0x498800: el dibujo del retrato de las cartas (su vtable + 0x14, 0x497c10) "
                        "es de Scenes (P-C212)"));
    return {};
}
} // namespace Client::UI
