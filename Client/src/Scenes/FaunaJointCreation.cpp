#include "Scenes/FaunaJointCreation.h"
#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"
#include "World/Ftol.h"
#include "World/IFishStepServices.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/OriginalAngles.h"
#include "World/KalimaParticleKernels.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include <cmath>
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/HelperTable.h"
#include "World/WorldObjects.h"
#include "World/WorldObjectStepServices.h"
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
void Prefix(JointState& joint, const World::FishEffectRequest& request, JointOwnerKind ownerKind)
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
    joint.ownerKind = ownerKind;
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

// 0x5309d6 y 0x531691..0x53180e: el 614/10 de la espada 0x224 (0x50ee26). Velocidad 50, el ancho, la textura que se
// repite (+0x9f0), vida 0, los ángulos en 0, la luz (0,3; 0,3; 1), 10 puntos y el décimo de lo que falta hasta el
// segundo punto. Nueve tramos: cada uno corre el avance (rand() & 7) − 4 en x e y (con dueño, (rand() & 15) − 8),
// agrega la cola con la matriz de los ángulos en 0 y deshace la corrida. Al final, la posición es el destino (+0x58).
Core::Expected<void, std::string> CreateSwordBeam(JointState& joint, const World::FishEffectRequest& request,
                                                  Render::Models::CrtRandom& random)
{
    constexpr float Speed = 50;
    constexpr std::int32_t Points = 10;
    joint.speed = Speed;
    joint.width = request.scale;
    joint.repeatedTexture = true;
    joint.life = 0;
    joint.angles = {};
    joint.light = {0.3f, 0.3f, 1};
    joint.maximumCount = Points;
    for (int axis = 0; axis < 3; ++axis)
        joint.pathIncrement[axis] = static_cast<float>(
            (static_cast<double>(request.secondPosition[axis]) - static_cast<double>(joint.position[axis])) *
            static_cast<double>(0.1f));
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    const bool owned = joint.ownerContext != nullptr;
    const int mask = owned ? 15 : 7;
    const double center = owned ? 8.0 : 4.0; // 0x6b86c4 y 0x6b8a44.
    for (std::int32_t sample = 0; sample < joint.maximumCount - 1; ++sample)
    {
        const auto dx = static_cast<float>(static_cast<double>(random.Next() & mask) +
                                           static_cast<double>(joint.pathIncrement.x) - center);
        const auto dy = static_cast<float>(static_cast<double>(random.Next() & mask) +
                                           static_cast<double>(joint.pathIncrement.y) - center);
        joint.position.x = static_cast<float>(static_cast<double>(dx) + static_cast<double>(joint.position.x));
        joint.position.y = static_cast<float>(static_cast<double>(dy) + static_cast<double>(joint.position.y));
        joint.position.z =
            static_cast<float>(static_cast<double>(joint.pathIncrement.z) + static_cast<double>(joint.position.z));
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        joint.position.x = static_cast<float>(static_cast<double>(joint.position.x) - static_cast<double>(dx) +
                                              static_cast<double>(joint.pathIncrement.x));
        joint.position.y = static_cast<float>(static_cast<double>(joint.position.y) - static_cast<double>(dy) +
                                              static_cast<double>(joint.pathIncrement.y));
    }
    joint.position = joint.targetPosition;
    return {};
}

// 0x532125, 0x5321e0..0x53220b, 0x5323ba y 0x534cb6: el 618/0x11 del bastón 0xc1e (0x50b4e7), con el ActorState del
// personaje como dueño (+0x54). El ancho, velocidad 0, vida 12, la posición en +0x40 del dueño y la luz (0,8; 0,2;
// 1). No escribe +0x6c: los puntos quedan los del uso anterior del lugar, a lo sumo 50 (Q-C361).
Core::Expected<void, std::string> CreateStaffBeam(JointState& joint, const World::FishEffectRequest& request)
{
    constexpr std::int32_t StaffBeamLife = 12;
    constexpr std::int32_t MostPoints = 50;
    const auto* owner = static_cast<const ActorState*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x5321ea: falta el personaje dueño del joint 0x618/0x11"));
    joint.width = request.scale;
    joint.speed = 0;
    joint.life = StaffBeamLife;
    joint.position = owner->effectOrigin;
    joint.light = {0.8f, 0.2f, 1};
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

// 0x532125..0x532155, 0x5322df (/0) y 0x5322f8 (/1): el 618/0 y el 618/1 del que mató (0x505169, 0x5051a4), con el
// ActorState del que pega como dueño (+0x54). El ancho del pedido, velocidad 3, vida 120, 8 puntos y la luz
// (0,4; 0,3; 0,2) en la /0 y (0,1; 0,1; 0,5) en la /1. El destino (+0x58) queda el del uso anterior: con dueño, el
// prefijo no lo escribe. Sin rand().
void CreateKillBeam(JointState& joint, const World::FishEffectRequest& request)
{
    constexpr float KillBeamSpeed = 3.0f;
    constexpr std::int32_t KillBeamLife = 120, KillBeamPoints = 8;
    joint.width = request.scale;
    joint.speed = KillBeamSpeed;
    joint.life = KillBeamLife;
    joint.maximumCount = KillBeamPoints;
    joint.light = request.subtype == 0 ? glm::vec3(std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au),
                                                   std::bit_cast<float>(0x3e4ccccdu))
                                       : glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu),
                                                   std::bit_cast<float>(0x3f000000u));
}

// 0x531c43..0x531cc7, 0x531d24..0x531dcb y 0x532003: el 610/4 de la espada 0x223 (0x50ec26), con el ActorState del
// personaje como dueño (+0x54). Lo común del 0x610 pone la vida en la key, 50 puntos y la key en 0, y el color del
// skill (+0x9ea) elige el ancho y la luz; el caso 4 pisa todo salvo la key. Mezcla 3, los ángulos en 0, vida 0, el
// ancho, la luz en 1, 10 puntos y el décimo de lo que falta hasta el segundo punto (+0x24). Diez tramos, cada uno con
// el avance sumado y la cola con la matriz de los ángulos en 0, y el destino (+0x58) en el segundo punto. Sin rand().
// Con vida 0, vive un paso (Q-C365).
Core::Expected<void, std::string> CreateSwordTrail(JointState& joint, const World::FishEffectRequest& request)
{
    constexpr std::int32_t Points = 10;
    constexpr std::uint8_t OwnerColor = 3;
    // 0x531c71..0x531c8c: el color 3 lee la luz del dueño (+0xf4), que el caso 4 pisa; sin dueño, el original cae.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/4"));
    joint.key = 0;
    joint.blend = 3;
    joint.angles = {};
    joint.life = 0;
    joint.width = request.scale;
    joint.light = {1, 1, 1};
    joint.maximumCount = Points;
    for (int axis = 0; axis < 3; ++axis)
        joint.pathIncrement[axis] = static_cast<float>(
            (static_cast<double>(request.secondPosition[axis]) - static_cast<double>(joint.position[axis])) *
            static_cast<double>(0.1f));
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    for (std::int32_t sample = 0; sample < joint.maximumCount; ++sample)
    {
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = static_cast<float>(static_cast<double>(joint.position[axis]) +
                                                      static_cast<double>(joint.pathIncrement[axis]));
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x537d49..0x537ee1: el paso del 610/4. Con el personaje muerto, vida −1 y la posición 50 sobre el destino. Vivo, el
// destino va a +0x17c del personaje y se corre tres veces un tercio de lo que le falta hasta su posición (+0x10); cada
// vez la posición es el destino más la velocidad girada por los ángulos de la cabeza (+0x28..+0x30), y la cola se
// agrega, con la matriz de los ángulos del joint, solo si todavía no está llena o hay skill. Con la vida debajo de 15
// la luz baja a dos tercios. Las dos ramas siguen con la cola y la vida comunes.
Core::Expected<void, std::string> StepSwordTrail(JointState& joint, std::size_t index)
{
    constexpr std::int32_t Steps = 3;
    constexpr std::int32_t FadingLife = 15;
    constexpr double Third = static_cast<double>(std::bit_cast<float>(0x3eaaaaabu)); // 0x6ba2a0.
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu));  // 0x6b8c64.
    constexpr double Above = 50.0;                                                   // 0x6b86b0.
    const auto* actor = static_cast<const ActorState*>(joint.ownerContext);
    if (actor == nullptr)
        return Core::Unexpected(std::format("0x537d49: falta el personaje dueño del joint {} (0x610/4)", index));
    if (!actor->alive)
    {
        joint.life = -1;
        joint.position = {joint.targetPosition.x, joint.targetPosition.y,
                          static_cast<float>(static_cast<double>(joint.targetPosition.z) + Above)};
        return {};
    }
    if (joint.velocity != glm::vec3(0.0f))
        return Core::Unexpected(
            std::format("0x537e3c: el joint {} (0x610/4) tiene velocidad y su giro pide +0x30 del personaje", index));
    glm::vec3 delta{};
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = static_cast<float>(static_cast<double>(actor->position[axis]) -
                                         static_cast<double>(actor->trailOrigin[axis]));
    joint.targetPosition = actor->trailOrigin;
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = static_cast<float>(static_cast<double>(delta[axis]) * Third);
    for (std::int32_t step = 0; step < Steps; ++step)
    {
        for (int axis = 0; axis < 3; ++axis)
            joint.targetPosition[axis] =
                static_cast<float>(static_cast<double>(delta[axis]) + static_cast<double>(joint.targetPosition[axis]));
        // 0x537e2b..0x537e65: la velocidad (+0x9dc) del 610/4 queda en el 0 del prefijo, así que el giro por los
        // ángulos de la cabeza (+0x30 no está en MuRet) solo da ceros, y la posición es el destino (Q-C364).
        joint.position = joint.targetPosition;
        const auto last = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.maximumCount) - 1u);
        if (joint.count < last || joint.skill != 0)
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
                return appended;
    }
    if (joint.life < FadingLife)
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = static_cast<float>(static_cast<double>(joint.light[axis]) * Fade);
    return {};
}

