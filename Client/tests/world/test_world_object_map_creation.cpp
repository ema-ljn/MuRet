#include "support/CheckedWorldObjectProjection.h"

#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "Data/WorldFolder.h"
#include "Render/Models/Cloth.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <functional>
#include <vector>
#include <span>
#include <utility>

namespace
{
struct DirectMapRule
{
    int map;
    int type;
    int blend;
    bool registered;
    bool hidden;
    float maxZ;
};
constexpr std::array DirectMapRules{
    DirectMapRule{1, 59, -1, true, false, 80.0f}, DirectMapRule{1, 60, -1, true, true, 160.0f},
    DirectMapRule{2, 19, 0, false, false, 80.0f}, DirectMapRule{2, 92, 0, false, false, 80.0f},
    DirectMapRule{2, 93, 0, false, false, 80.0f}, DirectMapRule{2, 54, 1, false, false, 80.0f},
    DirectMapRule{2, 56, 1, false, false, 80.0f}, DirectMapRule{2, 78, 3, false, false, 80.0f},
    DirectMapRule{2, 22, -1, true, false, 80.0f}, DirectMapRule{2, 25, -1, true, false, 80.0f},
    DirectMapRule{2, 40, -1, true, false, 80.0f}, DirectMapRule{2, 45, -1, true, false, 80.0f},
    DirectMapRule{2, 55, -1, true, false, 80.0f}, DirectMapRule{2, 73, -1, true, false, 80.0f},
    DirectMapRule{2, 91, -1, true, true, 160.0f}, DirectMapRule{2, 100, -1, false, true, 80.0f},
    DirectMapRule{3, 1, 1, false, false, 80.0f},  DirectMapRule{3, 8, -1, true, false, 80.0f},
    DirectMapRule{3, 9, 3, false, false, 80.0f},  DirectMapRule{3, 17, 0, false, false, 80.0f},
    DirectMapRule{3, 19, 0, false, false, 80.0f}, DirectMapRule{3, 37, 0, false, false, 80.0f},
    DirectMapRule{3, 18, 2, false, false, 80.0f}, DirectMapRule{3, 38, -1, true, true, 80.0f},
    DirectMapRule{7, 39, -1, true, true, 80.0f},  DirectMapRule{8, 78, -1, true, false, 80.0f}};

// Tabla literal independiente del switch: detecta handlers omitidos y fallthrough de mapa especial.
TEST(WorldObjectMapCreationTest, EverySpecialMapUsesOnlyItsOwnDirectCases)
{
    for (const std::uint8_t map : std::array<std::uint8_t, 5>{1, 2, 3, 7, 8})
    {
        for (int type = -1; type <= 323; ++type)
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(type);
            DirectMapRule expected{map, type, -1, false, false, 80.0f};
            for (const DirectMapRule& rule : DirectMapRules)
            {
                if (rule.map == map && rule.type == type)
                {
                    expected = rule;
                }
            }
            Client::Data::Terrain::TerrainObject record{.type = static_cast<std::int16_t>(type),
                                                        .position = {1600.0f, 14400.0f, 25.0f},
                                                        .angle = {10.0f, 20.0f, 30.0f},
                                                        .scale = 2.0f};
            Client::World::WorldObjectRegistrations registrations;
            registrations.entries[0] = {.active = 2, .metadata = {1, 2, 3, 4, 5, 6, 7}, .object = 999};
            registrations.nextIdentity = 1000;
            const auto objects = Client::Tests::CheckedWorldObjectProjection(
                std::span(&record, 1), map, Client::World::ObjectScene::Other, &registrations);
            ASSERT_EQ(objects.size(), 1u);
            const auto& object = objects[0];
            EXPECT_EQ(object.state.blendMesh, expected.blend);
            EXPECT_EQ(object.state.hiddenMesh, expected.hidden ? -2 : -1);
            EXPECT_EQ(object.boundsMinimum, glm::vec3(-40.0f, -40.0f, 0.0f));
            EXPECT_EQ(object.boundsMaximum, glm::vec3(40.0f, 40.0f, expected.maxZ));
            EXPECT_EQ(registrations.entries[1].active, expected.registered ? 1 : 0);
            EXPECT_EQ(registrations.entries[1].object, expected.registered ? 1000u : 0u);
            EXPECT_EQ(registrations.entries[0].object, 999u);
            EXPECT_EQ(registrations.entries[0].metadata, (std::array<std::uint8_t, 7>{1, 2, 3, 4, 5, 6, 7}));
            EXPECT_EQ(object.sectorX, 0);
            EXPECT_EQ(object.sectorY, 8);
            EXPECT_EQ(object.basePosition, glm::vec3(1600.0f, 14400.0f, 25.0f));
            EXPECT_EQ(object.transform.position, object.basePosition);
            EXPECT_FALSE(object.seen);
            EXPECT_TRUE(object.alive);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(object.speed), 0x3e23d70au);
            EXPECT_FLOAT_EQ(object.transform.scale, 2);
            EXPECT_FLOAT_EQ(object.cullingMargin, -30);
            EXPECT_EQ(object.light, glm::vec3(0));
            EXPECT_TRUE(object.vertexLight);
            const bool copied = map == 2 && (type == 20 || type == 65 || type == 86 || type == 88);
            EXPECT_EQ(object.copiedPosition, copied ? glm::vec3(1600, 14400, 25) : glm::vec3(0));
            EXPECT_EQ(object.copiedAngles, copied ? glm::vec3(10, 20, 30) : glm::vec3(0));
        }
    }
}
// Detecta resto positivo/fmod, uso de int64%360 y pérdida de las copias originales del mapa2. El NaN pasa
// por el mismo ftol que el sector de Q-C186, en la misma llamada a 0x606660: 0x8000000000000000, resto 0.
TEST(WorldObjectMapCreationTest, DeviasCopiesTheTransformAndUsesTheSignedLowDwordRemainder)
{
    struct Angle
    {
        float input;
        float output;
    };
    constexpr std::array angles{Angle{std::numeric_limits<float>::quiet_NaN(), 0.0f},
                                Angle{721.75f, 1.0f},
                                Angle{-721.75f, -1.0f},
                                Angle{-0.75f, 0.0f},
                                Angle{360.0f, 0.0f},
                                Angle{0x1p32f, 0.0f},
                                Angle{0x1p31f, -128.0f},
                                Angle{-0x1p31f, -128.0f},
                                Angle{-0x1p63f, 0.0f},
                                Angle{0x1p32f + 512.0f, 152.0f},
                                Angle{-0x1p32f - 512.0f, -152.0f}};
    for (const std::int16_t type : std::array<std::int16_t, 4>{20, 65, 86, 88})
    {
        for (const Angle& angle : angles)
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(angle.input);
            Client::Data::Terrain::TerrainObject record{.type = type,
                                                        .position = {123.0f, 456.0f, 789.0f},
                                                        .angle = {10.0f, 20.0f, angle.input},
                                                        .scale = 2.0f};
            const auto objects = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), 2,
                                                                             Client::World::ObjectScene::Other);
            ASSERT_EQ(objects.size(), 1u);
            EXPECT_EQ(objects[0].transform.angles, glm::vec3(10.0f, 20.0f, angle.output));
            EXPECT_EQ(objects[0].copiedAngles, glm::vec3(10.0f, 20.0f, angle.output));
            EXPECT_EQ(objects[0].copiedPosition, glm::vec3(123.0f, 456.0f, 789.0f));
            EXPECT_EQ(objects[0].basePosition, glm::vec3(123.0f, 456.0f, 789.0f));
        }
    }
}

