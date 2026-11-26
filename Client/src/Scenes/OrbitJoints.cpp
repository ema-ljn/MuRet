#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
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
#include <string>

// El 0x60f y el 0x62b con el subtipo 0x10, 0xe o 0xf (alta 0x533677, 0x5337fa y 0x534132; paso 0x53a5c7, 0x53b18a y
// 0x53b318). La /0x10 es la columna de la lluvia de sombras 0xbd (0x51bcb0): veinte tramos que suben de a 50, sin la
// cola común, que se corren con lo que se mueve su personaje dueño. La /0xe y la /0xf dan vueltas alrededor de su
// destino con un radio (la velocidad) que crece, en sentidos opuestos; las pide el paso del efecto 0x60f (0x52b408 y
// 0x52b42b, con él de dueño), que MuRet no crea.
namespace Client::Scenes::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

constexpr double Hundredth = Math::HundredthAsDouble; // 0x6b86c0, 0,01f

Core::Expected<void, std::string> CreateTheColumn(JointState& joint, const World::FishEffectRequest& request)
{
    // 0x5337fa..0x533855: sin la cola común, la luz en 0,1f, vida 60, el destino y el ancho; después, mientras sea
    // menor que los puntos (20), la z + 50 y un tramo con la matriz de los ángulos del alta (0x530afa, [ebp−0x54]).
    constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
    constexpr std::int32_t Life = 60;
    constexpr double Rise = 50.0; // 0x6b86b0
    joint.tailEnabled = false;
    joint.light = {Dim, Dim, Dim};
    joint.life = Life;
    joint.targetPosition = request.secondPosition;
    joint.width = request.scale;
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    for (std::int32_t point = 0; point < joint.maximumCount; ++point)
    {
        joint.position.z = F32(D(joint.position.z) + Rise);
        if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    return {};
}

void CreateTheWhirl(JointState& joint, const World::FishEffectRequest& request, Crt::CrtRandom& random)
{
    // 0x534132..0x5341f7: la mezcla 4, el ancho, la textura 0x613; vz (rand() % 150) · 0,01f, vy rand() % 500 − 250,
    // velocidad 30 y la luz (0,2f; 0,4f; 0,8f). Con el ancho de más de 10 (fcomp y test ah,0x41: ni menor, ni igual, ni
    // sin orden), vz (rand() % 250 + 150) · 0,01f. Vida 20 y la z + rand() % 25.
    constexpr std::uint8_t Blend = 4;
    constexpr std::uint16_t Texture = 0x613;
    constexpr float Speed = 30, NarrowWidth = 10;
    constexpr std::int32_t Life = 20;
    joint.blend = Blend;
    joint.width = request.scale;
    joint.texture = Texture;
    const std::int32_t lift = random.Next() % 150;
    joint.velocity.z = F32(static_cast<double>(lift) * Hundredth);
    joint.velocity.y = static_cast<float>(random.Next() % 500 - 250);
    joint.speed = Speed;
    joint.light = {std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3ecccccdu),
                   std::bit_cast<float>(0x3f4ccccdu)};
    if (joint.width > NarrowWidth)
    {
        const std::int32_t wide = random.Next() % 250 + 150;
        joint.velocity.z = F32(static_cast<double>(wide) * Hundredth);
    }
    joint.life = Life;
    const std::int32_t height = random.Next() % 25;
    joint.position.z = F32(static_cast<double>(height) + D(joint.position.z));
}

// 0x53a5c7..0x53a6ae: con el personaje dueño, lo que se movió desde el paso anterior (el destino menos su posición, en
// float) se resta de los cuatro puntos de los tramos cuenta − 1..0 y el destino pasa a ser su posición. La luz: con
// vida de menos de 20, por 0,9090909f; de menos de 40, en 0,25; si no, por 1,04f. Sin dueño, nada.
Core::Expected<void, std::string> StepTheColumn(JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return {};
    const ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(
            std::format("0x53a5d2: el dueño del joint {:#x}/0x10 no es un personaje (ActorState)", joint.type));
    glm::vec3 moved{};
    for (int axis = 0; axis < 3; ++axis)
        moved[axis] = F32(D(joint.targetPosition[axis]) - D(owner->position[axis]));
    joint.targetPosition = owner->position;
    if (joint.count > static_cast<std::int32_t>(joint.points.size()))
        return Core::Unexpected(std::string("0x53a603: la cola del joint excede sus puntos canónicos"));
    for (std::int32_t segment = joint.count - 1; segment >= 0; --segment)
        for (glm::vec3& point : joint.points[static_cast<std::size_t>(segment)])
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = F32(D(point[axis]) - D(moved[axis]));
    constexpr std::int32_t DimLife = 20, FlatLife = 40;
    constexpr double Dimming = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));     // 0x6ba26c
    constexpr double Brightening = static_cast<double>(std::bit_cast<float>(0x3f851eb8u)); // 0x6ba2dc
    constexpr float Flat = 0.25f;                                                          // 0x3e800000
    if (joint.life < DimLife)
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Dimming);
    else if (joint.life < FlatLife)
        joint.light = {Flat, Flat, Flat};
    else
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Brightening);
    return {};
}

