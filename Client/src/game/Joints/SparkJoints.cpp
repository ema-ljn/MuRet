#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Actors/ActorFlags.h"
#include "Kernel/ActorState.h"
#include "Joints/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

// El 0x610/5, las chispas que el sexto ayudante de Kalima (0x196) suelta de sus huesos 63..67 hacia el 60 (0x43e904):
// alta 0x531c43..0x531cc7 y 0x531dd0..0x531dff, paso 0x537a00..0x537b31. Y el 0x60f/0x2a, el que gira alrededor del
// NPC 257 (0x5165d1, con él de dueño), con las 0x60f/0x29 que suelta (0x53a0eb): alta 0x5339a5 y 0x533915, paso
// 0x53a039 y 0x539fc2.
namespace Client::Scenes::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

constexpr double Glimmer = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu)); // 0x6b8bd4, 0,03f

// 0x531c43..0x531cc7 y 0x531dd0..0x532003: lo común del 0x610 (la vida en la key, la key en 0, 50 puntos y el ancho
// del color del skill: 20 con el 0 y el 3, 40 con el 1; con otro, el ancho que tenía el lugar) y la /5: la luz en 1
// (el color la pisa), la mezcla 4, sólo la segunda cara (+0x11 en 2), velocidad 3, vida 50, 8 puntos y el pase 2
// (+0x64). El destino es el segundo punto. Sin rand().
Core::Expected<void, std::string> CreateHelperSpark(JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::uint8_t OwnerColor = 3;
    // 0x531c71..0x531c8c: el color 3 lee la luz del dueño (+0xf4), que la /5 pisa; sin dueño, el original cae.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/5"));
    joint.life = joint.key;
    joint.key = 0;
    constexpr std::int32_t SharedPoints = 50;
    joint.maximumCount = SharedPoints;
    constexpr float NarrowWidth = 20.0f, WideWidth = 40.0f; // 0x41a00000 y 0x42200000
    if (joint.skill == 0 || joint.skill == OwnerColor)
        joint.width = NarrowWidth;
    else if (joint.skill == 1)
        joint.width = WideWidth;
    constexpr std::uint8_t Subtractive = 4, SecondFace = 2, SecondPass = 2;
    constexpr float Speed = 3.0f; // 0x40400000
    constexpr std::int32_t Life = 50, Points = 8;
    joint.light = glm::vec3(1.0f);
    joint.blend = Subtractive;
    joint.drawEnabled = SecondFace;
    joint.speed = Speed;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.category = SecondPass;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x537a00..0x537b31, después del movimiento común: con vida de 25 o más gira la z 10 hacia atrás y sube 2,5. Después
// acelera de a 5 hasta 30 y gira hacia el destino a esa velocidad (0x4e9810); cerca (70 o menos, o NaN), si el rumbo
// cambió más de 20 y la velocidad es de 20 o más, frena 10. Con vida de menos de 10, las tres luces son la roja por
// 0,8333333f. Siempre, la luz dinámica de radio 2 en su posición, (0,4; 1; 0,8) por (rand() % 4 + 8) · 0,03f.
Core::Expected<void, std::string> StepHelperSpark(JointState& joint, const JointKernels::StepServices& services)
{
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x537ae0: falta el RNG del joint 0x610/5"));
    constexpr std::int32_t RisingLife = 25, FadeLife = 10;
    if (joint.life >= RisingLife)
    {
        constexpr double Turn = 10.0, Rise = 2.5; // 0x6b87a8 y 0x6b89a8
        joint.angles.z = F32(D(joint.angles.z) - Turn);
        joint.position.z = F32(D(joint.position.z) + Rise);
    }
    else
    {
        constexpr double Accel = 5.0, TopSpeed = 30.0; // 0x6b87f8 y 0x6b8858
        const double faster = D(joint.speed) + Accel;
        joint.speed = F32(faster);
        if (faster >= TopSpeed) // 0x537a38: compara la suma sin redondear.
            joint.speed = static_cast<float>(TopSpeed);
        const float heading = joint.angles.z; // [ebp+8]
        const glm::vec3 target = joint.targetPosition;
        const double distance = Math::MoveToward(joint.position, joint.angles, target, joint.speed);
        constexpr double Near = 70.0, Swerve = 20.0, Brake = 10.0; // 0x6ba1b0, 0x6ba2f8 (double) y 0x6b86d0
        if (!(distance > Near) && std::abs(D(heading) - D(joint.angles.z)) > Swerve && D(joint.speed) >= Swerve)
            joint.speed = F32(D(joint.speed) - Brake);
        if (joint.life < FadeLife)
        {
            constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98
            const float faded = F32(D(joint.light.x) * Fading);
            joint.light = glm::vec3(faded);
        }
    }
    const double glow = static_cast<double>((services.random->Next() & 3) + 8) * Glimmer;
    constexpr double Red = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4, 0,4f
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0,8f
    const glm::vec3 color{F32(glow * Red), F32(glow), F32(glow * Blue)};
    constexpr std::int32_t Radius = 2;
    return World::AddDynamicLight(services.lighting, joint.position, color, Radius, "0x537b29");
}

// 0x533677..0x53368e: lo común del 0x60f (vida 100, 20 puntos y ancho 10), que el subtipo pisa.
void CreateCommonSparkle(JointState& joint)
{
    constexpr std::int32_t Life = 100, Points = 20;
    constexpr float Width = 10.0f;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.width = Width;
}

// 0x533915..0x53399b: la /0x29, vida 40, el ancho del pedido, 50 puntos, la luz (1 − a, 1 − b, 1) con a y b en
// rand() % 300 · 0,001f (a guardada en float, b no) y vz en rand() % 5 + 5. Tres rand().
void CreateFallingSparkle(JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    constexpr std::int32_t Life = 40, Points = 50, Spread = 300, Lift = 5;
    constexpr double Thousandth = Math::ThousandthAsDouble; // 0x6ba128
    joint.life = Life;
    joint.width = request.scale;
    joint.maximumCount = Points;
    const float first = F32(static_cast<double>(random.Next() % Spread) * Thousandth);
    const double second = static_cast<double>(random.Next() % Spread) * Thousandth;
    joint.light = {F32(1.0 - D(first)), F32(1.0 - second), 1.0f};
    joint.velocity.z = static_cast<float>(random.Next() % Lift + Lift);
}

// 0x5339a5..0x533a1e: la /0x2a nace (0, −15, 0) girado por sus ángulos más allá, con el ancho del pedido; el camino
// (+0x24) es esa posición, la velocidad (0, −50, 0), sin la cola común (+0x9f2) y la luz en 1. Sin rand().
void CreateSpinningSparkle(JointState& joint, const Kernel::JointRequest& request)
{
    constexpr float Start = -15.0f, Radius = -50.0f; // 0xc1700000 y 0xc2480000
    joint.velocity.y = Start;
    const auto moved = Math::OriginalVectorRotate(joint.velocity, Math::OriginalAngleMatrix(joint.angles));
    joint.width = request.scale;
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    joint.pathIncrement = joint.position;
    joint.velocity.y = Radius;
    joint.tailEnabled = false;
    joint.light = glm::vec3(1.0f);
}

// 0x539fc2..0x53a028: la /0x29 con vida de 20 o menos queda sobre el destino, sube vz (que crece de a 1) y apaga la luz
// por 0,9090909f; siempre, la luz dinámica de radio 1 con su luz en su posición.
Core::Expected<void, std::string> StepFallingSparkle(JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::int32_t RisingLife = 20;
    if (joint.life <= RisingLife)
    {
        const double rise = D(joint.velocity.z) + 1.0;
        joint.position.x = joint.targetPosition.x;
        joint.position.y = joint.targetPosition.y;
        joint.velocity.z = F32(rise);
        joint.position.z = F32(rise + D(joint.position.z));
        constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)); // 0x6ba26c
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    }
    constexpr std::int32_t Radius = 1;
    return World::AddDynamicLight(services.lighting, joint.position, joint.light, Radius, "0x53a023");
}

