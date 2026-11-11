#include "World/TrialsParticleKernels.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/KalimaParticleKernels.h"
#include "World/Particles.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::World
{
namespace
{
constexpr std::int32_t TrialsLife = 60;
constexpr int FullTurn = 360;

float F32(double value)
{
    return static_cast<float>(value);
}

void ScaleLight(Particle& p, double factor)
{
    for (int axis = 0; axis < 3; ++axis)
        p.light[axis] = F32(static_cast<double>(p.light[axis]) * factor);
}

// 0x544319..0x5443a6: los cinco rand() en el orden del ASM; la escala multiplica la del argumento (+0x10,
// todavía la del prefijo) antes del 0,02f, y las sumas a la posición son de la x87 guardadas en float.
void CreateSplash(Particle& p, Render::Models::CrtRandom& random)
{
    constexpr double SplashScale = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90.
    constexpr int ScaleSpan = 10, ScaleBase = 15, XSpan = 40, ZSpan = 25;
    constexpr double XCenter = 20.0, ZCenter = 10.0; // 0x6b86d0 y 0x6b87a8.
    p.life = TrialsLife;
    p.velocity.z = -1.0f;
    const int size = random.Next() % ScaleSpan + ScaleBase;
    p.scale = F32((static_cast<double>(size) * static_cast<double>(p.scale)) * SplashScale);
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.position.x = F32((static_cast<double>(random.Next() % XSpan) - XCenter) + static_cast<double>(p.position.x));
    p.position.z = F32((static_cast<double>(random.Next() % ZSpan) - ZCenter) + static_cast<double>(p.position.z));
}

// 0x5423e0..0x542448: el and con la corrección del signo de rand() (que nunca es negativo) es el & simple.
void CreateSmoke(Particle& p, Render::Models::CrtRandom& random)
{
    constexpr double SmokeScale = static_cast<double>(std::bit_cast<float>(0x3ba3d70au)); // 0x6b89d8.
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));      // 0x6b87b8.
    constexpr int ScaleMask = 63, ScaleBase = 64, RiseMask = 31, RiseBase = 60;
    p.life = TrialsLife;
    const int size = (random.Next() & ScaleMask) + ScaleBase;
    p.scale = F32((static_cast<double>(size) * static_cast<double>(p.scale)) * SmokeScale);
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.verticalSpeed = F32(static_cast<double>((random.Next() & RiseMask) + RiseBase) * Tenth);
}

void StepSplash(Particle& p)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3ba3d70au)); // 0x6b89d8.
    constexpr double Fall = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));   // 0x6b8a48.
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f7851ecu));   // 0x6ba33c.
    p.scale = F32(static_cast<double>(p.scale) + Growth);
    p.velocity.z = F32(static_cast<double>(p.velocity.z) - Fall);
    ScaleLight(p, Fade);
}

Core::Expected<void, ParticleError> StepSmoke(Particle& p, const float* clock)
{
    constexpr double LightPerLife = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b89e4.
    constexpr double Red = static_cast<double>(std::bit_cast<float>(0x3f666666u));          // 0x6b8a1c.
    constexpr double Half = 0.5;                                                            // 0x6b882c.
    constexpr double Slow = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));         // 0x6b87b8.
    constexpr double Sway = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));         // 0x6b8a48.
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3d23d70au));       // 0x6b8aec.
    // 0x546706..0x546723: el producto queda en la x87 para el rojo y para la mitad del verde y del azul.
    const double light = static_cast<double>(p.life) * LightPerLife;
    p.light.x = F32(light * Red);
    p.light.y = F32(light * Half);
    p.light.z = p.light.y;
    // 0x546726..0x54672f: fst guarda la velocidad en float y la deja sin redondear en la pila.
    const double rise = static_cast<double>(p.verticalSpeed) - Slow;
    p.verticalSpeed = F32(rise);
    if (clock == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    // 0x546732..0x546745: FSIN del reloj, FMUL ST1 por la velocidad de la pila, por 0,05f y más la x.
    const std::optional<double> sway = Render::Models::Detail::SineDoubleProductPC53(static_cast<double>(*clock), rise);
    if (!sway)
        return Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
    p.position.x = F32((*sway * Sway) + static_cast<double>(p.position.x));
    p.position.z = F32(rise + static_cast<double>(p.position.z));
    p.scale = F32(static_cast<double>(p.scale) + Growth);
    return {};
}
} // namespace

bool IsTrialsParticle(std::int32_t type, std::int32_t subtype)
{
    const bool splash =
        (type == WaterfallSplashFirst || type == WaterfallSplashFirst + 1) && subtype == TrialsSplashSubtype;
    return splash || (type == BreathParticle && subtype == TrialsSmokeSubtype);
}

void CreateTrialsParticle(Particle& particle, Render::Models::CrtRandom& random)
{
    if (particle.type == BreathParticle)
        CreateSmoke(particle, random);
    else
        CreateSplash(particle, random);
}

Core::Expected<void, ParticleError> StepTrialsParticle(Particle& particle, const float* clock)
{
    if (particle.type == BreathParticle)
        return StepSmoke(particle, clock);
    StepSplash(particle);
    return {};
}
} // namespace Client::World
