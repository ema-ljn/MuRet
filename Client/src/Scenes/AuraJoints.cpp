#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::AppendJointTail;
using JointKernels::D;
using JointKernels::F32;

// 0x530f0d..0x531214: lo que pone cada alta.
constexpr std::int32_t RingLife = 0xf423f;                     // 0x530f15
constexpr std::int32_t BindingLife = 0x2710;                   // 0x530f4c
constexpr std::int32_t BeamLife = 0x14;                        // 0x530fa6
constexpr std::int32_t RiseLife = 0x3c;                        // 0x53107b
constexpr std::int32_t SpinLife = 0x28;                        // 0x5311a4
constexpr std::int32_t RingPoints = 0x1e;                      // 0x530f1f, 0x530f56, 0x531088, 0x5311ae
constexpr std::int32_t BeamPoints = 5;                         // 0x530fb0
constexpr std::uint16_t RingTexture = 0x62b;                   // 0x530f26, 0x530f5d
constexpr std::uint16_t BeamTexture = 0x62c;                   // 0x530fb7
constexpr std::uint16_t RiseTexture = 0x610;                   // 0x5310df
constexpr std::uint16_t SpinTexture = 0x5ac;                   // 0x531189
constexpr float RiseSpeed = std::bit_cast<float>(0x44480000u); // 0x5310f5, 800
constexpr float SpinSpeed = std::bit_cast<float>(0xc2200000u); // 0x53119a, −40
constexpr float Tenth8 = std::bit_cast<float>(0x3f4ccccdu);    // 0,8f

// 0x535d4f..0x535db2: el 10f/4 pasa a /9 si el personaje de +0xa0c monta a Fenrir (0x1c37 en +0x318) fuera de la zona
// segura (+0x3e2), y el /9 vuelve a /4 si no. Con +0xa0c en −1, nada.
Core::Expected<void, std::string> SwapByFenrir(JointState& joint, std::span<const ActorState> actors)
{
    constexpr std::uint16_t FenrirModel = 0x1c37;
    if (joint.target == -1 || (joint.subtype != 4 && joint.subtype != 9))
        return {};
    if (joint.target < 0 || static_cast<std::size_t>(joint.target) >= actors.size())
        return Core::Unexpected(std::format("0x535d63: el personaje {} (+0xa0c) del joint 0x10f/{} no está entre los "
                                            "personajes del paso",
                                            joint.target, joint.subtype));
    const auto& actor = actors[static_cast<std::size_t>(joint.target)];
    const bool riding = actor.pet.model == FenrirModel && actor.safeZone == 0;
    if (joint.subtype == 4 && riding)
        joint.subtype = 9;
    else if (joint.subtype == 9 && !riding)
        joint.subtype = 4;
    return {};
}

// 0x5360dc..0x536185: corre los tramos con el destino, restando el viejo y sumando el nuevo en dos vueltas de float.
void ShiftByTarget(JointState& joint, const glm::vec3& target, bool add)
{
    const auto count = joint.count < 0 ? 0 : static_cast<std::size_t>(joint.count);
    for (std::size_t segment = 0; segment < count && segment < joint.points.size(); ++segment)
        for (auto& point : joint.points[segment])
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = add ? F32(D(target[axis]) + D(point[axis])) : F32(D(point[axis]) - D(target[axis]));
}

