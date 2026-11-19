#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

// El 0x614: el alta (0x5309d6, después del primer tramo de 0x530afa) y el paso (0x53683e..0x537278).
namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::AppendJointTail;
using JointKernels::D;
using JointKernels::F32;
using Render::Models::CrtRandom;

constexpr float SharedSpeed = 50;         // 0x5309d9
constexpr std::int32_t SharedPoints = 50; // 0x5309ec
constexpr std::int32_t MostPoints = 50;   // 0x534cb9

// 0x531676: (rand() % 10) · 0,06666667f (0x6ba1e0) + 0,1f (0x6b87b8), guardado una vez en las tres luces.
glm::vec3 RandomGray(CrtRandom& random)
{
    constexpr int GraySpan = 10;
    constexpr double GrayStep = static_cast<double>(std::bit_cast<float>(0x3d888889u));
    constexpr double GrayBase = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    return glm::vec3(F32(static_cast<double>(random.Next() % GraySpan) * GrayStep + GrayBase));
}

// 0x5313fe..0x53145b: la /2 apunta al hueso 33 (0x630) del personaje dueño, (0, −150, 0) con el modelo del jugador.
Core::Expected<void, std::string> AimAtTheOwnerBone(JointState& joint, const JointKernels::CreationServices* services)
{
    constexpr std::size_t ChestBone = 33;
    constexpr glm::vec3 BelowTheBone{0.0f, -150.0f, 0.0f}; // 0xc3160000
    const ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x531412: falta el personaje dueño del joint 0x614/2"));
    if (services == nullptr || !services->playerBonePoint)
        return Core::Unexpected(std::string("0x531456: falta el modelo del jugador para el joint 0x614/2 (0x4ec110)"));
    joint.targetPosition = services->playerBonePoint(*owner, ChestBone, BelowTheBone);
    return {};
}
} // namespace

