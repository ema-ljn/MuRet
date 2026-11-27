#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "Scenes/RingEffects.h"
#include "Scenes/SkillHits.h"
#include "Math/Ftol.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/TerrainAttributeQuery.h"
#include "World/WizardModelEffects.h"
#include "World/WorldObjectStepServices.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>

// El 0x613 y el 0x6ee: comparten el alta (0x53265e, la mezcla en 3 y la tabla 0x534f18 por subtipo) y el paso
// (0x538d77). Los sprites del paso son los de 0x5491e0 (el dueño que reciben no lo guarda MuRet).
namespace Client::Scenes::JointFamilies
{
namespace
{
using Crt::CrtRandom;
using Math::D;
using Math::F32;

constexpr std::uint8_t Subtractive = 3;
constexpr std::uint8_t Lighter = 1;
constexpr std::uint8_t SecondFace = 2;
constexpr std::uint16_t SpiritSprite = 0x5ac;
constexpr std::uint16_t GlowSprite = 0x5fd;
using Math::FullTurn;

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
    const Models::LogicalAccess own(*services->ownRecordDepth, random, Models::OwnRecordCloseRandoms);
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
        constexpr Audio::SoundId BirthSound{0x3e};
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
    case 9: // 0x5332b8 y 0x5333f6: FaunaJointCreation.cpp los crea antes (CreateHelperBeam y CreateMeteor).
        return Core::Unexpected(std::format("{:#x}: el joint {:#x}/{} no llega a CreateSpiritJoint",
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
        for (std::uint16_t draw = 0; draw < Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(services.random->Next());
    const auto phase = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(services.index) + steps);
    const auto direction = Math::Detail::FaunaDirectionPC53(phase);
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
            const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
            if (owner == nullptr)
                return Core::Unexpected(std::string("0x5397dd: el dueño del joint 0x613/0xd no es un personaje"));
            if ((random.Next() & 1) == 0)
                joint.angles.x = F32(joint.angles.x >= -90.0f ? D(joint.angles.x) - 20.0 : D(joint.angles.x) + 20.0);
            glm::vec3 target = owner->position;
            target.x = F32((static_cast<double>(random.Next() % 150) - 70.0) + D(target.x));
            target.y = F32((static_cast<double>(random.Next() % 150) - 70.0) + D(target.y));
            target.z = F32(D(target.z) + 50.0);
            const double distance = Math::MoveToward(joint.position, joint.angles, target, F32(D(joint.speed) - 20.0));
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

namespace
{
Core::Expected<CrtRandom*, std::string> RandomOf(const JointState& joint, const JointKernels::StepServices& services,
                                                 std::uint32_t address)
{
    if (services.random == nullptr)
        return Core::Unexpected(
            std::format("{:#x}: falta el RNG del joint {:#x}/{:#x}", address, joint.type, joint.subtype));
    return services.random;
}

// 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, 0): una partícula sin dueño, con el RNG y el reloj del paso.
// Con la tabla llena no hace nada (ni rand()).
Core::Expected<void, std::string> Particle(const JointState& joint, const JointKernels::StepServices& services,
                                           std::uint32_t address, std::int32_t type, std::int32_t subtype,
                                           const glm::vec3& position, const glm::vec3& light, float scale)
{
    if (services.particles == nullptr || services.random == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan las partículas o el RNG del joint {:#x}/{:#x}", address,
                                            joint.type, joint.subtype));
    const auto created = services.particles->CreateChecked(type, position, joint.angles, light, subtype, scale,
                                                           services.worldClock, *services.random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: la partícula {:#x}/{:#x} del joint {:#x}/{:#x} falla con el error {}", address, type,
                        subtype, joint.type, joint.subtype, static_cast<unsigned>(created.Error())));
    return {};
}

// 0x530860(0x613, desde, desde, ángulos, subtipo, 0, 0,9f, −1, 0, 0, −1): los joints que sueltan la /0xb y la /0xf, sin
// dueño y siempre del 0x613 (también desde un 0x6ee).
Core::Expected<void, std::string> Release(JointState& joint, const JointKernels::StepServices& services,
                                          std::uint32_t address, glm::vec3& from, glm::vec3& angles,
                                          std::int32_t subtype)
{
    const auto random = RandomOf(joint, services, address);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    const World::FishEffectRequest request{.type = SpiritJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = angles,
                                           .subtype = subtype,
                                           .ownerContext = nullptr,
                                           .scale = Bits(0x3f666666u),
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, request, *random.Value()); !created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    return {};
}

// FCOS y FSIN de la z de los ángulos (sin pasar a radianes) por un float de memoria, sumados a la x y a la y.
Core::Expected<void, std::string> Drift(JointState& joint, float reach, std::uint32_t address)
{
    const auto cosine = Math::Detail::CosineProductPC53(D(joint.angles.z), reach);
    const auto sine = Math::Detail::SineProductPC53(D(joint.angles.z), reach);
    if (!cosine || !sine)
        return Core::Unexpected(
            std::format("{:#x}: el giro del joint {:#x}/{:#x} excede el perfil", address, joint.type, joint.subtype));
    joint.position.x = F32(*cosine + D(joint.position.x));
    joint.position.y = F32(*sine + D(joint.position.y));
    return {};
}

// 0x538f45..0x538feb (la /0xb): la x de los ángulos es la vida (fild); la partícula 0x5da/5 de 0,9f con la luz 1 en una
// copia de la posición; con rand() % 200 en 0, el joint 0x613/0xc desde esa copia; después avanza 20 por FCOS y FSIN de
// la z (0x6b86d0).
Core::Expected<void, std::string> StepTheStill(JointState& joint, const JointKernels::StepServices& services)
{
    glm::vec3 from = joint.position;
    joint.angles.x = static_cast<float>(joint.life);
    constexpr std::int32_t Glow = 0x5da, GlowSubtype = 5;
    if (auto made = Particle(joint, services, 0x538f8e, Glow, GlowSubtype, from, glm::vec3(1.0f), Bits(0x3f666666u));
        !made.HasValue())
        return made;
    const auto random = RandomOf(joint, services, 0x538f96);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    constexpr std::int32_t Rarity = 200; // 0xc8
    constexpr std::int32_t SmokeSubtype = 0xc;
    if (random.Value()->Next() % Rarity == 0)
        if (auto released = Release(joint, services, 0x538fc3, from, joint.angles, SmokeSubtype); !released.HasValue())
            return released;
    constexpr float Reach = 20.0f; // 0x6b86d0
    return Drift(joint, Reach, 0x538fce);
}

// 0x539328..0x539384 (la /0x10): la x de los ángulos es la vida; la partícula 0x6f4/4 de escala 1 en la posición, con
// la luz del joint (+0x48); después avanza 30 por FCOS y FSIN de la z (0x6b8858).
Core::Expected<void, std::string> StepTheColumn(JointState& joint, const JointKernels::StepServices& services)
{
    joint.angles.x = static_cast<float>(joint.life);
    constexpr std::int32_t Splash = 0x6f4, SplashSubtype = 4;
    if (auto made = Particle(joint, services, 0x53935c, Splash, SplashSubtype, joint.position, joint.light, 1.0f);
        !made.HasValue())
        return made;
    constexpr float Reach = 30.0f; // 0x6b8858
    return Drift(joint, Reach, 0x539361);
}

// 0x538ff9..0x539091 (la /0xc): en una copia de la posición, la 0x5f5/0 de escala ((rand() & 31) + 48) · 0,01f
// (0x6b88d4) con la luz (1; 0,2f; 0,1f) y la 0x5da/6 de 0,6f con la luz 1.
Core::Expected<void, std::string> StepTheSmoke(const JointState& joint, const JointKernels::StepServices& services)
{
    const auto random = RandomOf(joint, services, 0x539024);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    const glm::vec3 from = joint.position;
    constexpr std::int32_t Mask = 31, Base = 0x30;
    constexpr double Hundredth = Math::HundredthAsDouble; // 0x6b88d4
    const std::int32_t size = (random.Value()->Next() & Mask) + Base;
    const float scale = F32(static_cast<double>(size) * Hundredth);
    constexpr std::int32_t Smoke = 0x5f5, Glow = 0x5da, GlowSubtype = 6;
    const glm::vec3 ember{1.0f, Bits(0x3e4ccccdu), Bits(0x3dcccccdu)};
    if (auto made = Particle(joint, services, 0x539058, Smoke, 0, from, ember, scale); !made.HasValue())
        return made;
    return Particle(joint, services, 0x539089, Glow, GlowSubtype, from, glm::vec3(1.0f), Bits(0x3f19999au));
}

// 0x53909f..0x539125 (la /0xe): la x de los ángulos − 2 (0x6b8818) y rand() % 5: con 0 la partícula 0x6f2/2, con 1 o 2
// la 0x6f3/2 y si no la 0x6f4/4, de escala 1 en la posición, con la luz del joint (+0x48).
Core::Expected<void, std::string> StepTheSplash(JointState& joint, const JointKernels::StepServices& services)
{
    const auto random = RandomOf(joint, services, 0x5390c2);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    constexpr double Tilt = 2.0; // 0x6b8818
    joint.angles.x = F32(D(joint.angles.x) - Tilt);
    constexpr std::int32_t Choices = 5;
    const std::int32_t pick = random.Value()->Next() % Choices;
    constexpr std::int32_t First = 0x6f2, Second = 0x6f3, Third = 0x6f4;
    constexpr std::int32_t SplashSubtype = 2, LastSubtype = 4;
    if (pick == 0)
        return Particle(joint, services, 0x5390e6, First, SplashSubtype, joint.position, joint.light, 1.0f);
    if (pick <= 2)
        return Particle(joint, services, 0x539109, Second, SplashSubtype, joint.position, joint.light, 1.0f);
    return Particle(joint, services, 0x539120, Third, LastSubtype, joint.position, joint.light, 1.0f);
}

// 0x539249..0x5392e6: el dueño de la /0xf (el 0x126 de Kundun, 0x51cf45: un efecto con modelo). La x de sus ángulos en
// 0 si no es cero (fcomp con C3: un cero de cualquier signo o NaN quedan); con su vida (+0x60) de más de 5, el alpha
// (+0x174) en 1; su posición, la del joint con k = clave · 0,1f + 30 guardado en float: x + FSIN(z) · k (el fst deja la
// suma en la pila y el fstp la guarda igual), y − FCOS(z) · k y la z; otra vez la x de los ángulos y la y en 0.
Core::Expected<void, std::string> CarryThePhoenix(const JointState& joint, World::ModelEffect& owner)
{
    const auto flatten = [](float& pitch)
    {
        if (pitch < 0.0f || pitch > 0.0f)
            pitch = 0.0f;
    };
    flatten(owner.transform.angles.x);
    constexpr std::int32_t ShownLife = 5;
    if (owner.life > ShownLife)
        owner.alpha = 1.0f;
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    constexpr double Reach = 30.0;                // 0x6b8858
    const float radius = F32(static_cast<double>(joint.key) * Tenth + Reach);
    const auto sine = Math::Detail::SineDoubleProductPC53(D(joint.angles.z), D(radius));
    const auto cosine = Math::Detail::CosineDoubleProductPC53(D(joint.angles.z), D(radius));
    if (!sine || !cosine)
        return Core::Unexpected(std::format("0x5392a0: el giro del joint {:#x}/0xf excede el perfil", joint.type));
    owner.transform.position = {F32(D(joint.position.x) + *sine), F32(D(joint.position.y) - *cosine), joint.position.z};
    flatten(owner.transform.angles.x);
    owner.transform.angles.y = 0.0f;
    return {};
}

// 0x539133..0x53931e: la /0xf. La key (WORD) suma 30. Con vida de menos de 35, en una copia de la posición: la
// partícula 0x5d9/7 de escala 1 con la luz 1 (0x53918a); con rand() & 1 en 0, la copia pasa a (x − FSIN(z) · k, FCOS(z)
// · k + y, 350) con k = 40 − key · 0,1f retenido en la x87 y suelta dos joints 0x613/0xe desde ella con los ángulos (0,
// 0, 0) y (0, 0, 3); después, con dueño, CarryThePhoenix. Siempre (0x5392e9): avanza k = key · 0,1f + 30 (en la x87,
// sin redondear) en x con el seno de la z de los ángulos y lo resta en y con el coseno.
Core::Expected<void, std::string> StepTheSpiral(JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::uint16_t KeyStep = 30;         // 0x53913b
    constexpr std::int32_t SpiralLife = 35;       // 0x53914c
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    joint.key =
        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(std::bit_cast<std::uint16_t>(joint.key) + KeyStep));
    if (joint.life < SpiralLife)
    {
        glm::vec3 from = joint.position;
        constexpr std::int32_t Fall = 0x5d9, FallSubtype = 7;
        if (auto made = Particle(joint, services, 0x53918a, Fall, FallSubtype, from, glm::vec3(1.0f), 1.0f);
            !made.HasValue())
            return made;
        const auto random = RandomOf(joint, services, 0x539192);
        if (!random.HasValue())
            return Core::Unexpected(random.Error());
        if ((random.Value()->Next() & 1) == 0)
        {
            constexpr float Hover = 350.0f; // 0x43af0000
            constexpr double Span = 40.0;   // 0x6b8bf4
            from.z = Hover;
            const double radius = Span - static_cast<double>(joint.key) * Tenth;
            const auto sine = Math::Detail::SineDoubleProductPC53(D(joint.angles.z), radius);
            const auto cosine = Math::Detail::CosineDoubleProductPC53(D(joint.angles.z), radius);
            if (!sine || !cosine)
                return Core::Unexpected(
                    std::format("0x5391d3: el giro del joint {:#x}/0xf excede el perfil", joint.type));
            from.x = F32(D(from.x) - *sine);
            from.y = F32(*cosine + D(from.y));
            constexpr int Pieces = 2;
            constexpr double Spread = 3.0; // 0x6b86b8
            constexpr std::int32_t SplashSubtype = 0xe;
            for (int piece = 0; piece < Pieces; ++piece)
            {
                glm::vec3 angles{0.0f, 0.0f, F32(static_cast<double>(piece) * Spread)};
                if (auto released = Release(joint, services, 0x539228, from, angles, SplashSubtype);
                    !released.HasValue())
                    return released;
            }
        }
        if (joint.ownerContext != nullptr)
        {
            if (joint.ownerKind != JointOwnerKind::ModelEffect)
                return Core::Unexpected(std::format(
                    "0x539249: el dueño del joint {:#x}/0xf que no es un efecto con modelo no está portado (CL11)",
                    joint.type));
            if (auto carried = CarryThePhoenix(joint, *static_cast<World::ModelEffect*>(joint.ownerContext));
                !carried.HasValue())
                return carried;
        }
    }
    constexpr double Reach = 30.0; // 0x6b8858
    const double radius = static_cast<double>(joint.key) * Tenth + Reach;
    const auto sine = Math::Detail::SineDoubleProductPC53(D(joint.angles.z), radius);
    if (!sine)
        return Core::Unexpected(std::format("0x539305: el seno del joint {:#x}/0xf excede el perfil", joint.type));
    joint.position.x = F32(*sine + D(joint.position.x));
    const auto cosine = Math::Detail::CosineDoubleProductPC53(D(joint.angles.z), radius);
    if (!cosine)
        return Core::Unexpected(std::format("0x539312: el coseno del joint {:#x}/0xf excede el perfil", joint.type));
    joint.position.y = F32(D(joint.position.y) - *cosine);
    return {};
}

// 0x539392..0x53941e: la /0x11, con la vida justo en 100, suelta 120 joints 0x613/0x10 sin dueño desde una copia de
// la posición hasta la posición, de ancho 0,9f (0x3f666666) y con los ángulos (0, 0, 3 · i).
Core::Expected<void, std::string> StepTheBurst(JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::int32_t BurstLife = 100;
    if (joint.life != BurstLife)
        return {};
    const auto random = RandomOf(joint, services, 0x53940d);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    constexpr int Pieces = 120;    // 0x78
    constexpr double Spread = 3.0; // 0x6b86b8
    glm::vec3 from = joint.position;
    for (int piece = 0; piece < Pieces; ++piece)
    {
        glm::vec3 angles{0.0f, 0.0f, F32(static_cast<double>(piece) * Spread)};
        const World::FishEffectRequest request{.type = SpiritJoint,
                                               .firstPosition = from,
                                               .secondPosition = joint.position,
                                               .angles = angles,
                                               .subtype = 0x10,
                                               .ownerContext = nullptr,
                                               .scale = Bits(0x3f666666u),
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
        if (auto created = CreateFaunaJoint(services.joints, request, *random.Value()); !created.HasValue())
            return Core::Unexpected(std::format("0x53940d: {}", created.Error()));
    }
    return {};
}

// 0x53942c..0x539500: la /0x12. La velocidad + 3; la key (WORD) + 6 con vida % 12 de 5 o menos y − 6 si no; la x de los
// ángulos −90 − key y la z + 10. Con dueño (el 0x125 de Kundun, 0x51ce91: JointOwnerKind::ModelEffect), le pone el
// alpha (+0x174) en 1 si su vida (+0x60) pasa de 5 y le copia la posición (+0x10, 0x539494); otro dueño corta ahí.
// Después (0x5394be..0x5394fb), en una copia de la posición, la partícula 0x5f2/0x13 de escala 7 (0x40e00000) con la
// luz 1, y sigue a la cola (0x53c390).
Core::Expected<void, std::string> StepTheSwing(JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::int32_t SwingPeriod = 12, RisingHalf = 5;
    const std::int32_t phase = joint.life % SwingPeriod;
    joint.speed = F32(D(joint.speed) + 3.0);
    const std::uint16_t delta = phase <= RisingHalf ? 6 : 0xfffa;
    joint.key =
        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(std::bit_cast<std::uint16_t>(joint.key) + delta));
    constexpr double Down = -90.0; // 0x6ba1dc
    joint.angles.x = F32(Down - static_cast<double>(joint.key));
    joint.angles.z = F32(D(joint.angles.z) + 10.0);
    if (joint.ownerContext != nullptr)
    {
        if (joint.ownerKind != JointOwnerKind::ModelEffect)
            return Core::Unexpected(std::format(
                "0x539494: el dueño del joint {:#x}/0x12 que no es un efecto con modelo no está portado (CL11)",
                joint.type));
        auto& owner = *static_cast<World::ModelEffect*>(joint.ownerContext);
        constexpr std::int32_t ShownLife = 5;
        if (owner.life > ShownLife)
            owner.alpha = 1.0f;
        owner.transform.position = joint.position;
    }
    const glm::vec3 from = joint.position;
    constexpr std::int32_t Flame = 0x5f2, FlameSubtype = 0x13;
    constexpr float FlameScale = 7.0f; // 0x40e00000
    return Particle(joint, services, 0x5394fb, Flame, FlameSubtype, from, glm::vec3(1.0f), FlameScale);
}

// 0x539b10..0x539cc7: la /0 y la /5 con un ancho distinto de 80. El destino es el dueño 80 más arriba; la distancia en
// el plano es la de antes del movimiento (0x539b33, en float); 0x4e9810 gira hacia el destino de a 10 (la /5, de a 2) y
// con el doble de la velocidad no menor que esa distancia prende +0x9d4. La /0 se sacude con dos (rand() & 31) − 16:
// vx + r1 · 0,2f, la x de los ángulos + vx, la z + (r2 · 0,8f + vz), vx · 0,6f y vz el sacudón · 0,8f. Con 0x5f93c0, a
// menos de 100 sobre el terreno sube (x en −5) y a más de 400 baja (x en 5), con vx en 0. La luz es la vida · 0,1f y la
// luz dinámica de radio 4 resta ((rand() & 3) + 4) · 0,01f.
Core::Expected<void, std::string> StepTheVolley(JointState& joint, const JointKernels::StepServices& services)
{
    const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(
            std::format("0x539b13: el joint {:#x}/{} no tiene un personaje de dueño", joint.type, joint.subtype));
    const auto random = RandomOf(joint, services, 0x539b8e);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    if (services.terrainHeight == nullptr || !*services.terrainHeight)
        return Core::Unexpected(
            std::format("0x539c19: falta la altura del terreno del joint {:#x}/{}", joint.type, joint.subtype));
    constexpr double Above = 80.0; // 0x6b9ffc
    joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Above)};
    const float distance = F32(std::sqrt(D(services.startDeltaY) * D(services.startDeltaY) +
                                         D(services.startDeltaX) * D(services.startDeltaX)));
    constexpr float SlowTurn = 2.0f, FastTurn = 10.0f; // 0x40000000 y 0x41200000
    static_cast<void>(
        Math::MoveToward(joint.position, joint.angles, joint.targetPosition, joint.subtype == 5 ? SlowTurn : FastTurn));
    if (joint.field9d4 == 0 && D(joint.speed) + D(joint.speed) >= D(distance))
        joint.field9d4 = 1;
    if (joint.subtype != 5)
    {
        constexpr double Pitch = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));   // 0x6b8854, 0,2f
        constexpr double Yaw = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));     // 0x6b8a38, 0,8f
        constexpr double Damping = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0,6f
        constexpr int Mask = 31, Center = 16;
        const auto first = (random.Value()->Next() & Mask) - Center;
        joint.velocity.x = F32(static_cast<double>(first) * Pitch + D(joint.velocity.x));
        const auto second = (random.Value()->Next() & Mask) - Center;
        const double shake = static_cast<double>(second) * Yaw + D(joint.velocity.z);
        joint.angles.x = F32(D(joint.angles.x) + D(joint.velocity.x));
        joint.angles.z = F32(shake + D(joint.angles.z));
        joint.velocity.x = F32(D(joint.velocity.x) * Damping);
        joint.velocity.z = F32(shake * Yaw);
    }
    const double ground = (*services.terrainHeight)(joint.position.x, joint.position.y);
    constexpr double Low = 100.0, High = 400.0; // 0x6b86b4 y 0x6b87b0
    if (ground + Low > D(joint.position.z))
    {
        joint.velocity.x = 0.0f;
        joint.angles.x = -5.0f; // 0xc0a00000
    }
    if (!(ground + High >= D(joint.position.z)))
    {
        joint.velocity.x = 0.0f;
        joint.angles.x = 5.0f; // 0x40a00000
    }
    constexpr double Tenth = Math::TenthAsDouble;                                    // 0x6b87b8
    constexpr double Shade = static_cast<double>(std::bit_cast<float>(0xbc23d70au)); // 0x6b8a58, −0,01f
    joint.light = glm::vec3(F32(static_cast<double>(joint.life) * Tenth));
    const auto shadeSteps = (random.Value()->Next() & 3) + 4;
    const glm::vec3 shade(F32(static_cast<double>(shadeSteps) * Shade));
    constexpr std::int32_t ShadeRadius = 4;
    return World::AddDynamicLight(services.lighting, joint.position, shade, ShadeRadius, "0x539cbf");
}

