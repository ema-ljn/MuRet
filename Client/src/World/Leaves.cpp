#include "World/Leaves.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/LeafWeather.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace Client::World
{
namespace
{
constexpr std::int32_t LeafMap31 = 31;
constexpr std::int32_t LeafMap35 = 35;
constexpr std::int32_t LeafMap34 = 34;
} // namespace

bool LeafMap31Predicate(std::int32_t map)
{
    return map == LeafMap31;
}

bool LeafMapIsNot35(std::int32_t map)
{
    return map != LeafMap35;
}

bool LeafMap34Predicate(std::int32_t map)
{
    return map == LeafMap34;
}

Core::Expected<bool, std::error_code> LeafPointWithinFiniteStableBounds(const float& x, const float& y,
                                                                        const LeafWorldBoundsView& bounds)
{
    const float firstX = x;
    if (!std::isfinite(firstX))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const float lowerX = bounds.lowerX;
    if (!std::isfinite(lowerX))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!(firstX > lowerX))
        return false;

    const float secondX = x;
    if (!std::isfinite(secondX))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const float upperX = bounds.upperX;
    if (!std::isfinite(upperX))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!(secondX < upperX))
        return false;

    const float firstY = y;
    if (!std::isfinite(firstY))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const float lowerY = bounds.lowerY;
    if (!std::isfinite(lowerY))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!(firstY > lowerY))
        return false;

    const float secondY = y;
    if (!std::isfinite(secondY))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const float upperY = bounds.upperY;
    if (!std::isfinite(upperY))
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    return secondY < upperY;
}

