#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// El 0x618 (0x530860 y 0x535840): los rayos que vuelven al dueño y los que lo siguen.
namespace Client::Scenes::JointFamilies
{
namespace
{
using Crt::CrtRandom;
using Math::D;
using Math::F32;

constexpr std::int32_t TarkanSubtype = 4;
constexpr std::int32_t StaffSubtype = 0x11;
constexpr std::int32_t WideSubtype = 8;

// Lo que el 0x618 lee de su dueño (+0x54): el byte vivo (+0), la posición (+0x10) y los puntos de +0x40 y +0x4c. El
// dueño es un personaje (ActorState: +0x40 es effectOrigin y +0x4c bonePoint4c), el FaunaObject de la /4 (0x447374:
// +0x40 es previousPosition; su +0x4c no lo lee nadie) o el efecto con modelo (JointOwnerKind::ModelEffect: las
// flechas de la /5 y el 0x11b/1 de la /0xa y la /0xb, con point40 y point4c).
struct BeamOwner
{
    bool alive = false;
    glm::vec3 position{};
    glm::vec3 point40{};
    std::optional<glm::vec3> point4c{};
};

std::optional<BeamOwner> OwnerOf(const JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return std::nullopt;
    if (joint.ownerKind == Kernel::JointOwnerKind::ModelEffect)
    {
        const auto& effect = *static_cast<const World::ModelEffect*>(joint.ownerContext);
        return BeamOwner{effect.alive, effect.transform.position, effect.point40, effect.point4c};
    }
    if (joint.ownerKind == Kernel::JointOwnerKind::ByType && joint.subtype == TarkanSubtype)
    {
        const auto& object = *static_cast<const FaunaObject*>(joint.ownerContext);
        return BeamOwner{object.active != 0, object.position, object.previousPosition, std::nullopt};
    }
    if (joint.ownerKind != Kernel::JointOwnerKind::ByType && joint.ownerKind != Kernel::JointOwnerKind::Actor)
        return std::nullopt;
    const auto& actor = *static_cast<const Kernel::ActorState*>(joint.ownerContext);
    return BeamOwner{actor.alive, actor.position, actor.effectOrigin, actor.bonePoint4c};
}

// La /3, la /0xb y la /0xf siguen el +0x4c del dueño (0x5322b2, 0x538256); las demás de su grupo, el +0x40.
bool FollowsThe4c(std::int32_t subtype)
{
    return subtype == 3 || subtype == 0xb || subtype == 0xf;
}

// Los que vuelven al dueño (0x53214b en el alta, 0x538313 en el paso): la /0, /1, /6, /7, /9, /0xc, /0xd y /0x10.
// La /7 no tiene paso propio (0x53c5a0: va derecho a la cola).
bool Homes(std::int32_t subtype)
{
    return subtype == 0 || subtype == 1 || subtype == 6 || subtype == 7 || subtype == 9 || subtype == 0xc ||
           subtype == 0xd || subtype == 0x10;
}

// La /0xc, la /0xd y la /0x10: con un sentido al azar (+0x9ec), más rápidos y sin la cuenta de vueltas de la /0.
bool Swerves(std::int32_t subtype)
{
    return subtype == 0xc || subtype == 0xd || subtype == 0x10;
}

Core::Expected<glm::vec3, std::string> PointOf(const JointState& joint, const BeamOwner& owner, std::uint32_t address)
{
    if (!FollowsThe4c(joint.subtype))
        return owner.point40;
    if (!owner.point4c)
        return Core::Unexpected(
            std::format("{:#x}: el dueño del joint 0x618/{:#x} no tiene el punto +0x4c", address, joint.subtype));
    return *owner.point4c;
}

// 0x5322cc..0x5323ce: la luz de cada subtipo; la /7 y la /9 no la escriben aquí (la /9 ya tiene la suya).
void PaintTheBeam(JointState& joint)
{
    const auto bits = [](std::uint32_t red, std::uint32_t green, std::uint32_t blue)
    { return glm::vec3(std::bit_cast<float>(red), std::bit_cast<float>(green), std::bit_cast<float>(blue)); };
    switch (joint.subtype)
    {
    case 0:
    case 6:
        joint.light = bits(0x3ecccccdu, 0x3e99999au, 0x3e4ccccdu); // 0x5322df
        break;
    case 1:
        joint.light = bits(0x3dcccccdu, 0x3dcccccdu, 0x3f000000u); // 0x5322f8
        break;
    case 2:
    case 3:
    case 5:
        joint.light = bits(0x3f000000u, 0x3dcccccdu, 0x3f800000u); // 0x53230e
        break;
    case 4:
        joint.light = bits(0x3e99999au, 0x3e19999au, 0x3dcccccdu); // 0x532327
        break;
    case 8:
        joint.light = bits(0x3f800000u, 0, 0x3f000000u); // 0x532340
        break;
    case 0xa:
    case 0xb:
        joint.light = bits(0x3f800000u, 0x3e99999au, 0x3dcccccdu); // 0x532359
        break;
    case 0xc:
    case 0xd:
        joint.light = bits(0x3f333333u, 0x3e99999au, 0x3f800000u); // 0x532372
        break;
    case 0xe:
    case 0xf:
        joint.light = bits(0x3f800000u, 0x3dcccccdu, 0x3dcccccdu); // 0x53238b
        break;
    case 0x10:
        joint.light = bits(0x3ecccccdu, 0x3e4ccccdu, 0x3ecccccdu); // 0x5323a1
        break;
    case 0x11:
        joint.light = bits(0x3f4ccccdu, 0x3e4ccccdu, 0x3f800000u); // 0x5323ba
        break;
    default:
        break;
    }
}

// 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, 0) en la posición y con los ángulos del joint. Todas las del
// 0x618 (0x5d5/0, /1, /3, /4 y 0x5f2/0x1f) las acepta World::Particles; un error suyo corta con la dirección.
Core::Expected<void, std::string> Particle(const JointState& joint, const JointKernels::StepServices& services,
                                           std::uint32_t address, std::int32_t type, std::int32_t subtype,
                                           const glm::vec3& light, float scale)
{
    if (services.particles == nullptr || services.random == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan las partículas del joint {} (0x618/{:#x})", address,
                                            services.index, joint.subtype));
    const auto created = services.particles->CreateChecked(type, joint.position, joint.angles, light, subtype, scale,
                                                           services.worldClock, *services.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{:#x} del joint {} (0x618/{:#x}) no está "
                                            "portada (CL11)",
                                            address, type, subtype, services.index, joint.subtype));
    return {};
}

// 0x538352..0x538387 y 0x53840c..0x538441: rand() · el sentido, % 5 (el resto de C), en x y en y.
void Jitter(JointState& joint, CrtRandom& random)
{
    constexpr std::int32_t Spread = 5;
    const auto step = [&random, &joint]
    {
        const std::int32_t drawn = random.Next();
        return static_cast<double>(drawn * joint.field9ec % Spread);
    };
    joint.position.x = F32(step() + D(joint.position.x));
    joint.position.y = F32(step() + D(joint.position.y));
}

// 0x5383a6..0x5383e7: (rand() % 5 + 3) · el sentido, en x y en y.
void WideJitter(JointState& joint, CrtRandom& random)
{
    constexpr std::int32_t Spread = 5, Least = 3;
    const auto step = [&random, &joint]
    {
        const std::int32_t drawn = random.Next();
        return static_cast<double>((drawn % Spread + Least) * joint.field9ec);
    };
    joint.position.x = F32(step() + D(joint.position.x));
    joint.position.y = F32(step() + D(joint.position.y));
}

// 0x538313..0x538470: con la vida en 100 o más, gira y sube. La /0 suma 10 al rumbo y 6 a la z; la /0xc, 15 y 6 con
// dos rand() en x y en y; la /0x10 resta 20 · el sentido y suma 8, con dos rand(); la /0xd resta 20 y suma 6, con dos
// rand(); las demás (la /1, /6 y /9) restan 10 y suman 5.
Core::Expected<void, std::string> Spin(JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    if (Swerves(subtype) && services.random == nullptr)
        return Core::Unexpected(
            std::format("0x538352: falta el RNG del joint {} (0x618/{:#x})", services.index, subtype));
    double turn = -10.0; // 0x6b87a8
    double rise = 5.0;   // 0x6b87f8
    if (subtype == 0)
    {
        turn = 10.0;
        rise = 6.0; // 0x6b87f4
    }
    else if (subtype == 0xc)
    {
        turn = 15.0; // 0x6b87bc
        rise = 6.0;
    }
    else if (subtype == 0x10)
    {
        constexpr double Swing = 20.0; // 0x6b86d0
        turn = -(static_cast<double>(joint.field9ec) * Swing);
        rise = 8.0; // 0x6b86c4
    }
    else if (subtype == 0xd)
    {
        turn = -20.0;
        rise = 6.0;
    }
    joint.angles.z = F32(D(joint.angles.z) + turn);
    if (subtype == 0xc || subtype == 0xd)
        Jitter(joint, *services.random);
    else if (subtype == 0x10)
        WideJitter(joint, *services.random);
    joint.position.z = F32(D(joint.position.z) + rise);
    return {};
}

// 0x538475..0x5385cd: debajo de vida 100, la velocidad sube (de a 7 hasta 40 la /0xc, /0xd y /0x10; de a 5 hasta 30
// las demás) y 0x4e9810 los gira hacia el destino: el propio en la /6 y la /9, y en las demás el dueño (+0x10, la z +
// 120), que queda en +0x58. A 35 o menos (o NaN) muere y pide el 0x5d5/1 y el sonido 0x23; de 35 a 70, con el rumbo
// girado más de 20 y la velocidad en 20 o más, la velocidad − 10.
Core::Expected<void, std::string> Return(JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    const bool swerves = Swerves(subtype);
    const double faster = D(joint.speed) + (swerves ? 7.0 : 5.0); // 0x6b8878, 0x6b87f8
    const double top = swerves ? 40.0 : 30.0;                     // 0x6b8bf4, 0x6b8858
    joint.speed = F32(faster);
    // 0x538496 y 0x5384c1: FCOMP con sólo C0, así que NaN no llega al tope.
    if (faster >= top)
        joint.speed = F32(top);
    const float heading = joint.angles.z;
    double distance = 0;
    if (subtype == 6 || subtype == 9)
    {
        // 0x538525: una copia local del destino propio (+0x58).
        const glm::vec3 target = joint.targetPosition;
        distance = Math::MoveToward(joint.position, joint.angles, target, joint.speed);
    }
    else
    {
        const auto owner = OwnerOf(joint);
        if (!owner)
            return Core::Unexpected(
                std::format("0x538501: falta el personaje dueño del joint {} (0x618/{})", services.index, subtype));
        constexpr double Above = 120.0; // 0x6b86c8
        joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Above)};
        distance = Math::MoveToward(joint.position, joint.angles, joint.targetPosition, joint.speed);
    }
    constexpr double Hit = 35.0, Near = 70.0, Swerve = 20.0; // 0x6b8974, 0x6ba1b0, 0x6ba2f8
    constexpr double SlowSpeed = 20.0, Brake = 10.0;         // 0x6b86d0, 0x6b87a8
    constexpr Audio::SoundId HitSound{0x23};
    if (!(distance > Hit))
    {
        joint.alive = false;
        if (auto made = Particle(joint, services, 0x538577, World::LightningParticle, World::LightningHitSubtype,
                                 joint.light, 1.0f);
            !made.HasValue())
            return made;
        if (services.soundRequest != nullptr && *services.soundRequest)
            (*services.soundRequest)(HitSound);
        return {};
    }
    if (!(distance > Near) && std::abs(D(heading) - D(joint.angles.z)) > Swerve && D(joint.speed) >= SlowSpeed)
        joint.speed = F32(D(joint.speed) - Brake);
    return {};
}

// 0x5385d3..0x538704: v = ((rand() & 3) + 8) · 0,03f, la luz dinámica de radio 2 (v · 0,4f; v; v · 0,8f) y lo que
// deja cada uno: la /6 y la /9 el 0x5d5/3 (escala 0,05f), la /0xc y la /0xd los sprites 0x5ac y 0x5fd (el segundo
// girado rand() % 360), la /0x10 el 0x5f2/0x1f (escala 0,8f, luz (0,4; 0,8; 1)) y la /0 y la /1 el 0x5d5/0.
Core::Expected<void, std::string> Glow(JointState& joint, const JointKernels::StepServices& services)
{
    if (services.random == nullptr)
        return Core::Unexpected(
            std::format("0x5385d3: falta el RNG del joint {} (0x618/{})", services.index, joint.subtype));
    constexpr std::int32_t GlowMask = 3, GlowBase = 8;
    constexpr std::int32_t GlowRadius = 2;
    constexpr double GlowStep = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu)); // 0x6b8bd4
    constexpr double GlowRed = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4
    constexpr double GlowBlue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38
    const double glow = static_cast<double>((services.random->Next() & GlowMask) + GlowBase) * GlowStep;
    const glm::vec3 color{F32(glow * GlowRed), F32(glow), F32(glow * GlowBlue)};
    if (auto lit = World::AddDynamicLight(services.lighting, joint.position, color, GlowRadius, "0x53861c");
        !lit.HasValue())
        return lit;
    const std::int32_t subtype = joint.subtype;
    if (subtype == 6 || subtype == 9)
    {
        constexpr std::int32_t SparkSubtype = 3;
        constexpr float SparkScale = std::bit_cast<float>(0x3d4ccccdu); // 0x5386ea
        return Particle(joint, services, 0x5386ff, World::LightningParticle, SparkSubtype, joint.light, SparkScale);
    }
    if (subtype == 0xc || subtype == 0xd)
    {
        constexpr std::uint16_t Flash = 0x5ac, Ring = 0x5fd;
        using Math::FullTurn;
        if (services.sprites == nullptr)
            return Core::Unexpected(
                std::format("0x5386b4: faltan los sprites del joint {} (0x618/{:#x})", services.index, subtype));
        static_cast<void>(services.sprites->Create(Flash, joint.position, 1.0f, joint.light, 0.0f, 0));
        const auto turn = static_cast<float>(services.random->Next() % FullTurn);
        static_cast<void>(services.sprites->Create(Ring, joint.position, 1.0f, joint.light, turn, 0));
        return {};
    }
    if (subtype == 0x10)
    {
        constexpr std::int32_t SmokeSubtype = 0x1f;
        constexpr float SmokeScale = std::bit_cast<float>(0x3f4ccccdu); // 0x53864a
        const glm::vec3 smoke{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f4ccccdu), 1.0f};
        return Particle(joint, services, 0x538674, World::BreathParticle, SmokeSubtype, smoke, SmokeScale);
    }
    return Particle(joint, services, 0x538695, World::LightningParticle, World::LightningTrailSubtype, joint.light,
                    1.0f);
}

