#pragma once

namespace Client::Scenes
{
struct WorldRuntime;

// 0x430250(map), once per frame on every map (0x637700): in Crywolf's map the track of the stage and, at the end
// stage, the result's sound (0x244, or 0x245 with the failure banner in 0x6c73dc); on any other map it stops the
// map's own track.
void StepTheCrywolfMusic(WorldRuntime& runtime);
} // namespace Client::Scenes
