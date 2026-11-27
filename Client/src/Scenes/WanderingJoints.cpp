#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>

// El 0x60f/7, /0xb y /0x19: la luz que vaga alrededor de su dueño (alta 0x533bbd, 0x533d5f y 0x533db8; paso
// 0x53a87a..0x53b01f, sin velocidad). La /7 lleva un personaje de dueño (0x4f8592); la /0xb y la /0x19, la flecha 0xf8
// (0x51a405, 0x51a3b7), un efecto con modelo.
namespace Client::Scenes::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

std::int32_t Wrapped(std::int32_t value, std::uint32_t amount)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) + amount);
}

// +0x54 de la /0xb y la /0x19: la flecha 0xf8 (un efecto con modelo).
World::ModelEffect* ArrowOwner(const JointState& joint)
{
    if (joint.ownerKind != JointOwnerKind::ModelEffect)
        return nullptr;
    return static_cast<World::ModelEffect*>(joint.ownerContext);
}

// El punto alrededor del que vaga: el +0x10 del personaje dueño (la /7, 0x53adb1) o el +0x17c de la flecha, su
// partida (la /0xb y la /0x19, 0x53ad64).
const glm::vec3* Anchor(const JointState& joint)
{
    if (joint.subtype == WanderSubtype)
    {
        const ActorState* actor = JointKernels::ActorOwner(joint);
        return actor != nullptr ? &actor->position : nullptr;
    }
    const World::ModelEffect* arrow = ArrowOwner(joint);
    return arrow != nullptr ? &arrow->start : nullptr;
}

// Las tres frecuencias de 0x53a9a5..0x53a9ce: (0,048f; 0,0613f; 0,1113f) y, en la /0xb, una vez y media.
struct Rates
{
    float first;
    float second;
    float third;
};

// 0x53a9d5..0x53aa84: la dirección con los argumentos y los senos guardados en float (0x52e3a0 y 0x52e390 son FSIN y
// FCOS de un float). Devuelve [ebp−0x3c], [ebp−0x38] y [ebp−0x54].
std::optional<std::array<float, 3>> Direction(std::int32_t phase, const Rates& rates)
{
    namespace Math = Client::Math::Detail;
    constexpr std::uint32_t FirstOffset = 0xd903;  // 0x53a9d8, 55555
    constexpr std::uint32_t SecondOffset = 0x2b67; // 0x53aa33, 11111
    const float a = F32(static_cast<double>(Wrapped(phase, FirstOffset)) * D(rates.first));
    const float b = F32(static_cast<double>(phase) * D(rates.second));
    const auto cosB = Math::CosineSpillF32(D(b));
    const auto sinB = Math::SineSpillF32(D(b));
    const auto cosA = Math::CosineSpillF32(D(a));
    if (!cosB || !sinB || !cosA)
        return std::nullopt;
    const auto up = Math::SineDoubleProductPC53(D(a), D(*cosB));
    const auto across = Math::SineDoubleProductPC53(D(a), D(*sinB));
    if (!up || !across)
        return std::nullopt;
    const float upward = F32(*up);       // [ebp−0x54]
    const float sideways = F32(*across); // [ebp−0x50]
    const float c = F32(static_cast<double>(Wrapped(phase, SecondOffset)) * D(rates.third));
    const auto sinC = Math::SineSpillF32(D(c));
    const auto first = Math::CosineDoubleProductPC53(D(c), D(*cosA));
    const auto second = Math::CosineDoubleProductPC53(D(c), D(sideways));
    if (!sinC || !first || !second)
        return std::nullopt;
    const float y = F32(*first + D(*sinC) * D(sideways)); // [ebp−0x38]
    const float x = F32(*second - D(*sinC) * D(*cosA));   // [ebp−0x3c]
    return std::array<float, 3>{x, y, upward};
}

// 0x53aa87..0x53acaf: la vida · 4/3 da la mezcla con el punto (s) y el radio: la /7 se aleja hasta 150 en los últimos
// pasos ((40 − t) · 15) y la /0xb y la /0x19 al principio ((t − 10) · 5).
struct Reach
{
    float blend;
    double radius;
};

Reach ReachOf(const JointState& joint)
{
    constexpr double FourThirds = static_cast<double>(std::bit_cast<float>(0x3faaaaabu)); // 0x6ba2d4
    constexpr double Widest = 150.0;                                                      // 0x6b87ac
    const bool arrow = joint.subtype != WanderSubtype;
    const float t = F32(static_cast<double>(joint.life) * FourThirds);
    // 0x53aaa2..0x53abbd: debajo de 10 (20 en la flecha; NaN también) por 7 (por 4); si no, más 60.
    const double early = arrow ? 20.0 : 10.0;
    const double grown = D(t) >= early ? D(t) + 60.0 : D(t) * (arrow ? 4.0 : 7.0);
    const double divisor = static_cast<double>(Wrapped(joint.field9ec, 30u));
    const float blend = F32(grown / divisor * 30.0);
    // 0x53abff..0x53acaf: el fcomp con 0 deja v si no es negativo (NaN también) y el de 150 deja 150 si no es menor.
    const double v = arrow ? D(t) - 10.0 : 40.0 - D(t);
    const double kept = v < 0 ? 0.0 : v;
    const double scaled = kept * (arrow ? 5.0 : 15.0);
    return {blend, scaled >= Widest ? Widest : scaled};
}