// 0x53820e..0x53830e: el byte vivo del dueño en 0 apaga el joint y sale del paso sin la cola ni la vida. Vivo, la
// posición sigue su +0x40 (la /2, /4, /5, /8, /0xa, /0xe y /0x11; la /8 además ensancha 10,1f) o su +0x4c (la /3,
// /0xb y /0xf). La /2 y la /3 dejan el 0x5d5/0 y la /0xe y la /0xf el 0x5d5/4, con la luz del seno del reloj.
Core::Expected<void, std::string> Follow(JointState& joint, const JointKernels::StepServices& services)
{
    const auto owner = OwnerOf(joint);
    if (!owner)
        return Core::Unexpected(
            std::format("0x53820e: falta el dueño del joint {} (0x618/{:#x})", services.index, joint.subtype));
    if (!owner->alive)
    {
        joint.alive = false;
        if (services.returned != nullptr)
            *services.returned = true;
        return {};
    }
    const auto point = PointOf(joint, *owner, 0x538256);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    joint.position = point.Value();
    if (joint.subtype == WideSubtype)
    {
        constexpr double Widen = static_cast<double>(std::bit_cast<float>(0x4121999au)); // 0x6ba2f0, 10,1f
        joint.width = F32(D(joint.width) + Widen);
    }
    const bool barrier = joint.subtype == 2 || joint.subtype == 3;
    const bool red = joint.subtype == 0xe || joint.subtype == 0xf;
    if (!barrier && !red)
        return {};
    // 0x538285 y 0x5382c9: sen(reloj · 0,002f) · 0,3f + 0,8f, en la x87.
    constexpr double Phase = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc
    constexpr float Amplitude = std::bit_cast<float>(0x3e99999au);                   // 0x6b89dc
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));  // 0x6b8a38
    constexpr double Tenth = Math::TenthAsDouble;                                    // 0x6b87b8
    const auto wave = Math::Detail::SineProductPC53(D(services.worldClock) * Phase, Amplitude);
    if (!wave)
        return Core::Unexpected(
            std::format("0x53829d: el reloj del joint 0x618/{:#x} excede el seno canónico", joint.subtype));
    const double glow = *wave + Base;
    if (barrier)
        return Particle(joint, services, 0x5386ff, World::LightningParticle, World::LightningTrailSubtype,
                        {F32(glow * 0.5), F32(glow * Tenth), F32(glow)}, 1.0f);
    constexpr std::int32_t RedSubtype = 4;
    return Particle(joint, services, 0x538309, World::LightningParticle, RedSubtype,
                    {F32(glow), F32(glow * Tenth), F32(glow * Tenth)}, 1.0f);
}
} // namespace

