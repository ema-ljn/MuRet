#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Cuánto avanza cada acción de Player.bmd por paso de lógica: el lector no escribe la velocidad de una
// acción (Q-C87) y 0x60dbe0 la fija a mano al final, después de Shadow (0x60f335..0x60f754), en
// keys por paso de 40 ms (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto avanza la
// animación"). Lo que hace el paso con ella es de la CL6.2.
namespace Client::Data::Models
{
// Una escritura de 0x60dbe0: la misma velocidad en las acciones de `first` a `last`, un bucle o una
// sola instrucción del original.
struct ActionSpeedWrite
{
    std::uint8_t first = 0;
    std::uint8_t last = 0;
    float speed = 0.0f;
};

// Las 181 escrituras, en el orden del original y agrupadas como las escribe: 47 instrucciones. Las de
// después pisan a las de antes.
[[nodiscard]] std::span<const ActionSpeedWrite> PlayerActionSpeeds();

// La velocidad que deja la última escritura que toca `action`, o std::nullopt si ninguna la toca: 57
// de las 205 se quedan con lo que había en el heap (Q-C87, P-C14 a).
[[nodiscard]] std::optional<float> PlayerActionSpeed(std::size_t action);

// Las que no repiten y se quedan en la última key: 0x60dbe0 les pone +0 en 1 (0x60f54c..0x60f585), en
// este orden.
inline constexpr std::array<std::uint8_t, 4> PlayerActionsWithoutRepeat = {195, 196, 193, 68};
} // namespace Client::Data::Models
