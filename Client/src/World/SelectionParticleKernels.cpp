#include "World/SelectionParticleKernels.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/IParticleTrigonometry.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>

namespace Client::World
{
namespace
{
// 0x6b86c0 y 0x6b88d4 (0x3c23d70a), 0x6b87b8 y 0x6b8a34 (0x3dcccccd), 0x6b8a48, 0x6b8bf8, 0x6b882c.
constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double Twentieth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double FlameRiseBase = static_cast<double>(std::bit_cast<float>(0x3fe66666u));
constexpr double Half = 0.5;
// 0x5476f0..0x547771: 0x6ba394 (0,0002f), 0x6b86d0 (20), 0x6b8b90 (0,02f) y 0x6ba354 (−0,02f).
constexpr double MistPhase = static_cast<double>(std::bit_cast<float>(0x3951b717u));
constexpr float MistAmplitude = 20.0f;
constexpr double MistTurn = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
// 0x545e3f..0x545ea9: 0x6b8818 (2), 0x6ba3b8 y 0x6ba3b4.
constexpr double FlameTurn = 2.0;
constexpr double FlameShrink = static_cast<double>(std::bit_cast<float>(0x3c2c7692u));
constexpr double FlameFade = static_cast<double>(std::bit_cast<float>(0x3f7e3870u));
// 0x547880..0x5478cf: 0x6ba350 (1,16f) y 0x6ba34c.
constexpr double ChaseBrighten = static_cast<double>(std::bit_cast<float>(0x3f947ae1u));
constexpr double ChaseFade = static_cast<double>(std::bit_cast<float>(0x3f5cb08eu));
constexpr double ChaseLift = 250.0;
constexpr int MistLife = 30, MistSeenLife = 50, FlameLife = 33, ChaseFadeLife = 10;
// 0x6b8ae8 (0,15f): la subida de la llama 5de/0.
constexpr double FloorFlameRise = static_cast<double>(std::bit_cast<float>(0x3e19999au));
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

// 0x5435cf..0x54363b (el else de 0x622, subtipo 1), y la salida 0x5435bb que limpia la velocidad.
void CreateOwnedMist(Particle& p, float scaleArgument, Render::Models::CrtRandom& random)
{
    p.life = MistLife;
    p.verticalSpeed = static_cast<float>(random.Next() % 1000);
    constexpr int Spread = 500, Center = 250, RiseSpan = 20, RiseBase = 20;
    p.position.x = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.x);
    p.position.y = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.y);
    p.position.z = F32(static_cast<double>(random.Next() % RiseSpan + RiseBase) + p.position.z);
    const int phase = random.Next() % 100;
    p.start.z = p.position.z;
    p.start.y = F32(static_cast<double>(phase) * Hundredth);
    constexpr int ScaleSpan = 20, ScaleBase = 180;
    p.scale = F32(static_cast<double>(random.Next() % ScaleSpan + ScaleBase) * Hundredth);
    constexpr int TurnSpan = 30;
    p.rotationFactor = F32(static_cast<double>(random.Next() % TurnSpan) * Hundredth + scaleArgument);
    p.velocity = glm::vec3(0.0f);
}

// 0x54150e..0x5415fb: el x de partida en +0x6c antes del giro; el y sigue al corrimiento del x.
void CreateRisingFlame(Particle& p, Render::Models::CrtRandom& random)
{
    p.life = FlameLife;
    constexpr int ShrinkSpan = 20;
    p.scale = F32(static_cast<double>(p.scale) - static_cast<double>(random.Next() % ShrinkSpan) * Hundredth);
    const int turn = random.Next() % FullTurn;
    p.start.x = p.position.x;
    constexpr double HalfTurn = 180.0;
    p.rotation = F32(static_cast<double>(turn) - HalfTurn);
    const int side = random.Next() & 1;
    p.position.x = F32(static_cast<double>(side) * Half * p.scale + p.position.x);
    p.verticalSpeed = F32((static_cast<double>(random.Next() % 100) * Hundredth + FlameRiseBase) * p.scale);
    constexpr int Still = 3, StillSpan = 10, DriftSpan = 20;
    float drift = 0.0f;
    if (random.Next() % StillSpan >= Still)
        drift = F32((static_cast<double>(random.Next() % DriftSpan) * Tenth - 1.0) * p.scale);
    p.velocity = glm::vec3(0.0f, drift, 0.0f);
    p.position.y = F32((static_cast<double>(p.position.x) - p.start.x) + p.position.y);
}

// 0x54143c..0x54149d: la llama del chorro 0x5de del 4 sube recta: vida 20, la velocidad (0; 0; ((& 127) + 128)
// · 0,15f) y la escala ((& 63) + 64) por el argumento por 0,01f, redondeada una vez.
void CreateFloorFlame(Particle& p, float scaleArgument, Render::Models::CrtRandom& random)
{
    constexpr int FloorFlameLife = 20, RiseMask = 127, RiseBase = 128, SizeMask = 63, SizeBase = 64;
    p.life = FloorFlameLife;
    p.velocity.x = 0.0f;
    p.velocity.y = 0.0f;
    p.velocity.z = F32(static_cast<double>((random.Next() & RiseMask) + RiseBase) * FloorFlameRise);
    p.scale = F32((static_cast<double>((random.Next() & SizeMask) + SizeBase) * static_cast<double>(scaleArgument)) *
                  Hundredth);
}

