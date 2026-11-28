#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"

#include <bit>
#include <cstdint>

// El 0x60f/2 y el 0x60f/0x18: la gota que el 0x613/2 suelta en su segundo paso (0x5396ff). Nace con la luz blanca y
// su segundo punto de destino, quieta (sin velocidad) y, con vida de 25 o menos, cae sobre el destino: cada paso la
// z suma una velocidad que crece de a 5. La /0x13 (la del 0x62b de las columnas del círculo 0xc3, 0x5236ce y
// 0x523898) es la misma con la velocidad negada: baja. El 0x62b comparte el alta y el paso del 0x60f.
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

// 0x53b339..0x53b363: con vida de menos de 10, la luz por 0,7692308f (0x6ba240).
void Fade(Scenes::JointState& joint)
{
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
    constexpr std::int32_t FadeLife = 10;
    if (joint.life >= FadeLife)
        return;
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * Fading);
}
} // namespace

bool IsDrop(std::int32_t type, std::int32_t subtype)
{
    return (type == DropJoint || type == SecondHelixJoint) &&
           (subtype == DropSubtype || subtype == DarkDropSubtype || subtype == SinkingDropSubtype);
}

void CreateDropJoint(Scenes::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    // 0x533677..0x53368e: lo del 0x60f antes del subtipo (vida 100 y ancho 10, que el subtipo pisa) y 20 puntos.
    constexpr std::int32_t Points = 20;
    joint.maximumCount = Points;
    // 0x5340dc..0x53412d: la /0x18 con la textura 0x62d; vz en rand() % 20 + 35, el ancho, la luz blanca y la vida
    // rand() % 50 + 25. 0x533a28..0x533a40: la /0x13 niega vz (fchs) y sigue en 0x5340fe, sin la textura. 0x5342be:
    // el destino es el segundo punto.
    constexpr std::uint16_t DarkDropTexture = 0x62d;
    constexpr std::int32_t FallBase = 35, LifeBase = 25;
    if (joint.subtype == DarkDropSubtype)
        joint.texture = DarkDropTexture;
    const std::int32_t fall = random.Next() % 20 + FallBase;
    joint.velocity.z = static_cast<float>(joint.subtype == SinkingDropSubtype ? -fall : fall);
    joint.width = request.scale;
    const std::int32_t life = random.Next() % 50;
    joint.light = {1, 1, 1};
    joint.life = life + LifeBase;
    joint.targetPosition = request.secondPosition;
}

void StepDropJoint(Scenes::JointState& joint)
{
    // 0x53b156..0x53b185: con vida de 25 o menos, vz + 5 (el float en +0x9e4, la suma sin redondear en la z), x e y
    // en el destino. 0x53a12c..0x53a15b: la /0x13, vz − 5.
    constexpr std::int32_t FallLife = 25;
    if (joint.life <= FallLife)
    {
        const double fall = joint.subtype == SinkingDropSubtype ? D(joint.velocity.z) - 5.0 : D(joint.velocity.z) + 5.0;
        joint.position.x = joint.targetPosition.x;
        joint.position.y = joint.targetPosition.y;
        joint.velocity.z = F32(fall);
        joint.position.z = F32(fall + D(joint.position.z));
    }
    Fade(joint);
}
} // namespace Client::Joints::JointFamilies
