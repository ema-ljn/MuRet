#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

// Los sprintf y wsprintfA del original con sus argumentos tal cual (CL9 hito 11), para los textos de Text.bmd que el
// original usa de formato. A diferencia de UI::PlaceArguments, mira el tipo de cada argumento: un texto se pasa por
// su dirección (g_GlobalText, 0x76bd1dc + índice · 300), y un "%d" que lo toma imprime esa dirección, como el
// original con los textos 853 y 868 del Data en español.
namespace Client::UI
{
// 0x76bd1dc: el primer registro de Text.bmd en memoria, de 300 bytes cada uno (06 § Los textos).
inline constexpr std::uint32_t TextTableAddress = 0x076bd1dc;
inline constexpr std::uint32_t TextRecordBytes = 300;

// Un texto de Text.bmd como argumento: su índice (para la dirección) y su contenido.
struct TextArgument
{
    std::size_t index;
    std::string_view text;
};

// Un número por valor (un DWORD en la pila) o un texto por puntero.
using FormatArgument = std::variant<std::int32_t, TextArgument>;

enum class FormatFunction : std::uint8_t
{
    // 0x69fd9b, el sprintf de la CRT: las banderas "-+ #0", el ancho y la precisión. "% d" pone un espacio delante
    // de un número sin signo, y con "%s" el espacio no hace nada.
    Sprintf,
    // wsprintfA de user32 ([0x6b851c]): solo las banderas "-", "#" y "0". *Inferido* (P-C290): con otro carácter
    // después de las banderas, el ancho y la precisión (el ' ' de "% d"), lo copia y no toma argumento, y la 'd'
    // que sigue sale como texto.
    Wsprintf,
};

// `format` con `arguments` en orden. Las conversiones son d, i, u, x, X, c, s y %%. Un "%s" con un número (el
// original lee un puntero que no es), una conversión sin argumento (lee la pila) o una que MuRet no tiene cortan con
// error en `where`.
[[nodiscard]] Core::Expected<std::string, std::string> FormatOriginal(FormatFunction function, std::string_view format,
                                                                      std::span<const FormatArgument> arguments,
                                                                      std::string_view where);
} // namespace Client::UI
