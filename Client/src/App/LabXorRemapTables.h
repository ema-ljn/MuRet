#pragma once

#include "Network/XorRemap/XorRemapTables.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Skill.h"

#include <cstddef>

// The tables of the lab server's FoxAnti.dll layer. Contract: Client/docs/research/03-red-y-cifrado.md, "La capa de
// FoxAnti", and the research repo's analysis/foxanti/minimal/src/packet.c, which ports each function with its address.
namespace Client::App
{
constexpr Network::XorRemapTables LabXorRemapTables()
{
    return {
        // fox_xor_aa, 0x341140
        .inboundXor = std::byte{0xAA},
        // send_forward, 0x3416A0
        .encryptedRemaps = {{{0x10, 0x1D}, {0x19, 0xA5}, {0x1E, 0xA6}}},
        // encode_outbound, 0x341540
        .clearRemaps = {{{0xD3, 0xA3}, {0xD7, 0xA4}}},
        .serverOpcode = Protocol::Connection::ServerListOpcode,
        .labServerListSub = 0x02,
        .serverListSub = Protocol::Connection::ServerListSubcode,
        .serverSelectSub = Protocol::Connection::ServerSelectSubcode,
        .characterOpcode = Protocol::Character::CharacterOpcode,
        .skillListSub = Protocol::Skill::SkillListSubcode,
        // parse_f4_02's deltas by (code + 1) / 11, 0x34129C
        .codeGroupSize = 11,
        .listCodeDeltas = {0x190, 0x172, 0x154, 0x136, 0x118, 0xFA, 0xDC, 0xBE, 0xA0},
        // encode_outbound's ladder, 0x34156C to 0x34162F
        .ladderStepCodes = 0x14,
        .selectLadder = {{
            {0x190, 0x190},
            {0x17C, 0x172},
            {0x168, 0x136},
            {0x154, 0x118},
            {0x140, 0xFA},
            {0x12C, 0xDC},
            {0x118, 0xBE},
            {0x104, 0xA0},
        }},
    };
}
} // namespace Client::App
