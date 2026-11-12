#pragma once

#include "Core/Expected.h"
#include "UI/ChatInput.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>

// Data\Macro.txt (0x6c649c), las diez macros de 0x77f1db0 (CL8 hito 3; 06 § El chat). WinMain lo lee una vez,
// detrás de Text.bmd (0x61ee30 → 0x61c1c0); lo escriben la salida del juego, el cambio de servidor y el gate a los
// mapas 30 y 31 (0x61c170 desde 0x412b6d, 0x59ad45, 0x624067, 0x624edf y 0x624f1d), que son de otras olas. Lo que el
// jugador guarda en sus macros no va al log. *Estático*.
namespace Client::UI
{
// 0x77f1db0: cada macro en 0x100 bytes con su '\0'.
inline constexpr std::size_t MacroSlotBytes = 0x100;

using Macros = std::array<std::string, MacroCount>;

// 0x61c1c0: fopen("rt") y diez fscanf("%s"), una palabra por macro en su orden. "%s" salta el blanco (0x09..0x0d y
// el espacio, en el locale "C") y lee hasta el siguiente, así que una macro con espacios vuelve partida (Q-C449); al
// final del archivo, las que faltan quedan como estaban (vacías: la lectura es la primera). El modo texto de la CRT
// termina el archivo en el primer Ctrl-Z (0x1a). "%s" no tiene ancho: una palabra de MacroSlotBytes o más pisa la
// macro siguiente o lo que sigue a 0x77f27b0 (Q-C531); MuRet no la carga y devuelve el error.
[[nodiscard]] Core::Expected<Macros, std::string> ReadTheMacros(std::span<const std::byte> file);

// 0x61c170: fopen("wt") y diez fprintf("%s\n"), con el "\r\n" del modo texto. No mira si se abrió.
[[nodiscard]] std::string WrittenMacros(const Macros& macros);
} // namespace Client::UI
