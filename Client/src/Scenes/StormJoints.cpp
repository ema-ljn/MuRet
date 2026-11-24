#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Scenes/ActorState.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "Scenes/SkinShells.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <functional>
#include <string>

namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;

// 0x53c0a5..0x53c390 con la /0..3 y la /5, después del movimiento común. La /2 gira su z rand() % 10 + 10 (en la x87,
// rand + z + 10). La velocidad sube 10, la cola con la matriz de los ángulos (0x535520, si está prendida), sube otros
// 10 y avanza (0, −velocidad, 0) girado por +0x3c. Debajo de la altura del terreno menos +0x9ec (150, o 300 la /2 y la
// /3), la z queda ahí y, con la cola y la clave distinta de 1: la /0 y la /1 con dueño posan el 0x20b y sueltan las
// cáscaras 0x137 de sus vértices (ShedTheSkinShells); la /2, la /3 y la /5, la onda 0x6eb/4 (0x53c295: 0x518cf0(0x6eb,
// posición, ángulos, luz, 4, 0, −1, 0, 0, 0, 0), con su rand() si hay lugar) con la luz (1; 0,8f; 0,3f) la /5 y (0,3f;
// 0,8f; 1) las otras; después la /5 pone la clave en 1 y la velocidad en −20, las otras apagan la cola, y la luz por
// 0,8333333f. Siempre, el sprite con el giro rand() % 360: el 0x60f de escala 1 con la luz la /0 y la /1, de 2,5 la /5
// y el 0x5fe de 1,6f con (0; 1; 1) la /2 y la /3.
Core::Expected<void, std::string> StepFallingJoint(JointState& joint, const JointKernels::StepServices& services)
{
    if (services.random == nullptr || services.sprites == nullptr || services.terrainHeight == nullptr ||
        !*services.terrainHeight)
        return Core::Unexpected(std::format(
            "0x53c0c9: faltan el RNG, los sprites o la altura del terreno del joint 0x632/{}", joint.subtype));
    constexpr std::int32_t TurningSubtype = 2, FirstGlowSubtype = 2;
    constexpr double Boost = 10.0; // 0x6b87a8
    if (joint.subtype == TurningSubtype)
    {
        constexpr int TurnSpan = 10;
        const int turn = services.random->Next() % TurnSpan;
        joint.angles.z = F32((static_cast<double>(turn) + D(joint.angles.z)) + Boost);
    }
    joint.speed = F32(D(joint.speed) + Boost);
    if (joint.tailEnabled)
        if (auto appended = JointKernels::AppendJointTail(joint, Render::Models::OriginalAngleMatrix(joint.angles));
            !appended.HasValue())
            return appended;
    joint.speed = F32(D(joint.speed) + Boost);
    const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0},
                                                            Render::Models::OriginalAngleMatrix(joint.bendAngles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    // 0x53c16d..0x53c18a: fisub de +0x9ec y fcom con la z (test ah,0x41): sólo con el suelo estrictamente arriba.
    const double floor =
        (*services.terrainHeight)(joint.position.x, joint.position.y) - static_cast<double>(joint.field9ec);
    if (floor > D(joint.position.z))
    {
        joint.position.z = F32(floor);
        constexpr std::int16_t Landed = 1;
        if (joint.tailEnabled && joint.key != Landed)
        {
            if (joint.subtype < FirstGlowSubtype)
            {
                // 0x53c1bb..0x53c246: con dueño (el personaje blanco de la 0x44 o la 0x47), el 0x20b posado con sus
                // ángulos y las cáscaras 0x137 de sus vértices (Scenes/SkinShells.h); sin dueño, nada.
                if (joint.ownerContext != nullptr)
                {
                    if (joint.ownerKind != JointOwnerKind::Actor && joint.ownerKind != JointOwnerKind::ByType)
                        return Core::Unexpected(
                            std::format("0x53c1bb: el dueño del joint 0x632/{} no es un personaje", joint.subtype));
                    if (services.models == nullptr || services.world == nullptr || services.effects == nullptr)
                        return Core::Unexpected(std::format("0x53c1c6: el joint 0x632/{} en el suelo pide los modelos, "
                                                            "los modelos compartidos y la tabla de efectos",
                                                            joint.subtype));
                    auto& owner = *static_cast<ActorState*>(joint.ownerContext);
                    if (auto shed = ShedTheSkinShells(owner, joint.subtype, *services.models, *services.world,
                                                      *services.effects, *services.random);
                        !shed.HasValue())
                        return shed;
                }
            }
            else
            {
                if (services.effects == nullptr)
                    return Core::Unexpected(std::format(
                        "0x53c295: el joint 0x632/{} pide la tabla de efectos (0x518cf0) para la onda 0x6eb/4",
                        joint.subtype));
                constexpr std::int32_t WaveSubtype = 4;
                const glm::vec3 glow =
                    joint.subtype == FallingStarSubtype
                        ? glm::vec3{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3e99999au)}
                        : glm::vec3{std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3f4ccccdu), 1.0f};
                static_cast<void>(services.effects->CreateCastleShockWave({.type = World::ShockWaveEffect,
                                                                           .position = joint.position,
                                                                           .angles = joint.angles,
                                                                           .light = glow,
                                                                           .subtype = WaveSubtype},
                                                                          *services.random));
            }
        }
        if (joint.subtype == FallingStarSubtype)
        {
            constexpr float Bounce = std::bit_cast<float>(0xc1a00000u); // −20
            joint.key = Landed;
            joint.speed = Bounce;
        }
        else
            joint.tailEnabled = false;                                                  // 0x53c2b8
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98, 0,8333333f
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    // 0x53c2e7..0x53c38b: 0x5491e0(textura, posición, escala, luz, 0, giro, 0); MuRet no guarda el dueño del sprite.
    constexpr int FullTurn = 360;
    const auto rotation = static_cast<float>(services.random->Next() % FullTurn);
    if (joint.subtype < FirstGlowSubtype || joint.subtype == FallingStarSubtype)
    {
        constexpr std::uint16_t StarTexture = 0x60f;
        const float scale = joint.subtype == FallingStarSubtype ? 2.5f : 1.0f; // 0x40200000 y 0x3f800000
        static_cast<void>(services.sprites->Create(StarTexture, joint.position, scale, joint.light, rotation, 0));
        return {};
    }
    constexpr std::uint16_t ShinyTexture = 0x5fe;
    constexpr float ShinyScale = std::bit_cast<float>(0x3fcccccdu); // 1,6f
    const glm::vec3 cyan{0.0f, 1.0f, 1.0f};
    static_cast<void>(services.sprites->Create(ShinyTexture, joint.position, ShinyScale, cyan, rotation, 0));
    return {};
}

