#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// El registro del chat, la lista nueva de 0x576350c (CL8 hito 3; 06 § El chat): el objeto de 0x640 bytes que arma
// 0x45b620 desde WinMain (vtable 0x6b8dd0) y que agrega 0x555190 antes del área vieja. Sus entradas van en un
// std::deque de 0x11c bytes, la más nueva adelante (push_front). Lo que leen sus funciones de afuera (el lugar del
// registro, 0x6c7a88, y F2, 0x6c8dd1) llega en ChatModes; lo que escriben el dibujo y el paso y dura (el lugar, el
// tamaño, la barra, los botones y el parpadeo), en ChatLogFrame, que el juego copia en sus dibujos intermedios.
namespace Client::UI
{
// +0x40: el id de la ventana, del contador 0x5762eb0 (0x459ef0). *Inferido*: antes que este arma WinMain ocho
// ventanas viejas (0x4674c0), que se llevan del 1 al 8.
inline constexpr std::int32_t ChatLogId = 9;
// +4 (strncpy de 0xb) y +0xf (0x100). Un nombre de 11 bytes queda sin '\0' y se lee seguido del texto.
inline constexpr std::size_t ChatNameBytes = 0xb;
inline constexpr std::size_t ChatTextBytes = 0x100;
// +0x140: 5 palabras de 0x100 bytes.
inline constexpr std::size_t ChatFilterWords = 5;
// 0x45dc40: la suma de los largos de las 5 palabras tiene que quedar por debajo de 31.
inline constexpr std::size_t ChatFilterTotalBytes = 0x1f;
// 0x45bb20 corta en 2 renglones de 180 de ancho (0x459c70(texto, 2 × 0x100, 0xb4, 2, 0x100, 0, 0)).
inline constexpr int ChatWrapWidth = 0xb4;
// Text.bmd: el aviso del filtro puesto (755) y el del filtro sacado (756), y los de los botones (750, 751, 752).
inline constexpr std::size_t FilterOnText = 755;
inline constexpr std::size_t FilterOffText = 756;
inline constexpr std::size_t FilterTitleText = 754;
inline constexpr std::array<std::size_t, 3> ChatButtonHelpTexts{750, 751, 752};
// 0x41ed10(id, 0, 0): los sonidos que pide.
inline constexpr std::int32_t ChatButtonSound = 0x19;
inline constexpr std::int32_t ChatFilterSound = 0x26;

struct ChatEntry
{
    // +0: lo cambia de paridad el mouse sobre el nombre (0x45dec0). Nadie lo lee.
    std::int32_t blink = 0;
    std::string name;
    std::string text;
    std::int32_t type = 0;
    // +0x114: el cuarto argumento de 0x45bb20, que 0x555190 da en 0.
    std::int32_t param = 0;
    // +0x118: cuántos agregados vacíos pasaron con el registro arriba (0x45bb10). 0x45bfe0 lo suma sin signo.
    std::uint32_t age = 0;

