#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Protocol::Actors
{
struct Transformed;
struct TransformedRecord;
} // namespace Client::Protocol::Actors

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Actors
{
class IViewportHooks;
} // namespace Client::Actors

// The transformed players enter the viewport: the 0x45. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los
// otros jugadores en el viewport". All *static*, from the ASM.
namespace Client::Actors::In
{
// 0x4c92a0: with "webzen" in the name, the record does nothing. Otherwise, the published hero's class (0x4c9328), the
// creation of the transformed character or of the monster of type +4 (CreateMonster, 0x5141e0, without looking at
// whether it returns 0) and FinishTheTransformed. What other modules do, through `hooks`.
[[nodiscard]] Core::Expected<void, std::string> ApplyTransformed(const Protocol::Actors::Transformed& transformed,
                                                                 const Scenes::WorldEntryResources& resources,
                                                                 const IViewportHooks& hooks);

// 0x4c9356..0x4c9762, what 0x4c92a0 does with the character `index` that 0x5141e0 returned: with +0x376 at 7 the scale
// at 0.8; the published hero's class it read before the creation (`heroClass`, 0x4c9328), the PK, +0x88 and +0x3e3 at
// 1; the flags (0x4e6380 and 0x42ab40); the protected cell, +0x39a/+0x39b and the angle. Just arrived, the position,
// the alpha at 0, circle 0xec (0x4c965c) and ray 0x5d5/2 that follows it (0x4c9674), both with it as owner; otherwise,
// the path (0x4eac00) with the Y read before the X. Then the name, +0x1e3 at 0 and 0x512a30 on the published hero.
[[nodiscard]] Core::Expected<void, std::string>
FinishTheTransformed(std::size_t index, const Protocol::Actors::TransformedRecord& record, std::uint8_t heroClass,
                     const Scenes::WorldEntryResources& resources, const IViewportHooks& hooks);
} // namespace Client::Actors::In