// 0x53a039..0x53a122: la /0x2a gira 15 su z. Con el dueño muerto (su byte vivo), apaga todos los 0x60f/0x2a de ese
// dueño (0x534f90, él incluido) y va a la cola común. Vivo: vida 100, la posición en el camino más la velocidad girada
// por los ángulos; rand() % 100 y, si la z en ftol es múltiplo de 45 y el rand() no pasa de 50, suelta una 0x60f/0x29
// en su posición, con los ángulos en 0, sin dueño y su ancho. Después, la luz dinámica de radio 2 en el destino.
Core::Expected<void, std::string> StepSpinningSparkle(JointState& joint, const JointKernels::StepServices& services)
{
    constexpr double Spin = 15.0; // 0x6b87bc
    joint.angles.z = F32(D(joint.angles.z) + Spin);
    const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x53a048: el dueño del joint 0x60f/0x2a no es un personaje"));
    if (!owner->alive)
    {
        Actors::KillJointsOf(services.joints, DropJoint, owner, SpinningSparkleSubtype);
        return {};
    }
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x53a09f: falta el RNG del joint 0x60f/0x2a"));
    constexpr std::int32_t Life = 100;
    joint.life = Life;
    const auto moved = Math::OriginalVectorRotate(joint.velocity, Math::OriginalAngleMatrix(joint.angles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.pathIncrement[axis]));
    constexpr int Chance = 100, Most = 50, Beat = 45;
    const int roll = services.random->Next() % Chance;
    const auto turn = Math::OriginalFtolLow(D(joint.angles.z));
    if (!turn)
        return Core::Unexpected(std::string("0x53a0b2: el giro del joint 0x60f/0x2a excede ftol"));
    if (*turn % Beat == 0 && roll <= Most)
    {
        glm::vec3 still(0.0f); // [ebp−0x3c]
        const Kernel::JointRequest sparkle{.type = DropJoint,
                                           .firstPosition = joint.position,
                                           .secondPosition = joint.position,
                                           .angles = still,
                                           .subtype = FallingSparkleSubtype,
                                           .ownerContext = nullptr,
                                           .scale = joint.width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = CreateFaunaJoint(services.joints, sparkle, *services.random); !created.HasValue())
            return Core::Unexpected(std::format("0x53a0eb: {}", created.Error()));
    }
    constexpr std::int32_t Radius = 2;
    return World::AddDynamicLight(services.lighting, joint.targetPosition, joint.light, Radius, "0x53a105");
}
} // namespace