// 0x5309d6 y 0x531563..0x53168c, 0x53463b: el 614/6 de los objetos de Icarus (0x6063d0). Velocidad 50, el
// ancho y 50 puntos como todo 614; sin cola común, vida rand() % 20 + 6, velocidad rand() % 10 + 15 y el
// gris (rand() % 10) · 0,06666667f + 0,1f, redondeado una vez. +0x24 guarda la partida. Tres rand().
Core::Expected<void, std::string> CreateObjectBolt(JointState& joint, const World::FishEffectRequest& request,
                                                   Render::Models::CrtRandom& random)
{
    constexpr float SharedSpeed = 50;
    constexpr std::int32_t Points = 50;
    joint.speed = SharedSpeed;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.tailEnabled = false;
    constexpr int LifeSpan = 20, LifeBase = 6, SpeedSpan = 10, GraySpan = 10;
    constexpr double SpeedBase = 15;
    constexpr double GrayStep = static_cast<double>(std::bit_cast<float>(0x3d888889u));
    constexpr double GrayBase = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    joint.life = random.Next() % LifeSpan + LifeBase;
    joint.speed = static_cast<float>(static_cast<double>(random.Next() % SpeedSpan) + SpeedBase);
    const auto gray = static_cast<float>(static_cast<double>(random.Next() % GraySpan) * GrayStep + GrayBase);
    joint.light = glm::vec3(gray);
    joint.pathIncrement = joint.position;
    return {};
}

// 0x5319ea..0x531a30 y 0x533242: el 615/0 del cielo de Icarus (0x605d70). Textura 0x614, el ancho, 50
// puntos, vida 20 (0x531a12 guarda el divisor; el resto sólo da la velocidad rand() % 20 + 80) y la z de la
// luz en 50. +0x24 guarda la partida. Un rand().
Core::Expected<void, std::string> CreateSkyBolt(JointState& joint, const World::FishEffectRequest& request,
                                                Render::Models::CrtRandom& random)
{
    constexpr std::uint16_t BoltTexture = 0x614;
    constexpr std::int32_t Points = 50;
    constexpr int Divisor = 20;
    constexpr double SpeedBase = 80;
    joint.texture = BoltTexture;
    joint.width = request.scale;
    joint.maximumCount = Points;
    const int speed = random.Next() % Divisor;
    joint.life = Divisor;
    joint.speed = static_cast<float>(static_cast<double>(speed) + SpeedBase);
    joint.light.z = static_cast<float>(joint.maximumCount);
    joint.pathIncrement = joint.position;
    return {};
}

// 0x5319ea, 0x531b0c..0x531bd2 y 0x534cb6: el 615/2 de la trampa de Chaos Castle (0x405d49). Textura 0x614, el
// ancho, 50 puntos y vida 20; la x y la y se corren rand() % 10 − 5 (fild y fadd, guardado en float) y la z sube
// 800 (0x6b8ac8). Ángulos en cero, el destino es el segundo punto y +0x24 guarda la partida ya corrida. Dos
// rand().
Core::Expected<void, std::string> CreateTrapBolt(JointState& joint, const World::FishEffectRequest& request,
                                                 Render::Models::CrtRandom& random)
{
    constexpr std::uint16_t BoltTexture = 0x614;
    constexpr std::int32_t Points = 50;
    constexpr std::int32_t Life = 20;
    constexpr int JitterSpan = 10, JitterCenter = 5;
    constexpr double Rise = static_cast<double>(std::bit_cast<float>(0x44480000u));
    joint.texture = BoltTexture;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.life = Life;
    joint.position.x =
        static_cast<float>(static_cast<double>(random.Next() % JitterSpan - JitterCenter) + joint.position.x);
    joint.position.y =
        static_cast<float>(static_cast<double>(random.Next() % JitterSpan - JitterCenter) + joint.position.y);
    joint.position.z = static_cast<float>(static_cast<double>(joint.position.z) + Rise);
    joint.angles = {};
    joint.targetPosition = request.secondPosition;
    joint.pathIncrement = joint.position;
    return {};
}

// 0x53265e (+0x10 = 3 en todo 613 y 6ee), 0x5333f6..0x533449 y 0x5331a9: el meteoro 6ee/9 del preludio de
// Chaos Castle (0x4054ad). Resta, la segunda cara sola, el ancho, velocidad 10, luz blanca, ángulos (−90, 0, 0), el
// desvío en cero, vida 20 y 15 puntos; +0x24 guarda la partida. Sin rand().
void CreateMeteor(JointState& joint, const World::FishEffectRequest& request)
{
    constexpr std::uint8_t Subtractive = 3, SecondFace = 2;
    constexpr float Speed = 10, Pitch = -90;
    constexpr std::int32_t Life = 20, Points = 15;
    joint.blend = Subtractive;
    joint.drawEnabled = SecondFace;
    joint.width = request.scale;
    joint.speed = Speed;
    joint.light = {1, 1, 1};
    joint.angles = {Pitch, 0, 0};
    joint.velocity = {};
    joint.life = Life;
    joint.maximumCount = Points;
    joint.pathIncrement = joint.position;
}

// Los joints del conjunto completo (0x50ba30, 0x5102c3..0x5104cb) y el del 5ac/3 que suelta (0x52aa5d), con el
// ActorState del personaje como dueño (+0x54).
constexpr std::int32_t SparkleJoint = 0x60f;
constexpr std::int32_t SparkleSubtype = 0, FallenSparkleSubtype = 1, WideSparkleSubtype = 0x12;
constexpr std::int32_t SetTrailJoint = 0x610, SetTrailSubtype = 7;
constexpr std::int32_t EnergyJoint = 0x619, SetEnergySubtype = 9;
// El 619/1 que suelta el calco 0x61e/1 de la mascota (0x518900), con el héroe de dueño (+0x54).
constexpr std::int32_t SummonEnergySubtype = 1;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

