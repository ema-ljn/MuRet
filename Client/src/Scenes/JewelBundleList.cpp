#include "Scenes/JewelBundleList.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLogWindow.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/OldWindowDraw.h"
#include "UI/Widget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Input::Key;
using Render::Color;

// 0x459f00: el rectángulo común y el que cuelga de su borde de abajo (el modo 2 de +0x78 de la lista).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t BottomLeftRect = 2;
// 0x46e940: las casillas que agrega, de 0 a 0x40 con los bordes.
constexpr std::int32_t LastCell = 0x40;
// 0x46f289..0x46f290: las que pide el derecho suelto, de 1 a 0x3f (07 Q-C1152).
constexpr std::int32_t AskedCells = 0x40;
// 0x4834a0: los mensajes de las flechas del teclado (0x483c21..0x483c84): el siguiente (la vtable + 0x5c) y el
// anterior (+ 0x60).
constexpr std::int32_t NextMessage = 0xf;
constexpr std::int32_t PreviousMessage = 0x10;
// 0x483bc0: las flechas del teclado repiten desde el sexto paso; las de la barra y la pista, desde el 16.
constexpr std::int32_t KeyRepeatSteps = 6;
constexpr std::int32_t RepeatSteps = 0x10;
// 0x483bc0: las flechas de la barra, de 13, a 0x15 del borde derecho (menos 8 con el modo 1) y, sin el modo 1, 8 más
// adentro; el cursor y la pista, a 0x13.
constexpr std::int32_t ArrowSize = 0xd;
constexpr std::int32_t ArrowInset = 0x15;
constexpr std::int32_t TrackInset = 0x13;
constexpr std::int32_t BarModeInset = 8;
// 0x483bc0: la rueda corre de a 3 renglones.
constexpr std::int32_t WheelRows = 3;
// 0x41ed10(0x19, 0, 0): las flechas de la barra.
constexpr std::int32_t ArrowSound = 0x19;
// 0x46f2c0: los renglones, de 13, desde 10 sobre el borde de abajo; 0x46f1a0 y 0x46f020 los toman desde 3 más arriba.
constexpr std::int32_t RowHeight = 0xd;
constexpr std::int32_t RowsBottom = 10;
constexpr std::int32_t RowRise = 3;
// 0x46f0f4..0x46f163: el texto en x + 4 + 2.
constexpr std::int32_t RowTextX = 6;
// 0x46ebe0: el fondo, 0x491960(7, 0,4) (0x3ecccccd) un punto más grande; las líneas de la pista con el 2.
constexpr std::int32_t FramePalette = 7;
constexpr float FrameAlpha = 0.4f;
constexpr std::int32_t TrackPalette = 2;
// win_scrollbar.jpg: las flechas de 13 a 12 del borde derecho, la de arriba un punto sobre el borde de arriba y la de
// abajo 12 sobre el de abajo; con el clic sobre ellas, dadas vuelta.
constexpr std::uint16_t ScrollbarSlot = 0x649;
constexpr float ArrowQuad = 13.0f;
constexpr float ArrowRight = 12.0f;
constexpr std::int32_t ArrowHitRight = 0xc;
constexpr float ArrowUv = 0.8125f;
constexpr float ArrowDv = 0.40625f;
constexpr float UpArrowV = 0.09375f;
constexpr float UpArrowPressedV = 0.90625f;
constexpr float DownArrowV = 0.5f;
// El cursor, 2 más adentro y 2 más angosto (0x6b8818), con su borde de arriba y de abajo de un punto.
constexpr double ThumbInset = 2.0;
constexpr float ThumbDu = 0.6875f;
constexpr float ThumbDv = 0.03125f;
constexpr float ThumbV = 0.03125f;
constexpr float ThumbBottomV = 0.0625f;
// 0x46f020: el elegido, con un velo blanco de 0,5 y el texto en negro; los demás, en 0xffc8dce6.
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color ChosenVeil{1.0f, 1.0f, 1.0f, 0.5f};
constexpr std::uint32_t ChosenTextColor = 0xff000000u;
constexpr std::uint32_t RowTextColor = 0xffc8dce6u;
// 0x46f119..0x46f141: el nombre de la joya (0x7741644 y 0x7741770 en Text.bmd), "%s,  %d" (0x6c7afc) con (nivel + 1)
// · 10, en un buffer de 300 bytes (0x46f10d).
constexpr std::size_t BlessName = 1806;
constexpr std::size_t SoulName = 1807;
constexpr std::int32_t JewelsPerLevel = 10;
constexpr std::size_t RowRoom = 300;

