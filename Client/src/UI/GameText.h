#pragma once

#include "Data/Text/TextTable.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Los textos de Text.bmd que arma la UI del login (CL5.5). El original los usa como formato de
// sprintf, como el 456 de la escena (Q-C61); MuRet no: cambia cada "%s" y cada "%d", en orden,
// por el argumento que sigue, y lo demás queda como está.
namespace Client::UI
{
// `format` con su primer "%s" o "%d" cambiado por arguments[0], el siguiente por arguments[1], y
// así. Una marca sin argumento queda; un argumento de más no va; un "%" que no es "%s" ni "%d",
// tampoco "%%", queda como está. Lo que entra de un argumento no se vuelve a leer.
[[nodiscard]] std::string PlaceArguments(std::string_view format, std::span<const std::string_view> arguments);

// El texto `index` de `texts`, o nada sin tabla o sin ese índice.
[[nodiscard]] std::optional<std::string_view> TextAt(const Data::Text::TextTable* texts, std::size_t index);
} // namespace Client::UI
