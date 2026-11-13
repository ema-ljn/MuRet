#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Scenes
{
class ITextDrawer;
}

// El área de mensajes vieja y los avisos centrados (CL8 hito 3; 06 § El chat). Las dos tablas son de la .bss y las
// tres escenas las dibujan (el login, la selección y el juego, 0x6350b0, 0x634680 y 0x6371a0); sus relojes corren en
// cada paso, después del de la escena (0x637cf3 y 0x637cf8, dentro de 0x637700). Lo que el dibujo escribe y dura
// (los anchos de los renglones, los contadores del parpadeo, la fuente del DC) va en MessageFrameState, que el
// juego copia en sus dibujos intermedios.
namespace Client::UI
{
// Los tipos de mensaje de 0x555190 y 0x45bb20, por sus colores (0x5554e0 y 0x45cbb0) y por quién los manda.
inline constexpr std::int32_t WhisperMessage = 0;
inline constexpr std::int32_t SystemMessage = 1;
inline constexpr std::int32_t ErrorMessage = 2;
inline constexpr std::int32_t ChatMessage = 3;
inline constexpr std::int32_t PartyMessage = 4;
inline constexpr std::int32_t GuildMessage = 5;
// El "@@" del 00 (0x4c6230): ningún dibujo tiene su caso y sale con los colores del texto anterior.
inline constexpr std::int32_t GuildNoticeMessage = 6;

// 0x77d9868: 120 renglones de 0x118 bytes, con el nombre en +0 (11 bytes), el texto en +0xb (0x101), el tipo en
// +0x10c y el ancho que escribe el dibujo en +0x114.
inline constexpr std::size_t MessageAreaRows = 120;
// Lo que entra sin pisar el campo que sigue, sin el '\0'. El original copia con strcpy, sin tope: un nombre de más
// de 10 bytes sigue en el texto y uno de más de 0x100 en el tipo, que se escriben después. MuRet corta (Q-C462).
inline constexpr std::size_t MessageNameBytes = 10;
inline constexpr std::size_t MessageAreaTextBytes = 0x100;
// 0x77985c0: 6 avisos de 0x108 bytes, el texto en +0 (0x104) y el color en el byte +0x104.
inline constexpr std::size_t NoticeRows = 6;
inline constexpr std::size_t NoticeTextBytes = 0x103;
// 0x4b8841 (después de mandar) es el único que lo escribe, con el mismo 0x72 de la .data.
inline constexpr std::int32_t FirstShownRow = 0x72;
// Los relojes: 0x555190 deja 150 en 0x6c8eb8 y 0x554610 deja 300 en 0x6c8eb0; las .data traen 200 y 300.
inline constexpr std::int32_t MessageClockSteps = 150;
inline constexpr std::int32_t NoticeClockSteps = 300;

struct MessageAreaRow
{
    std::string name;
    std::string text;
    std::int32_t type = 0;

    friend bool operator==(const MessageAreaRow&, const MessageAreaRow&) = default;
};

struct NoticeRow
{
    std::string text;
    // +0x104: distinto de 0, el aviso sale fijo en 0xffc8ff64; en 0, parpadea.
    std::uint8_t highlighted = 0;