// +0xf4 del dueño del 60f, la luz propia: la del personaje o la del caballo (0x443d70 la pone en 3).
const glm::vec3* SparkleOwnerLight(const JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return nullptr;
    if (joint.ownerKind == JointOwnerKind::Helper)
        return &static_cast<const HelperState*>(joint.ownerContext)->light;
    if (joint.ownerKind == JointOwnerKind::Raven)
        return &static_cast<const DarkSpirit*>(joint.ownerContext)->light;
    return &static_cast<const ActorState*>(joint.ownerContext)->light;
}

// +0x111 del dueño del 60f, la acción: la del personaje o la del caballo.
std::optional<std::uint8_t> SparkleOwnerAction(const JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return std::nullopt;
    if (joint.ownerKind == JointOwnerKind::Helper)
        return static_cast<const HelperState*>(joint.ownerContext)->horse.animation.action;
    if (joint.ownerKind == JointOwnerKind::Raven)
        return static_cast<const DarkSpirit*>(joint.ownerContext)->animation.action;
    return static_cast<const ActorState*>(joint.ownerContext)->animation.action;
}

void FadeJointLight(JointState& joint, std::uint32_t factorBits)
{
    const double factor = D(std::bit_cast<float>(factorBits));
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * factor);
}

// 0x533677..0x53369e, 0x5341fc..0x5342d2 y 0x534cb6: el 60f/0 (0x51037c) y el 60f/0x12 (0x5103be, 0x5103db, 0x510435 y
// 0x510452) del conjunto completo. Vida 100, 20 puntos y el ancho (el 10 de 0x53368e lo pisa); la /0x12 resta (mezcla
// 4). La z de la velocidad (rand() % 150) · 0,01f, la y rand() % 500 − 250 y velocidad 40; con el ancho por encima de
// 10, vida 50 y otra z, (rand() % 250 + 200) · 0,01f. La luz es la del dueño (+0xf4: la del personaje o, con los
// del caballo oscuro, la del caballo) y el destino el segundo punto. Dos rand(), o tres con el ancho.
Core::Expected<void, std::string> CreateSparkle(JointState& joint, const World::FishEffectRequest& request,
                                                Render::Models::CrtRandom& random)
{
    constexpr std::int32_t SparkleLife = 100, WideLife = 50, Points = 20;
    constexpr float FirstWidth = 10, SparkleSpeed = 40;
    constexpr std::uint8_t Subtractive = 4;
    constexpr int RiseSpan = 150, DriftSpan = 500, DriftCenter = 250, WideRiseSpan = 250, WideRiseBase = 200;
    constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b86c0, 0,01f
    const glm::vec3* ownerLight = SparkleOwnerLight(joint);
    if (ownerLight == nullptr)
        return Core::Unexpected(std::format("0x53429b: falta el dueño del joint 0x60f/{:#x}", request.subtype));
    joint.life = SparkleLife;
    joint.maximumCount = Points;
    joint.width = FirstWidth;
    if (request.subtype == WideSparkleSubtype)
        joint.blend = Subtractive;
    joint.width = request.scale;
    joint.velocity.z = F32(static_cast<double>(random.Next() % RiseSpan) * Hundredth);
    joint.velocity.y = static_cast<float>(random.Next() % DriftSpan - DriftCenter);
    joint.speed = SparkleSpeed;
    // 0x534252..0x53426a: fcomp con 10 y test ah,0x41: sólo por encima de 10 (NaN tampoco).
    if (joint.width > FirstWidth)
    {
        joint.life = WideLife;
        joint.velocity.z = F32(static_cast<double>(random.Next() % WideRiseSpan + WideRiseBase) * Hundredth);
    }
    joint.light = *ownerLight;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x531c43..0x531cc7, 0x531e2c..0x531ec7, 0x532003 y 0x534cb6: el 610/7 del conjunto completo con +13 (0x5104a8). Lo
// común del 0x610 pone la vida en la key, 50 puntos y la key en 0; el ancho y la luz del color del skill (+0x9ea) los
// pisa el caso 7: la luz en 1, vy y vz en 0, resta (mezcla 4), velocidad 10, la textura 0x60f, el ancho, 16 puntos y vx
// 15. La x y la y se corren 40 desde el segundo punto con fcos y fsin de rand() % 360 sin pasar a radianes (Q-C384): la
// x más el coseno y la y menos el seno. +0x24 guarda la partida y +0x58 el segundo punto. Dos rand().
Core::Expected<void, std::string> CreateSetTrail(JointState& joint, const World::FishEffectRequest& request,
                                                 Render::Models::CrtRandom& random)
{
    constexpr std::uint8_t OwnerColor = 3;
    constexpr std::int32_t SharedPoints = 50, Points = 16;
    constexpr std::uint8_t Subtractive = 4;
    constexpr float Speed = 10, Sway = 15, Reach = 40; // 0x6b8bf4
    constexpr std::uint16_t FlareTexture = 0x60f;
    constexpr int FullTurn = 360;
    // 0x531c71..0x531c8c: el color 3 lee la luz del dueño (+0xf4), que el caso 7 pisa; sin dueño, el original cae.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/7"));
    joint.life = joint.key;
    joint.key = 0;
    joint.maximumCount = SharedPoints;
    joint.light = {1, 1, 1};
    joint.velocity.y = 0;
    joint.velocity.z = 0;
    joint.blend = Subtractive;
    joint.speed = Speed;
    joint.texture = FlareTexture;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.velocity.x = Sway;
    const auto cosine = Render::Models::Detail::CosineProductPC53(static_cast<double>(random.Next() % FullTurn), Reach);
    if (!cosine)
        return Core::Unexpected(std::string("0x531e89: el coseno del joint 0x610/7 excede el perfil matemático"));
    joint.position.x = F32(*cosine + D(request.secondPosition.x));
    const auto sine = Render::Models::Detail::SineProductPC53(static_cast<double>(random.Next() % FullTurn), Reach);
    if (!sine)
        return Core::Unexpected(std::string("0x531ea8: el seno del joint 0x610/7 excede el perfil matemático"));
    joint.position.y = F32(D(request.secondPosition.y) - *sine);
    joint.pathIncrement = joint.position;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x5323d3..0x5323f8, 0x53254f..0x5325c2 y 0x534cb6: el 619/9 del 5ac/3 del conjunto completo (0x52aa5d). Lo común del
// 0x619 (velocidad 0, vida 12, el ancho y 2 puntos) y el caso 9: vida 90, 20 puntos, la cola en 0, +0x9ec en ftol del
// giro z, vx 50, la luz (0,09090909f; 0,04545455f; 0,09090909f) y el destino y la posición en la del dueño (+0x10). Sin
// rand().
Core::Expected<void, std::string> CreateSetEnergy(JointState& joint, const World::FishEffectRequest& request)
{
    constexpr std::int32_t SharedLife = 12, SharedPoints = 2, EnergyLife = 90, Points = 20;
    constexpr float Orbit = 50;
    constexpr float Pale = std::bit_cast<float>(0x3dba2e8cu), Dim = std::bit_cast<float>(0x3d3a2e8cu);
    const auto* owner = static_cast<const ActorState*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x5325a2: falta el personaje dueño del joint 0x619/9"));
    joint.speed = 0;
    joint.life = SharedLife;
    joint.width = request.scale;
    joint.maximumCount = SharedPoints;
    joint.life = EnergyLife;
    joint.maximumCount = Points;
    joint.count = 0;
    joint.field9ec = World::FtolLow(D(joint.angles.z));
    joint.velocity.x = Orbit;
    joint.light = {Pale, Dim, Pale};
    joint.targetPosition = owner->position;
    joint.position = owner->position;
    return {};
}

// 0x5323d3..0x5323f8, 0x532552..0x5325ca y 0x534cb6: el 619/1 del calco de la mascota (0x518900). Lo común del 0x619
// (velocidad 0, vida 12, el ancho y 2 puntos); el subtipo 1 no tiene caso. Sin rand().
void CreateSummonEnergy(JointState& joint, const World::FishEffectRequest& request)
{
    constexpr std::int32_t SharedLife = 12, SharedPoints = 2;
    joint.speed = 0;
    joint.life = SharedLife;
    joint.width = request.scale;
    joint.maximumCount = SharedPoints;
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
    if (request.type == 0x614 && request.subtype == 6)
        return CreateObjectBolt(joint, request, random);
    if (request.type == 0x614 && request.subtype == 10)
        return CreateSwordBeam(joint, request, random);
    if (request.type == 0x615 && request.subtype == 0)
        return CreateSkyBolt(joint, request, random);
    if (request.type == 0x615 && request.subtype == 2)
        return CreateTrapBolt(joint, request, random);
    if (request.type == 0x6ee && request.subtype == 9)
    {
        CreateMeteor(joint, request);
        return {};
    }
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
    if (request.type == 0x618 && request.subtype == 0x11)
        return CreateStaffBeam(joint, request);
    if (request.type == KillBeamJoint && (request.subtype == 0 || request.subtype == 1))
    {
        CreateKillBeam(joint, request);
        return {};
    }
    if (request.type == 0x610 && request.subtype == 4)
        return CreateSwordTrail(joint, request);
    if (request.type == SparkleJoint && (request.subtype == SparkleSubtype || request.subtype == WideSparkleSubtype))
        return CreateSparkle(joint, request, random);
    if (request.type == SetTrailJoint && request.subtype == SetTrailSubtype)
        return CreateSetTrail(joint, request, random);
    if (request.type == EnergyJoint && request.subtype == SetEnergySubtype)
        return CreateSetEnergy(joint, request);
    if (request.type == EnergyJoint && request.subtype == SummonEnergySubtype)
    {
        CreateSummonEnergy(joint, request);
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
                                                                         Render::Models::CrtRandom& random,
                                                                         JointOwnerKind ownerKind)
{
    // Lo que lee MuRet de un ayudante o del cuervo de dueño: sólo la luz y la acción del 60f/0 y del 60f/0x12.
    const bool helperSparkle =
        request.type == SparkleJoint && (request.subtype == SparkleSubtype || request.subtype == WideSparkleSubtype);
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        if (joints[i].alive)
            continue;
        if (ownerKind != JointOwnerKind::ByType && !helperSparkle)
            return Core::Unexpected(std::format("0x530860: el joint {:#x}/{} con un ayudante o el cuervo de dueño no "
                                                "está portado",
                                                request.type, request.subtype));
        Prefix(joints[i], request, ownerKind);
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

// 4e9810(posición, ángulos, destino, giro): los cuadrados X/Y y delta Z se derraman antes de las dos raíces.
float SteerJoint(JointState& joint, const glm::vec3& target, float turnStep)
{
    using Render::Models::CreateAngle;
    using Render::Models::TurnAngle;
    constexpr double FullTurn = 360;
    const auto yaw = static_cast<float>(CreateAngle(joint.position.x, joint.position.y, target.x, target.y));
    joint.angles.z = static_cast<float>(TurnAngle(joint.angles.z, yaw, turnStep));
    const double x = static_cast<double>(joint.position.x) - target.x;
    const double y = static_cast<double>(joint.position.y) - target.y;
    const auto z = static_cast<float>(static_cast<double>(joint.position.z) - target.z);
    const auto xSquared = static_cast<float>(x * x);
    const auto ySquared = static_cast<float>(y * y);
    const auto horizontal = static_cast<float>(std::sqrt(static_cast<double>(xSquared) + ySquared));
    const auto pitch = static_cast<float>(FullTurn - CreateAngle(joint.position.z, horizontal, target.z, 0));
    joint.angles.x = static_cast<float>(TurnAngle(joint.angles.x, pitch, turnStep));
    return static_cast<float>(std::sqrt((static_cast<double>(z) * z + xSquared) + ySquared));
}

float SteerHelperBeam(JointState& joint)
{
    constexpr float TurnStep = 25;
    return SteerJoint(joint, joint.targetPosition, TurnStep);
}

void MoveJoint(JointState& joint, const glm::vec3& moved)
{
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = static_cast<float>(static_cast<double>(moved[axis]) + joint.position[axis]);
}

// 0x53727d..0x537408 y 0x5379af..0x5379ef: el 615/0. Con vida de más de 15, rehace el rayo desde +0x24 hacia
// un destino sorteado una vez por paso (±100 en x, ±50 en y del +0x58), con 50 tramos que giran
// rand() % 80 + 60, sacuden (rand() % 1.400 − 700) / ancho en x y en z (la z sin redondear) y avanzan con el
// giro en el lugar de 0x5fcbd0; si no, la z de la luz baja 10,12f. Después, la cola común sólo con vida de
// más de 10.
Core::Expected<void, std::string> StepSkyBolt(JointState& joint, Render::Models::CrtRandom* random)
{
    constexpr std::int32_t FadeLife = 15;
    constexpr std::int32_t TailLife = 10;
    if (joint.life <= FadeLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x4121eb85u));
        joint.light.z = static_cast<float>(static_cast<double>(joint.light.z) - Fade);
    }
    else
    {
        if (random == nullptr)
            return Core::Unexpected(std::string("0x537295: falta el RNG del joint 0x615/0"));
        constexpr int SpreadX = 200, SpreadY = 100, TurnSpan = 80, JitterSpan = 1400, JitterCenter = 700;
        constexpr double CenterX = 100, CenterY = 50, TurnBase = 60;
        glm::vec3 target;
        target.x = static_cast<float>(
            (static_cast<double>(random->Next() % SpreadX) + static_cast<double>(joint.targetPosition.x)) - CenterX);
        target.y = static_cast<float>(
            (static_cast<double>(random->Next() % SpreadY) + static_cast<double>(joint.targetPosition.y)) - CenterY);
        target.z = joint.targetPosition.z;
        joint.position = joint.pathIncrement;
        for (std::int32_t iteration = 0; iteration < joint.maximumCount; ++iteration)
        {
            const auto turn = static_cast<float>(static_cast<double>(random->Next() % TurnSpan) + TurnBase);
            static_cast<void>(SteerJoint(joint, target, turn));
            const double width = static_cast<double>(joint.width);
            joint.velocity.x =
                static_cast<float>(static_cast<double>(random->Next() % JitterSpan - JitterCenter) / width);
            // 0x53736c: fst, la suma del giro z lee el cociente de la pila.
            const double roll = static_cast<double>(random->Next() % JitterSpan - JitterCenter) / width;
            joint.velocity.z = static_cast<float>(roll);
            const glm::vec3 perturbed{static_cast<float>(static_cast<double>(joint.angles.x) + joint.velocity.x),
                                      static_cast<float>(static_cast<double>(joint.angles.y) + joint.velocity.y),
                                      static_cast<float>(roll + static_cast<double>(joint.angles.z))};
            const auto matrix = Render::Models::OriginalAngleMatrix(perturbed);
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
                return appended;
            glm::vec3 moved{0, -joint.speed, 0};
            Render::Models::OriginalVectorRotateInPlace(moved, matrix);
            MoveJoint(joint, moved);
        }
    }
    joint.tailEnabled = joint.life > TailLife;
    return {};
}

// 0x53683e..0x536e8a: el 614/6. Con vida de 4 o menos, rehace el rayo desde +0x24: en cada uno de los 50
// tramos el destino es la posición del objeto dueño (+0x54, leída en vivo) más rand() % 200 + 2.050 en x y en
// y y 10.000 más abajo, gira rand() % 100 + 50 y sacude rand() % 100 + 20 en x y en z, sin dividir. Sin
// partículas ni luz (el ancho es menor que 50).
Core::Expected<void, std::string> StepObjectBolt(JointState& joint, Render::Models::CrtRandom* random)
{
    constexpr std::int32_t RebuildLife = 4;
    if (joint.life > RebuildLife)
        return {};
    const auto* owner = static_cast<const World::WorldObject*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x536a15: falta el objeto dueño del joint 0x614/6"));
    if (random == nullptr)
        return Core::Unexpected(std::string("0x536a2c: falta el RNG del joint 0x614/6"));
    constexpr int Spread = 200, TurnSpan = 100, TurnBase = 50, JitterSpan = 100, JitterBase = 20;
    constexpr double Offset = static_cast<double>(std::bit_cast<float>(0x45002000u));
    constexpr double Depth = static_cast<double>(std::bit_cast<float>(0x461c4000u));
    joint.position = joint.pathIncrement;
    for (std::int32_t iteration = 0; iteration < joint.maximumCount; ++iteration)
    {
        joint.targetPosition = owner->transform.position;
        joint.targetPosition.x = static_cast<float>((static_cast<double>(random->Next() % Spread) + Offset) +
                                                    static_cast<double>(joint.targetPosition.x));
        joint.targetPosition.y = static_cast<float>((static_cast<double>(random->Next() % Spread) + Offset) +
                                                    static_cast<double>(joint.targetPosition.y));
        joint.targetPosition.z = static_cast<float>(static_cast<double>(joint.targetPosition.z) - Depth);
        const auto turn = static_cast<float>(random->Next() % TurnSpan + TurnBase);
        static_cast<void>(SteerJoint(joint, joint.targetPosition, turn));
        joint.velocity.x = static_cast<float>(random->Next() % JitterSpan + JitterBase);
        joint.velocity.z = static_cast<float>(random->Next() % JitterSpan + JitterBase);
        const glm::vec3 perturbed{static_cast<float>(static_cast<double>(joint.angles.x) + joint.velocity.x),
                                  static_cast<float>(static_cast<double>(joint.velocity.y) + joint.angles.y),
                                  static_cast<float>(static_cast<double>(joint.velocity.z) + joint.angles.z)};
        const auto matrix = Render::Models::OriginalAngleMatrix(perturbed);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        MoveJoint(joint, Render::Models::OriginalVectorRotate({0, -joint.speed, 0}, matrix));
    }
    return {};
}

// 0x53768f..0x5379ef: el 615/2. Rehace el rayo desde +0x24 con la matriz de sus ángulos (0x5fc8f0): en los
// primeros 50 − 5 tramos corre x e y rand() % 20 − 10 y baja la z 16 (0x6b8f00); los cuatro siguientes avanzan un
// quinto de lo que falta hasta el destino (la x sin redondear la resta, la y y la z guardadas antes del 0,2f) y
// vuelven a correr x e y; el último es el destino. Con vida menor que 4 la luz se multiplica por 0,8333333f
// (0x6b8b98). Después, dos sprites 0x5fd de 1,5 en el destino con un giro rand() % 360 y la partícula 6ed/0 de
// escala 1, con la luz (x · 0,5, y · 0,6f, z). La cola común sigue sólo con vida de más de 10.
Core::Expected<void, std::string> StepTrapBolt(JointState& joint, Render::Models::CrtRandom* random,
                                               World::FrameSprites* sprites, World::Particles* particles,
                                               float worldClock)
{
    if (random == nullptr || sprites == nullptr || particles == nullptr)
        return Core::Unexpected(std::string("0x53770a: faltan el RNG, los sprites o las partículas del joint 0x615/2"));
    constexpr std::int32_t Approach = 5;
    constexpr int JitterSpan = 20, JitterCenter = 10;
    constexpr double Drop = static_cast<double>(std::bit_cast<float>(0x41800000u));
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    constexpr std::int32_t FadeLife = 4, TailLife = 10;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    const auto jitter = [&](float& axis)
    { axis = static_cast<float>(static_cast<double>(random->Next() % JitterSpan - JitterCenter) + axis); };
    joint.position = joint.pathIncrement;
    const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
    for (std::int32_t i = 0; i < joint.maximumCount - Approach; ++i)
    {
        jitter(joint.position.x);
        jitter(joint.position.y);
        joint.position.z = static_cast<float>(static_cast<double>(joint.position.z) - Drop);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    // 0x53777b..0x5377b9: la resta de la x queda en la pila; las de y y z se guardan antes de multiplicar.
    const double toX = static_cast<double>(joint.targetPosition.x) - joint.position.x;
    const auto toY = static_cast<float>(static_cast<double>(joint.targetPosition.y) - joint.position.y);
    const auto toZ = static_cast<float>(static_cast<double>(joint.targetPosition.z) - joint.position.z);
    const glm::vec3 step{static_cast<float>(toX * Step), static_cast<float>(static_cast<double>(toY) * Step),
                         static_cast<float>(static_cast<double>(toZ) * Step)};
    for (std::int32_t i = joint.maximumCount - Approach; i < joint.maximumCount - 1; ++i)
    {
        MoveJoint(joint, step);
        jitter(joint.position.x);
        jitter(joint.position.y);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    joint.position = joint.targetPosition;
    if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
        return appended;
    if (joint.life < FadeLife)
        joint.light = {static_cast<float>(static_cast<double>(joint.light.x) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.y) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.z) * Fade)};
    constexpr std::uint16_t ShinyTexture = 0x5fd;
    constexpr float ShinyScale = 1.5f;
    constexpr int FullTurn = 360;
    constexpr double HalfRed = 0.5;
    constexpr double GreenShare = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    const glm::vec3 light{static_cast<float>(static_cast<double>(joint.light.x) * HalfRed),
                          static_cast<float>(static_cast<double>(joint.light.y) * GreenShare), joint.light.z};
    for (int sprite = 0; sprite < 2; ++sprite)
    {
        // 0x5491e0(0x5fd, destino, 1,5, luz, 0, giro, 0): MuRet no guarda el dueño del sprite (Q-C303).
        const auto rotation = static_cast<float>(random->Next() % FullTurn);
        static_cast<void>(sprites->Create(ShinyTexture, joint.targetPosition, ShinyScale, light, rotation, 0));
    }
    auto created = particles->CreateChecked(World::KalimaFanta, joint.targetPosition, joint.angles, light, 0, 1,
                                            worldClock, *random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x537920: falló la partícula 0x6ed del joint 0x615/2 ({})",
                                            static_cast<int>(created.Error())));
    joint.tailEnabled = joint.life > TailLife;
    return {};
}

// 0x538ea4..0x538f37: el 6ee/9. Después del avance común, la x y la y se corren (rand() % 10) − 5,0f, la z sube la
// velocidad y la velocidad 1; con vida menor que 10 la luz se multiplica por 0,8695652f (0x6ba2e4).
Core::Expected<void, std::string> StepMeteor(JointState& joint, Render::Models::CrtRandom* random)
{
    if (random == nullptr)
        return Core::Unexpected(std::string("0x538ead: falta el RNG del joint 0x6ee/9"));
    constexpr int JitterSpan = 10;
    constexpr double JitterCenter = 5;
    constexpr double Acceleration = 1;
    constexpr std::int32_t FadeLife = 10;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f5e9bd4u));
    joint.position.x =
        static_cast<float>((static_cast<double>(random->Next() % JitterSpan) - JitterCenter) + joint.position.x);
    joint.position.y =
        static_cast<float>((static_cast<double>(random->Next() % JitterSpan) - JitterCenter) + joint.position.y);
    joint.position.z = static_cast<float>(static_cast<double>(joint.speed) + joint.position.z);
    joint.speed = static_cast<float>(static_cast<double>(joint.speed) + Acceleration);
    if (joint.life < FadeLife)
        joint.light = {static_cast<float>(static_cast<double>(joint.light.x) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.y) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.z) * Fade)};
    return {};
}

