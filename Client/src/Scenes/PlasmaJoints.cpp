#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// El 0x14f (alta 0x530ddd..0x530ed8, paso 0x535b37..0x535d4a): los cuatro rayos que la skill de 0x5025b8 lanza hacia
// su blanco (0x5026a4, con el personaje blanco de dueño y ancho 100). Como el 0x614 (LightningJoints.cpp), da una
// vuelta por punto: gira hacia el dueño 80 más arriba (0x4e9810, de a 50), se sacude, agrega un tramo y avanza, hasta
// llegar. Su llamador sigue cortado: el subtipo sale de 0x4f2230 (el +0x31b de Fenrir, que ActorState no tiene).
namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;

Core::Expected<void, std::string> Particle(const JointState& joint, std::int32_t type,
                                           const JointKernels::StepServices& services, std::string_view address)
{
    if (services.particles == nullptr)
        return Core::Unexpected(std::format("{}: faltan las partículas del joint 0x14f/{}", address, joint.subtype));
    const auto created = services.particles->CreateChecked(type, joint.position, joint.angles, joint.light, 0, 1.0f,
                                                           services.worldClock, *services.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: falló la partícula {:#x} del joint 0x14f/{} ({})", address, type,
                                            joint.subtype, static_cast<int>(created.Error())));
    return {};
}

// 0x535cdf..0x535d4a: al llegar, sólo con el ancho en 50 justo (el inmediato 0x42480000), la partícula 0x5ca; después,
// con rand() % 8 en 0, tres rand() sin usar y la 0x5f2/0.
Core::Expected<void, std::string> Arrive(const JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::uint32_t ArrivalWidthBits = 0x42480000u;
    if (std::bit_cast<std::uint32_t>(joint.width) != ArrivalWidthBits)
        return {};
    if (auto made = Particle(joint, World::ThunderParticle, services, "0x535d03"); !made.HasValue())
        return made;
    if (services.random->Next() % 8 != 0)
        return {};
    for (int unused = 0; unused < 3; ++unused)
        static_cast<void>(services.random->Next());
    return Particle(joint, World::BreathParticle, services, "0x535d45");
}

// 0x535c26..0x535c77: con el ancho de 50 o más, la luz dinámica de radio 2 con ((rand() & 3) + 4) · 0,04f · 0,4f en
// los tres canales.
Core::Expected<void, std::string> Glow(const JointState& joint, const JointKernels::StepServices& services)
{
    constexpr float BrightWidth = 50; // 0x6b86b0
    if (!(joint.width >= BrightWidth))
        return {};
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 0x6b8aec, 0,04f
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4, 0,4f
    const std::int32_t shade = (services.random->Next() & 3) + 4;
    const float value = F32(static_cast<double>(shade) * Step * Dim);
    return World::AddDynamicLight(services.lighting, joint.position, glm::vec3(value), 2, "0x535c77");
}
} // namespace

void CreatePlasmaJoint(JointState& joint, const World::FishEffectRequest& request)
{
    // 0x530ddd..0x530e0a: velocidad 50, la textura repetida (+0x9f0), el ancho, 50 puntos y vida 20. Por subtipo, la
    // textura y la luz: /0..2 la 0x614 con (0,7f; 1; 0,7f), (1; 0,6f; 0,6f) y (0,7f; 0,7f; 1); /3..5 la 0x632 con
    // (0,1f; 0,8f; 0,1f), (1; 0,3f; 0,2f) y (0,2f; 0,3f; 1). Los demás, con la textura y la luz del prefijo.
    constexpr float Speed = 50;
    constexpr std::int32_t Points = 50, Life = 20;
    joint.speed = Speed;
    joint.repeatedTexture = true;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.life = Life;
    constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu), Fifth = std::bit_cast<float>(0x3e4ccccdu);
    constexpr float Third = std::bit_cast<float>(0x3e99999au), Sixth = std::bit_cast<float>(0x3f19999au);
    constexpr float Seventh = std::bit_cast<float>(0x3f333333u), Eighth = std::bit_cast<float>(0x3f4ccccdu);
    constexpr std::uint16_t Lightning = 0x614, Storm = 0x632;
    switch (joint.subtype)
    {
    case 0:
        joint.texture = Lightning;
        joint.light = {Seventh, 1, Seventh};
        break;
    case 1:
        joint.texture = Lightning;
        joint.light = {1, Sixth, Sixth};
        break;
    case 2:
        joint.texture = Lightning;
        joint.light = {Seventh, Seventh, 1};
        break;
    case 3:
        joint.texture = Storm;
        joint.light = {Tenth, Eighth, Tenth};
        break;
    case 4:
        joint.texture = Storm;
        joint.light = {1, Third, Fifth};
        break;
    case 5:
        joint.texture = Storm;
        joint.light = {Fifth, Third, 1};
        break;
    default:
        break;
    }
}

Core::Expected<void, std::string> StepPlasmaJoint(JointState& joint, const JointKernels::StepServices& services)
{
    // 0x535b37..0x535cd4: después del movimiento común, una vuelta por punto.
    for (std::int32_t point = 0; point < joint.maximumCount; ++point)
    {
        // 0x535b4c..0x535b68: con dueño, el destino es su posición (+0x10) 80 más arriba.
        if (joint.ownerContext != nullptr)
        {
            const ActorState* owner = JointKernels::ActorOwner(joint);
            if (owner == nullptr)
                return Core::Unexpected(std::string("0x535b53: el dueño del joint 0x14f no es un personaje"));
            constexpr double Above = 80.0; // 0x6b9ffc
            joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Above)};
        }
        constexpr float Turn = 50; // 0x42480000
        const float distance =
            F32(Render::Models::MoveToward(joint.position, joint.angles, joint.targetPosition, Turn));
        if (services.random == nullptr)
            return Core::Unexpected(std::string("0x535b81: falta el RNG del joint 0x14f"));
        // 0x535b81..0x535bf4: vx y vz, ((rand() & 0x3ff) − 512) / ancho; el tramo con los ángulos más (vx, vy, vz), la
        // z con el cociente sin redondear.
        constexpr int Mask = 0x3ff, Center = 0x200;
        const std::int32_t pitch = (services.random->Next() & Mask) - Center;
        joint.velocity.x = F32(static_cast<double>(pitch) / D(joint.width));
        const std::int32_t yaw = (services.random->Next() & Mask) - Center;
        const double turn = static_cast<double>(yaw) / D(joint.width);
        joint.velocity.z = F32(turn);
        const glm::vec3 shaken{F32(D(joint.angles.x) + D(joint.velocity.x)),
                               F32(D(joint.angles.y) + D(joint.velocity.y)), F32(turn + D(joint.angles.z))};
        const auto matrix = Render::Models::OriginalAngleMatrix(shaken);
        if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        // 0x535c09..0x535c20: llega con la velocidad · 1,5 mayor que la distancia (fcomp, test ah,0x41).
        constexpr double Reach = 1.5; // 0x6b8b94
        if (D(joint.speed) * Reach > D(distance))
            return Arrive(joint, services);
        if (auto lit = Glow(joint, services); !lit.HasValue())
            return lit;
        // 0x535c7f..0x535cd1: el avance (0, −velocidad, 0) girado por la matriz del tramo.
        const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    }
    return {};
}
} // namespace Client::Scenes::JointFamilies