namespace
{
// 0x42b1e0, en el mapa 0x37: la escala (rand() % 50) · 0,01 (0x6b86c0) + 0,5 (0x6b882c); la posición, el
// héroe más (rand() % 1600 − 800, rand() % 1400 − 500, rand() % 1000 + 50); la velocidad x
// (rand() % 64 + 64) · −0,1 (0x6b8930), y si la y del nacimiento queda debajo de la cámara + 400
// (0x6b87b0), 3,2 (0x6b8a6c) menos esa; las demás, de a 0,1 (0x6b87b8).
constexpr double ScaleStep = static_cast<double>(0.01f);
constexpr double ScaleBase = 0.5;
constexpr double FallSpeed = static_cast<double>(-0.1f);
constexpr double Tenth = static_cast<double>(0.1f);
constexpr float LeafC3Scale = -0.1f;
constexpr float LeafC3Step = 0.1f;
constexpr double SpinStep = static_cast<double>(0.02f);
constexpr double DriftingDistanceSquared = 200000.0;
constexpr float CameraMargin = 400.0f;
constexpr float Bounce = 3.2f;
// 0x5303e8: en el suelo, la luz baja 0,05 (0x6b8a48) por paso; en 0 se apaga.
constexpr double Fade = static_cast<double>(0.05f);
constexpr std::int32_t LorenciaLeafTexture = 100;
constexpr int LorenciaHeightSpan = 300;
constexpr int EmberHeightSpan = 1000;
constexpr std::int32_t RainTexture = 0x66;
constexpr std::int32_t HighRainIndex = 300;
constexpr std::uint32_t DensityMultiplier = 200;
constexpr std::int32_t DensityDivisor = 100;
constexpr double RainImpactHeight = 10;
constexpr float CastleEmberMinX = 4700;
constexpr float CastleEmberMaxX = 14800;
constexpr float CastleEmberMinY = 5300;
constexpr float CastleEmberMaxY = 23900;

enum class LeafBirthLight : std::uint8_t
{
    Preserve,
    Ember,
};

float F32(double value)
{
    return static_cast<float>(value);
}

// rand() % n con el idiom de `and 0x8000000f` (n potencia de 2) o con cdq e idiv: con un rand() no
// negativo, los dos son el resto.
int Around(Render::Models::CrtRandom& random, int span, int shift)
{
    return (random.Next() % span) + shift;
}

float HeroOffset(Render::Models::CrtRandom& random, int span, int shift, const float& coordinate)
{
    const int offset = Around(random, span, shift);
    return F32(static_cast<double>(offset) + static_cast<double>(coordinate));
}

void PlaceExtendedRain(Leaf& leaf, const RainLeafWorld& world, std::int32_t index, Render::Models::CrtRandom& random)
{
    leaf.position.x = HeroOffset(random, 1600, -800, world.hero.x);
    if (index < HighRainIndex)
        leaf.position.y = HeroOffset(random, 1400, -500, world.hero.y);
    else
    {
        const int offset = Around(random, 300, 1000);
        const double y = static_cast<double>(offset) + static_cast<double>(world.hero.y);
        leaf.position.y = F32(y - static_cast<double>(world.weather.phase));
    }
    leaf.position.z = HeroOffset(random, 200, 300, world.hero.z);
}

void RotateRainVelocity(Leaf& leaf, float vertical)
{
    const Render::Models::OriginalMatrix matrix = Render::Models::OriginalAngleMatrix(leaf.angles);
    leaf.velocity = Render::Models::OriginalVectorRotate({0, 0, vertical}, matrix);
}

void FinishExtendedRain(Leaf& leaf, const LeafWeather& weather, std::uint32_t extraSpeed,
                        Render::Models::CrtRandom& random)
{
    const int parity = random.Next() % 2;
    const int sample = random.Next() % 20;
    std::uint32_t tilt = static_cast<std::uint32_t>(-20 - sample);
    if (parity != 0)
        tilt = static_cast<std::uint32_t>(-30 - sample) - static_cast<std::uint32_t>(weather.secondWave);
    leaf.angles.x = static_cast<float>(std::bit_cast<std::int32_t>(tilt));
    leaf.angles.y = 0;
    leaf.angles.z = 0;
    const int speedSample = random.Next() % 40;
    const std::uint32_t speed =
        static_cast<std::uint32_t>(speedSample) + static_cast<std::uint32_t>(weather.firstWave) + extraSpeed;
    // FCHS sigue al FILD signed: INT_MIN se niega como FP, y cero produce -0.
    const auto vertical = F32(-static_cast<double>(std::bit_cast<std::int32_t>(speed)));
    RotateRainVelocity(leaf, vertical);
}

// Común a 0x52fa60 y al nacimiento de brasas: nueve draws, posición y velocidad retenida.
void PlaceLeaf(Leaf& leaf, const glm::vec3& hero, const float& cameraY, int heightSpan, LeafBirthLight light,
               Render::Models::CrtRandom& random)
{
    const int offsetX = Around(random, 1600, -800);
    const auto x = F32(static_cast<double>(offsetX) + static_cast<double>(hero.x));
    const int offsetY = Around(random, 1400, -500);
    const auto y = F32(static_cast<double>(offsetY) + static_cast<double>(hero.y));
    const int offsetZ = Around(random, heightSpan, 50);
    const auto z = F32(static_cast<double>(offsetZ) + static_cast<double>(hero.z));
    leaf.position.x = x;
    leaf.position.y = y;
    leaf.start.x = x;
    leaf.start.y = y;
    leaf.position.z = z;
    leaf.start.z = z;
    const double fall = static_cast<double>(Around(random, 64, 64)) * FallSpeed;
    leaf.velocity.x = F32(fall);
    // `fcomp`: la cámara + 400 contra la y; con `test ah, 0x41` (menor o igual, o sin orden) no cambia.
    const double camera = static_cast<double>(cameraY) + static_cast<double>(CameraMargin);
    if (!(camera < static_cast<double>(y) || camera == static_cast<double>(y) || std::isnan(camera) || std::isnan(y)))
    {
        leaf.velocity.x = F32(static_cast<double>(Bounce) - fall);
    }
    leaf.velocity.y = F32(static_cast<double>(Around(random, 32, -16)) * Tenth);
    leaf.velocity.z = F32(static_cast<double>(Around(random, 32, -16)) * Tenth);
    leaf.spin.x = F32(static_cast<double>(Around(random, 16, -8)) * Tenth);
    leaf.spin.y = F32(static_cast<double>(Around(random, 64, -32)) * Tenth);
    const int spinZ = Around(random, 16, -8);
    if (light == LeafBirthLight::Ember)
    {
        // 42b457..42b474: los tres stores de luz preceden al último store de spin.
        leaf.light.x = 1;
        leaf.light.y = 0;
        leaf.light.z = 0;
    }
    leaf.spin.z = F32(static_cast<double>(spinZ) * Tenth);
}

void InitializeEmberScale(Leaf& leaf, Render::Models::CrtRandom& random)
{
    leaf.type = EmberTexture;
    leaf.scale = F32((static_cast<double>(random.Next() % 50) * ScaleStep) + ScaleBase);
}

void Born(Leaf& leaf, const EmberWorld& world, Render::Models::CrtRandom& random)
{
    InitializeEmberScale(leaf, random);
    PlaceLeaf(leaf, world.hero, world.cameraY, EmberHeightSpan, LeafBirthLight::Ember, random);
}

double JitterValue(float& value, Render::Models::CrtRandom& random, int span, int shift, double rate)
{
    const double extended = (static_cast<double>(Around(random, span, shift)) * rate) + static_cast<double>(value);
    value = F32(extended);
    return extended;
}

double JitterLeafVelocity(Leaf& leaf, Render::Models::CrtRandom& random)
{
    JitterValue(leaf.velocity.x, random, 16, -8, Tenth);
    JitterValue(leaf.velocity.y, random, 16, -8, Tenth);
    return JitterValue(leaf.velocity.z, random, 16, -8, Tenth);
}

void AdvanceLeafPosition(Leaf& leaf, double rise, double z)
{
    leaf.position.x = F32(static_cast<double>(leaf.position.x) + static_cast<double>(leaf.velocity.x));
    leaf.position.y = F32(static_cast<double>(leaf.velocity.y) + static_cast<double>(leaf.position.y));
    leaf.position.z = F32(rise + z);
}

// 0x5303a0: con el terreno por debajo (o un NaN), en el aire: cada componente de la velocidad suma
// (rand() % 16 − 8) · 0,1 y la posición, la velocidad (la z, con la de z antes de guardarla en float). Si
// no, queda en el suelo y se apaga de a 0,05; con el rojo en 0 o menos, muere.
void FallAtHeight(Leaf& leaf, double ground, Render::Models::CrtRandom& random)
{
    const auto z = static_cast<double>(leaf.position.z);
    if (ground < z || std::isnan(ground) || std::isnan(z))
    {
        const double rise = JitterLeafVelocity(leaf, random);
        AdvanceLeafPosition(leaf, rise, z);
        return;
    }
    leaf.position.z = F32(ground);
    const double red = static_cast<double>(leaf.light.x) - Fade;
    leaf.light.x = F32(red);
    leaf.light.y = F32(static_cast<double>(leaf.light.y) - Fade);
    leaf.light.z = F32(static_cast<double>(leaf.light.z) - Fade);
    // `fcomp` con 0 y `test ah, 0x41`: mayor sigue; menor, igual o sin orden, muere.
    if (!(red > 0.0))
    {
        leaf.alive = false;
    }
}
void Fall(Leaf& leaf, const EmberWorld& world, Render::Models::CrtRandom& random)
{
    const double ground =
        TerrainHeightAt(*world.heights, *world.attributes, leaf.position.x, leaf.position.y, world.fixedHeight);
    FallAtHeight(leaf, ground, random);
}

double LeafHeightAt(const LeafTerrainWorld& world, float x, float y)
{
    const std::int32_t scene = world.scene;
    if (scene == 0 || scene == 1 || scene == 3)
        return 0;
    return TerrainHeightAt(world.heights, world.attributes, x, y, world.fixedHeight);
}

glm::vec2 IntegrateRainLeaf(Leaf& leaf)
{
    leaf.position.x = F32(static_cast<double>(leaf.velocity.x) + static_cast<double>(leaf.position.x));
    const float x = leaf.position.x;
    leaf.position.y = F32(static_cast<double>(leaf.velocity.y) + static_cast<double>(leaf.position.y));
    const float y = leaf.position.y;
    leaf.position.z = F32(static_cast<double>(leaf.velocity.z) + static_cast<double>(leaf.position.z));
    return {x, y};
}

Core::Expected<bool, std::error_code> EmitRainImpact(Leaf& leaf, double height, Render::Models::CrtRandom& random,
                                                     const LeafSplash& splash)
{
    const double corrected = height + RainImpactHeight;
    leaf.alive = 0;
    leaf.position.z = F32(corrected);
    const int sample = random.Next();
    const std::int32_t type = sample % 4 == 0 ? 0x67 : 0x68;
    if (!splash)
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    LeafSplashRequest request{leaf.position, leaf.angles, leaf.light};
    request.type = type;
    const std::error_code error = splash(request);
    if (error)
        return Core::Unexpected(error);
    return true;
}

} // namespace