namespace
{
// 0x531907..0x5319e5: la /0xf no se dibuja (+0x11 en 0), vive 80 y no tiene puntos. Recorre los 400 personajes y guarda
// en +0x9f4 los lugares de los que están a 400 o menos en el plano (con NaN también), vivos, sin +0x389, de clase 2
// (+0x88), visibles (+0x16c) y que no son el dueño, hasta seis. +0x9eb es la cuenta · 15 (en un byte), +0x9ec vuelve
// a 0 y +0x24 es el segundo punto 150 más arriba.
Core::Expected<void, std::string> CreateTheChain(JointState& joint, const World::FishEffectRequest& request,
                                                 const JointKernels::CreationServices* services)
{
    if (services == nullptr || services->actors.empty())
        return Core::Unexpected(std::string("0x531922: faltan los personajes del joint 0x614/0xf"));
    joint.drawEnabled = 0;
    constexpr std::int32_t ChainLife = 80; // 0x50
    joint.life = ChainLife;
    joint.maximumCount = 0;
    joint.field9ec = 0;
    constexpr double Reach = 400.0;          // 0x6b87b0
    constexpr std::uint8_t MonsterClass = 2; // +0x88
    constexpr std::int32_t MostLinks = 6;
    for (std::size_t slot = 0; slot < services->actors.size(); ++slot)
    {
        const ActorState& actor = services->actors[slot];
        const double dx = D(joint.position.x) - D(actor.position.x);
        const double dy = D(joint.position.y) - D(actor.position.y);
        const double distance = std::sqrt(dy * dy + dx * dx);
        if (!(distance > Reach) && actor.alive && actor.dying == 0 && actor.byte88 == MonsterClass && actor.seen &&
            static_cast<const void*>(&actor) != request.ownerContext)
        {
            const auto link = static_cast<std::size_t>(joint.field9ec);
            joint.chainTargets[link] = static_cast<std::int32_t>(slot);
            if (link + 1 == joint.chainTargets.size())
                joint.skillSerial = static_cast<std::uint8_t>(slot); // el DWORD de +0xa08.
            ++joint.field9ec;
        }
        if (joint.field9ec >= MostLinks)
            break;
    }
    constexpr std::uint8_t LinkSteps = 15;
    joint.volley = static_cast<std::uint8_t>(static_cast<std::uint8_t>(joint.field9ec) * LinkSteps);
    joint.field9ec = 0;
    constexpr double Lift = 150.0; // 0x6b87ac
    joint.pathIncrement = {request.secondPosition.x, request.secondPosition.y, F32(D(request.secondPosition.z) + Lift)};
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateLightningJoint(JointState& joint, const World::FishEffectRequest& request,
                                                       CrtRandom& random,
                                                       const JointKernels::CreationServices* services)
{
    // 0x5309d6..0x5309ec.
    joint.speed = SharedSpeed;
    joint.width = request.scale;
    joint.maximumCount = SharedPoints;
    const glm::vec3 halfBlue{0.5f, 0.5f, 1.0f}; // 0x3f000000 y 0x3f800000
    switch (request.subtype)
    {
    case 0xc: // 0x531356..0x531371: rojo; su paso lee un hueso del modelo del dueño (0x536adf) y corta.
        joint.light = {1.0f, std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu)};
        joint.life = 10;
        break;
    case 0xf:
        if (auto chained = CreateTheChain(joint, request, services); !chained.HasValue())
            return chained;
        break;
    case 0: // 0x5313b1
        joint.life = 2;
        break;
    case 1: // 0x5313c0..0x5313f3
        joint.speed = F32(static_cast<double>(random.Next() % 10) + 20.0);
        joint.life = random.Next() % 8 + 8;
        break;
    case 2: // 0x5313fe..0x53145b
        joint.light = {1.0f, std::bit_cast<float>(0x3dcccccdu), 0.0f};
        joint.life = 2;
        if (auto aimed = AimAtTheOwnerBone(joint, services); !aimed.HasValue())
            return aimed;
        break;
    case 3: // 0x531460..0x53148f y 0x533236: la vida en la key, los puntos y la velocidad en el skill.
        joint.life = static_cast<std::int16_t>(request.key);
        joint.maximumCount = request.skill & 0xff;
        joint.speed = static_cast<float>(request.skill & 0xff);
        joint.key = -1;
        joint.light = halfBlue;
        joint.pathIncrement = joint.position;
        break;
    case 4: // 0x531494..0x5314e8 y 0x53463b
    case 5: // 0x5314ed..0x53155e: hacia atrás, con el destino 600 más arriba.
        joint.speed = request.subtype == 4 ? 60.0f : -60.0f;
        joint.velocity = {};
        joint.light = halfBlue;
        joint.life = 20;
        joint.maximumCount = 10;
        joint.targetPosition = request.secondPosition;
        joint.pathIncrement = joint.position;
        if (request.subtype == 5)
            joint.targetPosition.z = F32(D(joint.targetPosition.z) + 600.0);
        break;
    case 6: // 0x531563..0x53168c y 0x53463b: sin cola común.
        joint.tailEnabled = false;
        joint.life = random.Next() % 20 + 6;
        joint.speed = F32(static_cast<double>(random.Next() % 10) + 15.0);
        joint.light = RandomGray(random);
        joint.pathIncrement = joint.position;
        break;
    case 7: // 0x5315b7..0x531613: textura repetida, tres puntos y vida 1, girado ya hacia el destino.
        joint.repeatedTexture = true;
        joint.speed = 5.0f;
        joint.light = halfBlue;
        joint.pathIncrement = joint.position;
        joint.maximumCount = 3;
        joint.life = 1;
        static_cast<void>(Render::Models::MoveToward(joint.position, joint.angles, joint.targetPosition, 360.0f));
        break;
    case 8: // 0x531618..0x531635 y 0x53204c..0x532073.
        joint.width = 5.0f;
        joint.speed = static_cast<float>(random.Next() % 10 + 6);
        joint.life = random.Next() % 8 + 8;
        joint.maximumCount = 2;
        joint.light = glm::vec3(1.0f);
        break;
    case 9: // 0x53163a..0x53168c y 0x53463b.
        joint.life = 30;
        joint.speed = F32(static_cast<double>(random.Next() % 20) + 80.0);
        joint.light = RandomGray(random);
        joint.pathIncrement = joint.position;
        break;
    case 0xb: // 0x53139d..0x5313bb
        joint.light = {1.0f, 0.5f, std::bit_cast<float>(0x3dcccccdu)};
        joint.life = 2;
        break;
    case 0xd: // 0x531376..0x531398: nace como la /0xb.
        joint.light = {1.0f, std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu)};
        joint.life = 2;
        joint.subtype = 0xb;
        break;
    case 0x10: // 0x531318..0x53134b
        joint.speed = F32(static_cast<double>(random.Next() % 10) + 20.0);
        joint.life = random.Next() % 2 + 2;
        break;
    case 0x11: // 0x530a00..0x530a1f: la vida es el divisor (10).
        joint.life = 10;
        joint.speed = F32(static_cast<double>(random.Next() % 10) + 20.0);
        break;
    default: // 0x5309f3: sin caso, la vida queda la del uso anterior.
        break;
    }
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

namespace
{
// 0x536982: el dueño de la /0, /1, /2 y /0x10 es un personaje; sin dueño apunta a lo que ya tenía.
Core::Expected<void, std::string> FollowTheOwner(JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return {};
    const ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(
            std::format("0x536989: el dueño del joint 0x614/{:#x} no es un personaje", joint.subtype));
    joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + 80.0)};
    return {};
}

