#pragma once

#include <cstddef>
#include <cstdint>

namespace Client::Scenes
{
// 0x5f0b40(mapa): el texto de Text.bmd con el nombre del mapa, que devuelve como dirección (0x76bd1dc + índice · 300).
// Compara el int con signo, en este orden: 0x22 el 1851, 0x21 el 1850, 0x1e el 669, 0x1f el 59, de 0x12 a 0x17
// (Chaos Castle) el 57, de 0x18 a 0x1d (Kalima) y 0x24 el 58, de 0x0b a 0x11 (Blood Castle) el 56, 0x0a el 55, 0x20 el
// 39 y cualquier otro el 30 + mapa (0x76bf504 + mapa · 300). Lo usan la ventana del grupo (0x5f0ec8) y el renglón de
// bienvenida del F3 03 (0x4c4b38).
[[nodiscard]] std::size_t MapNameText(std::int32_t map);
} // namespace Client::Scenes
