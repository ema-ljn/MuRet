#pragma once

#include "Core/Expected.h"
#include "Kernel/WorldRuntime.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Protocol::Pets
{
struct PetInfoReply;
}

namespace Client::Crt
{
class CrtRandom;
}

// What the pets' A9 brings: the 0x723960 table (WorldRuntime::pets), the hero's raven and the own record's horse.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "El A9 de las mascotas". Each own-record access spends its
// 3848 rand() when it closes at 0.
namespace Client::Kernel
{
struct DarkSpirit;
} // namespace Client::Kernel

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Pets
{

// 0x420cd0, from 0x657670 on the way back to the selection: 0x723910 (the tab), 0x723911 (Ui::DelegatedPanels::pet)
// and the three pets' fifteen dwords to 0. No rand().
void ResetThePets(Kernel::WorldRuntime& runtime);

// 0x4221d0(type): with the own record opened and closed, the leadership (+0x36 + +0x20) and, for the horse, strength
// (+0x18) and +0x2a; then, with type 0 or 1, that pet's next experience and +0xc..+0x12. With another type, only the
// access.
void UpdatePetStats(std::uint8_t pet, Kernel::WorldState& world, Crt::CrtRandom& random);
// The same on `pets` (WorldRuntime::pets or a copy) with the given own-record access count: the pets' tooltip
// (0x422a35) calls it with the record open around it (0x5f7bc0), or with an in-between draw's copies.
void UpdatePetStats(std::uint8_t pet, const Kernel::WorldState& world,
                    std::array<Kernel::PetStats, Kernel::PetStatsCount>& pets, std::uint8_t& ownRecordDepth,
                    Crt::CrtRandom& random);

// 0x417f30(level, experience), the raven's vtable + 8: two own-record accesses (the leadership and slot 1's durability,
// +0xbce) and what comes from them in the raven (DarkSpirit::word1e2 to word45a).
void ApplyRavenInfo(Kernel::DarkSpirit& raven, std::uint8_t level, std::uint32_t experience, Kernel::WorldState& world,
                    Crt::CrtRandom& random);

// 0x4e4ca1: 0x421db0(type, kind, slot, level, experience), then 0x550d10 with the own record open. With the pet window
// open (0x420cc0), the hero's raven and the table with the horse's durability; otherwise the table with the label
// item's (0x787a518) unless the trainer is open and the slot is under 12, and with kind 0, 0xfe or 0xff, the hero's
// raven (slot 1) or the horse (slot 8: its level, its +8 on the hero and its option, 0x4225d0). A type of 3 or more,
// which the original writes outside 0x723960, is dropped whole (Q-C1208); without writing the table, only the 0x4221d0
// access. It cuts with the original's address at the raven's joints with 0xfe and 0xff (0x418390 and 0x418400, CL11)
// and where 0x550d10 cuts.
[[nodiscard]] Core::Expected<void, std::string> ApplyPetInfo(const Protocol::Pets::PetInfoReply& reply,
                                                             const Scenes::WorldEntryResources& resources);
} // namespace Client::Pets