// FSIN o FCOS por un float de cualquier signo (vx del 619/9 llega a 0): el producto R53 por |f| con el signo de f; con
// f en ±0, el cero con el signo del producto.
std::optional<double> SignedTrigonometricProduct(double argument, float multiplier, bool cosine)
{
    const float magnitude = multiplier < 0 ? -multiplier : multiplier;
    const auto product = cosine ? Render::Models::Detail::CosineProductPC53(argument, magnitude > 0 ? magnitude : 1.0f)
                                : Render::Models::Detail::SineProductPC53(argument, magnitude > 0 ? magnitude : 1.0f);
    if (!product)
        return std::nullopt;
    if (!(magnitude > 0))
    {
        if (multiplier != 0)
            return std::nullopt;
        return std::signbit(*product) != std::signbit(multiplier) ? -0.0 : 0.0;
    }
    return std::signbit(multiplier) ? -*product : *product;
}

// 0x53b239..0x53b3d1: el 60f/0 y el 60f/0x12, después del avance común. Con la key en −1, el argumento es (vida + vy)
// · 0,1f, con la vida doble en la /0x12 y negado con el skill 1 (0x6b8b9c); con otra key, (vida + vy) / key. La x es
// el destino más cos · velocidad y la y el destino menos sen · velocidad. La /0 sube vz; la /0x12 sube vz · 1,1f, y la
// velocidad 0,1f y el ancho 0,2f más. Con la vida debajo de 10 la luz por 0,7692308f (0x6ba240). Con el ancho debajo
// de 40 y la acción del dueño (+0x111, la del personaje o la del caballo) en [0xd, 0x21] o [0x38, 0x3c], pasa a la /1:
// sin cola y la vida en 10 a lo sumo (Q-C383). Con 40 o más no lee al dueño.
Core::Expected<void, std::string> StepSparkle(JointState& joint, std::size_t index)
{
    constexpr std::int16_t NoKey = -1;
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8
    constexpr double Mirror = -1.0;                                                  // 0x6b8b9c
    constexpr std::uint8_t MirroredSkill = 1;
    constexpr double Climb = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu)); // 0x6b8830, 1,1f
    constexpr double Widen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0,2f
    constexpr std::int32_t FadeLife = 10, FallenLife = 10;
    constexpr float FallWidth = 40; // 0x6b8bf4
    constexpr std::uint8_t FirstFall = 0xd, LastFall = 0x21, SecondFirstFall = 0x38, SecondLastFall = 0x3c;
    double argument = 0;
    if (joint.key != NoKey)
        argument = (static_cast<double>(joint.life) + D(joint.velocity.y)) / static_cast<double>(joint.key);
    else if (joint.subtype == WideSparkleSubtype)
    {
        const auto twice = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) << 1u);
        argument = (static_cast<double>(twice) + D(joint.velocity.y)) * Tenth;
        if (joint.skill == MirroredSkill)
            argument = argument * Mirror;
    }
    else
        argument = (static_cast<double>(joint.life) + D(joint.velocity.y)) * Tenth;
    const auto cosine = Render::Models::Detail::CosineProductPC53(argument, joint.speed);
    const auto sine = Render::Models::Detail::SineProductPC53(argument, joint.speed);
    if (!cosine || !sine)
        return Core::Unexpected(std::format("0x53b2a2: el giro del joint {} (0x60f/{:#x}) excede el perfil matemático",
                                            index, joint.subtype));
    joint.position.x = F32(*cosine + D(joint.targetPosition.x));
    joint.position.y = F32(D(joint.targetPosition.y) - *sine);
    if (joint.subtype == SparkleSubtype)
        joint.position.z = F32(D(joint.velocity.z) + D(joint.position.z));
    else
    {
        joint.position.z = F32(D(joint.velocity.z) * Climb + D(joint.position.z));
        joint.speed = F32(D(joint.speed) + Tenth);
        joint.width = F32(D(joint.width) + Widen);
    }
    if (joint.life < FadeLife)
        FadeJointLight(joint, 0x3f44ec4fu);
    // 0x53b378: fcomp con 40 y test ah,1: debajo de 40 o NaN.
    if (joint.width >= FallWidth)
        return {};
    const std::optional<std::uint8_t> ownerAction = SparkleOwnerAction(joint);
    if (!ownerAction)
        return Core::Unexpected(
            std::format("0x53b38c: falta el dueño del joint {} (0x60f/{:#x})", index, joint.subtype));
    const std::uint8_t action = *ownerAction;
    if (!((action >= FirstFall && action <= LastFall) || (action >= SecondFirstFall && action <= SecondLastFall)))
        return {};
    joint.subtype = FallenSparkleSubtype;
    joint.tailEnabled = false;
    if (joint.life > FallenLife)
        joint.life = FallenLife;
    return {};
}

