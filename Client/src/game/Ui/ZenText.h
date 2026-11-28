#pragma once

#include <cstdint>
#include <string>

// El zen como lo escriben las ventanas: 0x5b1130 (el texto) y 0x5b10f0 (su color), que llaman el diálogo de las
// misiones (0x41bca1 y 0x41bcb6) y el texto del 0x99 (0x63333d). El inventario tiene la misma cuenta copiada adentro de
// 0x5f2340 (0x5f26a7..0x5f2751 y 0x5f2771..0x5f27ef, CL8 hito 4).
namespace Client::Scenes
{
// 0x5b1130(zen, buffer): el zen con comas cada tres cifras ("%d,%03d,%03d,%03d" desde mil millones, 0x6c9844, y así
// hasta "%d", 0x6c9870).
[[nodiscard]] std::string FormatZen(std::uint32_t zen);

// 0x5b10f0(zen): el color del texto por cuánto es, con comparaciones sin signo: desde diez millones 0xff0000ff, desde
// un millón 0xff0096ff, desde cien mil 0xff18c900 y si no 0xff96dcff.
[[nodiscard]] std::uint32_t ZenColor(std::uint32_t zen);
} // namespace Client::Scenes
