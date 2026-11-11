#include "Scenes/FaunaJointCreation.h"
#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/OriginalAngles.h"
#include "World/Particles.h"
#include <cmath>
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/FaunaObject.h"
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
void Prefix(JointState& joint, const World::FishEffectRequest& request)
{
    joint.alive = true;
    joint.type = request.type;
    joint.texture = static_cast<std::uint16_t>(request.type);
    joint.subtype = request.subtype;
    joint.blend = 1;
    joint.drawEnabled = 3;
    joint.field9d4 = 0;
    joint.key = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(request.key));
    joint.skill = static_cast<std::uint8_t>(request.skill);
    joint.skillSerial = static_cast<std::uint8_t>(request.skillSerial);
    joint.speed = 0;
    joint.ownerContext = nullptr;
    joint.tailEnabled = true;
    joint.category = 0;
    joint.repeatedTexture = false;
    joint.orientation = 0;
    joint.position = request.firstPosition;
    joint.angles = request.angles;
    joint.light = {1, 1, 1};
    joint.velocity = {};
    if (request.ownerContext == nullptr)
        joint.targetPosition = request.secondPosition;
    else
        joint.ownerContext = request.ownerContext;
    joint.count = 0;
    joint.icarusRotations.reset();
}
void FirstQuad(JointState& joint, const Render::Models::OriginalMatrix& matrix)
{
    // 530afa: width es el retenido del uso anterior, antes de que el caso escriba el nuevo.
    const auto half = static_cast<float>(static_cast<double>(joint.width) * 0.5);
    const std::array<glm::vec3, 4> offsets{{{-half, 0, 0}, {half, 0, 0}, {0, 0, -half}, {0, 0, half}}};
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        const auto rotated = Render::Models::OriginalVectorRotate(offsets[i], matrix);
        joint.points[0][i] = {static_cast<float>(static_cast<double>(joint.position.x) + rotated.x),
                              static_cast<float>(static_cast<double>(joint.position.y) + rotated.y),
                              static_cast<float>(static_cast<double>(joint.position.z) + rotated.z)};
    }
}
void FirstQuad(JointState& joint)
{
    FirstQuad(joint, Render::Models::OriginalAngleMatrix(joint.angles));
}
Core::Expected<void, std::string> AppendJointTail(JointState& joint, const Render::Models::OriginalMatrix& matrix)
{
    const auto count = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.count) + 1u);
    const auto maximum = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.maximumCount) - 1u);
    joint.count = count > maximum ? maximum : count;
    if (joint.count >= static_cast<std::int32_t>(joint.points.size()) || joint.count < -1)
        return Core::Unexpected(std::string("0x53553f: la cola del joint excede sus puntos canónicos"));
    for (std::int32_t n = joint.count; n > 0; --n)
        joint.points[static_cast<std::size_t>(n)] = joint.points[static_cast<std::size_t>(n - 1)];
    FirstQuad(joint, matrix);
    return {};
}
Core::Expected<void, std::string> AppendJointTail(JointState& joint)
{
    return AppendJointTail(joint, Render::Models::OriginalAngleMatrix(joint.angles));
}
Core::Expected<void, std::string> CreateHelperBeam(JointState& joint, const World::FishEffectRequest& request)
{
    joint.width = request.scale;
    joint.life = 0;
    joint.maximumCount = 10;
    joint.angles = {};
    joint.light = {0.3f, 0.3f, 1};
    joint.blend = 1;
    joint.speed = request.type == 0x614 ? 50.0f : 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        joint.pathIncrement[axis] =
            static_cast<float>((static_cast<double>(request.secondPosition[axis]) - joint.position[axis]) * 0.1f);
    for (int sample = 0; sample < 9; ++sample)
    {
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] =
                static_cast<float>(static_cast<double>(joint.position[axis]) + joint.pathIncrement[axis]);
        if (auto appended = AppendJointTail(joint); !appended.HasValue())
            return appended;
        // 5318e0/533385 conservan estos dos stores, aunque algebraicamente se cancelen.
        for (int axis = 0; axis < 2; ++axis)
            joint.position[axis] = static_cast<float>(
                (static_cast<double>(joint.position[axis]) - joint.pathIncrement[axis]) + joint.pathIncrement[axis]);
    }
    joint.position = joint.targetPosition; // 5333a7 lee +58 retenido con dueño opaco no nulo.
    return {};
}

