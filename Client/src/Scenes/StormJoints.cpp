#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <functional>
#include <string>

namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;

// 0x53c0a5..0x53c390 con la /5 (la que suelta el pulso 0x61c/0 del asedio, 0x52c266), después del movimiento común: la
// velocidad sube 10, la cola con la matriz de los ángulos (0x535520, si está prendida), sube otros 10 y avanza
// (0, −velocidad, 0) girado por +0x3c. Debajo de la altura del terreno menos +0x9ec (150), la z queda ahí y, la primera
// vez (la clave en 0), el efecto 0x6eb/4 (0x518cf0) con la luz (1; 0,8f; 0,3f), que es del lote de los altos: corta;
// después, la clave en 1, la velocidad en −20 y la luz por 0,8333333f. Siempre, el sprite 0x60f de escala 2,5 con la
// luz y el giro rand() % 360.
Core::Expected<void, std::string> StepFallingStar(JointState& joint, const JointKernels::StepServices& services)
{
    if (services.random == nullptr || services.sprites == nullptr || services.terrainHeight == nullptr ||
        !*services.terrainHeight)
        return Core::Unexpected(
            std::string("0x53c0c9: faltan el RNG, los sprites o la altura del terreno del joint 0x632/5"));
    constexpr double Boost = 10.0; // 0x6b87a8
    joint.speed = F32(D(joint.speed) + Boost);
    if (joint.tailEnabled)
        if (auto appended = JointKernels::AppendJointTail(joint, Render::Models::OriginalAngleMatrix(joint.angles));
            !appended.HasValue())
            return appended;
    joint.speed = F32(D(joint.speed) + Boost);
    const auto moved = Render::Models::OriginalVectorRotate({0, -joint.speed, 0},
                                                            Render::Models::OriginalAngleMatrix(joint.bendAngles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    // 0x53c16d..0x53c18a: fisub de +0x9ec y fcom con la z (test ah,0x41): sólo con el suelo estrictamente arriba.
    const double floor =
        (*services.terrainHeight)(joint.position.x, joint.position.y) - static_cast<double>(joint.field9ec);
    if (floor > D(joint.position.z))
    {
        joint.position.z = F32(floor);
        constexpr std::int16_t Landed = 1;
        if (joint.tailEnabled && joint.key != Landed)
            return Core::Unexpected(std::string("0x53c295: el efecto 0x6eb/4 (0x518cf0) del joint 0x632/5 al llegar "
                                                "al suelo es del lote de los altos (CL11)"));
        constexpr float Bounce = std::bit_cast<float>(0xc1a00000u); // −20
        joint.key = Landed;
        joint.speed = Bounce;
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98, 0,8333333f
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    // 0x53c320..0x53c38b: 0x5491e0(0x60f, posición, 2,5, luz, 0, giro, 0); MuRet no guarda el dueño del sprite.
    constexpr std::uint16_t StarTexture = 0x60f;
    constexpr float StarScale = 2.5f; // 0x40200000
    constexpr int FullTurn = 360;
    const auto rotation = static_cast<float>(services.random->Next() % FullTurn);
    static_cast<void>(services.sprites->Create(StarTexture, joint.position, StarScale, joint.light, rotation, 0));
    return {};
}
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
    if (request.subtype > StormSubtype)
    {
        // 0x534b1e → 0x534cb6: después del primer tramo, sólo el tope de 50 puntos.
        constexpr std::int32_t MostPoints = 50;
        if (joint.maximumCount > MostPoints)
            joint.maximumCount = MostPoints;
        return {};
    }
    if (request.subtype == FallingStarSubtype)
    {
        // 0x534bcb..0x534c0c: 10 puntos, vida 40, velocidad 70, el ancho, +0x9f1 en rand() & 1, la clave en 0 y +0x9ec
        // en 150 (lo que baja la altura del terreno en el paso). 0x534c61..0x534c8e: la /5 con la textura 0x60f, 15
        // puntos, la velocidad rand() % 20 + 10 y +0x9f1 en 2. 0x534ca4: +0x3c, los ángulos del pedido. Dos rand().
        constexpr std::int32_t Life = 0x28, Points = 0xf, Below = 0x96;
        constexpr float Speed = 70;                  // 0x428c0000
        constexpr std::uint16_t StarTexture = 0x60f; // 0x534c61
        constexpr int SpeedSpan = 0x14;
        constexpr double SpeedBase = 10.0; // 0x6b87a8
        constexpr std::uint8_t StarOrientation = 2;
        joint.maximumCount = 10;
        joint.life = Life;
        joint.speed = Speed;
        joint.width = request.scale;
        joint.orientation = static_cast<std::uint8_t>(random.Next() & 1);
        joint.key = 0;
        joint.field9ec = Below;
        joint.texture = StarTexture;
        joint.maximumCount = Points;
        joint.speed = F32(static_cast<double>(random.Next() % SpeedSpan) + SpeedBase);
        joint.orientation = StarOrientation;
        joint.bendAngles = joint.angles;
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
    if (joint.subtype >= StormSubtype)
        return {}; // 0x53bec5: la /7 y las de más de 7 sólo pasan por la cola y la vida.
    if (joint.subtype == FallingStarSubtype)
        return StepFallingStar(joint, services);
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
    // partícula 0x5ee/0 en +0x24, con la luz y escala 3, y el ancho 100; después, el ancho 40.
    if (static_cast<std::int32_t>(joint.volley) - joint.life < 10)
    {
        constexpr std::int32_t Smoke = 0x5ee;
        constexpr float SmokeScale = 3.0f; // 0x53bf59
        const bool made =
            services.particles != nullptr && services.particles
                                                 ->CreateChecked(Smoke, joint.pathIncrement, joint.angles, joint.light,
                                                                 0, SmokeScale, services.worldClock, *services.random)
                                                 .HasValue();
        if (!made)
            return Core::Unexpected(std::string("0x53bf6e: la partícula 0x5ee (0x53fe70) del joint 0x632/6 es del lote "
                                                "de partículas (CL11)"));
        joint.width = 100;
    }
    else
        joint.width = 40;
    // 0x53bf86..0x53bfa3: la altura del terreno (0x5f93c0), guardada en float. Con la z por encima, sólo la cola
    // cruzada; en el suelo o debajo (o NaN), el 0x614/4, 0x52e3a0 y la partícula 0x6ac/2, que no están portados.
    if (services.terrainHeight == nullptr || !*services.terrainHeight)
        return Core::Unexpected(std::string("0x53bf8d: falta la altura del terreno del joint 0x632/6"));
    const float ground = F32((*services.terrainHeight)(joint.position.x, joint.position.y));
    if (!(joint.position.z > ground))
        return Core::Unexpected(std::string("0x53bfd3: el joint 0x632/6 en el suelo (el 0x614/4, 0x52e3a0 y la "
                                            "partícula 0x6ac) no está portado (CL11)"));
    // 0x53c052..0x53c0a0: 0x535020(joint, la matriz del paso, 1) y, con la vida por debajo de la mitad de la tanda,
    // la luz · 0,7692308f.
    if (auto appended = JointKernels::AppendCrossedJointTail(joint, matrix, true); !appended.HasValue())
        return appended;
    if (joint.life < static_cast<std::int32_t>(joint.volley >> 1))
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace Client::Scenes::JointFamilies