TEST(WorldObjectMapCreationTest, TheLoginRelocationPreservesTheInputBucketAndBaseCopy)
{
    for (const std::int16_t type : std::array<std::int16_t, 2>{86, 90})
    {
        Client::Data::Terrain::TerrainObject record{
            .type = type, .position = {1600.0f, 14400.0f, 25.0f}, .angle = {10.0f, 20.0f, 30.0f}, .scale = 2.0f};
        const auto objects =
            Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), 55, Client::World::ObjectScene::Other);
        ASSERT_EQ(objects.size(), 1u);
        EXPECT_EQ(objects[0].transform.position, glm::vec3(8600.0f, 25000.0f, 5000.0f));
        EXPECT_EQ(objects[0].basePosition, glm::vec3(1600.0f, 14400.0f, 25.0f));
        EXPECT_EQ(objects[0].transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(objects[0].transform.scale, type == 86 ? 60.0f : 110.0f);
        EXPECT_EQ(objects[0].sectorX, 0);
        EXPECT_EQ(objects[0].sectorY, 8);
    }
}
// Detecta que el registro de otro mapa reinicie la tabla, pierda metadata o use un pool de objetos.
TEST(WorldObjectMapCreationTest, EveryMapRegistrationPreservesAFullTableAndStableObjectIdentities)
{
    struct RegisteredType
    {
        std::uint8_t map;
        std::int16_t type;
    };
    constexpr std::array cases{RegisteredType{1, 59}, RegisteredType{1, 60}, RegisteredType{2, 22},
                               RegisteredType{2, 25}, RegisteredType{2, 40}, RegisteredType{2, 45},
                               RegisteredType{2, 55}, RegisteredType{2, 73}, RegisteredType{2, 91},
                               RegisteredType{3, 8},  RegisteredType{3, 38}, RegisteredType{7, 39},
                               RegisteredType{8, 78}};
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.map);
        SCOPED_TRACE(test.type);
        Client::World::WorldObjectRegistrations registrations;
        for (auto& entry : registrations.entries)
        {
            entry = {.active = 255, .metadata = {9, 8, 7, 6, 5, 4, 3}, .object = 400};
        }
        registrations.nextIdentity = 500;
        registrations.entries.back().active = 0;
        const std::array records{
            Client::Data::Terrain::TerrainObject{.type = test.type, .position = {10.0f, 20.0f, 30.0f}},
            Client::Data::Terrain::TerrainObject{.type = test.type, .position = {25700.0f, 20.0f, 30.0f}},
            Client::Data::Terrain::TerrainObject{.type = test.type, .position = {10.0f, 20.0f, 40.0f}}};
        auto objects = Client::Tests::CheckedWorldObjectProjection(records, test.map, Client::World::ObjectScene::Other,
                                                                   &registrations);
        ASSERT_EQ(objects.size(), 2u);
        EXPECT_EQ(registrations.nextIdentity, 502u);
        EXPECT_EQ(registrations.entries.front().active, 255);
        EXPECT_EQ(registrations.entries.front().object, 400u);
        EXPECT_EQ(registrations.entries.back().active, 1);
        EXPECT_EQ(registrations.entries.back().object, 500u);
        EXPECT_EQ(registrations.entries.back().metadata, (std::array<std::uint8_t, 7>{9, 8, 7, 6, 5, 4, 3}));
        auto moved = std::move(objects);
        moved.reserve(1024);
        EXPECT_EQ(moved.front().identity, 500u);
        EXPECT_EQ(moved.back().identity, 501u);
        const auto fullEntries = registrations.entries;
        const auto nextBatch = Client::Tests::CheckedWorldObjectProjection(
            std::span(records).first(1), test.map, Client::World::ObjectScene::Other, &registrations);
        ASSERT_EQ(nextBatch.size(), 1u);
        EXPECT_EQ(nextBatch.front().identity, 502u);
        EXPECT_EQ(registrations.nextIdentity, 503u);
        EXPECT_EQ(registrations.entries, fullEntries);
    }
}

