#include "World/Birds.h"
#include "World/IBirdStepServices.h"

#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
using namespace Client::World;
// Sólo observación de fronteras; estos registros no implementan ningún cuerpo opaco.
enum class Boundary
{
    Actor,
    Convert,
    TileIndex,
    TileAttributes,
    PrefixEffect,
    PrefixLight,
    Audio,
    PrepareSpecial,
    Handler,
    BorrowModel,
    Pose,
    Rotation,
    Transform,
    Height,
    Sine,
    Wave,
    Distance,
};
struct Observation
{
    Boundary boundary;
    BirdHandler handler{};
    std::int32_t argument = -1;
    const void* identity = nullptr;
    std::byte* record = nullptr;
    const std::uint8_t* actionAddress = nullptr;
    std::uint8_t actionValue = 0;
    glm::vec3 vector{};
};
struct TestModel
{
    std::uint32_t identityTag = 0;
    std::uint8_t action = 0;
};

class Services final : public IBirdStepServices
{
public:
    std::int32_t map = 24;
    std::int32_t mode = 0;
    int draws = 0;
    int randomValue = -1;
    std::vector<int> scripted;
    std::vector<double> coordinates;
    int tileReads = 0;
    double heightValue = 20;
    double waveValue = 0;
    float suppliedSine = 0;
    float observedPhase = 0;
    float observedSine = 0;
    bool switchActors = false;
    std::array<glm::vec3, 2> actors{{{10, 20, 30}, {100, 200, 300}}};
    std::size_t selectedActor = 0;
    glm::vec3 observedLight{};
    std::vector<std::byte*> physicalRecords;
    std::vector<std::int32_t> physicalIndices;
    bool clearOnCommon = false;
    bool near = false;
    bool mutatePose = false;
    std::int16_t modelType = 0;
    float poseFactor = 0;
    std::vector<std::int32_t> audios;
    std::vector<std::byte*> audioObjects;
    std::uint16_t tile = 0;
    glm::vec3 actor{};
    std::array<TestModel, 2> models{};
    std::vector<Observation> trace;
    std::vector<Boundary> Boundaries() const
    {
        std::vector<Boundary> result;
        for (const auto& observation : trace)
            result.push_back(observation.boundary);
        return result;
    }
    std::string DescribeTrace() const
    {
        std::ostringstream stream;
        for (const auto& observation : trace)
        {
            stream << "boundary=" << static_cast<int>(observation.boundary)
                   << " handler=" << static_cast<int>(observation.handler) << " arg=" << observation.argument
                   << " identity=" << observation.identity << " slot=" << static_cast<const void*>(observation.record)
                   << " actionAddress=" << static_cast<const void*>(observation.actionAddress)
                   << " actionValue=" << static_cast<unsigned int>(observation.actionValue)
                   << " xyz=" << observation.vector.x << ',' << observation.vector.y << ',' << observation.vector.z
                   << '\n';
        }
        return stream.str();
    }
    std::optional<Boundary> failAt;
    BirdRecord* preparedRecord = nullptr;
    bool writeThenFailPose = false;
    bool writeActionOnSelect = false;
    bool changeModelBaseOnSelect = false;
    bool changedModelBase = false;
    std::vector<BirdHandler> calls;
    std::int32_t raw = 1;
    bool fail = false;
    bool useSpawnResults = false;
    std::array<std::int32_t, 3> spawnResults{};
    bool changeMap = false;
    std::int32_t Map() override
    {
        return map;
    }
    std::int32_t Mode() override
    {
        return mode;
    }
    const glm::vec3& ActorPosition() override
    {
        const auto& result = switchActors ? actors[selectedActor] : actor;
        Observation observation{Boundary::Actor};
        observation.identity = &result;
        trace.push_back(observation);
        return result;
    }
    std::int32_t Random() override
    {
        ++draws;
        if (switchActors && draws == 1)
            selectedActor = 1;
        return static_cast<std::size_t>(draws) <= scripted.size() ? scripted[static_cast<std::size_t>(draws - 1)]
                                                                  : randomValue;
    }
    std::int32_t ConvertTileCoordinate(double value) override
    {
        trace.push_back({Boundary::Convert});
        coordinates.push_back(value);
        return 0;
    }
    std::int32_t TileIndex(std::int32_t x, std::int32_t y) override
    {
        Observation observation{Boundary::TileIndex};
        observation.vector = {static_cast<float>(x), static_cast<float>(y), 0};
        trace.push_back(observation);
        return 0;
    }
    const std::uint16_t& TileAttributes(std::int32_t) override
    {
        trace.push_back({Boundary::TileAttributes});
        ++tileReads;
        return tile;
    }
    std::error_code PrefixEffect(const glm::vec3& position) override
    {
        Observation observation{Boundary::PrefixEffect};
        observation.vector = position;
        trace.push_back(observation);
        if (switchActors)
            actors[0] = {11, 22, 33};
        return Failure(Boundary::PrefixEffect);
    }
    std::error_code PrefixLight(float x, float y) override
    {
        observedLight = {x, y, 0};
        Observation observation{Boundary::PrefixLight};
        observation.vector = observedLight;
        trace.push_back(observation);
        return Failure(Boundary::PrefixLight);
    }
    std::error_code Audio(std::int32_t id, std::byte* object) override
    {
        audios.push_back(id);
        audioObjects.push_back(object);
        Observation observation{Boundary::Audio};
        observation.argument = id;
        observation.record = object;
        trace.push_back(observation);
        return Failure(Boundary::Audio);
    }
    std::error_code PrepareSpecial() override
    {
        Observation observation{Boundary::PrepareSpecial};
        if (preparedRecord)
        {
            observation.record = preparedRecord->data();
            observation.actionValue = std::to_integer<std::uint8_t>((*preparedRecord)[0]);
            std::int16_t type;
            std::memcpy(&type, preparedRecord->data() + 2, sizeof type);
            observation.argument = type;
        }
        trace.push_back(observation);
        return Failure(Boundary::PrepareSpecial);
    }
    BirdCallResult Call(BirdHandler handler, BirdRecord bird, std::int32_t index) override
    {
        calls.push_back(handler);
        Observation observation{Boundary::Handler};
        observation.handler = handler;
        observation.record = bird.data();
        observation.argument = index;
        trace.push_back(observation);
        if (handler == BirdHandler::SelectAction)
        {
            if (writeActionOnSelect)
                bird[0x111] = std::byte{0x6b};
            if (changeModelBaseOnSelect)
                changedModelBase = true;
        }
        if (handler == BirdHandler::CommonMotion)
        {
            physicalRecords.push_back(bird.data());
            physicalIndices.push_back(index);
        }
        if (clearOnCommon && handler == BirdHandler::CommonMotion)
            bird[0] = std::byte{0};
        if (changeMap && handler == BirdHandler::Finalize)
            map = 10;
        std::int32_t value = raw;
        if (useSpawnResults)
        {
            switch (handler)
            {
            case BirdHandler::SpawnIcarus:
                value = spawnResults[0];
                break;
            case BirdHandler::SpawnAtlans:
                value = spawnResults[1];
                break;
            case BirdHandler::SpawnKalima:
                value = spawnResults[2];
                break;
            default:
                break;
            }
        }
        return {value, fail ? std::make_error_code(std::errc::io_error) : std::error_code{}};
    }
    BirdModelBorrow BorrowModel(std::int16_t type) override
    {
        modelType = type;
        const std::size_t selected = (type == 0x171 || type == 0x173) && !changedModelBase ? 0u : 1u;
        auto& model = models[selected];
        Observation observation{Boundary::BorrowModel};
        observation.argument = type;
        observation.identity = &model;
        observation.actionAddress = &model.action;
        observation.actionValue = model.action;
        trace.push_back(observation);
        return {&model, &model.action};
    }
    std::error_code Pose(BirdModelBorrow model, BirdRecord bird, float factor) override
    {
        poseFactor = factor;
        Observation observation{Boundary::Pose};
        observation.identity = model.identity;
        observation.record = bird.data();
        observation.actionAddress = model.action;
        observation.actionValue = *model.action;
        trace.push_back(observation);
        if (writeThenFailPose)
            bird[0x1d2] = std::byte{0xa7};
        if (mutatePose)
        {
            const std::int16_t type = 0xaf;
            std::memcpy(bird.data() + 2, &type, sizeof type);
        }
        return Failure(Boundary::Pose);
    }
    std::error_code Rotation(BirdRecord bird) override
    {
        Observation observation{Boundary::Rotation};
        observation.record = bird.data();
        trace.push_back(observation);
        return Failure(Boundary::Rotation);
    }
    glm::vec3 Transform(BirdRecord bird, const glm::vec3& vector) override
    {
        Observation observation{Boundary::Transform};
        observation.record = bird.data();
        observation.vector = vector;
        trace.push_back(observation);
        return {};
    }
    double Height(float x, float y) override
    {
        Observation observation{Boundary::Height};
        observation.vector = {x, y, 0};
        trace.push_back(observation);
        return heightValue;
    }
    float SinSpill(float phase) override
    {
        trace.push_back({Boundary::Sine});
        observedPhase = phase;
        return suppliedSine;
    }
    double Wave(float sine) override
    {
        trace.push_back({Boundary::Wave});
        observedSine = sine;
        return waveValue;
    }
    bool AudioDistance(const glm::vec3&, const glm::vec3& actorPosition) override
    {
        Observation observation{Boundary::Distance};
        observation.identity = &actorPosition;
        trace.push_back(observation);
        return near;
    }

private:
    std::error_code Failure(Boundary boundary) const
    {
        return failAt == boundary ? std::make_error_code(std::errc::io_error) : std::error_code{};
    }
};
struct Pool
{
    std::array<std::array<std::byte, BirdRecordSize>, 40> data{};
    std::vector<BirdRecord> views;
    Pool()
    {
        for (auto& record : data)
            views.emplace_back(record);
    }
    template <typename T> void Set(std::size_t i, std::size_t offset, const T& value)
    {
        std::memcpy(data[i].data() + offset, &value, sizeof value);
    }
    template <typename T> T Read(std::size_t i, std::size_t offset)
    {
        T value;
        std::memcpy(&value, data[i].data() + offset, sizeof value);
        return value;
    }
};
TEST(BirdLifecycleTest, KalimaConsumesTerminalGateAndPreservesUnusedSlots)
{
    Pool pool;
    pool.data[2][0] = std::byte{0x71};
    Services services;
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(result.visited, 2u);
    EXPECT_EQ(services.draws, 3);
    EXPECT_EQ(pool.data[2][0], std::byte{0x71});
}
TEST(BirdLifecycleTest, ActiveKalimaIgnoresFailedGateAndFinalizesAfterTail)
{
    Pool pool;
    pool.data[0][0] = std::byte{2};
    Services services;
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(result.finalized, 1u);
    EXPECT_EQ(services.calls, (std::vector<BirdHandler>{BirdHandler::CommonMotion, BirdHandler::Finalize}));
    EXPECT_EQ(pool.Read<std::int32_t>(0, 0x60), -1);
}
TEST(BirdLifecycleTest, FinalizerMapMutationChangesNextLimit)
{
    Pool pool;
    // Cinco activos impiden que una relectura de nacimiento oculte la omisión tras finalizar.
    for (std::size_t i = 0; i < 5; ++i)
        pool.data[i][0] = std::byte{1};
    Services services;
    services.map = -1;
    services.changeMap = true;
    auto result = StepBirds(pool.views, services);
    EXPECT_EQ(result.visited, 13u);
}
TEST(BirdLifecycleTest, ErrorKeepsPrefixAndSkipsTailAndFinalize)
{
    Pool pool;
    pool.data[0][0] = std::byte{1};
    Services services;
    services.map = -1;
    services.fail = true;
    auto result = StepBirds(pool.views, services);
    EXPECT_EQ(result.error, std::make_error_code(std::errc::io_error));
    EXPECT_EQ(result.finalized, 0u);
    EXPECT_EQ(pool.Read<std::int32_t>(0, 0x60), 0);
    EXPECT_EQ(services.calls, (std::vector<BirdHandler>{BirdHandler::CommonMotion}));
}
TEST(BirdLifecycleTest, SpawnRawTwoRunsGenericTailWhileOneSkipsIt)
{
    for (std::int32_t raw : {1, 2, -1, 0})
    {
        Pool pool;
        Services services;
        services.map = 10;
        services.raw = raw;
        auto result = StepBirds(pool.views, services);
        EXPECT_FALSE(result.error);
        EXPECT_EQ(result.finalized, 13u);
        EXPECT_EQ(pool.Read<std::uint8_t>(0, 0x16d), raw == 1 ? 0 : 1);
        EXPECT_EQ(services.draws, raw == 1 ? 0 : 52);
    }
}

