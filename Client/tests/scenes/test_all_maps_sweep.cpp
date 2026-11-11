#include "Scenes/MapLoad.h"

#include "Core/Crash/Crc32.h"
#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/WorldFolder.h"
#include "Render/Models/Cloth.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GroundMarks.h"
#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/MapSweepCases.h"

#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Scenes::SceneId;
using Client::Scenes::WorldState;
using Client::Tests::MapSweepCase;
using Client::Tests::MapSweepCases;
using Client::Tests::MapSweepCut;
using Client::Tests::MapSweepWorldCase;
using Client::Tests::MapSweepWorlds;

constexpr std::uint16_t PlayerKey = 5;
constexpr Client::World::WorldObjectIdentity FirstIdentity = 50000;
constexpr Client::World::WorldObjectIdentity StaleIdentity = 1000;
constexpr std::int16_t StaleType = -30000;
constexpr std::int32_t EffectLife = 73;
constexpr std::int32_t ParticleLife = 81;
constexpr std::array<std::uint8_t, 7> RegistrationPayload{1, 2, 3, 4, 5, 6, 7};
constexpr std::size_t OwnActor = 1;
constexpr std::size_t OtherActor = 2;
constexpr std::size_t LastActor = 400;
constexpr std::size_t DurableMarkSlot = 63;
constexpr Client::World::GroundMark DurableMark{.live = true, .type = 0x5e3, .size = 0.65f, .life = 25};

const MapSweepWorldCase* WorldCase(std::uint8_t folder)
{
    const auto found = std::ranges::find(MapSweepWorlds, folder, &MapSweepWorldCase::folder);
    return found == MapSweepWorlds.end() ? nullptr : &*found;
}

bool PublishesTerrain(MapSweepCut cut)
{
    return cut == MapSweepCut::None;
}

SceneId SceneOf(std::uint8_t nominal)
{
    if (nominal == 54)
        return SceneId::CharacterSelect;
    return nominal == 55 ? SceneId::Login : SceneId::Game;
}

std::string_view ExpectedError(MapSweepCut cut)
{
    switch (cut)
    {
    case MapSweepCut::None:
        return {};
    case MapSweepCut::MissingHeight:
        return "Data\\World6\\TerrainHeight.OZB no está";
    case MapSweepCut::CrywolfState:
        return "0x61a560: el .att del mapa 34 depende del estado de Crywolf (0x42fe50)";
    case MapSweepCut::MissingWorld:
        return "Data\\World36\\EncTerrain36.att no está";
    case MapSweepCut::LoginSpecials:
        return "0x616ec0: los especiales del mapa 55 (0x6188ae) no están leídos";
    }
    return "corte sin inventariar";
}

void SeedThePreviousMap(WorldState& world)
{
    auto& loaded = world.Map();
    loaded.kalima = std::make_unique<Client::World::KalimaWorld>(24);
    loaded.terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
    loaded.terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    loaded.terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    loaded.terrain.lighting = std::make_unique<Client::World::TerrainLighting>();
    loaded.terrain.mapping->layer1.front() = 91;
    loaded.terrain.heights->cells.front() = -123456.0f;
    loaded.grassOffsets = {-99.0f};
    loaded.objectRecords.push_back(Client::Data::Terrain::TerrainObject{.type = StaleType});
    Client::World::WorldObject stale;
    stale.identity = StaleIdentity;
    stale.type = StaleType;
    loaded.objects.push_back(stale);
    for (std::size_t slot = 0; slot < loaded.registrations.entries.size(); ++slot)
    {
        loaded.registrations.entries[slot] = {
            .active = 0x80, .metadata = RegistrationPayload, .object = StaleIdentity + slot};
    }
    loaded.registrations.nextIdentity = FirstIdentity;
}

void SeedTheEffectPools(WorldState& world)
{
    for (Client::World::ModelEffect& effect : world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.seen = true;
        effect.life = EffectLife;
    }
    for (Client::World::Particle& particle : world.Particles().All())
    {
        particle.alive = true;
        particle.life = ParticleLife;
    }
    world.Particles().Wind().wind = glm::vec2(1.0f, -0.5f);
    world.Particles().Wind().change = glm::vec2(-0.25f, 0.125f);
    world.Runtime().groundMarks[DurableMarkSlot] = DurableMark;
    world.Runtime().warpCounter = 123;
    world.Runtime().drawingEnabled = true;
}

