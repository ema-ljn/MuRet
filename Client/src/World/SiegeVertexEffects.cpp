#include "World/SiegeVertexEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
constexpr std::int16_t FirstStoneFragment = 0x12d, SecondStoneFragment = 0x12e;
constexpr std::int16_t FirstGateFragment = 0x12f, LastGateFragment = 0x130;
constexpr std::int16_t FirstStatueFragment = 0x131, LastStatueFragment = 0x133;
// Skill\golem_stone (0x616894): las piedras del cuerpo que 0x4ee770 suelta al morir el 0x1a4 (0x441696).
constexpr std::int16_t GolemStone = 0x13c;
// Las piedritas 0x12d y 0x12e del /2 (las que 0x4ee770 suelta en lugar del 0x13c con el subtipo 1, 0x4eee35) y el
// 0x12e/3 (la que tira el 0x1a6 en su ataque, 0x442397).
constexpr std::int32_t ShowerSubtype = 2, ThrownSubtype = 3;
constexpr double Tenth = Math::TenthAsDouble;
constexpr int Degrees = 360;
constexpr int GatePushMask = 255, StonePushMask = 127, GatePushBase = 128, StatuePushBase = 64;
constexpr int ScaleMask = 1, ScaleBase = 7, StoneScaleSpan = 7, StoneScaleBase = 10;
constexpr int FallSpan = 5, GateFallBase = 6, OtherFallBase = 2, LifeMask = 15, LifeBase = 32;
constexpr double SpawnLift = 50.0;
constexpr double Friction = static_cast<double>(0.9f), Boost = static_cast<double>(1.1f);
constexpr double GateGravity = 6.0, StoneGravity = 3.0, StatueGravity = 4.0;
constexpr double StoneBounce = static_cast<double>(-0.2f), StatueBounce = -0.5;
constexpr double GroundTilt = 128.0, AirTilt = 16.0;
constexpr int GroundWear = 5, DustChance = 10;

using Math::F32;

void RotateThePush(ModelEffect& effect, float push, Crt::CrtRandom& random)
{
    effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
    effect.velocity =
        Math::OriginalVectorRotate({0.0f, push, 0.0f}, Math::OriginalAngleMatrix(effect.transform.angles));
}

void RandomizeTheAngles(ModelEffect& effect, Crt::CrtRandom& random)
{
    effect.transform.angles.x = static_cast<float>(random.Next() % Degrees);
    effect.transform.angles.y = static_cast<float>(random.Next() % Degrees);
    effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
}

// La familia de las piedras: la que 0x5291ce mueve con la caída de +0xe4 y 0x51d50d termina de dar de alta.
bool IsStoneFragment(std::int16_t type)
{
    return (type >= FirstStoneFragment && type <= LastGateFragment) || type == GolemStone;
}

bool IsShower(const ModelEffect& effect)
{
    return (effect.type == FirstStoneFragment || effect.type == SecondStoneFragment) && effect.subtype == ShowerSubtype;
}

bool IsThrownStone(const ModelEffect& effect)
{
    return effect.type == SecondStoneFragment && effect.subtype == ThrownSubtype;
}

void BeginTheGateFragment(ModelEffect& effect, Crt::CrtRandom& random)
{
    // 51d2a9..51d50d, lo de cada uno, un rand() por campo: el empuje (en y), la escala y la caída (+0xe4). La puerta
    // (0x12f, 0x130): ((r & 255) + 128) · 0,1f, ((r & 1) + 7) · 0,1f y r % 5 + 6. El 0x13c (0x51d32c): ((r & 127) +
    // 32) · 0,1f, ((r & 1) + 10) · 0,25 y r % 5 + 2. El 0x12d/2 y el 0x12e/2 (0x51d3b1): ((r & 127) + 32) · 0,1f,
    // ((r & 1) + 4) · 0,12f + 0,6f y r % 5 + 2. Los demás (0x51d4a5): ((r & 127) + 128) · 0,1f, (r % 7 + 10) · 0,1f y
    // r % 5 + 2. 51d50d..51d5b6, de todos: vida, yaw del empuje, tres ángulos, altura.
    constexpr int SmallPushBase = 32, GolemScaleBase = 10, ShowerScaleBase = 4;
    constexpr double GolemScaleStep = 0.25;                                                     // 0x6b89e0
    constexpr double ShowerScaleStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));  // 0x6ba1d8, 0,12f
    constexpr double ShowerScaleFloor = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0,6f
    const bool gate = effect.type >= FirstGateFragment && effect.type <= LastGateFragment;
    const bool golem = effect.type == GolemStone;
    const bool shower = IsShower(effect);
    const bool small = golem || shower;
    const float push = F32(static_cast<double>((random.Next() & (gate ? GatePushMask : StonePushMask)) +
                                               (small ? SmallPushBase : GatePushBase)) *
                           Tenth);
    if (golem)
        effect.transform.scale =
            F32(static_cast<double>((random.Next() & ScaleMask) + GolemScaleBase) * GolemScaleStep);
    else if (shower)
        effect.transform.scale = F32(
            static_cast<double>((random.Next() & ScaleMask) + ShowerScaleBase) * ShowerScaleStep + ShowerScaleFloor);
    else
        effect.transform.scale = F32(static_cast<double>(gate ? (random.Next() & ScaleMask) + ScaleBase
                                                              : random.Next() % StoneScaleSpan + StoneScaleBase) *
                                     Tenth);
    effect.turnDirection = static_cast<float>(random.Next() % FallSpan + (gate ? GateFallBase : OtherFallBase));
    effect.life = (random.Next() & LifeMask) + LifeBase;
    RotateThePush(effect, push, random);
    RandomizeTheAngles(effect, random);
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + SpawnLift);
}