// Detecta que copiar int16 como unsigned convierta tipos negativos en casos de otro mapa.
TEST(WorldObjectMapCreationTest, SignedTypeBoundariesPreserveDefaultsOnEverySpecialMap)
{
    constexpr std::array<std::int16_t, 4> types{std::numeric_limits<std::int16_t>::min(), -515, -1,
                                                std::numeric_limits<std::int16_t>::max()};
    for (const std::uint8_t map : std::array<std::uint8_t, 7>{0, 1, 2, 3, 7, 8, 55})
    {
        for (const std::int16_t type : types)
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(type);
            const Client::Data::Terrain::TerrainObject record{
                .type = type, .position = {10.0f, 20.0f, 30.0f}, .angle = {40.0f, 50.0f, 60.0f}, .scale = 2.0f};
            Client::World::WorldObjectRegistrations registrations;
            const auto objects = Client::Tests::CheckedWorldObjectProjection(
                std::span(&record, 1), map, Client::World::ObjectScene::Login, &registrations);
            ASSERT_EQ(objects.size(), 1u);
            EXPECT_EQ(objects[0].type, type);
            EXPECT_EQ(objects[0].state.blendMesh, -1);
            EXPECT_EQ(objects[0].state.hiddenMesh, -1);
            EXPECT_EQ(objects[0].transform.angles, glm::vec3(40.0f, 50.0f, 60.0f));
            EXPECT_EQ(objects[0].transform.position, glm::vec3(10.0f, 20.0f, 30.0f));
            EXPECT_EQ(objects[0].copiedAngles, glm::vec3(0.0f));
            EXPECT_EQ(objects[0].copiedPosition, glm::vec3(0.0f));
            EXPECT_EQ(registrations.entries[0].active, 0);
        }
    }
}

class GoldenWorldObjectMapCreationTest : public Client::Tests::GoldenDataTest
{
};

// Los expected vienen de la tabla ASM literal, no del constructor ni de archivos generados por él.
TEST_F(GoldenWorldObjectMapCreationTest, RealSpecialMapObjectsReceiveTheDirectRulesAndFillTheSameRegistry)
{
    for (const std::uint8_t map : std::array<std::uint8_t, 5>{1, 2, 3, 7, 8})
    {
        SCOPED_TRACE(map);
        const auto records = Client::World::LoadWorldObjects(m_dataRoot, map);
        ASSERT_TRUE(records.HasValue()) << records.Error();
        ASSERT_FALSE(records.Value().empty());
        Client::World::WorldObjectRegistrations registrations;
        const auto objects = Client::Tests::CheckedWorldObjectProjection(
            records.Value(), map, Client::World::ObjectScene::Other, &registrations);
        ASSERT_FALSE(objects.empty());
        std::size_t registered = 0;
        std::size_t specialCases = 0;
        for (const auto& object : objects)
        {
            SCOPED_TRACE(object.identity);
            SCOPED_TRACE(object.type);
            DirectMapRule expected{map, object.type, -1, false, false, 80.0f};
            for (const DirectMapRule& rule : DirectMapRules)
            {
                if (rule.map == map && rule.type == object.type)
                {
                    expected = rule;
                    ++specialCases;
                }
            }
            EXPECT_EQ(object.state.blendMesh, expected.blend);
            EXPECT_EQ(object.state.hiddenMesh, expected.hidden ? -2 : -1);
            EXPECT_EQ(object.boundsMaximum, glm::vec3(40.0f, 40.0f, expected.maxZ));
            if (expected.registered && registered < registrations.entries.size())
            {
                EXPECT_EQ(registrations.entries[registered].object, object.identity);
                EXPECT_EQ(registrations.entries[registered].active, 1);
                ++registered;
            }
        }
        EXPECT_GT(specialCases, 0u) << "el Data real debe ejercitar alguna regla propia del mapa";
        EXPECT_EQ(registrations.nextIdentity, objects.size() + 1);
        for (std::size_t index = registered; index < registrations.entries.size(); ++index)
        {
            EXPECT_EQ(registrations.entries[index].active, 0);
        }
    }
}
// Este servicio de prueba observa la frontera síncrona y modifica el objeto real prestado.
// No prueba que ModelEffects implemente los efectos 322..324: ese binding se integra por su dueño.
class MutatingCreationServices final : public Client::World::IWorldObjectCreationServices
{
public:
    struct Emission
    {
        std::int16_t type;
        glm::vec3 position;
        glm::vec3 angles;
        glm::vec3 light;
        float scale;
        Client::World::WorldObjectIdentity owner;
    };
    std::vector<Emission> emissions;
    std::size_t failAt = std::numeric_limits<std::size_t>::max();
    Client::World::WorldObjectCreationContext* context = nullptr;
    bool changeScene = false;
    std::function<bool(const Client::World::WorldObjectModelEffectRequest&, Client::World::WorldObject&)> onCreate;