// 0x53aec1..0x53afb5: el sprite con la rotación rand() % 360 y la escala ((rand() & 1) + 8) · factor.
void Glint(World::FrameSprites& sprites, Crt::CrtRandom& random, std::uint16_t texture, const glm::vec3& position,
           const glm::vec3& light, float factor)
{
    constexpr int FullTurn = Math::FullTurn;
    const auto rotation = static_cast<float>(random.Next() % FullTurn);
    const float scale = F32(static_cast<double>(random.Next() % 2 + 8) * D(factor));
    static_cast<void>(sprites.Create(texture, position, scale, light, rotation, 0));
}
} // namespace

void CreateWanderingJoint(JointState& joint, const World::FishEffectRequest& request, Crt::CrtRandom& random)
{
    // 0x533677..0x53368e: lo común del 0x60f (vida 100, 20 puntos y ancho 10), que el subtipo pisa.
    joint.life = 100;
    joint.maximumCount = 20;
    joint.width = 10;
    constexpr float Dim = std::bit_cast<float>(0x3e4ccccdu); // 0,2f
    constexpr std::int32_t Life = 0x1e, Points = 0xf;
    if (joint.subtype == WanderSubtype)
    {
        // 0x533bc2..0x533bf3: la luz azul, la vida rand() % 10 + 30 con el resto en +0x9ec.
        joint.light = {Dim, Dim, 1};
        const std::int32_t delay = random.Next() % 10;
        joint.maximumCount = Points;
        joint.field9ec = delay;
        joint.life = delay + Life;
    }
    else
    {
        // 0x533d64..0x533d8c y 0x533dbd..0x533de5: la /0xb azul y la /0x19 rosa, sin demora.
        joint.light = joint.subtype == ArrowWanderSubtype
                          ? glm::vec3{Dim, Dim, 1}
                          : glm::vec3{std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu),
                                      std::bit_cast<float>(0x3f19999au)};
        joint.field9ec = 0;
        joint.life = Life;
        joint.maximumCount = Points;
    }
    // 0x533bf9..0x533c13: el ancho 30 y vx en rand() % 3000; 0x5342be, el destino en el segundo punto.
    constexpr int Spread = 0xbb8;
    const std::int32_t drift = random.Next() % Spread;
    joint.width = 30;
    joint.velocity.x = static_cast<float>(drift);
    joint.targetPosition = request.secondPosition;
}

