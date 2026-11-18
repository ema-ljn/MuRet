#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/ChatLog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// El paso del registro del chat (CL8 hito 3; 06 § El chat): 0x45aa80, su vtable + 0x18, que corre desde el paso de
// la UI (0x636e4c) con 0, y lo que llama: el mouse (0x479960), la barra, los botones y el borde de arriba (0x45d760 y
// 0x45d990) y el mouse sobre los nombres (0x45dec0). También la barra que calcula el dibujo del marco (0x47a230).
namespace Client::UI
{
// Los globales de la UI que lee y escribe el paso.
struct ChatLogGlobals
{
    // 0x7d366d4: el flanco del clic izquierdo, que lo apaga quien lo usa.
    bool& leftClick;
    // 0x7d3685c: el derecho abajo, que 0x45d760 apaga cuando lo usa.
    bool& rightDown;
    // 0x7d366d8: la rueda en pasos de 120.
    std::int32_t& wheel;
    // 0x5762eb4: la ventana que agarró el mouse (el borde o la barra).
    std::int32_t& capture;
    // 0x5762eb8: la ventana bajo el mouse; el paso de la UI lo vuelve a 0 antes de las ventanas.
    std::int32_t& hovered;
    // 0x5762ec0: la lista que toma las flechas del teclado.
    std::int32_t& focusedList;
    // 0x775857c.
    std::uint8_t& mouseOnUi;
    // 0x6c7a88 y 0x6c8dd1.
    ChatModes& modes;
};

// Lo que el paso lee del juego.
struct ChatLogStepView
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x7d36874: el izquierdo abajo.
    bool leftDown = false;
    // 0x6c8e68: el campo del chat abierto.
    bool chatOpen = false;
    // +0x3e2 del héroe.
    bool heroSafeZone = false;
    // +0x1d9 del héroe, hasta su '\0'.
    std::string_view heroName;
    // GetFocus() en uno de los dos EDIT del chat (+0x8c y +0x178 de 0x57634f8).
    bool chatEditFocused = false;
    // GetTextExtentPointA sobre el DC de los textos (0x57634f4), con la fuente que tenga.
    const Render::Text::ITextMeasure* measure = nullptr;
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
};

struct ChatLogStepResult
{
    // 0x41ed10(id, 0, 0): los sonidos que pide, en orden.
    std::vector<std::int32_t> sounds;
    // 0x4694c0(0, 0, 1, nombre) sobre 0x57634f8: el nombre del renglón elegido para el campo del susurro (su EDIT
    // de +0xf0, vtable + 0x4c), con el clic derecho.
    std::optional<std::string> whisperTo;
    // Lo que devuelve 0x479960: el mouse estaba sobre la lista, su barra o sus botones.
    bool handled = false;
};

// 0x459f00(x, y, w, h, modo): el mouse en el rectángulo; con el modo 2, `y` es el borde de abajo.
[[nodiscard]] bool MouseInRect(std::int32_t mouseX, std::int32_t mouseY, std::int32_t x, std::int32_t y,
                               std::int32_t width, std::int32_t height, std::int32_t mode);

// La barra de una lista vieja: los bordes de la pista (+0x108 y +0x10c), y el lugar y el alto del cursor (+0x110 y
// +0x118).
struct OldListBar
{
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbHeight = 0.0f;
};

// 0x47a230 y 0x484490, iguales byte a byte salvo los saltos: la barra de una lista con la esquina de abajo en `y`, de
// `height` de alto, el modo de +0x12c, `rows` renglones a la vista, `count` en la lista (la vtable + 0x54) y `scroll`
// el primero que se ve.
[[nodiscard]] OldListBar PlaceTheListBar(std::int32_t y, std::int32_t height, std::int32_t barMode, std::int32_t rows,
                                         std::int32_t count, std::int32_t scroll);

// 0x47a230, la vtable + 0x70: los bordes de la pista y el tamaño y el lugar del cursor de la barra.
void PlaceTheLogBar(const ChatLogState& log, ChatModes modes, ChatLogFrame& frame);

// 0x45aa80(0), la vtable + 0x18. Con el mouse sobre el rectángulo de la lista (el modo de +0x78 es 2), la marca como
// la de abajo del mouse si no hay otra o si nadie la agarró; quieta (+0x48 en 0), sin ser la de abajo ni la que agarró,
// no sigue. Después 0x479960.
[[nodiscard]] ChatLogStepResult StepTheChatLog(ChatLogState& log, ChatLogFrame& frame, const ChatLogGlobals& globals,
                                               const ChatLogStepView& view);
} // namespace Client::UI
