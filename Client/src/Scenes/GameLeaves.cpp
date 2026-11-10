#include "Scenes/GameLeaves.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameLeafClouds.h"
#include "World/ILeafWaveEvaluator.h"
#include "World/ITerrainAttributeQuerySource.h"
#include "World/LeafWeather.h"
#include "World/Leaves.h"
#include "World/Particles.h"
#include "World/TerrainAttributeQuery.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

namespace Client::Scenes
{
namespace
{
// Selectores de 637082..6370e5 y de la cadena de creadores de 5305b3.
constexpr std::int32_t LorenciaMap = 0;
constexpr std::int32_t DeviasMap = 2;
constexpr std::int32_t NoriaMap = 3;
constexpr std::int32_t AtlansMap = 7;
constexpr std::int32_t RainMap = 9;
constexpr std::int32_t IcarusMap = 10;
constexpr std::int32_t FirstEventRainMap = 18;
constexpr std::int32_t LastEventRainMap = 23;
constexpr std::int32_t ReducedLeafMap = 30;
constexpr std::int32_t SpecialLeafMap = 31;
constexpr std::int32_t FirstExternalEffectMap = 33;
constexpr std::int32_t CrywolfMap = 34;
constexpr std::int32_t SecondExternalEffectMap = 54;
constexpr std::int32_t CrywolfRainState = 1;
constexpr std::int32_t CrywolfEffectState = 2;
constexpr std::size_t CrywolfRainCount = 60;
constexpr std::size_t CrywolfEffectCount = 50;
constexpr std::int32_t RainTexture = 102;
// 52fc10/52fd20 y los creadores 52f6b0/52f880, en unidades del mundo.
constexpr int BirthXSpan = 1600;
constexpr int BirthXCenter = 800;
constexpr int BirthYSpan = 1400;
constexpr int BirthYCenter = 500;
constexpr int BirthHeightSpan = 200;
constexpr int SnowHeightBase = 200;
constexpr int RainHeightBase = 300;
constexpr int SnowVariantSpan = 10;
constexpr float FallingPitch = -30;
constexpr int SnowSpeedSpan = 16;
constexpr int SnowSpeedBase = 8;
constexpr int IcarusSpeedSpan = 24;
constexpr int IcarusSpeedBase = 20;
constexpr int RainSlopeSpan = 20;
constexpr std::uint32_t RainSlopeBase = 20;
constexpr std::uint32_t InclinedRainSlopeBase = 30;
constexpr int RainSpeedSpan = 40;
constexpr std::uint32_t EventRainSpeedOffset = 20;
constexpr int EventSpinSpan = 10;
constexpr int EventSpinBase = 30;
constexpr int FlutterSpan = 16;
constexpr int FlutterCenter = 8;
constexpr int OuterSpinSpan = 8;
constexpr int OuterSpinCenter = 4;
constexpr int SplashChoiceSpan = 4;
constexpr std::uint32_t RainBlockedAttribute = 8;
constexpr std::uint32_t IntensityScale = 200;
constexpr std::int32_t IntensityDivisor = 100;
constexpr std::size_t StandardLeafCount = 80;
constexpr std::size_t MapThirtyLeafCount = 40;
constexpr std::int32_t RainTarget = 100;
constexpr double FlutterStep = static_cast<double>(0.1f);
constexpr double SpinStep = static_cast<double>(0.02f);
constexpr double MaximumTravelSquared = 200000.0;
constexpr float SplashHeight = 10;

// Adapta el perfil retenido ya usado por el kernel de clima (ADR0014).
class RetainedWaveEvaluator final : public World::ILeafWaveEvaluator
{
public:
    Core::Expected<void, std::error_code> Evaluate(World::LeafWave wave, float time) override
    {
        m_value = wave == World::LeafWave::First ? Render::Models::Detail::FirstLeafSine(time)
                                                 : Render::Models::Detail::SecondLeafSine(time);
        return {};
    }

    Core::Expected<std::int32_t, std::error_code> Truncate() override
    {
        const auto value = Render::Models::Detail::TruncateLeafSine(m_value.value);
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value.integer));
    }

private:
    Render::Models::Detail::LeafSine m_value{};
};

class BorrowedAttributeQuery final : public World::ITerrainAttributeQuerySource
{
public:
    explicit BorrowedAttributeQuery(const Data::Terrain::TerrainAttributes& attributes) : m_attributes(attributes) {}
    float ReadScale() override
    {
        return 0.01f;
    }
    const Data::Terrain::TerrainAttributes& Attributes() override
    {
        return m_attributes;
    }

private:
    const Data::Terrain::TerrainAttributes& m_attributes;
};

