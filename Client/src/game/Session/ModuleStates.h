#pragma once

// What each module owns of the original's globals, one member per module, by value (ADR 0029). App::GameClient owns it
// beside Kernel::WorldState, and Session/Route.h hands a routed handler the member it names.
namespace Client::Session
{
struct ModuleStates
{
};
} // namespace Client::Session
