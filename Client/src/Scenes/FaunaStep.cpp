#include "Scenes/FaunaStep.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainLight.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaMotion.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaState.h"
#include "World/Birds.h"
#include "World/Fish.h"
#include "World/Ftol.h"
#include "World/IBirdStepServices.h"
#include "World/IFishStepServices.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <system_error>

namespace Client::Scenes
{
namespace
{
std::error_code Unsupported()
{
    return std::make_error_code(std::errc::function_not_supported);
}
FaunaObject& Object(World::BirdRecord bird)
{
    // BirdRecord fue producido con as_writable_bytes del mismo objeto vivo y alineado.
    return *reinterpret_cast<FaunaObject*>(bird.data());
}
struct ModelBorrow
{
    const Render::Models::Skeleton* skeleton = nullptr;
    std::uint8_t action = 0;
};
class BirdServices final : public World::IBirdStepServices
{
public:
    explicit BirdServices(FaunaStepContext& context) : m_context(context) {}
    std::int32_t Map() override
    {
        return m_context.map;
    }
    std::int32_t Mode() override
    {
        return m_context.storm;
    }
    const glm::vec3& ActorPosition() override
    {
        return m_context.heroPosition;
    }
    std::int32_t Random() override
    {
        return m_context.cloth.random.Next();
    }
    std::int32_t ConvertTileCoordinate(double value) override
    {
        return World::FtolLow(value);
    }
    std::int32_t TileIndex(std::int32_t x, std::int32_t y) override
    {
        return static_cast<std::int32_t>(World::FishTileIndex(x, y));
    }
    const std::uint16_t& TileAttributes(std::int32_t index) override
    {
        return m_context.terrain.attributes->cells[static_cast<std::size_t>(index)];
    }
    std::error_code PrefixEffect(const glm::vec3& position) override
    {
        return m_context.effects.lightning ? m_context.effects.lightning(position) : Unsupported();
    }
    std::error_code PrefixLight(float x, float y) override
    {
        World::AddTerrainLight(m_context.lighting, x, y, Data::Terrain::TerrainLightColor{-.3f, -.3f, -.2f}, 16);
        return {};
    }
    std::error_code Audio(std::int32_t id, std::byte* owner) override
    {
        m_context.fauna.Sound(id, owner);
        return {};
    }
    std::error_code PrepareSpecial() override
    {
        if (m_context.effects.prepare)
            return m_context.effects.prepare(0x1f);
        return m_context.models.SkeletonOf(0x171) != nullptr ? std::error_code{} : Unsupported();
    }
    World::BirdCallResult Call(World::BirdHandler handler, World::BirdRecord bird, std::int32_t index) override
    {
        return RunFaunaBirdHandler(m_context, handler, Object(bird), index);
    }
    World::BirdModelBorrow BorrowModel(std::int16_t type) override
    {
        auto& model = m_models[type];
        model.skeleton = m_context.models.SkeletonOf(static_cast<std::uint16_t>(type));
        return model.skeleton ? World::BirdModelBorrow{&model, &model.action}
                              : World::BirdModelBorrow{nullptr, nullptr};
    }
    std::error_code Pose(World::BirdModelBorrow model, World::BirdRecord bird, float factor) override
    {
        const auto& borrow = *static_cast<ModelBorrow*>(model.identity);
        AdvanceFaunaAnimation(Object(bird), *borrow.skeleton, factor);
        return {};
    }
    std::error_code Rotation(World::BirdRecord bird) override
    {
        auto& object = Object(bird);
        object.matrix = Render::Models::OriginalAngleMatrix(object.angles);
        return {};
    }
    glm::vec3 Transform(World::BirdRecord bird, const glm::vec3& vector) override
    {
        return Render::Models::OriginalVectorRotate(vector, Object(bird).matrix);
    }
    double Height(float x, float y) override
    {
        return FaunaHeight(m_context, x, y);
    }
    float SinSpill(float phase) override
    {
        return FaunaSine(phase);
    }
    double Wave(float sine) override
    {
        return FaunaWave(sine);
    }
    bool AudioDistance(const glm::vec3& bird, const glm::vec3& actor) override
    {
        const double x = static_cast<double>(bird.x) - actor.x, y = static_cast<double>(bird.y) - actor.y;
        return !(std::sqrt(x * x + y * y) >= 600);
    }

private:
    FaunaStepContext& m_context;
    std::map<std::int16_t, ModelBorrow> m_models;
};
class FishServices final : public World::IFishStepServices
{
public:
    explicit FishServices(FaunaStepContext& context) : m_context(context) {}
    Core::Expected<std::int32_t, std::error_code> Random() override
    {
        return m_context.cloth.random.Next();
    }
    Core::Expected<std::int32_t, std::error_code> Truncate(double value) override
    {
        return World::FtolLow(value);
    }
    Core::Expected<std::uint8_t, std::error_code> EventGate() override
    {
        return static_cast<std::uint8_t>(World::FishSpawnEventGate(m_context.map));
    }
    Core::Expected<std::uint32_t, std::error_code> CreateEffect(const World::FishEffectRequest& request) override
    {
        if (!m_context.effects.effect)
            return Core::Unexpected(Unsupported());
        return m_context.effects.effect(request);
    }
    Core::Expected<double, std::error_code> Height(float x, float y) override
    {
        return FaunaHeight(m_context, x, y);
    }
    Core::Expected<World::FishModelStepView, std::error_code> LookupModel(std::int16_t type) override
    {
        auto& model = m_models[type];
        model.skeleton = m_context.models.SkeletonOf(static_cast<std::uint16_t>(type));
        if (!model.skeleton)
            return Core::Unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        return World::FishModelStepView{model.action, &model};
    }
    Core::Expected<std::uint8_t, std::error_code> AdvanceModel(World::FishModelStepView model,
                                                               World::FishStepView& fish, float speed) override
    {
        auto& object = *static_cast<FaunaObject*>(fish.context);
        AdvanceFaunaAnimation(object, *static_cast<ModelBorrow*>(model.identity)->skeleton, speed);
        return std::uint8_t{1};
    }
    Core::Expected<void, std::error_code> Move(World::FishStepView& fish, std::int32_t mode) override
    {
        auto& object = *static_cast<FaunaObject*>(fish.context);
        const auto* model = object.type == -1 ? m_context.prefixModel
                                              : m_context.models.SkeletonOf(static_cast<std::uint16_t>(object.type));
        // 4ea030 conserva su índice firmado: el prefijo de la tabla necesita préstamo explícito.
        if (!model)
            return Core::Unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        SetFaunaAction(object, *model, static_cast<std::uint8_t>(mode));
        return {};
    }
    Core::Expected<void, std::error_code> Steer(World::FishStepView& fish, std::size_t index,
                                                std::span<World::FishStepView, World::FishPoolSize>) override
    {
        SteerFauna(*static_cast<FaunaObject*>(fish.context), index, m_context.fauna.Fish());
        return {};
    }
    Core::Expected<void, std::error_code> AngleMatrix(World::FishStepView& fish) override
    {
        fish.matrix = Render::Models::OriginalAngleMatrix(fish.angles);
        return {};
    }
    Core::Expected<glm::vec3, std::error_code> Rotate(const glm::vec3& input,
                                                      const Render::Models::OriginalMatrix& matrix) override
    {
        return Render::Models::OriginalVectorRotate(input, matrix);
    }
    Core::Expected<std::int32_t, std::error_code> Sound(std::int32_t id, void* owner, std::int32_t) override
    {
        m_context.fauna.Sound(id, owner);
        return std::int32_t{0};
    }
    Core::Expected<void, std::error_code> Finalize(World::FishStepView& fish) override
    {
        FinalizeFauna(*static_cast<FaunaObject*>(fish.context));
        return {};
    }
    Core::Expected<double, std::error_code> OscillationProductPC53(float phase) override
    {
        const auto product = Render::Models::Detail::SineProductPC53(phase, .4f);
        if (!product)
            return Core::Unexpected(std::make_error_code(std::errc::result_out_of_range));
        return *product;
    }

private:
    FaunaStepContext& m_context;
    std::map<std::int16_t, ModelBorrow> m_models;
};
bool HasTerrain(const FaunaStepContext& context)
{
    return context.terrain.heights != nullptr && context.terrain.attributes != nullptr &&
           context.terrain.mapping != nullptr;
}
} // namespace
Core::Expected<void, std::string> StepGameBirds(FaunaStepContext& context)
{
    if (!HasTerrain(context))
        return Core::Unexpected(std::string("446120: terreno de fauna ausente"));
    auto birds = context.fauna.BirdRecords();
    BirdServices services(context);
    const auto result = World::StepBirds(birds, services);
    if (result.error)
        return Core::Unexpected("446120: " + result.error.message());
    return {};
}
Core::Expected<void, std::string> StepGameFish(FaunaStepContext& context)
{
    if (!HasTerrain(context))
        return Core::Unexpected(std::string("447030: terreno de fauna ausente"));
    auto fish = context.fauna.FishViews();
    const World::FishHeroView heroView{context.heroPosition, context.heroFlag};
    const World::FishHeroView* hero = &heroView;
    World::FishStepWorld world{context.map, hero, context.terrain.mapping->layer1, context.terrain.attributes->cells};
    FishServices services(context);
    const auto result = World::StepFish(fish, world, services);
    if (!result.HasValue())
        return Core::Unexpected("447030: " + result.Error().message());
    return {};
}
Core::Expected<void, std::string> StepGameFauna(FaunaStepContext& context)
{
    auto birds = StepGameBirds(context);
    if (!birds.HasValue())
        return birds;
    return StepGameFish(context);
}
} // namespace Client::Scenes