// 0x537c82..0x537d17: el 610/7, después del avance común. La x es la partida (+0x24) más sen(vida · 0,4f) · vx, con el
// argumento guardado en float para 0x52e3a0; la y es la de la partida y la z sube la velocidad. La velocidad y vx bajan
// 0,1f, y con la vida debajo de 15 la luz por 0,8333333f (0x6b8b98).
Core::Expected<void, std::string> StepSetTrail(JointState& joint, std::size_t index)
{
    constexpr double Pace = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6ba2f4, 0,4f
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8
    constexpr std::int32_t FadeLife = 15;
    const float argument = F32(static_cast<double>(joint.life) * Pace);
    const auto sine = SignedTrigonometricProduct(D(argument), joint.velocity.x, false);
    if (!sine)
        return Core::Unexpected(
            std::format("0x537c9b: el seno del joint {} (0x610/7) excede el perfil matemático", index));
    joint.position.x = F32(*sine + D(joint.pathIncrement.x));
    joint.position.y = joint.pathIncrement.y;
    joint.position.z = F32(D(joint.speed) + D(joint.position.z));
    joint.speed = F32(D(joint.speed) - Tenth);
    joint.velocity.x = F32(D(joint.velocity.x) - Tenth);
    if (joint.life < FadeLife)
        FadeJointLight(joint, 0x3f555555u);
    return {};
}

