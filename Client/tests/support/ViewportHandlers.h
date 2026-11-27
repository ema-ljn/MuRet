#pragma once

#include "Actors/In/EquipmentShown.h"
#include "Actors/In/Transformed.h"
#include "Actors/In/ViewportLeave.h"
#include "Actors/In/ViewportPlayers.h"
#include "Core/Expected.h"
#include "Kernel/WorldState.h"
#include "Protocol/Actors/Viewport.h"
#include "Session/ViewportHooks.h"
#include "Session/WorldEntry.h"

#include <cstddef>
#include <cstdint>
#include <string>

// The viewport's handlers with the session's slots, as Session/Route.h calls them.
namespace Client::Tests
{
inline Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Actors::Players& players,
                                                              const Scenes::WorldEntryResources& resources)
{
    return Actors::In::ApplyViewportPlayers(players, resources, Session::ViewportHooks(resources));
}

inline void ApplyViewportLeave(const Protocol::Actors::Leave& leave, Kernel::WorldState& world)
{
    Actors::In::ApplyViewportLeave(leave, world, Session::ViewportLeaveHooks(world));
}

inline Core::Expected<void, std::string> ApplyTransformed(const Protocol::Actors::Transformed& transformed,
                                                          const Scenes::WorldEntryResources& resources)
{
    return Actors::In::ApplyTransformed(transformed, resources, Session::ViewportHooks(resources));
}

inline Core::Expected<void, std::string> FinishTheTransformed(std::size_t index,
                                                              const Protocol::Actors::TransformedRecord& record,
                                                              std::uint8_t heroClass,
                                                              const Scenes::WorldEntryResources& resources)
{
    return Actors::In::FinishTheTransformed(index, record, heroClass, resources, Session::ViewportHooks(resources));
}

inline Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Actors::EquipmentShown& shown,
                                                             const Scenes::WorldEntryResources& resources)
{
    return Actors::In::ApplyEquipmentShown(shown, resources.world, Session::ViewportHooks(resources));
}
} // namespace Client::Tests
