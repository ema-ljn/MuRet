#pragma once

#include "Data/Text/TextTable.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// La búsqueda de los emotes en lo dicho (CL9 hito 1; 06 § El chat, "El emote"): 0x576eb0 hasta el salto a su caso.
// Lo que hace cada caso es Scenes::DoTheEmote. *Estático*.
namespace Client::UI
{
// 0x576eb0: los textos de Text.bmd que busca, del 270 al 348 y el 783.
inline constexpr std::size_t FirstEmoteText = 270;
inline constexpr std::size_t LastEmoteText = 348;
inline constexpr std::size_t LastEmoteExtraText = 783;
// Los 95 que busca: los 80 de Text.bmd y los 15 literales del binario.
inline constexpr std::size_t EmoteTriggerCount = 95;

// Un caso de 0x576eb0: la animación y la acción que le pasa a 0x576a60, y la acción de su propio C1 18.
struct EmoteCase
{
    std::uint8_t animation = 0;
    std::uint8_t action = 0;

    friend bool operator==(const EmoteCase&, const EmoteCase&) = default;
};

// 0x576eb0, la búsqueda (0x576ede..0x577f88): los 95 en el orden del original, cada uno con 0x576960(lo dicho, texto,
// 0); el primero que está decide el caso. Los 15 literales (0x6c90e0..0x6c912c) se comparan en CP949 tal cual. El 291,
// el 317 y el 348 se prueban al final, cada uno solo. Un texto que Text.bmd no tiene queda vacío, y un texto vacío
// 0x576960 lo encuentra en cualquier mensaje, como el original con sus textos en cero. Sin ninguno, vacío.
[[nodiscard]] std::optional<EmoteCase> FindTheEmote(const Data::Text::TextTable& texts, std::string_view said);
} // namespace Client::UI
