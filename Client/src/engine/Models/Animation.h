#pragma once

#include "Core/IClock.h"
#include "EngineApi.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"

#include <chrono>
#include <cstdint>
#include <optional>

// How far a model's animation advances (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto avanza la
// animación"): the body per logic step at its action's speed, and the pet and the wings by time, at the speed they
// have at 21.33 FPS (Q-C20 corrected).
namespace Client::Models
{
// Scene 4 decides "finished" with frame + 2 (Q-C21); the others, with the real frame.
enum class AnimationScene : std::uint8_t
{
    Other,
    CharacterSelect,
};

// 0x4ec200 with `speed` (the action's, or the caller's). With one key or none it does nothing. Otherwise it adds the
// speed; if the whole key changed, stores the old one and its action as previous; with the frame at 0 or less adds
// keys minus 1; past the last key, a repeating action wraps with the modulo (keys, or keys minus 1 with
// lock_positions) and any other stays at keys minus 0.01. Returns whether it finished: in the selection frame + 2
// decides, but the modulo uses the real frame.
ENGINE_API bool AdvanceAnimation(AnimationState& state, const ActionInfo& action, float speed, AnimationScene scene);

// 0x4ea030: nothing for an action `skeleton` lacks or for the same one; otherwise the current one and its key (the
// original stores the frame as is, and the pose truncates it) become previous and the frame goes to 0.
ENGINE_API void SetAction(AnimationState& state, const Skeleton& skeleton, std::uint8_t action);

// 1 / 21.33 FPS: the frame of CL3's faithful mode (64 frames every 3 s).
inline constexpr Core::Duration OriginalFrameTime = std::chrono::microseconds{46'875};

// How what the original runs once per drawn frame (the cloth, Q-C124, and the scepter glow) counts the original's
// frames: by the clock (Decoupled, Q-C124 and Q-C20 corrected), or one per draw (Faithful, like the original).
enum class OriginalFramePace : std::uint8_t
{
    ByTheClock,
    OncePerDraw,
};

// The most a draw runs at once: 3 s of the original.
inline constexpr int MaxOriginalFramesPerDraw = 64;

// The original frames that run in each MuRet draw. By the clock: the first draw runs one; then one per
// OriginalFrameTime elapsed since the previous draw, carrying the remainder; a clock that goes back adds nothing; a
// jump beyond MaxOriginalFramesPerDraw runs that many and drops the rest. One per draw in faithful mode: its frame
// lasts one of the original's, but not exactly 46.875 ms (steps and latency shift it), and by the clock the draw
// arriving before the first frame would run 0 and the next 2. It also does not catch up on what was not drawn, like the
// original.
class ENGINE_API OriginalFrameCarry
{
public:
    [[nodiscard]] int FramesAt(Core::Duration clock, OriginalFramePace pace);

private:
    std::optional<Core::Duration> m_lastClock;
    Core::Duration m_carry{};
};

// 0x50ba30: Helper02 (0x1c13, character + 0x318) advances by half a key per original frame.
inline constexpr float PetKeysPerOriginalFrame = 0.5f;

// The state, and the remainder that does not reach an original frame yet.
struct TimedAnimation
{
    AnimationState state;
    Core::Duration carry{};
};

// Q-C20 corrected: `keysPerOriginalFrame` every OriginalFrameTime of `elapsed`, with AdvanceAnimation's rule. A
// negative `elapsed` counts as 0.
ENGINE_API void AdvanceByTime(TimedAnimation& animation, const ActionInfo& action, float keysPerOriginalFrame,
                              Core::Duration elapsed);

// The copy the renderer poses between two steps: `alpha` of one more step, with the same rule, so the keys it crosses
// keep their previous one as in the step.
[[nodiscard]] ENGINE_API AnimationState Interpolated(const AnimationState& state, const ActionInfo& action, float speed,
                                                     float alpha);
} // namespace Client::Models