    bool CreateModelEffect(const Client::World::WorldObjectModelEffectRequest& request,
                           Client::World::WorldObject& source) noexcept override
    {
        emissions.push_back(
            {request.type, request.position, source.transform.angles, source.light, request.scale, request.owner});
        const bool sourcePosition = request.type == 180 || request.type == 309;
        if (sourcePosition)
        {
            EXPECT_EQ(&request.position, &source.transform.position);
        }
        if (onCreate)
            return onCreate(request, source);
        source.transform.position += glm::vec3(100.0f);
        if (sourcePosition)
        {
            EXPECT_EQ(request.position, source.transform.position);
        }
        source.transform.angles += glm::vec3(1.0f, 2.0f, 3.0f);
        source.light += glm::vec3(1.0f);
        source.type = 0;
        if (context != nullptr)
        {
            context->map = 0;
            if (changeScene)
            {
                context->scene = Client::World::ObjectScene::Other;
            }
        }
        return emissions.size() != failAt;
    }
};

// Detecta requests diferidos, posiciones cacheadas, tipo/owner/escala equivocados y el offset16 errado.
TEST(WorldObjectMapCreationTest, NoriaEmitsFiveEffectsAndRereadsTheLiveSourceBetweenCalls)
{
    const Client::Data::Terrain::TerrainObject record{
        .type = 322, .position = {10.0f, 20.0f, 30.0f}, .angle = {1.0f, 2.0f, 3.0f}, .scale = 2.0f};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    Client::World::WorldObjectCreationContext context{3, Client::World::ObjectScene::Other, registrations, &services};
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    ASSERT_EQ(services.emissions.size(), 5u);
    constexpr std::array<std::int16_t, 5> types{322, 323, 322, 323, 324};
    const std::array<glm::vec3, 5> positions{glm::vec3(10.0f, 20.0f, 380.0f), glm::vec3(110.0f, 124.0f, 480.0f),
                                             glm::vec3(210.0f, 228.0f, 580.0f), glm::vec3(310.0f, 332.0f, 680.0f),
                                             glm::vec3(410.0f, 440.0f, 780.0f)};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        EXPECT_EQ(services.emissions[index].type, types[index]);
        EXPECT_EQ(services.emissions[index].position, positions[index]);
        EXPECT_EQ(services.emissions[index].owner, 0u);
        EXPECT_EQ(services.emissions[index].scale, 0.0f);
    }
    EXPECT_EQ(services.emissions.front().angles, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(services.emissions.back().angles, glm::vec3(5.0f, 10.0f, 15.0f));
    EXPECT_EQ(services.emissions.back().light, glm::vec3(4.0f));
    EXPECT_EQ(result.objects[0].transform.position, glm::vec3(510.0f, 520.0f, 530.0f));
    EXPECT_EQ(result.objects[0].basePosition, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(result.objects[0].identity, 1u);
    EXPECT_EQ(registrations.nextIdentity, 2u);
}

// Detecta lectura del mapa antes de la emisión180 y dispatch por object.type mutado en vez del parámetro.
TEST(WorldObjectMapCreationTest, SceneEmissionRunsBeforeReadingTheMapAndPreservesTheOriginalDispatchType)
{
    const Client::Data::Terrain::TerrainObject record{
        .type = 180, .position = {10.0f, 20.0f, 30.0f}, .angle = {1.0f, 2.0f, 3.0f}, .scale = 2.0f};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    Client::World::WorldObjectCreationContext context{4, Client::World::ObjectScene::Login, registrations, &services};
    services.context = &context;
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    ASSERT_EQ(services.emissions.size(), 1u);
    EXPECT_EQ(services.emissions[0].type, 180);
    EXPECT_EQ(services.emissions[0].position, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(services.emissions[0].light, glm::vec3(1.0f));
    EXPECT_EQ(services.emissions[0].owner, 1u);
    EXPECT_EQ(services.emissions[0].scale, 1.6f);
    EXPECT_EQ(result.objects[0].type, 0);
    EXPECT_EQ(result.objects[0].boundsMaximum, glm::vec3(40.0f, 40.0f, 80.0f));
    EXPECT_EQ(result.objects[0].state.hiddenMesh, -2);
    EXPECT_EQ(context.map, 0);
}

// Detecta fallback silencioso, rollback de efectos previos y continuar con records posteriores tras un binding ausente.
TEST(WorldObjectMapCreationTest, UnsupportedEffectRetainsTheCreatedObjectsAndStopsAtItsExactRecord)
{
    const std::array records{Client::Data::Terrain::TerrainObject{.type = 8, .position = {10.0f, 20.0f, 30.0f}},
                             Client::Data::Terrain::TerrainObject{.type = 322, .position = {10.0f, 20.0f, 30.0f}},
                             Client::Data::Terrain::TerrainObject{.type = 8, .position = {10.0f, 20.0f, 30.0f}}};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    services.failAt = 3;
    Client::World::WorldObjectCreationContext context{3, Client::World::ObjectScene::Other, registrations, &services};
    const auto result = Client::World::CreateWorldObjects(records, context);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::UnsupportedEffect);
    EXPECT_EQ(result.failure->recordIndex, 1u);
    ASSERT_EQ(result.objects.size(), 2u);
    EXPECT_EQ(registrations.entries[0].object, 1u);
    EXPECT_EQ(registrations.entries[1].active, 0);
    EXPECT_EQ(registrations.nextIdentity, 3u);
    EXPECT_EQ(services.emissions.size(), 3u);
    EXPECT_EQ(result.objects.back().transform.position, glm::vec3(310.0f, 320.0f, 330.0f));
}

TEST(WorldObjectMapCreationTest, MissingEmissionServiceIsAnExplicitIntegrationError)
{
    for (const auto scene : {Client::World::ObjectScene::Login, Client::World::ObjectScene::CharacterSelect})
    {
        Client::Data::Terrain::TerrainObject record{.type = 180};
        Client::World::WorldObjectRegistrations registrations;
        Client::World::WorldObjectCreationContext context{0, scene, registrations};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_TRUE(result.failure.has_value());
        EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::UnsupportedEffect);
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(result.objects[0].state.hiddenMesh, -2);
        EXPECT_EQ(result.objects[0].light, glm::vec3(1.0f));
    }
}

