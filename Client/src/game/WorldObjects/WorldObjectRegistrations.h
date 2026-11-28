#pragma once

#include "Kernel/WorldObject.h"

// 06 § Lo propio de Lorencia: 0x606630's auxiliary registry, separate from the objects and actors.
namespace Client::WorldObjects
{
// First active == 0: activates it and stores the argument; null and duplicates are valid. If full it changes nothing.
// It does not touch the objects or compute poses. The caller keeps the metadata and the entities' lifetime.
void RegisterWorldObject(Kernel::WorldObjectRegistrations& registrations, Kernel::WorldObjectIdentity object);
} // namespace Client::WorldObjects
