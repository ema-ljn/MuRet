#pragma once

#include "Network/Packet.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The values XorRemapObfuscation translates with. The packet offsets and sizes are its mechanism and stay with it;
// the values come from the composition root (App/LabXorRemapTables.h for the lab server).
namespace Client::Network::XorRemap
{
struct OpcodeRemap
{
    std::uint8_t from;
    std::uint8_t to;
};

// SelectCode subtracts `subtract` from a server code in [first, first + ladderStepCodes).
struct LadderStep
{
    std::uint16_t first;
    std::uint16_t subtract;
};

struct XorRemapTables
{
    std::byte inboundXor;
    std::array<OpcodeRemap, 3> encryptedRemaps;
    std::array<OpcodeRemap, 2> clearRemaps;
    Opcode serverOpcode;
    Subcode labServerListSub;
    Subcode serverListSub;
    Subcode serverSelectSub;
    Opcode characterOpcode;
    Subcode skillListSub;
    std::uint32_t codeGroupSize;
    std::array<std::uint16_t, 9> listCodeDeltas;
    std::uint16_t ladderStepCodes;
    std::array<LadderStep, 8> selectLadder;
};
} // namespace Client::Network::XorRemap
