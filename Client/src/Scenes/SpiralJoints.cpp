#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
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

// El 0x62c (0x5346d8 y 0x53b93f): los que suben del efecto 0x62c/0 (la 0x41) y de la 0x4c, y los del hueso del dueño.
namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;
using Render::Models::OriginalAngleMatrix;
using Render::Models::OriginalVectorRotate;

constexpr std::uint16_t SpiralTexture = 0x614;
constexpr std::uint16_t OrbitTexture = 0x60d;
constexpr float Pale = std::bit_cast<float>(0x3f4ccccdu); // 0,8f

// La /1..4 y la /0xb..0xd, que dan la vuelta alrededor de su subida (0x53476c, 0x53ba2c).
bool Turns(std::int32_t subtype)
{
    return (subtype >= 1 && subtype <= 4) || (subtype >= 0xb && subtype <= 0xd);
}

// La /5..7: siguen un hueso del dueño (0x53bbe6).
bool Orbits(std::int32_t subtype)
{
    return subtype >= 5 && subtype <= 7;
}

glm::vec3 Sum(const glm::vec3& first, const glm::vec3& second)
{
    return {F32(D(first.x) + D(second.x)), F32(D(first.y) + D(second.y)), F32(D(first.z) + D(second.z))};
}

glm::vec3 Difference(const glm::vec3& first, const glm::vec3& second)
{
    return {F32(D(first.x) - D(second.x)), F32(D(first.y) - D(second.y)), F32(D(first.z) - D(second.z))};
}

void Fade(glm::vec3& light, float factor)
{
    light = {F32(D(light.x) * D(factor)), F32(D(light.y) * D(factor)), F32(D(light.z) * D(factor))};
}

// Un tramo de la subida: la cola con la matriz de los ángulos y el avance (0, velocidad, 0) girado por ella, sumado al
// incremento del camino (+0x24).
Core::Expected<Render::Models::OriginalMatrix, std::string> Rise(JointState& joint)
{
    const auto matrix = OriginalAngleMatrix(joint.angles);
    if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
        return Core::Unexpected(appended.Error());
    joint.pathIncrement = Sum(OriginalVectorRotate({0, joint.speed, 0}, matrix), joint.pathIncrement);
    return matrix;
}

// 0x53b974..0x53ba27: la /0 sube derecho, con más tramos en cada paso (+0x9ec, que crece de a 2).
Core::Expected<void, std::string> StepTheColumn(JointState& joint)
{
    constexpr double Brake = 2.0; // 0x6b8818
    for (std::int32_t step = 1; step < joint.field9ec; ++step)
    {
        if (auto risen = Rise(joint); !risen.HasValue())
            return Core::Unexpected(risen.Error());
        joint.position = joint.pathIncrement;
        joint.speed = F32(D(joint.speed) - Brake);
    }
    joint.field9ec += 2;
    return {};
}

