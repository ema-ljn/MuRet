#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// El 0x613 y el 0x6ee: comparten el alta (0x53265e, la mezcla en 3 y la tabla 0x534f18 por subtipo) y el paso
// (0x538d77). Los sprites del paso son los de 0x5491e0 (el dueño que reciben no lo guarda MuRet).
namespace Client::Scenes::JointFamilies
{
namespace
{
using JointKernels::D;
using JointKernels::F32;
using Render::Models::CrtRandom;

constexpr std::uint8_t Subtractive = 3;
constexpr std::uint8_t Lighter = 1;
constexpr std::uint8_t SecondFace = 2;
constexpr std::uint16_t SpiritSprite = 0x5ac;
constexpr std::uint16_t GlowSprite = 0x5fd;
constexpr std::int32_t FullTurn = 360;

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// 0x532675 y 0x532bab: el +0xf04 del registro propio con un acceso abierto y cerrado alrededor.
Core::Expected<void, std::string> ReadTheVolley(JointState& joint, CrtRandom& random,
                                                const JointKernels::CreationServices* services, std::uint32_t address)
{
    if (services == nullptr || services->ownRecordDepth == nullptr || services->ownVolley == nullptr)
        return Core::Unexpected(
            std::format("{:#x}: falta el registro propio del joint {:#x}/{:#x}", address, joint.type, joint.subtype));
    const Render::Models::LogicalAccess own(*services->ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    joint.volley = *services->ownVolley;
    return {};
}

// 0x5331a9..0x5331b7 y 0x53463b..0x534649: +0x24 guarda la partida.
void KeepTheStart(JointState& joint)
{
    joint.pathIncrement = joint.position;
}

// 0x533208..0x53323b y 0x53326c..0x5332ac (con 0x534633): la luz de la /6 y la /7 según la skill (+0x9ea).
void LightBySkill(JointState& joint)
{
    if (joint.skill == 0)
        joint.light = {Bits(0x3e99999au), Bits(0x3e99999au), 1.0f};
    else if (joint.skill == 1)
    {
        joint.speed = 10.0f;
        joint.light = glm::vec3(0.5f);
    }
}
} // namespace

Core::Expected<void, std::string> CreateSpiritJoint(JointState& joint, const World::FishEffectRequest& request,
                                                    CrtRandom& random, const JointKernels::CreationServices* services)
{
    const bool spirit = joint.type == SpiritJoint;
    joint.blend = Subtractive; // 0x53265e
    switch (request.subtype)
    {
    case 0: // 0x532675..0x532b45
        if (auto read = ReadTheVolley(joint, random, services, 0x532675); !read.HasValue())
            return read;
        joint.speed = 70.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 6;
        break;
    case 0x13: // 0x532b64..0x532ba6
        if (spirit)
            joint.blend = Lighter;
        joint.speed = 70.0f;
        joint.light = {Bits(0x3dcccccdu), 0.5f, Bits(0x3e4ccccdu)};
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 9;
        break;
    case 5: // 0x532bab..0x53307f: la mezcla antes del acceso.
        joint.blend = Lighter;
        if (auto read = ReadTheVolley(joint, random, services, 0x532bab); !read.HasValue())
            return read;
        joint.speed = 30.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 12;
        break;
    case 1: // 0x53309e..0x5330cd: y suena 0x3e.
    {
        constexpr std::int32_t BirthSound = 0x3e;
        joint.speed = 70.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 6;
        if (services != nullptr && services->soundRequest)
            services->soundRequest(BirthSound);
        break;
    }
    case 3: // 0x5330d2..0x533127
        joint.speed = 140.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 10;
        if (spirit)
        {
            joint.blend = Lighter;
            joint.light = {1.0f, 0.5f, Bits(0x3dcccccdu)};
        }
        else
            joint.light = glm::vec3(1.0f);
        break;
    case 0xd: // 0x53312c..0x53316e
        if (spirit)
            joint.blend = Lighter;
        joint.speed = 40.0f;
        joint.light = {1.0f, Bits(0x3f19999au), Bits(0x3ecccccdu)};
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 10;
        break;
    case 2:
    case 0x15: // 0x533173..0x5331ba
        joint.blend = Lighter;
        joint.width = request.scale;
        joint.drawEnabled = SecondFace;
        joint.speed = 50.0f;
        joint.light = glm::vec3(0.5f);
        joint.life = 20;
        joint.maximumCount = 3;
        KeepTheStart(joint);
        break;
    case 6: // 0x5331bf..0x533253: la key en rand() % 5; con otra que 0, sin cola.
        joint.blend = Lighter;
        joint.drawEnabled = SecondFace;
        joint.speed = 50.0f;
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 5;
        joint.key = static_cast<std::int16_t>(random.Next() % 5);
        if (joint.key != 0)
            joint.tailEnabled = false;
        LightBySkill(joint);
        KeepTheStart(joint);
        break;
    case 7: // 0x533258..0x5332b3
        joint.drawEnabled = 0;
        joint.speed = 10.0f;
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 5;
        LightBySkill(joint);
        KeepTheStart(joint);
        break;
    case 8: // 0x5333be
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 15;
        break;
    case 0xa: // 0x5333da
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 6;
        break;
    case 4:
    case 9:
        return Core::Unexpected(std::format("{:#x}: la creación del joint {:#x}/{} no está portada",
                                            request.subtype == 4 ? 0x5332b8 : 0x5333f6, request.type, request.subtype));
    case 0xb: // 0x53344e..0x533495: quietos, de un punto.
    case 0xc:
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x11:
    {
        // 0xb..0x11, con la 0xd fuera: 0x53344e, 0x533474, 0x53349a, 0x5334c7, 0x5334ed y 0x533513.
        constexpr std::array<std::int32_t, 7> Lives{50, 30, 0, 10, 20, 50, 100};
        joint.speed = 0.0f;
        joint.life = Lives[static_cast<std::size_t>(request.subtype - 0xb)];
        joint.width = request.scale;
        joint.maximumCount = 1;
        if (request.subtype == 0xe)
            joint.angles.x = 20.0f; // 0x5334bb
        break;
    }
    case 0x12: // 0x533539..0x533574
        if (!spirit)
            joint.blend = Lighter;
        joint.speed = 50.0f;
        joint.light = glm::vec3(Bits(0x3f333333u));
        joint.life = 39;
        joint.width = request.scale;
        joint.maximumCount = 15;
        break;
    case 0x14: // 0x533579..0x533596
        if (!spirit)
            joint.blend = Lighter;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 15;
        break;
    default: // 0x532668: más allá de 0x15, sólo la mezcla.
        break;
    }
    return {};
}

namespace
{
Core::Expected<void, std::string> Sprite(const JointKernels::StepServices& services, std::uint16_t texture,
                                         const glm::vec3& position, float scale, const glm::vec3& light, float rotation,
                                         std::uint32_t address)
{
    if (services.sprites == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan los sprites del joint 0x613", address));
    static_cast<void>(services.sprites->Create(texture, position, scale, light, rotation, 0));
    return {};
}

// 0x539600..0x539641 y 0x5398be..0x5398ff: la luz por 0,8333333f (0x6b8b98) en float; la local queda igual.
void FadeTheLight(JointState& joint)
{
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * Fading);
}

// 0x538d99..0x538dda: la /1, el sprite 0x5ac de escala 4 con la luz (0,8; 0,4; 1) y un giro rand() % 360.
Core::Expected<void, std::string> StepTheSpark(const JointState& joint, const JointKernels::StepServices& services)
{
    const glm::vec3 local{Bits(0x3f4ccccdu), Bits(0x3ecccccdu), 1.0f};
    const auto rotation = static_cast<float>(services.random->Next() % FullTurn);
    return Sprite(services, SpiritSprite, joint.position, 4.0f, local, rotation, 0x538dda);
}

// 0x5396ff: la gota 0x60f/2 del 0x613/2, sin dueño, de ancho 40, en el punto y con los ángulos de la vuelta.
Core::Expected<void, std::string> DropTheDrop(const JointKernels::StepServices& services, glm::vec3 point,
                                              glm::vec3 angles)
{
    constexpr float Width = 40.0f; // 0x42200000
    const World::FishEffectRequest drop{.type = DropJoint,
                                        .firstPosition = point,
                                        .secondPosition = point,
                                        .angles = angles,
                                        .subtype = DropSubtype,
                                        .ownerContext = nullptr,
                                        .scale = Width,
                                        .key = -1,
                                        .skill = 0,
                                        .skillSerial = 0,
                                        .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, drop, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x5396ff: {}", created.Error()));
    return {};
}

// 0x539586..0x539775: la /2, la /6, la /7 y la /0x15. La luz local por subtipo (la /6 y la /7 por la skill); con vida
// de menos de 10, la propia se apaga y la local es ella; con vida 19, la /2 y la /0x15 sortean un punto alrededor de
// la partida (+0x24) y la /2 suelta allí la gota. Después el sprite 0x5ac, de escala (20 − vida) / 5 + 4, y la
// velocidad sube 5.
Core::Expected<void, std::string> StepTheCross(JointState& joint, const JointKernels::StepServices& services)
{
    CrtRandom& random = *services.random;
    std::optional<glm::vec3> local;
    if (joint.subtype == 2)
        local = glm::vec3{1.0f, 0.5f, Bits(0x3dcccccdu)}; // 0x53959d
    else if (joint.subtype == 0x15)
        local = glm::vec3{Bits(0x3dcccccdu), 0.5f, 1.0f}; // 0x5395b4
    else if (joint.skill == 0)
        local = glm::vec3{Bits(0x3e99999au), Bits(0x3e99999au), 1.0f}; // 0x5395eb
    else if (joint.skill == 1)
        local = glm::vec3(0.5f); // 0x5395d4
    constexpr std::int32_t FadeLife = 10, DropLife = 19;
    if (joint.life < FadeLife)
    {
        FadeTheLight(joint);
        local = joint.light;
    }
    else if (joint.life == DropLife && (joint.subtype == 2 || joint.subtype == 0x15))
    {
        // 0x539669..0x5396d8: los ángulos (0, 0, rand() % 360) y el punto (±100, ±100, −200) desde la partida.
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        angles.z = static_cast<float>(random.Next() % FullTurn);
        glm::vec3 point{};
        point.x = F32((static_cast<double>(random.Next() % 200) + D(joint.pathIncrement.x)) - 100.0);
        point.y = F32((static_cast<double>(random.Next() % 200) + D(joint.pathIncrement.y)) - 100.0);
        point.z = F32(D(joint.pathIncrement.z) - 200.0);
        if (joint.subtype == 2)
            if (auto dropped = DropTheDrop(services, point, angles); !dropped.HasValue())
                return dropped;
    }
    const auto rotation = static_cast<float>(random.Next() % FullTurn); // 0x539709, antes de leer la luz.
    if (!local)
        return Core::Unexpected(
            std::format("0x53975b: la luz local del joint {:#x}/{} con la skill {} queda sin escribir (sin portar)",
                        joint.type, joint.subtype, joint.skill));
    const auto growth = std::bit_cast<std::int32_t>(20u - std::bit_cast<std::uint32_t>(joint.life)) / 5;
    const float scale = F32(static_cast<double>(growth) + 4.0);
    if (auto drawn = Sprite(services, SpiritSprite, joint.position, scale, *local, rotation, 0x53975b);
        !drawn.HasValue())
        return drawn;
    joint.speed = F32(D(joint.speed) + 5.0);
    return {};
}

// 0x539941..0x5399a2: el desvío de 0x53d1a0(lugar, dir, 1): el contador compartido con su acceso (4 rand() al
// cerrarlo en 0) y la dirección de la fase lugar + contador, por 50. La x suma el producto sin redondear.
Core::Expected<void, std::string> Wander(JointState& joint, const JointKernels::StepServices& services)
{
    if (services.cloth == nullptr)
        return Core::Unexpected(
            std::format("0x53994e: falta el contador compartido del joint {:#x}/{}", joint.type, joint.subtype));
    const auto steps = std::bit_cast<std::uint32_t>(services.cloth->logicSteps);
    if (services.cloth->openCounterAccesses == 0)
        for (std::uint16_t draw = 0; draw < Render::Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(services.random->Next());
    const auto phase = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(services.index) + steps);
    const auto direction = Render::Models::Detail::FaunaDirectionPC53(phase);
    if (!direction)
        return Core::Unexpected(std::string("0x53994e: dirección fuera del perfil PC53"));
    constexpr double Reach = 50.0;
    const float y = F32(D((*direction)[1]) * Reach);
    const float z = F32(D((*direction)[2]) * Reach);
    joint.position.x = F32(D((*direction)[0]) * Reach + D(joint.position.x));
    joint.position.y = F32(D(y) + D(joint.position.y));
    joint.position.z = F32(D(z) + D(joint.position.z));
    return {};
}

// 0x53977a..0x539a09: la /3 y la /0xd. La /0xd con vida de 28 o menos busca a su dueño: medio giro de 20 en x con
// rand() & 1 en 0, un punto ±70 alrededor del dueño y 50 más arriba, 0x4e9810 con la velocidad − 20, la velocidad + 1
// hasta 50, una vida menos cerca del punto, la luz local es la propia y la escala (vida / 10 + 1) · 2. Si no, el
// desvío y la luz (1; 0,5; 0,1) con escala 3. Después, los sprites 0x5ac y 0x5fd (a media escala).
Core::Expected<void, std::string> StepTheBolt(JointState& joint, const JointKernels::StepServices& services)
{
    CrtRandom& random = *services.random;
    float scale = 3.0f; // [ebp−0x30]
    std::optional<glm::vec3> local;
    constexpr std::int32_t SeekLife = 28;
    if (joint.subtype == 0xd && joint.life <= SeekLife)
    {
        if (joint.ownerContext != nullptr)
        {
            const ActorState* owner = JointKernels::ActorOwner(joint);
            if (owner == nullptr)
                return Core::Unexpected(std::string("0x5397dd: el dueño del joint 0x613/0xd no es un personaje"));
            if ((random.Next() & 1) == 0)
                joint.angles.x = F32(joint.angles.x >= -90.0f ? D(joint.angles.x) - 20.0 : D(joint.angles.x) + 20.0);
            glm::vec3 target = owner->position;
            target.x = F32((static_cast<double>(random.Next() % 150) - 70.0) + D(target.x));
            target.y = F32((static_cast<double>(random.Next() % 150) - 70.0) + D(target.y));
            target.z = F32(D(target.z) + 50.0);
            const double distance =
                Render::Models::MoveToward(joint.position, joint.angles, target, F32(D(joint.speed) - 20.0));
            joint.speed = joint.speed > 50.0f ? 50.0f : F32(D(joint.speed) + 1.0);
            if (!(distance >= 50.0))
                joint.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) - 1u);
            if (joint.life < 10)
                FadeTheLight(joint);
            local = joint.light;
            scale = F32((static_cast<double>(joint.life / 10) + 1.0) * 2.0);
        }
    }
    else
    {
        if (auto wandered = Wander(joint, services); !wandered.HasValue())
            return wandered;
        local = glm::vec3{1.0f, 0.5f, Bits(0x3dcccccdu)};
    }
    const auto first = static_cast<float>(random.Next() % FullTurn); // 0x5399a4, antes de leer la luz.
    if (!local)
        return Core::Unexpected(
            std::string("0x5399cd: la luz local del joint 0x613/0xd sin dueño queda sin escribir (sin portar)"));
    if (auto drawn = Sprite(services, SpiritSprite, joint.position, scale, *local, first, 0x5399cd); !drawn.HasValue())
        return drawn;
    const auto second = static_cast<float>(random.Next() % FullTurn);
    return Sprite(services, GlowSprite, joint.position, F32(D(scale) * 0.5), *local, second, 0x539a09);
}

// 0x538e1e..0x538e7f: la /8 sube de 110 en x con velocidad 10 hasta la vida 41; después, con velocidad 50, la x va
// de 180 a 360 de a rand() % 3 + 2.
void StepTheArc(JointState& joint, CrtRandom& random)
{
    constexpr std::int32_t RiseLife = 42;
    if (joint.life >= RiseLife)
    {
        joint.speed = 10.0f;
        joint.angles.x = 110.0f;
        return;
    }
    joint.speed = 50.0f;
    if (!(joint.angles.x > 180.0f))
        joint.angles.x = 180.0f;
    if (!(joint.angles.x > 360.0f))
        joint.angles.x = F32((static_cast<double>(random.Next() % 3) + D(joint.angles.x)) + 2.0);
}
} // namespace

Core::Expected<void, std::string> StepSpiritJoint(JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    if (subtype == 0 || subtype == 5 || subtype == 0x13)
        return Core::Unexpected(std::format(
            "0x539a0e: el paso del joint {:#x}/{:#x} (el efecto 0xd0 con ancho 80, el asedio de 0x4294b0, los blancos "
            "de 0x517a20 y la altura de 0x5f93c0) no está portado (CL11)",
            joint.type, subtype));
    if (subtype == 0xa)
    {
        // 0x538e89..0x538e9f: la z de los ángulos − 50 y velocidad 50.
        joint.angles.z = F32(D(joint.angles.z) - 50.0);
        joint.speed = 50.0f;
        return {};
    }
    // 0x538d77..0x539505: la cadena de comparaciones no tiene caso para los subtipos mayores que 0x15; van directo a
    // la cola 0x53c393 sin hacer nada.
    constexpr std::int32_t LastSpiritSubtype = 0x15;
    if (subtype > LastSpiritSubtype)
        return {};
    if (subtype == 0xe)
    {
        // 0x53909f..0x539125: la x de los ángulos − 2 (0x6b8818), la luz local en 1 y rand() % 5: con 0 la partícula
        // 0x6f2/2 (0x5390e6), con 1 o 2 la 0x6f3/2 (0x539109) y si no la 0x6f4/4 (0x539120), en la posición con los
        // ángulos y la luz +0x48 del joint. Las tres son del lote de partículas de la CL11.
        if (services.random == nullptr)
            return Core::Unexpected(std::format("0x5390c2: falta el RNG del joint {:#x}/0xe", joint.type));
        constexpr double Tilt = 2.0; // 0x6b8818
        joint.angles.x = F32(D(joint.angles.x) - Tilt);
        const std::int32_t pick = services.random->Next() % 5;
        const std::uint32_t address = pick == 0 ? 0x5390e6u : (pick <= 2 ? 0x539109u : 0x539120u);
        const std::int32_t particle = pick == 0 ? 0x6f2 : (pick <= 2 ? 0x6f3 : 0x6f4);
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} del joint {:#x}/0xe no está portada (CL11)",
                                            address, particle, pick > 2 ? 4 : 2, joint.type));
    }
    const bool ported = subtype == 1 || subtype == 2 || subtype == 3 || subtype == 6 || subtype == 7 || subtype == 8 ||
                        subtype == 0xd || subtype == 0x15;
    if (!ported)
        return Core::Unexpected(
            std::format("0x538ea4: el paso del joint {:#x}/{:#x} no está portado (CL11)", joint.type, subtype));
    if (services.random == nullptr)
        return Core::Unexpected(std::format("0x538d77: falta el RNG del joint {:#x}/{:#x}", joint.type, subtype));
    switch (subtype)
    {
    case 1:
        return StepTheSpark(joint, services);
    case 3:
    case 0xd:
        return StepTheBolt(joint, services);
    case 8:
        StepTheArc(joint, *services.random);
        return {};
    default:
        return StepTheCross(joint, services);
    }
}
} // namespace Client::Scenes::JointFamilies