// 0x5387a9..0x5388e2 y 0x5389f7: el 619/9. Lee al dueño (+0x54) en vivo: corre los puntos de los tramos de count − 1 a
// 0 lo que el dueño se movió desde el destino (+0x58), que pasa a ser su posición. La posición gira alrededor del
// destino con el argumento +0x9ec · 0,1f (+0x9ec sube 2) a la distancia vx; la z es la del destino más 90 − vida y 10
// más. vx baja 1 y, por debajo de 0 sin redondear, queda en 0 con la vida en 0. Con la vida debajo de 50 la luz por
// 0,9090909f (0x6ba26c) y por encima de 80 por 1,25 (0x6b8ad4).
Core::Expected<void, std::string> StepSetEnergy(JointState& joint, std::size_t index)
{
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8
    constexpr std::int32_t Rise = 90;
    constexpr double Lift = 10.0, Shrink = 1.0;
    constexpr std::int32_t FadeLife = 50, BrightLife = 80;
    const auto* owner = static_cast<const ActorState*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::format("0x5387b2: falta el personaje dueño del joint {} (0x619/9)", index));
    glm::vec3 delta{};
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = F32(D(joint.targetPosition[axis]) - D(owner->position[axis]));
    joint.targetPosition = owner->position;
    if (joint.count >= static_cast<std::int32_t>(joint.points.size()))
        return Core::Unexpected(std::format("0x5387e0: la cola del joint {} (0x619/9) excede sus puntos", index));
    for (std::int32_t segment = joint.count - 1; segment >= 0; --segment)
        for (glm::vec3& point : joint.points[static_cast<std::size_t>(segment)])
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = F32(D(point[axis]) - D(delta[axis]));
    const double argument = static_cast<double>(joint.field9ec) * Tenth;
    joint.field9ec = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.field9ec) + 2u);
    const auto sine = SignedTrigonometricProduct(argument, joint.velocity.x, false);
    const auto cosine = SignedTrigonometricProduct(argument, joint.velocity.x, true);
    if (!sine || !cosine)
        return Core::Unexpected(
            std::format("0x53884e: el giro del joint {} (0x619/9) excede el perfil matemático", index));
    joint.position.x = F32(*sine + D(joint.targetPosition.x));
    joint.position.y = F32(*cosine + D(joint.targetPosition.y));
    const auto height =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(Rise) - std::bit_cast<std::uint32_t>(joint.life));
    joint.position.z = F32((static_cast<double>(height) + D(joint.targetPosition.z)) + Lift);
    // 0x538885: fst y fcomp con 0 de la resta sin redondear.
    const double reduced = D(joint.velocity.x) - Shrink;
    joint.velocity.x = F32(reduced);
    if (!(reduced >= 0))
    {
        joint.velocity.x = 0;
        joint.life = 0;
    }
    if (joint.life < FadeLife)
        FadeJointLight(joint, 0x3f68ba2eu);
    else if (joint.life > BrightLife)
        FadeJointLight(joint, 0x3fa00000u);
    return {};
}

