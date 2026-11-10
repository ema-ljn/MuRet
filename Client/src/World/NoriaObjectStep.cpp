#include "World/NoriaObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/ObjectModelStepState.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <format>
#include <string>

namespace Client::World
{
Core::Expected<void, std::string> StepNoriaObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                  const ObjectStepGlobals& globals)
{
    constexpr float HalfThousandth = std::bit_cast<float>(0x3a03126fu);
    constexpr float Thousandth = std::bit_cast<float>(0x3a83126fu);
    constexpr float TwoThousandths = std::bit_cast<float>(0x3b03126fu);
    if (object.type == 0x27)
    {
        object.state.blendMesh = 1; // 605461: salto compartido, no el switch del mapa4.
        return {};
    }
    if (object.type != 0x12 && object.type != 0x29 && object.type != 0x2a && object.type != 0x2b &&
        object.type != 0x142)
        return {};
    if (object.type == 0x29)
        object.state.blendMesh = 0;
    if (object.type == 0x2a || object.type == 0x2b)
    {
        if (globals.modelState == nullptr)
            return Core::Unexpected(std::string("0x6052e1/0x6053a5: falta el estado durable model+88 de Noria"));
        globals.modelState->unlitMeshes[object.type] = 0;
    }
    if (object.type == 0x142)
    {
        const glm::vec3 position(object.transform.position.x,
                                 static_cast<float>(static_cast<double>(object.transform.position.y) - 50.0),
                                 static_cast<float>(static_cast<double>(object.transform.position.z) + 350.0));
        if (globals.particles == nullptr)
            return Core::Unexpected(std::string("0x60536e: falta el pool para5c6/9 de Noria"));
        const auto emitted =
            globals.particles->CreateChecked(SparkParticle, position, object.transform.angles, glm::vec3(0.5f),
                                             MapSparkSubtype, 1.4f, globals.worldClock, random);
        if (!emitted.HasValue())
            return Core::Unexpected(std::format("0x60536e:5c6/9 rechazado({})", static_cast<int>(emitted.Error())));
    }
    const auto clock = OriginalFtolLow(static_cast<double>(globals.worldClock));
    if (!clock)
        return Core::Unexpected(std::string("0x605281:ftol del reloj de Noria fuera de dominio"));
    if (object.type == 0x29)
        object.state.uv.y = static_cast<float>(static_cast<double>(*clock % 2000) * HalfThousandth);
    else if (object.type == 0x2a || object.type == 0x2b)
        object.state.uv.x = static_cast<float>(static_cast<double>(*clock % 500) *
                                               (object.type == 0x2a ? -TwoThousandths : TwoThousandths));
    else
        object.state.uv.y = static_cast<float>(static_cast<double>(*clock % 1000) * Thousandth);
    return {};
}
} // namespace Client::World
