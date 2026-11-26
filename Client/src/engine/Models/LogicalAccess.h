#pragma once

#include "EngineApi.h"

#include <cstdint>

// The original's protection table (0x57630c8; 06 § La tabla de protección, Q-C125 and Q-C183), without porting the
// encryption: the logical value lives in the clear in its owner, like the original's live variable, and of the entry
// only the byte counting the accesses that hold it open remains. Opening an access adds 1 to the byte and costs
// nothing; closing subtracts 1 and, if that leaves it at 0, draws from the CRT rand() the ones the original spends
// encrypting the entry again, one per byte. The byte wraps (255 + 1 = 0) and the close that leaves it at 0 spends,
// even if it is not the outermost one. Does not register or look up the entry (0x40ad90, 0x40b240), nor leave the
// rand() & 0xff of the last close in the variable (Q-C125), unless the owner asks for those bytes (`left`, up to 4).
// The byte's owner sets each entry's starting count: the original's is not read (CL7.P, P-C22 c).
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
// 0x412890, and inline at 0x40b570 and 0x40b800: a 4-byte variable, one rand() per byte (Q-C125).
inline constexpr std::uint16_t SmallLogicalCloseRandoms = 4;
// 0x647f60: the C3 serial (0x5767913), one byte, one rand() (CL8, milestone 8).
inline constexpr std::uint16_t SerialCloseRandoms = 1;
// 0x40b4d0: the own record of 0x7681eb4, 0xf08 bytes, one rand() per byte: 3848 (Q-C183).
inline constexpr std::uint16_t OwnRecordCloseRandoms = 0xf08;

class ENGINE_API LogicalAccess
{
public:
    // depth and random outlive the access; closeRandoms is what the entry spends when encrypted again
    // (SmallLogicalCloseRandoms or OwnRecordCloseRandoms). A discarded temporary would open and close in the same
    // statement: nodiscard warns about it.
    [[nodiscard]] LogicalAccess(std::uint8_t& depth, Crt::CrtRandom& random, std::uint16_t closeRandoms);
    // Same, and the close that leaves the count at 0 writes into `left` what 0x412890 leaves in the variable
    // (0x4128e2..0x4128f9): the rand() & 0xff of each byte, the first one lowest. With another count, `left` stays as
    // it was (the variable stays in the clear). Only with closeRandoms from 1 to 4.
    [[nodiscard]] LogicalAccess(std::uint8_t& depth, Crt::CrtRandom& random, std::uint16_t closeRandoms,
                                std::uint32_t& left);
    LogicalAccess(const LogicalAccess&) = delete;
    LogicalAccess& operator=(const LogicalAccess&) = delete;
    ~LogicalAccess();

private:
    std::uint8_t& m_depth;
    Crt::CrtRandom& m_random;
    std::uint16_t m_closeRandoms;
    std::uint32_t* m_left = nullptr;
};
} // namespace Client::Models