std::span<Leaf> Leaves::All()
{
    return m_leaves;
}

std::span<const Leaf> Leaves::All() const
{
    return m_leaves;
}

bool Leaves::Any() const
{
    for (const Leaf& leaf : m_leaves)
    {
        if (leaf.alive)
        {
            return true;
        }
    }
    return false;
}

void Leaves::Clear()
{
    for (Leaf& leaf : m_leaves)
    {
        leaf.alive = false;
    }
}

void StepLoginEmbers(Leaves& leaves, const EmberWorld& world, Render::Models::CrtRandom& random)
{
    if (world.heights == nullptr || world.attributes == nullptr)
    {
        return;
    }
    const std::span<Leaf> all = leaves.All();
    for (std::size_t k = 0; k < LoginEmberCount; ++k)
    {
        Leaf& leaf = all[k];
        if (!leaf.alive)
        {
            // 0x5305a0: la luz en 1 y el byte vivo, y después el creador del mapa.
            leaf.light = glm::vec3(1.0f);
            leaf.alive = true;
            Born(leaf, world, random);
        }
        else
        {
            Fall(leaf, world, random);
        }
    }
}

Core::Expected<void, std::error_code> VisitLeafPool(Leaves& leaves, std::size_t prefix, const std::int32_t& selector,
                                                    const LeafPoolHandlers& handlers)
{
    if (prefix > LeafCount)
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const std::span<Leaf> pool = leaves.All();
    for (std::size_t i = 0; i < prefix; ++i)
    {
        Leaf& leaf = pool[i];
        // El flag se lee una vez para elegir creación/update, sin reelección dentro de la cadena.
        if (leaf.alive == 0)
        {
            leaf.light.x = 1;
            leaf.light.y = 1;
            leaf.light.z = 1;
            leaf.alive = 1;
            bool recognized = false;
            for (const LeafCreator& creator : handlers.creators)
            {
                if (!creator)
                    return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
                const auto result = creator(leaf, static_cast<std::int32_t>(i));
                if (!result.HasValue())
                    return Core::Unexpected(result.Error());
                if (result.Value() != 0)
                {
                    recognized = true;
                    break;
                }
            }
            // 530639: S se relee solo después de nueve AL0, incluyendo efectos persistentes deC9.
            if (!recognized && selector == 2)
            {
                if (!handlers.fallback)
                    return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
                const auto result = handlers.fallback(leaf, static_cast<std::int32_t>(i));
                if (!result.HasValue())
                    return Core::Unexpected(result.Error());
            }
            continue;
        }
        bool updated = false;
        for (const LeafUpdater& updater : handlers.updaters)
        {
            if (!updater)
                return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
            const auto result = updater(leaf);
            if (!result.HasValue())
                return Core::Unexpected(result.Error());
            if (result.Value() != 0)
            {
                updated = true;
                break;
            }
        }
        if (!updated)
        {
            if (!handlers.fall)
                return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
            const auto result = handlers.fall(leaf);
            if (!result.HasValue())
                return Core::Unexpected(result.Error());
        }
    }
    return {};
}