Core::Expected<void, std::string> StepWanderingJoint(JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    // 0x53a884..0x53a899: la flecha muerta (su byte vivo en 0) apaga el joint con la vida en −1 y va a la cola.
    if (subtype != WanderSubtype)
    {
        const World::ModelEffect* arrow = ArrowOwner(joint);
        if (arrow == nullptr)
            return Core::Unexpected(std::format("0x53a887: falta la flecha dueña del joint 0x60f/{:#x}", subtype));
        if (!arrow->alive)
        {
            joint.alive = false;
            joint.life = -1;
            return {};
        }
    }
    // 0x53a89e..0x53a96c: el acceso al contador de pasos (0x7d3a0f0, 4 rand() al cerrarlo en 0), como el hada.
    if (services.cloth == nullptr || services.random == nullptr || services.sprites == nullptr)
        return Core::Unexpected(
            std::format("0x53a89e: faltan el contador, el RNG o los sprites del joint 0x60f/{:#x}", subtype));
    Crt::CrtRandom& random = *services.random;
    const auto steps = std::bit_cast<std::uint32_t>(services.cloth->logicSteps);
    if (services.cloth->openCounterAccesses == 0)
        for (std::uint16_t draw = 0; draw < Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(random.Next());
    // 0x53a971..0x53a9ba: la fase es el contador (negado en los lugares pares) más el lugar · 53731.
    const auto index = static_cast<std::uint32_t>(services.index);
    constexpr std::uint32_t PhaseStride = 53731;
    const auto phase = std::bit_cast<std::int32_t>(index * PhaseStride + ((index & 1u) != 0 ? steps : 0u - steps));
    const Rates rates = subtype == ArrowWanderSubtype
                            ? Rates{std::bit_cast<float>(0x3d9374bcu), // 0x6ba2d8, 0,072f
                                    std::bit_cast<float>(0x3dbc5048u), std::bit_cast<float>(0x3e2af4f1u)}
                            : Rates{std::bit_cast<float>(0x3d449ba6u), // 0x6ba314, 0,048f
                                    std::bit_cast<float>(0x3d7b15b5u), std::bit_cast<float>(0x3de3f141u)};
    const auto direction = Direction(phase, rates);
    if (!direction)
        return Core::Unexpected(
            std::format("0x53a9f8: la dirección del joint 0x60f/{:#x} excede el perfil PC53", subtype));
    const Reach reach = ReachOf(joint);
    // 0x53abe2..0x53abef: el sonido 0x42 con la vida en 30 (0x41ed10(0x42, 0, 0)).
    constexpr std::int32_t ChimeLife = 0x1e, Chime = 0x42;
    if (joint.life == ChimeLife && services.soundRequest != nullptr && *services.soundRequest)
        (*services.soundRequest)(Chime);
    // 0x53acaf..0x53acd7: el destino más la dirección por el radio.
    const auto& [x, y, upward] = *direction;
    joint.position = {F32(reach.radius * D(x) + D(joint.targetPosition.x)),
                      F32(reach.radius * D(y) + D(joint.targetPosition.y)),
                      F32(reach.radius * D(upward) + D(joint.targetPosition.z))};
    // 0x53acda..0x53adf3: por eje, la mezcla con el punto del dueño corrido por cos(arg · 0,01f) · 25, con arg la fase
    // / 10 + k + el lugar · 51231 (k de a 3711).
    const glm::vec3* anchor = Anchor(joint);
    if (anchor == nullptr)
        return Core::Unexpected(std::format("0x53ad5e: falta el dueño del joint 0x60f/{:#x}", subtype));
    constexpr double Hundredth = Math::HundredthAsDouble; // 0x6b88d4
    constexpr std::uint32_t PlaceStride = 51231, AxisStride = 0xe7f;
    const double rest = 100.0 - D(reach.blend);
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        const std::uint32_t whole = std::bit_cast<std::uint32_t>(phase / 10) + axis * AxisStride + index * PlaceStride;
        const float argument = F32(static_cast<double>(std::bit_cast<std::int32_t>(whole)) * Hundredth);
        const auto wave = Math::Detail::CosineDoubleProductPC53(D(argument), 25.0);
        if (!wave)
            return Core::Unexpected(
                std::format("0x53ad53: el vaivén del joint 0x60f/{:#x} excede el perfil PC53", subtype));
        const double point = (*wave + D((*anchor)[static_cast<int>(axis)])) * rest;
        auto& coordinate = joint.position[static_cast<int>(axis)];
        coordinate = F32((D(reach.blend) * D(coordinate) + point) * Hundredth);
    }
    // 0x53ae06: la /7, 100 más arriba.
    if (subtype == WanderSubtype)
        joint.position.z = F32(D(joint.position.z) + 100);
    // 0x53ae12..0x53aebe: la luz de los sprites; con el skill 1, la flecha sigue al joint y choca (0x520170).
    glm::vec3 glow{0.5f, 0.5f, 1};
    if (subtype == ArrowWanderSubtype)
        glow = {std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e99999au), 0.5f};
    else if (subtype == PinkWanderSubtype)
        glow = {std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu),
                std::bit_cast<float>(0x3f19999au)};
    if (subtype != WanderSubtype && joint.skill == 1)
    {
        World::ModelEffect* arrow = ArrowOwner(joint);
        arrow->transform.position = joint.position;
        if (services.effectHit == nullptr || !*services.effectHit)
            return Core::Unexpected(
                std::format("0x53ae6d: falta el choque de la flecha (0x520170) del joint 0x60f/{:#x}", subtype));
        if (auto hit = (*services.effectHit)(*arrow); !hit.HasValue())
            return hit;
    }
    // 0x53aec1..0x53afb5: el 0x5fd a 0,1f y dos 0x5ac a 0,18f.
    constexpr std::uint16_t Flare = 0x5fd, Shine = 0x5ac;
    constexpr float FlareStep = std::bit_cast<float>(0x3dcccccdu); // 0x6b87b8
    constexpr float ShineStep = std::bit_cast<float>(0x3e3851ecu); // 0x6ba2d0
    Glint(*services.sprites, random, Flare, joint.position, glow, FlareStep);
    Glint(*services.sprites, random, Shine, joint.position, glow, ShineStep);
    Glint(*services.sprites, random, Shine, joint.position, glow, ShineStep);
    // 0x53afc0..0x53b017: la /7 y la /0x19 en su última vida, la partícula 0x5ed/1 de escala 0,6f.
    if (subtype != ArrowWanderSubtype && joint.life == 1)
    {
        constexpr std::int32_t Burst = 0x5ed;
        constexpr float BurstScale = std::bit_cast<float>(0x3f19999au);
        const bool made =
            services.particles != nullptr && services.particles
                                                 ->CreateChecked(Burst, joint.position, glm::vec3(0), glm::vec3(1), 1,
                                                                 BurstScale, services.worldClock, random)
                                                 .HasValue();
        if (!made)
            return Core::Unexpected(std::format("0x53b017: falló la partícula 0x5ed/1 del joint 0x60f/{:#x}", subtype));
    }
    // 0x53b318..0x53b363: la /7 y la /0xb no se apagan; la /0x19, por 0,7692308f con menos de 10 de vida.
    constexpr std::int32_t FadeLife = 10;
    if (subtype == PinkWanderSubtype && joint.life < FadeLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace Client::Scenes::JointFamilies