TEST(BirdLifecycleTest, EveryStableMapUsesIndependentExpectedCount)
{
    const std::array<std::int32_t, 18> maps{-2147483647, -1, 0, 1, 3, 4, 7, 10, 11, 17, 18, 24, 25, 26, 27, 28, 29, 36};
    const std::array<std::size_t, 18> counts{5, 5, 5, 5, 5, 5, 40, 13, 5, 5, 5, 2, 2, 2, 2, 2, 2, 2};
    for (std::size_t i = 0; i < maps.size(); ++i)
    {
        Pool pool;
        Services services;
        services.map = maps[i];
        // Mantiene todos activos con tipo default para observar sólo el límite.
        for (auto& bird : pool.data)
            bird[0] = std::byte{0x80};
        auto result = StepBirds(pool.views, services);
        EXPECT_FALSE(result.error);
        EXPECT_EQ(result.visited, counts[i]) << maps[i];
        EXPECT_EQ(result.finalized, counts[i]) << maps[i];
    }
}
TEST(BirdLifecycleTest, OrdinaryBirthClearsWholeRecordAndUsesSignedRandom)
{
    Pool pool;
    Services services;
    services.map = 0;
    pool.data[0].fill(std::byte{0x7f});
    pool.data[0][0] = std::byte{0};
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(pool.Read<std::int16_t>(0, 2), 0xad);
    EXPECT_EQ(pool.data[0].back(), std::byte{0});
    EXPECT_FLOAT_EQ(pool.Read<float>(0, 0x10), -513);
    EXPECT_FLOAT_EQ(pool.Read<float>(0, 0x14), -513);
    EXPECT_FLOAT_EQ(pool.Read<float>(0, 0x18), 169);
    EXPECT_EQ(services.draws, 20);
}
TEST(BirdLifecycleTest, PoseKeepsInitialModelAndFactorButDispatchRereadsType)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mutatePose = true;
    pool.data[0][0] = std::byte{1};
    const std::int16_t type = 0x173;
    std::memcpy(pool.data[0].data() + 2, &type, sizeof type);
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(services.modelType, 0x173);
    EXPECT_FLOAT_EQ(services.poseFactor, 0.5f);
    EXPECT_EQ(services.calls,
              (std::vector<BirdHandler>{BirdHandler::MoveAf, BirdHandler::CommonMotion, BirdHandler::Finalize}));
}
TEST(BirdLifecycleTest, SpecialBirthRetainsUnwrittenBytesAndUpdatesInSameStep)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 3;
    services.randomValue = 0;
    pool.data[0][0x1d3] = std::byte{0x55};
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(result.finalized, 5u);
    EXPECT_EQ(pool.data[0][0x1d3], std::byte{0x55});
    EXPECT_EQ(pool.Read<std::int16_t>(0, 2), 0x171);
    EXPECT_EQ(pool.Read<std::int32_t>(0, 4), 1);
    EXPECT_EQ(pool.Read<std::int32_t>(0, 0x60), 127);
    EXPECT_EQ(services.draws, 38); // contrato: prefijo3 + cinco (gate300,speed,timer,phase,x,y,update)
    EXPECT_EQ(services.audios.front(), 0x42);
}
TEST(BirdLifecycleTest, OrdinaryAudioRunsAfterDeathAndUsesObjectPointer)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.near = true;
    services.randomValue = 0;
    pool.data[0][0] = std::byte{1};
    const std::int16_t type = 0xad;
    std::memcpy(pool.data[0].data() + 2, &type, sizeof type);
    const std::int32_t state = 2;
    std::memcpy(pool.data[0].data() + 4, &state, sizeof state);
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(pool.data[0][0], std::byte{0});
    EXPECT_EQ(services.audios, (std::vector<std::int32_t>{0xc, 0xd}));
    ASSERT_EQ(services.audioObjects.size(), 2u);
    EXPECT_EQ(services.audioObjects[0], pool.data[0].data());
    EXPECT_EQ(services.draws, 2);
    EXPECT_EQ(result.finalized, 1u);
}
TEST(BirdLifecycleTest, SmallPoolFailsBeforeAnyRngOrMutation)
{
    Pool pool;
    Services services;
    services.mode = 3;
    auto result = StepBirds(std::span<BirdRecord>{pool.views}.first(39), services);
    EXPECT_EQ(result.error, std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(services.draws, 0);
}

TEST(BirdLifecycleTest, MapSevenBirthRejectsOtherWordsWithoutClearingSlot)
{
    for (std::uint16_t tile : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{3}})
    {
        Pool pool;
        Services services;
        services.map = 7;
        services.tile = tile;
        pool.data[0].back() = std::byte{0x66};
        auto result = StepBirds(pool.views, services);
        EXPECT_FALSE(result.error);
        EXPECT_EQ(result.finalized, tile == 0 || tile == 2 ? 40u : 0u);
        EXPECT_EQ(pool.data[0].back(), tile == 0 || tile == 2 ? std::byte{0} : std::byte{0x66});
    }
}
TEST(BirdLifecycleTest, SpecialGateFailurePreservesInactiveRecord)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    pool.data[0].back() = std::byte{0x66};
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(result.finalized, 0u);
    EXPECT_EQ(services.draws, 6);
    EXPECT_EQ(pool.data[0].back(), std::byte{0x66});
    EXPECT_TRUE(services.calls.empty());
}
TEST(BirdLifecycleTest, SpecialExpiryStillRequestsNullAudioThenFinalizes)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    // Prefijo falla, update audio acierta; los cuatro inactivos fallan gate300.
    services.scripted = {-1, 0, -1, -1, -1, -1};
    pool.data[0][0] = std::byte{1};
    const std::int16_t type = 0x171;
    std::memcpy(pool.data[0].data() + 2, &type, sizeof type);
    const std::int32_t timer = 1;
    std::memcpy(pool.data[0].data() + 0x60, &timer, sizeof timer);
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(pool.data[0][0], std::byte{0});
    EXPECT_EQ(pool.Read<std::int32_t>(0, 0x60), 0);
    EXPECT_EQ(services.audios, (std::vector<std::int32_t>{0x14e}));
    EXPECT_EQ(services.audioObjects, (std::vector<std::byte*>{nullptr}));
    EXPECT_EQ(services.calls, (std::vector<BirdHandler>{BirdHandler::SelectAction, BirdHandler::Finalize}));
    EXPECT_EQ(result.finalized, 1u);
}
TEST(BirdLifecycleTest, CommonMotionDeactivationStillReachesTailAndFinalize)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.clearOnCommon = true;
    pool.data[0][0] = std::byte{1};
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(result.finalized, 1u);
    EXPECT_EQ(pool.Read<std::int32_t>(0, 0x60), -1);
    EXPECT_EQ(pool.data[0][0], std::byte{0});
}
TEST(BirdLifecycleTest, TileCoordinatesConvertYBeforeX)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.actor = {100, 200, 300};
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    ASSERT_EQ(services.coordinates.size(), 2u);
    EXPECT_EQ(services.coordinates[0], 200.0 * static_cast<double>(0.01f));
    EXPECT_EQ(services.coordinates[1], 100.0 * static_cast<double>(0.01f));
}
TEST(BirdLifecycleTest, BcAudioUsesTileWithoutMapSevenGate)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.tile = 1;
    services.near = true;
    services.randomValue = 0;
    pool.data[0][0] = std::byte{1};
    const std::int16_t type = 0xbc;
    std::memcpy(pool.data[0].data() + 2, &type, sizeof type);
    auto result = StepBirds(pool.views, services);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(services.audios, (std::vector<std::int32_t>{0x8a}));
    EXPECT_EQ(services.tileReads, 1);
    EXPECT_EQ(services.draws, 1);
}