void SeedTheActors(WorldState& world)
{
    auto actors = world.Actors();
    for (const std::size_t slot : {OwnActor, OtherActor, LastActor})
    {
        actors[slot].alive = true;
        actors[slot].key = slot == OwnActor ? PlayerKey : PlayerKey + 1;
        actors[slot].darkSpirit.emplace();
    }
    actors[3].darkSpirit.emplace(); // También se suelta el cuervo del que ya estaba muerto.
    world.Helpers()[0] = {.alive = true, .rider = OwnActor};
    world.Helpers()[1] = {.alive = true, .rider = OtherActor};
    world.Helpers()[2] = {.alive = true, .rider = LastActor};
}

std::uint32_t CrcOf(std::span<const std::uint8_t> bytes)
{
    Client::Core::Crash::Crc32 crc;
    crc.Update(std::as_bytes(bytes));
    return crc.Value();
}

void ExpectTerrain(const Client::World::WorldTerrain& terrain, const MapSweepWorldCase& expected)
{
    ASSERT_NE(terrain.mapping, nullptr);
    ASSERT_NE(terrain.attributes, nullptr);
    ASSERT_NE(terrain.heights, nullptr);
    ASSERT_NE(terrain.lighting, nullptr);
    EXPECT_EQ(CrcOf(terrain.mapping->layer1), expected.layer1Crc);
    EXPECT_EQ(CrcOf(terrain.mapping->layer2), expected.layer2Crc);
    // La altura ocupa byte*1,5; World56 usa byte*3. Ambas sumas son exactas en double.
    const double scale = expected.folder == 56 ? 3.0 : 1.5;
    EXPECT_EQ(std::accumulate(terrain.heights->cells.begin(), terrain.heights->cells.end(), 0.0),
              static_cast<double>(expected.heightByteSum) * scale);
}

void ExpectPublishedFiles(const Client::Scenes::WorldMap& loaded, const MapSweepCase& probe,
                          const MapSweepWorldCase& expected)
{
    if (probe.cut == MapSweepCut::MissingHeight)
    {
        // 61a650..61a7e9 publica MAP/ATT y OBJ antes de cargar las alturas (Q-C180).
        ASSERT_NE(loaded.terrain.mapping, nullptr);
        ASSERT_NE(loaded.terrain.attributes, nullptr);
        EXPECT_EQ(CrcOf(loaded.terrain.mapping->layer1), expected.layer1Crc);
        EXPECT_EQ(CrcOf(loaded.terrain.mapping->layer2), expected.layer2Crc);
        EXPECT_EQ(loaded.terrain.heights, nullptr);
        EXPECT_EQ(loaded.terrain.lighting, nullptr);
        EXPECT_EQ(loaded.objectRecords.size(), expected.records);
        return;
    }
    if (!PublishesTerrain(probe.cut))
    {
        EXPECT_EQ(loaded.terrain.mapping, nullptr);
        EXPECT_EQ(loaded.terrain.attributes, nullptr);
        EXPECT_EQ(loaded.terrain.heights, nullptr);
        EXPECT_EQ(loaded.terrain.lighting, nullptr);
        EXPECT_TRUE(loaded.objectRecords.empty());
        return;
    }
    ExpectTerrain(loaded.terrain, expected);
    ASSERT_EQ(loaded.objectRecords.size(), expected.records);
    ASSERT_FALSE(loaded.objectRecords.empty());
    EXPECT_EQ(loaded.objectRecords.front().type, expected.firstType);
    EXPECT_EQ(loaded.objectRecords.back().type, expected.lastType);
    // Los especiales preceden al archivo: dos en Devias, la torre de Noria y los cinco lanceros 0x19f del 30
    // (0x42a760).
    const std::size_t specialCount = probe.nominal == 2 ? 2 : (probe.nominal == 3 ? 1 : (probe.nominal == 30 ? 5 : 0));
    ASSERT_EQ(loaded.objects.size(), specialCount + loaded.objectRecords.size());
    for (std::size_t record = 0; record < loaded.objectRecords.size(); ++record)
    {
        ASSERT_EQ(loaded.objects[specialCount + record].type, loaded.objectRecords[record].type) << record;
    }
}