Core::Expected<void, std::string> CreateCase(JointState& joint, const World::FishEffectRequest& request,
                                             Render::Models::CrtRandom& random)
{
    const auto* owner =
        (joint.type == 0x613 || joint.type == 0x614) ? nullptr : static_cast<const FaunaObject*>(joint.ownerContext);
    if (request.type == 0x697 && request.subtype == 0)
    {
        joint.count = -1;
        joint.width = request.scale;
        joint.life = 120;
        joint.maximumCount = 28;
        joint.speed = 3;
        return {};
    }
    if (request.type == 0x610 && request.subtype == 8)
    {
        joint.count = -1;
        joint.life = joint.key; // 531c43..531c60, antes del selector de color y de las escrituras de/8.
        joint.maximumCount = 50;
        joint.key = 0;
        if (joint.skill == 3)
        {
            joint.width = 20;
            if (owner == nullptr)
                return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610"));
            joint.light = owner->light;
        }
        joint.width = request.scale;
        joint.life = 100;
        joint.maximumCount = 20;
        joint.drawEnabled = 2;
        joint.blend = 4;
        joint.category = 1;
        joint.field9ec = joint.skill;
        joint.light = {1, 1, 1};
        if (owner == nullptr)
        {
            joint.alive = false;
            return Core::Unexpected(std::string("0x531f50: falta el dueño durable del joint 0x610/8"));
        }
        joint.position = owner->position;
        joint.angles = owner->angles;
        joint.position.z = 300;
        joint.targetPosition = request.secondPosition;
        return {};
    }
    FirstQuad(joint);
    if ((request.type == 0x613 && request.subtype == 4) || (request.type == 0x614 && request.subtype == 14))
        return CreateHelperBeam(joint, request);
    if (request.type == 0x617 && request.subtype >= 0 && request.subtype <= 3)
    {
        // 534e9c: /0→53202f,/1→5320a4,/2→5320dd,/3→532085.
        joint.width = request.subtype == 2 ? 4.0f : 2.0f;
        joint.speed =
            request.subtype == 2 ? 30.0f : static_cast<float>(random.Next() % 20 + (request.subtype == 1 ? 16 : 6));
        joint.life = random.Next() % (request.subtype == 1 || request.subtype == 2 ? 4 : 8) +
                     (request.subtype == 1 || request.subtype == 2 ? 4 : 8);
        joint.maximumCount = 2;
        joint.light = request.subtype == 2 ? glm::vec3(0.3f, 0.3f, 1) : glm::vec3(1);
        return {};
    }
    if (request.type == 0x618 && request.subtype == 4)
    {
        joint.width = request.scale;
        joint.life = 999999999;
        joint.maximumCount = 20;
        if (owner == nullptr)
            return Core::Unexpected(std::string("0x53229c: falta el dueño durable del joint 0x618/4"));
        joint.position = owner->previousPosition;
        joint.light = {0.3f, 0.15f, 0.1f};
        return {};
    }
    if (request.type == 0x10f && (request.subtype == 1 || request.subtype == 3))
    {
        if (owner == nullptr)
            return Core::Unexpected(std::string("0x530ee0: falta el dueño durable del joint 0x10f"));
        joint.targetPosition = owner->position;
        if (request.subtype == 1)
            joint.light = {0.2f, 0.2f, 0.2f};
        else
        {
            joint.light = owner->light;
            joint.angles = {-90, 0, 0};
            joint.velocity.y = static_cast<float>(random.Next() % 500);
            joint.speed = static_cast<float>(random.Next() % 5 + 5);
        }
        joint.life = 999999;
        joint.maximumCount = 30;
        joint.texture = 0x613;
        joint.width = request.scale;
        return {};
    }
    return Core::Unexpected(
        std::format("0x530c18: la creación del joint {:#x}/{} no está portada", request.type, request.subtype));
}
} // namespace

Core::Expected<std::optional<std::size_t>, std::string> CreateFaunaJoint(std::span<JointState> joints,
                                                                         const World::FishEffectRequest& request,
                                                                         Render::Models::CrtRandom& random)
{
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        if (joints[i].alive)
            continue;
        Prefix(joints[i], request);
        if (auto created = CreateCase(joints[i], request, random); !created.HasValue())
            return Core::Unexpected(created.Error());
        return std::optional<std::size_t>(i);
    }
    return std::optional<std::size_t>{};
}

namespace
{
std::int32_t AddWrapped(std::int32_t value, std::uint32_t amount)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) + amount);
}