// 0x539a0e..0x539ae5: el 0x613/0, /5 y /0x13 de ancho 80. Suelta el 0xd0 (0x518cf0(0xd0, posición, ángulos, luz,
// 3 la /5 y 0 los demás, sin dueño, −1, 0, 0, 0, 0), el rayo que cae, que no saca rand()). Con el asedio prendido
// (0x4294b0) y el bit 0x100 de 0x5f82c0 en su punto, la velocidad en 0 y la vida en _ftol(vida · 0,2f), y vuelve a la
// cola 0x53c393 sin el resto (false). Si no, con la vida múltiplo de 15 y el héroe de dueño, sus blancos (0x517a20(la
// skill +0x9ea, la posición, 150, la tanda +0x9eb, la clave +0x9e8, el byte +0xa08)), y sigue (true).
Core::Expected<bool, std::string> VolleyFromTheSky(JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::int32_t ShieldSubtype = 5;
    constexpr std::int32_t FallingRay = 3; // 0x539a2f
    constexpr std::uint32_t SiegeCell = 0x100;
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8cfc: 0,2f
    constexpr std::int32_t HitEvery = 15;                                             // 0x539aa0
    constexpr float HitRadius = 150.0f;                                               // 0x539ad6: 0x43160000
    if (services.effects == nullptr || services.random == nullptr)
        return Core::Unexpected(std::format("0x539a48: el joint {:#x}/{:#x} de ancho 80 pide la tabla de efectos "
                                            "(0x518cf0) para el 0xd0",
                                            joint.type, joint.subtype));
    const World::ModelEffectArguments ray{.type = World::LaserEffect,
                                          .position = joint.position,
                                          .angles = joint.angles,
                                          .light = joint.light,
                                          .subtype = joint.subtype == ShieldSubtype ? FallingRay : 0,
                                          .owner = nullptr,
                                          .key = 0xffff};
    const auto created =
        World::CreateWizardModelEffect(*services.effects, ray, *services.random, World::WizardEffectServices{});
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x539a48: {}", created.Error()));
    if (services.volley == nullptr)
        return Core::Unexpected(std::format("0x539a50: el joint {:#x}/{:#x} de ancho 80 pide el asedio, los atributos "
                                            "y los blancos del héroe",
                                            joint.type, joint.subtype));
    if (services.volley->siegeActive)
    {
        if (services.volley->attributes == nullptr)
            return Core::Unexpected(std::string("0x539a60: faltan los atributos de las celdas (0x7d0d348)"));
        const auto cell =
            Terrain::TerrainAttributeQueryAt(*services.volley->attributes, joint.position.x, joint.position.y);
        if (!cell.HasValue())
            return Core::Unexpected(std::string("0x539a60: 0x5f82c0 lee fuera de la tabla de atributos"));
        if ((cell.Value() & SiegeCell) == SiegeCell)
        {
            joint.speed = 0.0f;
            joint.life = Math::FtolLow(static_cast<double>(joint.life) * Shrink);
            return false;
        }
    }
    if (joint.life % HitEvery != 0)
        return true;
    if (services.hero == nullptr)
        return Core::Unexpected(std::string("0x539aaf: falta el héroe ([0x743a170])"));
    if (joint.ownerContext != services.hero)
        return true;
    if (!services.volley->skillHits)
        return Core::Unexpected(std::string("0x539add: faltan los blancos de la skill (0x517a20)"));
    const SkillHitsRequest request{.skillIndex = joint.skill,
                                   .point = joint.position,
                                   .radius = HitRadius,
                                   .volley = joint.volley,
                                   .key = static_cast<std::uint16_t>(joint.key),
                                   .extra = joint.skillSerial};
    if (auto hit = services.volley->skillHits(request); !hit.HasValue())
        return Core::Unexpected(hit.Error());
    return true;
}
} // namespace