// 0x538a52..0x538a70, 0x538b17..0x538b4a, 0x538b5d..0x538b94 y 0x538cc9..0x538d28: el 619/1, después del avance
// común. La velocidad sube 4 (0x6b8a44); gira de a 10 (0x4e9810) hacia el dueño (+0x54, leído en vivo), 120 más arriba
// (0x6b86c8); la luz es (12 − vida) · 0,1f por (0,4f; 0,6f; 1), con el producto sin redondear; con la vida en 1, el
// sprite 0x5fd en ese destino con la luz, el giro rand() % 360 y la escala ((rand() & 7) + 8) · 0,2f. Sigue la cola
// común (0x53c393).
Core::Expected<void, std::string> StepSummonEnergy(JointState& joint, std::size_t index,
                                                   Render::Models::CrtRandom* random, World::FrameSprites* sprites)
{
    constexpr std::int32_t FullLife = 12, LastLife = 1;
    if (joint.life == LastLife && (random == nullptr || sprites == nullptr))
        return Core::Unexpected(
            std::format("0x538cce: faltan el RNG o los sprites del joint {} (0x619/1) en su última vida", index));
    const auto* owner = static_cast<const ActorState*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::format("0x538b17: falta el personaje dueño del joint {} (0x619/1)", index));
    constexpr double Climb = 4.0, Lift = 120.0;
    constexpr float TurnStep = 10;
    joint.speed = F32(Climb + D(joint.speed));
    const glm::vec3 target{owner->position.x, owner->position.y, F32(D(owner->position.z) + Lift)};
    static_cast<void>(SteerJoint(joint, target, TurnStep));
    constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu), Blue = std::bit_cast<float>(0x3ecccccdu),
                    Green = std::bit_cast<float>(0x3f19999au);
    const double brightness = static_cast<double>(FullLife - joint.life) * D(Tenth);
    joint.light = {F32(brightness * D(Blue)), F32(brightness * D(Green)), F32(brightness)};
    if (joint.life != LastLife)
        return {};
    constexpr std::uint16_t ShinyTexture = 0x5fd;
    constexpr int FullTurn = 360, ScaleSpread = 7, ScaleBase = 8;
    constexpr float ScaleStep = std::bit_cast<float>(0x3e4ccccdu);
    // 0x5491e0(0x5fd, destino, escala, luz, dueño, giro, 0): MuRet no guarda el dueño del sprite (Q-C303).
    const auto rotation = static_cast<float>(random->Next() % FullTurn);
    const auto scale = F32(static_cast<double>((random->Next() & ScaleSpread) + ScaleBase) * D(ScaleStep));
    static_cast<void>(sprites->Create(ShinyTexture, target, scale, joint.light, rotation, 0));
    return {};
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

// 0x538313..0x5386a4 con la /0 y la /1, después del movimiento común (con la velocidad de antes del caso). Con la
// vida en 100 o más, gira: la /0 suma 10 al rumbo y 6 a la z; la /1 resta 10 y suma 5. Con menos, la velocidad + 5,
// hasta 30, el destino en el dueño (+0x54: su posición, la z + 120) y 0x4e9810 hacia él con la velocidad de paso. A 35
// o menos (o NaN), muere y pide el 0x5d5/1 (0x538577) y el sonido 0x23 (0x538582); de 35 a 70, con el rumbo girado más
// de 20 y la velocidad en 20 o más, la velocidad − 10. Después, siempre: v = ((rand() & 3) + 8) · 0,03f, la luz
// dinámica de radio 2 (v · 0,4f; v; v · 0,8f) (0x53861c) y el 0x5d5/0 (0x538695) con la luz del joint. Un rand().
Core::Expected<void, std::string> StepKillBeam(JointState& joint, std::size_t index, Render::Models::CrtRandom* random,
                                               World::Particles* particles, float worldClock,
                                               World::TerrainLighting* lighting,
                                               const std::function<void(std::int32_t)>& soundRequest)
{
    constexpr std::int32_t SpinLife = 100;
    constexpr double Turn = 10.0, Accel = 5.0, Brake = 10.0; // 0x6b87a8, 0x6b87f8
    constexpr double FirstRise = 6.0, SecondRise = 5.0;      // 0x6b87f4, 0x6b87f8
    constexpr double TopSpeed = 30.0, SlowSpeed = 20.0;      // 0x6b8858, 0x6b86d0
    constexpr double Hit = 35.0, Near = 70.0, Swerve = 20.0; // 0x6b8974, 0x6ba1b0, 0x6ba2f8
    constexpr double Above = 120.0;                          // 0x6b86c8
    constexpr std::int32_t HitSound = 0x23;
    constexpr std::int32_t GlowMask = 3, GlowBase = 8, GlowRadius = 2;
    constexpr double GlowStep = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu)); // 0x6b8bd4
    constexpr double GlowRed = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4
    constexpr double GlowBlue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38
    const bool first = joint.subtype == 0;
    if (joint.life >= SpinLife)
    {
        joint.angles.z = static_cast<float>(D(joint.angles.z) + (first ? Turn : -Turn));
        joint.position.z = static_cast<float>(D(joint.position.z) + (first ? FirstRise : SecondRise));
    }
    else
    {
        const double faster = D(joint.speed) + Accel;
        joint.speed = static_cast<float>(faster);
        // 0x538496: FCOMP con solo C0, así que NaN no llega a 30.
        if (faster >= TopSpeed)
            joint.speed = static_cast<float>(TopSpeed);
        const float heading = joint.angles.z;
        const auto* owner = static_cast<const ActorState*>(joint.ownerContext);
        if (owner == nullptr)
            return Core::Unexpected(
                std::format("0x538501: falta el personaje dueño del joint {} (0x618/{})", index, joint.subtype));
        joint.targetPosition = {owner->position.x, owner->position.y, static_cast<float>(D(owner->position.z) + Above)};
        const double distance =
            Render::Models::MoveToward(joint.position, joint.angles, joint.targetPosition, joint.speed);
        if (!(distance > Hit))
        {
            joint.alive = false;
            if (particles == nullptr || random == nullptr)
                return Core::Unexpected(
                    std::format("0x538577: faltan las partículas del joint {} (0x618/{})", index, joint.subtype));
            if (!particles
                     ->CreateChecked(World::LightningParticle, joint.position, joint.angles, joint.light,
                                     World::LightningHitSubtype, 1.0f, worldClock, *random)
                     .HasValue())
                return Core::Unexpected(std::format("0x538577: el 0x5d5/1 del joint {} falla", index));
            if (soundRequest)
                soundRequest(HitSound);
        }
        else if (!(distance > Near) && std::abs(D(heading) - D(joint.angles.z)) > Swerve && D(joint.speed) >= SlowSpeed)
        {
            joint.speed = static_cast<float>(D(joint.speed) - Brake);
        }
    }
    if (random == nullptr || particles == nullptr)
        return Core::Unexpected(
            std::format("0x5385d3: faltan el RNG y las partículas del joint {} (0x618/{})", index, joint.subtype));
    const double glow = static_cast<double>((random->Next() & GlowMask) + GlowBase) * GlowStep;
    const glm::vec3 color{static_cast<float>(glow * GlowRed), static_cast<float>(glow),
                          static_cast<float>(glow * GlowBlue)};
    if (auto lit = World::AddDynamicLight(lighting, joint.position, color, GlowRadius, "0x53861c"); !lit.HasValue())
        return lit;
    if (!particles
             ->CreateChecked(World::LightningParticle, joint.position, joint.angles, joint.light,
                             World::LightningTrailSubtype, 1.0f, worldClock, *random)
             .HasValue())
        return Core::Unexpected(std::format("0x538695: el 0x5d5/0 del joint {} falla", index));
    return {};
}