Core::Expected<void, std::string> StepBloodCastleJoint(JointState& joint, const FaunaObject& owner,
                                                       Render::Models::CrtRandom* random)
{
    if (owner.active == 0)
    {
        joint.life = 0;
        joint.alive = false;
        return {}; // 535f13 continúa en la cola común, incluso con alive=0.
    }
    if (random == nullptr)
        return Core::Unexpected(std::string("0x535f18: falta el RNG del paso del joint 0x10f/3"));
    if (random->Next() % 5 == 0)
        joint.life = AddWrapped(joint.life, 0xfffffffbu);
    else if ((random->Next() & 1) == 0)
        joint.life = AddWrapped(joint.life, 1);
    const auto angle = (static_cast<double>(joint.life) - joint.velocity.y) * static_cast<double>(0.05f);
    const auto product = Render::Models::Detail::SineProductPC53(angle, 3.0f);
    if (!product.has_value())
        return Core::Unexpected(std::string("0x535f6b: el seno del joint 0x10f/3 excede el perfil matemático"));
    joint.velocity.x = static_cast<float>(*product);
    // FSTS conserva el producto retenido para el FADD siguiente: no sumar velocity.x redondeado.
    joint.position.x = static_cast<float>(static_cast<double>(joint.position.x) + *product);
    double factor =
        owner.timer > 50
            ? static_cast<double>(std::bit_cast<std::int32_t>(100u - std::bit_cast<std::uint32_t>(owner.timer))) *
                  static_cast<double>(0.025f)
            : static_cast<double>(owner.timer) * static_cast<double>(0.1f);
    if (factor > 1)
        factor = 1;
    joint.light = {static_cast<float>(factor * owner.light.x), static_cast<float>(factor * owner.light.y),
                   static_cast<float>(factor * owner.light.z)};
    return {};
}