void ExpectObjectIdentities(const Client::Scenes::WorldMap& loaded, const MapSweepCase& probe)
{
    ASSERT_EQ(loaded.objects.size(), probe.created);
    EXPECT_EQ(loaded.registrations.nextIdentity, FirstIdentity + probe.created);
    for (std::size_t index = 0; index < loaded.objects.size(); ++index)
    {
        ASSERT_EQ(loaded.objects[index].identity, FirstIdentity + index);
        ASSERT_NE(loaded.objects[index].type, StaleType);
    }
    for (std::size_t slot = 0; slot < loaded.registrations.entries.size(); ++slot)
    {
        const auto& entry = loaded.registrations.entries[slot];
        ASSERT_EQ(entry.metadata, RegistrationPayload) << slot;
        ASSERT_EQ(entry.active, slot < probe.registrations ? 1 : 0) << slot;
        if (entry.active != 0)
        {
            ASSERT_GE(entry.object, FirstIdentity) << slot;
            ASSERT_LT(entry.object, FirstIdentity + probe.created) << slot;
        }
        else
        {
            ASSERT_EQ(entry.object, StaleIdentity + slot) << slot;
        }
    }
}

void ExpectGrassAndRandom(const Client::Scenes::WorldMap& loaded, const MapSweepCase& probe,
                          const Client::Render::Models::CrtRandom& random)
{
    EXPECT_EQ(random.State(), probe.randomState);
    if (probe.randomState == Client::Tests::MapSweepSeed)
    {
        EXPECT_TRUE(loaded.grassOffsets.empty());
        return;
    }
    ASSERT_EQ(loaded.grassOffsets.size(), Client::Data::Terrain::TerrainCellCount);
    std::vector<std::uint8_t> quarters;
    quarters.reserve(loaded.grassOffsets.size());
    for (const float offset : loaded.grassOffsets)
    {
        ASSERT_TRUE(offset == 0.0f || offset == 0.25f || offset == 0.5f || offset == 0.75f);
        quarters.push_back(static_cast<std::uint8_t>(offset * 4.0f));
    }
    // CRC de la secuencia literal de la CRT, calculada fuera de MuRet desde seed=5eed.
    // Noria antepone12 sorteos de sus cuatro efectos322/323; CRC calculado con la LCG literal.
    EXPECT_EQ(CrcOf(quarters), probe.nominal == 3 ? 0x7c9c1daau : 0xc78fa93bu);
}

void ExpectEffectCleanup(const WorldState& world, const MapSweepCase& probe)
{
    constexpr std::array<std::int16_t, 5> NoriaEffects{322, 323, 322, 323, 324};
    const auto effects = world.Map().effects.Effects();
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        const auto& effect = effects[slot];
        ASSERT_TRUE(effect.seen); // El creador retiene seen incluso al reutilizar estos primeros slots.
        if (probe.nominal == 3 && slot < NoriaEffects.size())
        {
            ASSERT_TRUE(effect.alive);
            ASSERT_EQ(effect.type, NoriaEffects[slot]);
            ASSERT_EQ(effect.life, 0xffffff);
        }
        else if (probe.nominal == 30 && slot == 0)
        {
            // El único 79 de World31 pide el plano 0x135 con vida 100 (0x42c6b0 → 0x51d73b).
            ASSERT_TRUE(effect.alive);
            ASSERT_EQ(effect.type, 0x135);
            ASSERT_EQ(effect.life, 100);
        }
        else
        {
            ASSERT_FALSE(effect.alive);
            ASSERT_EQ(effect.life, EffectLife);
        }
    }
    for (const Client::World::Particle& particle : world.Particles().All())
    {
        ASSERT_FALSE(particle.alive);
        ASSERT_EQ(particle.life, ParticleLife);
    }
    EXPECT_EQ(world.Particles().Wind().wind, glm::vec2(1.0f, -0.5f));
    EXPECT_EQ(world.Particles().Wind().change, glm::vec2(-0.25f, 0.125f));
    EXPECT_EQ(world.Runtime().groundMarks[DurableMarkSlot], DurableMark);
    EXPECT_EQ(world.Runtime().warpCounter, 123);
    EXPECT_TRUE(world.Runtime().drawingEnabled);
}