Core::Expected<void, std::error_code> StepWorldLeaves(Leaves& leaves, LeafWeather& weather, const std::int32_t& map,
                                                      const std::int32_t& selector, const float& time,
                                                      ILeafWaveEvaluator* evaluator, const LeafPoolHandlers& handlers)
{
    const std::int32_t initialMap = map;
    // 4303c0 es puro; Searly se consulta únicamente si reconoce34.
    const std::int32_t initialSelector = initialMap == 34 ? selector : 0;
    const std::size_t prefix = PrepareLeafWeather(weather, initialMap, initialSelector);
    const auto waves = StepLeafWeatherWaves(weather, time, evaluator);
    if (!waves.HasValue())
        return Core::Unexpected(waves.Error());
    return VisitLeafPool(leaves, prefix, selector, handlers);
}

bool CreateLorenciaLeaf(Leaf& leaf, const LeafCreationWorld& world, Render::Models::CrtRandom& random)
{
    if (world.map != 0)
        return false;
    // Convenience binding for tests/legacy callers; Core should pass its live hero pointer and tuning refs directly.
    const glm::vec3* hero = &world.hero;
    const LeafC3World c3{world.map, hero, world.cameraY, LeafC3Scale, CameraMargin, Bounce, LeafC3Step};
    const LeafRandomDraw draw = [&random]() -> Core::Expected<std::int32_t, std::error_code> { return random.Next(); };
    return CreateMapZeroLeafC3(leaf, c3, draw).Value() != 0;
}