// _ftol (0x69fd3c): trunca hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

float F32(std::int32_t value)
{
    return static_cast<float>(value);
}

std::int32_t Count(const JewelBundleList& list)
{
    return static_cast<std::int32_t>(list.rows.size());
}

// 0x4839c0, la vtable + 0x58: elige `row`, sin pasarse de la cuenta.
void Select(JewelBundleList& list, std::int32_t row)
{
    list.selection = row;
    if (Count(list) < list.selection)
        list.selection = Count(list);
}

// 0x4839f0, la vtable + 0x5c: con un elegido, `step` más arriba, sin pasarse de la cuenta. Sin filas, ninguno.
void SelectNext(JewelBundleList& list, std::int32_t step)
{
    if (list.rows.empty())
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection += step;
    if (list.selection > Count(list))
        list.selection = Count(list);
}

// 0x483a50, la vtable + 0x60: con un elegido, `step` más abajo, hasta el 1. Sin filas, ninguno.
void SelectPrevious(JewelBundleList& list, std::int32_t step)
{
    if (list.rows.empty())
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection -= step;
    if (list.selection < 1)
        list.selection = 1;
}

// 0x4833e0, la vtable + 0x34: el primero que se ve baja `rows` (sube con negativo), entre 0 y la cuenta menos los que
// se ven. Con menos filas que las que se ven (sin signo), nada.
void Scroll(JewelBundleList& list, std::int32_t rows)
{
    if (static_cast<std::uint32_t>(Count(list)) < static_cast<std::uint32_t>(list.shownRows))
        return;
    list.scroll -= rows;
    if (list.scroll < 0)
    {
        list.scroll = 0;
        return;
    }
    if (Count(list) - list.shownRows < list.scroll)
        list.scroll = Count(list) - list.shownRows;
}

// 0x46f2c0, la vtable + 0x98: la y del renglón `row` desde el primero que se ve, de abajo hacia arriba.
std::int32_t RowY(const JewelBundleList& list, std::int32_t row)
{
    const std::int32_t shown = list.shownRows < Count(list) ? list.shownRows : Count(list);
    return (shown - row) * RowHeight - list.height - RowsBottom + list.y;
}

// 0x46f1a0 y 0x46f020: el ancho de un renglón, ftol(ancho − ancho del cursor + 1).
double RowWidth(const JewelBundleList& list)
{
    return static_cast<double>(list.width) - static_cast<double>(list.thumbWidth) + 1.0;
}

// 0x4834a0, la vtable + 0x28: un mensaje de la cola. El 9 y el 0xe no le llegan a esta lista.
void HandleTheMessage(JewelBundleList& list, std::int32_t message)
{
    // +0xbc es 1 (el constructor).
    if ((message != NextMessage && message != PreviousMessage) || list.rows.empty())
        return;
    if (message == NextMessage)
        SelectNext(list, 1);
    else
        SelectPrevious(list, 1);
    if (static_cast<std::uint32_t>(list.shownRows) >= static_cast<std::uint32_t>(Count(list)))
        return;
    // Sin elegido es −1: el primero que se ve vuelve a 0 (07 Q-C1153).
    const std::int32_t chosen = list.selection - 1;
    if (list.scroll + list.shownRows <= chosen)
        Scroll(list, list.scroll - (chosen - list.shownRows + 1));
    else if (chosen < list.scroll)
        Scroll(list, list.scroll - chosen);
}