// 0x536a15..0x536a93: la /6 apunta 2.050 a 2.250 al costado del objeto dueño y 10.000 más abajo.
Core::Expected<float, std::string> TurnFromTheObject(JointState& joint, CrtRandom& random)
{
    const auto* owner = joint.ownerKind == JointOwnerKind::ByType
                            ? static_cast<const World::WorldObject*>(joint.ownerContext)
                            : nullptr;
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x536a15: falta el objeto dueño del joint 0x614/6"));
    constexpr double Offset = 2050.0; // 0x6ba308
    constexpr double Depth = 10000.0; // 0x6ba304
    joint.targetPosition = owner->transform.position;
    joint.targetPosition.x = F32((static_cast<double>(random.Next() % 200) + Offset) + D(joint.targetPosition.x));
    joint.targetPosition.y = F32((static_cast<double>(random.Next() % 200) + Offset) + D(joint.targetPosition.y));
    joint.targetPosition.z = F32(D(joint.targetPosition.z) - Depth);
    const auto turn = static_cast<float>(random.Next() % 100 + 50);
    return F32(Render::Models::MoveToward(joint.position, joint.angles, joint.targetPosition, turn));
}

// 0x53695e..0x536b3a: hacia dónde gira cada subtipo; devuelve lo que falta (el float de 0x536b3a).
Core::Expected<float, std::string> Turn(JointState& joint, const glm::vec3& scratch, CrtRandom& random)
{
    using Render::Models::MoveToward;
    switch (joint.subtype)
    {
    case 0:
    case 1:
    case 2:
    case 0x10:
        if (auto followed = FollowTheOwner(joint); !followed.HasValue())
            return Core::Unexpected(followed.Error());
        return F32(MoveToward(joint.position, joint.angles, joint.targetPosition, 50.0f));
    case 3: // 0x5369b3
        return F32(MoveToward(joint.position, joint.angles, joint.targetPosition, 50.0f));
    case 4: // 0x5369bd: hacia 300 debajo del destino, girando −10.
    {
        const glm::vec3 below{joint.targetPosition.x, joint.targetPosition.y, F32(D(joint.targetPosition.z) - 300.0)};
        return F32(MoveToward(joint.position, joint.angles, below, -10.0f));
    }
    case 5: // 0x5369e7: desde 600 encima del destino hacia la posición, girando −10.
    {
        const glm::vec3 above{joint.targetPosition.x, joint.targetPosition.y, F32(D(joint.targetPosition.z) + 600.0)};
        return F32(MoveToward(above, joint.angles, joint.position, -10.0f));
    }
    case 6:
        return TurnFromTheObject(joint, random);
    case 7: // 0x536a98
    {
        const auto turn = static_cast<float>(random.Next() % 100 + 50);
        return F32(MoveToward(joint.position, joint.angles, joint.targetPosition, turn));
    }
    case 0xc: // 0x536adf..0x536b1d: 0x4ec110 con el modelo del tipo del dueño y los huesos de 0x743751c (sin leer).
        return Core::Unexpected(std::string(
            "0x536b1d: el joint 0x614/0xc pone su posición en un hueso del modelo del dueño (0x4ec110 con 0x743751c), "
            "sin portar (CL11)"));
    case 9: // 0x536abc: hacia el punto sorteado antes del lazo.
    {
        const auto turn = F32(static_cast<double>(random.Next() % 80) + 60.0);
        return F32(MoveToward(joint.position, joint.angles, scratch, turn));
    }
    default: // 0x536b24
        return F32(MoveToward(joint.position, joint.angles, joint.targetPosition, 25.0f));
    }
}