// El infinito y lo que no entra en int64 siguen sin observar (Q-C186); el NaN ya no es frontera.
TEST(WorldObjectMapCreationTest, FullDeviasCreationRejectsTheUnverifiedFtolDomainExplicitly)
{
    for (const float angle : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0x1p63f})
    {
        const Client::Data::Terrain::TerrainObject record{.type = 20, .angle = {1.0f, 2.0f, angle}};
        Client::World::WorldObjectRegistrations registrations;
        Client::World::WorldObjectCreationContext context{2, Client::World::ObjectScene::Other, registrations};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_TRUE(result.failure.has_value());
        EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::InvalidAngle);
        EXPECT_EQ(result.failure->recordIndex, 0u);
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(result.objects[0].copiedPosition, glm::vec3(0.0f));
    }
}
// Detecta stop condicionado a stores, mapas fuera de18..23 aceptados y el tipo firmado mal extendido.
TEST(WorldObjectMapCreationTest, ChaosCastleAcceptsEverySignedTypeAndStopsBeforeOtherMapCases)
{
    std::vector<Client::Data::Terrain::TerrainObject> records;
    records.reserve(65536);
    for (int type = -32768; type <= 32767; ++type)
    {
        records.push_back({.type = static_cast<std::int16_t>(type), .position = {10.0f, 20.0f, 30.0f}});
    }
    for (const std::int32_t map : {18, 19, 20, 21, 22, 23})
    {
        SCOPED_TRACE(map);
        Client::World::WorldObjectRegistrations registrations;
        Client::World::WorldObjectCreationContext context{map, Client::World::ObjectScene::Other, registrations};
        const auto result = Client::World::CreateWorldObjects(records, context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 65536u);
        for (const auto& object : result.objects)
        {
            const bool hidden = (object.type >= 18 && object.type <= 21) || (object.type >= 24 && object.type <= 35);
            EXPECT_EQ(object.state.hiddenMesh, hidden ? -2 : -1) << object.type;
            EXPECT_EQ(object.basePosition, glm::vec3(10.0f, 20.0f, 30.0f));
        }
        EXPECT_EQ(registrations.entries[0].active, 0);
        EXPECT_EQ(registrations.nextIdentity, 65537u);
    }
}
// Sólo las lecturas de estado requeridas pueden fallar; se conservan los stores previos de Castle Siege.
TEST(WorldObjectMapCreationTest, CastleCreationPreservesKnownStoresBeforeAnyRequiredStateRead)
{
    for (const std::int16_t type :
         std::array<std::int16_t, 16>{7, 8, 9, 10, 13, 14, 19, 39, 61, 62, 63, 64, 77, 84, 415, -1})
    {
        SCOPED_TRACE(type);
        const Client::Data::Terrain::TerrainObject record{.type = type, .scale = 2.0f};
        Client::World::WorldObjectRegistrations registrations;
        Client::World::WorldObjectCreationContext context{30, Client::World::ObjectScene::Other, registrations};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        const bool requiresMissingState = type == 39 || type == 84 || type == 415;
        EXPECT_EQ(result.failure.has_value(), requiresMissingState);
        if (requiresMissingState)
        {
            ASSERT_TRUE(result.failure.has_value());
            EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::UnsupportedMapHandler);
        }
        ASSERT_EQ(result.objects.size(), 1u);
        const auto& object = result.objects[0];
        EXPECT_EQ(object.field08, 0);
        const bool marks91 = type == 7 || type == 10 || type == 13 || type == 14 || (type >= 61 && type <= 64);
        EXPECT_EQ(object.field91, marks91 ? 1 : 0);
        EXPECT_EQ(object.transform.scale, type == 19 || type == 415 ? 1.0f : 2.0f);
        EXPECT_EQ(object.state.hiddenMesh, type == 39 ? -2 : -1);
        EXPECT_EQ(registrations.entries[0].active, type == 77 || type == 84 ? 1 : 0);
        EXPECT_EQ(registrations.entries[0].object, type == 77 || type == 84 ? 1u : 0u);
    }
}