Core::Expected<void, std::string> CreateBeamJoint(JointState& joint, const Kernel::JointRequest& request,
                                                  CrtRandom& random)
{
    // 0x532125: el ancho; 0x534ebc elige el grupo.
    joint.width = request.scale;
    const std::int32_t subtype = request.subtype;
    constexpr std::int32_t MostPoints = 50; // 0x534cb6
    if (subtype < 0 || subtype > StaffSubtype)
    {
        // 0x532136 → 0x5322cc → 0x534cb6: sólo el tope de los puntos.
        if (joint.maximumCount > MostPoints)
            joint.maximumCount = MostPoints;
        return {};
    }
    if (subtype == StaffSubtype)
    {
        // 0x5321e0..0x53220b: velocidad 0, vida 12 y la posición en el +0x40 del dueño; no escribe +0x6c.
        constexpr std::int32_t StaffLife = 12;
        joint.speed = 0;
        joint.life = StaffLife;
        const auto owner = OwnerOf(joint);
        if (!owner)
            return Core::Unexpected(std::string("0x5321ea: falta el personaje dueño del joint 0x618/0x11"));
        joint.position = owner->point40;
    }
    else if (subtype == 9)
    {
        // 0x532210..0x532229: luz (0,2; 0,2; 0,2), vida 120, 8 puntos y velocidad 3.
        constexpr float Gray = std::bit_cast<float>(0x3e4ccccdu);
        joint.light = glm::vec3(Gray);
        joint.life = 120;
        joint.maximumCount = 8;
        joint.speed = 3;
    }
    else if (Homes(subtype))
    {
        // 0x53214b..0x5321db: velocidad 3, vida 120 y 8 puntos. La /0xc, /0xd y /0x10: el sentido (+0x9ec) es 1 con
        // rand() impar y −1 con par, velocidad 10 y los ángulos x e y en rand() % 45.
        joint.speed = 3;
        joint.life = 120;
        joint.maximumCount = 8;
        if (Swerves(subtype))
        {
            joint.field9ec = random.Next() % 2 != 0 ? 1 : -1;
            joint.speed = 10;
            constexpr std::int32_t Tilt = 45;
            joint.angles.x = static_cast<float>(random.Next() % Tilt);
            joint.angles.y = static_cast<float>(random.Next() % Tilt);
        }
    }
    else
    {
        // 0x532238..0x5322c9: velocidad 0, vida 999.999.999, los puntos (10 la /5, 5 la /0xa y /0xb, 15 la /0xe y
        // /0xf, 20 las demás) y la posición en el +0x40 o el +0x4c del dueño.
        constexpr std::int32_t FollowLife = 999999999;
        joint.speed = 0;
        joint.life = FollowLife;
        joint.maximumCount = subtype == 5                       ? 10
                             : subtype == 0xa || subtype == 0xb ? 5
                             : subtype == 0xe || subtype == 0xf ? 15
                                                                : 20;
        const auto owner = OwnerOf(joint);
        if (!owner)
            return Core::Unexpected(std::format("0x53229c: falta el dueño del joint 0x618/{:#x} (0x530860)", subtype));
        const auto point = PointOf(joint, *owner, 0x5322b5);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        joint.position = point.Value();
    }
    PaintTheBeam(joint);
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

Core::Expected<void, std::string> StepBeamJoint(JointState& joint, const JointKernels::StepServices& services)
{
    // 0x5381f3: la /7 (y lo que pasa de 0x11) va derecho a la cola.
    const std::int32_t subtype = joint.subtype;
    if (subtype < 0 || subtype > StaffSubtype || subtype == 7)
        return {};
    if (!Homes(subtype))
        return Follow(joint, services);
    constexpr std::int32_t SpinLife = 100; // 0x538313
    if (joint.life >= SpinLife)
    {
        if (auto spun = Spin(joint, services); !spun.HasValue())
            return spun;
    }
    else if (auto returned = Return(joint, services); !returned.HasValue())
        return returned;
    return Glow(joint, services);
}
} // namespace Client::Scenes::JointFamilies
