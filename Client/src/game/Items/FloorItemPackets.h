#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// The floor item packets (CL9 milestone 5; 06 § Los ítems del piso): 0x20 (0x4d0ba0, with 0x608350, 0x607b60 and
// 0x607fb0), 0x21 (0x4e3849), 0x22 (0x4d0cc0) and the stat fruit, 0x2C (0x4de500). The 23 is in InventoryMoves
// (ApplyDropResult): it is the cursor item's.
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Tables
{
class TextTable;
}

namespace Client::Effects
{
class ModelEffects;
}

namespace Client::Kernel
{
class WorldState;
struct FloorItemState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Items
{

// 0x20: the 0x220-byte entry of `key & 0x7fff` (0 past 999) at 0x77f46c8.
inline constexpr std::uint16_t FloorItemIndexMask = 0x7fff;
inline constexpr std::int32_t FloorItemCount = 1000;
// 0x608369.
inline constexpr std::int16_t ZenItem = 0x1c0f;

// What 0x608350 reads from outside.
struct FloorItemCreation
{
    Crt::CrtRandom& random;
    // 0x5f93c0, the terrain height at (x, y): Terrain::TerrainHeightAt's with the map's.
    std::function<Core::Expected<double, std::string>(float x, float y)> height{};
    // The world clock (0x5767b88) for the pulsing light of some models in 0x607b60.
    Core::Duration clock{};
    // 0x41ed10 with the just dropped item (CL10): the last one stays in lastSoundId.
    std::optional<Audio::SoundId>* sound = nullptr;
    std::function<void(Audio::SoundId, const glm::vec3&)> soundRequest{};
    // 0x518cf0: the map's effect table, where the just dropped medal creates its two cracks. Without it, the medal
    // stops (the service is missing, not the input).
    Effects::ModelEffects* effects = nullptr;
};

// 0x608350(entry, item, position, justDropped): the type (0x54b3a0) in +4, the value, the durability and the options,
// the model (type + 0x212, with those that change by level), 0x607b60 with its rand()
// (CharacterDrawing::HangingRandomCount), the ±30 box, the position and the height: with `dropped`, 180 above the
// terrain and the speed 20 (the silver and gold medals, models 0x2242 and 0x2243, first create the cracks 0x100 and
// 0xfe with the medal as owner, with no rand(), and stay at 3 with the speed 50); otherwise, on the terrain. It ends
// with 0x607fb0.
[[nodiscard]] Core::Expected<void, std::string> CreateFloorItem(Kernel::FloorItemState& item,
                                                                const Protocol::Items::ItemData& bytes,
                                                                const glm::vec3& position, bool dropped,
                                                                const FloorItemCreation& creation);

// 0x4d0ba0: each entry at (x + 0.5, y + 0.5) · 100, in order; the 0x54b3a0 that follows leaves nothing.
[[nodiscard]] Core::Expected<void, std::string> ApplyFloorItemsCreate(const Protocol::Items::FloorItemsCreate& create,
                                                                      const Scenes::WorldEntryResources& resources);

// 0x4e3849: turns off (+0x4c at 0) each key's entry, with those of 1000 or more at 0.
void ApplyFloorItemsDelete(const Protocol::Items::FloorItemsDelete& deleted, Kernel::WorldState& world);

// 0x4d0cc0: with 0xfe, zen in the own record (3848 rand() when closing it with no accesses) and, with the picked one
// (0x77f27b0) alive, the notice "%d %s %s" with its value and texts 224 and 918; with a slot, 0x5bc740 (except 0xfd)
// and the notice "%s %s" with the picked one's name (0x5b1c70) and 918. Then the sound by the type in pkt[4..] (0x45
// or 0x1d, CL10) and, also with 0xff, the request (0x6c82e8) at −1.
[[nodiscard]] Core::Expected<void, std::string> ApplyPickUpResult(const Protocol::Items::PickUpResult& result,
                                                                  const Scenes::WorldEntryResources& resources);

// The short 22 with 0xfe (07 Q-C1240, the GameServer 0.97's GCMoneySend): of 0x4d0cc0, what does not read past
// pkt[7], in its order: zen with the own record open (0x4d0d9b), the notice with the picked one alive (0x4d0f5f) and
// the request at −1 (0x4d10b9). The sound (0x4d1051) does not play: its type falls in pkt[8..10], outside the packet.
[[nodiscard]] Core::Expected<void, std::string> ApplyShortZenPickUp(const Protocol::Items::ShortZenPickUp& result,
                                                                    const Scenes::WorldEntryResources& resources);

// A short 22 (07 Q-C1240): of 0x4d0cc0, only the request (0x6c82e8) at −1, which 0x4d10b9 writes on every path.
// Without it, RequestThePickUp (0x5667d7, 0x566e90) never requests again.
void ApplyShortPickUpResult(Kernel::WorldState& world);

// 0x4de500, the 2C: with 0 and a stat from 0 to 4, the own record's stat and +0x44 go up by the points; with 3, the
// stat goes down and +0x68 and +0x48 go up; each with its notice (0x632e30) built from texts 379, the stat's and 1412
// or 1903. Results 1, 2, 4, 5, 0x10, 0x21, 0x25 and 0x26 open their notice. Always, 0x5767954 at 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyStatFruit(const Protocol::Items::StatFruit& fruit,
                                                               Kernel::WorldState& world, Crt::CrtRandom& random,
                                                               const Tables::TextTable* texts);

// A short 2C (07 Q-C1240): of 0x4de500, only the use wait (0x5767954) at 0, which it writes on every path. Without
// it, the JG of 0x5c5920 and 0x5a2bea does not let another item be used.
void ApplyShortStatFruit(Kernel::WorldState& world);
} // namespace Client::Items
