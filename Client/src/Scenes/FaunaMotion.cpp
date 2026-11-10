#include "Scenes/FaunaMotion.h"

#include "Render/Models/Animation.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaStep.h"
#include "World/Ftol.h"
#include "World/ObjectAlpha.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t FullTurn = 360;
constexpr float FlockDistance = 400;
constexpr float SeparationDistance = 80;
float F32(double value)
{
    return static_cast<float>(value);
}
std::int32_t TargetAngle(float fromX, float fromY, float targetX, float targetY)
{
    // 4e97a0 usa una división y atan, no atan2; dx cero/unordered toma el cociente cero.
    const double dx = static_cast<double>(targetX) - fromX;
    const double quotient = dx != 0 && !std::isnan(dx) ? (static_cast<double>(fromY) - targetY) / dx : 0;
    std::int32_t angle = World::FtolLow(Render::Models::Detail::AtanDegreesProductPC53(quotient));
    if (!(targetX >= fromX))
        angle += FullTurn / 2;
    if (angle < 0)
        angle += FullTurn;
    return (FullTurn - angle) % FullTurn;
}
} // namespace

double FaunaHeight(const FaunaStepContext& context, float x, float y)
{
    return World::TerrainHeightAt(*context.terrain.heights, *context.terrain.attributes, x, y, context.fixedHeight);
}
float FaunaSine(float phase)
{
    // La misma proyección retenido64→R53 que las partículas. El binding limita el argumento finito.
    const auto product = Render::Models::Detail::SineProductPC53(static_cast<double>(phase), 1.0f);
    return product ? F32(*product) : std::numeric_limits<float>::quiet_NaN();
}
double FaunaWave(float sine)
{
    return sine >= 0 ? static_cast<double>(sine) : -static_cast<double>(sine);
}
std::int32_t TurnFaunaAngle(std::int32_t from, std::int32_t target, std::int32_t step)
{
    // 4e94c0: los callers entregan ángulos acotados. Igualdad conserva el extraño EAX=from.
    const std::int32_t difference = from - target;
    const std::int32_t distance = std::abs(difference);
    std::int32_t delta = from;
    if (from > target)
        delta = distance < std::abs(target - from + FullTurn) ? -std::min(step, distance) : std::min(step, distance);
    if (from < target)
        delta = distance < std::abs(difference + FullTurn) ? std::min(step, distance) : -std::min(step, distance);
    return (from + delta + FullTurn) % FullTurn;
}
void SteerFauna(FaunaObject& object, std::size_t index, std::span<FaunaObject> pool)
{
    double sumX = 0, sumY = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < pool.size(); ++i)
    {
        const auto& neighbor = pool[i];
        if (neighbor.active == 0 || i == index)
            continue;
        const double dx = static_cast<double>(object.position.x) - neighbor.position.x;
        const double dy = static_cast<double>(object.position.y) - neighbor.position.y;
        const double distance = std::sqrt(dx * dx + dy * dy);
        const float savedDistance = F32(distance);
        if (distance >= FlockDistance)
            continue;
        double x = static_cast<double>(neighbor.aheadX) - neighbor.position.x;
        const float yDirection = F32(static_cast<double>(neighbor.aheadY) - neighbor.position.y);
        const double targetX = static_cast<double>(neighbor.aheadX) - object.position.x;
        const double targetY = static_cast<double>(neighbor.aheadY) - object.position.y;
        const bool separate = !(savedDistance >= SeparationDistance);
        x = separate ? x - targetX : x + targetX;
        const float y =
            F32(separate ? static_cast<double>(yDirection) - targetY : static_cast<double>(yDirection) + targetY);
        const float norm = F32(std::sqrt(x * x + static_cast<double>(y) * y));
        sumX += x / norm;
        sumY += static_cast<double>(y) / norm;
        ++count;
    }
    if (count == 0)
        return;
    const float savedY = F32(sumY);
    const float denominator = static_cast<float>(count);
    const float targetX = F32(sumX / denominator + object.position.x);
    const float targetY = F32(static_cast<double>(savedY) / denominator + object.position.y);
    const auto target = TargetAngle(object.position.x, object.position.y, targetX, targetY);
    object.angles.z =
        static_cast<float>(TurnFaunaAngle(World::FtolLow(object.angles.z), target, World::FtolLow(object.turnRate)));
}
void SetFaunaAction(FaunaObject& object, const Render::Models::Skeleton& skeleton, std::uint8_t action)
{
    if (action >= skeleton.actions.size() || action == object.action)
        return;
    // 4ea030 guarda el frame completo, a diferencia del cruce entero de 4ec200.
    object.previousAction = object.action;
    object.previousFrame = object.frame;
    object.action = action;
    object.frame = 0;
}
void AdvanceFaunaAnimation(FaunaObject& object, const Render::Models::Skeleton& skeleton, float speed)
{
    if (object.action >= skeleton.actions.size())
        return;
    Render::Models::AnimationState animation{object.action, object.frame, object.previousAction,
                                             static_cast<std::int16_t>(World::FtolLow(object.previousFrame))};
    const auto before = World::FtolLow(object.frame);
    const auto& action = skeleton.actions[object.action];
    if (action.keyCount > 1 && World::FtolLow(static_cast<double>(object.frame) + speed) != before)
        object.previousFrame = static_cast<float>(before);
    static_cast<void>(
        Render::Models::AdvanceAnimation(animation, action, speed, Render::Models::AnimationScene::Other));
    object.frame = animation.frame;
    object.previousAction = animation.previousAction;
}
void FinalizeFauna(FaunaObject& object)
{
    World::StepObjectAlpha(object.alphaTarget, object.alpha, object.ceiling, object.alphaMode);
}
} // namespace Client::Scenes
