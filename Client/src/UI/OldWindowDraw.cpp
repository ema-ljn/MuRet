#include "UI/OldWindowDraw.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/OriginalQuad.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ITextDrawer.h"
#include "UI/ChatLog.h"
#include "UI/ChatLogWindow.h"
#include "UI/FriendWindowsDraw.h"
#include "UI/LetterWindowsDraw.h"
#include "UI/OldControls.h"
#include "UI/OldEditLines.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindows.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::UI
{
namespace
{
using OldDraw::DrawText;
using OldDraw::DrawTheButton;
using OldDraw::Gray;
using OldDraw::Quad;
using OldDraw::RatioY;
using OldDraw::SetThePalette;
using OldDraw::ToFloat;
using OldDraw::Truncate;
using OldDraw::WhiteWithTexture;

// 0x459f00: el rectángulo desde arriba a la izquierda.
constexpr std::int32_t TopLeftRect = 1;
// 0x491960: los 19 colores (r, g, b) de glColor4ub; el 3 y el 7 son negros con su alfa, y el 3 lo toma de alfa · 179
// (0x6b9f94) en lugar de alfa · 255 (0x6b9f98).
struct PaletteEntry
{
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};
constexpr std::array<PaletteEntry, 19> Palette{{
    {146, 134, 121}, // 0, el filo de afuera del borde
    {37, 37, 37},    // 1, el medio del borde
    {106, 97, 88},   // 2, el filo de adentro y la raya bajo el título
    {0, 0, 0},       // 3, la sombra
    {173, 167, 150}, // 4, las cuatro esquinas
    {53, 49, 48},    // 5
    {26, 22, 21},    // 6
    {0, 0, 0},       // 7
    {153, 156, 166}, // 8, la punta de arriba de la izquierda de la barra
    {136, 138, 147}, // 9, la izquierda de la barra
    {83, 85, 93},    // 10, la punta de abajo de la izquierda (y las de arriba, sin el foco)
    {102, 104, 112}, // 11, la punta de arriba de la derecha
    {0, 0, 8},       // 12, la derecha de la barra
    {0, 0, 0},       // 13, la punta de abajo de la derecha
    {185, 185, 185}, // 14
    {194, 194, 194}, // 15
    {194, 194, 194}, // 16
    {209, 188, 134}, // 17
    {205, 209, 133}, // 18
}};
constexpr std::int32_t ShadowIndex = 3;
constexpr float FullAlpha = 255.0f;
constexpr float ShadowAlpha = 179.0f;
// 0x492500: la sombra cae 5 más abajo; con el borde, 10 más corta.
constexpr float ShadowDrop = 5.0f;
constexpr float ShadowCut = 10.0f;
// 0x492500: la barra del título, de 15 de alto en (x + 6, y + 5) y 12 menos de ancho, con 1 × 0,9375 de la textura.
constexpr float TitleBarX = 6.0f;
constexpr float TitleBarY = 5.0f;
constexpr float TitleBarCut = 12.0f;
constexpr float TitleBarHeight = 15.0f;
constexpr float TitleBarDv = 0.9375f;
// Los filos de la barra: puntos en y + 5 e y + 19 y una raya de 13 en y + 6, en x + 5 y en x + w − 6.
constexpr float TitleEdgeLeft = 5.0f;
constexpr float TitleEdgeRight = 6.0f;
constexpr float TitleEdgeTop = 5.0f;
constexpr float TitleEdgeMiddle = 6.0f;
constexpr float TitleEdgeLength = 13.0f;
constexpr float TitleEdgeBottom = 19.0f;
// La raya bajo el título, en (x + 5, y + 20), 10 menos de ancho.
constexpr float TitleLineX = 5.0f;
constexpr float TitleLineY = 20.0f;
constexpr float TitleLineCut = 10.0f;
// El título, en (x + 9, y + 8), cortado en un renglón de w − 0x32.
constexpr std::int32_t TitleTextX = 9;
constexpr std::int32_t TitleTextY = 8;
constexpr int TitleWrapCut = 0x32;
// Los colores del título (0x6c8e5c): el de la de arriba y, sin ella, la suma de 0x492838 (0xff9b918d + 0xffc8dce6).
constexpr std::uint32_t TitleColor = 0xffc8dce6u;
constexpr std::uint32_t DimTitleColor = 0xff646e73u;
// Los botones de la barra: 9 × 9 en y + 8, con 0,28125 de la textura de lado. El de minimizar está en x + w − 0x1b o,
// con el de estirar, en x + w − 0x26; el de estirar, en x + w − 27, y la cruz, en x + w − 16.
constexpr float TitleButtonSize = 9.0f;
constexpr float TitleButtonY = 8.0f;
constexpr std::int32_t MinimizeAloneX = 0x1b;
constexpr std::int32_t MinimizeWithStretchX = 0x26;
constexpr float StretchButtonX = 27.0f;
constexpr float CloseButtonX = 16.0f;
constexpr float TitleButtonUv = 0.28125f;
// 0x492460: el rectángulo de los tres botones, 0x26 × 9 desde x + w − 0x26.
constexpr std::int32_t TitleButtonsWidth = 0x26;
constexpr std::int32_t TitleButtonsHeight = 9;
// La esquina del tamaño, 9 × 9 en (x + w − 10, y + h − 10), con 0,5625 de la textura.
constexpr float ResizeGripInset = 10.0f;
constexpr float ResizeGripSize = 9.0f;
constexpr float ResizeGripUv = 0.5625f;
// Los grises de glColor4f: el de las que no son la de arriba (0,5), el de un botón sin el mouse (0,8, 0x3f4ccccd) y
// el apretado (0,6, 0x3f19999a).
constexpr float DimGray = 0.5f;
constexpr float IdleGray = 0.8f;
constexpr float PressedGray = 0.6f;
// 0x45b2e0: el botón apagado, en rojo (1, 0,4, 0,4).
constexpr float DisabledGreen = 0.4f;
// 0x45b2e0: win_button.tga, 0,765625 × 1 de la textura; apretado, 1 adentro y 0,75 × 0,9375.
constexpr float ButtonDu = 0.765625f;
constexpr float ButtonDv = 1.0f;
constexpr float PressedButtonDu = 0.75f;
constexpr float PressedButtonDv = 0.9375f;
// 0x45b2e0: el texto del botón, centrado con ((lado − texto) + 0,5) · 0,5 (0x6b882c).
constexpr double Half = 0.5;
constexpr std::uint32_t ButtonTextColor = 0xffc8dce6u;
// 0x4a3a90: el texto de la caja, en blanco, en (5, 8) del área de adentro; el segundo renglón 8 más abajo que el alto
// del primero (0x6b86c4).
constexpr std::uint32_t BoxTextColor = 0xffffffffu;
constexpr std::int32_t BoxTextX = 5;
constexpr std::int32_t BoxTextY = 8;
constexpr double BoxLineGap = 8.0;
// 0x48f890 y 0x48f8b0: el área de adentro empieza en (x + 5, y + 0x15).
constexpr std::int32_t ClientLeft = 5;
constexpr std::int32_t ClientTop = 0x15;
// 0x4a5490: el renglón de la lista se mide con "0" (0x6c7ae4).
constexpr std::string_view ListRowSample = "0";
// 0x6b87d4, 0x6b8818 y 0x6b86b8: el 1, el 2 y el 3 de float de los dibujos.
constexpr double One = 1.0;
constexpr double Two = 2.0;
constexpr double Three = 3.0;
// 0x4a2460: el encabezado de la vista con los filos del 2 y el relleno del 5; los otros, con el relleno del 6.
constexpr std::int32_t HeaderEdgeIndex = 2;
constexpr std::int32_t CurrentHeaderIndex = 5;
constexpr std::int32_t OtherHeaderIndex = 6;
// 0x463090: el marco de 9 × 9 con el 0 de la paleta, su lado de enfrente en +8, y la marca de win_check.tga (0x64a)
// de 5 × 5 en (x + 2, y + 2), con 0,625 de la textura.
constexpr std::uint16_t CheckSlot = 0x64a;
constexpr std::int32_t CheckboxEdgeIndex = 0;
constexpr float CheckboxSize = 9.0f;
constexpr std::int32_t CheckboxFar = 8;
constexpr std::int32_t CheckInset = 2;
constexpr float CheckSize = 5.0f;
constexpr float CheckUv = 0.625f;
// 0x461890: las flechas de win_scrollbar.jpg (0x649), de 13 × 13 a 12 del borde derecho (0x6b8864); la de arriba un
// punto sobre el borde de arriba y la de abajo a 12 del de abajo. El mouse las toma desde 12 del borde derecho.
constexpr std::uint16_t ScrollbarSlot = 0x649;
constexpr std::int32_t ArrowInset = 12;
constexpr std::int32_t ArrowSize = 13;
constexpr float ArrowSide = 13.0f;
// Las UV de las flechas: la de arriba de (0, 0,09375) y la de abajo de (0, 0,5), de 0,8125 × 0,40625; con el clic,
// dadas vuelta desde (0,8125, 0,90625) y (0,8125, 0,5).
constexpr float ArrowDu = 0.8125f;
constexpr float ArrowDv = 0.40625f;
constexpr float UpArrowV = 0.09375f;
constexpr float PressedUpArrowV = 0.90625f;
constexpr float DownArrowV = 0.5f;
// Los filos de la pista con el 2 de la paleta, 1 adentro del cursor y en el borde; el cursor, 2 adentro y 2 más
// angosto, con el medio de (0, 0,03125), la punta de arriba de (0, 0) y la de abajo de (0, 0,0625), de 0,6875 ×
// 0,03125.
constexpr std::int32_t TrackEdgeIndex = 2;
constexpr float ThumbDu = 0.6875f;
constexpr float ThumbDv = 0.03125f;
constexpr float ThumbMiddleV = 0.03125f;
constexpr float ThumbBottomV = 0.0625f;
// 0x461cd0: la franja del elegido, 13 de alto y 3 más arriba que el renglón, de alfa 0x3f333333 con las flechas del
// teclado y 0x3e99999a sin ellas; su texto en negro y el de los demás en 0xffc8dce6.
constexpr std::int32_t ChosenBandRise = 3;
constexpr float ChosenBandHeight = 13.0f;
constexpr float FocusedBandAlpha = 0.7f;
constexpr float BandAlpha = 0.3f;
constexpr std::uint32_t ChosenRowTextColor = 0xff000000u;
constexpr std::uint32_t RowTextColor = 0xffc8dce6u;
// 0x468320: el cursor de 1,5 de ancho sin IME (+0xa8, 0x3fc00000) y la caja del bitmap 1,5 más ancha que el texto y
// el cursor (0x6b8b94); el alto del cursor, el de "Q" (0x6c7af4); prendido con el resto de 24 menor que 12, y sus
// píxeles en 0xffc8c8c8.
constexpr double CaretWidth = 1.5;
constexpr double BoxSlack = 1.5;
constexpr std::string_view CaretSample = "Q";
constexpr std::int32_t BlinkPeriod = 0x18;
constexpr std::int32_t BlinkOn = 0xc;
constexpr std::uint32_t CaretColor = 0xffc8c8c8u;
// +0x4c & 8 del EDIT (el filtro de la subclase, que con 8 no filtra): el negro de antes (glColor3f(0, 0, 0)).
constexpr std::int32_t EditBlackFlag = 0x8;
// 0x468970: la barra del EDIT de varios renglones, de 13 de ancho (+0xd4, 0x41500000), con la pista de y + 9 a
// y + h − 9, las flechas en x + w − 15 (0x6b87bc), en y − 4 (0x6b8a44) y en y + h − 9 (0x6b887c); los filos en
// x + w − 13 − 2 y x + w − 3.
constexpr float EditBarWidth = 13.0f;
constexpr std::int32_t EditBarEnd = 9;
constexpr double EditArrowInset = 15.0;
constexpr std::int32_t EditArrowRise = 4;
constexpr std::int32_t EditArrowDrop = 9;

float F32(double value)
{
    return static_cast<float>(value);
}

// Un color de 0xAABBGGRR (Render/Text/TextStyle.h) en glColor.
Render::Color ColorOf(std::uint32_t abgr)
{
    const auto channel = [abgr](std::uint32_t shift) { return static_cast<float>((abgr >> shift) & 0xffu) / 255.0f; };
    return Render::Color{.red = channel(0), .green = channel(8), .blue = channel(16), .alpha = channel(24)};
}

// 0x492180(x, y, w, h): el borde de tres filos (0, 1 y 2 de la paleta) arriba, abajo, a la izquierda y a la derecha, y
// las cuatro esquinas del 4; la de abajo a la derecha no, con la esquina del tamaño (+0x4c & 4).
void DrawTheEdges(Render::SpriteBatch& sprites, const OldWindowBase& base)
{
    const float x = ToFloat(base.x);
    const float y = ToFloat(base.y);
    const float w = ToFloat(base.width);
    const float h = ToFloat(base.height);
    const float bottom = y + h;
    const float right = x + w;
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(x, y, w, 1.0f);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(x, y + 1.0f, w, 3.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(x, y + 4.0f, w, 1.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(x, bottom - 5.0f, w, 1.0f);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(x, bottom - 4.0f, w, 3.0f);
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(x, bottom - 1.0f, w, 1.0f);
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(x, y, 1.0f, h);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(x + 1.0f, y + 5.0f, 3.0f, h - 10.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(x + 4.0f, y + 1.0f, 1.0f, h - 2.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(right - 5.0f, y + 1.0f, 1.0f, h - 2.0f);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(right - 4.0f, y + 5.0f, 3.0f, h - 10.0f);
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(right - 1.0f, y, 1.0f, h);
    SetThePalette(sprites, 4, 1.0f);
    sprites.ColorQuad(x + 2.0f, y + 2.0f, 1.0f, 1.0f);
    sprites.ColorQuad(right - 3.0f, y + 2.0f, 1.0f, 1.0f);
    sprites.ColorQuad(x + 2.0f, bottom - 3.0f, 1.0f, 1.0f);
    if ((base.flags & ResizeFlag) == 0)
        sprites.ColorQuad(right - 3.0f, bottom - 3.0f, 1.0f, 1.0f);
}

// 0x492460(botón): el gris de un botón de la barra: el del mouse (1, o 0,6 con el clic) si es el de +0xa0 y el mouse
// está sobre los tres; si no, 0,8.
void TitleButtonGray(Render::SpriteBatch& sprites, const OldWindow& window, std::int32_t button,
                     const OldWindowDrawView& view)
{
    const OldWindowBase& base = window.base;
    if (window.frame.hotButton == button &&
        MouseInRect(view.mouseX, view.mouseY, base.x - TitleButtonsWidth + base.width, base.y + TitleTextY,
                    TitleButtonsWidth, TitleButtonsHeight, TopLeftRect))
    {
        Gray(sprites, view.leftClick ? PressedGray : 1.0f);
        return;
    }
    Gray(sprites, IdleGray);
}

// 0x4a3a90, la vtable + 0x50 de la caja: arrastrada o cambiando de tamaño, vuelve a ubicar los botones; el texto en
// blanco y los botones (el segundo, solo con dos).
void DrawTheBox(OldWindowManager& manager, OldWindow& window, const OldWindowDrawContext& context,
                const OldWindowDrawView& view)
{
    if (window.base.state == DraggedWindow || window.base.state == ResizedWindow)
        SettleTheButtons(manager, window);
    context.textColors = Render::Text::TextColors{.text = BoxTextColor, .background = 0};
    const OldWindowBase& base = window.base;
    const std::int32_t x = base.x + BoxTextX + ClientLeft;
    const double height = DrawText(context, x, base.y + BoxTextY + ClientTop, window.box.lines[0]);
    if (!window.box.lines[1].empty())
        static_cast<void>(
            DrawText(context, x, base.y + Truncate(height + BoxLineGap) + ClientTop, window.box.lines[1]));
    DrawTheButton(context, window.box.buttons[0], view);
    if (window.box.oneButton == 0)
        DrawTheButton(context, window.box.buttons[1], view);
}

// 0x4a5490, la vtable + 0x50 de la lista: mide el renglón la primera vez, corre el parpadeo y, con entradas y a la
// vista, las dibuja (los chats con un amigo, hito 10).
Core::Expected<void, std::string> DrawTheListRows(OldWindow& window, const OldWindowDrawContext& context)
{
    OldWindowListPart& list = window.list;
    if (list.rowHeight == 0.0f)
    {
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, ListRowSample);
        list.rowHeight = static_cast<float>(static_cast<double>(extent.height) / RatioY(context.sprites.Screen()));
    }
    const double blink = static_cast<double>(list.blink) + static_cast<double>(list.blinkStep);
    list.blink = static_cast<float>(blink);
    if (blink < 0.0)
    {
        list.blinkStep = 0.0f;
        list.blink = 0.0f;
    }
    else if (blink > 1.0)
    {
        list.blinkStep = 0.0f;
        list.blink = 1.0f;
    }
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    // 0x4a5537: sin entradas (+0xdc, que solo llenan los chats con un amigo del hito 10), nada más.
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

// 0x492500, la vtable + 0x14 del marco, la caja y la lista.
Core::Expected<void, std::string> DrawTheFrame(OldWindowManager& manager, OldWindow& window,
                                               const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    const std::int32_t flags = window.base.flags;
    if (flags != 0)
    {
        SetThePalette(sprites, ShadowIndex, 1.0f);
        const float height = ToFloat(window.base.height) - ((flags & BorderFlag) != 0 ? ShadowCut : 0.0f);
        sprites.ColorQuad(ToFloat(window.base.x), ToFloat(window.base.y) + ShadowDrop, ToFloat(window.base.width),
                          height);
    }
    WhiteWithTexture(sprites);
    context.dcFont = Render::Text::FontKind::Normal;
    // La vtable + 0x50: 0x48fb00 (nada) en el marco.
    if (window.kind == OldWindowKind::MessageBox)
        DrawTheBox(manager, window, context, view);
    else if (window.kind == OldWindowKind::AddFriendBox)
        DrawTheAddFriendBox(manager, window, context, view);
    else if (window.kind == OldWindowKind::FriendsWindow)
    {
        if (auto drawn = DrawTheFriendsWindow(manager, window, context, view); !drawn.HasValue())
            return drawn;
    }
    else if (window.kind == OldWindowKind::WindowList)
    {
        if (auto rows = DrawTheListRows(window, context); !rows.HasValue())
            return rows;
    }
    else if (window.kind == OldWindowKind::ChatWindow)
        DrawTheChatWindow(manager, window, context, view);
    else if (window.kind == OldWindowKind::LetterReadWindow)
        DrawTheReadWindow(manager, window, context, view);
    else if (window.kind == OldWindowKind::LetterWriteWindow)
        DrawTheWriteWindow(manager, window, context, view);
    const OldWindowBase& base = window.base;
    // 0x49258d..0x4925dd: la del final de la lista con la ventana de amigos abierta se dibuja como es; las demás, en
    // gris.
    const bool dim = LastOldWindow(manager) != base.id || !view.friendsOpen;
    if (dim)
        Gray(sprites, DimGray);
    const float x = ToFloat(base.x);
    const float y = ToFloat(base.y);
    const float w = ToFloat(base.width);
    const float right = x + w;
    if ((flags & BorderFlag) != 0)
    {
        if ((flags & TitleFlag) != 0)
        {
            Quad(sprites, WindowTitleSlot, x + TitleBarX, y + TitleBarY, w - TitleBarCut, TitleBarHeight, 0.0f, 0.0f,
                 1.0f, TitleBarDv);
            SetThePalette(sprites, dim ? 10 : 8, 1.0f);
            sprites.ColorQuad(x + TitleEdgeLeft, y + TitleEdgeTop, 1.0f, 1.0f);
            SetThePalette(sprites, dim ? 10 : 9, 1.0f);
            sprites.ColorQuad(x + TitleEdgeLeft, y + TitleEdgeMiddle, 1.0f, TitleEdgeLength);
            SetThePalette(sprites, 10, 1.0f);
            sprites.ColorQuad(x + TitleEdgeLeft, y + TitleEdgeBottom, 1.0f, 1.0f);
            SetThePalette(sprites, 11, 1.0f);
            sprites.ColorQuad(right - TitleEdgeRight, y + TitleEdgeTop, 1.0f, 1.0f);
            SetThePalette(sprites, 12, 1.0f);
            sprites.ColorQuad(right - TitleEdgeRight, y + TitleEdgeMiddle, 1.0f, TitleEdgeLength);
            SetThePalette(sprites, 13, 1.0f);
            sprites.ColorQuad(right - TitleEdgeRight, y + TitleEdgeBottom, 1.0f, 1.0f);
        }
        DrawTheEdges(sprites, base);
        WhiteWithTexture(sprites);
    }
    if ((flags & TitleFlag) != 0)
    {
        sprites.SetBlend(Render::BlendMode::Alpha);
        SetThePalette(sprites, 2, 1.0f);
        sprites.ColorQuad(x + TitleLineX, y + TitleLineY, w - TitleLineCut, 1.0f);
        WhiteWithTexture(sprites);
        context.dcFont = Render::Text::FontKind::Bold;
        context.textColors = Render::Text::TextColors{.text = dim ? DimTitleColor : TitleColor, .background = 0};
        // 0x459c70(título, renglón, w − 0x32, 1, 0x100, 0, 0), con la fuente del DC.
        auto wrapped = WrapByWidth(window.frame.title.value_or(std::string()), context.text, context.dcFont,
                                   sprites.Screen(), base.width - TitleWrapCut, 1, 0);
        if (!wrapped.HasValue())
            return Core::Unexpected(std::move(wrapped).Error());
        const std::string title = wrapped.Value().lines.empty() ? std::string() : wrapped.Value().lines[0];
        static_cast<void>(DrawText(context, base.x + TitleTextX, base.y + TitleTextY, title));
        const float buttonY = y + TitleButtonY;
        if ((flags & MinimizeFlag) != 0)
        {
            TitleButtonGray(sprites, window, 1, view);
            const std::int32_t from = (flags & StretchFlag) != 0 ? MinimizeWithStretchX : MinimizeAloneX;
            Quad(sprites, WindowTitleButtonsSlot, ToFloat(base.x) + w - ToFloat(from), buttonY, TitleButtonSize,
                 TitleButtonSize, 0.0f, 0.0f, TitleButtonUv, TitleButtonUv);
        }
        if ((flags & StretchFlag) != 0)
        {
            TitleButtonGray(sprites, window, 2, view);
            Quad(sprites, WindowTitleButtonsSlot, right - StretchButtonX, buttonY, TitleButtonSize, TitleButtonSize,
                 TitleButtonUv, window.frame.stretched != 0 ? TitleButtonUv : 0.0f, TitleButtonUv, TitleButtonUv);
        }
        TitleButtonGray(sprites, window, 3, view);
        Quad(sprites, WindowTitleButtonsSlot, right - CloseButtonX, buttonY, TitleButtonSize, TitleButtonSize, 0.0f,
             TitleButtonUv, TitleButtonUv, TitleButtonUv);
        sprites.SetColor(Render::Color{});
        context.dcFont = Render::Text::FontKind::Normal;
    }
    if ((flags & ResizeFlag) != 0)
        Quad(sprites, WindowResizeSlot, right - ResizeGripInset, ToFloat(base.y + base.height) - ResizeGripInset,
             ResizeGripSize, ResizeGripSize, 0.0f, 0.0f, ResizeGripUv, ResizeGripUv);
    if (dim)
    {
        sprites.SetColor(Render::Color{});
        sprites.SetBlend(Render::BlendMode::Opaque);
    }
    // La vtable + 0x54: 0x48f900 (nada) en todas, salvo en las de una carta (0x49a1d0 y 0x498800, el retrato).
    return DrawTheLetterPortrait(window);
}
} // namespace

namespace OldDraw
{
// _ftol (0x69fd3c): trunca hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

// rx y ry (0x5762ea8 y 0x5762eac): W / 640 y H / 480 en float. Sin pantalla, 1.
double RatioX(App::PixelSize screen)
{
    return screen.width > 0 ? static_cast<double>(Render::ScaleX(1.0f, screen)) : 1.0;
}

double RatioY(App::PixelSize screen)
{
    return screen.height > 0 ? static_cast<double>(Render::ScaleY(1.0f, screen)) : 1.0;
}

float ToFloat(std::int32_t value)
{
    return static_cast<float>(value);
}

void Gray(Render::SpriteBatch& sprites, float gray)
{
    sprites.SetColor(Render::Color{.red = gray, .green = gray, .blue = gray, .alpha = 1.0f});
}

// 0x459ec0: glColor4f(1, 1, 1, 1) y glEnable(GL_TEXTURE_2D), después de los quads sin textura.
void WhiteWithTexture(Render::SpriteBatch& sprites)
{
    sprites.SetColor(Render::Color{});
    sprites.EnableTexture();
}

void SetThePalette(Render::SpriteBatch& sprites, std::int32_t index, float alpha)
{
    if (const std::optional<Render::Color> color = OldWindowPalette(index, alpha))
        sprites.SetColor(*color);
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa.
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

// 0x5542f0(x, y, texto, 0, 1, 0) → 0x465950: el texto con la fuente del DC y los colores de 0x6c8e5c; uno vacío no
// se dibuja. Devuelve lo que deja en la x87 0x467060: el alto del texto en 640 × 480 (cy / ry), la mitad si empieza
// con '\n', o 0 sin texto.
double DrawText(const OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    if (text.empty())
        return 0.0;
    context.text.Draw(x, y, text, Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors});
    const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, text);
    const double height = static_cast<double>(extent.height) / RatioY(context.sprites.Screen());
    return text.front() == '\n' ? height / 2.0 : height;
}

// 0x45b2e0, la vtable + 0x14 del botón: win_button.tga del gris del mouse y el texto centrado. Apagado, en rojo y sin
// volver a la mezcla opaca.
void DrawTheButton(const OldWindowDrawContext& context, const OldButton& button, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OldWindowBase& base = button.base;
    sprites.SetBlend(Render::BlendMode::Alpha);
    const bool disabled = base.state == DisabledWindow;
    const bool pressed = !disabled && button.pressed == 1;
    if (disabled)
        sprites.SetColor(Render::Color{.red = 1.0f, .green = DisabledGreen, .blue = DisabledGreen, .alpha = 1.0f});
    else if (!MouseInRect(view.mouseX, view.mouseY, base.x, base.y, base.width, base.height, TopLeftRect))
        Gray(sprites, IdleGray);
    else
        Gray(sprites, pressed ? PressedGray : 1.0f);
    if (pressed)
        Quad(sprites, WindowButtonSlot, ToFloat(base.x + 1), ToFloat(base.y + 1), ToFloat(base.width) - 1.0f,
             ToFloat(base.height) - 1.0f, 0.0f, 0.0f, PressedButtonDu, PressedButtonDv);
    else
        Quad(sprites, WindowButtonSlot, ToFloat(base.x), ToFloat(base.y), ToFloat(base.width), ToFloat(base.height),
             0.0f, 0.0f, ButtonDu, ButtonDv);
    if (button.label)
    {
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, *button.label);
        context.textColors = Render::Text::TextColors{.text = ButtonTextColor, .background = 0};
        const App::PixelSize screen = sprites.Screen();
        const std::int32_t top = base.y + (pressed ? 2 : 1);
        const std::int32_t left = base.x + (pressed ? 1 : 0);
        const std::int32_t y = Truncate(
            (static_cast<double>(base.height) - static_cast<double>(extent.height) / RatioY(screen) + Half) * Half +
            static_cast<double>(top));
        const std::int32_t x = Truncate(
            (static_cast<double>(base.width) - static_cast<double>(extent.width) / RatioX(screen) + Half) * Half +
            static_cast<double>(left));
        static_cast<void>(DrawText(context, x, y, *button.label));
    }
    sprites.SetColor(Render::Color{});
    if (!disabled)
        sprites.SetBlend(Render::BlendMode::Opaque);
}

bool MouseIn(const OldWindowDrawView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    return MouseInRect(view.mouseX, view.mouseY, x, y, width, height, TopLeftRect);
}

void DrawTheTabHeaders(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, std::int32_t width,
                       std::int32_t height, std::int32_t count, std::int32_t current)
{
    std::int32_t left = x;
    for (std::int32_t at = 0; at < count; ++at, left += width)
    {
        SetThePalette(sprites, HeaderEdgeIndex, 1.0f);
        const double fx = ToFloat(left);
        const double fy = ToFloat(y);
        const double fw = ToFloat(width);
        const double fh = ToFloat(height);
        if (at == current)
        {
            sprites.ColorQuad(F32(fx), F32(fy), F32(fw), 1.0f);
            sprites.ColorQuad(F32(fx - One), F32(fy), 1.0f, F32(fh));
            sprites.ColorQuad(F32(fw + fx - One), F32(fy), 1.0f, F32(fh));
            SetThePalette(sprites, CurrentHeaderIndex, 1.0f);
            sprites.ColorQuad(F32(fx), F32(fy + One), F32(fw - One), F32(fh - One));
            continue;
        }
        const float below = F32(fy + One);
        sprites.ColorQuad(F32(fx), below, F32(fw), 1.0f);
        sprites.ColorQuad(F32(fw + fx - One), below, 1.0f, F32(fh - One));
        sprites.ColorQuad(F32(fx), F32(fh + fy - One), F32(fw), 1.0f);
        SetThePalette(sprites, OtherHeaderIndex, 1.0f);
        sprites.ColorQuad(F32(fx), F32(fy + Two), F32(fw - One), F32(fh - Three));
    }
}

void DrawTheCheckbox(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool checked)
{
    sprites.SetBlend(Render::BlendMode::Opaque);
    SetThePalette(sprites, CheckboxEdgeIndex, 1.0f);
    const float fx = ToFloat(x);
    const float fy = ToFloat(y);
    sprites.ColorQuad(fx, fy, CheckboxSize, 1.0f);
    sprites.ColorQuad(fx, ToFloat(y + CheckboxFar), CheckboxSize, 1.0f);
    sprites.ColorQuad(fx, fy, 1.0f, CheckboxSize);
    sprites.ColorQuad(ToFloat(x + CheckboxFar), fy, 1.0f, CheckboxSize);
    WhiteWithTexture(sprites);
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (checked)
        Quad(sprites, CheckSlot, ToFloat(x + CheckInset), ToFloat(y + CheckInset), CheckSize, CheckSize, 0.0f, 0.0f,
             CheckUv, CheckUv);
}

void DrawTheListBar(Render::SpriteBatch& sprites, OldList& list, std::size_t size, const OldWindowDrawView& view)
{
    PlaceTheListBar(list, size);
    const OldWindowBase& base = list.base;
    // FILD x y FIADD w.
    const double right = static_cast<double>(ToFloat(base.x)) + static_cast<double>(base.width);
    const float arrowX = F32(right - static_cast<double>(ArrowInset));
    const std::int32_t mouseX = base.x + base.width - ArrowInset;
    const bool up = view.leftClick && MouseIn(view, mouseX, base.y - base.height - 1, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX, F32(static_cast<double>(base.y) - static_cast<double>(base.height) - One),
         ArrowSide, ArrowSide, up ? ArrowDu : 0.0f, up ? PressedUpArrowV : UpArrowV, up ? -ArrowDu : ArrowDu,
         up ? -ArrowDv : ArrowDv);
    const bool down = view.leftClick && MouseIn(view, mouseX, base.y - ArrowInset, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX, F32(static_cast<double>(base.y) - static_cast<double>(ArrowInset)), ArrowSide,
         ArrowSide, down ? ArrowDu : 0.0f, DownArrowV, down ? -ArrowDu : ArrowDu, down ? -ArrowDv : ArrowDv);

    SetThePalette(sprites, TrackEdgeIndex, 1.0f);
    const float track = F32(static_cast<double>(list.barBottom) - static_cast<double>(list.barTop));
    sprites.ColorQuad(F32(right - static_cast<double>(list.thumbWidth) + One), list.barTop, 1.0f, track);
    sprites.ColorQuad(F32(right), list.barTop, 1.0f, track);
    WhiteWithTexture(sprites);

    // 0x461a53: con menos que los renglones, el cursor es toda la pista.
    const bool full = ListCount(list, size) < list.rows;
    const float top = full ? list.barTop : list.thumbY;
    const float height = full ? track : list.thumbHeight;
    const float thumbX = F32(right - static_cast<double>(list.thumbWidth) + Two);
    const float thumbWidth = F32(static_cast<double>(list.thumbWidth) - Two);
    Quad(sprites, ScrollbarSlot, thumbX, top, thumbWidth, height, 0.0f, ThumbMiddleV, ThumbDu, ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX, top, thumbWidth, 1.0f, 0.0f, 0.0f, ThumbDu, ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX, F32(static_cast<double>(list.thumbHeight) + static_cast<double>(top) - One),
         thumbWidth, 1.0f, 0.0f, ThumbBottomV, ThumbDu, ThumbDv);
}

void DrawTheOldList(const OldWindowDrawContext& context, const OldList& list, std::size_t size,
                    const OldListDrawing& drawing)
{
    if (drawing.head)
        drawing.head();
    std::size_t at = FirstShownInTheList(list);
    context.sprites.SetColor(Render::Color{});
    context.dcFont = Render::Text::FontKind::Normal;
    // 0x47a859..0x47a8ed: +0x104 se vuelve a leer en cada vuelta; el iterador corre uno por renglón dibujado.
    for (std::int32_t row = 0; row < list.rows && at < size; ++row, ++at)
    {
        const std::int32_t step = drawing.row ? drawing.row(row, at) : 1;
        if (step < 0)
            row -= step;
        else if (step == 0)
            --row;
    }
    if (drawing.tail)
        drawing.tail();
}

void DrawTheChosenRow(const OldWindowDrawContext& context, const OldList& list, std::size_t size, std::int32_t row,
                      std::int32_t focusedList)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.textColors.text = RowTextColor;
    if (list.selection == list.scroll + row + 1)
    {
        const float alpha = focusedList == list.base.id ? FocusedBandAlpha : BandAlpha;
        sprites.SetColor(Render::Color{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = alpha});
        sprites.ColorQuad(ToFloat(list.base.x), ToFloat(ListRowY(list, size, row) - ChosenBandRise),
                          F32(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + One),
                          ChosenBandHeight);
        WhiteWithTexture(sprites);
        context.textColors.text = ChosenRowTextColor;
    }
    context.textColors.background = 0;
}

void DrawTheEditBar(const OldWindowDrawContext& context, OldEdit& edit, std::int32_t lineCount,
                    const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OldWindowBase& base = edit.base;
    // GetScrollPos(SB_VERT) y EM_GETLINECOUNT, en la x87; +0xd0 / renglones, a un float.
    const double position = static_cast<double>(edit.firstLine);
    const double lines = static_cast<double>(lineCount);
    const auto shown = static_cast<float>(static_cast<double>(edit.visibleLines) / lines);
    edit.barWidth = EditBarWidth;
    edit.barTop = ToFloat(base.y + EditBarEnd);
    edit.barBottom = ToFloat(base.y + base.height - EditBarEnd);
    const double track = static_cast<double>(edit.barBottom) - static_cast<double>(edit.barTop);
    edit.thumbHeight = F32(track * std::min(static_cast<double>(shown), One));
    edit.thumbY = F32(static_cast<double>(edit.barTop) + track * std::min(position / lines, One));
    // FILD w, FIADD x: las flechas en x + w − 15, y − 4 e y + h − 9.
    const double right = static_cast<double>(base.width) + static_cast<double>(base.x);
    const float arrowX = F32(right - EditArrowInset);
    const std::int32_t mouseArrowX = base.x + base.width - static_cast<std::int32_t>(EditArrowInset);
    const bool up = view.leftClick && MouseIn(view, mouseArrowX, base.y - EditArrowRise, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX, F32(static_cast<double>(base.y) - static_cast<double>(EditArrowRise)),
         ArrowSide, ArrowSide, up ? ArrowDu : 0.0f, up ? PressedUpArrowV : UpArrowV, up ? -ArrowDu : ArrowDu,
         up ? -ArrowDv : ArrowDv);
    const bool down =
        view.leftClick && MouseIn(view, mouseArrowX, base.y + base.height - EditArrowDrop, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX,
         F32(static_cast<double>(base.height) + static_cast<double>(base.y) - static_cast<double>(EditArrowDrop)),
         ArrowSide, ArrowSide, down ? ArrowDu : 0.0f, DownArrowV, down ? -ArrowDu : ArrowDu, down ? -ArrowDv : ArrowDv);

    sprites.SetBlend(Render::BlendMode::Alpha);
    SetThePalette(sprites, TrackEdgeIndex, 1.0f);
    const float trackHeight = F32(track);
    const double inner = right - static_cast<double>(edit.barWidth);
    sprites.ColorQuad(F32(inner - Two), edit.barTop, 1.0f, trackHeight);
    sprites.ColorQuad(F32(right - Three), edit.barTop, 1.0f, trackHeight);
    WhiteWithTexture(sprites);
    sprites.SetBlend(Render::BlendMode::Opaque);
    const float thumbX = F32(inner - One);
    const float thumbWidth = F32(static_cast<double>(edit.barWidth) - Two);
    Quad(sprites, ScrollbarSlot, thumbX, edit.thumbY, thumbWidth, edit.thumbHeight, 0.0f, ThumbMiddleV, ThumbDu,
         ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX, edit.thumbY, thumbWidth, 1.0f, 0.0f, 0.0f, ThumbDu, ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX,
         F32(static_cast<double>(edit.thumbY) + static_cast<double>(edit.thumbHeight) - One), thumbWidth, 1.0f, 0.0f,
         ThumbBottomV, ThumbDu, ThumbDv);
}

void DrawTheOldEdit(const OldWindowDrawContext& context, OldEdit& edit, bool focused, const OldWindowDrawView& view)
{
    // IsWindowVisible: la vtable + 4 (0x468260) la esconde con el estado 3.
    if (edit.base.state == HiddenWindow)
        return;
    Render::SpriteBatch& sprites = context.sprites;
    const App::PixelSize screen = sprites.Screen();
    const double rx = RatioX(screen);
    const double ry = RatioY(screen);
    // 0x468444: GetTextExtentPointA de "Q" en el DC, en píxeles, la primera vez.
    if (edit.caretHeight == 0.0f)
        edit.caretHeight = ToFloat(context.text.Measure(context.dcFont, CaretSample).height);
    const OldWindowBase& base = edit.base;
    // 0x468477: +0x4c del EDIT, el filtro de la subclase (OldEdit::filter).
    if ((edit.filter & EditBlackFlag) != 0)
    {
        sprites.SetColor(Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f});
        sprites.ColorQuad(ToFloat(base.x), ToFloat(base.y), ToFloat(base.width), edit.caretHeight);
        WhiteWithTexture(sprites);
    }
    // 0x4684bc..0x4684e4: GetFocus() es el EDIT y +0x80 % 0x18 < 0xc.
    const bool caretOn = focused && edit.draws % BlinkPeriod < BlinkOn;
    const Render::Text::TextStyle style{.font = Render::Text::FontKind::Normal,
                                        .colors = Render::Text::TextColors{.text = edit.textColor, .background = 0}};
    // Los renglones (uno sin +0xc4), el primero a la vista y el del cursor, que en MuRet es el último.
    const std::vector<std::string_view> lines = EditLines(edit, context.text);
    const auto lineCount = static_cast<std::int32_t>(lines.size());
    FollowTheEditCaret(edit, lineCount);
    std::int32_t width = 0;
    std::int32_t height = 0;
    if (edit.multiline == 0)
    {
        // 0x4685ee..0x468622: el ancho del bitmap, ftol(+0xa8 + 1,5 + cx), hasta ftol(w · rx); el alto, ftol(h · ry).
        const std::int32_t textWidth = context.text.Measure(context.dcFont, lines.front()).width;
        width = Truncate(CaretWidth + BoxSlack + static_cast<double>(textWidth));
        const double edge = static_cast<double>(base.width) * rx;
        if (static_cast<double>(width) > edge)
            width = Truncate(edge);
        height = Truncate(static_cast<double>(base.height) * ry);
    }
    else
    {
        // 0x468638..0x468687: con +0xc4, todo el ancho (ftol(w · rx), en una o dos texturas); con 1 y menos renglones
        // que +0xd0, ftol(renglones · +0xac) de alto, y si no, ftol(h · ry).
        width = Truncate(static_cast<double>(base.width) * rx);
        if (edit.multiline == 1 && lineCount < edit.visibleLines)
            height = Truncate(static_cast<double>(lineCount) * static_cast<double>(edit.caretHeight));
        else
            height = Truncate(static_cast<double>(base.height) * ry);
    }
    // 0x4688d8..0x468915: 0x554000 en (ftol(x · rx), ftol(y · ry)), en píxeles.
    const std::int32_t left = Truncate(static_cast<double>(base.x) * rx);
    const std::int32_t top = Truncate(static_cast<double>(base.y) * ry);
    sprites.SetColor(ColorOf(edit.backgroundColor));
    sprites.ColorQuad(F32(static_cast<double>(left) / rx), F32(static_cast<double>(top) / ry),
                      F32(static_cast<double>(width) / rx), F32(static_cast<double>(height) / ry));
    WhiteWithTexture(sprites);
    // Lo que pinta el EDIT de Windows: los renglones enteros que entran en el alto desde el primero a la vista, cada
    // uno con el alto de "Q" (*inferido*, P-C217).
    const double lineHeight = static_cast<double>(edit.caretHeight);
    std::int32_t caretRow = -1;
    for (std::int32_t at = edit.firstLine, row = 0; at < lineCount; ++at, ++row)
    {
        if (static_cast<double>(row + 1) * lineHeight > static_cast<double>(height) && edit.multiline != 0)
            break;
        if (at == lineCount - 1)
            caretRow = row;
        const std::string_view line = lines[static_cast<std::size_t>(at)];
        if (!line.empty())
            context.text.Draw(base.x, base.y + Truncate(static_cast<double>(row) * lineHeight / ry), line, style);
    }
    // El cursor: la columna de GetCaretPos + 1 (0x468805..0x46881d: más que la x y menos que la x + 1,5), que con el
    // texto que solo crece por el final es la del ancho del último renglón (*inferido*), en las filas de su y al alto
    // de "Q".
    const std::int32_t caretColumn = context.text.Measure(context.dcFont, lines.back()).width + 1;
    if (caretOn && caretRow >= 0 && caretColumn < width)
    {
        const double caretTop = static_cast<double>(caretRow) * lineHeight;
        const double rows = std::min(lineHeight, static_cast<double>(height) - caretTop);
        sprites.SetColor(ColorOf(CaretColor));
        sprites.ColorQuad(F32(static_cast<double>(left + caretColumn) / rx),
                          F32((static_cast<double>(top) + caretTop) / ry), F32(1.0 / rx), F32(rows / ry));
        WhiteWithTexture(sprites);
    }
    // 0x46894e: con +0xc4 en 1, la barra (0x468970).
    if (edit.multiline == 1)
        DrawTheEditBar(context, edit, lineCount, view);
    ++edit.draws;
}
} // namespace OldDraw

std::optional<Render::Color> OldWindowPalette(std::int32_t index, float alpha)
{
    if (index < 0 || index >= static_cast<std::int32_t>(Palette.size()))
        return std::nullopt;
    // ftol(alfa · 255) y ftol(alfa · 179), de los que glColor4ub toma el byte de abajo.
    const auto byteOf = [](double value)
    { return static_cast<float>(static_cast<std::uint32_t>(OldDraw::Truncate(value)) & 0xffu) / 255.0f; };
    const PaletteEntry& entry = Palette[static_cast<std::size_t>(index)];
    const double scale = index == ShadowIndex ? static_cast<double>(ShadowAlpha) : static_cast<double>(FullAlpha);
    return Render::Color{.red = static_cast<float>(entry.red) / 255.0f,
                         .green = static_cast<float>(entry.green) / 255.0f,
                         .blue = static_cast<float>(entry.blue) / 255.0f,
                         .alpha = byteOf(static_cast<double>(alpha) * scale)};
}

Core::Expected<void, std::string> DrawTheOldWindows(OldWindowManager& manager, const OldWindowDrawContext& context,
                                                    const OldWindowDrawView& view)
{
    for (const std::int32_t id : manager.order)
    {
        OldWindow* window = FindTheOldWindow(manager, id);
        if (window == nullptr || window->base.state == HiddenWindow || window->base.state == MinimizedWindow)
            continue;
        if (auto drawn = DrawTheFrame(manager, *window, context, view); !drawn.HasValue())
            return drawn;
    }
    manager.drawn = 1;
    return {};
}

Core::Expected<void, std::string> DrawTheWindowList(OldWindowManager& manager, const OldWindowDrawContext& context,
                                                    const OldWindowDrawView& view)
{
    return DrawTheFrame(manager, manager.windowList, context, view);
}
} // namespace Client::UI