// GetAsyncKeyState(vk) con el byte alto en 0x80: la tecla abajo. La lectura borra el bit bajo (Q-C45).
bool ReadTheKey(const Input::InputState& input, Key key)
{
    static_cast<void>(input.TakePress(key));
    return input.IsDown(key);
}

// 0x483bc0: una flecha del teclado abajo. En 0 manda el mensaje y empieza la cuenta, hasta 5 la sube y desde 6 manda
// en cada paso.
void RepeatTheKey(JewelBundleList& list, std::int32_t message)
{
    if (list.keyRepeat == 0)
    {
        list.messages.push_back(message);
        list.keyRepeat = 1;
    }
    else if (list.keyRepeat < KeyRepeatSteps)
        ++list.keyRepeat;
    else
        list.messages.push_back(message);
}

// 0x483bc0: una flecha o la pista con el botón apretado. En 0 corre (las flechas, con el sonido) y empieza la cuenta,
// hasta 15 la sube (la lista no tiene padre, +0x44 en 0) y desde 16 corre en cada paso.
void Repeat(JewelBundleList& list, WorldRuntime& runtime, std::int32_t& counter, std::int32_t rows, bool sound)
{
    if (counter == 0)
    {
        Scroll(list, rows);
        if (sound)
            runtime.lastSoundId = ArrowSound; // (CL10)
        counter = 1;
    }
    else if (counter < RepeatSteps)
        ++counter;
    else
        Scroll(list, rows);
}