// 0x536b40..0x536cfb: la sacudida de cada subtipo en vx y vz.
void Jitter(JointState& joint, CrtRandom& random)
{
    const auto centered = [&random](int mask, int center) { return (random.Next() & mask) - center; };
    switch (joint.subtype)
    {
    case 1:
        joint.velocity.x = static_cast<float>(centered(0xff, 0x80));
        joint.velocity.z = static_cast<float>(centered(0xff, 0x80));
        return;
    case 4:
        joint.velocity.x = static_cast<float>(centered(0x3f, 0x20));
        return;
    case 5:
        joint.velocity.x = static_cast<float>(centered(0x1f, 0x10));
        return;
    case 6:
    case 7:
        joint.velocity.x = static_cast<float>(random.Next() % 100 + 20);
        joint.velocity.z = static_cast<float>(random.Next() % 100 + 20);
        return;
    case 0xb: // 0x536c60: · 0,7f (0x6b8a5c) antes de dividir por el ancho.
    {
        constexpr double Softer = static_cast<double>(std::bit_cast<float>(0x3f333333u));
        joint.velocity.x = F32(static_cast<double>(centered(0x3ff, 0x200)) * Softer / D(joint.width));
        joint.velocity.z = F32(static_cast<double>(centered(0x3ff, 0x200)) * Softer / D(joint.width));
        return;
    }
    default:
        joint.velocity.x = F32(static_cast<double>(centered(0x3ff, 0x200)) / D(joint.width));
        joint.velocity.z = F32(static_cast<double>(centered(0x3ff, 0x200)) / D(joint.width));
        return;
    }
}

// 0x536d8e..0x536e32: con el ancho en 50 o más, la luz dinámica de radio 2 de la /0, la /0xb y la /2.
Core::Expected<void, std::string> Glow(const JointState& joint, CrtRandom& random, World::TerrainLighting* lighting)
{
    if (!(joint.width >= 50.0f))
        return {};
    const std::int32_t subtype = joint.subtype;
    if (subtype != 0 && subtype != 0xb && subtype != 2)
        return {};
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3d23d70au));  // 0x6b8aec, 0,04f
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8
    constexpr double Red = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));   // 0x6b87b4, 0,4f
    const double value = static_cast<double>((random.Next() & 3) + 4) * Step;
    const glm::vec3 color = subtype == 2 ? glm::vec3(F32(value * Red), F32(value * Tenth), F32(value * Tenth))
                                         : glm::vec3(F32(value * Tenth), F32(value * Tenth), F32(value * 0.5));
    return World::AddDynamicLight(lighting, joint.position, color, 2, "0x536e2d");
}

Core::Expected<void, std::string> Particle(const JointState& joint, std::int32_t type,
                                           const JointKernels::StepServices& services, std::string_view address)
{
    if (services.particles == nullptr)
        return Core::Unexpected(std::format("{}: faltan las partículas del joint 0x614/{:#x}", address, joint.subtype));
    const auto created = services.particles->CreateChecked(type, joint.position, joint.angles, joint.light, 0, 1.0f,
                                                           services.worldClock, *services.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: falló la partícula {:#x} del joint 0x614/{:#x} ({})", address, type,
                                            joint.subtype, static_cast<int>(created.Error())));
    return {};
}