TEST(WorldObjectMapCreationTest, CastleRandomRunsBeforeARequiredLiveStateRead)
{
    const Client::Data::Terrain::TerrainObject record{.type = 41};
    Client::World::WorldObjectRegistrations registrations;
    Client::Render::Models::CrtRandom random(1);
    Client::World::WorldObjectCreationContext context{30, Client::World::ObjectScene::Other, registrations, nullptr,
                                                      &random};
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_TRUE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_EQ(random.State(), 0x0029E2C0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].field84), 0x3ED1EB85u);
}

TEST(WorldObjectMapCreationTest, CastleEffectRunsBeforeTheFollowingCreationHandlers)
{
    const Client::Data::Terrain::TerrainObject record{.type = 79, .position = {10.0f, 20.0f, 30.0f}};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    Client::World::WorldObjectCreationContext context{30, Client::World::ObjectScene::Other, registrations, &services};
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(services.emissions.size(), 1u);
    EXPECT_EQ(services.emissions[0].type, 309);
    EXPECT_EQ(services.emissions[0].position, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(services.emissions[0].owner, 1u);
    EXPECT_EQ(services.emissions[0].scale, 0.0f);
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_EQ(result.objects[0].transform.position, glm::vec3(110.0f, 120.0f, 130.0f));
}
// Detecta gastar IDs/RNG o pedir un servicio antes del prefijo espacial de 0x606660. Un NaN no sirve acá:
// cae en el sector 0 y se crea (Q-C186).
TEST(WorldObjectMapCreationTest, InvalidSpatialRecordsNeverReachServicesRegistrationOrRandom)
{
    struct Scenario
    {
        std::int16_t type;
        std::int32_t map;
        Client::World::ObjectScene scene;
    };
    const std::array scenarios{Scenario{180, 0, Client::World::ObjectScene::Login},
                               Scenario{322, 3, Client::World::ObjectScene::Other},
                               Scenario{41, 30, Client::World::ObjectScene::Other}};
    for (const auto& scenario : scenarios)
    {
        const std::array records{
            Client::Data::Terrain::TerrainObject{.type = scenario.type, .position = {25700.0f, 0.0f, 0.0f}},
            Client::Data::Terrain::TerrainObject{.type = scenario.type, .position = {0.0f, 25700.0f, 0.0f}},
            Client::Data::Terrain::TerrainObject{.type = scenario.type, .position = {-1601.0f, 0.0f, 0.0f}}};
        Client::World::WorldObjectRegistrations registrations;
        registrations.nextIdentity = 900;
        registrations.entries[0] = {.active = 2, .metadata = {1, 2, 3, 4, 5, 6, 7}, .object = 899};
        const auto previous = registrations.entries;
        Client::Render::Models::CrtRandom random(123);
        MutatingCreationServices services;
        Client::World::WorldObjectCreationContext context{scenario.map, scenario.scene, registrations, &services,
                                                          &random};
        const auto result = Client::World::CreateWorldObjects(records, context);
        EXPECT_FALSE(result.failure.has_value());
        EXPECT_TRUE(result.objects.empty());
        EXPECT_TRUE(services.emissions.empty());
        EXPECT_EQ(registrations.nextIdentity, 900u);
        EXPECT_EQ(registrations.entries, previous);
        EXPECT_EQ(random.State(), 123u);
    }
}
// Cada invocación nativa lee la escena global; el batch no debe congelar la del primer record.
TEST(WorldObjectMapCreationTest, EachRecordReadsTheSceneAfterThePreviousSynchronousService)
{
    const std::array records{Client::Data::Terrain::TerrainObject{.type = 180},
                             Client::Data::Terrain::TerrainObject{.type = 180}};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    Client::World::WorldObjectCreationContext context{0, Client::World::ObjectScene::Login, registrations, &services};
    services.context = &context;
    services.changeScene = true;
    const auto result = Client::World::CreateWorldObjects(records, context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 2u);
    EXPECT_EQ(services.emissions.size(), 1u);
    EXPECT_EQ(result.objects[0].state.hiddenMesh, -2);
    EXPECT_EQ(result.objects[1].state.hiddenMesh, -1);
    EXPECT_EQ(result.objects[1].light, glm::vec3(0.0f));
}
// El catálogo acredita creación del tipo default, no gameplay. No quedan errores sintéticos de wrappers puros.
TEST(WorldObjectMapCreationTest, EveryCatalogAndRawMapCompletesTheDefaultType)
{
    const auto check = [](std::int32_t map)
    {
        SCOPED_TRACE(map);
        const Client::Data::Terrain::TerrainObject record{.type = -1};
        Client::World::WorldObjectRegistrations registrations;
        Client::World::WorldObjectCreationContext context{map, Client::World::ObjectScene::Other, registrations};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        EXPECT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(result.objects[0].identity, 1u);
        EXPECT_EQ(registrations.nextIdentity, 2u);
        EXPECT_TRUE(result.objects[0].alive);
    };
    for (const std::uint8_t map : Client::Data::OriginalMaps)
    {
        check(map);
    }
    for (const std::int32_t map : std::array<std::int32_t, 6>{std::numeric_limits<std::int32_t>::min(), -1, 37, 255,
                                                              256, std::numeric_limits<std::int32_t>::max()})
    {
        check(map);
    }
}

// Proyecciones comprobadas: conservar el prefijo y el objeto parcial sin tratar sus copias como válidas.
TEST(WorldObjectMapCreationTest, BothProjectionsReturnInvalidAngleAndStopBeforeTheFollowingRecord)
{
    for (const bool withRegistry : {false, true})
        for (const std::int16_t type : std::array<std::int16_t, 4>{20, 65, 86, 88})
            for (const float angle :
                 {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0x1p63f})
            {
                SCOPED_TRACE(withRegistry);
                SCOPED_TRACE(type);
                SCOPED_TRACE(angle);
                const std::array records{
                    Client::Data::Terrain::TerrainObject{.type = 22, .position = {11, 22, 33}},
                    Client::Data::Terrain::TerrainObject{
                        .type = type, .position = {123, 456, 789}, .angle = {10, 20, angle}, .scale = 2},
                    Client::Data::Terrain::TerrainObject{.type = 22, .position = {44, 55, 66}}};
                Client::World::WorldObjectRegistrations registrations;
                registrations.entries[5] = {.active = 2, .metadata = {7, 6, 5, 4, 3, 2, 1}, .object = 999};
                const auto result =
                    withRegistry ? Client::World::CreateWorldObjects(records, 2, Client::World::ObjectScene::Other,
                                                                     registrations)
                                 : Client::World::CreateWorldObjects(records, 2, Client::World::ObjectScene::Other);
                EXPECT_TRUE(result.failure.has_value());
                if (!result.failure.has_value())
                    continue;
                EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::InvalidAngle);
                EXPECT_EQ(result.failure->recordIndex, 1u);
                ASSERT_EQ(result.objects.size(), 2u);
                EXPECT_EQ(result.objects[0].transform.position, glm::vec3(11, 22, 33));
                EXPECT_EQ(result.objects[1].transform.position, glm::vec3(123, 456, 789));
                EXPECT_EQ(result.objects[1].copiedPosition, glm::vec3(0));
                EXPECT_EQ(result.objects[1].copiedAngles, glm::vec3(0));
                if (withRegistry)
                {
                    EXPECT_EQ(registrations.nextIdentity, 3u);
                    EXPECT_EQ(registrations.entries[0].object, 1u);
                    EXPECT_EQ(registrations.entries[1].active, 0);
                    EXPECT_EQ(registrations.entries[5].object, 999u);
                    EXPECT_EQ(registrations.entries[5].metadata, (std::array<std::uint8_t, 7>{7, 6, 5, 4, 3, 2, 1}));
                }
            }
}