void ExpectActorCleanup(const WorldState& world, SceneId scene)
{
    const bool selection = scene == SceneId::CharacterSelect;
    const auto actors = world.Actors();
    EXPECT_TRUE(actors[OwnActor].alive);
    EXPECT_EQ(actors[OtherActor].alive, selection);
    EXPECT_TRUE(actors[LastActor].alive);
    EXPECT_EQ(actors[OwnActor].darkSpirit.has_value(), selection);
    EXPECT_EQ(actors[OtherActor].darkSpirit.has_value(), selection);
    EXPECT_EQ(actors[3].darkSpirit.has_value(), selection);
    EXPECT_TRUE(actors[LastActor].darkSpirit.has_value());
    EXPECT_TRUE(world.Helpers()[0].alive);
    EXPECT_EQ(world.Helpers()[1].alive, selection);
    EXPECT_TRUE(world.Helpers()[2].alive);
}

void ExpectArchiveObjects(const std::filesystem::path& dataRoot, const MapSweepCase& probe,
                          const MapSweepWorldCase& expected)
{
    const auto records = Client::World::LoadWorldObjects(dataRoot, probe.nominal);
    if (probe.folder == 36)
    {
        ASSERT_FALSE(records.HasValue());
        EXPECT_EQ(records.Error(), "Data\\World36\\EncTerrain36.obj no está");
        return;
    }
    ASSERT_TRUE(records.HasValue()) << records.Error();
    ASSERT_EQ(records.Value().size(), expected.records);
    if (expected.records != 0)
    {
        EXPECT_EQ(records.Value().front().type, expected.firstType);
        EXPECT_EQ(records.Value().back().type, expected.lastType);
    }
}

void ExpectArchiveTerrain(const std::filesystem::path& dataRoot, const MapSweepCase& probe,
                          const MapSweepWorldCase& expected)
{
    const auto terrain = Client::World::LoadWorldTerrain(dataRoot, probe.nominal);
    if (probe.cut == MapSweepCut::MissingHeight || probe.cut == MapSweepCut::MissingWorld)
    {
        ASSERT_FALSE(terrain.HasValue());
        EXPECT_EQ(terrain.Error(), ExpectedError(probe.cut));
        return;
    }
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    ExpectTerrain(terrain.Value(), expected);
}

void ExpectGateInventory(const std::filesystem::path& path, const MapSweepCase& probe)
{
    const auto bytes = Client::Tests::ReadFileBytes(path);
    ASSERT_TRUE(bytes.has_value());
    const auto gates = Client::Data::Tables::LoadGateTable(std::as_bytes(std::span(*bytes)));
    ASSERT_TRUE(gates.HasValue());
    std::array<std::size_t, 3> counts{};
    std::size_t populated = 0;
    for (std::size_t index = 0; index < Client::Data::Tables::GateCount; ++index)
    {
        const auto record = Client::Data::Tables::ReadGateRecord(gates.Value(), index);
        ASSERT_TRUE(record.has_value());
        if (*record == Client::Data::Tables::GateRecord{})
            continue;
        ++populated;
        ASSERT_LT(record->flag, counts.size()) << index;
        ASSERT_NE(std::ranges::find(Client::Data::OriginalMaps, record->map), Client::Data::OriginalMaps.end())
            << index;
        if (record->map == probe.nominal)
            ++counts[record->flag];
    }
    EXPECT_EQ(populated, 125u);
    EXPECT_EQ(counts, probe.gates);
}

