#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// El C1 05 18 [dir] [acción] que manda el cliente (04 § La acción, 18): el giro del héroe quieto (0x59b74c), sentarse
// (0x4f2416..0x4f2bea) y el emote (0x576a60 y los casos de 0x576eb0). Los cuatro sacan la dirección del ángulo +0x24
// de un personaje con la misma cuenta. *Estático*.
namespace Client::Scenes
{
// Quien manda un C1 18 por el socket del GameServer (App::ServerSession::SendAction), con la dirección y la acción.
using ActionSender = std::function<void(std::uint8_t direction, std::uint8_t action)>;

// ftol((ángulo + 22,5f) · 0,0222f + 1) & 0xff y después el resto con signo por 8 (0x576b44..0x576b6e,
// 0x4f248a..0x4f24b4, 0x59b456), que con el & 0xff antes es & 7. La cuenta va en double, sin redondear a float en el
// medio, como la x87 con PC53 (04 § Caminar y pegar). El _ftol trunca hacia el 0: de −112,5° a −22,5° da 0 (Q-C777).
// Vacío si el _ftol no tiene un resultado portable (World::OriginalFtolLow).
[[nodiscard]] std::optional<std::uint8_t> ActionDirection(float angle);

// El C1 18 de `action` con la dirección de `angle`. Sin `send`, o con un ángulo que el _ftol no resuelve, el error con
// la dirección `where`.
[[nodiscard]] Core::Expected<void, std::string> SendTheAction(float angle, std::uint8_t action,
                                                              const ActionSender& send, std::string_view where);
} // namespace Client::Scenes
