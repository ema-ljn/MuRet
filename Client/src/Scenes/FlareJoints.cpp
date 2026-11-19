#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"

#include <bit>
#include <cstdint>
#include <string>

// El 0x5ac (alta 0x530c93..0x530dd8 y 0x53323b..0x533253, paso 0x5359d7..0x535b32): los rayos que el 0x114/0 (0x51c1b8)
// y el combo de BlastModelEffects (0x51c004) sueltan de a sesenta, sin dueño. Sin la cola común (+0x9f2 en 0) ni el
// movimiento común (0x53585e): el paso mismo agrega los tramos y avanza.
namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;

// 0x535a07..0x535a61 y 0x535aa0..0x535b0c: un tramo con la matriz de los ángulos (0x535520) y después el avance,
// (0, −velocidad, 0) girado (0x5fcbd0) y sumado a la posición.
Core::Expected<void, std::string> Advance(JointState& joint, const Render::Models::OriginalMatrix& matrix)
{
    if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
        return appended;
    const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    return {};
}
} // namespace

void CreateFlareJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random)
{
    // 0x530c93: todos sin la cola común.
    joint.tailEnabled = false;
    constexpr int FullTurn = 360;
    if (joint.subtype == 0)
    {
        // 0x530ca5..0x530d35: vida 20, 30 puntos, el ancho; la velocidad rand() % 10 + 10, la x de los ángulos
        // 30 − rand() % 40, la y en 0 y la z rand() % 360; la velocidad en 0 y la luz (0,1f; 0,5f; 1) (0x53323b).
        constexpr std::int32_t Life = 20, Points = 30;
        constexpr double SpeedBase = 10.0, Tilt = 30.0;
        constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
        joint.life = Life;
        joint.maximumCount = Points;
        joint.width = request.scale;
        const std::int32_t speed = random.Next() % 10;
        joint.speed = F32(static_cast<double>(speed) + SpeedBase);
        const std::int32_t tilt = random.Next() % 40;
        joint.angles.x = F32(Tilt - static_cast<double>(tilt));
        joint.angles.y = 0;
        joint.angles.z = static_cast<float>(random.Next() % FullTurn);
        joint.velocity = {0, 0, 0};
        joint.light = {Dim, 0.5f, 1};
        // 0x533242..0x533250: +0x24 guarda la posición.
        joint.pathIncrement = joint.position;
    }
    else if (joint.subtype == 1)
    {
        // 0x530d43..0x530dd8: vida 10, 20 puntos, el ancho; el skill (+0x9ea) rand() % 5, la velocidad rand() % 5 + 1,
        // los ángulos (30, 0, rand() % 360), la velocidad en 0 y la luz (1; 0,8f; 0,6f). 0x533242: +0x24.
        constexpr std::int32_t Life = 10, Points = 20;
        constexpr float Tilt = 30;
        joint.life = Life;
        joint.maximumCount = Points;
        joint.width = request.scale;
        joint.skill = static_cast<std::uint8_t>(random.Next() % 5);
        const std::int32_t speed = random.Next() % 5;
        joint.speed = F32(static_cast<double>(speed) + 1.0);
        joint.angles.x = Tilt;
        joint.angles.y = 0;
        joint.angles.z = static_cast<float>(random.Next() % FullTurn);
        joint.velocity = {0, 0, 0};
        joint.light = {1, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
        joint.pathIncrement = joint.position;
    }
    // 0x534cb6: el tope de los puntos (los demás subtipos se quedan con los del lugar).
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
}

Core::Expected<void, std::string> StepFlareJoint(JointState& joint)
{
    if (joint.subtype == 0 && joint.life > 16)
    {
        // 0x5359e2..0x535a61: con vida de más de 16, diez tramos con la matriz de los ángulos.
        constexpr int Steps = 10;
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        for (int step = 0; step < Steps; ++step)
            if (auto advanced = Advance(joint, matrix); !advanced.HasValue())
                return advanced;
    }
    else if (joint.subtype == 1 && (joint.skill == 0 || joint.life > 3))
    {
        // 0x535a71..0x535b0c: con el skill en 0 o la vida de más de 3, cinco tramos; la velocidad suma 0,5 en cada uno.
        constexpr int Steps = 5;
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        for (int step = 0; step < Steps; ++step)
        {
            if (auto advanced = Advance(joint, matrix); !advanced.HasValue())
                return advanced;
            joint.speed = F32(D(joint.speed) + 0.5);
        }
    }
    // 0x535b0e..0x535b2f: todos apagan la luz por 0,8333333f (0x6b8b98).
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    return {};
}
} // namespace Client::Scenes::JointFamilies