// 0x51d438..0x51d4a4, el 0x12e/3: vida (r & 15) + 32, la escala (r % 3 + 2) · 0,12f + 1,2f, la velocidad z en
// −20 − r % 5 y +0xd8 en 1,8f. Sin la cola de los demás: ni los ángulos ni la altura.
void BeginTheThrownStone(ModelEffect& effect, Crt::CrtRandom& random)
{
    constexpr int ThrownScaleSpan = 3, ThrownScaleBase = 2, DropSpan = 5, DropBase = -20;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));  // 0x6ba1d8, 0,12f
    constexpr double ScaleFloor = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0, 1,2f
    constexpr float Spin = std::bit_cast<float>(0x3fe66666u);                             // 0x51d496, 1,8f
    effect.life = (random.Next() & LifeMask) + LifeBase;
    effect.transform.scale =
        F32(static_cast<double>(random.Next() % ThrownScaleSpan + ThrownScaleBase) * ScaleStep + ScaleFloor);
    effect.velocity.z = static_cast<float>(DropBase - random.Next() % DropSpan);
    effect.speed = Spin;
}

void BeginTheStatueFragment(ModelEffect& effect, Crt::CrtRandom& random)
{
    // 51d5b7..51d744: el131/0 pasa al1 y consume un sorteo más para la gravedad.
    const float push = F32(static_cast<double>((random.Next() & StonePushMask) + StatuePushBase) * Tenth);
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + SpawnLift);
    effect.life = (random.Next() & LifeMask) + LifeBase;
    effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * Tenth);
    RotateThePush(effect, push, random);
    effect.turnDirection = static_cast<float>(random.Next() % FallSpan + OtherFallBase);
    if (effect.type == FirstStatueFragment && effect.subtype == 0)
    {
        effect.subtype = 1;
        effect.turnDirection = F32(static_cast<double>(random.Next() % FallSpan) + effect.turnDirection);
    }
    else if (effect.subtype == 2)
        effect.transform.scale = F32(static_cast<double>(effect.transform.scale) * static_cast<double>(0.6f));
    RandomizeTheAngles(effect, random);
}

void MoveTheFragment(ModelEffect& effect, bool stoneFamily)
{
    // 0x5291f8: la caída de 6 es solo de la puerta; el 0x13c cae de a 3 como las piedritas.
    const bool door = effect.type >= FirstGateFragment && effect.type <= LastGateFragment;
    for (int axis = 0; axis < 3; ++axis)
    {
        effect.transform.position[axis] =
            F32(static_cast<double>(effect.velocity[axis]) + effect.transform.position[axis]);
        effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Friction);
    }
    if (stoneFamily)
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - (door ? GateGravity : StoneGravity));
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + effect.turnDirection);
    if (!stoneFamily && effect.subtype == 1)
    {
        for (int axis = 0; axis < 3; ++axis)
            effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Boost);
    }
    if (!stoneFamily)
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - StatueGravity);
}

