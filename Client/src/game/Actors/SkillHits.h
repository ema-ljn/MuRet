#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

// 0x517a20: a skill's targets around a point and the C1 10 that sends them (FoxAnti: 1D). Called by the skill arrows'
// impact (0x520170 at 0x520497, Effects/ArrowEffects); the matrix puts it in CL11 (06, "La flecha").
namespace Client::Actors
{

// The six arguments of 0x517a20.
struct SkillHitsRequest
{
    // The skill's slot in the own record (the byte of +0x6c + slot, [0x7681eb0]).
    std::uint8_t skillIndex = 0;
    glm::vec3 point{};
    float radius = 0.0f;
    // The volley's count, which goes in the packet (the arrow's +0x8c).
    std::uint8_t volley = 0;
    // The key (the arrow's +0x8a): a player (+0x88 at 1) enters only with it in +0x1f4.
    std::uint16_t key = 0;
    // What goes after each key in the packet.
    std::uint8_t extra = 0;
};

// 0x517a20: the slot's skill with the own record opened and closed (3848 rand() if it is left at 0); on map 30 with
// the siege on, it cuts (0x517b1e, CL9.5). Then, from the 400 in order, those alive, visible (+0x16c), neither the hero
// ([0x743a170]) nor dying (+0x389), at `radius` or less in the plane (the root on the x87 with PC53, stored as a float;
// NaN enters), and monsters (+0x88 at 2) or the key's player: up to five. With skill 8 or 9, each one with +0x394 at 0
// gets +0x398 at 10. With none, false. With "webzen" in the hero's name, true without sending. Otherwise the C1 10
// (Protocol::Skills::WriteSkillHits) with the point's cell (ftol of 0x69fd3c; outside the integer range, cuts at
// 0x517deb) and as a C3: its two rand() (padding and serial) come out even without `send`. true.
[[nodiscard]] Core::Expected<bool, std::string>
HitTheTargetsAround(const SkillHitsRequest& request, Kernel::WorldState& world, const Terrain::WorldTerrain& terrain,
                    Crt::CrtRandom& random, const std::function<void(Protocol::Outgoing)>& send);
} // namespace Client::Actors