    friend bool operator==(const NoticeRow&, const NoticeRow&) = default;
};

struct MessageAreaState
{
    std::array<MessageAreaRow, MessageAreaRows> rows{};
    // 0x77f3b90: cuántos renglones hay; llega a 120 y después corre todo uno.
    std::int32_t count = 0;
    // 0x6c8eb4 (.data 0x72): el primero de los 6 que dibuja 0x5554e0. Siempre 0x72: los renglones nuevos no se ven
    // hasta que la cuenta llega a 0x73.
    std::int32_t firstShown = FirstShownRow;
    // 0x6c8eb8 (.data 200): el reloj de 0x5554b0.
    std::int32_t clock = 200;
    std::array<NoticeRow, NoticeRows> notices{};
    // 0x77f3b88.
    std::int32_t noticeCount = 0;
    // 0x6c8eb0 (.data 300): el reloj de 0x5547e0.
    std::int32_t noticeClock = NoticeClockSteps;
};

// Lo que escriben los dibujos y dura hasta el próximo frame (y el paso lo lee).
struct MessageFrameState
{
    // +0x114 de cada renglón: el ancho en píxeles del texto que dibujó 0x5554e0. Lo lee el clic derecho del paso
    // (0x59ff98). El corrimiento de 0x555190 no lo mueve: queda el del renglón que estaba.
    std::array<std::int32_t, MessageAreaRows> widths{};
    // 0x77f3b94 y 0x77f3b8c: suben de a uno por dibujo del área y de los avisos.
    std::int32_t areaBlink = 0;
    std::int32_t noticeBlink = 0;
};

// Lo que el dibujo del área lee del juego.
struct MessageAreaView
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x6c8e68: el campo del chat abierto.
    bool chatOpen = false;
    // +0x3e2 del héroe (0x743a170).
    bool heroSafeZone = false;
    // +0x1d9 del héroe, hasta su '\0'.
    std::string_view heroName;
    // 0x6c8dd1 (.data 1): F2, el chat normal (el tipo 3) también en las áreas.
    bool showChat = true;
    // 0x723c05: con 3 o 4, los renglones bajan 0x89.
    std::uint8_t crywolfStage = 0;
};

// El texto y la fuente que deja cada dibujo. `dcFont` es la del DC de los textos (0x57634f4): la eligen los
// SelectObject y la usan los GetTextExtentPointA que no eligen otra, como 0x555190.
struct MessageDrawContext
{
    Render::SpriteBatch& sprites;
    Scenes::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c y 0x6c8e64.
    Render::Text::TextColors& textColors;
    // 0x77f31b8 y 0x77f31bc: el tamaño del último texto, en 640 × 480.
    std::int32_t& textWidth;
    std::int32_t& textHeight;
};

// 0x5549a0(texto, cola, cabeza, largo): corta `text` en el primer espacio desde la mitad menos 2 o en la mitad
// más 2, lo primero que llegue; si el texto se acaba antes, todo va a la cola. La cola empieza con el espacio. Avanza
// de a un byte (el _mbclen del original en una página de códigos de un byte).
struct HalvedText
{
    std::string head;
    std::string tail;
};
[[nodiscard]] HalvedText SplitInHalf(std::string_view text);

// 0x555190 después de la lista nueva: con el nombre vacío o un tipo que no es 0, 3, 4, 5 ni 6, el renglón entra en la
// tabla vieja. Mide "%s: %s" con la fuente del DC; desde 256 píxeles lo corta en dos (SplitInHalf): el nombre con la
// cabeza y 10 espacios con la cola. Con la tabla llena corre todo un renglón antes de cada uno. Deja el reloj en 150.
void AddToTheMessageArea(MessageAreaState& area, const Render::Text::ITextMeasure& measure,
                         Render::Text::FontKind dcFont, std::string_view name, std::string_view text,
                         std::int32_t type);

// 0x554610(texto, color): elige la negrita en el DC, mide el texto y, desde 256 píxeles, lo corta en dos avisos.
// Con 6 avisos corre uno antes de cada uno. Deja el reloj en 300.
void AddNotice(MessageAreaState& area, const Render::Text::ITextMeasure& measure, Render::Text::FontKind& dcFont,
               std::string_view text, std::uint8_t highlighted);

// 0x5547e0, por paso: con el reloj en 0 o menos antes de bajarlo, un aviso vacío (que sube los demás) y 300.
void StepTheNoticeClock(MessageAreaState& area, const Render::Text::ITextMeasure& measure,
                        Render::Text::FontKind& dcFont);

// 0x5554e0: el área vieja, 6 renglones desde firstShown en y = 0, 13, ..., 65 (x = 0), con la fuente normal.
void DrawTheMessageArea(const MessageAreaState& area, MessageFrameState& frame, const MessageDrawContext& context,
                        const MessageAreaView& view);

// 0x554810: los 6 avisos, centrados en 320, desde y = 300 (o 20 con el registro del chat en su ventana, 0x6c7a88
// en 1), con la negrita y la mezcla normal.
void DrawTheNotices(const MessageAreaState& area, MessageFrameState& frame, const MessageDrawContext& context,
                    std::int32_t chatPlacement);
} // namespace Client::UI
