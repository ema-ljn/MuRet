#pragma once

#include "Joints/FaunaJointCreation.h"
#include "Kernel/WorldState.h"

#include <memory>

namespace Client::Tests
{
// What App::GameClient lends its world before the first frame: the joint creation (Kernel/JointCreation.h).
inline void LendTheWorldPorts(Kernel::WorldState& world)
{
    world.LendJointCreation(&Joints::CreateJoint);
}

inline std::unique_ptr<Kernel::WorldState> LentWorld()
{
    auto world = std::make_unique<Kernel::WorldState>();
    LendTheWorldPorts(*world);
    return world;
}
} // namespace Client::Tests