// 0x536e95..0x537042: al llegar, sólo con el ancho en 50 justo (0x536e9c: con otro ancho salta a 0x537045, también
// la /0), el 0x5ca y una vez cada ocho el 0x5f2 (tres rand() para un punto que nadie lee); la /0 en la primera mitad de
// cada segundo, una vez cada dieciséis, suelta una /1 hacia el destino.
Core::Expected<void, std::string> Arrive(JointState& joint, const JointKernels::StepServices& services)
{
    CrtRandom& random = *services.random;
    constexpr std::uint32_t ArrivalWidthBits = 0x42480000u; // 50.0f, el inmediato de 0x536e95.
    if (std::bit_cast<std::uint32_t>(joint.width) != ArrivalWidthBits)
        return {};
    if (auto made = Particle(joint, World::ThunderParticle, services, "0x536eb9"); !made.HasValue())
        return made;
    if (random.Next() % 8 == 0)
    {
        for (int axis = 0; axis < 3; ++axis)
            static_cast<void>(random.Next());
        if (auto made = Particle(joint, World::BreathParticle, services, "0x536f58"); !made.HasValue())
            return made;
    }
    if (joint.subtype != 0)
        return {};
    const auto clock = World::OriginalFtolLow(services.worldClock);
    if (!clock)
        return Core::Unexpected(std::string("0x536f71: el reloj del joint 0x614/0 excede ftol"));
    if (*clock % 1000 >= 500 || random.Next() % 16 != 0)
        return {};
    const auto x = random.Next() % 100 - 50;
    const auto y = random.Next() % 100 - 50;
    const auto z = random.Next() % 120 - 60;
    glm::vec3 from{F32(static_cast<double>(x) + D(joint.targetPosition.x)),
                   F32(static_cast<double>(y) + D(joint.targetPosition.y)),
                   F32(static_cast<double>(z) + D(joint.targetPosition.z))};
    const float scale = F32(static_cast<double>(random.Next() % 8) + 6.0);
    glm::vec3 to = joint.targetPosition;
    glm::vec3 angles = joint.angles;
    const World::FishEffectRequest branch{.type = LightningJoint,
                                          .firstPosition = from,
                                          .secondPosition = to,
                                          .angles = angles,
                                          .subtype = 1,
                                          .ownerContext = joint.ownerContext,
                                          .scale = scale,
                                          .key = -1,
                                          .skill = 0,
                                          .skillSerial = 0,
                                          .target = -1};
    const auto kind = joint.ownerKind;
    if (auto created = CreateFaunaJoint(services.joints, branch, random, kind); !created.HasValue())
        return Core::Unexpected(std::format("0x53703d: {}", created.Error()));
    return {};
}
// El primer rand() de cada subtipo en el giro (0x536a2c, 0x536a98, 0x536abc) y en la sacudida (0x536b48..0x536cb3).
std::uint32_t FirstTurnRandom(std::int32_t subtype)
{
    switch (subtype)
    {
    case 6:
        return 0x536a2c;
    case 7:
        return 0x536a98;
    case 9:
        return 0x536abc;
    default:
        return 0;
    }
}

std::uint32_t FirstJitterRandom(std::int32_t subtype)
{
    switch (subtype)
    {
    case 1:
        return 0x536b48;
    case 4:
        return 0x536b94;
    case 5:
        return 0x536bbe;
    case 0xb:
        return 0x536c60;
    default:
        return 0x536cb3;
    }
}

// Sin el RNG de MuRet: hace lo que el paso hace antes del primer rand() y corta ahí (el original siempre lo tiene).
Core::Expected<void, std::string> StepWithoutRandom(JointState& joint)
{
    const std::int32_t subtype = joint.subtype;
    const auto cut = [subtype](std::uint32_t address)
    { return Core::Unexpected(std::format("{:#x}: falta el RNG del joint 0x614/{:#x}", address, subtype)); };
    if (subtype == 4)
    {
        joint.position = joint.targetPosition;
        return cut(0x5368a2);
    }
    if (subtype == 9)
        return cut(0x5368f6);
    if (subtype == 5)
        joint.position = joint.targetPosition;
    else if (subtype == 6 || subtype == 7)
        joint.position = joint.pathIncrement;
    if (joint.maximumCount <= 0)
    {
        if (subtype == 7)
            joint.position = joint.targetPosition;
        return {};
    }
    if (const std::uint32_t address = FirstTurnRandom(subtype); address != 0)
        return cut(address);
    CrtRandom unused(0); // Estos giros no sacan rand().
    if (auto turned = Turn(joint, glm::vec3(0.0f), unused); !turned.HasValue())
        return Core::Unexpected(turned.Error());
    return cut(FirstJitterRandom(subtype));
}
} // namespace