Core::Expected<void, std::string> StepSpiritJoint(JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    if (subtype == 0 || subtype == 5 || subtype == 0x13)
    {
        // 0x539a0e: con el ancho en 80 (los bits 0x42a00000), el 0xd0 y lo que sigue (VolleyFromTheSky); si no vuelve
        // antes, la misma cola que con otro ancho. Con otro ancho, la /0x13 suelta antes el destello 0x11b/0
        // (0x539b08).
        constexpr std::uint32_t VolleyWidth = 0x42a00000u;
        if (std::bit_cast<std::uint32_t>(joint.width) == VolleyWidth)
        {
            const auto fallen = VolleyFromTheSky(joint, services);
            if (!fallen.HasValue())
                return Core::Unexpected(fallen.Error());
            if (!fallen.Value())
                return {};
            return StepTheVolley(joint, services);
        }
        if (subtype == 0x13)
        {
            // 0x539aec..0x539b08: 0x518cf0(0x11b, posición, ángulos, luz, 0, 0, −1, 0, 0, 0, 0), sin rand().
            if (services.effects == nullptr)
                return Core::Unexpected(std::format(
                    "0x539b08: el joint {:#x}/0x13 pide la tabla de efectos (0x518cf0) para el 0x11b/0", joint.type));
            static_cast<void>(CreateTheRingSpark(*services.effects, joint.position, joint.angles, joint.light));
        }
        return StepTheVolley(joint, services);
    }
    switch (subtype)
    {
    case 0xa:
        // 0x538e89..0x538e9f: la z de los ángulos − 50 y velocidad 50.
        joint.angles.z = F32(D(joint.angles.z) - 50.0);
        joint.speed = 50.0f;
        return {};
    case 0xb:
        return StepTheStill(joint, services);
    case 0x10:
        return StepTheColumn(joint, services);
    case 0xc:
        return StepTheSmoke(joint, services);
    case 0xe:
        return StepTheSplash(joint, services);
    case 0xf:
        return StepTheSpiral(joint, services);
    case 0x11:
        return StepTheBurst(joint, services);
    case 0x12:
        return StepTheSwing(joint, services);
    case 1:
    case 2:
    case 3:
    case 6:
    case 7:
    case 8:
    case 0xd:
    case 0x14:
    case 0x15:
        break;
    default:
        // 0x538d77..0x539505: la cadena de comparaciones no tiene caso para la /4 (y la /9 tiene el suyo en
        // FaunaJointCreation.cpp, StepMeteor) ni para los subtipos mayores que 0x15: van directo a la cola 0x53c393.
        return {};
    }
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
    case 0x14: // 0x53950e..0x53956b: igual que la /8.
        StepTheArc(joint, *services.random);
        return {};
    default:
        return StepTheCross(joint, services);
    }
}
} // namespace Client::Scenes::JointFamilies