// 0x536073..0x5364d2: el 10f/0, /4 y /9 giran alrededor del dueño, 80 de radio y 120 de alto, 110 (o 140 la /9) por
// encima de su +0x10 + 10. El /4 y el /9 viven mientras el dueño tenga 8 o 0x2000000 en +0x78.
Core::Expected<void, std::string> StepTheRing(JointState& joint, const JointKernels::StepServices& services)
{
    const auto* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(std::format("0x536076: falta el personaje dueño del joint 0x10f/{}", joint.subtype));
    if (!owner->alive)
    {
        joint.life = 0;
        joint.alive = false;
        return {}; // 0x536088: sigue en la cola y la vida.
    }
    if (joint.subtype == 4 || joint.subtype == 9)
    {
        constexpr std::uint32_t BoundFlag = 8;
        constexpr std::uint32_t BlessedFlag = 0x2000000;
        if ((owner->flags78 & BoundFlag) == 0 && (owner->flags78 & BlessedFlag) == 0)
        {
            joint.life = 0;
            joint.alive = false;
            return {};
        }
        joint.life = 100; // 0x5360c4
    }
    ShiftByTarget(joint, joint.targetPosition, false);
    joint.targetPosition = owner->position;
    joint.targetPosition.z = F32(D(owner->position.z) + 10);
    ShiftByTarget(joint, joint.targetPosition, true);
    // 0x536187..0x536388: el acceso al contador de pasos (0x7d3a0f0), como el hada.
    if (services.cloth == nullptr || services.random == nullptr)
        return Core::Unexpected(
            std::format("0x536187: falta el contador compartido del joint 0x10f/{}", joint.subtype));
    const auto steps = std::bit_cast<std::uint32_t>(services.cloth->logicSteps);
    if (services.cloth->openCounterAccesses == 0)
        for (std::uint16_t draw = 0; draw < Render::Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(services.random->Next());
    const auto index = static_cast<std::uint32_t>(services.index);
    constexpr std::uint32_t PhaseStride = 53731;
    const auto phase = std::bit_cast<std::int32_t>(index * PhaseStride + ((index & 1u) != 0 ? steps : 0u - steps));
    // 0x5363bf: la frecuencia entera (0,048), la del 0x53d1a0 con escala 1.
    const auto direction = Render::Models::Detail::FaunaDirectionPC53(phase);
    if (!direction)
        return Core::Unexpected(
            std::format("0x536409: la dirección del joint 0x10f/{} excede el perfil PC53", joint.subtype));
    const double lift = joint.subtype == 9 ? 140 : 110; // 0x5364c9, 0x536491
    joint.position = {F32(D((*direction)[0]) * 80 + D(joint.targetPosition.x)),
                      F32(D((*direction)[1]) * 80 + D(joint.targetPosition.y)),
                      F32(D((*direction)[2]) * 120 + D(joint.targetPosition.z) + lift)};
    return {};
}

// 0x536005..0x53606e: el 10f/8 gira 25 en z alrededor de su +0x24, agrega la cola y sube 15.
Core::Expected<void, std::string> StepTheSpin(JointState& joint)
{
    joint.angles.z = F32(D(joint.angles.z) + 25);
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    const auto turned = Render::Models::OriginalVectorRotate(joint.velocity, matrix);
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(turned[axis]) + D(joint.pathIncrement[axis]));
    if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
        return appended;
    joint.pathIncrement.z = F32(D(joint.pathIncrement.z) + 15);
    return {};
}