// 0x483bc0 con el clic sobre la lista: las flechas de la barra y, con más filas que las que se ven (o tantas), el
// cursor y la pista. El cursor no se agarra: 0x402af0 dejó el mouse agarrado por la lista (07 Q-C1151).
void PressTheBar(const GameUiContext& context, JewelBundleList& list, WorldRuntime& runtime)
{
    const auto in = [&](std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
    { return UI::MouseInRect(context.mouseX, context.mouseY, x, y, width, height, TopLeftRect); };
    const std::int32_t inset = list.barMode == 1 ? BarModeInset : 0;
    const std::int32_t margin = list.barMode != 1 ? BarModeInset : 0;
    const std::int32_t arrowX = list.x + inset - ArrowInset + list.width;
    if (in(arrowX, margin - list.height + list.y, ArrowSize, ArrowSize))
        Repeat(list, runtime, list.arrowRepeat, -1, true);
    if (in(arrowX, list.y - (margin + ArrowSize), ArrowSize, ArrowSize))
        Repeat(list, runtime, list.arrowRepeat, 1, true);
    if (list.shownRows > Count(list))
        return;
    const std::int32_t barX = list.x + inset - TrackInset + list.width;
    const std::int32_t thumbWidth = Truncate(list.thumbWidth);
    if (in(barX, Truncate(list.thumbY), thumbWidth, Truncate(list.thumbHeight)))
        return;
    if (in(barX, Truncate(list.barTop), thumbWidth,
           Truncate(static_cast<double>(list.thumbY) - static_cast<double>(list.barTop))))
        Repeat(list, runtime, list.trackRepeat, -list.shownRows, false);
    else if (in(barX, Truncate(static_cast<double>(list.thumbHeight) + static_cast<double>(list.thumbY)), thumbWidth,
                Truncate(static_cast<double>(list.barBottom) - static_cast<double>(list.thumbY) -
                         static_cast<double>(list.thumbHeight))))
        Repeat(list, runtime, list.trackRepeat, list.shownRows, false);
}

// 0x4845d0 (la vtable + 0x74) y el renglón de 0x483bc0 con 0x46f1a0 (la vtable + 0x88), desde el primero que se ve
// hasta `shownRows` renglones o el final (0x5762ebc es siempre 0). El clic elige y da vuelta +0; el derecho suelto
// elige y devuelve la fila para 0x402a90 si su nivel no es 0xff y su casilla está en [1, 0x3f].
std::optional<JewelBundleList::Row> HoverTheRows(const GameUiContext& context, JewelBundleList& list, bool& click,
                                                 bool& rightReleased)
{
    std::optional<JewelBundleList::Row> asked{};
    const std::int32_t width = Truncate(RowWidth(list));
    auto at = static_cast<std::size_t>(list.scroll);
    for (std::int32_t row = 0; row < list.shownRows && at < list.rows.size(); ++row, ++at)
    {
        if (!UI::MouseInRect(context.mouseX, context.mouseY, list.x, RowY(list, row) - RowRise, width, RowHeight,
                             TopLeftRect))
            continue;
        if (click)
        {
            Select(list, list.scroll + 1 + row);
            // (+0 + 1) % 2 con el signo de C.
            list.rows[at].flag = (list.rows[at].flag + 1) % 2;
            click = false;
        }
        if (rightReleased)
        {
            Select(list, list.scroll + 1 + row);
            // El elegido acaba de quedar en una fila: 0x46f27e no lee el final.
            const std::optional<JewelBundleList::Row> chosen = ChosenBundle(list);
            if (chosen && chosen->level != 0xff && chosen->cell > 0 && chosen->cell < AskedCells)
                asked = chosen;
            rightReleased = false;
        }
    }
    return asked;
}

// 0x621b60(win_scrollbar, x, y, w, h, u, v, du, dv, 1, 1, 0): escalado, con el color que haya.
void Quad(Render::SpriteBatch& sprites, float x, float y, float width, float height, float u, float v, float du,
          float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{ScrollbarSlot},
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

// 0x491960(índice, alfa).
void Palette(Render::SpriteBatch& sprites, std::int32_t index, float alpha)
{
    if (const std::optional<Color> color = UI::OldWindowPalette(index, alpha))
        sprites.SetColor(*color);
}

// 0x459ec0: glColor4f(1, 1, 1, 1) y glEnable(GL_TEXTURE_2D).
void WhiteWithTexture(Render::SpriteBatch& sprites)
{
    sprites.SetColor(White);
    sprites.EnableTexture();
}

// 0x46ebe0, la vtable + 0x7c: el fondo, la barra (0x484490, que guarda para el paso), las flechas (dadas vuelta con
// el clic sobre ellas, 0x7d366d4) y la pista con el cursor; con menos filas que las que se ven, el cursor ocupa la
// pista.
void DrawTheFrame(Render::SpriteBatch& sprites, JewelBundleList& list, bool click, UI::Point mouse)
{
    const auto in = [&](std::int32_t x, std::int32_t y)
    { return UI::MouseInRect(mouse.x, mouse.y, x, y, ArrowSize, ArrowSize, TopLeftRect); };
    sprites.SetBlend(Render::BlendMode::Alpha);
    Palette(sprites, FramePalette, FrameAlpha);
    sprites.ColorQuad(F32(list.x - 1), F32(list.y - list.height - 1), F32(list.width + 1), F32(list.height + 2));
    WhiteWithTexture(sprites);
    const UI::OldListBar bar =
        UI::PlaceTheListBar(list.y, list.height, list.barMode, list.shownRows, Count(list), list.scroll);
    list.barTop = bar.barTop;
    list.barBottom = bar.barBottom;
    list.thumbY = bar.thumbY;
    list.thumbHeight = bar.thumbHeight;

    const std::int32_t right = list.width + list.x;
    const float arrowX = F32(right - static_cast<std::int32_t>(ArrowRight));
    if (click && in(right - ArrowHitRight, list.y - list.height - 1))
        Quad(sprites, arrowX, F32(list.y - list.height - 1), ArrowQuad, ArrowQuad, ArrowUv, UpArrowPressedV, -ArrowUv,
             -ArrowDv);
    else
        Quad(sprites, arrowX, F32(list.y - list.height - 1), ArrowQuad, ArrowQuad, 0.0f, UpArrowV, ArrowUv, ArrowDv);
    const float downY = F32(list.y - static_cast<std::int32_t>(ArrowRight));
    if (click && in(right - ArrowHitRight, list.y - ArrowHitRight))
        Quad(sprites, arrowX, downY, ArrowQuad, ArrowQuad, ArrowUv, DownArrowV, -ArrowUv, -ArrowDv);
    else
        Quad(sprites, arrowX, downY, ArrowQuad, ArrowQuad, 0.0f, DownArrowV, ArrowUv, ArrowDv);

    sprites.SetBlend(Render::BlendMode::Alpha);
    Palette(sprites, TrackPalette, 1.0f);
    const float trackHeight = F32(static_cast<double>(list.barBottom) - static_cast<double>(list.barTop));
    sprites.ColorQuad(F32(static_cast<double>(right) - static_cast<double>(list.thumbWidth) + 1.0), list.barTop, 1.0f,
                      trackHeight);
    sprites.ColorQuad(F32(right), list.barTop, 1.0f, trackHeight);
    WhiteWithTexture(sprites);

    const float thumbX = F32(static_cast<double>(right) - static_cast<double>(list.thumbWidth) + ThumbInset);
    const float thumbWidth = F32(static_cast<double>(list.thumbWidth) - ThumbInset);
    float top = list.thumbY;
    float height = list.thumbHeight;
    if (Count(list) < list.shownRows)
    {
        top = list.barTop;
        height = trackHeight;
    }
    Quad(sprites, thumbX, top, thumbWidth, height, 0.0f, ThumbV, ThumbDu, ThumbDv);
    Quad(sprites, thumbX, top, thumbWidth, 1.0f, 0.0f, 0.0f, ThumbDu, ThumbDv);
    Quad(sprites, thumbX, F32(static_cast<double>(list.thumbHeight) + static_cast<double>(top) - 1.0), thumbWidth, 1.0f,
         0.0f, ThumbBottomV, ThumbDu, ThumbDv);
}

// 0x46f020, la vtable + 0x80: el renglón `row` desde el primero que se ve, con el velo del elegido, y su texto con
// 0x465950 en la fuente del DC. Deja la mezcla apagada (0x620980).
Core::Expected<void, std::string> DrawTheRow(GameHudContext& context, const JewelBundleList& list, std::int32_t row,
                                             const JewelBundleList::Row& entry, std::uint8_t kind)
{
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (list.selection == list.scroll + 1 + row)
    {
        sprites.SetColor(ChosenVeil);
        sprites.ColorQuad(F32(list.x), F32(RowY(list, row) - RowRise), F32(RowWidth(list)), F32(RowHeight));
        sprites.SetColor(White);
        hud.textColors.text = ChosenTextColor;
    }
    else
        hud.textColors.text = RowTextColor;
    sprites.EnableTexture();
    hud.textColors.background = 0;
    const std::size_t index = kind == 0 ? BlessName : SoulName;
    const std::optional<std::string_view> name = UI::TextAt(context.texts, index);
    if (!name)
        return Core::Unexpected(std::format("0x46f152: sin el texto {} de Text.bmd", index));
    std::string text = std::format("{},  {}", *name, (static_cast<std::int8_t>(entry.level) + 1) * JewelsPerLevel);
    if (text.size() >= RowRoom)
        return Core::Unexpected(std::format("0x46f152: el sprintf escribe {} bytes en un buffer de {} y pisa la pila",
                                            text.size() + 1, RowRoom));
    context.text.Draw(list.x + RowTextX, RowY(list, row), text,
                      Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace

void ClearTheBundles(JewelBundleList& list)
{
    list.rows.clear();
    // 0x483610 (+0x48) con 0 sobre ninguna fila.
    Select(list, 0);
    list.scroll = 0;
}

void AddTheBundle(JewelBundleList& list, std::int32_t cell, std::uint8_t level)
{
    if (cell < 0 || cell > LastCell)
        return;
    if (std::ranges::any_of(list.rows, [cell](const JewelBundleList::Row& row) { return row.cell == cell; }))
        return;
    list.rows.push_back(JewelBundleList::Row{.flag = 0, .cell = cell, .level = level});
    // 0x4842b0 (+0x6c) recorta a 150 filas (+0xfc): con las casillas de [0, 0x40] sin repetir, nunca pasa de 65.
    Select(list, 0);
    if (list.shownRows < Count(list))
        ++list.scroll;
    if (list.shownRows <= Count(list) && Count(list) - list.scroll < list.shownRows)
        list.scroll = Count(list) - list.shownRows;
    // 0x46e4c0: el sort de la STL con 0x46e4a0 (la casilla, menor); las casillas no se repiten.
    std::ranges::sort(list.rows, {}, &JewelBundleList::Row::cell);
}

std::optional<JewelBundleList::Row> ChosenBundle(JewelBundleList& list)
{
    if (list.rows.empty())
    {
        list.selection = 0;
        return std::nullopt;
    }
    if (list.selection < 1 || list.selection > Count(list))
        return std::nullopt;
    return list.rows[static_cast<std::size_t>(list.selection - 1)];
}

BundleListStep StepTheBundleList(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    JewelBundleList& list = runtime.npcWindows.jewels.bundles;
    bool& click = runtime.movement.pendingClick;
    ui.capture = list.id;
    // 0x45aa8c..0x45aab1: la cola, en orden. La vtable + 0x24 (0x45abc0) solo atiende el 8, y la + 0x1c (0x403160)
    // no hace nada.
    while (!list.messages.empty())
    {
        const std::int32_t message = list.messages.front();
        list.messages.erase(list.messages.begin());
        HandleTheMessage(list, message);
    }
    const bool over =
        UI::MouseInRect(context.mouseX, context.mouseY, list.x, list.y, list.width, list.height, BottomLeftRect);
    // 0x45aad0..0x45abb3: con el mouse agarrado por ella misma, la lista siempre sigue. Con el izquierdo abajo,
    // 0x490aa0 sobre el administrador (0x5763514) no la encuentra: no está registrada ahí (*inferido*, como el
    // registro del chat).
    if (over && ui.hovered == 0)
        ui.hovered = list.id;

    // 0x483bc0, la vtable + 0x20.
    if (ui.focusedList == list.id)
    {
        static_cast<void>(ReadTheKey(context.input, Key::Left));
        static_cast<void>(ReadTheKey(context.input, Key::Right));
        const bool up = ReadTheKey(context.input, Key::Up);
        if (up)
            RepeatTheKey(list, NextMessage);
        if (ReadTheKey(context.input, Key::Down))
            RepeatTheKey(list, PreviousMessage);
        else if (!up)
            list.keyRepeat = 0;
    }
    // 0x484650, la vtable + 0x78: el mouse sobre la lista.
    if (over)
    {
        if (ui.wheel != 0)
        {
            Scroll(list, ui.wheel * -WheelRows);
            ui.wheel = 0;
        }
        if (!click)
        {
            list.arrowRepeat = 0;
            list.trackRepeat = 0;
        }
        else
        {
            ui.focusedList = list.id;
            PressTheBar(context, list, runtime);
        }
        ui.mouseOnUi = 1;
    }
    BundleListStep result{};
    result.asked = HoverTheRows(context, list, click, ui.rightReleased);
    ui.capture = 0;
    return result;
}

Core::Expected<void, std::string> DrawTheBundleList(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    WorldRuntime& runtime = context.world.Runtime();
    // Un dibujo intermedio de MuRet no escribe la esquina ni la barra que lee el paso: dibuja una copia.
    JewelBundleList projected{};
    if (context.projecting)
        projected = runtime.npcWindows.jewels.bundles;
    JewelBundleList& list = context.projecting ? projected : runtime.npcWindows.jewels.bundles;
    // 0x45a9d0(x, y).
    list.x = x;
    list.y = y;
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheFrame(sprites, list, runtime.movement.pendingClick, context.mouse);
    // 0x4832ef..0x483312: glColor3f(1, 1, 1) y la fuente normal en el DC.
    sprites.SetColor(White);
    context.hud.dcFont = Render::Text::FontKind::Normal;
    auto at = static_cast<std::size_t>(list.scroll);
    for (std::int32_t row = 0; row < list.shownRows && at < list.rows.size(); ++row, ++at)
    {
        if (auto drawn = DrawTheRow(context, list, row, list.rows[at], runtime.npcWindows.jewels.kind);
            !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace Client::Scenes