// 0x543840..0x543931: el /1 se dibuja con Impack03 y sube; los dos sortean vida y lugar igual.
void CreateChaseLight(Particle& p, Render::Models::CrtRandom& random)
{
    constexpr int SizeSpan = 10, SizeBase = 20, WideSpan = 5;
    if (p.subtype == 1)
    {
        p.texture = ChaseLightImpactTexture;
        p.scale = F32(static_cast<double>(random.Next() % SizeSpan + SizeBase) * Hundredth);
        p.verticalSpeed = F32(static_cast<double>(random.Next() % SizeSpan + SizeBase) * Twentieth);
    }
    else
    {
        p.verticalSpeed = 0.0f;
        p.scale = F32(static_cast<double>(random.Next() % WideSpan + SizeBase) * Tenth);
    }
    constexpr int LifeSpan = 3, LifeBase = 20, Spread = 50, Center = 25, HeightSpan = 200, HeightCenter = 100;
    p.life = random.Next() % LifeSpan + LifeBase;
    p.position.x = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.x);
    p.position.y = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.y);
    p.position.z = F32((static_cast<double>(random.Next() % HeightSpan - HeightCenter) + p.position.z) + ChaseLift);
}

Core::Expected<void, ParticleError> StepOwnedMist(Particle& p, const float* worldClock,
                                                  IParticleTrigonometry* trigonometry,
                                                  const ParticleObjectOwners& owners)
{
    WorldObject* owner = nullptr;
    if (p.typedOwner.kind == ParticleOwnerKind::WorldObject && owners)
        owner = owners(p.typedOwner.identity);
    if (owner == nullptr)
        return Core::Unexpected(ParticleError::MissingOwner);
    if (worldClock == nullptr || trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    // 0x5476f0..0x547711: el dueño visto renueva la vida; sin vida, el dueño vuelve a emitir (+0x58 = 0).
    if (owner->seen)
        p.life = MistSeenLife;
    if (p.life <= 0)
    {
        owner->state.hiddenMesh = 0;
        p.alive = false;
    }
    const double argument = (static_cast<double>(*worldClock) + p.verticalSpeed) * MistPhase;
    const auto sine = trigonometry->SineProductPC53(argument, MistAmplitude);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    p.position.z = F32(sine.Value() + p.start.z);
    // 0x547731 (la tabla 0x54848c): el 1 y el 4 giran a favor, el 2 y el 5 en contra (0x6ba354) y el 0 y el 3
    // no tocan el giro, que el dibujo pone por la paridad del lugar.
    switch (p.subtype)
    {
    case 1:
    case 4:
        p.rotation = F32((static_cast<double>(*worldClock) * p.rotationFactor) * MistTurn + p.start.y);
        break;
    case 2:
    case 5:
        p.rotation = F32((static_cast<double>(*worldClock) * p.rotationFactor) * -MistTurn + p.start.y);
        break;
    default:
        break;
    }
    return {};
}

void StepRisingFlame(Particle& p)
{
    // 0x545e3f: fcomp de la partida con x; C0 (menor o sin orden) gira a favor.
    if (!(p.start.x >= p.position.x))
        p.rotation = F32(static_cast<double>(p.rotation) + FlameTurn);
    else
        p.rotation = F32(static_cast<double>(p.rotation) - FlameTurn);
    p.position.z = F32(static_cast<double>(p.position.z) + p.verticalSpeed);
    // 0x545e75..0x545e85: el store es float, la comparación con 0 es del valor de la pila.
    const double scale = static_cast<double>(p.scale) - static_cast<double>(p.verticalSpeed) * FlameShrink;
    p.scale = F32(scale);
    if (!(scale > 0.0))
        p.alive = false;
    ScaleLight(p, FlameFade);
}

void StepChaseLight(Particle& p)
{
    ScaleLight(p, p.life < ChaseFadeLife ? ChaseFade : ChaseBrighten);
    p.position.z = F32(static_cast<double>(p.verticalSpeed) + p.position.z);
}
} // namespace

bool IsSelectionParticle(std::int32_t type, std::int32_t subtype)
{
    // 0x54327b/0x54747e: del 0 al 5 (los de Icarus y el 1 de la selección) y el 7 (el 0xc de Chaos Castle)
    // comparten el alta (0x5435cf) y el paso; el 7 no gira (0x547731 lo manda al default).
    constexpr std::int32_t LastOwnedMist = 5;
    constexpr std::int32_t BrightOwnedMist = 7;
    return (type == CloudParticle && ((subtype >= 0 && subtype <= LastOwnedMist) || subtype == BrightOwnedMist)) ||
           (type == FlameParticle && (subtype == RisingFlameSubtype || subtype == FloorFlameSubtype)) ||
           (type == ChaseLightParticle && (subtype == 0 || subtype == 1));
}

void CreateSelectionParticle(Particle& particle, float scaleArgument, Render::Models::CrtRandom& random)
{
    if (particle.type == CloudParticle)
        CreateOwnedMist(particle, scaleArgument, random);
    else if (particle.type == FlameParticle && particle.subtype == FloorFlameSubtype)
        CreateFloorFlame(particle, scaleArgument, random);
    else if (particle.type == FlameParticle)
        CreateRisingFlame(particle, random);
    else
        CreateChaseLight(particle, random);
}

Core::Expected<void, ParticleError> StepSelectionParticle(Particle& particle, const float* worldClock,
                                                          IParticleTrigonometry* trigonometry,
                                                          const ParticleObjectOwners& owners)
{
    if (particle.type == CloudParticle)
        return StepOwnedMist(particle, worldClock, trigonometry, owners);
    // 0x545d03: el 5de/0 no tiene más paso que la vida y el movimiento comunes.
    if (particle.type == FlameParticle && particle.subtype == RisingFlameSubtype)
        StepRisingFlame(particle);
    else if (particle.type == ChaseLightParticle)
        StepChaseLight(particle);
    return {};
}
} // namespace Client::World