TEST(BirdLifecycleTest, Pc53RoundsWaveProductBeforeCancellation)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.heightValue = 0x1p60;
    services.waveValue = 0x1.47ae147ae147bp53;
    pool.data[0][0] = std::byte{1};
    const std::int16_t type = 0x171;
    std::memcpy(pool.data[0].data() + 2, &type, sizeof type);
    auto result = StepBirds(pool.views, services);
    ASSERT_FALSE(result.error);
    // R53(Wave*100)=2^60; R53(2^60-2^60)+100=100, F32 bits42c80000.
    // Con mantisa64 retenida el residuo es-24 y se guarda76: no son equivalentes.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(pool.Read<float>(0, 0x18)), 0x42c80000u);
}
TEST(BirdLifecycleTest, CachedActorsSurviveCrossActorLookupsAndRemainLive)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.switchActors = true;
    services.scripted = {0, 1, 2};
    auto result = StepBirds(pool.views, services);
    ASSERT_FALSE(result.error);
    // Luz conserva identidadA0 cacheada, pero observa su mutación posterior al efecto.
    EXPECT_EQ(services.observedLight, (glm::vec3{11, 22, 0}));
    ASSERT_EQ(services.coordinates.size(), 2u);
    // Conversión pura: el actorA1 sigue canónico; no mutar globals desde servicios matemáticos.
    EXPECT_EQ(services.coordinates[0], 200.0 * static_cast<double>(0.01f));
    EXPECT_EQ(services.coordinates[1], 100.0 * static_cast<double>(0.01f));
}
TEST(BirdLifecycleTest, PhysicalSlotAndExcludedIndexStayPaired)
{
    Pool pool;
    Services services;
    services.map = 7;
    for (auto& bird : pool.data)
        bird[0] = std::byte{1};
    auto result = StepBirds(pool.views, services);
    ASSERT_FALSE(result.error);
    ASSERT_EQ(result.finalized, 40u);
    ASSERT_EQ(services.physicalRecords.size(), 40u);
    for (std::size_t i = 0; i < 40; ++i)
    {
        EXPECT_EQ(services.physicalRecords[i], pool.data[i].data());
        EXPECT_EQ(services.physicalRecords[i],
                  std::as_writable_bytes(std::span{pool.data}).data() + i * BirdRecordSize);
        EXPECT_EQ(services.physicalIndices[i], static_cast<std::int32_t>(i));
    }
}
TEST(BirdLifecycleTest, RequiredSineServiceResultIsForwardedWithoutReevaluation)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.suppliedSine = 0.25f;
    pool.data[0][0] = std::byte{1};
    const std::int16_t type = 0x171;
    std::memcpy(pool.data[0].data() + 2, &type, sizeof type);
    auto result = StepBirds(pool.views, services);
    ASSERT_FALSE(result.error);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(services.observedPhase), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(services.observedSine), 0x3e800000u);
    // Sentinel de servicio, distinto de sin(0); prueba forwarding, no payload FSIN numérico.
}