float F32(double value)
{
    return static_cast<float>(value);
}

bool EventRain(std::int32_t map)
{
    return map >= FirstEventRainMap && map <= LastEventRainMap;
}

bool CallerRunsLeaves(const GameLeafContext& context)
{
    constexpr std::int32_t TavernLayer = 4;
    constexpr std::int32_t DeviasRoofLayer = 3;
    constexpr std::int32_t DeviasRoofRange = 10;
    if (context.map == LorenciaMap)
        return context.heroCellLayer != TavernLayer;
    if (context.map == DeviasMap)
        return context.heroCellLayer != DeviasRoofLayer && context.heroCellLayer < DeviasRoofRange;
    return context.map == NoriaMap || context.map == AtlansMap || context.map == RainMap || context.map == IcarusMap ||
           EventRain(context.map) || context.map == ReducedLeafMap || context.map == SpecialLeafMap ||
           context.map == FirstExternalEffectMap || context.map == CrywolfMap || context.map == SecondExternalEffectMap;
}

World::EmberWorld FallingWorld(const GameLeafContext& context)
{
    return {.hero = context.hero,
            .cameraY = context.cameraY,
            .heights = &context.heights,
            .attributes = &context.attributes,
            .fixedHeight = context.fixedHeight};
}

void BirthPosition(World::Leaf& leaf, GameLeafContext& context, int heightSpan, int heightBase)
{
    leaf.position.x = F32(static_cast<double>(context.random.Next() % BirthXSpan - BirthXCenter) + context.hero.x);
    leaf.position.y = F32(static_cast<double>(context.random.Next() % BirthYSpan - BirthYCenter) + context.hero.y);
    leaf.position.z = F32(static_cast<double>(context.random.Next() % heightSpan + heightBase) + context.hero.z);
}

void FallingVelocity(World::Leaf& leaf, float speed)
{
    leaf.velocity =
        Render::Models::OriginalVectorRotate({0, 0, -speed}, Render::Models::OriginalAngleMatrix(leaf.angles));
}

void SpawnSnow(World::Leaf& leaf, GameLeafContext& context)
{
    constexpr int SnowTexture = 100;
    constexpr int LargeSnowTexture = 101;
    constexpr float SnowScale = 5;
    constexpr float LargeSnowScale = 10;
    leaf.type = SnowTexture;
    leaf.scale = SnowScale;
    if (context.random.Next() % SnowVariantSpan == 0)
    {
        leaf.type = LargeSnowTexture;
        leaf.scale = LargeSnowScale;
    }
    BirthPosition(leaf, context, BirthHeightSpan, SnowHeightBase);
    leaf.angles = {FallingPitch, 0, 0};
    FallingVelocity(leaf, static_cast<float>(context.random.Next() % SnowSpeedSpan + SnowSpeedBase));
}

void SpawnRain(World::Leaf& leaf, GameLeafContext& context, bool eventRain)
{
    leaf.type = RainTexture;
    if (eventRain)
    {
        leaf.spin.x = 1;
        leaf.spin.y = static_cast<float>(context.random.Next() % EventSpinSpan + EventSpinBase);
    }
    BirthPosition(leaf, context, BirthHeightSpan, RainHeightBase);
    const bool inclined = context.random.Next() % 2 != 0;
    const auto slope = static_cast<std::uint32_t>(context.random.Next() % RainSlopeSpan);
    // SUB y LEA de 32 bits; el clima puede haber recibido valores fuera del rango nominal.
    const auto slopeBits =
        inclined ? 0u - InclinedRainSlopeBase - slope - static_cast<std::uint32_t>(context.weather.secondWave)
                 : 0u - RainSlopeBase - slope;
    leaf.angles = {static_cast<float>(std::bit_cast<std::int32_t>(slopeBits)), 0, 0};
    const auto speedBits = static_cast<std::uint32_t>(context.random.Next() % RainSpeedSpan) +
                           static_cast<std::uint32_t>(context.weather.firstWave) +
                           (eventRain ? EventRainSpeedOffset : 0u);
    FallingVelocity(leaf, static_cast<float>(std::bit_cast<std::int32_t>(speedBits)));
}

