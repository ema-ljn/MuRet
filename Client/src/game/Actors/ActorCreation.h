#pragma once

#include "Core/Expected.h"
#include "Kernel/ActorSearch.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Models
{
struct ClothWorld;
}

namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
struct ActorState;
} // namespace Client::Kernel

// The creation and removal of a character of the world's pool by its key: 0x512870 and 0x510ff0. They are called by
// 0x12 and 0x14 (Actors/In/) and by the creation of monsters (0x5141e0). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". *Static*.
namespace Client::Actors
{
struct ActorConstructionContext;
struct ActorPlacement;

inline constexpr std::size_t CreatedActorSearch = Kernel::SearchedActors;

// 0x512870(key, type, X, Y, angle). First it looks for a live one with the key (MOVSX of +0x1f4 against the int): it
// builds it again (0x511290) without touching the key and sets +0x78 and +0x7c to 0. If there is none, the first one
// without the live byte: it frees the raven (0x421d60) and the layer (0x414340), builds it, sets its key (the low
// WORD) and both flags to 0. With all 400 alive it returns 400 without building or touching anything. 0x401040 takes
// the named bones out of the record (ForgetNamedBones); 0x4fb1a0 (the cloths) has nothing to free in MuRet's world:
// the cloths of the game's characters live in the scene (GameScene), which the handler does not see (08, P-C161). It
// returns the slot; an error of 0x511290 (another type) stops with what came before done.
[[nodiscard]] Core::Expected<std::size_t, std::string> CreateActor(Kernel::WorldState& world, std::int32_t key,
                                                                   std::uint16_t type, const ActorPlacement& placement,
                                                                   const ActorConstructionContext& context,
                                                                   Models::ClothWorld& cloth);

// 0x510ff0(key): the first live one with the key (MOVSX) is left dead: +0 at 0, 0x401040, turns off the helpers that
// carry it (0x511031..0x51104b, like 0x443d40), frees the raven (0x421d60), the cloths (0x4fb1a0, as above) and the
// layer (0x414340). With none, nothing.
void RemoveActor(Kernel::WorldState& world, std::int32_t key);

// 0x511070(c, o): removes the direct reference, without resolving the key or checking that it is still alive. It
// uses `slot` only to clear the world's indexed resources; it keeps the other fields so the body finishes the current
// step.
void RemoveActorAtSlot(Kernel::WorldState& world, Kernel::ActorState& actor, std::size_t slot);

// 0x401040 → 0x401410: takes the named bones of character `actor` out of [0x6e5640].
void ForgetNamedBones(Kernel::WorldRuntime& runtime, std::size_t actor);

// 0x401000 → 0x4011c0: bone `bone` of the model of `actor` with the name `name`; first takes out the one with the
// same character and name (0x4016c0 and 0x401350).
void NameABone(Kernel::WorldRuntime& runtime, std::size_t actor, std::string_view name, std::uint32_t bone);
} // namespace Client::Actors