TEST(BirdLifecycleTest, SpawnChainUsesIcarusThenAtlansThenKalimaWithFullEax)
{
    for (const std::int32_t atlansResult : {0, 1, 2, 256, 257, -1})
    {
        Pool pool;
        Services services;
        services.map = 10;
        services.useSpawnResults = true;
        services.spawnResults = {0, atlansResult, 1};
        // Sólo slot0 nace: el resto activo con tipo default no llama a spawn.
        for (std::size_t i = 1; i < 40; ++i)
            pool.data[i][0] = std::byte{1};
        auto result = StepBirds(pool.views, services);
        ASSERT_FALSE(result.error);
        ASSERT_GE(services.calls.size(), 3u);
        EXPECT_EQ(services.calls[0], BirdHandler::SpawnIcarus); // 4455e0
        EXPECT_EQ(services.calls[1], BirdHandler::SpawnAtlans); // 445890: mapa7
        EXPECT_EQ(services.calls[2],
                  atlansResult == 0 ? BirdHandler::SpawnKalima : BirdHandler::CommonMotion); // 439680
        EXPECT_EQ(pool.Read<std::uint8_t>(0, 0x16d), atlansResult == 0 || atlansResult == 1 ? 0 : 1);
    }
}

TEST(BirdLifecycleObservabilityTest, ZeroFallbackIsObservedBeforeGenericTail)
{
    Pool pool;
    Services services;
    services.map = 10;
    services.useSpawnResults = true;
    services.spawnResults = {0, 0, 0};
    for (auto& bird : pool.data)
        bird[0] = std::byte{1};
    constexpr std::size_t SpawnIndex = 3;
    pool.data[SpawnIndex][0] = std::byte{0};
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    ASSERT_FALSE(result.error);
    std::vector<BirdHandler> handlers;
    for (const auto& entry : services.trace)
    {
        if (entry.boundary != Boundary::Handler || entry.record != pool.data[SpawnIndex].data())
            continue;
        handlers.push_back(entry.handler);
        EXPECT_EQ(entry.argument, 3);
    }
    EXPECT_EQ(handlers,
              (std::vector<BirdHandler>{BirdHandler::SpawnIcarus, BirdHandler::SpawnAtlans, BirdHandler::SpawnKalima,
                                        BirdHandler::CommonMotion, BirdHandler::Finalize}));
    EXPECT_EQ(pool.Read<std::uint8_t>(SpawnIndex, 0x16d), 1);
    EXPECT_EQ(services.draws, 4);
}
TEST(BirdLifecycleObservabilityTest, IndexedMovementPreservesEveryPhysicalSlotArgument)
{
    Pool pool;
    Services services;
    services.map = 10;
    for (std::size_t i = 0; i < 13; ++i)
    {
        pool.data[i][0] = std::byte{1};
        pool.Set(i, 2, std::int16_t{0x10f});
    }
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    ASSERT_FALSE(result.error);
    std::vector<Observation> handlers;
    for (const auto& entry : services.trace)
        if (entry.boundary == Boundary::Handler)
            handlers.push_back(entry);
    ASSERT_EQ(handlers.size(), 39u);
    for (std::size_t i = 0; i < 13; ++i)
    {
        EXPECT_EQ(handlers[i * 3].handler, BirdHandler::Move10f);
        EXPECT_EQ(handlers[i * 3 + 1].handler, BirdHandler::CommonMotion);
        EXPECT_EQ(handlers[i * 3 + 2].handler, BirdHandler::Finalize);
        for (std::size_t offset = 0; offset < 3; ++offset)
        {
            EXPECT_EQ(handlers[i * 3 + offset].argument, static_cast<std::int32_t>(i));
            EXPECT_EQ(handlers[i * 3 + offset].record, pool.data[i].data());
        }
    }
}
TEST(BirdLifecycleObservabilityTest, ModelActionAndGlobalOrderSurviveBaseChangeDuringSelect)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.scripted = {-1, 0};
    services.writeActionOnSelect = true;
    services.changeModelBaseOnSelect = true;
    services.models[0].action = 0xc1;
    services.models[1].action = 0xd2;
    pool.data[0][0] = std::byte{1};
    pool.Set(0, 2, std::int16_t{0x171});
    pool.Set(0, 0xc, 2.0f);
    pool.Set(0, 0x10, glm::vec3{3, 4, 5});
    pool.data[0][0x111] = std::byte{0x21};
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    ASSERT_FALSE(result.error);
    EXPECT_EQ(result.finalized, 1u);
    EXPECT_EQ(services.Boundaries(),
              (std::vector<Boundary>{Boundary::Actor, Boundary::PrefixLight, Boundary::Actor, Boundary::Convert,
                                     Boundary::Convert, Boundary::TileIndex, Boundary::BorrowModel, Boundary::Handler,
                                     Boundary::Pose, Boundary::Rotation, Boundary::Transform, Boundary::Height,
                                     Boundary::Sine, Boundary::Wave, Boundary::Audio, Boundary::Handler}));
    ASSERT_EQ(services.trace.size(), 16u);
    EXPECT_EQ(services.trace[6].identity, &services.models[0]);
    EXPECT_EQ(services.trace[7].handler, BirdHandler::SelectAction);
    EXPECT_EQ(services.trace[7].argument, 7);
    EXPECT_EQ(services.trace[8].identity, &services.models[0]);
    EXPECT_EQ(services.trace[8].actionAddress, &services.models[0].action);
    EXPECT_EQ(services.trace[8].actionValue, 0x6b);
    EXPECT_EQ(services.models[0].action, 0x6b);
    EXPECT_EQ(services.models[1].action, 0xd2);
    EXPECT_EQ(services.trace[8].record, pool.data[0].data());
    EXPECT_EQ(services.trace[10].vector, (glm::vec3{80, 0, 0}));
    EXPECT_EQ(services.trace[11].vector, (glm::vec3{3, 4, 0}));
    EXPECT_EQ(services.trace[14].argument, 0x14e);
    EXPECT_EQ(services.trace[14].record, nullptr);
    EXPECT_EQ(services.trace[15].handler, BirdHandler::Finalize);
    EXPECT_EQ(services.trace[15].record, pool.data[0].data());
}
TEST(BirdLifecycleObservabilityTest, PoseErrorKeepsWrittenPrefixAndStopsEverySuffix)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.failAt = Boundary::Pose;
    services.writeThenFailPose = true;
    services.writeActionOnSelect = true;
    pool.data[0][0] = std::byte{1};
    pool.Set(0, 2, std::int16_t{0x171});
    pool.Set(0, 0x60, std::int32_t{7});
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    EXPECT_EQ(result.error, std::make_error_code(std::errc::io_error));
    EXPECT_EQ(result.visited, 1u);
    EXPECT_EQ(result.finalized, 0u);
    EXPECT_EQ(pool.data[0][0x1d2], std::byte{0xa7});
    EXPECT_EQ(pool.data[0][0], std::byte{1});
    EXPECT_EQ(pool.Read<std::int32_t>(0, 0x60), 7);
    EXPECT_EQ(services.models[0].action, 0x6b);
    EXPECT_EQ(services.Boundaries(), (std::vector<Boundary>{Boundary::Actor, Boundary::PrefixLight, Boundary::Actor,
                                                            Boundary::Convert, Boundary::Convert, Boundary::TileIndex,
                                                            Boundary::BorrowModel, Boundary::Handler, Boundary::Pose}));
    ASSERT_EQ(services.trace.size(), 9u);
    EXPECT_EQ(services.trace.back().record, pool.data[0].data());
    EXPECT_EQ(services.trace.back().identity, &services.models[0]);
    EXPECT_EQ(services.trace.back().actionValue, 0x6b);
}
TEST(BirdLifecycleObservabilityTest, PrefixLightErrorKeepsEffectAudioOrderAndSkipsTile)
{
    Pool pool;
    const auto before = pool.data;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.actor = {10, 20, 30};
    services.scripted = {0, 1, 2};
    services.failAt = Boundary::PrefixLight;
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    EXPECT_EQ(result.error, std::make_error_code(std::errc::io_error));
    EXPECT_EQ(result.visited, 0u);
    EXPECT_EQ(result.finalized, 0u);
    EXPECT_EQ(services.draws, 3);
    EXPECT_EQ(pool.data, before);
    EXPECT_EQ(services.Boundaries(),
              (std::vector<Boundary>{Boundary::Actor, Boundary::Actor, Boundary::Actor, Boundary::PrefixEffect,
                                     Boundary::Audio, Boundary::PrefixLight}));
    ASSERT_EQ(services.trace.size(), 6u);
    EXPECT_EQ(services.trace[0].identity, &services.actor);
    EXPECT_EQ(services.trace[3].vector, (glm::vec3{-189, 222, 330}));
    EXPECT_EQ(services.trace[4].argument, 0x42);
    EXPECT_EQ(services.trace[4].record, nullptr);
    EXPECT_EQ(services.trace[5].vector, (glm::vec3{10, 20, 0}));
}
TEST(BirdLifecycleObservabilityTest, PrepareErrorSeesActiveBeforeTypeAndStopsBirth)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.scripted = {-1, 0};
    services.preparedRecord = &pool.views[0];
    services.failAt = Boundary::PrepareSpecial;
    pool.Set(0, 2, std::int16_t{0x222});
    pool.data[0][0x1d3] = std::byte{0x55};
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    EXPECT_EQ(result.error, std::make_error_code(std::errc::io_error));
    EXPECT_EQ(result.finalized, 0u);
    EXPECT_EQ(services.draws, 2);
    EXPECT_EQ(pool.data[0][0], std::byte{1});
    EXPECT_EQ(pool.Read<std::int16_t>(0, 2), 0x222);
    EXPECT_EQ(pool.data[0][0x1d3], std::byte{0x55});
    EXPECT_EQ(services.Boundaries(),
              (std::vector<Boundary>{Boundary::Actor, Boundary::PrefixLight, Boundary::Actor, Boundary::Convert,
                                     Boundary::Convert, Boundary::TileIndex, Boundary::PrepareSpecial}));
    ASSERT_EQ(services.trace.size(), 7u);
    EXPECT_EQ(services.trace.back().actionValue, 1);
    EXPECT_EQ(services.trace.back().argument, 0x222);
    EXPECT_EQ(services.trace.back().record, pool.data[0].data());
}
TEST(BirdLifecycleObservabilityTest, HeightAdditionUsesQaPc53TieBeforeFloatStore)
{
    Pool pool;
    Services services;
    services.map = -1;
    services.mode = 1;
    services.heightValue = 0x1.a800040000001p7;
    pool.data[0][0] = std::byte{1};
    pool.Set(0, 2, std::int16_t{0x171});
    pool.Set(0, 0x60, std::int32_t{2});
    auto result = StepBirds(pool.views, services);
    SCOPED_TRACE(services.DescribeTrace());
    ASSERT_FALSE(result.error);
    // Oracle QA: R53(H+300) cae en el empateF32 de512; después +100 da612 exacto.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(pool.Read<float>(0, 0x18)), 0x44190000u);
}
} // namespace