void SpawnIcarusRain(World::Leaf& leaf, GameLeafContext& context)
{
    leaf.type = RainTexture;
    BirthPosition(leaf, context, BirthHeightSpan, SnowHeightBase);
    leaf.angles = {FallingPitch, 0, 0};
    FallingVelocity(leaf, static_cast<float>(context.random.Next() % IcarusSpeedSpan + IcarusSpeedBase));
}

Core::Expected<void, std::string> Spawn(World::Leaf& leaf, GameLeafContext& context, std::size_t slot)
{
    if (context.map == RainMap || (context.map == CrywolfMap && *context.crywolfState == CrywolfRainState))
        SpawnRain(leaf, context, false);
    else if (EventRain(context.map))
        SpawnRain(leaf, context, true);
    else if (context.map == LorenciaMap || context.map == NoriaMap || context.map == AtlansMap)
        World::SpawnDriftingLeaf(leaf, FallingWorld(context), context.random);
    else if (context.map == IcarusMap)
    {
        // 52fc25: multiplicación 32 bits por 200 y división signed por 100.
        const auto product = static_cast<std::uint32_t>(context.weather.current) * IntensityScale;
        const auto limit = std::bit_cast<std::int32_t>(product) / IntensityDivisor;
        if (static_cast<std::int32_t>(slot) < limit)
            SpawnIcarusRain(leaf, context);
    }
    else if (context.map == DeviasMap)
        SpawnSnow(leaf, context);
    else if (context.map == SpecialLeafMap)
        return SpawnGameLeafCloud(leaf, context, false);
    // 4292c0 (33/54) sólo consulta el rectángulo puro 425090, descarta su resultado y
    // devuelve false. No hay creador ni efecto: continúa hasta el final con los campos retenidos.
    else if (context.map == CrywolfMap && *context.crywolfState == CrywolfEffectState)
        return SpawnGameLeafCloud(leaf, context, true);
    return {};
}

void Flutter(World::Leaf& leaf, GameLeafContext& context)
{
    leaf.velocity.x =
        F32(static_cast<double>(context.random.Next() % FlutterSpan - FlutterCenter) * FlutterStep + leaf.velocity.x);
    leaf.velocity.y =
        F32(static_cast<double>(context.random.Next() % FlutterSpan - FlutterCenter) * FlutterStep + leaf.velocity.y);
    const double velocityZ =
        static_cast<double>(context.random.Next() % FlutterSpan - FlutterCenter) * FlutterStep + leaf.velocity.z;
    leaf.velocity.z = F32(velocityZ);
    leaf.position.x = F32(static_cast<double>(leaf.position.x) + leaf.velocity.x);
    leaf.position.y = F32(static_cast<double>(leaf.position.y) + leaf.velocity.y);
    leaf.position.z = F32(velocityZ + leaf.position.z);
    leaf.spin.x =
        F32(static_cast<double>(context.random.Next() % OuterSpinSpan - OuterSpinCenter) * SpinStep + leaf.spin.x);
    leaf.spin.y =
        F32(static_cast<double>(context.random.Next() % FlutterSpan - FlutterCenter) * SpinStep + leaf.spin.y);
    const double spinZ =
        static_cast<double>(context.random.Next() % OuterSpinSpan - OuterSpinCenter) * SpinStep + leaf.spin.z;
    leaf.spin.z = F32(spinZ);
    leaf.angles.x = F32(static_cast<double>(leaf.spin.x) + leaf.angles.x);
    leaf.angles.y = F32(static_cast<double>(leaf.spin.y) + leaf.angles.y);
    leaf.angles.z = F32(spinZ + leaf.angles.z);
    const double x = static_cast<double>(leaf.start.x) - leaf.position.x;
    const double y = static_cast<double>(leaf.start.y) - leaf.position.y;
    const double z = static_cast<double>(leaf.start.z) - leaf.position.z;
    // El NaN también conserva la hoja (`test ah, 1` en 530383).
    if (z * z + y * y + x * x >= MaximumTravelSquared)
        leaf.alive = false;
}