// Devuelve la altura del terreno como la dejó el original (la de las piedras, guardada en float en [ebp+8]).
Core::Expected<double, std::string> LandTheFragment(ModelEffect& effect, bool stoneFamily,
                                                    const ModelEffectStepServices& services)
{
    if (!services.terrainHeight)
        return Core::Unexpected(std::format("{}: falta la altura del terreno para el fragmento {:#x}",
                                            stoneFamily ? "0x5292a3" : "0x5294ab", effect.type));
    // 5292a8 redondea antes de comparar;5294b0 conserva la altura en la x87.
    const double sampled = services.terrainHeight(effect.transform.position.x, effect.transform.position.y);
    const double ground = stoneFamily ? static_cast<double>(F32(sampled)) : sampled;
    // TEST C0 solo en5292b6 también toma la rama de impacto con NaN;5294b8 mira C0|C3.
    const bool hit = stoneFamily ? !(effect.transform.position.z >= ground) : ground > effect.transform.position.z;
    if (hit)
    {
        effect.transform.position.z = F32(ground);
        effect.turnDirection =
            F32(static_cast<double>(effect.turnDirection) * (stoneFamily ? StoneBounce : StatueBounce));
        effect.life -= GroundWear;
    }
    effect.transform.angles.x = F32(static_cast<double>(effect.transform.angles.x) -
                                    static_cast<double>(effect.transform.scale) * (hit ? GroundTilt : AirTilt));
    return ground;
}

// 0x53fe70 con el dueño en 0: un tipo o subtipo que World::Particles no tiene corta con la dirección (con la tabla
// llena no crea nada ni saca rand() y el paso sigue).
Core::Expected<void, std::string> Emit(Particles& particles, std::int32_t type, const ModelEffect& effect,
                                       const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                                       Crt::CrtRandom& random, std::uint32_t address)
{
    const auto made = particles.CreateChecked(type, effect.transform.position, effect.transform.angles, light, subtype,
                                              scale, worldClock, random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} del fragmento {:#x} falla con el error {} "
                                            "(CL11)",
                                            address, type, subtype, effect.type, static_cast<unsigned>(made.Error())));
    return {};
}

// La luz local del paso ([ebp−0x1c], 0x520921): (1; 1; 1).
constexpr glm::vec3 StepLight{1.0f};

// 0x52930e..0x529385, el 0x13c después de caer: con rand() & 3 en 0, la piedra 0x6ec/5 (escala 2,8f) y el humo
// 0x5f2/0x15 (1,8f); con rand() % 10 en 0, el polvo 0x5f3/0 (1). Todos con la luz local.
Core::Expected<void, std::string> CrumbleTheGolemStone(const ModelEffect& effect, Particles& particles,
                                                       Crt::CrtRandom& random, float worldClock)
{
    constexpr std::int32_t StoneParticle = 0x6ec, StoneSubtype = 5, SmokeSubtype = 0x15;
    constexpr float StoneScale = std::bit_cast<float>(0x40333333u); // 0x529323, 2,8f
    constexpr float SmokeScale = std::bit_cast<float>(0x3fe66666u); // 0x52933c, 1,8f
    constexpr int StoneChance = 3;
    if ((random.Next() & StoneChance) == 0)
    {
        if (auto made = Emit(particles, StoneParticle, effect, StepLight, StoneSubtype, StoneScale, worldClock, random,
                             0x529335);
            !made.HasValue())
            return made;
        if (auto made = Emit(particles, BreathParticle, effect, StepLight, SmokeSubtype, SmokeScale, worldClock, random,
                             0x52934e);
            !made.HasValue())
            return made;
    }
    if (random.Next() % DustChance != 0)
        return {};
    return Emit(particles, DustParticle, effect, StepLight, 0, 1.0f, worldClock, random, 0x52937d);
}

// 0x52938a..0x5293ea, el 0x12d/2: con rand() % 10 en 0, la z en la altura del terreno de este paso y el humo
// 0x5f2/0xb (escala 2) con la luz local en (0,2f; 0,5f; 0,35f).
Core::Expected<void, std::string> SmokeTheShower(ModelEffect& effect, double ground, Particles& particles,
                                                 Crt::CrtRandom& random, float worldClock)
{
    constexpr glm::vec3 SmokeLight{std::bit_cast<float>(0x3e4ccccdu), 0.5f, std::bit_cast<float>(0x3eb33333u)};
    constexpr float SmokeScale = 2.0f; // 0x5293b8
    if (random.Next() % DustChance != 0)
        return {};
    effect.transform.position.z = F32(ground);
    return Emit(particles, BreathParticle, effect, SmokeLight, StoneDustSubtype, SmokeScale, worldClock, random,
                0x5293e2);
}

