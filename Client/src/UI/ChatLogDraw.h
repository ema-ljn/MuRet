#pragma once

#include "Data/Text/TextTable.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "UI/ChatLog.h"

#include <cstdint>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Scenes
{
class ITextDrawer;
}

// El dibujo del registro del chat (CL8 hito 3; 06 § El chat): 0x479080, su vtable + 0x14, que el HUD llama en el
// juego (0x5a7320, paso 17). Arma el marco con la barra (0x45c460, solo con el registro en su ventana), los renglones
// (0x45cbb0) y los botones (0x45d420), todo en 640 × 480.
namespace Client::UI
{
// Los slots de Interface\ que carga 0x61ccb0: el fondo del marco (Message_box2.jpg), el agarre (nis_rsframe), los
// lados (nis_vframe), el cursor (nis_bar), la pista (nis_back), las flechas (nis_btnarrow) y los botones (nis_btnchat,
// nis_btnsize y nis_btnalpha). La pista la pisa SwordEff.jpg en la misma función (Q-C463).
inline constexpr std::uint16_t ChatFrameSlot = 0xfc;
inline constexpr std::uint16_t ChatGripSlot = 0x63d;
inline constexpr std::uint16_t ChatSideSlot = 0x63e;
inline constexpr std::uint16_t ChatThumbSlot = 0x63f;
inline constexpr std::uint16_t ChatTrackSlot = 0x640;
inline constexpr std::uint16_t ChatArrowSlot = 0x641;
inline constexpr std::uint16_t ChatShowButtonSlot = 0x642;
inline constexpr std::uint16_t ChatSizeButtonSlot = 0x643;
inline constexpr std::uint16_t ChatAlphaButtonSlot = 0x644;

// Lo que el dibujo usa y deja: el lote, los textos, la fuente del DC (0x57634f4) y los colores de 0x465950.
struct ChatLogDrawContext
{
    Render::SpriteBatch& sprites;
    Scenes::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c y 0x6c8e64.
    Render::Text::TextColors& textColors;
};

// Lo que el dibujo lee del juego.
struct ChatLogDrawView
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x7d36874: el izquierdo abajo.
    bool leftDown = false;
    // 0x6c8e68: el campo del chat abierto.
    bool chatOpen = false;
    // 0x723c05: con 3 o 4, los renglones bajan 0x75.
    std::uint8_t crywolfStage = 0;
    // Text.bmd, para el título del filtro (754) y las ayudas de los botones (750, 751 y 752).
    const Data::Text::TextTable* texts = nullptr;
};

// 0x479080: el marco (y la barra, 0x47a230), desde el primero que se ve hasta `rows` renglones o el final, y los
// botones. Escribe en `frame` el alto, la barra, el parpadeo, la transparencia de los botones y, en Crywolf con la
// lista en x = 0xba, la y (0x12f).
void DrawTheChatLog(const ChatLogState& log, ChatLogFrame& frame, ChatModes modes, const ChatLogDrawContext& context,
                    const ChatLogDrawView& view);
} // namespace Client::UI