    friend bool operator==(const ChatEntry&, const ChatEntry&) = default;
};

struct ChatLogState
{
    // La más nueva primero.
    std::deque<ChatEntry> entries;
    // +0x48: 0 quieta, 1 cambiando el alto (el borde de arriba) y 2 arrastrando la barra.
    std::int32_t state = 0;
    // +0xc4: el renglón elegido, contando desde 1 entre los que se ven; 0, ninguno.
    std::int32_t selection = 0;
    // +0xfc.
    std::int32_t maxEntries = 150;
    // +0x100: cuántos de los que se ven quedan abajo, por debajo del último renglón.
    std::int32_t scroll = 0;
    // +0x104: los renglones que muestra.
    std::int32_t rows = 6;
    // +0x11c: dónde agarró la barra el mouse, desde su borde de arriba.
    float dragOffset = 0.0f;
    // +0x120 y +0x124: cuántos pasos lleva apretada una flecha o la pista. Con 16, repite en cada paso.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0x12c: 0 desde el constructor; con 1, la barra sería la de las otras listas (otros bordes).
    std::int32_t barMode = 0;
    // +0x134: la transparencia del fondo (el fondo sale con 1 − esto). La cambia el tercer botón y el F3 30.
    float alpha = 0.4f;
    // +0x138: los botones a la vista (el mouse pasó por abajo).
    std::int32_t buttonsShown = 0;
    // +0x140.
    std::array<std::string, ChatFilterWords> filter{};
};

// Lo que el dibujo y el paso escriben y dura.
struct ChatLogFrame
{
    // +0x50 y +0x54: la esquina de abajo a la izquierda. El paso la pone en cada vuelta (0x637036..0x637056) y el
    // dibujo de un renglón, en Crywolf, deja la y en 0x12f (0x45cbb0).
    std::int32_t x = 0xba;
    std::int32_t y = 0x1a4;
    // +0x58 y +0x5c. El marco (0x45c460) deja el alto con el registro en su ventana.
    std::int32_t width = 0x10c;
    std::int32_t height = 0xfa;
    // +0x108, +0x10c, +0x110, +0x114 y +0x118: la barra (0x47a230).
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbWidth = 13.0f;
    float thumbHeight = 0.0f;
    // +0x130: sube uno por dibujo del marco (0x45c460), también con el registro fuera de su ventana.
    std::int32_t blink = 0;
    // +0x13c: la transparencia de los botones, que sube y baja de a 0,2 (0x45d420).
    float buttonsAlpha = 0.0f;
};

// Los globales que leen las funciones de la lista: 0x6c7a88 (.data 1, el constructor lo pone en 0) y 0x6c8dd1
// (.data 1, F2).
struct ChatModes
{
    std::int32_t placement = 0;
    bool showChat = true;
};

// El nombre (+4) como lo leen lstrlenA, strcmp y sprintf: uno de 11 bytes no tiene '\0' y sigue con el texto.
[[nodiscard]] std::string NameOf(const ChatEntry& entry);

// 0x45c140: el que no se ve. El chat normal (3) sin F2; el de sistema y el de error (1 y 2) con el registro en su
// ventana.
[[nodiscard]] bool IsHiddenInTheLog(const ChatEntry& entry, ChatModes modes);

// 0x45c2e0: los que se ven. Cuenta los escondidos solo sin F2 o con el registro en su ventana.
[[nodiscard]] std::int32_t VisibleInTheLog(const ChatLogState& log, ChatModes modes);

// 0x45c3a0: el primero que dibuja, después de saltear `scroll` de los que se ven. Puede ser el final.
[[nodiscard]] std::size_t FirstShownInTheLog(const ChatLogState& log, ChatModes modes);

// 0x45c190: el elegido, el `selection`-ésimo de los que se ven. Sin él, el final.
[[nodiscard]] std::size_t SelectedInTheLog(const ChatLogState& log, ChatModes modes);

// 0x45bfe0(n), la vtable + 0x34: corre `lines` hacia arriba con n negativo. Solo con tantos como renglones o más
// (sin signo). Con el registro fuera de su ventana, sin correr y n = −6, busca por las edades cuántos entran.
void ScrollTheLog(ChatLogState& log, ChatModes modes, std::int32_t lines);

// 0x479760, la vtable + 0x58: la elección, hasta los que se ven.
void SelectInTheLog(ChatLogState& log, ChatModes modes, std::int32_t row);

// 0x47a050, la vtable + 0x6c: con 150 o más, saca los viejos de atrás y vuelve la elección a 0.
void TrimTheLog(ChatLogState& log, ChatModes modes);

// 0x478d10, la vtable + 0x2c: vacía la lista, la elección y el corrimiento.
void ClearTheLog(ChatLogState& log, ChatModes modes);

// 0x459c70(texto, renglones de 0x100, ancho, cuántos, 0x100, sangría, 0): corta `text` en hasta `lines` renglones de
// `width` en 640 × 480, midiendo con GetTextExtentPointA sobre el DC (`font`) y pasando a 640 con ftol(cx / rx). Un
// renglón que no entra se busca a saltos de la mitad: entra si mide entre el ancho − 4 y el ancho + 4. Devuelve los
// renglones escritos (los que no, vacíos) y lo que devuelve el original: los usados, o uno más si sobra texto. La
// división por el ancho medio de una letra (0x459d21) se rompe con un texto de letras de menos de un punto: el error.
struct WrappedText
{
    std::vector<std::string> lines;
    std::int32_t result = 0;
};
[[nodiscard]] Core::Expected<WrappedText, std::string> WrapByWidth(std::string_view text,
                                                                   const Render::Text::ITextMeasure& measure,
                                                                   Render::Text::FontKind font, App::PixelSize screen,
                                                                   int width, int lines, int indent);

// Lo que necesita medir 0x45bb20 al cortar.
struct ChatMeasure
{
    const Render::Text::ITextMeasure& measure;
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
    App::PixelSize screen{};
};

// 0x45bb20(nombre, texto, tipo, param), la vtable + 0x94:
//   - los dos vacíos: con el registro fuera de su ventana y sin correr, todas las entradas suben su edad;
//   - el chat normal sin F2 no entra; con F2, si el filtro (0x45ddc0) no deja pasar al nombre ni al texto, tampoco, y
//     con el filtro puesto y el sonido de los avisos (0x77f3b78) pide el 0x26;
//   - con menos de 20 bytes entre los dos, una entrada; si no, el texto cortado en 2 de 180 (WrapByWidth), con el
//     nombre en el primero;
//   - después, TrimTheLog, la elección en 0, el corrimiento uno más (si corría y el tipo se ve) y lo acota.
// Devuelve el sonido que pide.
[[nodiscard]] Core::Expected<std::optional<std::int32_t>, std::string>
AddToTheLog(ChatLogState& log, ChatModes modes, const ChatMeasure& measure, std::string_view name,
            std::string_view text, std::int32_t type, std::int32_t param, bool noticeSound);

// 0x45ddc0: sin filtro, todo pasa; con él, `text` tiene que tener alguna de las palabras (0x576960).
[[nodiscard]] bool PassesTheChatFilter(const ChatLogState& log, std::string_view text);

// 0x45de20: las palabras separadas por ", ". Con las cinco puestas, lee un byte más allá del objeto (+0x640): MuRet
// lo toma en 0 y no agrega la coma del final.
[[nodiscard]] std::string JoinedChatFilter(const ChatLogState& log);

// 0x45db50.
void ClearTheChatFilter(ChatLogState& log);

// 0x45dc40(/filter palabras): las palabras después del comando, hasta 5, mientras la suma de los largos de todas
// (con las que quedaban de antes) no llegue a 31: la primera que no entra corta todo si es la primera, o queda vacía.
// Devuelve el aviso de Text.bmd que agrega 0x555190("", aviso, 1): 755 con un filtro puesto, 756 si se sacó uno que
// había, ninguno si no.
[[nodiscard]] std::optional<std::size_t> SetTheChatFilter(ChatLogState& log, std::string_view command);

// 0x45db70(texto, n): entre los primeros n, uno con los mismos 16 primeros bytes de texto o con edad.
[[nodiscard]] bool SaidRecently(const ChatLogState& log, std::string_view text, std::int32_t count);

// 0x45d8c0, F4 y el segundo botón: (0, 6) → (1, 3) → (1, 6) → (1, 30) → (0, 6), y después ScrollTheLog(0).
void CycleTheLogSize(ChatLogState& log, ChatModes& modes);
} // namespace Client::UI
