#include "World/AtlansObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <bit>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x6b89bc (0,002f), 0x6b89e8 (0,004f), 0x6b89dc (0,3f) y 0x6b882c (0,5).
constexpr double SlowRate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
constexpr double FastRate = static_cast<double>(std::bit_cast<float>(0x3b83126fu));
constexpr float Swing = std::bit_cast<float>(0x3e99999au);
constexpr double Half = 0.5;

// fld del reloj, fmul por `rate`, fsin y fmul por `factor`, en la x87.
std::optional<double> Wave(float worldClock, double rate, float factor)
{
    return Render::Models::Detail::SineProductPC53(static_cast<double>(worldClock) * rate, factor);
}

Core::Expected<void, std::string> SetSwingLight(WorldObject& object, float worldClock, double rate,
                                                std::string_view address)
{
    const std::optional<double> wave = Wave(worldClock, rate, Swing);
    if (!wave)
        return Core::Unexpected(std::format("{}: el seno del reloj no cabe en el perfil PC53", address));
    object.state.light = F32(*wave + Half);
    return {};
}

// 0x60556e..0x6055c8: la fase sube 0,1f (0x6b87b8) con fst, la suma sin redondear se compara con 10
// (0x6b87a8) y la fase guardada con 5 (0x6b87f8); test ah, 0x41 deja pasar sólo el mayor ordenado.
Core::Expected<void, std::string> StepBubbles(WorldObject& object, Render::Models::CrtRandom& random,
                                              const ObjectStepGlobals& globals)
{
    constexpr double Rise = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double Ceiling = 10.0;
    constexpr float Threshold = 5.0f;
    const double phase = static_cast<double>(object.field84) + Rise;
    object.state.hiddenMesh = HiddenObject;
    object.field84 = F32(phase);
    if (phase > Ceiling)
        object.field84 = 0.0f; // EDX, todavía en 0 desde 0x604d50.
    if (!(object.field84 > Threshold))
        return {};
    if (globals.particles == nullptr)
        return Core::Unexpected(std::string("0x6055c8: la burbuja 0x607 del 0x16 pide el pool de partículas "
                                            "0x743a198"));
    const auto created =
        globals.particles->CreateChecked(DropParticle, object.transform.position, object.transform.angles, object.light,
                                         0, 1.0f, globals.worldClock, random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x6055c8: la burbuja 0x607/0 del 0x16 falla con error {}",
                                            static_cast<unsigned>(created.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepAtlansObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                   const ObjectStepGlobals& globals)
{
    switch (object.type)
    {
    case 0x16:
        return StepBubbles(object, random, globals);
    case 0x17:
        object.state.blendMesh = 0; // 0x6055d7.
        return SetSwingLight(object, globals.worldClock, SlowRate, "0x6055e7");
    case 0x20:
    case 0x22:
    {
        // 0x6055fe..0x605620: fsin, fadd 1 (0x6b87d4) con un solo redondeo y fmul 0,5, en la x87.
        object.state.blendMesh = 1;
        const std::optional<double> wave =
            Render::Models::Detail::SineSumPC53(static_cast<double>(globals.worldClock) * FastRate, 1.0);
        if (!wave)
            return Core::Unexpected(std::string("0x605612: el seno del reloj no cabe en el perfil PC53"));
        object.state.light = F32(*wave * Half);
        return {};
    }
    case 0x26:
        object.state.blendMesh = 0; // 0x605629.
        return {};
    case 0x28:
    {
        // 0x605633..0x60565b: el blendMesh, la velocidad (0x3d4ccccd) antes del fsin y la luz.
        constexpr float SlowAnimation = std::bit_cast<float>(0x3d4ccccdu);
        object.state.blendMesh = 0;
        object.speed = SlowAnimation;
        return SetSwingLight(object, globals.worldClock, FastRate, "0x60564d");
    }
    default:
        return {};
    }
}
} // namespace Client::World
