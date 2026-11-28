#include "Core/Expected.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Joints/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Effects/BlastModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

// El 0x61c/2..6 (alta 0x534444..0x53463b, paso 0x53b6a0..0x53b7ec): suben acelerando con la velocidad (+0x9d8) que
// crece en vz, que a su vez crece en vx (de a 0,5 con menos de 15 de vida). La /4 (la skill 0x49, 0x504b33) suelta en
// cada paso un 0x62b/6 y los sprites 0x5ac y 0x5fd; la /2 suelta el efecto 0xf6/6 cada cinco vidas.
namespace Client::Scenes::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;
} // namespace

bool IsInferno(std::int32_t type, std::int32_t subtype)
{
    return type == InfernoJoint && subtype >= 2 && subtype <= 6; // 0x534432..0x53443e y 0x53b6a0..0x53b6ac
}

void CreateInfernoJoint(JointState& joint, const Kernel::JointRequest& request)
{
    // 0x534444..0x53448c: lo común de la /2..6.
    constexpr float Speed = 8;                                  // 0x41000000
    constexpr float Spread = std::bit_cast<float>(0x40600000u); // 3,5
    joint.width = request.scale;
    joint.speed = Speed;
    joint.velocity.x = Spread;
    joint.velocity.z = 1;
    joint.life = 20;
    joint.maximumCount = 12;
    joint.count = -1;
    joint.field9ec = 5;
    const glm::vec3 turn{0, 0, joint.angles.z};
    switch (joint.subtype)
    {
    case 3:
        // 0x534494..0x5344d8.
        joint.texture = 0x60f;
        joint.speed = 10;
        joint.velocity.z = 2;
        joint.orientation = 0;
        joint.velocity.x = std::bit_cast<float>(0x41280000u); // 10,5
        joint.field9ec = 10;
        joint.bendAngles = turn;
        break;
    case 4:
        // 0x5344e2..0x534522: Effect\Hole.jpg, 13 puntos, vx 15,5 y vz 3.
        joint.texture = 0x6ae;
        joint.velocity.x = std::bit_cast<float>(0x41780000u); // 15,5
        joint.life = 20;
        joint.velocity.z = 3;
        joint.maximumCount = 13;
        joint.field9ec = 10;
        joint.bendAngles = turn;
        break;
    case 5:
        joint.texture = 0x634; // 0x53452b: Effect\Lava.jpg
        joint.bendAngles = turn;
        break;
    case 6:
        // 0x53454a..0x534573: la lava más rápida, con vida 16.
        joint.texture = 0x634;
        joint.speed = 20;
        joint.life = 16;
        joint.bendAngles = turn;
        break;
    default:
        // 0x534578..0x5345bd: la /2, 10 más arriba, con Effect\Inferno.jpg, vida 15 y vx y vz en 5.
        joint.position.z = F32(D(joint.position.z) + 10);
        joint.life = 15;
        joint.field9ec = 5;
        joint.texture = 0x633;
        joint.velocity.x = 5;
        joint.velocity.z = 5;
        joint.bendAngles = turn;
        break;
    }
    // 0x53463b: el camino (+0x24) en la posición.
    joint.pathIncrement = joint.position;
}

Core::Expected<void, std::string> StepInfernoJoint(JointState& joint, const JointKernels::StepServices& services)
{
    // 0x53b6b2..0x53b6ec: la velocidad suma vz; vz suma 0,5 con menos de 15 de vida y si no vx.
    const std::int32_t life = joint.life;
    joint.speed = F32(D(joint.speed) + D(joint.velocity.z));
    constexpr std::int32_t SlowLife = 15;
    joint.velocity.z =
        life < SlowLife ? F32(D(joint.velocity.z) + 0.5) : F32(D(joint.velocity.x) + D(joint.velocity.z));
    // 0x53b6f2..0x53b702: salvo la /4, con la cola llena deja de agregar tramos.
    if (joint.subtype != 4 && joint.count >= joint.maximumCount - 1)
        joint.tailEnabled = false;
    // 0x53b709..0x53b732: con la vida debajo de +0x9ec, la luz por 0,7692308f.
    if (life < joint.field9ec)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    // 0x53b735..0x53b768: la /2 con la vida múltiplo de 5 suelta el 0xf6/6 en su camino: 0x518cf0(0xf6, +0x24, +0x3c,
    // +0x48, 6, sin dueño, 20, 0, 0, 0, 0), y vuelve a la cola (0x53c393).
    constexpr std::int32_t BurstEvery = 5; // 0x53b736
    if (life % BurstEvery == 0 && joint.subtype == 2)
    {
        constexpr std::int16_t InfernoBurst = 0xf6;
        constexpr std::int32_t BurstSubtype = 6;
        constexpr std::uint16_t BurstKey = 20; // 0x53b74a: 0x14
        if (services.effects == nullptr || services.random == nullptr)
            return Core::Unexpected(
                std::string("0x53b760: el joint 0x61c/2 pide la tabla de efectos (0x518cf0) y el RNG para el 0xf6/6"));
        const auto made = World::CreateBlastModelEffect(*services.effects,
                                                        {.type = InfernoBurst,
                                                         .position = joint.pathIncrement,
                                                         .angles = joint.bendAngles,
                                                         .light = joint.light,
                                                         .subtype = BurstSubtype,
                                                         .owner = nullptr,
                                                         .key = BurstKey},
                                                        World::ModelEffectOwner{}, *services.random,
                                                        World::BlastEffectServices{.hero = services.hero});
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x53b760: {}", made.Error()));
        return {};
    }
    if (joint.subtype != 4)
        return {};
    // 0x53b776..0x53b7ec: la /4 suelta un 0x62b/6 sin dueño de ancho 30 desde su posición, con sus ángulos (un rand()
    // si hay lugar), el sprite 0x5ac de escala 1,6f y el 0x5fd de 1,5f girado el reloj · 0,1f, con la luz (0,1; 0,6;
    // 1).
    if (services.random == nullptr || services.sprites == nullptr)
        return Core::Unexpected(std::string("0x53b7a7: faltan el RNG o los sprites del joint 0x61c/4"));
    constexpr std::int32_t ImpactJoint = 0x62b, ImpactSubtype = 6;
    constexpr float ImpactWidth = 30; // 0x41f00000
    const Kernel::JointRequest impact{.type = ImpactJoint,
                                      .firstPosition = joint.position,
                                      .secondPosition = joint.position,
                                      .angles = joint.angles,
                                      .subtype = ImpactSubtype,
                                      .ownerContext = nullptr,
                                      .scale = ImpactWidth,
                                      .key = -1,
                                      .skill = 0,
                                      .skillSerial = 0,
                                      .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, impact, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x53b7a7: {}", created.Error()));
    const glm::vec3 glow{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f19999au), 1};
    constexpr std::uint16_t Shine = 0x5ac, Flare = 0x5fd;
    constexpr float ShineScale = std::bit_cast<float>(0x3fcccccdu); // 1,6f
    constexpr float FlareScale = 1.5f;                              // 0x3fc00000
    constexpr double Tenth = Math::TenthAsDouble;                   // 0x6b87b8
    static_cast<void>(services.sprites->Create(Shine, joint.position, ShineScale, glow, 0, 0));
    static_cast<void>(
        services.sprites->Create(Flare, joint.position, FlareScale, glow, F32(D(services.worldClock) * Tenth), 0));
    return {};
}
} // namespace Client::Scenes::JointFamilies
