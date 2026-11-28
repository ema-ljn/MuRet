#pragma once
#include "Core/Expected.h"
#include "Particles/Particles.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <system_error>
namespace Client::Particles
{
class IParticleDrawingServices;
enum class ParticleDrawingPhase
{
    Dimensions,
    MaterialFlag,
    Material,
    Type620850,
    Type620870,
    Branch,
    SubtractivePrefix,
    Helper6208f0,
    Helper6208b0,
    SubtractiveTail,
    BlendedPrefix,
    BlendedTail,
    Bind,
    Begin,
    ColorFlag,
    Color,
    TexCoord,
    Vertex,
    End,
    // glTexEnvi of 0x5489eb; it goes last so the phase numbers that already show in the errors do not move.
    TextureEnv
};
struct ParticleDrawingError
{
    ParticleError reason;
    ParticleDrawingPhase phase;
    std::error_code backendError;
};
struct ParticleDrawingFailure
{
    std::size_t slot;
    ParticleDrawingError error;
};
// 0x548550: same pool, physical index and prefix without rollback. Clock is the live G, not delta/seed. Verified
// destinations: Fire default except 5/7/8/11/12/13; Smoke with the closed guards and rereads; Cloud 8/10;
// Drop/Thunder/Spark/67/68, the 5fd hand glow and the 5ed explosion (with its 4 × 4 frames) direct without a subtype
// guard; 6ab direct and 6ac with 0x620c10 before the billboard, the /2 between GL_ADD and GL_MODULATE (0x5489eb).
// Other destinations fail in Branch AFTER the prefix. It is the drawing's only contract: Create/Step keep their own
// subset, which does not filter what is drawn. Pool, clock and services stay alive; no concurrent reload/release or
// deferred retention.

[[nodiscard]] Core::Expected<void, ParticleDrawingFailure>
DrawParticlesChecked(Particles& particles, std::uint8_t mode, std::reference_wrapper<const float> clock,
                     IParticleDrawingServices& services);
} // namespace Client::Particles
