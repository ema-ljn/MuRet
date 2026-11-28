#include "WorldObjects/WorldObjectRegistrations.h"

namespace Client::WorldObjects
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
} // namespace Client::WorldObjects