Core::Expected<void, std::string> StepFairyJoint(JointState& joint, const FaunaObject& owner, std::size_t index,
                                                 Render::Models::CrtRandom* random, Render::Models::ClothWorld* cloth)
{
    if (owner.active == 0)
    {
        joint.life = 0;
        joint.alive = false;
        return {}; // 53607b continúa en cola y vida, sin leer el contador.
    }
    joint.targetPosition = owner.position;
    joint.targetPosition.z = static_cast<float>(static_cast<double>(owner.position.z) + 10);
    if (cloth == nullptr || random == nullptr)
        return Core::Unexpected(std::string("0x536187: falta el contador compartido del joint0x10f/1"));
    const auto steps = std::bit_cast<std::uint32_t>(cloth->logicSteps);
    if (cloth->openCounterAccesses == 0)
        for (std::uint16_t draw = 0; draw < Render::Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(random->Next());
    const auto physicalIndex = static_cast<std::uint32_t>(index);
    constexpr std::uint32_t PhaseStride = 53731;
    const auto phase =
        std::bit_cast<std::int32_t>(physicalIndex * PhaseStride + ((physicalIndex & 1u) != 0 ? steps : 0u - steps));
    const auto direction = Render::Models::Detail::JointDirectionPC53(phase);
    if (!direction)
        return Core::Unexpected(std::string("0x536409: la dirección del joint0x10f/1 excede el perfil PC53"));
    joint.light.x = 0.2f;
    joint.light.y = 0.2f;
    joint.position = {static_cast<float>(static_cast<double>((*direction)[0]) * 70 + joint.targetPosition.x),
                      static_cast<float>(static_cast<double>((*direction)[1]) * 70 + joint.targetPosition.y),
                      static_cast<float>(static_cast<double>((*direction)[2]) * 140 + joint.targetPosition.z)};
    joint.light.z = static_cast<float>(static_cast<double>((*direction)[3]) * 0.2f + 0.4f);
    return {};
}

// 4e9810: los cuadrados X/Y y delta Z se derraman antes de las dos raíces.
float SteerHelperBeam(JointState& joint)
{
    using Render::Models::CreateAngle;
    using Render::Models::TurnAngle;
    constexpr float TurnStep = 25;
    constexpr double FullTurn = 360;
    const auto yaw = static_cast<float>(
        CreateAngle(joint.position.x, joint.position.y, joint.targetPosition.x, joint.targetPosition.y));
    joint.angles.z = static_cast<float>(TurnAngle(joint.angles.z, yaw, TurnStep));
    const double x = static_cast<double>(joint.position.x) - joint.targetPosition.x;
    const double y = static_cast<double>(joint.position.y) - joint.targetPosition.y;
    const auto z = static_cast<float>(static_cast<double>(joint.position.z) - joint.targetPosition.z);
    const auto xSquared = static_cast<float>(x * x);
    const auto ySquared = static_cast<float>(y * y);
    const auto horizontal = static_cast<float>(std::sqrt(static_cast<double>(xSquared) + ySquared));
    const auto pitch =
        static_cast<float>(FullTurn - CreateAngle(joint.position.z, horizontal, joint.targetPosition.z, 0));
    joint.angles.x = static_cast<float>(TurnAngle(joint.angles.x, pitch, TurnStep));
    return static_cast<float>(std::sqrt((static_cast<double>(z) * z + xSquared) + ySquared));
}

Core::Expected<void, std::string> EmitHelperBeamParticle(JointState& joint, std::int32_t type,
                                                         Render::Models::CrtRandom& random, World::Particles* particles,
                                                         float worldClock)
{
    if (particles == nullptr)
        return Core::Unexpected(std::string("0x536eb9: faltan las partículas del joint0x614/14"));
    auto created = particles->CreateChecked(type, joint.position, joint.angles, joint.light, 0, 1, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x536eb9: falló la partícula {:#x} del joint0x614/14 ({})", type,
                                            static_cast<int>(created.Error())));
    return {};
}

Render::Models::OriginalMatrix JitterHelperBeam(JointState& joint, Render::Models::CrtRandom& random)
{
    constexpr int JitterRange = 1024;
    constexpr int JitterCenter = 512;
    joint.velocity.x =
        static_cast<float>(static_cast<double>(random.Next() % JitterRange - JitterCenter) / joint.width);
    joint.velocity.z =
        static_cast<float>(static_cast<double>(random.Next() % JitterRange - JitterCenter) / joint.width);
    glm::vec3 perturbed;
    for (int axis = 0; axis < 3; ++axis)
        perturbed[axis] = static_cast<float>(static_cast<double>(joint.angles[axis]) + joint.velocity[axis]);
    return Render::Models::OriginalAngleMatrix(perturbed);
}

Core::Expected<void, std::string> FinishHelperBeam(JointState& joint, Render::Models::CrtRandom& random,
                                                   World::Particles* particles, float worldClock)
{
    constexpr std::uint32_t ParticleWidthBits = 0x42480000;
    if (std::bit_cast<std::uint32_t>(joint.width) != ParticleWidthBits)
        return {};
    if (auto emitted = EmitHelperBeamParticle(joint, World::ThunderParticle, random, particles, worldClock);
        !emitted.HasValue())
        return emitted;
    constexpr int SmokeChance = 8;
    if (random.Next() % SmokeChance != 0)
        return {};
    // 536eef..536f55: calcula scratch pero53fe70 recibe position, no el scratch.
    for (int axis = 0; axis < 3; ++axis)
        static_cast<void>(random.Next());
    return EmitHelperBeamParticle(joint, World::BreathParticle, random, particles, worldClock);
}

Core::Expected<void, std::string> StepHelperBeam(JointState& joint, Render::Models::CrtRandom* random,
                                                 World::Particles* particles, float worldClock)
{
    for (std::int32_t iteration = 0; iteration < joint.maximumCount; ++iteration)
    {
        const float distance = SteerHelperBeam(joint);
        if (random == nullptr)
            return Core::Unexpected(std::string("0x536cb3: falta el RNG del joint0x614/14"));
        const auto matrix = JitterHelperBeam(joint, *random);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        constexpr double ReachFactor = 1.5;
        if (static_cast<double>(joint.speed) * ReachFactor > distance)
            return FinishHelperBeam(joint, *random, particles, worldClock);
        const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = static_cast<float>(static_cast<double>(joint.position[axis]) + moved[axis]);
    }
    return {};
}

Core::Expected<void, std::string> StepJointsChecked(std::span<JointState> joints, Render::Models::CrtRandom* random,
                                                    Render::Models::ClothWorld* cloth, World::Particles* particles,
                                                    float worldClock)
{
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        auto& joint = joints[i];
        if (!joint.alive)
            continue;
        const bool crywolf = joint.type == 0x697 && joint.subtype == 0;
        const bool tarkan = joint.type == 0x618 && joint.subtype == 4;
        const bool bloodCastle = joint.type == 0x10f && joint.subtype == 3;
        const bool fairy = joint.type == 0x10f && joint.subtype == 1;
        const bool spiritBeam = joint.type == 0x613 && joint.subtype == 4;
        const bool flareBeam = joint.type == 0x614 && joint.subtype == 14;
        const bool icarus = joint.type == 0x610 && joint.subtype == 8;
        const bool noria = joint.type == 0x617 && joint.subtype >= 0 && joint.subtype <= 3;
        if (!crywolf && !tarkan && !bloodCastle && !fairy && !spiritBeam && !flareBeam && !icarus && !noria)
            return Core::Unexpected(
                std::format("0x535840: el paso del joint {} ({:#x}/{}) no está portado", i, joint.type, joint.subtype));
        // 53590e..53597a: el movimiento común precede la lectura del dueño, incluso si muere.
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        // 535932 también omite las sumas para unordered, conservando ceros con signo y posición.
        if (joint.speed < 0 || joint.speed > 0)
        {
            const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
            joint.position = {static_cast<float>(static_cast<double>(joint.position.x) + moved.x),
                              static_cast<float>(static_cast<double>(joint.position.y) + moved.y),
                              static_cast<float>(static_cast<double>(joint.position.z) + moved.z)};
        }
        if (flareBeam)
            if (auto result = StepHelperBeam(joint, random, particles, worldClock); !result.HasValue())
                return result;
        const auto* owner = (joint.type == 0x613 || joint.type == 0x614)
                                ? nullptr
                                : static_cast<const FaunaObject*>(joint.ownerContext);
        if (owner == nullptr && !spiritBeam && !flareBeam && !icarus && !noria)
            return Core::Unexpected(std::format("{}: falta el dueño durable del joint {:#x}/{}",
                                                fairy         ? "0x536076"
                                                : bloodCastle ? "0x535efe"
                                                : tarkan      ? "0x53820e"
                                                              : "0x5381ce",
                                                joint.type, joint.subtype));
        // 697 copia antes de comprobar active;618/4 comprueba antes de copiar.
        if (crywolf)
            joint.position = owner->previousPosition;
        if (!bloodCastle && !fairy && !spiritBeam && !flareBeam && !icarus && !noria && owner->active == 0)
        {
            joint.alive = false;
            continue; // 5381e9: no agrega cola ni descuenta vida.
        }
        if (tarkan)
            joint.position = owner->previousPosition;
        if (icarus)
        {
            // 53801e..538055: dueño nulo apaga; muerto también copia. Ambos continúan en cola/vida.
            joint.alive = owner != nullptr && owner->active != 0;
            if (owner != nullptr)
            {
                joint.position = owner->position;
                joint.position.z = 300;
            }
        }
        if (fairy)
            if (auto result = StepFairyJoint(joint, *owner, i, random, cloth); !result.HasValue())
                return result;
        if (bloodCastle)
            if (auto result = StepBloodCastleJoint(joint, *owner, random); !result.HasValue())
                return result;
        if (noria && (joint.subtype == 1 || joint.subtype == 3))
        {
            // 538074: las otras dos ramas continúan directamente en cola/vida, sin RNG.
            joint.speed = static_cast<float>(static_cast<double>(joint.speed) +
                                             static_cast<double>(joint.subtype == 1 ? 0.3f : 0.1f));
            constexpr float FirstFade = std::bit_cast<float>(0x3f36db6eu);
            constexpr float ThirdFade = std::bit_cast<float>(0x3f68ba2eu);
            const auto faded = static_cast<float>(static_cast<double>(joint.light.x) *
                                                  static_cast<double>(joint.subtype == 1 ? FirstFade : ThirdFade));
            joint.light = glm::vec3(faded);
        }
        if (joint.tailEnabled)
            if (auto appended = (flareBeam ? AppendJointTail(joint, matrix) : AppendJointTail(joint));
                !appended.HasValue())
                return appended;
        joint.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) - 1u);
        if (joint.life < 0)
            joint.alive = false;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepJoints(std::span<JointState> joints)
{
    return StepJointsChecked(joints, nullptr, nullptr, nullptr, 0);
}

Core::Expected<void, std::string> StepJoints(std::span<JointState> joints, Render::Models::CrtRandom& random,
                                             Render::Models::ClothWorld* cloth, World::Particles* particles,
                                             float worldClock)
{
    return StepJointsChecked(joints, &random, cloth, particles, worldClock);
}
} // namespace Client::Scenes
