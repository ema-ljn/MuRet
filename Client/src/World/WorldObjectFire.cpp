#include "World/WorldObjectFire.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainLight.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/IWorldObjectStepServices.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectParticleRequest.h"
#include "World/WorldObjectStepContext.h"
#include "World/WorldObjectStepError.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>

namespace Client::World
{
namespace
{
// 52f360 f4c2/f4cd/f4d6: conservar L extendida entre los tres stores.
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);
constexpr float GreenFactor = std::bit_cast<float>(0x3f19999au);
constexpr float BlueFactor = std::bit_cast<float>(0x3ecccccdu);

Core::Expected<void, WorldObjectStepError> EmitSmoke(std::int32_t fireClass, WorldObject& object, glm::vec3& position,
                                                     WorldObjectStepContext& context)
{
    if (context.random.Next() % 2 != 0)
    {
        return {};
    }
    const WorldObjectParticleRequest request{
        0x5f2, position, object.transform.angles, object.light, fireClass == 1 ? 0 : 2, 1.0f, 0};
    return context.services.CreateParticle(request, context.random);
}

Core::Expected<void, WorldObjectStepError> EmitFire(WorldObject& object, glm::vec3& position,
                                                    WorldObjectStepContext& context)
{
    const double intensity = static_cast<double>(context.random.Next() % 6 + 6) * static_cast<double>(Tenth);
    glm::vec3 color{static_cast<float>(intensity), static_cast<float>(intensity * static_cast<double>(GreenFactor)),
                    static_cast<float>(intensity * static_cast<double>(BlueFactor))};
    if (context.random.Next() % 2 == 0)
    {
        const std::int32_t subtype = context.random.Next() % 4;
        const WorldObjectParticleRequest request{0x5d9, position, object.transform.angles, color, subtype, 1.0f, 0};
        const auto emitted = context.services.CreateParticle(request, context.random);
        if (!emitted.HasValue())
        {
            return Core::Unexpected(emitted.Error());
        }
    }
    // 52f521..534 relee P/C después del productor síncrono. No usar una copia previa.
    const Data::Terrain::TerrainLightColor light{color.x, color.y, color.z};
    AddTerrainLight(context.terrainLight, position.x, position.y, light, 4);
    return {};
}
} // namespace

Core::Expected<void, WorldObjectStepError> EmitWorldObjectFire(std::int32_t fireClass, WorldObject& object,
                                                               const glm::vec3& offset, WorldObjectStepContext& context)
{
    const auto matrix = Render::Models::OriginalAngleMatrix(object.transform.angles);
    glm::vec3 position = Render::Models::OriginalVectorRotate(offset, matrix);
    position.x = static_cast<float>(static_cast<double>(position.x) + static_cast<double>(object.transform.position.x));
    position.y = static_cast<float>(static_cast<double>(position.y) + static_cast<double>(object.transform.position.y));
    position.z = static_cast<float>(static_cast<double>(position.z) + static_cast<double>(object.transform.position.z));
    position.x =
        static_cast<float>(static_cast<double>(context.random.Next() % 16 - 8) + static_cast<double>(position.x));
    position.y =
        static_cast<float>(static_cast<double>(context.random.Next() % 16 - 8) + static_cast<double>(position.y));
    position.z =
        static_cast<float>(static_cast<double>(context.random.Next() % 16 - 8) + static_cast<double>(position.z));
    if (fireClass == 0)
    {
        return EmitFire(object, position, context);
    }
    if (fireClass == 1 || fireClass == 2)
    {
        return EmitSmoke(fireClass, object, position, context);
    }
    return {};
}
} // namespace Client::World
