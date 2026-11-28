#pragma once

#include "Core/IClock.h"
#include "Models/Animation.h"
#include "Math/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

// The glow of the login Dark Lord's scepter: case 0x620 (HDK_Mace) of 0x509340 (0x50a169..0x50a398), which asks for
// five sprites per frame after drawing the item. Contract: 06 § Los efectos del login. Checked against the real
// emulated code in 300 cases (tests/vectors/world55/scepter.txt).
namespace Client::Actors::Drawing
{

inline constexpr std::uint16_t HdkMaceModel = 0x620;
inline constexpr std::size_t ScepterSpriteCount = 5;

// The five, additive and with the light (1; 0.3; 0), at TransformPosition(the item's bone 0, offset) with the body's
// scale and origin: two Shiny02 (0x5fd) and a flare01 (0x5ac) on the trident, and two Shiny02 on the pommel, with the
// position and scale from six rand() of the frame. With the table full they are not created, but the rand() are taken.
void CreateScepterGlow(Particles::FrameSprites& sprites, const Math::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, Crt::CrtRandom& random);

// The six rand() of a frame, already with their modulo, in the order they come out: the trident's p and scale (% 60
// and % 10), the flare01's (% 60 and % 10) and the pommel's (% 40 and % 8).
struct ScepterGlowRandoms
{
    int tridentStep = 0;
    int tridentScale = 0;
    int flareStep = 0;
    int flareScale = 0;
    int pommelStep = 0;
    int pommelScale = 0;
};
[[nodiscard]] ScepterGlowRandoms DrawScepterGlowRandoms(Crt::CrtRandom& random);
// The five with the rand() already taken.
void CreateScepterGlow(Particles::FrameSprites& sprites, const Math::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, const ScepterGlowRandoms& randoms);

// A character's scepter glow at any FPS, corrected like the cloth (Q-C124) and the pet (Q-C20): 0x509340 rolls it on
// every drawn frame of the original, and MuRet takes the six rand() once per original frame
// (Models::OriginalFrameTime, 46.875 ms) elapsed since its previous one and, in one with none, repeats the last roll on
// the current bone. The first one rolls; a jump of more than MaxScepterFramesPerAdvance frames takes those and drops
// the rest. In the faithful mode (`pace` OncePerDraw), one per draw, like the original (Models::OriginalFrameCarry).
inline constexpr int MaxScepterFramesPerAdvance = Models::MaxOriginalFramesPerDraw;

class ScepterGlowCarry
{
public:
    void Create(Particles::FrameSprites& sprites, const Math::OriginalMatrix& bone, float bodyScale,
                const glm::vec3& bodyOrigin, Crt::CrtRandom& random, Core::Duration clock,
                Models::OriginalFramePace pace = Models::OriginalFramePace::ByTheClock);
    // Create counts from zero again, as on the first frame: the next one rolls one frame and does not catch up on the
    // time that passed without a draw. It is for a held frame (Q-C85), like the cloth.
    void RestartTheClock()
    {
        m_frames = {};
    }

private:
    Models::OriginalFrameCarry m_frames;
    ScepterGlowRandoms m_last{};
};
} // namespace Client::Actors::Drawing
