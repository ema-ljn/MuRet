#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"

#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// El 0x60f/4 y /0xc (y el 0x62b, que comparte su alta y su paso): las chispas que el 0x812 suelta en cada paso
// (0x52c9da y 0x52c9a7, con él de dueño). Giran alrededor de su destino, que avanza (vy, vz) por paso, en un círculo
// vertical orientado por la z de sus ángulos; la /4 con un radio que se achica con la vida y la /0xc con uno fijo de 26
// que baja 0,3 por paso. Su paso se repite en el mismo cuadro hasta que la vida es múltiplo de 12 (0x53c3c6).
namespace Client::Scenes::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

// 0x6ba2c4 y 0x6b8cf8: los grados a radianes, negado (el alta) y no (el paso).
constexpr double NegativeRadians = static_cast<double>(std::bit_cast<float>(0xbc8efa35u));
constexpr double Radians = Math::DegreesToRadiansAsDouble;
} // namespace

bool IsHelix(std::int32_t type, std::int32_t subtype)
{
    return (type == DropJoint || type == SecondHelixJoint) && (subtype == HelixSubtype || subtype == WideHelixSubtype);
}

Core::Expected<void, std::string> CreateHelixJoint(JointState& joint, const World::FishEffectRequest& request,
                                                   Crt::CrtRandom& random)
{
    // 0x533677..0x53368e: lo común del 0x60f (vida 100, 20 puntos y ancho 10), que el subtipo pisa. 0x533faf: el ancho.
    joint.life = 100;
    joint.maximumCount = 20;
    joint.width = 10;
    joint.width = request.scale;
    const bool wide = joint.subtype == WideHelixSubtype;
    double reach = 0;
    if (wide)
    {
        // 0x533fba..0x533fd5: vida 70, la luz (0,1f; 0,1f; 1) y 50 puntos.
        constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
        joint.life = 0x46;
        joint.light = {Dim, Dim, 1};
        joint.maximumCount = 0x32;
        reach = -4.0; // 0x6ba2bc
    }
    else
    {
        // 0x53405c..0x53408d: vida 110, 200 puntos (el tope de 0x534cb6 los deja en 50) y la luz del dueño (+0xf4: el
        // efecto 0x812); sin dueño, el original desreferencia el puntero nulo (0x534070) y revienta.
        if (joint.ownerKind != JointOwnerKind::ModelEffect || joint.ownerContext == nullptr)
            return Core::Unexpected(std::format("0x534070: falta el efecto dueño del joint {:#x}/4", joint.type));
        joint.life = 0x6e;
        joint.maximumCount = 0xc8;
        joint.light = static_cast<const World::ModelEffect*>(joint.ownerContext)->light;
        reach = -2.0; // 0x6ba21c
    }
    // 0x533fdc..0x53401d y 0x534090..0x5340d1: vx en rand() % 360; vy y vz, el seno y el coseno de −z en radianes por
    // −4 (la /0xc) o −2 (la /4).
    constexpr int FullTurn = Math::FullTurn;
    joint.velocity.x = static_cast<float>(random.Next() % FullTurn);
    const double argument = D(joint.angles.z) * NegativeRadians;
    const auto sine = Render::Models::Detail::SineDoubleProductPC53(argument, reach);
    const auto cosine = Render::Models::Detail::CosineDoubleProductPC53(argument, reach);
    if (!sine || !cosine)
        return Core::Unexpected(std::format("0x533ffe: el giro del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    joint.velocity.y = F32(*sine);
    joint.velocity.z = F32(*cosine);
    // 0x5342be: el destino es el segundo punto. 0x534cb6: el tope de 50 puntos.
    joint.targetPosition = request.secondPosition;
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

Core::Expected<void, std::string> StepHelixJoint(JointState& joint)
{
    // 0x53b08a..0x53b10e: el giro a = (vida + vx) · 0,1f y, en la x87, el radio horizontal h y el vertical v.
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    const double turn = (static_cast<double>(joint.life) + D(joint.velocity.x)) * Tenth;
    const float stored = F32(turn); // [ebp+8]
    std::optional<double> horizontal;
    std::optional<double> vertical;
    if (joint.subtype == WideHelixSubtype)
    {
        // 0x53b0a7..0x53b0d0: fst deja el giro sin redondear para el fcos; el fsin lee el guardado. h = cos · −26 y v =
        // sen · 26 − (90 − vida) · 0,3f (fsubp st(1), st).
        constexpr double Radius = 26.0;                                                 // 0x6ba2cc y 0x6b88e0
        constexpr double Sink = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
        constexpr std::uint32_t Top = 0x5a;
        horizontal = Render::Models::Detail::CosineDoubleProductPC53(turn, -Radius);
        const auto rise = Render::Models::Detail::SineDoubleProductPC53(D(stored), Radius);
        if (rise)
        {
            const auto below = std::bit_cast<std::int32_t>(Top - std::bit_cast<std::uint32_t>(joint.life));
            vertical = *rise - static_cast<double>(below) * Sink;
        }
    }
    else
    {
        // 0x53b0d4..0x53b10e: el radio (vida + 40, no menos de 10, el ecx de 0x539f1e) · 0,65f; h = −(cos · radio) y v
        // = sen · radio.
        constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3f266666u)); // 0x6b8bc0, 0,65f
        constexpr std::uint32_t Growth = 0x28;
        constexpr std::int32_t Least = 0xa;
        const auto grown = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) + Growth);
        const double radius = static_cast<double>(grown > Least ? grown : Least) * Shrink;
        const auto across = Render::Models::Detail::CosineDoubleProductPC53(D(stored), radius);
        if (across)
            horizontal = -*across;
        vertical = Render::Models::Detail::SineDoubleProductPC53(D(stored), radius);
    }
    if (!horizontal || !vertical)
        return Core::Unexpected(std::format("0x53b0aa: el giro del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    // 0x53b110..0x53b14c: el destino avanza (vy, vz) y la posición es el destino más (sen f · h, cos f · h, v), con f =
    // (90 − z) en radianes.
    joint.targetPosition.x = F32(D(joint.velocity.y) + D(joint.targetPosition.x));
    joint.targetPosition.y = F32(D(joint.velocity.z) + D(joint.targetPosition.y));
    const double facing = (90.0 - D(joint.angles.z)) * Radians;
    const auto x = Render::Models::Detail::SineDoubleProductPC53(facing, *horizontal);
    const auto y = Render::Models::Detail::CosineDoubleProductPC53(facing, *horizontal);
    if (!x || !y)
        return Core::Unexpected(std::format("0x53b137: la dirección del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    joint.position = {F32(*x + D(joint.targetPosition.x)), F32(*y + D(joint.targetPosition.y)),
                      F32(*vertical + D(joint.targetPosition.z))};
    // 0x53b318..0x53b363: con menos de 10 de vida, la luz por 0,7692308f.
    constexpr std::int32_t FadeLife = 10;
    if (joint.life < FadeLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace Client::Scenes::JointFamilies
