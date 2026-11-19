#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

// El 0x61c/0 (alta 0x5342e4..0x534399, paso 0x53b3e1..0x53b623): el barrido que pide el paso del efecto 0x5be
// (0x52a72a, con el personaje dueño del efecto, ancho 150 y la skill y la clave del efecto), que MuRet no crea
// (0x4f9fae). Gira a 180 de su destino (la posición de partida) bajando la z de su giro 11 por tramo, ocho tramos por
// paso, y suelta en cada uno dos rayos 0x614/3 y a veces la partícula 0x5d9. Sin la cola común ni el movimiento común
// (0x53585e).
namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;

constexpr float Reach = std::bit_cast<float>(0xc3340000u); // −180

// 0x53b4c2..0x53b53b: 0x530860(0x614, partida, posición, ángulos, 3, 0, escala, 5, 0xa, 0, −1).
Core::Expected<void, std::string> Spark(const JointKernels::StepServices& services, JointState& joint, glm::vec3& from,
                                        float scale)
{
    constexpr std::int32_t SparkJoint = 0x614, SparkSubtype = 3, SparkKey = 5, SparkSkill = 0xa;
    const World::FishEffectRequest spark{.type = SparkJoint,
                                         .firstPosition = from,
                                         .secondPosition = joint.position,
                                         .angles = joint.angles,
                                         .subtype = SparkSubtype,
                                         .ownerContext = nullptr,
                                         .scale = scale,
                                         .key = SparkKey,
                                         .skill = SparkSkill,
                                         .skillSerial = 0,
                                         .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, spark, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x53b4f6: {}", created.Error()));
    return {};
}

// 0x53b400..0x53b53b: un tramo: la posición es el destino más (0, −180, 0) girado por la velocidad (los ángulos del
// barrido), un tramo con la matriz de los ángulos, vz − 11; con rand() & 1 en 0 la partícula 0x5d9 (luz del joint,
// escala 1) y los dos 0x614/3 desde la posición de antes, de escala rand() % 10 + 5 y (rand() & 7) + 4.
Core::Expected<void, std::string> Sweep(JointState& joint, const JointKernels::StepServices& services)
{
    glm::vec3 from = joint.position;
    const auto swing =
        Render::Models::OriginalVectorRotate({0, Reach, 0}, Render::Models::OriginalAngleMatrix(joint.velocity));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(swing[axis]) + D(joint.targetPosition[axis]));
    if (auto appended = JointKernels::AppendJointTail(joint, Render::Models::OriginalAngleMatrix(joint.angles));
        !appended.HasValue())
        return appended;
    constexpr double Turn = 11.0; // 0x6b8bb4
    joint.velocity.z = F32(D(joint.velocity.z) - Turn);
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x53b493: falta el RNG del joint 0x61c/0"));
    if ((services.random->Next() & 1) == 0)
    {
        constexpr std::int32_t Fire = 0x5d9;
        if (services.particles == nullptr)
            return Core::Unexpected(std::string("0x53b4ba: faltan las partículas del joint 0x61c/0"));
        const auto made = services.particles->CreateChecked(Fire, joint.position, joint.angles, joint.light, 0, 1.0f,
                                                            services.worldClock, *services.random);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x53b4ba: falló la partícula 0x5d9 del joint 0x61c/0 ({})",
                                                static_cast<int>(made.Error())));
    }
    constexpr double SmallBase = 5.0, LargeBase = 4.0; // 0x6b87f8 y 0x6b8a44
    const std::int32_t first = services.random->Next() % 10;
    if (auto spark = Spark(services, joint, from, F32(static_cast<double>(first) + SmallBase)); !spark.HasValue())
        return spark;
    const std::int32_t second = services.random->Next() & 7;
    return Spark(services, joint, from, F32(static_cast<double>(second) + LargeBase));
}
} // namespace

void CreateSweepJoint(JointState& joint, const World::FishEffectRequest& request)
{
    // 0x5342e4..0x534399: sin la cola común, velocidad 0, el ancho, la z de los ángulos + 30, el destino en la posición
    // de partida, vida 20, 18 puntos, la velocidad (+0x9dc) con los ángulos nuevos, la cuenta en −1; la posición, 180
    // hacia −y girado por esos ángulos, y la luz blanca.
    constexpr double Tilt = 30.0; // 0x6b8858
    constexpr std::int32_t Life = 20, Points = 18;
    joint.tailEnabled = false;
    joint.speed = 0;
    joint.width = request.scale;
    joint.angles.z = F32(D(joint.angles.z) + Tilt);
    joint.targetPosition = joint.position;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.velocity = joint.angles;
    joint.count = -1;
    const auto reach =
        Render::Models::OriginalVectorRotate({0, Reach, 0}, Render::Models::OriginalAngleMatrix(joint.angles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(reach[axis]) + D(joint.position[axis]));
    joint.light = {1, 1, 1};
}

Core::Expected<void, std::string> StepSweepJoint(JointState& joint, const JointKernels::StepServices& services)
{
    // 0x53b3f1..0x53b5c3: ocho vueltas; cada una agrega un tramo mientras la cuenta sea menor que los puntos − 1. Con
    // el dueño en el héroe ([0x743a170]), vida de más de 18 y la vuelta múltiplo de 5, el asedio (0x4294b0, 0x5f82c0) o
    // los blancos de la skill (0x517a20, radio 150): MuRet no tiene al héroe en el paso, y corta si hay dueño.
    constexpr int Turns = 8, HitEvery = 5;
    constexpr std::int32_t HitLife = 18;
    for (int turn = 0; turn < Turns; ++turn)
    {
        if (joint.count < joint.maximumCount - 1)
            if (auto swept = Sweep(joint, services); !swept.HasValue())
                return swept;
        if (joint.ownerContext != nullptr && joint.life > HitLife && turn % HitEvery == 0)
            return Core::Unexpected(std::string("0x53b548: el dueño del joint 0x61c/0 frente al héroe ([0x743a170]), "
                                                "el asedio y los blancos de 0x517a20 no están portados (CL11)"));
    }
    // 0x53b5ec..0x53b620: con vida de menos de 10, la x de la luz por 0,6666667f en los tres canales; si no, la vida
    // por 0,033333335f.
    constexpr std::int32_t DimLife = 10;
    constexpr double Dimming = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)); // 0x6b8c64
    constexpr double Glow = static_cast<double>(std::bit_cast<float>(0x3d088889u));    // 0x6ba29c
    const float light =
        joint.life < DimLife ? F32(D(joint.light.x) * Dimming) : F32(static_cast<double>(joint.life) * Glow);
    joint.light = {light, light, light};
    return {};
}
} // namespace Client::Scenes::JointFamilies