// 0x5290df..0x5291c9, el 0x12e/3: la velocidad z baja +0xd8, que sube 0,3f. Con el terreno (0x5f93c0, sin redondear)
// por encima de su z, la z en el terreno, la velocidad, los ángulos y +0x58 en 0 y, con rand() % 10 en 0, el humo
// 0x5f2/0x18 con la escala · 1,25; si no (también con NaN), con rand() & 1 en 0, el mismo humo con 0,2f. Los dos con su
// luz (+0xf4).
Core::Expected<void, std::string> StepTheThrownStone(ModelEffect& effect, Particles& particles, Crt::CrtRandom& random,
                                                     const ModelEffectStepServices& services, float worldClock)
{
    constexpr double SpinGain = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
    constexpr double LandedSmoke = 1.25;                                                // 0x6b8ad4
    constexpr float FallingSmoke = std::bit_cast<float>(0x3e4ccccdu);                   // 0x5291a9, 0,2f
    constexpr std::int32_t SmokeSubtype = 0x18;
    effect.velocity.z = F32(static_cast<double>(effect.velocity.z) - static_cast<double>(effect.speed));
    effect.speed = F32(static_cast<double>(effect.speed) + SpinGain);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x529117: falta la altura del terreno para el 0x12e/3"));
    const double ground = services.terrainHeight(effect.transform.position.x, effect.transform.position.y);
    if (ground > static_cast<double>(effect.transform.position.z))
    {
        effect.transform.position.z = F32(ground);
        effect.velocity = glm::vec3(0.0f);
        effect.transform.angles = glm::vec3(0.0f);
        effect.objectState.hiddenMesh = 0;
        if (random.Next() % DustChance != 0)
            return {};
        const float scale = F32(static_cast<double>(effect.transform.scale) * LandedSmoke);
        return Emit(particles, BreathParticle, effect, effect.light, SmokeSubtype, scale, worldClock, random, 0x529181);
    }
    if ((random.Next() & 1) != 0)
        return {};
    return Emit(particles, BreathParticle, effect, effect.light, SmokeSubtype, FallingSmoke, worldClock, random,
                0x5291c1);
}
} // namespace

bool IsSiegeVertexEffect(std::int16_t type)
{
    return (type >= FirstStoneFragment && type <= LastStatueFragment) || type == GolemStone;
}

std::optional<std::size_t> CreateSiegeVertexEffect(const ModelEffectArguments& arguments, ModelEffects& effects,
                                                   Crt::CrtRandom& random)
{
    if (!IsSiegeVertexEffect(arguments.type))
        return std::nullopt;
    const auto slot = effects.Begin(arguments);
    if (!slot)
        return slot;
    ModelEffect& effect = effects.Effects()[*slot];
    if (IsThrownStone(effect))
        BeginTheThrownStone(effect, random);
    else if (IsStoneFragment(effect.type))
        BeginTheGateFragment(effect, random);
    else
        BeginTheStatueFragment(effect, random);
    effect.previousPosition = effect.transform.position;
    return slot;
}

Core::Expected<void, std::string> StepSiegeVertexEffect(ModelEffect& effect, Particles& particles,
                                                        Crt::CrtRandom& random, const ModelEffectStepServices& services,
                                                        float worldClock)
{
    if (IsThrownStone(effect))
        return StepTheThrownStone(effect, particles, random, services, worldClock);
    const bool stoneFamily = IsStoneFragment(effect.type);
    MoveTheFragment(effect, stoneFamily);
    const auto landed = LandTheFragment(effect, stoneFamily, services);
    if (!landed.HasValue())
        return Core::Unexpected(landed.Error());
    if (stoneFamily)
    {
        effect.alpha = F32(static_cast<double>(effect.life) * Tenth);
        if (effect.type == GolemStone)
            return CrumbleTheGolemStone(effect, particles, random, worldClock);
        if (effect.type == FirstStoneFragment && effect.subtype == ShowerSubtype)
            return SmokeTheShower(effect, landed.Value(), particles, random, worldClock);
        return {};
    }
    if (random.Next() % DustChance != 0)
        return {};
    if (std::ranges::none_of(particles.All(), [](const Particle& particle) { return !particle.alive; }))
        return {};
    // 529516 pasa LEA[EBP-28], igual que5252e6: ningún camino lo inicializa (Q-C1176).
    return Core::Unexpected(std::string("0x529520: el polvo 0x5f3 del fragmento usa [EBP-0x28] sin inicializar "
                                        "en 0x5208a0 (Q-C1176; requiere evidencia de la VM)"));
}
} // namespace Client::World
