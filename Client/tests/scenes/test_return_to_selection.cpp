#include "Scenes/ReturnToSelection.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FaunaState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperTable.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

// La vuelta del juego a la selección (0x4c2710 con el F1 02 con 1 cifrado, 0x4c3626..0x4c390f, y 0x4c2480),
// en lo que MuRet tiene del mundo. Contrato: 06, "La vuelta a la selección", y Q-C211 a Q-C215.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::ApplyReturnToSelection;
using Client::Scenes::WorldEntryResources;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::uint16_t PlayerKey = 5;
// El cierre del registro propio que lo deja sin accesos (0x4c260f..0x4c264b, 0x40b4d0).
constexpr int OwnRecordClose = 0xf08;
// 0x510f70(−1) compara la clave con signo: solo sobrevive esta (Q-C212).
constexpr std::uint16_t SurvivingKey = 0xffff;
constexpr std::uint16_t OtherKey = 0x1234;
// El último del pool, que 0x510f70 no recorre.
constexpr std::size_t LastActor = Client::Scenes::ActorPoolSize - 1;

class SilentPublisher final : public Client::Scenes::IWorldEntryPublisher
{
public:
    void PublishWorldEntry() override {}
};

// Lo que dura la sesión, con Player.bmd, un Item.bmd sin armas marcadas y el registro propio sin equipo,
// como en test_world_entry.cpp.
struct Returning
{
    Returning(std::filesystem::path root, std::uint32_t seed)
        : dataRoot(std::move(root)), cloth{.random = CrtRandom(seed)}
    {
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = dataRoot,
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = &items,
                                   .log = log};
    }

    [[nodiscard]] std::size_t AliveEffects() const
    {
        const auto effects = world.Map().effects.Effects();
        return static_cast<std::size_t>(
            std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }));
    }

    std::filesystem::path dataRoot;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// Las tablas de los conjuntos sin ningún conjunto, para que la entrada llegue al final.
Client::Data::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// La respuesta del F3 03 en la celda (x, y) de `map`, con la dirección 0, el PK 4 y el CtlCode 0x20.
Client::Protocol::Character::CharacterSelectResult ReplyAt(std::uint8_t map, std::uint8_t x, std::uint8_t y)
{
    Client::Protocol::Character::CharacterSelectResult reply;
    reply.map = map;
    reply.x = x;
    reply.y = y;
    reply.pk = 4;
    reply.controlCode = 0x20;
    return reply;
}

CrtRandom Advanced(CrtRandom random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// Un lugar del pool entre los que mata 0x510f70 que no sea `taken`.
std::size_t SlotOtherThan(std::size_t wanted, std::size_t taken)
{
    return wanted == taken ? wanted + 1 : wanted;
}

struct ReferenceCell
{
    std::uint8_t map = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
constexpr ReferenceCell Lorencia{.map = 0, .x = 133, .y = 127};
constexpr ReferenceCell Devias{.map = 2, .x = 220, .y = 30};

class ReturnToSelectionTest : public Client::Tests::GoldenDataTest
{
};

// Con el héroe en el mundo, en Lorencia y en Devias: 0x6070a0 suelta los objetos y los efectos sin tocar el
// terreno ni el pasto; 0x510f70(−1) mata a los 400 menos al de la clave 0xffff, también al héroe, y el 401
// queda; la orden de la escena 4, el mundo sin cargar, el nivel de la cámara en 0, el mundo en curso en −1 y
// el registro propio con +0x3c..+0x42 en 0 y su cierre de 3.848 rand(), lo único que saca. El héroe publicado
// sigue en su lugar, y la cámara copia su posición; la selección mira el terreno del juego (Q-C213).
TEST_F(ReturnToSelectionTest, TheReturnReleasesTheGameAndKeepsTheTerrain)
{
    for (const ReferenceCell& probe : {Lorencia, Devias})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Returning tested(m_dataRoot, Seed);
        const Client::Data::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        SilentPublisher publisher;
        const auto entered =
            Client::Scenes::ApplyWorldEntry(ReplyAt(probe.map, probe.x, probe.y), PlayerKey, resources, publisher);
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
        const std::size_t hero = runtime.hero;
        ASSERT_TRUE(tested.world.Actors()[hero].alive);
        ASSERT_FALSE(tested.world.Map().objects.empty());
        ASSERT_GT(tested.AliveEffects(), 0u);
        // Lo que dejó la carga del 54 de la selección (0x633f30) o el paso del juego en el estado 0x3d.
        runtime.drawingEnabled = true;
        tested.camera.level = 2;
        const std::size_t stranger = SlotOtherThan(7, hero);
        const std::size_t survivor = SlotOtherThan(9, hero);
        tested.world.Actors()[stranger].alive = true;
        tested.world.Actors()[stranger].key = OtherKey;
        tested.world.Actors()[survivor].alive = true;
        tested.world.Actors()[survivor].key = SurvivingKey;
        tested.world.Actors()[LastActor].alive = true;
        tested.world.Actors()[LastActor].key = OtherKey;
        Client::Scenes::OwnCharacterRecord& own = tested.world.OwnCharacter();
        own.speedAdjustBits = 3;
        own.word3e = 4;
        own.word40 = 5;
        own.word42 = 6;
        const auto* attributes = tested.world.Map().terrain.attributes.get();
        const auto* heights = tested.world.Map().terrain.heights.get();
        const std::vector<float> grass = tested.world.Map().grassOffsets;
        const CrtRandom before = tested.cloth.random;

        ApplyReturnToSelection(resources);

        EXPECT_TRUE(tested.world.Map().objects.empty());
        EXPECT_TRUE(tested.world.Map().objectRecords.empty());
        EXPECT_EQ(tested.AliveEffects(), 0u);
        EXPECT_EQ(tested.world.Map().terrain.attributes.get(), attributes);
        EXPECT_EQ(tested.world.Map().terrain.heights.get(), heights);
        EXPECT_TRUE(tested.world.Map().terrain.mapping);
        EXPECT_TRUE(tested.world.Map().terrain.lighting);
        EXPECT_EQ(tested.world.Map().grassOffsets, grass);
        EXPECT_FALSE(tested.world.Actors()[hero].alive);
        EXPECT_FALSE(tested.world.Actors()[stranger].alive);
        EXPECT_TRUE(tested.world.Actors()[survivor].alive);
        EXPECT_TRUE(tested.world.Actors()[LastActor].alive);
        EXPECT_EQ(runtime.hero, hero);
        EXPECT_EQ(runtime.orderedScene, Client::Scenes::SceneId::CharacterSelect);
        EXPECT_FALSE(runtime.drawingEnabled);
        EXPECT_EQ(runtime.map, -1);
        EXPECT_EQ(tested.camera.level, 0);
        EXPECT_EQ(own.speedAdjustBits, 0);
        EXPECT_EQ(own.word3e, 0);
        EXPECT_EQ(own.word40, 0);
        EXPECT_EQ(own.word42, 0);
        EXPECT_EQ(runtime.ownRecordDepth, 0);
        EXPECT_EQ(tested.cloth.random.State(), Advanced(before, OwnRecordClose).State());
        EXPECT_EQ(tested.cameraInput.hero, tested.world.Actors()[hero].position);
        EXPECT_EQ(tested.selection.Terrain(), &tested.world.Map().terrain);
        EXPECT_EQ(tested.selection.Map(), -1);
    }
}

// Con el registro propio abierto por otro, el cierre no lo deja sin accesos y no saca rand(); los cuatro
// campos se escriben igual.
TEST(ReturnToSelectionWithoutDataTest, WithTheOwnRecordOpenTheReturnDrawsNoRand)
{
    Returning tested({}, Seed);
    tested.world.Runtime().ownRecordDepth = 1;
    tested.world.OwnCharacter().word40 = 5;
    const CrtRandom before = tested.cloth.random;

    ApplyReturnToSelection(tested.Resources());

    EXPECT_EQ(tested.cloth.random.State(), before.State());
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 1);
    EXPECT_EQ(tested.world.OwnCharacter().word40, 0);
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Scenes::SceneId::CharacterSelect);
}

