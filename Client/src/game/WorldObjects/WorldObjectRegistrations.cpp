#include "WorldObjects/WorldObjectRegistrations.h"
#include "Kernel/WorldObject.h"

namespace Client::WorldObjects
{
void RegisterWorldObject(Kernel::WorldObjectRegistrations& registrations, Kernel::WorldObjectIdentity object)
{
    for (Kernel::WorldObjectRegistration& entry : registrations.entries)
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
