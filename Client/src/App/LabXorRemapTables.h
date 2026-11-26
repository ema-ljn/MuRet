#pragma once

#include "Network/Packet.h"
#include "Network/XorRemap/XorRemapTables.h"
#include "Protocol/Session/Opcodes.h"
#include "Protocol/Opcodes.h"

#include <cstddef>

// The tables of the lab server's FoxAnti.dll layer. Contract: Client/docs/research/03-red-y-cifrado.md, "La capa de
// FoxAnti", and the research repo's analysis/foxanti/minimal/src/packet.c, which ports each function with its address.
namespace Client::App
{
constexpr Network::XorRemap::XorRemapTables LabXorRemapTables()
{
    return {
        // fox_xor_aa, 0x341140
        .inboundXor = std::byte{0xAA},
        // send_forward, 0x3416A0
        .encryptedRemaps = {{{Network::ByteOf(Protocol::SkillHitsOpcode), 0x1D},
                             {Network::ByteOf(Protocol::MagicAttackOpcode), 0xA5},
                             {Network::ByteOf(Protocol::DurationMagicOpcode), 0xA6}}},
        // encode_outbound, 0x341540
        .clearRemaps = {{{Network::ByteOf(Protocol::WalkOpcode), 0xA3},
                         {Network::ByteOf(Protocol::AttackOpcode), 0xA4}}},
        .serverOpcode = Protocol::Connection::ServerListOpcode,
        .labServerListSub = Protocol::Connection::ServerListRequestSubcode,
        .serverListSub = Protocol::Connection::ServerListSubcode,
        .serverSelectSub = Protocol::Connection::ServerSelectSubcode,
        .characterOpcode = Protocol::CharacterOpcode,
        .skillListSub = Protocol::Character::SkillListSubcode,
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
