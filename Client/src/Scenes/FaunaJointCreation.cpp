#include "Scenes/FaunaJointCreation.h"
#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
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
void FirstQuad(JointState& joint)
{
    // 530afa: width es el retenido del uso anterior, antes de que el caso escriba el nuevo.
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
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
Core::Expected<void, std::string> CreateCase(JointState& joint, const World::FishEffectRequest& request,
                                             Render::Models::CrtRandom& random)
{
    const auto* owner = static_cast<const FaunaObject*>(joint.ownerContext);
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

Core::Expected<void, std::string> StepJointsChecked(std::span<JointState> joints, Render::Models::CrtRandom* random)
{
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        auto& joint = joints[i];
        if (!joint.alive)
            continue;
        const bool crywolf = joint.type == 0x697 && joint.subtype == 0;
        const bool tarkan = joint.type == 0x618 && joint.subtype == 4;
        const bool bloodCastle = joint.type == 0x10f && joint.subtype == 3;
        const bool icarus = joint.type == 0x610 && joint.subtype == 8;
        const bool noria = joint.type == 0x617 && joint.subtype >= 0 && joint.subtype <= 3;
        if (!crywolf && !tarkan && !bloodCastle && !icarus && !noria)
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
        const auto* owner = static_cast<const FaunaObject*>(joint.ownerContext);
        if (owner == nullptr && !icarus && !noria)
            return Core::Unexpected(std::format("{}: falta el dueño durable del joint {:#x}/{}",
                                                bloodCastle ? "0x535efe"
                                                : tarkan    ? "0x53820e"
                                                            : "0x5381ce",
                                                joint.type, joint.subtype));
        // 697 copia antes de comprobar active;618/4 comprueba antes de copiar.
        if (crywolf)
            joint.position = owner->previousPosition;
        if (!bloodCastle && !icarus && !noria && owner->active == 0)
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
        {
            const auto count = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.count) + 1u);
            const auto maximum = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.maximumCount) - 1u);
            joint.count = count > maximum ? maximum : count;
            if (joint.count >= static_cast<std::int32_t>(joint.points.size()) || joint.count < -1)
                return Core::Unexpected(std::string("0x53553f: la cola del joint excede sus puntos canónicos"));
            for (std::int32_t n = joint.count; n > 0; --n)
                joint.points[static_cast<std::size_t>(n)] = joint.points[static_cast<std::size_t>(n - 1)];
            FirstQuad(joint);
        }
        joint.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) - 1u);
        if (joint.life < 0)
            joint.alive = false;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepJoints(std::span<JointState> joints)
{
    return StepJointsChecked(joints, nullptr);
}

Core::Expected<void, std::string> StepJoints(std::span<JointState> joints, Render::Models::CrtRandom& random)
{
    return StepJointsChecked(joints, &random);
}
} // namespace Client::Scenes
