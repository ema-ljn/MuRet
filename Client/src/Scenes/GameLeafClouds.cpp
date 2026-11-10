#include "Scenes/GameLeafClouds.h"

#include "Core/Expected.h"
#include "GameLogic/MovementSpeed.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameLeaves.h"
#include "World/Leaves.h"
#include "World/Particles.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
// 43ff20 y 425090: mismo rectángulo estricto, sin stores ni llamadas.
constexpr float CloudAreaMinX = 5449;
constexpr float CloudAreaMaxX = 17822;
constexpr float CloudAreaMinY = 6784;
constexpr float CloudAreaMaxY = 22419;
constexpr int CloudChanceSpan = 30;
constexpr int CloudDirectionCount = 8;
constexpr int CloudOffsetSpan = 250;
constexpr int CloudGroundOffsetSpan = 20;
constexpr float CrywolfCloudHeight = 400;
constexpr float CloudScale = 0.4f;
constexpr std::int32_t CloudSubtype = 8;
constexpr double MovementCompensation = -45.0;
constexpr std::array<int, CloudDirectionCount> Map31Offsets{300, 250, 200, 300, 400, 400, 400, 400};
constexpr std::array<int, CloudDirectionCount> CrywolfOffsets{100, 50, 0, 100, 200, 200, 200, 200};
constexpr std::array<int, CloudDirectionCount> CloudSignX{1, 1, -1, -1, 1, -1, 0, 0};
constexpr std::array<int, CloudDirectionCount> CloudSignY{1, -1, 1, -1, 0, 0, 1, -1};

bool InsideMap31CloudArea(const glm::vec3& hero)
{
    return hero.x > CloudAreaMinX && hero.x < CloudAreaMaxX && hero.y > CloudAreaMinY && hero.y < CloudAreaMaxY;
}

float CloudCoordinate(float hero, int offset, int sign, Render::Models::CrtRandom& random)
{
    if (sign == 0)
        return hero;
    const int displacement = random.Next() % CloudOffsetSpan + offset;
    return static_cast<float>(static_cast<double>(hero) + static_cast<double>(sign * displacement));
}

glm::vec3 CloudPosition(GameLeafContext& context, bool crywolf)
{
    const auto direction = static_cast<std::size_t>(context.random.Next() % CloudDirectionCount);
    const int offset = crywolf ? CrywolfOffsets[direction] : Map31Offsets[direction];
    glm::vec3 position{0, 0, crywolf ? CrywolfCloudHeight : 0};
    position.x = CloudCoordinate(context.hero.x, offset, CloudSignX[direction], context.random);
    position.y = CloudCoordinate(context.hero.y, offset, CloudSignY[direction], context.random);
    return position;
}

Core::Expected<void, std::string> CompensateMovingHero(glm::vec3& position, GameLeafContext& context, bool crywolf)
{
    if (context.movementSpeed == nullptr)
        return Core::Unexpected(std::string(crywolf ? "43586c" : "443c27") + ": falta el estado de velocidad");
    auto& actor = *context.cloudHero;
    const auto matrix = Render::Models::OriginalAngleMatrix(actor.angles);
    const GameLogic::MovementSpeedInput input{.type = actor.type,
                                              .baseSpeed = actor.baseMovementSpeed,
                                              .item = actor.pet.model,
                                              .wings = actor.wings.model,
                                              .movementMode = actor.byte88,
                                              .itemVariant = actor.pet.flags1b,
                                              .safeZone = actor.safeZone,
                                              .flags = actor.flags78,
                                              .map = context.map};
    const float speed = GameLogic::UpdateMovementSpeed(actor.run, *context.movementSpeed, input);
    const auto offset = Render::Models::OriginalVectorRotate(
        {0, static_cast<float>(static_cast<double>(speed) * MovementCompensation), 0}, matrix);
    for (glm::vec3::length_type axis = 0; axis < 3; ++axis)
        position[axis] = static_cast<float>(static_cast<double>(offset[axis]) + position[axis]);
    return {};
}

Core::Expected<void, std::string> CreateCloud(glm::vec3 position, GameLeafContext& context, bool crywolf)
{
    if (!crywolf)
    {
        // 443c96 guarda la altura en binary32 ANTES del rand%20 de 443c9e.
        const auto height = static_cast<float>(
            World::TerrainHeightAt(context.heights, context.attributes, position.x, position.y, context.fixedHeight));
        position.z = static_cast<float>(static_cast<double>(context.random.Next() % CloudGroundOffsetSpan) + height);
    }
    const char* address = crywolf ? "4358ed" : "443cd1";
    if (context.particles == nullptr)
        return Core::Unexpected(std::string(address) + ": falta el pool de partículas de las nubes");
    const glm::vec3 light = crywolf ? glm::vec3(0.07f) : glm::vec3(0.05f, 0.05f, 0.1f);
    const auto created = context.particles->CreateChecked(World::CloudParticle, position, glm::vec3(0), light,
                                                          CloudSubtype, CloudScale, context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::string(address) + ": falló la creación de la nube 622/8");
    return {};
}
} // namespace

Core::Expected<void, std::string> SpawnGameLeafCloud(World::Leaf& leaf, GameLeafContext& context, bool crywolf)
{
    if (!crywolf && !InsideMap31CloudArea(context.hero))
        return {};
    if (context.random.Next() % CloudChanceSpan != 0)
        return {};
    glm::vec3 position = CloudPosition(context, crywolf);
    if (context.cloudHero == nullptr)
        return Core::Unexpected(std::string(crywolf ? "435848" : "443c03") + ": falta el héroe de las nubes");
    if (context.cloudHero->movement != 0)
    {
        const auto compensated = CompensateMovingHero(position, context, crywolf);
        if (!compensated.HasValue())
            return compensated;
    }
    // El segundo byte Movement se relee tras 508940, como 443c71/4358b6.
    if (context.cloudHero->movement != 0 || context.random.Next() % 2 == 0)
    {
        const auto created = CreateCloud(position, context, crywolf);
        if (!created.HasValue())
            return created;
    }
    leaf.alive = false;
    return {};
}
} // namespace Client::Scenes
