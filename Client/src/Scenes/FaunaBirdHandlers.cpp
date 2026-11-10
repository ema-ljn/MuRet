#include "Scenes/FaunaStep.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaMotion.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaState.h"
#include "World/Ftol.h"
#include "World/IBirdStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <system_error>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t FullTurn = 360;
constexpr std::int16_t IcarusBird = 0x171;
constexpr std::int16_t KalimaBird = 0x173;
float F32(double x)
{
    return static_cast<float>(x);
}
std::int32_t Random(FaunaStepContext& c)
{
    return c.cloth.random.Next();
}
std::error_code Unsupported()
{
    return std::make_error_code(std::errc::function_not_supported);
}
bool IsKalima(std::int32_t map)
{
    return (map >= 24 && map <= 29) || map == 36;
}
double Distance(const glm::vec3& a, const glm::vec3& b)
{
    const double x = static_cast<double>(a.x) - b.x, y = static_cast<double>(a.y) - b.y;
    return std::sqrt(x * x + y * y);
}
std::int32_t ReadSteps(FaunaStepContext& c)
{
    const auto steps = c.cloth.logicSteps;
    if (c.cloth.openCounterAccesses == 0)
        for (int i = 0; i < 4; ++i)
            static_cast<void>(Random(c));
    return steps;
}
std::error_code Prepare(FaunaStepContext& c, std::int32_t model)
{
    if (c.effects.prepare)
        return c.effects.prepare(model);
    return c.models.SkeletonOf(static_cast<std::uint16_t>(model + 0x152)) != nullptr ? std::error_code{}
                                                                                     : Unsupported();
}
void Select(FaunaStepContext& c, FaunaObject& b, std::uint8_t action)
{
    if (const auto* model = c.models.SkeletonOf(static_cast<std::uint16_t>(b.type)))
        SetFaunaAction(b, *model, action);
}
std::error_code Effect(FaunaStepContext& c, FaunaObject& b, std::int32_t type, std::int32_t subtype, float scale)
{
    if (!c.effects.effect)
        return Unsupported();
    auto result = c.effects.effect({type, b.position, b.position, b.angles, subtype, &b, scale, -1, 0, 0, -1});
    return result.HasValue() ? std::error_code{} : result.Error();
}
std::error_code SpawnIcarus(FaunaStepContext& c, FaunaObject& b, std::int32_t index)
{
    if (index < 3)
    {
        b.active = 1;
        if (auto error = Prepare(c, 0x1f))
            return error;
        b.type = IcarusBird;
        b.bodyScale = F32((Random(c) % 3 + 6) * static_cast<double>(0.05f));
        b.alpha = b.alphaTarget = 1;
        b.scale = F32((Random(c) % 10 + 10) * static_cast<double>(0.02f));
        const auto turn = Random(c) % 10 + 10;
        b.vertexLight = 1;
        b.alphaMode = 0;
        b.counter = 0;
        b.action = 7;
        b.hiddenMesh = b.blendMesh = -1;
        b.turnRate = F32(turn * static_cast<double>(0.05f));
        Select(c, b, 7);
        b.angles.x = b.angles.y = 0;
        b.angles.z = static_cast<float>(Random(c) % FullTurn);
        b.position.x = F32((Random(c) % 4000 - 2000) + static_cast<double>(c.heroPosition.x));
        b.position.y = F32((Random(c) % 4000 - 2000) + static_cast<double>(c.heroPosition.y));
        b.position.z = F32(static_cast<double>(c.heroPosition.z) - 600);
        return {};
    }
    b.light = glm::vec3(1);
    b.type = 0x10f;
    b.scale = 2.2f;
    b.vertexLight = 0;
    b.timer = 9600;
    b.motionState = 2;
    b.alphaMode = 1;
    b.bodyScale = 0.8f;
    b.size = 10;
    b.hiddenMesh = b.blendMesh = -1;
    b.phase = F32((Random(c) % 314) * static_cast<double>(0.01f));
    b.position.x = F32((Random(c) % 1024 - 512) + static_cast<double>(c.heroPosition.x));
    b.position.y = F32((Random(c) % 1024 - 512) + static_cast<double>(c.heroPosition.y));
    b.position.z = c.heroPosition.z;
    if (auto error = Effect(c, b, 0x10f, c.map >= 11 && c.map <= 17 ? 3 : 1, 25))
        return error;
    b.angles = glm::vec3(0);
    b.angles.z = static_cast<float>(Random(c) % FullTurn);
    return {};
}
void SpawnAtlans(FaunaStepContext& c, FaunaObject& b)
{
    if (static_cast<double>(c.heroPosition.y) * static_cast<double>(0.01f) >= 128)
    {
        b.active = 0;
        return;
    }
    b.type = static_cast<std::int16_t>(Random(c) % 2 + 0xb3);
    b.turnRate = 15;
    b.alphaMode = 1;
    b.bodyScale = .8f;
    b.size = 10;
    b.hiddenMesh = b.blendMesh = -1;
    b.phase = F32((Random(c) % 314) * static_cast<double>(0.01f));
    b.scale = Random(c) % 100 < 90 ? .3f : .25f;
    b.vertexLight = 0;
    b.angles = glm::vec3(0);
    b.light = glm::vec3(1);
    b.position.x = F32((Random(c) % 1024 - 512) + static_cast<double>(c.heroPosition.x));
    b.position.y = F32((Random(c) % 1024 - 512) + static_cast<double>(c.heroPosition.y));
    b.position.z = c.heroPosition.z;
    const float height = F32(FaunaHeight(c, b.position.x, b.position.y));
    b.position.z = F32(static_cast<double>(height) + (Random(c) % 200 + 150));
}
std::error_code SpawnKalima(FaunaStepContext& c, FaunaObject& b)
{
    if (ReadSteps(c) % 100 != 0)
    {
        b.active = 0;
        return {};
    }
    b.active = 1;
    if (auto error = Prepare(c, 0x21))
        return error;
    b.type = KalimaBird;
    b.bodyScale = F32((Random(c) % 3 + 6) * static_cast<double>(.05f) + 2.5);
    b.alpha = b.alphaTarget = 1;
    b.scale = F32((Random(c) % 10 + 10) * static_cast<double>(.04f));
    const auto turn = Random(c) % 3;
    b.vertexLight = 1;
    b.alphaMode = 0;
    b.counter = 0;
    b.hiddenMesh = 5;
    b.blendMesh = -1;
    b.timer = 200;
    b.action = 2;
    b.turnRate = static_cast<float>(turn) - 1.5f;
    Select(c, b, 2);
    b.angles.x = b.angles.y = 0;
    b.angles.z = static_cast<float>(90 - Random(c) % 30 - 180);
    const float x = F32(static_cast<double>(c.heroPosition.x) - 1000);
    b.position.x = F32(static_cast<double>(x) - Random(c) % 200);
    b.position.y = F32((static_cast<double>(c.heroPosition.y) - 500) + Random(c) % 200);
    b.position.z = F32(static_cast<double>(c.heroPosition.z) - 800);
    return {};
}
void MoveAe(FaunaStepContext& c, FaunaObject& b)
{
    if (Random(c) % 32 == 0)
    {
        b.angles.z = static_cast<float>(Random(c) % FullTurn);
        b.vertical = static_cast<float>(Random(c) % 15 - 7);
    }
    b.vertical = F32((Random(c) % 15 - 7) * static_cast<double>(.2f) + b.vertical);
    const double h = FaunaHeight(c, b.position.x, b.position.y);
    if (h + 50 > b.position.z)
        b.vertical = F32(static_cast<double>(b.vertical) * .8f + 1);
    if (!(h + 300 >= b.position.z))
        b.vertical = F32(static_cast<double>(b.vertical) * .8f - 1);
    b.position.z = F32((Random(c) % 15 - 7) * static_cast<double>(.3f) + b.position.z);
}
void TakeOff(FaunaObject& b)
{
    b.motionState = 3;
    b.scale = 1.1f;
    b.vertical = 20;
    b.action = 0;
}
void MoveAdBc(FaunaStepContext& c, FaunaObject& b)
{
    if (b.motionState == 0)
    {
        if (World::FtolLow(c.worldTime) % 8192 < 2048)
        {
            const double distance = Distance(b.position, c.heroPosition);
            if (distance >= 200 && distance <= 400)
                b.motionState = 1;
        }
        b.scale = 1;
        const double z = static_cast<double>(b.position.z) + (Random(c) % 16 - 8);
        b.position.z = F32(z);
        if (!(z >= 200))
            b.vertical = 10;
        else if (z > 600)
            b.vertical = -10;
    }
    if (b.motionState == 1)
    {
        b.vertical = -20;
        if (FaunaHeight(c, b.position.x, b.position.y) > b.position.z)
            TakeOff(b);
    }
    if (b.motionState == 2 && (c.heroAction >= 13 || Random(c) % 256 == 0))
        TakeOff(b);
    if (b.motionState == 3)
    {
        b.position.z = F32(static_cast<double>(b.position.z) + (Random(c) % 16 - 8));
        const double scale = static_cast<double>(b.scale) - static_cast<double>(.005f);
        b.scale = F32(scale);
        if (!(scale > 1))
            b.motionState = 0;
    }
}
void Move173(FaunaStepContext& c, FaunaObject& b)
{
    b.angles.z = F32(static_cast<double>(b.angles.z) + b.turnRate);
    if (Random(c) % 5 == 0)
        b.turnRate = -b.turnRate;
    if (b.timer < 20)
    {
        b.alpha = F32(static_cast<double>(b.alpha) * std::bit_cast<float>(0x3f555555U));
        b.scale = F32(static_cast<double>(b.scale) + .5);
        b.angles.x = F32(static_cast<double>(b.angles.x) + 2);
    }
    if (b.timer < 0)
        b.active = 0;
}
void Move10f(FaunaStepContext& c, FaunaObject& b, std::int32_t index)
{
    const auto steps = ReadSteps(c);
    const auto sine = Render::Models::Detail::SineProductPC53(b.angles.z, b.scale);
    const auto cosine = Render::Models::Detail::CosineProductPC53(b.angles.z, b.scale);
    if (sine)
        b.position.x = F32(*sine + b.position.x);
    if (cosine)
        b.position.y = F32(static_cast<double>(b.position.y) - *cosine);
    const auto a = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(steps) +
                                               5161U * static_cast<std::uint32_t>(index) + 17732U);
    const auto z = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(steps) +
                                               41273U * static_cast<std::uint32_t>(index) + 34571U);
    // Las dos trascendentes permanecen retenidas en el original; el perfil portable declara su límite.
    const auto wave =
        Render::Models::Detail::SineCosineProductPC53(static_cast<double>(a) * .0003f, static_cast<double>(z) * .0003f);
    if (wave)
        b.angles.z = F32(*wave * .01f + b.angles.z);
    if (Distance(b.position, c.heroPosition) >= 1500)
        b.active = 0;
    if (Random(c) % 5120 == 0)
        b.active = 0;
    if (c.map >= 11 && c.map <= 17 && b.timer <= 0)
        b.active = 0;
}
void CommonMotion(FaunaStepContext& c, FaunaObject& b, std::int32_t index)
{
    if (b.motionState == 2)
        return;
    if (b.type != 0xae || Random(c) % 4 == 0)
        SteerFauna(b, static_cast<std::size_t>(index), c.fauna.Birds());
    b.matrix = Render::Models::OriginalAngleMatrix(b.angles);
    float length;
    if (c.map == 7)
    {
        if (!(b.phase >= 5))
        {
            length = F32((Random(c) % 16 + (index < 35 ? 8 : 16)) * static_cast<double>(b.scale));
            b.turnRate = 15;
        }
        else
        {
            length = F32((Random(c) % 32 + 32) * static_cast<double>(b.scale));
            b.turnRate = 5;
        }
        const double phase = static_cast<double>(b.phase) + static_cast<double>(.1f);
        b.phase = F32(phase);
        if (phase >= 10)
            b.phase = 0;
    }
    else
        length = F32(static_cast<double>(b.scale) * 25);
    const auto delta = Render::Models::OriginalVectorRotate({length, 0, b.vertical}, b.matrix);
    const double newX = static_cast<double>(delta.x) + b.position.x;
    b.position.x = F32(newX);
    b.position.y = F32(static_cast<double>(delta.y) + b.position.y);
    b.position.z = F32(static_cast<double>(delta.z) + b.position.z);
    b.aheadX = F32(static_cast<double>(delta.x) * 3 + newX);
    b.aheadY = F32(static_cast<double>(delta.y) * 3 + b.position.y);
    const double dx = newX - c.heroPosition.x, dy = static_cast<double>(b.position.y) - c.heroPosition.y;
    const float distance = F32(std::sqrt(dx * dx + dy * dy));
    const float limit = b.type == IcarusBird ? 4000.0f : b.type == KalimaBird ? 3000.0f : 1500.0f;
    if (b.type != IcarusBird && b.type != KalimaBird && Random(c) % 512 == 0)
        b.active = 0;
    if (distance >= limit)
        b.active = 0;
}
} // namespace
World::BirdCallResult RunFaunaBirdHandler(FaunaStepContext& c, World::BirdHandler handler, FaunaObject& b,
                                          std::int32_t index)
{
    switch (handler)
    {
    case World::BirdHandler::SpawnIcarus:
        if (c.map != 10)
            return {0, {}};
        return {1, SpawnIcarus(c, b, index)};
    case World::BirdHandler::SpawnAtlans:
        if (c.map != 7)
            return {0, {}};
        SpawnAtlans(c, b);
        return {1, {}};
    case World::BirdHandler::SpawnKalima:
        if (!IsKalima(c.map))
            return {0, {}};
        return {1, SpawnKalima(c, b)};
    case World::BirdHandler::MoveAf:
        b.position.z = F32(FaunaHeight(c, b.position.x, b.position.y));
        b.position.z = F32(static_cast<double>(b.position.z) - FaunaWave(FaunaSine(b.phase)) * 150 + 350);
        b.phase = F32(static_cast<double>(b.phase) + static_cast<double>(.2f));
        break;
    case World::BirdHandler::MoveAe:
        MoveAe(c, b);
        break;
    case World::BirdHandler::MoveAdBc:
        MoveAdBc(c, b);
        break;
    case World::BirdHandler::Move10f:
        Move10f(c, b, index);
        break;
    case World::BirdHandler::Move173:
        Move173(c, b);
        break;
    case World::BirdHandler::CommonMotion:
        CommonMotion(c, b, index);
        break;
    case World::BirdHandler::Finalize:
        FinalizeFauna(b);
        break;
    case World::BirdHandler::SelectAction:
        Select(c, b, static_cast<std::uint8_t>(index));
        break;
    }
    return {0, {}};
}
} // namespace Client::Scenes
