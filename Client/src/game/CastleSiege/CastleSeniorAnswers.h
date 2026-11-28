#pragma once

#include "CastleSiege/CastleSiegeReceive.h"

#include <cstdint>
#include <string_view>

namespace Client::Core
{
class Log;
}

namespace Client::CastleSiege
{
struct CastleData;
}

// The answers of the panels' dialog ([0x7d53704], milestone 8) that the Castle Senior windows open ([0x7d536fc],
// panel 0x100000; CL9 milestone 12): each checks the selected one and its record in 0x7d5c504..0x7d5c658 and sends
// its B2 through the game socket. They return 1 to the dialog in the original; here, nothing. The comparisons at
// 0x662403 and 0x666b53 require answer 8; the one at 0x667a63, 2 to withdraw money.
namespace Client::CastleSiege
{
struct CastleAnswerContext
{
    const CastleSiege::CastleData& data;
    // A selected one whose record falls outside the zone (Q-C1360) or a field that does not parse (Q-C1363): a warning
    // and nothing.
    Core::Log& log;
    CastleSender send{};
};

inline constexpr std::int32_t CastleSendAnswer = 8;
inline constexpr std::int32_t CastleMoneyAnswer = 2;

// 0x6623e0 and 0x664370: the selected gate (0x7d5c638, from 0x7d5c590) or statue (0x7d5c63c, from 0x7d5c520), with
// +0x1a at 0 B2 05 (buy it) and otherwise B2 06 (repair it), with its type and its index.
void AnswerTheGateDialog(std::int32_t answer, const CastleAnswerContext& context);
void AnswerTheStatueDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x662d50 and 0x664ce0: B2 07 with 3 and the next level's max life (0x660d20 with 0x660da0 + 1).
void AnswerTheGateLifeDialog(std::int32_t answer, const CastleAnswerContext& context);
void AnswerTheStatueLifeDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x663440 and 0x6653d0: B2 07 with 1 and defense + 1 (0x660f30: +8 of a gate or a statue, −1 of another type).
void AnswerTheGateDefenseDialog(std::int32_t answer, const CastleAnswerContext& context);
void AnswerTheStatueDefenseDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x665ab0: B2 07 with 2 and recovery + 1 (0x661020: 0 of a gate, +0xc of a statue, −1 of another type).
void AnswerTheStatueRecoveryDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x666b30: B2 09 with 1 and the tax of 0x7d5c640, and B2 09 with 2 and the one of 0x7d5c644.
void AnswerTheTaxDialog(std::int32_t answer, const CastleAnswerContext& context);
// 0x667a40: with answer 2, sscanf("%u") of the dialog field's text (0x6602d0, milestone 8) and B2 10.
void AnswerTheMoneyDialog(std::int32_t answer, std::string_view field, const CastleAnswerContext& context);
} // namespace Client::CastleSiege