namespace
{
Core::Expected<std::uint8_t, std::error_code> CreateMapZeroLeafC3Impl(Leaf& leaf, const std::int32_t& map,
                                                                      const float* publishedCameraY, const float& scale,
                                                                      const float& thresholdInput, const float& mirror,
                                                                      const float& step, const LeafHeroRead& heroReader,
                                                                      const LeafRandomDraw& draw)
{
    constexpr std::int32_t AdmittedMap = 0;
    constexpr std::int32_t XSpan = 1600;
    constexpr std::int32_t XShift = -800;
    constexpr std::int32_t YSpan = 1400;
    constexpr std::int32_t YShift = -500;
    constexpr std::int32_t ZSpan = 300;
    constexpr std::int32_t ZShift = 50;
    constexpr std::int32_t VelocitySpan = 64;
    constexpr std::int32_t VelocityShift = 64;
    constexpr std::int32_t DriftSpan = 32;
    constexpr std::int32_t DriftShift = -16;
    constexpr std::int32_t SpinSpan = 16;
    constexpr std::int32_t SpinShift = -8;
    constexpr std::int32_t SpinYShift = -32;

    if (map != AdmittedMap)
        return std::uint8_t{0};

    const auto next = [&]() -> Core::Expected<std::int32_t, std::error_code>
    {
        if (!draw)
            return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
        return draw();
    };
    const auto readHero = [&]() -> Core::Expected<const glm::vec3*, std::error_code>
    {
        if (!heroReader)
            return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
        const auto hero = heroReader();
        if (!hero.HasValue())
            return Core::Unexpected(hero.Error());
        if (hero.Value() == nullptr)
            return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
        return hero.Value();
    };

    leaf.type = LorenciaLeafTexture;

    const auto r1 = next();
    if (!r1.HasValue())
        return Core::Unexpected(r1.Error());
    const std::int32_t offsetX = (r1.Value() % XSpan) + XShift;
    const auto heroX = readHero();
    if (!heroX.HasValue())
        return Core::Unexpected(heroX.Error());
    const float x = F32(static_cast<double>(offsetX) + static_cast<double>(heroX.Value()->x));

    const auto r2 = next();
    if (!r2.HasValue())
        return Core::Unexpected(r2.Error());
    const std::int32_t offsetY = (r2.Value() % YSpan) + YShift;
    const auto heroY = readHero();
    if (!heroY.HasValue())
        return Core::Unexpected(heroY.Error());
    const float y = F32(static_cast<double>(offsetY) + static_cast<double>(heroY.Value()->y));

    const auto r3 = next();
    if (!r3.HasValue())
        return Core::Unexpected(r3.Error());
    const std::int32_t offsetZ = (r3.Value() % ZSpan) + ZShift;
    const auto heroZ = readHero();
    if (!heroZ.HasValue())
        return Core::Unexpected(heroZ.Error());
    const float z = F32(static_cast<double>(offsetZ) + static_cast<double>(heroZ.Value()->z));

    leaf.position.x = x;
    leaf.position.y = y;
    leaf.start.x = x;
    leaf.start.y = y;
    leaf.position.z = z;
    leaf.start.z = z;

    const auto r4 = next();
    if (!r4.HasValue())
        return Core::Unexpected(r4.Error());
    const std::int32_t velocityOffset = (r4.Value() % VelocitySpan) + VelocityShift;
    const double velocity = static_cast<double>(velocityOffset) * static_cast<double>(scale);
    leaf.velocity.x = F32(velocity);
    if (publishedCameraY == nullptr)
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const float cameraY = *publishedCameraY;
    const float threshold = thresholdInput;
    const double cameraThreshold = static_cast<double>(cameraY) + static_cast<double>(threshold);
    if (cameraThreshold > static_cast<double>(y))
        leaf.velocity.x = F32(static_cast<double>(mirror) - velocity);

    const auto r5 = next();
    if (!r5.HasValue())
        return Core::Unexpected(r5.Error());
    const std::int32_t velocityYOffset = (r5.Value() % DriftSpan) + DriftShift;
    leaf.velocity.y = F32(static_cast<double>(velocityYOffset) * static_cast<double>(step));

    const auto r6 = next();
    if (!r6.HasValue())
        return Core::Unexpected(r6.Error());
    const std::int32_t velocityZOffset = (r6.Value() % DriftSpan) + DriftShift;
    leaf.velocity.z = F32(static_cast<double>(velocityZOffset) * static_cast<double>(step));

    const auto r7 = next();
    if (!r7.HasValue())
        return Core::Unexpected(r7.Error());
    const std::int32_t spinXOffset = (r7.Value() % SpinSpan) + SpinShift;
    leaf.spin.x = F32(static_cast<double>(spinXOffset) * static_cast<double>(step));

    const auto r8 = next();
    if (!r8.HasValue())
        return Core::Unexpected(r8.Error());
    const std::int32_t spinYOffset = (r8.Value() % VelocitySpan) + SpinYShift;
    leaf.spin.y = F32(static_cast<double>(spinYOffset) * static_cast<double>(step));

    const auto r9 = next();
    if (!r9.HasValue())
        return Core::Unexpected(r9.Error());
    const std::int32_t spinZOffset = (r9.Value() % SpinSpan) + SpinShift;
    leaf.spin.z = F32(static_cast<double>(spinZOffset) * static_cast<double>(step));
    return std::uint8_t{1};
}
} // namespace