bool IsSpark(std::int32_t type, std::int32_t subtype)
{
    return (type == HelperSparkJoint && subtype == HelperSparkSubtype) ||
           (type == DropJoint && (subtype == FallingSparkleSubtype || subtype == SpinningSparkleSubtype));
}

Core::Expected<void, std::string> CreateSparkJoint(JointState& joint, const Kernel::JointRequest& request,
                                                   Crt::CrtRandom& random)
{
    if (request.type == HelperSparkJoint)
        return CreateHelperSpark(joint, request);
    CreateCommonSparkle(joint);
    if (request.subtype == FallingSparkleSubtype)
        CreateFallingSparkle(joint, request, random);
    else
        CreateSpinningSparkle(joint, request);
    // 0x5342be: el destino es el segundo punto. 0x534cb6: el tope de 50 puntos no cambia nada.
    joint.targetPosition = request.secondPosition;
    return {};
}

Core::Expected<void, std::string> StepSparkJoint(JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.type == HelperSparkJoint)
        return StepHelperSpark(joint, services);
    const auto stepped = joint.subtype == FallingSparkleSubtype ? StepFallingSparkle(joint, services)
                                                                : StepSpinningSparkle(joint, services);
    if (!stepped.HasValue())
        return stepped;
    // 0x53b318..0x53b363: con menos de 10 de vida, la luz por 0,7692308f. La /0x2a llega con vida 100 (y con el dueño
    // muerto va directo a la cola, 0x53a122): nunca se apaga acá.
    constexpr std::int32_t FadeLife = 10;
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
    if (joint.subtype == FallingSparkleSubtype && joint.life < FadeLife)
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    return {};
}
} // namespace Client::Scenes::JointFamilies