// 0x53b18a..0x53b234: con dueño (que no lee), el giro a = vy · 0,1f (la /0xf, por −1) retenido en la x87: la x es
// FCOS(a) · velocidad + la x del destino y la y, la y del destino − FSIN(a) · velocidad; la z + vz. Con vida de menos
// de 20, la velocidad + 5; el ancho + 1, vy + 4 y vz + (rand() % 200 + 200) · 0,01f. Sin dueño, nada.
Core::Expected<void, std::string> StepTheWhirl(JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.ownerContext == nullptr)
        return {};
    if (services.random == nullptr)
        return Core::Unexpected(std::format("0x53b20d: falta el RNG del joint {:#x}/{:#x}", joint.type, joint.subtype));
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    constexpr double Mirror = -1.0;               // 0x6b8b9c
    double argument = D(joint.velocity.y) * Tenth;
    if (joint.subtype == MirroredWhirlSubtype)
        argument = argument * Mirror;
    const auto cosine = Math::Detail::CosineProductPC53(argument, joint.speed);
    const auto sine = Math::Detail::SineProductPC53(argument, joint.speed);
    if (!cosine || !sine)
        return Core::Unexpected(std::format("0x53b1ae: el giro del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    joint.position.x = F32(*cosine + D(joint.targetPosition.x));
    joint.position.y = F32(D(joint.targetPosition.y) - *sine);
    joint.position.z = F32(D(joint.velocity.z) + D(joint.position.z));
    constexpr std::int32_t FastLife = 20;
    if (joint.life < FastLife)
        joint.speed = F32(D(joint.speed) + 5.0);
    joint.width = F32(D(joint.width) + 1.0);
    joint.velocity.y = F32(D(joint.velocity.y) + 4.0);
    const std::int32_t lift = services.random->Next() % 200 + 200;
    joint.velocity.z = F32(static_cast<double>(lift) * Hundredth + D(joint.velocity.z));
    return {};
}
} // namespace

bool IsOrbit(std::int32_t type, std::int32_t subtype)
{
    return (type == DropJoint || type == SecondHelixJoint) &&
           (subtype == ColumnSubtype || subtype == WhirlSubtype || subtype == MirroredWhirlSubtype);
}

Core::Expected<void, std::string> CreateOrbitJoint(JointState& joint, const World::FishEffectRequest& request,
                                                   Crt::CrtRandom& random)
{
    // 0x533677..0x53368e: lo común del 0x60f (vida 100, 20 puntos y ancho 10), que el subtipo pisa.
    constexpr std::int32_t Life = 100, Points = 20;
    constexpr float Width = 10;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.width = Width;
    if (joint.subtype == ColumnSubtype)
    {
        if (auto created = CreateTheColumn(joint, request); !created.HasValue())
            return created;
    }
    else
        CreateTheWhirl(joint, request, random);
    // 0x5342be: el destino es el segundo punto. 0x534cb6: el tope de 50 puntos no cambia los 20.
    joint.targetPosition = request.secondPosition;
    return {};
}

Core::Expected<void, std::string> StepOrbitJoint(JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.subtype == ColumnSubtype)
    {
        if (auto stepped = StepTheColumn(joint); !stepped.HasValue())
            return stepped;
    }
    else if (auto stepped = StepTheWhirl(joint, services); !stepped.HasValue())
        return stepped;
    // 0x53b318..0x53b363: con vida de menos de 10, la luz por 0,7692308f (0x6ba240).
    constexpr std::int32_t FadeLife = 10;
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
    if (joint.life < FadeLife)
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    return {};
}
} // namespace Client::Scenes::JointFamilies