Core::Expected<void, std::string> Splash(World::Leaf& leaf, GameLeafContext& context, bool eventRain, bool heavyRain)
{
    const bool randomType = eventRain || heavyRain;
    const int type =
        randomType && context.random.Next() % SplashChoiceSpan != 0 ? World::Particle68 : World::Particle67;
    const bool alternate = type == World::Particle68;
    const char* address = eventRain   ? (alternate ? "530187" : "53016c")
                          : heavyRain ? (alternate ? "530098" : "53007d")
                                      : "530231";
    if (context.particles == nullptr)
        return Core::Unexpected(std::string(address) + ": falta la tabla de partículas de la lluvia");
    const auto created = context.particles->CreateChecked(type, leaf.position, leaf.angles, leaf.light, 0, 1,
                                                          context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::string(address) + ": falló la creación de la partícula de lluvia");
    return {};
}

Core::Expected<void, std::string> StepRain(World::Leaf& leaf, GameLeafContext& context, bool eventRain, bool heavyRain)
{
    for (glm::vec3::length_type axis = 0; axis < 3; ++axis)
        leaf.position[axis] = F32(static_cast<double>(leaf.velocity[axis]) + leaf.position[axis]);
    const double height = World::TerrainHeightAt(context.heights, context.attributes, leaf.position.x, leaf.position.y,
                                                 context.fixedHeight);
    // 53010e guarda primero la altura en binary32; los otros dos conservan el intermedio.
    const double ground = eventRain ? static_cast<double>(F32(height)) : height;
    const bool hit = ground > static_cast<double>(leaf.position.z) ||
                     (eventRain && (std::isnan(ground) || std::isnan(leaf.position.z)));
    if (!hit)
        return {};
    if (eventRain)
    {
        // 5f82c0 conserva el wrap de DIRECCIÓN, no envuelve cada coordenada de celda.
        BorrowedAttributeQuery source(context.attributes);
        const auto attribute = World::TerrainAttributeQueryAt(source, leaf.position.x, leaf.position.y);
        if (!attribute.HasValue())
            return Core::Unexpected(std::string("530128: atributo de lluvia fuera de la tabla representada"));
        if ((attribute.Value() & RainBlockedAttribute) != 0)
            return {};
    }
    leaf.alive = false;
    leaf.position.z = F32(ground + SplashHeight);
    if (!eventRain && !heavyRain && context.map == IcarusMap)
        return {};
    return Splash(leaf, context, eventRain, heavyRain);
}

Core::Expected<void, std::string> Step(World::Leaf& leaf, GameLeafContext& context)
{
    if (context.map == RainMap || (context.map == CrywolfMap && *context.crywolfState == CrywolfRainState))
        return StepRain(leaf, context, false, true);
    if (EventRain(context.map))
        return StepRain(leaf, context, true, false);
    if (context.map == LorenciaMap || context.map == IcarusMap)
    {
        if (leaf.type == RainTexture)
            return StepRain(leaf, context, false, false);
        Flutter(leaf, context);
        return {};
    }
    World::StepFallingLeaf(leaf, FallingWorld(context), context.random);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepGameLeaves(GameLeafContext& context)
{
    if (!CallerRunsLeaves(context))
        return {};
    std::size_t count = context.map == RainMap ? World::LeafCount : StandardLeafCount;
    if (context.map == IcarusMap || EventRain(context.map))
        context.weather.target = RainTarget;
    else if (context.map == ReducedLeafMap)
        count = MapThirtyLeafCount;
    else if (context.map == CrywolfMap)
    {
        if (context.crywolfState == nullptr)
            return Core::Unexpected(std::string("5304f6: falta el estado de Crywolf"));
        if (*context.crywolfState == CrywolfRainState)
            count = CrywolfRainCount;
        else if (*context.crywolfState == CrywolfEffectState)
            count = CrywolfEffectCount;
    }
    if (context.weather.current > context.weather.target)
        --context.weather.current;
    else if (context.weather.current < context.weather.target)
        ++context.weather.current;
    RetainedWaveEvaluator retained;
    auto* evaluator = context.waveEvaluator != nullptr ? context.waveEvaluator : &retained;
    const auto waves = World::StepLeafWeatherWaves(context.weather, context.worldClock, evaluator);
    if (!waves.HasValue())
        return Core::Unexpected(std::string("53052b: ") + waves.Error().message());
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        auto& leaf = context.leaves.All()[slot];
        Core::Expected<void, std::string> result;
        if (leaf.alive)
            result = Step(leaf, context);
        else
        {
            leaf.light = glm::vec3(1);
            leaf.alive = true;
            result = Spawn(leaf, context, slot);
        }
        if (!result.HasValue())
            return Core::Unexpected(result.Error() + " (hoja " + std::to_string(slot) + ")");
    }
    return {};
}
} // namespace Client::Scenes