// 0x53ba4c..0x53bbe1: la /1..4 y la /0xb..0xd esperan su tanda (+0x9eb) y después suben como la /0, corridas del
// camino por (0, 0, +0x58) girado por (0, +0x5c, la z de los ángulos): +0x5c baja 20 y +0x58 2,5 por tramo. La /3 y la
// /4 restan el corrimiento; las demás lo suman.
Core::Expected<void, std::string> StepTheSpiral(JointState& joint)
{
    if (joint.volley > 0)
    {
        --joint.volley;
        return {};
    }
    constexpr double Brake = 2.0;  // 0x6b8818
    constexpr double Turn = 20.0;  // 0x6b86d0
    constexpr double Shrink = 2.5; // 0x6b89a8
    for (std::int32_t step = 1; step < joint.field9ec; ++step)
    {
        if (auto risen = Rise(joint); !risen.HasValue())
            return Core::Unexpected(risen.Error());
        // 0x53bab9..0x53bafc: la condición del original deja siempre la resta (Q-C1624).
        joint.targetPosition.y = F32(D(joint.targetPosition.y) - Turn);
        const auto turn = OriginalAngleMatrix({0, joint.targetPosition.y, joint.angles.z});
        const glm::vec3 offset = OriginalVectorRotate({0, 0, joint.targetPosition.x}, turn);
        joint.position = joint.subtype == 3 || joint.subtype == 4 ? Difference(joint.pathIncrement, offset)
                                                                  : Sum(offset, joint.pathIncrement);
        joint.speed = F32(D(joint.speed) - Brake);
        joint.targetPosition.x = F32(D(joint.targetPosition.x) - Shrink);
    }
    joint.field9ec += 2;
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateSpiralJoint(JointState& joint, const World::FishEffectRequest& request,
                                                    Render::Models::CrtRandom& random)
{
    // 0x5346d8..0x534765: lo común, con la z 150 más alta y el camino (+0x24) en la posición.
    constexpr double Lift = 150.0; // 0x6b87ac
    joint.tailEnabled = false;
    joint.speed = -3.0f; // 0xc0400000
    joint.position.z = F32(D(joint.position.z) + Lift);
    joint.width = request.scale;
    joint.life = 20;
    joint.maximumCount = 30;
    joint.velocity = {};
    joint.light = {1, Pale, 1};
    joint.field9ec = 1;
    joint.pathIncrement = joint.position;
    joint.targetPosition = {};
    joint.volley = 0;
    const std::int32_t subtype = joint.subtype;
    if (Turns(subtype))
    {
        // 0x53477e..0x534887: la tanda rand() % 3 + 2 (en 0 con la /3 y la /4) se suma a la vida; el radio 80 y el
        // giro 180 (−180 con la /2, la /4 y la /0xb..0xd, que tienen además su color) ponen la posición.
        joint.texture = SpiralTexture;
        joint.speed = -3.0f;
        joint.targetPosition.x = 80;
        joint.targetPosition.y = 180;
        joint.volley = static_cast<std::uint8_t>(random.Next() % 3 + 2);
        if (subtype == 2 || subtype == 4 || subtype >= 0xb)
        {
            constexpr float Soft = std::bit_cast<float>(0x3f333333u); // 0,7f
            constexpr float Dim = std::bit_cast<float>(0x3f19999au);  // 0,6f
            if (subtype == 0xb)
                joint.light = {Soft, 1, Soft};
            else if (subtype == 0xc)
                joint.light = {1, Dim, Dim};
            else if (subtype == 0xd)
                joint.light = {Soft, Soft, 1};
            joint.targetPosition.y = -180;
        }
        if (subtype == 3 || subtype == 4)
            joint.volley = 0;
        joint.life += joint.volley;
        const auto matrix = OriginalAngleMatrix(joint.targetPosition);
        joint.position = Sum(OriginalVectorRotate({0, 0, joint.targetPosition.x}, matrix), joint.pathIncrement);
    }
    else if (subtype != 0)
    {
        // 0x534897..0x534926: los demás subtipos (los /5..7 del efecto 0x62c/1) quedan quietos en el camino, con el
        // hueso del dueño en +0x9ec (el skill del pedido), una sola vuelta de cola y el giro subtipo · 90 en +0x60.
        joint.skill = 0;
        joint.speed = 0;
        joint.field9ec = request.skill & 0xff;
        joint.velocity = {0, -4, 0};
        joint.life = 15;
        joint.maximumCount = 1;
        joint.targetPosition.z =
            static_cast<float>(std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(subtype) * 90u));
        joint.volley = 30;
        joint.targetPosition.x = 30;
        if (joint.key != -1)
            joint.velocity.y = 0;
    }
    // 0x53492c..0x534954 y 0x534cb6.
    if (Orbits(subtype))
        joint.texture = OrbitTexture;
    else if (subtype == 0)
        joint.texture = SpiralTexture;
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

Core::Expected<void, std::string> StepSpiralJoint(JointState& joint, const JointKernels::StepServices& /*services*/)
{
    const std::int32_t subtype = joint.subtype;
    if (Orbits(subtype))
    {
        // 0x53bbe6..0x53be49: sin dueño, muere (y sigue a la cola). Con dueño, el camino es el punto (0, 20, 0) de su
        // hueso (+0x120 + +0x9ec · 0x30, 0x4ec110 con el modelo de su tipo) y cada vuelta gira la velocidad y llama a
        // 0x535520 con esa matriz.
        if (joint.ownerContext != nullptr)
            return Core::Unexpected(std::format("0x53bc71: el joint 0x62c/{} con dueño lee el hueso {} del dueño "
                                                "(+0x120, 0x4ec110), que no está portado (CL11)",
                                                subtype, joint.field9ec));
        joint.alive = false;
        constexpr std::int32_t FadeLife = 7;
        if (joint.life < FadeLife)
            Fade(joint.light, std::bit_cast<float>(0x3f2aaaabu)); // 0x6b8c64, 2/3
        return {};
    }
    // 0x53b95d..0x53b966: con la cola llena (la cuenta en los puntos − 1), sólo se apaga.
    if (joint.count < joint.maximumCount - 1)
    {
        if (subtype == 0)
        {
            if (auto stepped = StepTheColumn(joint); !stepped.HasValue())
                return stepped;
        }
        else if (Turns(subtype))
        {
            if (auto stepped = StepTheSpiral(joint); !stepped.HasValue())
                return stepped;
        }
    }
    // 0x53be4c..0x53be71 y 0x53b671: la /0..4 y la /0xb..0xd con menos de 10 de vida.
    constexpr std::int32_t FadeLife = 10;
    if ((subtype == 0 || Turns(subtype)) && joint.life < FadeLife)
        Fade(joint.light, std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
    return {};
}
} // namespace Client::Scenes::JointFamilies