namespace
{
// 0x53706d..0x5370e8: la /0xf, con la vida par, suelta dos 0x614/0 sin dueño de ancho 50 y 10 desde la posición hasta
// +0x24 con sus ángulos; después la partícula 0x5ca/0 con la luz de [ebp−0x18], que en este camino nadie escribe: es la
// que dejó el paso del joint anterior (Q-C1622), y corta. Lo que sigue (0x5370ed..0x537278: los joints hacia los
// personajes de +0x9f4 cada 15 de +0x9ec, que crece de a uno con la vida par hasta +0x9eb) queda documentado en 06.
Core::Expected<void, std::string> StepTheChain(JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.life % 2 != 0)
        return {};
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x5370a8: falta el RNG del joint 0x614/0xf"));
    for (const float width : {50.0f, 10.0f}) // 0x42480000 (0x5370a8) y 0x41200000 (0x5370c9)
    {
        const World::FishEffectRequest strike{.type = LightningJoint,
                                              .firstPosition = joint.position,
                                              .secondPosition = joint.pathIncrement,
                                              .angles = joint.angles,
                                              .subtype = 0,
                                              .ownerContext = nullptr,
                                              .scale = width,
                                              .key = -1,
                                              .skill = 0,
                                              .skillSerial = 0,
                                              .target = -1};
        if (auto created = CreateFaunaJoint(services.joints, strike, *services.random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", width > 10.0f ? 0x5370a8 : 0x5370c9, created.Error()));
    }
    return Core::Unexpected(std::string("0x5370e8: la luz de la partícula 0x5ca del joint 0x614/0xf es la que dejó el "
                                        "joint anterior en [ebp-0x18] (indefinido, Q-C1622)"));
}
} // namespace

Core::Expected<void, std::string> StepLightningJoint(JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    // 0x536956/0x536964: la /0xf no gira (sin puntos y con su salto) y va a 0x537064.
    if (subtype == 0xf)
        return StepTheChain(joint, services);
    // 0x536841..0x536856: la /6 sólo se rehace con la vida en 4 o menos; la /8 no tiene caso.
    if ((subtype == 6 && joint.life > 4) || subtype == 8)
        return {};
    if (services.random == nullptr)
        return StepWithoutRandom(joint);
    CrtRandom& random = *services.random;
    glm::vec3 scratch{}; // [ebp−0xc]: el punto de la /4 y de la /9, y después el avance de cada tramo.
    switch (subtype)
    {
    case 4: // 0x53686f..0x5368f1: en el destino, con el sprite 0x5fd 30 más arriba.
    {
        joint.position = joint.targetPosition;
        scratch = {joint.targetPosition.x, joint.targetPosition.y, F32(D(joint.targetPosition.z) + 30.0)};
        const auto rotation = static_cast<float>(random.Next() % 360);
        const auto scale = F32(static_cast<double>((random.Next() & 7) + 8) * D(std::bit_cast<float>(0x3e4ccccdu)));
        if (services.sprites == nullptr)
            return Core::Unexpected(std::string("0x5368ec: faltan los sprites del joint 0x614/4"));
        static_cast<void>(services.sprites->Create(0x5fd, scratch, scale, joint.light, rotation, 0));
        break;
    }
    case 5: // 0x53686f
        joint.position = joint.targetPosition;
        break;
    case 9: // 0x5368f6..0x536936: un punto a ±100 del destino en x e y.
        scratch.x = F32((static_cast<double>(random.Next() % 200) + D(joint.targetPosition.x)) - 100.0);
        scratch.y = F32((static_cast<double>(random.Next() % 200) + D(joint.targetPosition.y)) - 100.0);
        scratch.z = joint.targetPosition.z;
        joint.position = joint.pathIncrement;
        break;
    case 6:
    case 7: // 0x536939: desde la partida.
        joint.position = joint.pathIncrement;
        break;
    default:
        break;
    }
    // 0x53694a..0x536e8a: un tramo por punto, salvo que llegue.
    for (std::int32_t iteration = 0; iteration < joint.maximumCount; ++iteration)
    {
        const auto distance = Turn(joint, scratch, random);
        if (!distance.HasValue())
            return Core::Unexpected(distance.Error());
        Jitter(joint, random);
        const glm::vec3 shaken{F32(D(joint.angles.x) + D(joint.velocity.x)),
                               F32(D(joint.velocity.y) + D(joint.angles.y)),
                               F32(D(joint.velocity.z) + D(joint.angles.z))};
        const auto matrix = Render::Models::OriginalAngleMatrix(shaken);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        if (subtype == 3)
        {
            // 0x536d4d: lejos (más de 150; con NaN no) la vida va a 0, pero no llega.
            if (distance.Value() > 150.0f)
                joint.life = 0;
        }
        else if (D(joint.speed) * 1.5 > D(distance.Value()) && subtype != 6 && subtype != 9 && subtype != 7)
        {
            if (auto arrived = Arrive(joint, services); !arrived.HasValue())
                return arrived;
            break;
        }
        if (auto lit = Glow(joint, random, services.lighting); !lit.HasValue())
            return lit;
        // 0x536e35..0x536e56: el avance (0, −velocidad, 0) se arma en [ebp−0xc], la misma local del punto de la /9:
        // desde el segundo tramo la /9 gira hacia ese avance y no hacia el punto sorteado.
        scratch = {0.0f, -joint.speed, 0.0f};
        const auto moved = Render::Models::OriginalVectorRotate(scratch, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    }
    // 0x537045..0x53705f: la /7 termina en el destino.
    if (subtype == 7)
        joint.position = joint.targetPosition;
    return {};
}
} // namespace Client::Scenes::JointFamilies