TEST(MapSweepInventoryTest, IncludesAllThirtyNineNominalMapsAndAllGateSources)
{
    ASSERT_EQ(MapSweepCases.size(), 39u);
    ASSERT_EQ(MapSweepCases.size(), Client::Data::OriginalMaps.size());
    std::array<std::size_t, 3> gates{};
    for (std::size_t index = 0; index < MapSweepCases.size(); ++index)
    {
        const MapSweepCase& probe = MapSweepCases[index];
        EXPECT_EQ(probe.nominal, Client::Data::OriginalMaps[index]);
        EXPECT_NE(WorldCase(probe.folder), nullptr);
        for (std::size_t flag = 0; flag < gates.size(); ++flag)
            gates[flag] += probe.gates[flag];
    }
    constexpr std::array<std::size_t, 3> NativeGateCounts{49, 38, 38};
    EXPECT_EQ(gates, NativeGateCounts);
}

class MapSweep : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<MapSweepCase>
{
protected:
    WorldState m_world;
    Client::Render::Models::CrtRandom m_random{Client::Tests::MapSweepSeed};
};

TEST_P(MapSweep, LoadsTheNominalIdentityOrStopsAtTheDeclaredBoundary)
{
    const MapSweepCase& probe = GetParam();
    const MapSweepWorldCase* expected = WorldCase(probe.folder);
    ASSERT_NE(expected, nullptr);
    SeedThePreviousMap(m_world);
    SeedTheEffectPools(m_world);
    SeedTheActors(m_world);
    m_world.Runtime().map = probe.nominal;
    const SceneId scene = SceneOf(probe.nominal);

    const auto loaded = Client::Scenes::LoadMap(
        probe.nominal,
        {.dataRoot = m_dataRoot, .world = m_world, .scene = scene, .playerKey = PlayerKey, .random = m_random});

    EXPECT_EQ(loaded.HasValue(), probe.cut == MapSweepCut::None);
    if (!loaded.HasValue())
    {
        EXPECT_EQ(loaded.Error(), ExpectedError(probe.cut));
    }
    EXPECT_EQ(m_world.Runtime().map, probe.effective);
    EXPECT_EQ(static_cast<bool>(m_world.Map().kalima),
              loaded.HasValue() ? Client::World::IsKalimaMap(probe.effective) : true);
    if (m_world.Map().kalima)
    {
        EXPECT_EQ(m_world.Map().kalima->Map(), loaded.HasValue() ? probe.effective : 24);
    }
    ExpectPublishedFiles(m_world.Map(), probe, *expected);
    ExpectObjectIdentities(m_world.Map(), probe);
    ExpectGrassAndRandom(m_world.Map(), probe, m_random);
    ExpectEffectCleanup(m_world, probe);
    ExpectActorCleanup(m_world, scene);
    if (loaded.HasValue())
    {
        Client::Scenes::ArmEventMapObject(m_world.Runtime().map, m_world.Runtime());
        EXPECT_EQ(m_world.Runtime().eventObject.has_value(), probe.eventObject);
    }
}

// Los lectores aislados también cubren las carpetas que LoadMap todavía no alcanza: World4,
// World31/35 y World56. No afirman que estén implementados sus hooks, variantes de .att ni escenas.
TEST_P(MapSweep, MatchesTheIndependentArchivedWorldAndGateInventory)
{
    const MapSweepCase& probe = GetParam();
    const MapSweepWorldCase* expected = WorldCase(probe.folder);
    ASSERT_NE(expected, nullptr);
    EXPECT_EQ(Client::Data::WorldFolderNumber(probe.nominal), probe.folder);
    EXPECT_EQ(Client::Data::WorldFolder(probe.nominal), "World" + std::to_string(probe.folder));
    ExpectArchiveObjects(m_dataRoot, probe, *expected);
    ExpectArchiveTerrain(m_dataRoot, probe, *expected);
    const auto gates = DataFile("Gate.bmd");
    ASSERT_TRUE(gates.has_value());
    ExpectGateInventory(*gates, probe);
}

INSTANTIATE_TEST_SUITE_P(AllOriginalMaps, MapSweep, ::testing::ValuesIn(MapSweepCases),
                         [](const ::testing::TestParamInfo<MapSweepCase>& parameter)
                         { return "Map" + std::to_string(parameter.param.nominal); });
} // namespace