Core::Expected<std::uint8_t, std::error_code> CreateMapZeroLeafC3(Leaf& leaf, const LeafC3World& world,
                                                                  const LeafRandomDraw& draw)
{
    const LeafHeroRead readHero = [&world]() -> Core::Expected<const glm::vec3*, std::error_code>
    { return world.hero; };
    return CreateMapZeroLeafC3Impl(leaf, world.map, &world.cameraY, world.scale, world.threshold, world.mirror,
                                   world.step, readHero, draw);
}

Core::Expected<std::uint8_t, std::error_code> CreateMapZeroLeafC3(Leaf& leaf, const std::int32_t& map,
                                                                  const float* cameraY, const LeafHeroRead& readHero,
                                                                  const LeafRandomDraw& draw)
{
    // 52fa60/direct-data: los mismos f32 de la implementación revisada, prestados sin copia.
    static_assert(std::bit_cast<std::uint32_t>(LeafC3Scale) == 0xbdcccccdu);
    static_assert(std::bit_cast<std::uint32_t>(CameraMargin) == 0x43c80000u);
    static_assert(std::bit_cast<std::uint32_t>(Bounce) == 0x404ccccdu);
    static_assert(std::bit_cast<std::uint32_t>(LeafC3Step) == 0x3dcccccdu);
    return CreateMapZeroLeafC3Impl(leaf, map, cameraY, LeafC3Scale, CameraMargin, Bounce, LeafC3Step, readHero, draw);
}

bool CreateSnowLeaf(Leaf& leaf, const LeafCreationWorld& world, Render::Models::CrtRandom& random)
{
    return CreateSnowLeaf(leaf, world.map, world.hero, random);
}

bool CreateSnowLeaf(Leaf& leaf, const std::int32_t& map, const glm::vec3& hero, Render::Models::CrtRandom& random)
{
    if (map != 2)
        return false;
    leaf.type = LorenciaLeafTexture;
    leaf.scale = 5;
    if (random.Next() % 10 == 0)
    {
        leaf.type = 101;
        leaf.scale = 10;
    }
    leaf.position.x = HeroOffset(random, 1600, -800, hero.x);
    leaf.position.y = HeroOffset(random, 1400, -500, hero.y);
    // 52fdd1: leer Z antes de escribir los ángulos; no anticipar estos stores.
    const float z = HeroOffset(random, 200, 200, hero.z);
    leaf.angles.x = -30;
    leaf.angles.y = 0;
    leaf.angles.z = 0;
    leaf.position.z = z;
    const int speed = Around(random, 16, 8);
    RotateRainVelocity(leaf, F32(-static_cast<double>(speed)));
    return true;
}

bool CreateNoriaAtlansLeaf(Leaf& leaf, const LeafCreationWorld& world, Render::Models::CrtRandom& random)
{
    const std::int32_t map = world.map;
    if (map != 3 && map != 7)
        return false;
    leaf.type = LorenciaLeafTexture;
    PlaceLeaf(leaf, world.hero, world.cameraY, LorenciaHeightSpan, LeafBirthLight::Preserve, random);
    return true;
}

bool CreateEmberLeaf(Leaf& leaf, const LeafCreationWorld& world, const std::uint8_t& castlePresentation,
                     Render::Models::CrtRandom& random)
{
    const std::int32_t initialMap = world.map;
    if (initialMap != 55)
    {
        if (initialMap != 30 || castlePresentation == 0)
            return false;
        // Getter4294b0 puro; cada comparación conserva su lectura. Unordered rechaza solo en mínimos.
        const float xMin = world.hero.x;
        if (!(xMin >= CastleEmberMinX))
            return false;
        const float xMax = world.hero.x;
        if (xMax > CastleEmberMaxX)
            return false;
        const float yMin = world.hero.y;
        if (!(yMin >= CastleEmberMinY))
            return false;
        const float yMax = world.hero.y;
        if (yMax > CastleEmberMaxY)
            return false;
    }
    InitializeEmberScale(leaf, random);
    // 42b281: nueva lectura después del primer RNG, antes de las coordenadas.
    const int heightSpan = world.map == 55 ? EmberHeightSpan : LorenciaHeightSpan;
    PlaceLeaf(leaf, world.hero, world.cameraY, heightSpan, LeafBirthLight::Ember, random);
    return true;
}

