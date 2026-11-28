#pragma once

namespace Client::Kernel
{
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Events::Crywolf
{

// 0x430250(map), once per frame on every map (0x637700): in Crywolf's map the track of the stage and, at the end
// stage, the result's sound (0x244, or 0x245 with the failure banner in 0x6c73dc); on any other map it stops the
// track of Crywolf's map.
void StepTheCrywolfMusic(Kernel::WorldRuntime& runtime);
} // namespace Client::Events::Crywolf
