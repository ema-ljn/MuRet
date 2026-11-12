#pragma once

#include "Data/Text/TextTable.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Los textos de Text.bmd que arma la UI del login (CL5.5). El original los usa como formato de
// sprintf, como el 456 de la escena (Q-C61); MuRet no: cambia cada "%s" y cada "%d", en orden,
// por el argumento que sigue, y lo demás queda como está.
namespace Client::UI
{
// `format` con su primer "%s", "%d" o "%u" cambiado por arguments[0], el siguiente por arguments[1], y
// así (el %u es del 201 del HUD, la experiencia). Una marca sin argumento queda; un argumento de más no
// va; un "%" que no es "%s", "%d" ni "%u", tampoco "%%", queda como está. Lo que entra de un argumento no
// se vuelve a leer.
[[nodiscard]] std::string PlaceArguments(std::string_view format, std::span<const std::string_view> arguments);

// El texto `index` de `texts`, o nada sin tabla o sin ese índice.
[[nodiscard]] std::optional<std::string_view> TextAt(const Data::Text::TextTable* texts, std::size_t index);

// 0x632c90(texto, renglones, maxLines, lineBytes): `text`, hasta su primer '\0', en renglones de
// menos de `lineBytes` bytes, y a lo sumo `maxLines` (1 o más). Copia carácter por carácter y, si
// el que sigue haría llegar el renglón a `lineBytes`, lo corta: si hubo un espacio en ese renglón a
// menos del menor de lineBytes / 2 y 10 bytes del que no entró, después del espacio, que queda al
// final, y sigue desde ahí; si no, justo antes del que no entró. Lo que no entra en el último no
// va. Siempre da un renglón por lo menos. Va con un byte por carácter, como el _mbclen del original
// en una página de códigos de un byte (06 § La lista de servidores, *inferido* para el
// laboratorio); con una de dos bytes, el original no parte un carácter de dos.
[[nodiscard]] std::vector<std::string> SplitIntoLines(std::string_view text, std::size_t maxLines,
                                                      std::size_t lineBytes);
} // namespace Client::UI