// 0x4c2515..0x4c252d: la vuelta deja los cinco del picking en −1, también el personaje elegido y 0x6c8e38.
TEST(ReturnToSelectionWithoutDataTest, TheReturnForgetsThePickedTargets)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    tested->world.Runtime().selectedTargets = {3, 4, 5, 6, 1};

    ApplyReturnToSelection(tested->Resources());

    EXPECT_EQ(tested->world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
}

// 0x510f70 compara la clave con signo (movsx en 0x510f8a): con −1, la clave 0x7fff muere y la 0xffff no;
// los muertos quedan muertos. El caballo del que muere se apaga (0x510fa1..0x510fc0); el del que queda, no.
TEST(ReturnToSelectionWithoutDataTest, OnlyTheKeyFFFFSurvives)
{
    Returning tested({}, Seed);
    const auto actors = tested.world.Actors();
    actors[1].alive = true;
    actors[1].key = 0x7fff;
    actors[2].alive = true;
    actors[2].key = SurvivingKey;
    actors[3].key = SurvivingKey;
    auto& helpers = tested.world.Helpers();
    helpers[0] = Client::Scenes::HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = 1};
    helpers[1] = Client::Scenes::HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = 2};

    ApplyReturnToSelection(tested.Resources());

    EXPECT_FALSE(actors[1].alive);
    EXPECT_TRUE(actors[2].alive);
    EXPECT_FALSE(actors[3].alive);
    EXPECT_FALSE(helpers[0].alive);
    EXPECT_TRUE(helpers[1].alive);
}
TEST(ReturnToSelectionPoolsTest, ReleasesTheLiveFlagsWithoutReplacingRetainedRecords)
{
    Returning tested({}, Seed);
    tested.world.Fauna().Birds()[0].active = true;
    tested.world.Leaves().All()[0].alive = true;
    tested.world.Leaves().All()[0].type = 123;
    tested.world.DrawTail().damage[2].alive = true;
    tested.world.DrawTail().damage[2].number = 567;
    tested.world.DrawTail().floorItems[3].alive = true;
    tested.world.DrawTail().joints[4].alive = true;
    tested.world.DrawTail().ribbons[5].alive = true;
    ApplyReturnToSelection(tested.Resources());
    EXPECT_FALSE(tested.world.Fauna().Birds()[0].active);
    EXPECT_FALSE(tested.world.Leaves().All()[0].alive);
    EXPECT_EQ(tested.world.Leaves().All()[0].type, 123);
    EXPECT_FALSE(tested.world.DrawTail().damage[2].alive);
    EXPECT_EQ(tested.world.DrawTail().damage[2].number, 567);
    EXPECT_FALSE(tested.world.DrawTail().floorItems[3].alive);
    EXPECT_FALSE(tested.world.DrawTail().joints[4].alive);
    EXPECT_FALSE(tested.world.DrawTail().ribbons[5].alive);
}
} // namespace
