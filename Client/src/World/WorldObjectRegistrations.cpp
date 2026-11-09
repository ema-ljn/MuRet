#include "World/WorldObjectRegistrations.h"

namespace Client::World
{
void RegisterWorldObject(WorldObjectRegistrations& registrations, WorldObjectIdentity object)
{
    for (WorldObjectRegistration& entry : registrations.entries)
    {
        if (entry.active != 0)
        {
            continue;
        }
        entry.active = 1;
        entry.object = object;
        return;
    }
}
} // namespace Client::World
