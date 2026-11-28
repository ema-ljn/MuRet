#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <cstdint>
#include <string>

// El 0x5f2 (alta 0x531229..0x53128f y 0x532073, paso 0x5366bd..0x5366ee): la estela de fuego que los meteoros 0xc2/6 y
// /8 de los magos (0x51b098 y 0x51b268) sueltan con ellos de dueño. El dibujo (la /0 sin el tramo 0 y con la luz por
// la vida · 0,1f) está en GameJointDrawing.cpp.
namespace Client::Joints::JointFamilies
{
void CreateFireTrailJoint(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    // 0x531229..0x531231: el ancho. 0x531238..0x531260: la /0, velocidad 0, 20 puntos, vida 20 y la textura del 0x60f.
    // 0x531265..0x53128f: la /1, velocidad 0, 15 puntos y vida 15. 0x532073: las demás sólo la luz, con los puntos y la
    // vida que tenía el lugar. Todas con la luz en 1. Sin rand().
    joint.width = request.scale;
    if (joint.subtype == 0)
    {
        constexpr std::int32_t Points = 20;
        constexpr std::uint16_t SparkleTexture = 0x60f;
        joint.speed = 0;
        joint.maximumCount = Points;
        joint.life = Points;
        joint.texture = SparkleTexture;
    }
    else if (joint.subtype == 1)
    {
        constexpr std::int32_t Points = 15;
        joint.speed = 0;
        joint.maximumCount = Points;
        joint.life = Points;
    }
    joint.light = {1, 1, 1};
    // 0x534cb6: el tope de los puntos.
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
}

Core::Expected<void, std::string> StepFireTrailJoint(Kernel::JointState& joint)
{
    // 0x5366bd..0x5366ee: con dueño (+0x54) vivo (+0) y de tipo 0xc2 (+0x2), la posición es la suya (+0x10); si no,
    // queda donde la dejó el movimiento común. Sigue la cola común (0x53c393).
    if (joint.ownerContext == nullptr)
        return {};
    if (joint.ownerKind != Kernel::JointOwnerKind::ByType && joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
        return Core::Unexpected(std::string("0x5366c8: el dueño del joint 0x5f2 no es un efecto con modelo"));
    constexpr std::int16_t MeteorEffect = 0xc2;
    const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
    if (owner.alive && owner.type == MeteorEffect)
        joint.position = owner.transform.position;
    return {};
}
} // namespace Client::Joints::JointFamilies
