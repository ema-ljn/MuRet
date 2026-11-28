#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 06 § Lo propio de Lorencia: 0x606630's auxiliary registry, separate from the objects and actors.
namespace Client::WorldObjects
{
// Modern non-owning identity: 0 represents the null argument, not a live object.
using WorldObjectIdentity = std::uint64_t;
inline constexpr std::size_t WorldObjectRegistrationCount = 200;

struct WorldObjectRegistration
{
    std::uint8_t active = 0;
    // Original +1..+7: the registry neither interprets nor reinitializes those bytes.
    std::array<std::uint8_t, 7> metadata{};
    WorldObjectIdentity object = 0;

    friend bool operator==(const WorldObjectRegistration&, const WorldObjectRegistration&) = default;
};

// The caller's durable state. The defaults pick an empty port state; they do not prove the native reset.
// `nextIdentity` is the next free ID in this world, also when restoring preexisting entries. The caller does not
// reuse IDs while an entry may refer to them, nor allows overflow. The entries store identities, not addresses:
// moving or reallocating objects does not invalidate them. Copying a WorldObject keeps a snapshot's identity; it does
// not create another world entity.
struct WorldObjectRegistrations
{
    std::array<WorldObjectRegistration, WorldObjectRegistrationCount> entries{};
    WorldObjectIdentity nextIdentity = 1;
};

// First active == 0: activates it and stores the argument; null and duplicates are valid. If full it changes nothing.
// It does not touch the objects or compute poses. The caller keeps the metadata and the entities' lifetime.
void RegisterWorldObject(WorldObjectRegistrations& registrations, WorldObjectIdentity object);
} // namespace Client::WorldObjects
