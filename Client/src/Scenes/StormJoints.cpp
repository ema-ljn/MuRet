#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;
} // namespace

Core::Expected<void, std::string> CreateStormJoint(JointState& joint, const World::FishEffectRequest& request,
                                                   Render::Models::CrtRandom& random)
{
    if (request.subtype == StormSubtype)
    {
        // 0x534b27..0x534b2e: sin cola y con vida 0; sus tres /6 los crea FaunaJointCreation.cpp (0x534baa).
        joint.tailEnabled = false;
        joint.life = 0;
        return {};
    }
    if (request.subtype != BoltSubtype)
        return Core::Unexpected(
            std::format("0x534959: la creación del joint 0x632/{} no está portada (CL11)", request.subtype));
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

Core::Expected<void, std::string> StepStormJoint(JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.subtype == StormSubtype)
        return {}; // 0x53bec5: la /7 sólo pasa por la cola y la vida.
    if (joint.subtype != BoltSubtype)
        return Core::Unexpected(
            std::format("0x53be76: el paso del joint 0x632/{} no está portado (CL11)", joint.subtype));
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
    // partícula 0x5ee; después, el ancho 40, la altura del terreno y los 0x614/4 y las 0x6ac al tocarla.
    if (static_cast<std::int32_t>(joint.volley) - joint.life < 10)
        return Core::Unexpected(std::string("0x53bf6e: la partícula 0x5ee (0x53fe70) del joint 0x632/6 es del lote de "
                                            "partículas (CL11)"));
    joint.width = 40;
    return Core::Unexpected(std::string("0x53bf8d: el suelo del joint 0x632/6 (0x5f93c0, los 0x614/4 y las partículas "
                                        "0x6ac) no está portado (CL11)"));
}
} // namespace Client::Scenes::JointFamilies