// 0x536526..0x5366a7: el 10f/5..7, tres vueltas por paso. Cada una gira 10 en z, se pone en su +0x24 más la velocidad
// girada y agrega la cola. Con la tanda (+0x9eb) en 0, la velocidad y se frena (0,95) hasta −10 bajo 10; pasada la 40,
// cae 20, gira −5 y ensancha 15. Con la y negativa: la tanda 40 de la /5 y la /6 suelta el efecto 0x6eb (0x518cf0,
// otro lote: corta); la tanda sube y, hasta la 20, la y vuelve a −30 y el ancho a 40; después el +0x24 sube 5.
Core::Expected<void, std::string> StepTheRise(JointState& joint)
{
    constexpr float Brake = std::bit_cast<float>(0x3f733333u);  // 0x6ba30c, 0,95
    constexpr float Floor = std::bit_cast<float>(0xc1200000u);  // 0x5365b3, −10
    constexpr float Fall = std::bit_cast<float>(0xc1f00000u);   // 0x536642, −30
    constexpr float Narrow = std::bit_cast<float>(0x42200000u); // 0x53664c, 40
    constexpr float Fade = std::bit_cast<float>(0x3f555555u);   // 0x6b8b98, 0,8333333
    constexpr std::uint8_t BurstVolley = 0x28;
    constexpr std::uint8_t FallingVolleys = 0x14;
    for (int turn = 0; turn < 3; ++turn)
    {
        joint.angles.z = F32(D(joint.angles.z) + 10);
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        const auto turned = Render::Models::OriginalVectorRotate(joint.velocity, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(turned[axis]) + D(joint.pathIncrement[axis]));
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        const std::uint8_t volley = joint.volley;
        if (volley == 0)
        {
            // 0x5365a0: FST guarda el float y la comparación usa el producto retenido.
            const double braked = D(joint.velocity.y) * D(Brake);
            joint.velocity.y = F32(braked);
            if (braked < 10)
                joint.velocity.y = Floor;
        }
        if (volley > BurstVolley)
        {
            joint.velocity.y = F32(D(joint.velocity.y) - 20);
            joint.angles.z = F32(D(joint.angles.z) - 5);
            joint.width = F32(D(joint.width) + 15);
        }
        if (joint.velocity.y < 0)
        {
            if (volley == BurstVolley && joint.subtype <= 6)
                return Core::Unexpected(std::format("0x536626: el efecto 0x6eb del joint 0x10f/{} (0x518cf0) es de "
                                                    "otro lote (CL11)",
                                                    joint.subtype));
            joint.volley = static_cast<std::uint8_t>(joint.volley + 1);
            if (joint.volley < FallingVolleys)
            {
                joint.velocity.y = Fall;
                joint.width = Narrow;
            }
            else
                joint.pathIncrement.z = F32(D(joint.pathIncrement.z) + 5);
        }
    }
    if (joint.life < 10)
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * D(Fade));
    joint.width = F32(D(joint.width) - 5);
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateAuraJoint(JointState& joint, const World::FishEffectRequest& request)
{
    // 0x530edd: el destino es el +0x10 del dueño, que tiene que estar.
    const auto* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr || joint.ownerKind != JointOwnerKind::Actor)
        return Core::Unexpected(
            std::format("0x530ee0: falta el personaje dueño del joint 0x10f/{} (0x530860)", request.subtype));
    joint.targetPosition = owner->position;
    switch (request.subtype)
    {
    case 0:
        joint.light = {1, 1, 1};
        joint.life = RingLife;
        joint.maximumCount = RingPoints;
        joint.texture = RingTexture;
        break;
    case 4:
    case 9:
        joint.light = {std::bit_cast<float>(0x3ecccccdu), Tenth8, std::bit_cast<float>(0x3e4ccccdu)};
        joint.life = BindingLife;
        joint.maximumCount = RingPoints;
        joint.texture = RingTexture;
        break;
    case 2:
        joint.targetPosition = request.secondPosition;
        joint.light = {1, std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e99999au)};
        joint.life = BeamLife;
        joint.maximumCount = BeamPoints;
        joint.texture = BeamTexture;
        break;
    case 5:
    case 6:
    case 7:
    {
        joint.drawEnabled = 1;
        joint.tailEnabled = false;
        joint.life = RiseLife;
        joint.maximumCount = RingPoints;
        joint.volley = 0;
        if (request.subtype == 5)
            joint.light = {1, 1, Tenth8};
        else if (request.subtype == 6)
            joint.light = {1, Tenth8, 1};
        else
            joint.light = {Tenth8, 1, 1};
        joint.texture = RiseTexture;
        joint.velocity = {0, RiseSpeed, 0};
        joint.pathIncrement = request.secondPosition;
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        const auto turned = Render::Models::OriginalVectorRotate(joint.velocity, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(turned[axis]) + D(joint.pathIncrement[axis]));
        joint.count = -1;
        // 0x53115b: la cola antes del ancho, con el que quedó del uso anterior.
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        break;
    }
    case 8:
    {
        joint.drawEnabled = 1;
        joint.tailEnabled = false;
        joint.light = {0.5f, 0.5f, 0.5f};
        joint.texture = SpinTexture;
        joint.velocity = {0, SpinSpeed, 0};
        joint.life = SpinLife;
        joint.maximumCount = RingPoints;
        joint.pathIncrement = request.secondPosition;
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        const auto turned = Render::Models::OriginalVectorRotate(joint.velocity, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(turned[axis]) + D(joint.pathIncrement[axis]));
        break;
    }
    default:
        break; // 0x53120e: sólo el ancho.
    }
    joint.width = request.scale;
    return {};
}

Core::Expected<void, std::string> StepAuraJoint(JointState& joint, const JointKernels::StepServices& services)
{
    if (auto swapped = SwapByFenrir(joint, services.actors); !swapped.HasValue())
        return swapped;
    switch (joint.subtype)
    {
    case 2:
    {
        // 0x535dbe: sin dueño vivo, lo suelta y sigue en la cola. Con él, el ancho es la vida · 3 y el resto sale del
        // hueso del jugador (0x53d1a0, con escala 1,4f): corta.
        const auto* owner = JointKernels::ActorOwner(joint);
        if (owner == nullptr || !owner->alive)
        {
            joint.ownerContext = nullptr;
            return {};
        }
        joint.width = F32(static_cast<double>(joint.life) * 3);
        return Core::Unexpected(std::string("0x535e66: el punto del joint 0x10f/2 sale de 0x53d1a0 (escala 1,4), que "
                                            "no está portado (CL11)"));
    }
    case 5:
    case 6:
    case 7:
        return StepTheRise(joint);
    case 8:
        return StepTheSpin(joint);
    case 0:
    case 4:
    case 9:
        return StepTheRing(joint, services);
    default:
        return {}; // 0x536452: los demás siguen en la cola.
    }
}
} // namespace Client::Scenes::JointFamilies