bool CreateExtendedRainLeaf(Leaf& leaf, const RainLeafWorld& world, std::int32_t index,
                            Render::Models::CrtRandom& random)
{
    const std::int32_t map = world.map;
    if (map != 9 && (map != 34 || world.selector != 1))
        return false;
    leaf.type = RainTexture;
    PlaceExtendedRain(leaf, world, index, random);
    FinishExtendedRain(leaf, world.weather, 0, random);
    return true;
}

bool CreateEventRainLeaf(Leaf& leaf, const RainLeafWorld& world, std::int32_t index, Render::Models::CrtRandom& random)
{
    const std::int32_t map = world.map;
    if (map < 18 || map > 23)
        return false;
    leaf.type = RainTexture;
    leaf.spin.x = 1;
    leaf.spin.y = static_cast<float>(Around(random, 10, 30));
    PlaceExtendedRain(leaf, world, index, random);
    FinishExtendedRain(leaf, world.weather, 20, random);
    return true;
}

bool CreateIcarusRainLeaf(Leaf& leaf, const RainLeafWorld& world, std::int32_t index, Render::Models::CrtRandom& random)
{
    if (world.map != 10)
        return false;
    const std::uint32_t product = static_cast<std::uint32_t>(world.weather.current) * DensityMultiplier;
    const std::int32_t threshold = std::bit_cast<std::int32_t>(product) / DensityDivisor;
    if (index >= threshold)
        return true;
    leaf.type = RainTexture;
    leaf.position.x = HeroOffset(random, 1600, -800, world.hero.x);
    leaf.position.y = HeroOffset(random, 1400, -500, world.hero.y);
    const float z = HeroOffset(random, 200, 200, world.hero.z);
    leaf.angles = {-30, 0, 0};
    leaf.position.z = z;
    const int speed = Around(random, 24, 20);
    RotateRainVelocity(leaf, F32(-static_cast<double>(speed)));
    return true;
}

void StepDriftingLeaf(Leaf& leaf, Render::Models::CrtRandom& random)
{
    const double rise = JitterLeafVelocity(leaf, random);
    AdvanceLeafPosition(leaf, rise, static_cast<double>(leaf.position.z));
    JitterValue(leaf.spin.x, random, 8, -4, SpinStep);
    JitterValue(leaf.spin.y, random, 16, -8, SpinStep);
    const double turn = JitterValue(leaf.spin.z, random, 8, -4, SpinStep);
    leaf.angles.x = F32(static_cast<double>(leaf.spin.x) + static_cast<double>(leaf.angles.x));
    leaf.angles.y = F32(static_cast<double>(leaf.spin.y) + static_cast<double>(leaf.angles.y));
    leaf.angles.z = F32(turn + static_cast<double>(leaf.angles.z));
    const double dx = static_cast<double>(leaf.start.x) - static_cast<double>(leaf.position.x);
    const double dy = static_cast<double>(leaf.start.y) - static_cast<double>(leaf.position.y);
    const double dz = static_cast<double>(leaf.start.z) - static_cast<double>(leaf.position.z);
    const double distanceSquared = ((dz * dz) + (dy * dy)) + (dx * dx);
    if (distanceSquared >= DriftingDistanceSquared)
        leaf.alive = false;
}

void StepFallingLeaf(Leaf& leaf, const LeafTerrainWorld& world, Render::Models::CrtRandom& random)
{
    const double ground = LeafHeightAt(world, leaf.position.x, leaf.position.y);
    FallAtHeight(leaf, ground, random);
}

Core::Expected<bool, std::error_code> StepExtendedRainLeaf(Leaf& leaf, const RainLeafMotionWorld& world,
                                                           Render::Models::CrtRandom& random, const LeafSplash& splash)
{
    const std::int32_t map = world.map;
    if (map != 9 && (map != 34 || world.selector != 1))
        return false;
    const glm::vec2 position = IntegrateRainLeaf(leaf);
    const double height = LeafHeightAt(world.terrain, position.x, position.y);
    if (!(height > static_cast<double>(leaf.position.z)))
        return true;
    return EmitRainImpact(leaf, height, random, splash);
}

