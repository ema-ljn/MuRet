#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Creating a monster or an NPC on the pool: 0x5141e0(type, X, Y, key) with the steps of Monsters/MonsterKinds and its
// tail 0x5140c0. Contract: Client/docs/research/06-render-escenas-y-ui.md, "El alta de un monstruo". *Static*.
namespace Client::Models
{
struct Skeleton;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Monsters
{

// The text bytes from +0x1d9: the name (ten bytes), +0x1e3 and +0x1e4..+0x1f1 (ActorState::nameSpill).
inline constexpr std::size_t ActorTextSize = 25;
// The bytes MuRet keeps contiguous from +0x1d9: the text ones, the WORD at +0x1f2 and the key at +0x1f4. +0x1f6 is not
// kept.
inline constexpr std::size_t ActorTextReadable = 29;
// Up to the body parts at +0x218.
inline constexpr std::size_t ActorTextReach = 0x218 - 0x1d9;

// Writes `bytes` from +0x1d9 + `offset` (the original's unbounded strcpy or strcat, its 0 included, Q-C847), running
// past the 25 text bytes as the original does; what lands in +0x1f6, +0x1f7 and +0x1fa..+0x217, which ActorState lacks,
// is lost (P-C352). What would reach the parts at +0x218 cuts with `address` and writes nothing.
[[nodiscard]] Core::Expected<void, std::string> PutActorText(Kernel::ActorState& actor,
                                                             std::span<const std::byte> bytes, std::uint32_t address,
                                                             std::size_t offset = 0);

// The C string from +0x1d9, without the 0: up to the first 0 of the ActorTextReadable bytes MuRet keeps contiguous, or
// all of them if there is none (the original would go on into +0x1f6).
[[nodiscard]] std::string ActorText(const Kernel::ActorState& actor);

// The +0x1d9 name as an original string function reads it (strcat, wsprintfA): ActorText if it has its 0; without it,
// the original would go on into +0x1f6, which MuRet does not keep, and it cuts with `address` (Q-C847, P-C352).
[[nodiscard]] Core::Expected<std::string, std::string> ActorCString(const Kernel::ActorState& actor,
                                                                    std::uint32_t address);

// 0x5141e0 with its tail 0x5140c0: the event creator or the switch case (MonsterCreationOf with WorldRuntime::map) runs
// its steps in order, each rand() in its place among the creation's (0x511290 and its cells):
// - LoadMonster and LoadNpc load the model into Assets::AssetsState::fauna (0x612d90, 0x612270): without
//   Monster{n + 1}.bmd the original exits (0x612dd6..0x612de1, Q-C23) and MuRet cuts. LoadMonster also writes the
//   model's sound table into WorldRuntime::modelSounds (MonsterSounds.h).
// - Write, Name, Dress (0x512930), Box (0x5133b0), Ground, Lower and CopyHeight write the character; MeshFlag and
//   ModelByte go to Monsters::MonstersState::modelPatches (without the modules' states they stop), BoneName to
//   WorldRuntime::namedBones (0x401000), Sound to WorldRuntime::lastSoundId (CL10).
// - The siege-dependent writes use 0x4294b0. Joint618/2,3 use ActorState as owner in every native case; the other Joint
//   families and Arrival still cut with their address. Action (0x4ea030) uses the loaded model's actions.
// Then the tail (MonsterTailOf), which copies the type's name from WorldRuntime::npcNames (0x7681eb8) to +0x1d9 if
// there is one (PutActorText, Q-C847). Returns the pool slot: with all 400 alive, 400, which the original writes too.
[[nodiscard]] Core::Expected<std::size_t, std::string> CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y,
                                                                     std::int32_t key,
                                                                     const Scenes::WorldEntryResources& resources);
} // namespace Client::Monsters
