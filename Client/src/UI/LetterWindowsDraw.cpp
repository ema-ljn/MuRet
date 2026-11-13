#include "UI/LetterWindowsDraw.h"

#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"
#include "UI/FriendWindowsDraw.h"
#include "UI/LetterWindows.h"
#include "UI/OldControls.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"

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
} // namespace Client::UI