Core::Expected<bool, std::error_code> StepEventRainLeaf(Leaf& leaf, const RainLeafMotionWorld& world,
                                                        Render::Models::CrtRandom& random,
                                                        const LeafImpactAttributes& impactAttributes,
                                                        const LeafSplash& splash)
{
    const std::int32_t map = world.map;
    if (map < 18 || map > 23)
        return false;
    const glm::vec2 position = IntegrateRainLeaf(leaf);
    // U2 derramaH antes de comparar y antes de la consultaT, a diferencia delH retenido deU1.
    const float height = F32(LeafHeightAt(world.terrain, position.x, position.y));
    const float z = leaf.position.z;
    if (!(z < height || std::isnan(z) || std::isnan(height)))
        return true;
    if (!impactAttributes)
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    const float queryY = leaf.position.y;
    const float queryX = leaf.position.x;
    const auto attributes = impactAttributes(queryX, queryY);
    if (!attributes.HasValue())
        return Core::Unexpected(attributes.Error());
    if ((attributes.Value() & 8u) != 0)
        return true;
    return EmitRainImpact(leaf, static_cast<double>(height), random, splash);
}

Core::Expected<bool, std::error_code> StepGameplayLeaf(Leaf& leaf, const std::int32_t& map,
                                                       const LeafTerrainWorld& world, Render::Models::CrtRandom& random,
                                                       const LeafSplash& splash)
{
    const std::int32_t initialMap = map;
    if (initialMap != 0 && initialMap != 10)
        return false;
    if (leaf.type != RainTexture)
    {
        StepDriftingLeaf(leaf, random);
        return true;
    }
    leaf.position.x = F32(static_cast<double>(leaf.velocity.x) + static_cast<double>(leaf.position.x));
    const float x = leaf.position.x;
    leaf.position.y = F32(static_cast<double>(leaf.velocity.y) + static_cast<double>(leaf.position.y));
    const float y = leaf.position.y;
    leaf.position.z = F32(static_cast<double>(leaf.velocity.z) + static_cast<double>(leaf.position.z));
    const double ground = LeafHeightAt(world, x, y);
    if (!(ground > static_cast<double>(leaf.position.z)))
        return true;
    leaf.alive = 0;
    leaf.position.z = F32(ground + RainImpactHeight);
    if (map == 10)
        return true;
    if (!splash)
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    const std::error_code error = splash(LeafSplashRequest{leaf.position, leaf.angles, leaf.light});
    if (error)
        return Core::Unexpected(error);
    return true;
}

LeafBlend LeafBlendOf(std::int32_t initialMap)
{
    if (initialMap == 2 || initialMap == 7 || initialMap == 10 || initialMap == 30 || initialMap == 55)
        return LeafBlend::Additive;
    return LeafBlend::Alpha;
}

LeafGeometry LeafGeometryOf(const Leaf& leaf, std::int32_t map, std::int32_t selector)
{
    if (map >= 18 && map <= 23)
        return {LeafGeometryKind::Quad, {leaf.spin.x, leaf.spin.y}};
    if (leaf.type == RainTexture)
    {
        if (map == 34 && selector != 1)
            return {LeafGeometryKind::Suppressed, {0, 0}};
        return {LeafGeometryKind::Quad, {1, 20}};
    }
    if (leaf.type == EmberTexture)
    {
        const float b = F32(static_cast<double>(leaf.scale) * 4.0);
        const float a = F32(static_cast<double>(leaf.scale) + static_cast<double>(leaf.scale));
        return {LeafGeometryKind::Quad, {a, b}};
    }
    return {LeafGeometryKind::Quad, {3, 3}};
}

LeafQuad LeafQuadOf(const glm::vec3& translation, const Render::Models::OriginalMatrix& matrix, glm::vec2 dimensions)
{
    const float a = dimensions.x;
    const float b = dimensions.y;
    const std::array<glm::vec3, 4> local{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    LeafQuad quad;
    quad.translation = translation;
    for (std::size_t k = 0; k < local.size(); ++k)
        quad.corners[k] = Render::Models::OriginalVectorTransform(local[k], matrix);
    quad.uv = {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}};
    return quad;
}

LeafQuad LeafQuadOf(const Leaf& leaf)
{
    // La geometría del login conserva sus A/B históricos, ahora sobre el helper general621320.
    const float a = F32(static_cast<double>(leaf.scale) + static_cast<double>(leaf.scale));
    const float b = F32(static_cast<double>(leaf.scale) * 4.0);
    const Render::Models::OriginalMatrix turn = Render::Models::OriginalAngleMatrix(leaf.angles);
    return LeafQuadOf(leaf.position, turn, {a, b});
}
} // namespace Client::World
