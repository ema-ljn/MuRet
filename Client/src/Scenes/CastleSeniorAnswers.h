#pragma once

#include "Scenes/CastleSiegeReceive.h"

#include <cstdint>
#include <string_view>

namespace Client::Core
{
class Log;
}

namespace Client::UI
{
struct CastleData;
}

// Las respuestas del diálogo de los paneles ([0x7d53704], hito 8) que abren las ventanas del Castle Senior
// ([0x7d536fc], el panel 0x100000; CL9 hito 12): cada una mira la elegida y su registro en 0x7d5c504..0x7d5c658 y
// manda su B2 por el socket del juego. Devuelven 1 al diálogo en el original; acá, nada. La respuesta 8 es la que
// manda (*inferido*: el sí del diálogo), y en la de la plata, la 2.
namespace Client::Scenes
{
struct CastleAnswerContext
{
    const UI::CastleData& data;
    // Una elegida cuyo registro cae fuera de la zona (Q-C1360) o un campo que no se lee (Q-C1363): un aviso y nada.
    Core::Log& log;
    CastleSender send{};
};

inline constexpr std::int32_t CastleSendAnswer = 8;
inline constexpr std::int32_t CastleMoneyAnswer = 2;

// 0x6623e0 y 0x664370: la puerta elegida (0x7d5c638, desde 0x7d5c590) o la estatua (0x7d5c63c, desde 0x7d5c520), con
// +0x1a en 0 el B2 05 (comprarla) y si no el B2 06 (repararla), con su tipo y su índice.
void AnswerTheGateDialog(std::int32_t answer, const CastleAnswerContext& context);
void AnswerTheStatueDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x662d50 y 0x664ce0: el B2 07 con 3 y la vida máxima del nivel siguiente (0x660d20 con 0x660da0 + 1).
void AnswerTheGateLifeDialog(std::int32_t answer, const CastleAnswerContext& context);
void AnswerTheStatueLifeDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x663440 y 0x6653d0: el B2 07 con 1 y la defensa + 1 (0x660f30: +8 de una puerta o una estatua, −1 de otro tipo).
void AnswerTheGateDefenseDialog(std::int32_t answer, const CastleAnswerContext& context);
void AnswerTheStatueDefenseDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x665ab0: el B2 07 con 2 y la recuperación + 1 (0x661020: 0 de una puerta, +0xc de una estatua, −1 de otro tipo).
void AnswerTheStatueRecoveryDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x666b30: el B2 09 con 1 y el impuesto de 0x7d5c640, y el B2 09 con 2 y el de 0x7d5c644.
void AnswerTheTaxDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x667a40: con la respuesta 2, sscanf("%u") del texto del campo del diálogo (0x6602d0, hito 8) y el B2 10.
void AnswerTheMoneyDialog(std::int32_t answer, std::string_view field, const CastleAnswerContext& context);
} // namespace Client::Scenes
