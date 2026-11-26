#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Walking and hitting (04, "Caminar y pegar"): the D3 both ways and the D7 that arrives. The writer is SendMove's
// (0x56acd0) pure part; its UI, state and sending conditions belong to the scene.
namespace Client::Protocol::Movement
{
inline constexpr std::size_t MaxWalkCells = 15;
inline constexpr std::size_t MaxWireWalkCells = 14; // 0x56ad8d..0x56ad96: the local path is not cut.
// 0x59b74c: the standing hero's turn. 0x4f2416..0x4f2bea and 0x4e137a: sitting down.
inline constexpr std::uint8_t FacingAction = 0x7a;
inline constexpr std::uint8_t SittingAction = 0x82;
// The client's 18 (04, "La acción"; the GameServer's PMSG_ACTION): C1 05 18 direction action, with the XOR32 and in
// clear, the direction as it is. The turn (0x59b333..0x59b83a, FacingAction), sitting (0x4f2416..0x4f2bea,
// SittingAction), the emote (0x576a60 and each case of 0x576eb0) and the BD 03 with result 1 (0x4e11e1..0x4e1467:
// sitting on Crywolf's altar, SittingAction).
[[nodiscard]] Outgoing WriteAction(std::uint8_t direction, std::uint8_t action);

struct Cell
{
    std::uint8_t x;
    std::uint8_t y;
};

enum class WalkWriteError : std::uint8_t
{
    InvalidPathSize,
    InvalidAngle,
};

struct Walk
{
    std::uint16_t characterId;
    Cell cell;
    std::uint8_t direction;
};

// The server's 18 (0x4cb750; PMSG_ACTIONRESULT): pkt[3..4] the key, big endian, pkt[5] the direction and pkt[6] the
// action.
struct Action
{
    std::uint16_t key;
    std::uint8_t direction;
    std::uint8_t action;
};

struct Damage
{
    std::uint16_t characterId;
    bool shock;
    std::uint16_t amount;
    std::uint8_t type;
    bool flag;
    bool doubleHit = false; // pkt[7] & 0x40, 0x4cab40 and 0x4cb61c.
};

// A path of 1 to 15 cells, its start included, as a C1 with the XOR32 and Delivery::Clear. The target is an explicit
// input: the wire cannot rebuild it. Only one cell reads `angle`. The original's conversion truncates (0x69fd4b) with
// FISTP m64int (0x69fd55, EDX:EAX) and SendMove keeps the low bits & 7. MuRet rejects non-finite angles and counts
// outside [-2^63, 2^63): errors of its API, not a check of the original.
[[nodiscard]] Core::Expected<Outgoing, WalkWriteError> WriteWalk(std::span<const Cell> path, Cell target, float angle);

// The heading 0 to 7 of `angle`, as 0x56b0b7..0x56b0cc (and 0x4e1260..0x4e128a, which adds an AND 0xff): ftol((angle +
// 22.5) * 0x3cb60b61 + 1) & 7 on the x87, never stored as a float. SendMove's, and the D3's and D7's of the attack on
// arrival (0x5656e6..0x565715, 0x5659b8..0x5659e2).
[[nodiscard]] Core::Expected<std::uint8_t, WalkWriteError> DirectionFromAngle(float angle);

// 0x56ad1d..0x56ad4f: the ORIGINAL count (1 to 15), before the wire's limit of 14.
[[nodiscard]] Core::Expected<std::uint32_t, WalkWriteError> WalkUpdateTimeMax(std::size_t pathNum);

[[nodiscard]] Core::Expected<Walk, Network::ProtocolError> ReadWalk(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Damage, Network::ProtocolError> ReadDamage(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Action, Network::ProtocolError> ReadAction(const Network::Packet& packet);
} // namespace Client::Protocol::Movement