TEST(WorldObjectMapCreationTest, SceneEmissionChangesMapZeroToThirtyBeforeTheDirectDispatch)
{
    const Client::Data::Terrain::TerrainObject record{.type = 180, .position = {10, 20, 30}, .scale = 2};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    Client::World::WorldObjectCreationContext context{0, Client::World::ObjectScene::Login, registrations, &services};
    services.onCreate = [&](const auto& request, auto& source)
    {
        EXPECT_EQ(request.position, glm::vec3(10, 20, 30));
        source.transform.position.x = 77;
        EXPECT_FLOAT_EQ(request.position.x, 77);
        context.map = 30;
        source.type = 19;
        return true;
    };
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    ASSERT_EQ(services.emissions.size(), 1u);
    EXPECT_EQ(context.map, 30);
    EXPECT_EQ(record.type, 180);
    EXPECT_EQ(result.objects[0].type, 19);
    EXPECT_FLOAT_EQ(result.objects[0].transform.scale, 1);
    EXPECT_FLOAT_EQ(result.objects[0].transform.position.x, 77);
    EXPECT_EQ(result.objects[0].boundsMaximum, glm::vec3(40, 40, 80));
}
TEST(WorldObjectMapCreationTest, ASceneFailureSkipsTheDiscriminatingMapThirtyCaseAndTheNextRecord)
{
    const std::array records{Client::Data::Terrain::TerrainObject{.type = 180, .position = {10, 20, 30}, .scale = 2},
                             Client::Data::Terrain::TerrainObject{.type = 7, .position = {40, 50, 60}}};
    Client::World::WorldObjectRegistrations registrations;
    MutatingCreationServices services;
    Client::World::WorldObjectCreationContext context{0, Client::World::ObjectScene::Login, registrations, &services};
    services.onCreate = [&](const auto&, auto& source)
    {
        context.map = 30;
        source.type = 19;
        return false;
    };
    const auto result = Client::World::CreateWorldObjects(records, context);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::UnsupportedEffect);
    EXPECT_EQ(result.failure->recordIndex, 0u);
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_FLOAT_EQ(result.objects[0].transform.scale, 2);
    EXPECT_EQ(result.objects[0].type, 19);
    EXPECT_EQ(registrations.nextIdentity, 2u);
    EXPECT_EQ(services.emissions.size(), 1u);
}
TEST(WorldObjectMapCreationTest, NoriaFirstEmissionFailureKeepsTheRegisteredPrefixAndCurrentObjectOnly)
{
    const std::array records{Client::Data::Terrain::TerrainObject{.type = 8, .position = {10, 20, 30}},
                             Client::Data::Terrain::TerrainObject{.type = 322, .position = {40, 50, 60}},
                             Client::Data::Terrain::TerrainObject{.type = 8, .position = {70, 80, 90}}};
    Client::World::WorldObjectRegistrations registrations;
    registrations.entries[5] = {.active = 2, .metadata = {7, 6, 5, 4, 3, 2, 1}, .object = 999};
    MutatingCreationServices services;
    services.failAt = 1;
    Client::World::WorldObjectCreationContext context{3, Client::World::ObjectScene::Other, registrations, &services};
    const auto result = Client::World::CreateWorldObjects(records, context);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::UnsupportedEffect);
    EXPECT_EQ(result.failure->recordIndex, 1u);
    ASSERT_EQ(result.objects.size(), 2u);
    ASSERT_EQ(services.emissions.size(), 1u);
    EXPECT_EQ(services.emissions[0].type, 322);
    EXPECT_EQ(services.emissions[0].position, glm::vec3(40, 50, 410));
    EXPECT_EQ(registrations.entries[0].object, 1u);
    EXPECT_EQ(registrations.entries[1].active, 0);
    EXPECT_EQ(registrations.nextIdentity, 3u);
    EXPECT_EQ(registrations.entries[5].object, 999u);
    EXPECT_EQ(registrations.entries[5].metadata, (std::array<std::uint8_t, 7>{7, 6, 5, 4, 3, 2, 1}));
    EXPECT_EQ(result.objects[1].transform.position, glm::vec3(140, 150, 160));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].speed), 0x3e23d70au);
}
TEST(WorldObjectMapCreationTest, CastleByteStoresOverwriteAndPreserveNondefaultInputsAfterTheSceneEmission)
{
    for (const std::int16_t type : std::array<std::int16_t, 3>{7, 8, -1})
    {
        SCOPED_TRACE(type);
        const Client::Data::Terrain::TerrainObject record{.type = 180, .position = {10, 20, 30}, .scale = 2};
        Client::World::WorldObjectRegistrations registrations;
        const std::uint8_t state = 0;
        MutatingCreationServices services;
        Client::World::WorldObjectCreationContext context{
            0, Client::World::ObjectScene::Login, registrations, &services, nullptr, &state};
        services.onCreate = [&](const auto&, auto& source)
        {
            context.map = 30;
            source.type = type;
            source.field08 = 0xa5;
            source.field91 = 0x5a;
            return true;
        };
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(result.objects[0].field08, type == -1 ? 0xa5 : 0);
        EXPECT_EQ(result.objects[0].field91, type == 7 ? 1 : 0x5a);
        EXPECT_EQ(record.type, 180);
    }
}
TEST(WorldObjectMapCreationTest, FullApiRoutesTheExistingDirectRuleTableAndPreservesUnwrittenFields)
{
    const auto check = [](const DirectMapRule& row)
    {
        SCOPED_TRACE(row.map);
        SCOPED_TRACE(row.type);
        const Client::Data::Terrain::TerrainObject record{
            .type = static_cast<std::int16_t>(row.type), .position = {10, 20, 30}, .angle = {40, 50, 60}, .scale = 2};
        Client::World::WorldObjectRegistrations registrations;
        Client::World::WorldObjectCreationContext context{row.map, Client::World::ObjectScene::Other, registrations};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        const auto& object = result.objects[0];
        EXPECT_EQ(object.state.blendMesh, row.blend);
        EXPECT_EQ(object.state.hiddenMesh, row.hidden ? -2 : -1);
        EXPECT_EQ(object.boundsMaximum, glm::vec3(40, 40, row.maxZ));
        EXPECT_EQ(registrations.entries[0].active, row.registered ? 1 : 0);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(object.speed), 0x3e23d70au);
        EXPECT_FLOAT_EQ(object.transform.scale, row.map == 55 ? 60.0f : 2.0f);
        EXPECT_FLOAT_EQ(object.cullingMargin, -30);
        EXPECT_EQ(object.light, glm::vec3(0));
        EXPECT_TRUE(object.vertexLight);
        EXPECT_EQ(object.copiedPosition, glm::vec3(0));
        EXPECT_EQ(object.copiedAngles, glm::vec3(0));
        EXPECT_EQ(object.basePosition, glm::vec3(10, 20, 30));
        EXPECT_EQ(object.transform.position, row.map == 55 ? glm::vec3(8600, 25000, 5000) : glm::vec3(10, 20, 30));
        EXPECT_EQ(object.transform.angles, row.map == 55 ? glm::vec3(0) : glm::vec3(40, 50, 60));
    };
    for (const auto& row : DirectMapRules)
        check(row);
    for (const auto& row :
         std::array{DirectMapRule{0, 52, 1, false, false, 80}, DirectMapRule{55, 86, -1, false, false, 80}})
        check(row);
}
TEST(WorldObjectMapCreationTest, FullApiCopiesAllFourMapTwoTypesIncludingTheValidNegativeInt64Limit)
{
    struct Row
    {
        float angle;
        float normalized;
    };
    for (const std::int16_t type : std::array<std::int16_t, 4>{20, 65, 86, 88})
        for (const auto& row : std::array{Row{721.75f, 1}, Row{-721.75f, -1}, Row{-0x1p63f, 0}})
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(row.angle);
            const Client::Data::Terrain::TerrainObject record{
                .type = type, .position = {10, 20, 30}, .angle = {40, 50, row.angle}, .scale = 2};
            Client::World::WorldObjectRegistrations registrations;
            Client::World::WorldObjectCreationContext context{2, Client::World::ObjectScene::Other, registrations};
            const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
            ASSERT_FALSE(result.failure.has_value());
            ASSERT_EQ(result.objects.size(), 1u);
            EXPECT_EQ(result.objects[0].copiedPosition, glm::vec3(10, 20, 30));
            EXPECT_EQ(result.objects[0].copiedAngles, glm::vec3(40, 50, row.normalized));
            EXPECT_FLOAT_EQ(result.objects[0].transform.angles.z, row.normalized);
        }
}
} // namespace