Core::Expected<void, std::string> StepJointsChecked(std::span<JointState> joints, Render::Models::CrtRandom* random,
                                                    Render::Models::ClothWorld* cloth, World::Particles* particles,
                                                    float worldClock, World::FrameSprites* sprites,
                                                    World::TerrainLighting* lighting,
                                                    const std::function<void(std::int32_t)>& soundRequest)
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
        // 0x53c478: el 614/10 de la espada 0x224 comparte con el 614/14 la entrada 8 (0x536b24) y el resto del paso
        // (Q-C366).
        const bool flareBeam = joint.type == 0x614 && (joint.subtype == 14 || joint.subtype == 10);
        const bool icarus = joint.type == 0x610 && joint.subtype == 8;
        const bool noria = joint.type == 0x617 && joint.subtype >= 0 && joint.subtype <= 3;
        const bool skyBolt = joint.type == 0x615 && joint.subtype == 0;
        const bool objectBolt = joint.type == 0x614 && joint.subtype == 6;
        const bool trapBolt = joint.type == 0x615 && joint.subtype == 2;
        const bool meteor = joint.type == 0x6ee && joint.subtype == 9;
        // Los de lo que cuelga de un jugador, con el ActorState del personaje como dueño.
        const bool staffBeam = joint.type == 0x618 && joint.subtype == 0x11;
        const bool swordTrail = joint.type == 0x610 && joint.subtype == 4;
        // Los del conjunto completo (0x5102c3) y el del 5ac/3 (0x52aa5d), con el ActorState del personaje.
        const bool sparkle =
            joint.type == SparkleJoint && (joint.subtype == SparkleSubtype || joint.subtype == WideSparkleSubtype);
        const bool fallenSparkle = joint.type == SparkleJoint && joint.subtype == FallenSparkleSubtype;
        const bool setTrail = joint.type == SetTrailJoint && joint.subtype == SetTrailSubtype;
        const bool setEnergy = joint.type == EnergyJoint && joint.subtype == SetEnergySubtype;
        const bool fullSet = sparkle || fallenSparkle || setTrail || setEnergy;
        // El del calco de la mascota (0x518900), con el héroe de dueño.
        const bool summonEnergy = joint.type == EnergyJoint && joint.subtype == SummonEnergySubtype;
        // Los del que mató (0x505169), con el que pega de dueño.
        const bool killBeam = joint.type == KillBeamJoint && (joint.subtype == 0 || joint.subtype == 1);
        if (!crywolf && !tarkan && !bloodCastle && !fairy && !spiritBeam && !flareBeam && !icarus && !noria &&
            !skyBolt && !objectBolt && !trapBolt && !meteor && !staffBeam && !swordTrail && !fullSet && !summonEnergy &&
            !killBeam)
            return Core::Unexpected(
                std::format("0x535840: el paso del joint {} ({:#x}/{}) no está portado", i, joint.type, joint.subtype));
        // 0x5358bc: el 60f/1 no se mueve ni calcula la matriz de 0x53590e; con la cola, 0x53c393 usaría la que dejó el
        // joint anterior en [ebp−0xe8].
        if (fallenSparkle && joint.tailEnabled)
            return Core::Unexpected(std::format(
                "0x53c393: el joint {} (0x60f/1) con cola usa la matriz del joint anterior, no está portado", i));
        // 53590e..53597a: el movimiento común precede la lectura del dueño, incluso si muere.
        const auto matrix = Render::Models::OriginalAngleMatrix(joint.angles);
        // 535932 también omite las sumas para unordered, conservando ceros con signo y posición.
        if (!fallenSparkle && (joint.speed < 0 || joint.speed > 0))
        {
            const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
            joint.position = {static_cast<float>(static_cast<double>(joint.position.x) + moved.x),
                              static_cast<float>(static_cast<double>(joint.position.y) + moved.y),
                              static_cast<float>(static_cast<double>(joint.position.z) + moved.z)};
        }
        if (flareBeam)
            if (auto result = StepHelperBeam(joint, random, particles, worldClock); !result.HasValue())
                return result;
        if (skyBolt)
            if (auto result = StepSkyBolt(joint, random); !result.HasValue())
                return result;
        if (objectBolt)
            if (auto result = StepObjectBolt(joint, random); !result.HasValue())
                return result;
        if (trapBolt)
            if (auto result = StepTrapBolt(joint, random, sprites, particles, worldClock); !result.HasValue())
                return result;
        if (meteor)
            if (auto result = StepMeteor(joint, random); !result.HasValue())
                return result;
        if (sparkle)
            if (auto result = StepSparkle(joint, i); !result.HasValue())
                return result;
        if (fallenSparkle && joint.life < 10)
            FadeJointLight(joint, 0x3f44ec4fu); // 0x53b339: la /1 sólo se apaga.
        if (setTrail)
            if (auto result = StepSetTrail(joint, i); !result.HasValue())
                return result;
        if (setEnergy)
            if (auto result = StepSetEnergy(joint, i); !result.HasValue())
                return result;
        if (summonEnergy)
            if (auto result = StepSummonEnergy(joint, i, random, sprites); !result.HasValue())
                return result;
        if (killBeam)
            if (auto result = StepKillBeam(joint, i, random, particles, worldClock, lighting, soundRequest);
                !result.HasValue())
                return result;
        const bool actorOwned = staffBeam || swordTrail || fullSet || summonEnergy || killBeam;
        const auto* owner = (joint.type == 0x613 || joint.type == 0x614 || joint.type == 0x615 || meteor || actorOwned)
                                ? nullptr
                                : static_cast<const FaunaObject*>(joint.ownerContext);
        const bool ownerless =
            spiritBeam || flareBeam || icarus || noria || skyBolt || objectBolt || trapBolt || meteor || actorOwned;
        if (owner == nullptr && !ownerless)
            return Core::Unexpected(std::format("{}: falta el dueño durable del joint {:#x}/{}",
                                                fairy         ? "0x536076"
                                                : bloodCastle ? "0x535efe"
                                                : tarkan      ? "0x53820e"
                                                              : "0x5381ce",
                                                joint.type, joint.subtype));
        // 697 copia antes de comprobar active;618/4 comprueba antes de copiar.
        if (crywolf)
            joint.position = owner->previousPosition;
        if (!bloodCastle && !fairy && !ownerless && owner->active == 0)
        {
            joint.alive = false;
            continue; // 5381e9: no agrega cola ni descuenta vida.
        }
        if (tarkan)
            joint.position = owner->previousPosition;
        if (staffBeam)
        {
            // 0x53820e: con el personaje muerto se apaga sin cola ni vida; vivo, sigue su +0x40 (0x53822f).
            const auto* actor = static_cast<const ActorState*>(joint.ownerContext);
            if (actor == nullptr)
                return Core::Unexpected(std::format("0x53820e: falta el personaje dueño del joint {} (0x618/0x11)", i));
            if (!actor->alive)
            {
                joint.alive = false;
                continue;
            }
            joint.position = actor->effectOrigin;
        }
        if (swordTrail)
            if (auto result = StepSwordTrail(joint, i); !result.HasValue())
                return result;
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
            // 0x53c393: 535520 con la matriz de 0x53590e, la del principio del paso, aunque el caso haya girado.
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
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
    return StepJointsChecked(joints, nullptr, nullptr, nullptr, 0, nullptr, nullptr, {});
}

Core::Expected<void, std::string> StepJoints(std::span<JointState> joints, Render::Models::CrtRandom& random,
                                             Render::Models::ClothWorld* cloth, World::Particles* particles,
                                             float worldClock, World::FrameSprites* sprites,
                                             World::TerrainLighting* lighting,
                                             const std::function<void(std::int32_t)>& soundRequest)
{
    return StepJointsChecked(joints, &random, cloth, particles, worldClock, sprites, lighting, soundRequest);
}
} // namespace Client::Scenes
