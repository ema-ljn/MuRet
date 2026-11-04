#pragma once

#include "Protocol/Character/Opcodes.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

// El nombre de un personaje en un pedido de la selección, como lo arman 0x63d990 (crear) y 0x643740
// (borrar): los bytes hasta el primer cero y ceros hasta NameSize.
namespace Client::Protocol::Character
{
// Los bytes de `name` hasta su primer cero, y ceros hasta el final: lo que venía después del primer
// cero no sale.
[[nodiscard]] std::array<std::byte, NameSize> NameUpToItsEnd(std::span<const std::byte, NameSize> name);

// Los bytes de `text` y ceros hasta NameSize, o std::nullopt si no entra o tiene un cero: el original
// manda un nombre de más de 10 bytes entero y el paquete crece (0x63d990), y MuRet no lo manda.
[[nodiscard]] std::optional<std::array<std::byte, NameSize>> NameFromText(std::string_view text);
} // namespace Client::Protocol::Character