// 0x53be90..0x53bebd y 0x53c076..0x53c0a0: la /4 sigue a su dueño (el efecto 0x13a, 0x51e01a): sin él se apaga; con
// él, toma su byte vivo y su posición, y con menos de 15 de vida la luz por 0,7692308f.
Core::Expected<void, std::string> FollowTheBlast(JointState& joint)
{
    if (joint.ownerContext == nullptr)
    {
        joint.alive = false; // 0x53be97
        return {};
    }
    if (joint.ownerKind != JointOwnerKind::ModelEffect)
        return Core::Unexpected(std::string("0x53be9f: el dueño del joint 0x632/4 no es un efecto con modelo"));
    const auto& owner = *static_cast<const World::ModelEffect*>(joint.ownerContext);
    joint.alive = owner.alive;
    joint.position = owner.transform.position;
    constexpr std::int32_t FadingLife = 15;
    if (joint.life < FadingLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateStormJoint(JointState& joint, const World::FishEffectRequest& request,
                                                   Crt::CrtRandom& random)
{
    if (request.subtype == StormSubtype)
    {
        // 0x534b27..0x534b2e: sin cola y con vida 0; sus tres /6 los crea FaunaJointCreation.cpp (0x534baa).
        joint.tailEnabled = false;
        joint.life = 0;
        return {};
    }
    if (request.subtype > StormSubtype)
    {
        // 0x534b1e → 0x534cb6: después del primer tramo, sólo el tope de 50 puntos.
        constexpr std::int32_t MostPoints = 50;
        if (joint.maximumCount > MostPoints)
            joint.maximumCount = MostPoints;
        return {};
    }
    if (request.subtype <= LastFallingSubtype || request.subtype == FallingStarSubtype)
    {
        // 0x534bcb..0x534c0c: 10 puntos, vida 40, velocidad 70, el ancho, +0x9f1 en rand() & 1, la clave en 0 y +0x9ec
        // en 150 (lo que baja la altura del terreno en el paso). La /2 y la /3 (0x534c1e..0x534c54): velocidad 30,
        // +0x9ec en 300 y la textura 0x62c y 0x62b. 0x534c61..0x534c8e: la /5 con la textura 0x60f, 15 puntos, la
        // velocidad rand() % 20 + 10 y +0x9f1 en 2; las demás (0x534c97), los ángulos en (90, 0, 0). 0x534ca4: +0x3c,
        // los ángulos. Un rand() (dos la /5).
        constexpr std::int32_t Life = 0x28, Points = 0xf, Below = 0x96, DeepBelow = 0x12c;
        constexpr float Speed = 70, SlowSpeed = 30; // 0x428c0000 y 0x41f00000
        constexpr std::uint16_t StarTexture = 0x60f, SpiralTexture = 0x62c, FlareTexture = 0x62b;
        constexpr int SpeedSpan = 0x14;
        constexpr double SpeedBase = 10.0; // 0x6b87a8
        constexpr std::uint8_t StarOrientation = 2;
        constexpr float Upright = 90.0f; // 0x42b40000
        joint.maximumCount = 10;
        joint.life = Life;
        joint.speed = Speed;
        joint.width = request.scale;
        joint.orientation = static_cast<std::uint8_t>(random.Next() & 1);
        joint.key = 0;
        joint.field9ec = Below;
        if (request.subtype == 2 || request.subtype == 3)
        {
            joint.speed = SlowSpeed;
            joint.field9ec = DeepBelow;
            joint.texture = request.subtype == 2 ? SpiralTexture : FlareTexture;
        }
        if (request.subtype == FallingStarSubtype)
        {
            joint.texture = StarTexture;
            joint.maximumCount = Points;
            joint.speed = F32(static_cast<double>(random.Next() % SpeedSpan) + SpeedBase);
            joint.orientation = StarOrientation;
        }
        else
            joint.angles = {Upright, 0.0f, 0.0f};
        joint.bendAngles = joint.angles;
        return {};
    }
    if (request.subtype == BlastSubtype)
    {
        // 0x534977..0x534a08: el ancho, velocidad 0, el pase 1 (+0x64), la textura 0x62b, la luz en 1, la velocidad
        // (+0x9dc..+0x9e4) en 0, 20 puntos, vida 30 y la tanda (+0x9eb) en 0; con dueño (el efecto 0x13a), su posición
        // y sus ángulos (0x5343f8). Sin dueño, el original apaga el joint y desreferencia el puntero nulo (0x5349d3):
        // MuRet corta. Sin rand().
        constexpr std::uint16_t FlareTexture = 0x62b;
        constexpr std::int32_t Points = 0x14, Life = 0x1e;
        joint.width = request.scale;
        joint.speed = 0;
        joint.category = 1;
        joint.texture = FlareTexture;
        joint.light = glm::vec3(1.0f);
        joint.velocity = glm::vec3(0.0f);
        joint.maximumCount = Points;
        joint.life = Life;
        joint.volley = 0;
        if (joint.ownerContext == nullptr || joint.ownerKind != JointOwnerKind::ModelEffect)
        {
            joint.alive = false;
            return Core::Unexpected(std::string("0x5349d3: el joint 0x632/4 sin un efecto con modelo de dueño"));
        }
        const auto& owner = *static_cast<const World::ModelEffect*>(joint.ownerContext);
        joint.position = owner.transform.position;
        joint.angles = owner.transform.angles;
        return {};
    }
    if (request.subtype != BoltSubtype)
        return Core::Unexpected(std::format("0x534959: el subtipo {} del joint 0x632 no llega acá", request.subtype));
    // 0x534a16..0x534b19.
    constexpr std::int32_t Life = 0x19;
    constexpr float Radian = std::bit_cast<float>(0x3c8efa35u); // 0x6b8cf8
    constexpr float Pale = std::bit_cast<float>(0x3f4ccccdu);   // 0,8f
    joint.life = Life;
    joint.width = request.scale;
    joint.maximumCount = 0x1e;
    joint.volley = static_cast<std::uint8_t>(Life);
    joint.speed = F32(static_cast<double>(random.Next() % 3 + 0x46) * D(Radian));
    joint.light = {Pale, Pale, 1};
    joint.velocity.x = 0;
    joint.velocity.y = F32(-(static_cast<double>(random.Next() & 3) + 40));
    const auto swing = Render::Models::Detail::SineProductPC53(D(joint.speed), 10.0f);
    if (!swing)
        return Core::Unexpected(std::string("0x534aa4: el seno del joint 0x632/6 excede el perfil PC53"));
    joint.velocity.z = F32(*swing);
    joint.position.y = F32(D(joint.position.y) - 20);
    joint.position.z = F32(D(joint.position.z) + 130);
    joint.pathIncrement = joint.position;
    joint.angles = request.angles;
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    joint.tailEnabled = false;
    joint.orientation = 3;
    if (auto appended = JointKernels::AppendCrossedJointTail(joint, matrix, true); !appended.HasValue())
        return appended;
    joint.count = 0; // 0x534b12: después de 0x535020.
    return {};
}

namespace
{
// 0x53bfa9..0x53c04f: el 0x632/6 en el suelo. Una copia de la posición; el joint 0x614/4 desde la posición hasta la
// posición con sus ángulos, sin dueño, de escala 60 (0x42700000), clave −1 (0x530860, 0x53bfd3); con la z de la
// velocidad (+0x9e4) en 0 o menos (fcomp, C0 o C3), la z en FSIN(velocidad) · 20 (0x52e3a0, retenido hasta el fmul);
// la z de la posición en el suelo + 5 (fst) y la de la copia en ese valor sin redondear + 30; la partícula 0x6ac/2 de
// escala 3 en la copia, con los ángulos y la luz del joint (0x53c04a).
Core::Expected<void, std::string> HitTheGround(JointState& joint, const JointKernels::StepServices& services,
                                               float ground)
{
    glm::vec3 from = joint.position;
    constexpr std::int32_t GroundBoltJoint = 0x614, GroundBoltSubtype = 4;
    constexpr float GroundBoltScale = 60.0f; // 0x42700000
    const World::FishEffectRequest bolt{.type = GroundBoltJoint,
                                        .firstPosition = joint.position,
                                        .secondPosition = joint.position,
                                        .angles = joint.angles,
                                        .subtype = GroundBoltSubtype,
                                        .ownerContext = nullptr,
                                        .scale = GroundBoltScale,
                                        .key = -1,
                                        .skill = 0,
                                        .skillSerial = 0,
                                        .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, bolt, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x53bfd3: {}", created.Error()));
    if (!(joint.velocity.z > 0.0f))
    {
        constexpr float Bounce = 20.0f; // 0x6b86d0
        const auto sine = Render::Models::Detail::SineProductPC53(D(joint.speed), Bounce);
        if (!sine)
            return Core::Unexpected(std::string("0x53c00a: la velocidad del joint 0x632/6 excede el perfil del seno"));
        joint.velocity.z = F32(*sine);
    }
    constexpr double Lift = 5.0, Flame = 30.0; // 0x6b87f8 y 0x6b8858
    const double landed = D(ground) + Lift;
    joint.position.z = F32(landed);
    from.z = F32(landed + Flame);
    constexpr std::int32_t Flare = 0x6ac, FlareSubtype = 2;
    constexpr float FlareScale = 3.0f; // 0x40400000
    if (services.particles == nullptr)
        return Core::Unexpected(std::string("0x53c04a: faltan las partículas del joint 0x632/6"));
    const auto made = services.particles->CreateChecked(Flare, from, joint.angles, joint.light, FlareSubtype,
                                                        FlareScale, services.worldClock, *services.random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("0x53c04a: la partícula 0x6ac/2 del joint 0x632/6 falla con el error {}",
                                            static_cast<unsigned>(made.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepStormJoint(JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.subtype >= StormSubtype)
        return {}; // 0x53bec5: la /7 y las de más de 7 sólo pasan por la cola y la vida.
    if (joint.subtype <= LastFallingSubtype || joint.subtype == FallingStarSubtype)
        return StepFallingJoint(joint, services);
    if (joint.subtype == BlastSubtype)
        return FollowTheBlast(joint);
    if (joint.subtype != BoltSubtype)
        return Core::Unexpected(std::format("0x53be76: el subtipo {} del joint 0x632 no llega acá", joint.subtype));
    // 0x53becb..0x53bf55: la y de la velocidad tiembla rand() % 5 − 2, la z baja 10 y se mueve por los ángulos.
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x53becb: falta el RNG del paso del joint 0x632/6"));
    const int shake = services.random->Next() % 5 - 2;
    joint.velocity.y = F32(static_cast<double>(shake) + D(joint.velocity.y));
    joint.velocity.z = F32(D(joint.velocity.z) - 10);
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    const auto moved = Render::Models::OriginalVectorRotate(joint.velocity, matrix);
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    // 0x53bf3c: la tanda (+0x9eb, la vida del alta) menos la vida. Con menos de 10 (los diez primeros pasos), la
    // partícula 0x5ee/0 en +0x24, con la luz y escala 3, y el ancho 100; después, el ancho 40.
    if (static_cast<std::int32_t>(joint.volley) - joint.life < 10)
    {
        // 0x53bf57..0x53bf6e: 0x53fe70(0x5ee, +0x24, +0x30, +0x48, 0, 3, 0); su alta pide el sonido 0x40 (0x542aa7).
        constexpr float BlazeScale = 3.0f; // 0x53bf59
        constexpr std::int32_t BlazeSound = 0x40;
        if (services.particles == nullptr)
            return Core::Unexpected(std::string("0x53bf6e: faltan las partículas del joint 0x632/6"));
        const auto made =
            services.particles->CreateChecked(World::BlazeParticle, joint.pathIncrement, joint.angles, joint.light, 0,
                                              BlazeScale, services.worldClock, *services.random);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x53bf6e: la partícula 0x5ee del joint 0x632/6 falla con el error {}",
                                                static_cast<unsigned>(made.Error())));
        if (made.Value().has_value() && services.soundRequest != nullptr && *services.soundRequest)
            (*services.soundRequest)(BlazeSound);
        joint.width = 100;
    }
    else
        joint.width = 40;
    // 0x53bf86..0x53bfa3: la altura del terreno (0x5f93c0), guardada en float. Con la z por encima, sólo la cola
    // cruzada; en el suelo o debajo (o NaN), antes, HitTheGround.
    if (services.terrainHeight == nullptr || !*services.terrainHeight)
        return Core::Unexpected(std::string("0x53bf8d: falta la altura del terreno del joint 0x632/6"));
    const float ground = F32((*services.terrainHeight)(joint.position.x, joint.position.y));
    if (!(joint.position.z > ground))
        if (auto hit = HitTheGround(joint, services, ground); !hit.HasValue())
            return hit;
    // 0x53c052..0x53c0a0: 0x535020(joint, la matriz del paso, 1) y, con la vida por debajo de la mitad de la tanda,
    // la luz · 0,7692308f.
    if (auto appended = JointKernels::AppendCrossedJointTail(joint, matrix, true); !appended.HasValue())
        return appended;
    if (joint.life < static_cast<std::int32_t>(joint.volley >> 1))
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace Client::Scenes::JointFamilies
