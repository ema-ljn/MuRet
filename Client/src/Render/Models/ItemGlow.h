#pragma once

#include "Render/Models/Shine.h"

#include <cstdint>
#include <optional>
#include <vector>

// Cuánto brilla una parte por su nivel, como 0x60b3d0 (Client/docs/research/06-render-escenas-y-ui.md,
// "El modelo en el binario", Q-C97): el factor del color del pase base y los pases que van después,
// en orden. *Estático*; el nivel 13 (el base, el 141, el 115 y el 120) está **verificado** en la traza.
namespace Client::Render::Models
{
// Un pase de brillo: uno con las UV de la normal (ShinePass), o, sin él, el 0x42 de un ítem
// excelente, que repite la malla con su textura y aditiva.
struct GlowPass
{
    std::optional<ShinePass> shine;

    friend bool operator==(const GlowPass&, const GlowPass&) = default;
};

struct ItemGlow
{
    // Por la luz del personaje, en el color del pase base.
    float baseFactor = 1.0f;
    std::vector<GlowPass> passes;
};

// El byte de las opciones de 0x60b3d0 (DAT_006c9dcc): 4 en la .data.
inline constexpr std::uint8_t DefaultGlowDetail = 4;

// 0x60b3d0 con el nivel ya recortado a 2 · detail + 5 si detail es menor que 4: de 7 y 8, el 0x44
// (Chrome) y el color · 0,8; de 9 y 10, además el 0x48 (Shiny), con · 0,9; de 11 y 12, el 0x240
// (Chrome2), el 0x48 y el 0x44; del 13, el 0x1040 (Chrome4), el 0x48 y el 0x44; del 14 y el 15,
// ninguno (Q-C97). Uno excelente suma el 0x42, y uno de set que no es excelente el 0x840 (Chrome3).
// Los niveles 3 a 6 laten con DAT_07d36ba8 en el original: acá, el color sin latir (P-C18).
[[nodiscard]] ItemGlow ItemGlowOf(std::uint8_t level, bool excellent, bool ancient,
                                  std::uint8_t detail = DefaultGlowDetail);
} // namespace Client::Render::Models
