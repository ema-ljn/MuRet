#include "Scenes/WorldEntry.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameData.h"
#include "Scenes/GameScene.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::ApplyWorldEntry;
using Client::Scenes::SettleWorldEntry;
using Client::Scenes::WorldEntryResources;
using Client::Scenes::WorldEntryResult;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::uint16_t PlayerKey = 5;
constexpr std::uint16_t Player = Client::Data::Models::PlayerSkeletonModel;
// Los cierres del registro propio, el pasto, el lugar del héroe, 0x511290 y el sufijo de 0x4f2250.
constexpr int OwnRecordClose = 0xf08;
constexpr int GrassRandoms = 65536;
constexpr int HeroRandom = 1;
constexpr int ConstructionRandoms = 28;
constexpr int StopRandom = 1;
// Los de la cola de 0x550d10, con 0x512a30 entera.
constexpr int TailRandoms = 3;
// Después de 0x512a30: el contador (4) y las dos consultas de 0x657a90.
constexpr int WaitClose = 4;
constexpr int WindowRandoms = 2;
constexpr std::size_t PublishedSlot = 42;
// Los rand() del pase de los objetos del primer frame del juego (0x602ef4), uno por objeto visto que llega a
// 0x602ed0, con el héroe en la primera celda de cada mapa de referencia (*simulado*, con el port y la Data del
// repo): en Lorencia, en (133, 127), 84 hasta el 0x69 (1720), que corta en 0x600a4a antes del suyo; en Devias,
// en (220, 30), los 54 del pase entero, y el dibujo corta en los efectos del suelo (0x6374d3).
constexpr int LorenciaObjectRandoms = 84;
constexpr int DeviasObjectRandoms = 54;

class RecordingPublisher final : public Client::Scenes::IWorldEntryPublisher
{
public:
    void PublishWorldEntry() override
    {
        ++calls;
        if (onPublish)
        {
            onPublish();
        }
    }

    int calls = 0;
    std::function<void()> onPublish;
};

// Lo que dura la sesión, con Player.bmd, un Item.bmd sin armas marcadas y el registro propio sin equipo.
struct Entry
{
    Entry(std::filesystem::path root, std::uint32_t seed) : dataRoot(std::move(root)), cloth{.random = CrtRandom(seed)}
    {
        models.Set(Player, {}, Client::Tests::PlayerActionsSkeleton(), {});
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

    [[nodiscard]] ActorState& Hero()
    {
        return world.Actors()[static_cast<std::size_t>(world.Runtime().heroSlot)];
    }

    [[nodiscard]] std::size_t AliveArrivals() const
    {
        const auto effects = world.Map().effects.Effects();
        return static_cast<std::size_t>(std::ranges::count_if(
            effects, [](const auto& effect) { return effect.alive && effect.type == Client::World::ArrivalEffect; }));
    }

    // El héroe ya vestido en `slot`, para lo que sigue a 0x512a30.
    ActorState& DressedHero(std::size_t slot)
    {
        world.Runtime().heroSlot = static_cast<std::int32_t>(slot);
        ActorState& hero = Hero();
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.alpha = 1.0f;
        hero.position = {13350.0f, 12750.0f, 165.0f};
        hero.angles = {0.0f, 0.0f, -45.0f};
        return hero;
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

// La respuesta del cable: la celda (133, 127) del mapa 0 con la dirección 0, el PK 4 y el CtlCode 0x20.
Client::Protocol::Character::CharacterSelectResult CableReply()
{
    Client::Protocol::Character::CharacterSelectResult reply;
    reply.x = 133;
    reply.y = 127;
    reply.map = 0;
    reply.direction = 0;
    reply.pk = 4;
    reply.controlCode = 0x20;
    return reply;
}

class WorldEntryTest : public Client::Tests::GoldenDataTest
{
};

// 06, fila 3: el lugar del héroe es el rand() que sigue al primer cierre del registro y al pasto.
TEST_F(WorldEntryTest, TheHeroSlotIsTheRandAfterTheGrassOffsets)
{
    Entry tested(m_dataRoot, Seed);
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, tested.Resources(), publisher));

    CrtRandom replay = AfterDraws(Seed, OwnRecordClose + GrassRandoms);
    EXPECT_EQ(tested.world.Runtime().heroSlot, replay.Next() % 400);
}

// Q-C175: la dirección 0 da −45°, y el héroe queda en el centro de la celda, con la clave, la clase
// del registro, la piel en 0 y el PK y el CtlCode de la respuesta.
TEST_F(WorldEntryTest, TheCableReplyGives13350_12750AndMinus45)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.OwnCharacter().characterClass = 0x20;
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, tested.Resources(), publisher));

    const ActorState& hero = tested.Hero();
    EXPECT_TRUE(hero.alive);
    EXPECT_EQ(hero.position.x, 13350.0f);
    EXPECT_EQ(hero.position.y, 12750.0f);
    EXPECT_EQ(hero.angles.z, -45.0f);
    EXPECT_EQ(hero.key, PlayerKey);
    EXPECT_EQ(hero.characterClass, 0x20);
    EXPECT_EQ(hero.skin, 0);
    EXPECT_EQ(hero.pk, 4);
    EXPECT_EQ(hero.controlCode, 0x20);
    EXPECT_EQ(hero.byte88, 1);
    EXPECT_EQ(tested.cameraInput.map, 0);
}

// 06, filas 5 y 7: 0x512a30 viste con el registro viejo, y recién después se vacía su equipo.
TEST_F(WorldEntryTest, TheOldRecordDressesTheHeroBeforeItIsEmptied)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.OwnCharacter().equipment[0].id = 5;
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, tested.Resources(), publisher));

    constexpr std::uint16_t Sword = 5 + Client::Data::Models::FirstItemModel;
    EXPECT_EQ(tested.Hero().hands[0].model, Sword);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, 5);

    // Con 0x512a30 entero, lo que sigue vacía el registro y el héroe conserva lo puesto.
    ASSERT_TRUE(SettleWorldEntry({}, tested.Resources(), publisher).HasValue());
    EXPECT_EQ(tested.Hero().hands[0].model, Sword);
    for (const auto& item : tested.world.OwnCharacter().equipment)
    {
        EXPECT_EQ(item.id, Client::Scenes::EmptyOwnItemId);
    }
}

// La respuesta del F3 03 del GS, cifrada: C1 de 0x34 bytes con la celda (x, y) del mapa 0, la dirección 0,
// el PK 4 (pkt[0x2c]) y el CtlCode 0x20 (pkt[0x2d]).
std::vector<std::byte> EntryPacket(std::uint8_t x, std::uint8_t y, Client::Network::PacketSerials& serials)
{
    constexpr std::size_t Size = 0x34;
    std::vector<std::byte> reply(Size);
    reply[0] = std::byte{0xC1};
    reply[1] = std::byte{Size};
    reply[2] = std::byte{0xF3};
    reply[3] = std::byte{0x03};
    reply[4] = std::byte{x};
    reply[5] = std::byte{y};
    reply[0x2c] = std::byte{4};
    reply[0x2d] = std::byte{0x20};
    return Client::Tests::FromTheGameServer(reply, serials);
}

// Por la sesión: 0x512a30 corta (hoy en 0x550d10, en su primera llamada, 0x40e1b0) y la sesión falla con el
// héroe sin publicar, el contador sin tocar y los rand() que el original sacó hasta ahí.
TEST_F(WorldEntryTest, AnEquipmentFailureLeavesTheHeroUnpublished)
{
    Entry tested(m_dataRoot, Seed);
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    tested.world.Runtime().warpCounter = Client::Scenes::WorldLoadingWait;
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(133, 127, serials)};

    static_cast<void>(online.session.Poll());

    EXPECT_EQ(online.session.State(), Client::App::SessionState::Failed);
    EXPECT_EQ(tested.world.Runtime().hero, 0u);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait);
    EXPECT_EQ(tested.AliveArrivals(), 0u);
    EXPECT_EQ(tested.Hero().key, PlayerKey);
    EXPECT_EQ(tested.Hero().name, ActorState{}.name);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(
        tested.cloth.random.State(),
        AfterDraws(Seed, 3 * OwnRecordClose + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom).State());
}

// Las tablas de los conjuntos sin ningún conjunto: 0x40e1b0 recorre el equipo y no aplica nada.
Client::Data::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// El lugar del héroe con `Seed`, el de la primera prueba.
std::size_t HeroSlotOfSeed()
{
    CrtRandom replay = AfterDraws(Seed, OwnRecordClose + GrassRandoms);
    return static_cast<std::size_t>(replay.Next() % 400);
}

// Con las tablas de los conjuntos, 0x550d10 llega al final y la entrada sigue hasta asentarse. El lugar del
// héroe no es el publicado (el 0 de WinMain): 0x505820 no corre en 0x512a30, y 0x550990 lee el 0 de la
// .bss, el de un proceso que no dibujó a nadie. Los accesos de adentro anidan en el de 0x512a30: los
// rand() de más son los tres de la cola, y después los de SettleWorldEntry con su cierre.
TEST_F(WorldEntryTest, WithTheSetTablesTheEntryRunsTheWholeRecalculation)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_NE(HeroSlotOfSeed(), tested.world.Runtime().hero);

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().hero, HeroSlotOfSeed());
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(tested.world.Runtime().fullSetScan->byte743a17c, 0);
    constexpr int BeforeTheSettling = GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom + TailRandoms;
    constexpr int TheSettling = OwnRecordClose + WaitClose + WindowRandoms + StopRandom;
    EXPECT_EQ(tested.cloth.random.State(),
              AfterDraws(Seed, 3 * OwnRecordClose + BeforeTheSettling + TheSettling).State());
}

// La respuesta aplicada y, sin ningún paso, el primer frame del juego: el contador en 30 no retiene, la
// cámara mira al héroe publicado y lee su celda (8 rand() después de los 4 de 0x637229), el terreno de
// Lorencia sale con sus tiles y su pasto (0x5fc353, sin rand()), las marcas del suelo no dibujan ninguna
// (0x5fc365: la tabla de la .bss, vacía) y los objetos sacan su rand() hasta el 0x69, donde el dibujo corta
// (0x600a4a), sin presentar.
TEST_F(WorldEntryTest, AZeroStepFrameAfterTheEntryDrawsTheTerrainUpToTheObjects)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    // Lo que dejó la carga del 54 de la selección (0x633f30): el mundo cargado.
    tested.world.Runtime().drawingEnabled = true;
    Client::Tests::FakeTextureStore store;
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = m_dataRoot,
                                                                 .textures = store,
                                                                 .log = tested.log,
                                                                 .cloth = &tested.cloth,
                                                                 .camera = &tested.camera,
                                                                 .cameraInput = &tested.cameraInput,
                                                                 .world = &tested.world},
                                    nullptr);
    ASSERT_TRUE(scene.Enter().HasValue());
    CrtRandom expected = tested.cloth.random;
    for (int draw = 0; draw < 4 + 8 + LorenciaObjectRandoms; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_NE(scene.Failure()->find("0x600a4a"), std::string::npos) << *scene.Failure();
    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_FALSE(target.frames[0].presented);
    std::size_t vertices = 0;
    std::size_t grass = 0;
    for (const auto& event : target.frames[0].events)
    {
        vertices += event.modelVertices.size();
        for (const Client::Render::ModelDrawCommand& command : event.modelCommands)
        {
            grass += command.state.texture == Client::Render::TextureSlot{0x32} ? command.vertexCount : 0;
        }
    }
    EXPECT_GT(vertices, 0u);
    EXPECT_GT(grass, 0u) << "el pasto de World1 (TileGrass01) en 0x32";
    EXPECT_TRUE(scene.MouseRay().has_value());
    EXPECT_EQ(tested.cameraInput.hero, tested.Hero().position);
    EXPECT_EQ(tested.cloth.random.State(), expected.State());
    scene.Leave();
}

// 0x550990 lee lo que dejó el último 0x505820, acá el de la selección: [0x743a17c] en 1 y [0x743a178] en 12
// suman _ftol(+0x62 · 0.15f) a +0x62, que con la clase 0 es la agilidad >> 2 (0x550990).
TEST_F(WorldEntryTest, TheEntryReadsWhatTheSelectionDrawingLeft)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().fullSetScan = Client::Scenes::Scan505820{.dword743a178 = 12, .byte743a17c = 1};
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    Client::Protocol::Character::CharacterSelectResult reply = CableReply();
    reply.agility = 400;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(reply, PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(tested.world.OwnCharacter().word62, 100 + 15);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 12);
}

// Después de un frame del juego que dibuja los personajes MuRet no sabe qué dejó 0x505820: con otro
// lugar, la entrada corta en 0x550990 con el héroe sin publicar y los rand() de hasta ahí, sin la cola.
TEST_F(WorldEntryTest, WithoutKnowingTheLastScanTheEntryStopsAt0x550990)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().fullSetScan = std::nullopt;
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("0x550990"), std::string::npos) << entered.Error();
    EXPECT_EQ(publisher.calls, 0);
    EXPECT_EQ(tested.world.Runtime().hero, 0u);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(
        tested.cloth.random.State(),
        AfterDraws(Seed, 3 * OwnRecordClose + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom).State());
}

// Si rand() % 400 da el lugar del héroe publicado (en una segunda entrada, o el 0 de WinMain), 0x512a30
// viste a ese mismo y 0x505820 corre con él (0x512ee7): lo de antes no importa. El registro vacío deja
// las partes de la clase, que no son un conjunto: [0x743a178] en 0 y [0x743a17c] en 1.
TEST_F(WorldEntryTest, WithThePublishedHeroSlotTheEntryScansTheHero)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().hero = HeroSlotOfSeed();
    tested.world.Runtime().fullSetScan = std::nullopt;
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(publisher.calls, 1);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(tested.world.Runtime().fullSetScan->byte743a17c, 1);
}

// Tres cierres en línea (0x4c41c4, 0x4c46f0 y 0x4c481e) y el de 0x512a30: cada uno saca 3.848 rand()
// solo si deja el registro sin accesos.
TEST_F(WorldEntryTest, TheEntryClosesTheOwnRecordAtItsThreeSites)
{
    constexpr int BeforeTheSettling = GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom;
    Entry closing(m_dataRoot, Seed);
    Entry held(m_dataRoot, Seed);
    held.world.Runtime().ownRecordDepth = 1;
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, closing.Resources(), publisher));
    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, held.Resources(), publisher));

    EXPECT_EQ(closing.cloth.random.State(), AfterDraws(Seed, 3 * OwnRecordClose + BeforeTheSettling).State());
    EXPECT_EQ(held.cloth.random.State(), AfterDraws(Seed, BeforeTheSettling).State());
    EXPECT_EQ(closing.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(held.world.Runtime().ownRecordDepth, 1);

    Entry settling(m_dataRoot, Seed);
    Entry settlingHeld(m_dataRoot, Seed);
    settling.DressedHero(PublishedSlot);
    settlingHeld.DressedHero(PublishedSlot);
    settlingHeld.world.Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(SettleWorldEntry({}, settling.Resources(), publisher).HasValue());
    ASSERT_TRUE(SettleWorldEntry({}, settlingHeld.Resources(), publisher).HasValue());
    constexpr int AfterTheThirdClose = WaitClose + WindowRandoms + StopRandom;
    EXPECT_EQ(settling.cloth.random.State(), AfterDraws(Seed, OwnRecordClose + AfterTheThirdClose).State());
    EXPECT_EQ(settlingHeld.cloth.random.State(), AfterDraws(Seed, AfterTheThirdClose).State());
}

// 06, filas 6 a 17 y "El héroe": lo que ve la sesión al publicar el 0x3d y lo que queda al terminar.
TEST_F(WorldEntryTest, TheSettlingFollowsTheNativeOrder)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().map = 0;
    tested.world.Runtime().warpCounter = Client::Scenes::WorldLoadingWait;
    tested.world.OwnCharacter().name[0] = std::byte{'h'};
    tested.world.OwnCharacter().equipment[3].id = 7;
    ActorState& hero = tested.DressedHero(PublishedSlot);
    RecordingPublisher publisher;
    bool checked = false;
    publisher.onPublish = [&]
    {
        checked = true;
        EXPECT_EQ(tested.world.Runtime().hero, PublishedSlot);
        EXPECT_EQ(hero.name[0], std::byte{'h'});
        EXPECT_EQ(tested.world.OwnCharacter().equipment[3].id, Client::Scenes::EmptyOwnItemId);
        EXPECT_EQ(tested.AliveArrivals(), 1u);
        EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait);
        EXPECT_EQ(hero.alpha, 1.0f);
        EXPECT_EQ(tested.cameraInput.hero, hero.position);
    };

    const auto settled = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_TRUE(settled.HasValue()) << settled.Error();
    EXPECT_TRUE(checked);
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold);
    EXPECT_EQ(tested.AliveArrivals(), 2u);
    EXPECT_EQ(hero.alpha, 0.0f);
    EXPECT_EQ(hero.movement, 0);
    EXPECT_EQ(settled.Value().sounds, (std::vector<std::int32_t>{0x8b, 0x99, 0x9e}));
    EXPECT_EQ(tested.cloth.random.State(),
              AfterDraws(Seed, OwnRecordClose + WaitClose + WindowRandoms + StopRandom).State());
    for (const auto& effect : tested.world.Map().effects.Effects())
    {
        if (effect.alive)
        {
            EXPECT_EQ(effect.transform.position, hero.position);
            EXPECT_EQ(effect.transform.angles, hero.angles);
            EXPECT_EQ(effect.life, 20);
        }
    }
}

// 0x4f2250 corta en un personaje que no es jugador: el contador ya quedó en 30 y los dos efectos de
// llegada están creados.
TEST_F(WorldEntryTest, AStopFailureKeepsCounterThirtyAndTwoArrivals)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().warpCounter = Client::Scenes::WorldLoadingWait;
    ActorState& hero = tested.DressedHero(PublishedSlot);
    hero.type = 0xa9;
    RecordingPublisher publisher;

    const auto settled = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_FALSE(settled.HasValue());
    EXPECT_NE(settled.Error().find("0x4f2250"), std::string::npos) << settled.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold);
    EXPECT_EQ(tested.AliveArrivals(), 2u);
    EXPECT_EQ(tested.world.Runtime().hero, PublishedSlot);
    EXPECT_EQ(hero.alpha, 0.0f);
}

// 0x4c49d6..0x4c4a11: sin el 0x8b en Blood Castle y sin el 0x99 ni el 0x9e en Chaos Castle.
TEST_F(WorldEntryTest, TheSoundsSkipTheEventMaps)
{
    Entry tested(m_dataRoot, Seed);
    tested.DressedHero(PublishedSlot);
    RecordingPublisher publisher;

    tested.world.Runtime().map = 12;
    const auto bloodCastle = SettleWorldEntry({}, tested.Resources(), publisher);
    tested.world.Runtime().map = 20;
    const auto chaosCastle = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_TRUE(bloodCastle.HasValue()) << bloodCastle.Error();
    ASSERT_TRUE(chaosCastle.HasValue()) << chaosCastle.Error();
    EXPECT_EQ(bloodCastle.Value().sounds, (std::vector<std::int32_t>{0x99, 0x9e}));
    EXPECT_EQ(chaosCastle.Value().sounds, (std::vector<std::int32_t>{0x8b}));
}

// 0x420d60: el cuervo del 0x1c17 en la mano izquierda no está portado en el mundo.
TEST_F(WorldEntryTest, ARavenStopsAt420d60AfterTheWait)
{
    Entry tested(m_dataRoot, Seed);
    ActorState& hero = tested.DressedHero(PublishedSlot);
    hero.hands[1].model = 0x1c17;
    RecordingPublisher publisher;

    const auto settled = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_FALSE(settled.HasValue());
    EXPECT_NE(settled.Error().find("0x420d60"), std::string::npos);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold);
    EXPECT_EQ(tested.AliveArrivals(), 1u);
}

// ---- Una segunda respuesta (T13; 06, "La entrada al mundo", "Una segunda respuesta") ----

// Una entrada entera con las tablas de los conjuntos y el registro vacío, en el 0 o en el 2: los cuatro
// cierres del registro, el pasto, el lugar, 0x511290, los dos sufijos de 0x4f2250, la cola de 0x550d10, el
// contador y las ventanas.
constexpr int WholeEntryRandoms = (4 * OwnRecordClose) + GrassRandoms + HeroRandom + ConstructionRandoms +
                                  (2 * StopRandom) + TailRandoms + WaitClose + WindowRandoms;
// Lo que corta 0x550990 sin saber qué dejó 0x505820: el primer y el segundo cierre en línea, el pasto, el
// lugar, 0x511290, el sufijo de 0x4f2250 y el cierre de 0x512a30, sin la cola.
constexpr int EntryUpTo550990 = (3 * OwnRecordClose) + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom;
// Un frame del juego sin pasos: el contador (0x637229) y los dos accesos a la celda del héroe (0x635ae0).
constexpr int FrameRandoms = 4 + 8;
// Del lugar de una entrada al de la siguiente, con un frame del juego de Lorencia en el medio.
constexpr int BetweenTheSlotsWithAFrame = WholeEntryRandoms - (OwnRecordClose + GrassRandoms + HeroRandom) +
                                          FrameRandoms + LorenciaObjectRandoms + OwnRecordClose + GrassRandoms;

// Los mapas de referencia, cada uno con dos celdas.
struct ReferenceMap
{
    std::uint8_t map = 0;
    std::uint8_t firstX = 0;
    std::uint8_t firstY = 0;
    std::uint8_t secondX = 0;
    std::uint8_t secondY = 0;
    // El primer frame del juego con el héroe en la primera celda: los rand() de los objetos y dónde corta.
    int firstObjectRandoms = 0;
    std::string_view firstCut;
};
constexpr ReferenceMap Lorencia{.map = 0,
                                .firstX = 133,
                                .firstY = 127,
                                .secondX = 143,
                                .secondY = 120,
                                .firstObjectRandoms = LorenciaObjectRandoms,
                                .firstCut = "0x600a4a"};
constexpr ReferenceMap Devias{.map = 2,
                              .firstX = 220,
                              .firstY = 30,
                              .secondX = 210,
                              .secondY = 40,
                              .firstObjectRandoms = DeviasObjectRandoms,
                              .firstCut = "0x6374d3"};

// La respuesta del cable en la celda (x, y) de `map`, con `direction`.
Client::Protocol::Character::CharacterSelectResult ReplyAt(std::uint8_t map, std::uint8_t x, std::uint8_t y,
                                                           std::uint8_t direction)
{
    Client::Protocol::Character::CharacterSelectResult reply = CableReply();
    reply.map = map;
    reply.x = x;
    reply.y = y;
    reply.direction = direction;
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

// El lugar que sortea la próxima respuesta desde `random`: el rand() después del primer cierre y el pasto.
std::size_t NextHeroSlot(const CrtRandom& random)
{
    CrtRandom replay = Advanced(random, OwnRecordClose + GrassRandoms);
    return static_cast<std::size_t>(replay.Next() % 400);
}

// La primera semilla con la que las dos entradas de Lorencia, con un frame del juego en el medio, sortean el
// mismo lugar.
std::uint32_t SeedOfTheSameSlotAfterAFrame()
{
    return Client::Tests::FirstSeedWhere(
        [](CrtRandom& random)
        {
            random = Advanced(random, OwnRecordClose + GrassRandoms);
            const int first = random.Next() % 400;
            random = Advanced(random, BetweenTheSlotsWithAFrame);
            return random.Next() % 400 == first;
        });
}

std::size_t AliveActors(const Entry& tested)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(tested.world.Actors(), [](const ActorState& actor) { return actor.alive; }));
}

// Un frame del juego sin pasos, como AZeroStepFrameAfterTheEntryDrawsTheTerrainUpToTheObjects: en el
// original llega a los personajes (0x6374dd → 0x510d00), y MuRet, que corta en los objetos (el 0x69 de
// Lorencia, 0x600a4a), ya no sabe qué dejó 0x505820.
void DrawAGameFrame(Entry& tested)
{
    tested.world.Runtime().drawingEnabled = true;
    Client::Tests::FakeTextureStore store;
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = tested.dataRoot,
                                                                 .textures = store,
                                                                 .log = tested.log,
                                                                 .cloth = &tested.cloth,
                                                                 .camera = &tested.camera,
                                                                 .cameraInput = &tested.cameraInput,
                                                                 .world = &tested.world},
                                    nullptr);
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    const CrtRandom before = tested.cloth.random;

    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_TRUE(scene.Failure().has_value());
    ASSERT_NE(scene.Failure()->find(Lorencia.firstCut), std::string::npos) << *scene.Failure();
    ASSERT_FALSE(tested.world.Runtime().fullSetScan.has_value());
    ASSERT_EQ(tested.cloth.random.State(), Advanced(before, FrameRandoms + LorenciaObjectRandoms).State());
    scene.Leave();
}

// Con el héroe en el mundo, otro F3 03 al mismo mapa, en Lorencia y en Devias: 0x61a560 lo vuelve a cargar
// sin compararlo con el cargado (los objetos otra vez, no además; el pasto con 65.536 rand() nuevos; de los
// efectos de llegada, solo los dos de esta), el lugar se sortea de nuevo después del pasto y el héroe nuevo
// se construye y se publica ahí. El anterior tiene la clave del jugador: 0x510f70 no lo mata y queda como
// estaba, vivo en su lugar (Q-C196). Los rand() son los de la primera.
TEST_F(WorldEntryTest, ASecondEntryReloadsAndRedrawsTheHeroSlot)
{
    for (const ReferenceMap& probe : {Lorencia, Devias})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Entry tested(m_dataRoot, Seed);
        const Client::Data::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        RecordingPublisher publisher;
        const auto first =
            ApplyWorldEntry(ReplyAt(probe.map, probe.firstX, probe.firstY, 0), PlayerKey, resources, publisher);
        ASSERT_TRUE(first.HasValue()) << first.Error();
        const std::size_t firstSlot = tested.world.Runtime().hero;
        const ActorState previousBefore = tested.world.Actors()[firstSlot];
        const std::size_t objects = tested.world.Map().objects.size();
        const CrtRandom afterFirst = tested.cloth.random;
        const std::size_t secondSlot = NextHeroSlot(afterFirst);
        ASSERT_NE(secondSlot, firstSlot);

        const auto second =
            ApplyWorldEntry(ReplyAt(probe.map, probe.secondX, probe.secondY, 3), PlayerKey, resources, publisher);

        ASSERT_TRUE(second.HasValue()) << second.Error();
        EXPECT_EQ(publisher.calls, 2);
        EXPECT_EQ(tested.world.Runtime().map, probe.map);
        EXPECT_EQ(tested.world.Runtime().heroSlot, static_cast<std::int32_t>(secondSlot));
        EXPECT_EQ(tested.world.Runtime().hero, secondSlot);
        const ActorState& hero = tested.world.Actors()[secondSlot];
        EXPECT_TRUE(hero.alive);
        EXPECT_EQ(hero.key, PlayerKey);
        EXPECT_EQ(hero.position.x, (static_cast<float>(probe.secondX) * 100.0f) + 50.0f);
        EXPECT_EQ(hero.position.y, (static_cast<float>(probe.secondY) * 100.0f) + 50.0f);
        EXPECT_EQ(hero.angles.z, 90.0f);
        EXPECT_EQ(hero.alpha, 0.0f);
        EXPECT_EQ(hero.revision, 1u);
        EXPECT_EQ(tested.cameraInput.hero, hero.position);
        const ActorState& previous = tested.world.Actors()[firstSlot];
        EXPECT_TRUE(previous.alive);
        EXPECT_EQ(previous.key, PlayerKey);
        EXPECT_EQ(previous.cellX, probe.firstX);
        EXPECT_EQ(previous.cellY, probe.firstY);
        EXPECT_EQ(previous.position, previousBefore.position);
        EXPECT_EQ(previous.revision, previousBefore.revision);
        EXPECT_EQ(AliveActors(tested), 2u);
        EXPECT_EQ(tested.world.Map().objects.size(), objects);
        EXPECT_EQ(tested.AliveArrivals(), 2u);
        CrtRandom grass = Advanced(afterFirst, OwnRecordClose);
        EXPECT_EQ(tested.world.Map().grassOffsets.front(), static_cast<float>(grass.Next() % 4) * 0.25f);
        EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold);
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(tested.cloth.random.State(), Advanced(afterFirst, WholeEntryRandoms).State());
    }
}

// La segunda respuesta en el otro mapa de referencia, de Lorencia a Devias y al revés: el terreno, los
// objetos (en Devias, los especiales 0x201 y 0x202 primero) y la cámara son los del mapa nuevo, y el héroe
// anterior sigue vivo con la posición del de antes. Los objetos del 0 y del 2 no sacan rand(): la cuenta es
// la de una entrada al mismo mapa.
TEST_F(WorldEntryTest, ASecondEntryToTheOtherReferenceMapLoadsItAndKeepsThePreviousHero)
{
    struct MapChange
    {
        ReferenceMap from;
        ReferenceMap to;
    };
    for (const MapChange& change : {MapChange{Lorencia, Devias}, MapChange{Devias, Lorencia}})
    {
        const ReferenceMap& from = change.from;
        const ReferenceMap& to = change.to;
        SCOPED_TRACE(static_cast<int>(to.map));
        Entry tested(m_dataRoot, Seed);
        const Client::Data::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        RecordingPublisher publisher;
        ASSERT_TRUE(ApplyWorldEntry(ReplyAt(from.map, from.firstX, from.firstY, 0), PlayerKey, resources, publisher)
                        .HasValue());
        const std::size_t firstSlot = tested.world.Runtime().hero;
        const ActorState previousBefore = tested.world.Actors()[firstSlot];
        const CrtRandom afterFirst = tested.cloth.random;
        const std::size_t secondSlot = NextHeroSlot(afterFirst);
        ASSERT_NE(secondSlot, firstSlot);

        const auto second = ApplyWorldEntry(ReplyAt(to.map, to.firstX, to.firstY, 0), PlayerKey, resources, publisher);

        ASSERT_TRUE(second.HasValue()) << second.Error();
        EXPECT_EQ(tested.world.Runtime().map, to.map);
        EXPECT_EQ(tested.cameraInput.map, to.map);
        const auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, to.map);
        ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
        ASSERT_TRUE(tested.world.Map().terrain.attributes);
        EXPECT_EQ(tested.world.Map().terrain.attributes->cells, terrain.Value().attributes->cells);
        ASSERT_FALSE(tested.world.Map().objects.empty());
        EXPECT_EQ(tested.world.Map().objects.front().type == 0x201, to.map == Devias.map);
        EXPECT_EQ(tested.world.Runtime().hero, secondSlot);
        const ActorState& hero = tested.world.Actors()[secondSlot];
        EXPECT_EQ(hero.position.x, (static_cast<float>(to.firstX) * 100.0f) + 50.0f);
        EXPECT_EQ(hero.position.y, (static_cast<float>(to.firstY) * 100.0f) + 50.0f);
        const ActorState& previous = tested.world.Actors()[firstSlot];
        EXPECT_TRUE(previous.alive);
        EXPECT_EQ(previous.position, previousBefore.position);
        EXPECT_EQ(tested.AliveArrivals(), 2u);
        EXPECT_EQ(tested.cloth.random.State(), Advanced(afterFirst, WholeEntryRandoms).State());
    }
}

// Q-C197: mientras 0x512a30 viste al héroe nuevo, el publicado (0x743a170) es el anterior, y 0x40d010 le
// pone a él el +0x358 en 0 (0x40d07d). El nuevo lo tiene en 0 desde 0x511290 (0x511c99).
TEST_F(WorldEntryTest, TheSecondEntryRecalculatesThe358OfThePreviousHero)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    ActorState& previous = tested.world.Actors()[tested.world.Runtime().hero];
    previous.byte358 = 1;
    const std::size_t secondSlot = NextHeroSlot(tested.cloth.random);
    ASSERT_NE(secondSlot, tested.world.Runtime().hero);
    tested.world.Actors()[secondSlot].byte358 = 1;

    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());

    EXPECT_EQ(previous.byte358, 0);
    EXPECT_EQ(tested.world.Actors()[secondSlot].byte358, 0);
}

// Después de un frame del juego, una segunda respuesta en otro lugar viste al nuevo con el anterior
// publicado: 0x505820 no corre y 0x550990 lee lo que dejó el dibujo de los personajes del frame, que MuRet
// no tiene. Corta ahí (P-C40) con el anterior todavía publicado y el nuevo construido, los dos vivos, y el
// contador como estaba.
TEST_F(WorldEntryTest, AfterAGameFrameASecondEntryElsewhereStopsAt0x550990)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    const std::size_t firstSlot = tested.world.Runtime().hero;
    ASSERT_NO_FATAL_FAILURE(DrawAGameFrame(tested));
    // Como si hubieran corrido pasos (0x59af01).
    tested.world.Runtime().warpCounter = 7;
    const CrtRandom afterTheFrame = tested.cloth.random;
    const std::size_t secondSlot = NextHeroSlot(afterTheFrame);
    ASSERT_NE(secondSlot, firstSlot);

    const auto second = ApplyWorldEntry(ReplyAt(0, 143, 120, 3), PlayerKey, resources, publisher);

    ASSERT_FALSE(second.HasValue());
    EXPECT_NE(second.Error().find("0x550990"), std::string::npos) << second.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().hero, firstSlot);
    EXPECT_EQ(tested.world.Runtime().heroSlot, static_cast<std::int32_t>(secondSlot));
    EXPECT_TRUE(tested.world.Actors()[firstSlot].alive);
    EXPECT_TRUE(tested.world.Actors()[secondSlot].alive);
    EXPECT_EQ(tested.world.Actors()[secondSlot].key, PlayerKey);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 7);
    EXPECT_FALSE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), Advanced(afterTheFrame, EntryUpTo550990).State());
}

// Con la semilla que sortea dos veces el mismo lugar, la segunda respuesta reconstruye al héroe publicado en
// su registro: 0x512a30 viste al publicado, 0x505820 corre con él (0x512ee7) y deja lo suyo, y 0x550990 lo
// lee aunque un frame del juego haya borrado lo de antes. Las partes de la clase no son un conjunto:
// [0x743a178] en 0 y [0x743a17c] en 1.
TEST_F(WorldEntryTest, AfterAGameFrameASecondEntryOnTheSameSlotScansTheRebuiltHero)
{
    const std::uint32_t seed = SeedOfTheSameSlotAfterAFrame();
    Entry tested(m_dataRoot, seed);
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    const std::size_t slot = tested.world.Runtime().hero;
    ASSERT_NO_FATAL_FAILURE(DrawAGameFrame(tested));
    const CrtRandom afterTheFrame = tested.cloth.random;
    ASSERT_EQ(NextHeroSlot(afterTheFrame), slot);

    const auto second = ApplyWorldEntry(ReplyAt(0, 143, 120, 3), PlayerKey, resources, publisher);

    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(publisher.calls, 2);
    EXPECT_EQ(tested.world.Runtime().hero, slot);
    const ActorState& hero = tested.world.Actors()[slot];
    EXPECT_EQ(hero.revision, 2u);
    EXPECT_EQ(hero.cellX, 143);
    EXPECT_EQ(hero.cellY, 120);
    EXPECT_EQ(AliveActors(tested), 1u);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(tested.world.Runtime().fullSetScan->byte743a17c, 1);
    EXPECT_EQ(tested.cloth.random.State(), Advanced(afterTheFrame, WholeEntryRandoms).State());
}

// Por la sesión: con el 0x3d ya publicado, otro F3 03 cifrado va igual a 0x4c3940 (0x4e306b no mira el
// estado ni la escena) y vuelve a publicar el 0x3d, con el héroe nuevo.
TEST_F(WorldEntryTest, AnEntryInTheWorldIsAppliedAgainByTheSession)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Data::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    Client::Scenes::SessionEventApplier applier(resources);
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(133, 127, serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    const std::size_t firstSlot = tested.world.Runtime().hero;
    const std::size_t secondSlot = NextHeroSlot(tested.cloth.random);
    ASSERT_NE(secondSlot, firstSlot);

    online.socket.reads = {EntryPacket(143, 120, serials)};
    static_cast<void>(online.session.Poll());

    EXPECT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().hero, secondSlot);
    EXPECT_EQ(tested.Hero().cellX, 143);
    EXPECT_TRUE(tested.world.Actors()[firstSlot].alive);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold);
}

// T11b, de punta a punta: el dibujo de la selección (0x63475c) deja en los globales el trapecio de su punto fijo;
// la entrada al mundo (F3 03) no lo toca, y el primer paso del juego después de la espera lo lee en 0x508f00
// (0x63710f). En Lorencia el héroe de (133, 127) cae adentro y se ve; en Devias el de (220, 30) cae afuera
// (Q-C216). Los rand() del paso: los 8 de MoveHero, los 8 del TerrainWall, el cuerpo del héroe (0x4fd5f0: el
// cierre del registro propio en 0x4fc450 y el rand() % 6 de 0x4fd7e4) y, con el héroe visto, los 8 de su celda
// lógica y el rand() & 7; después, la tela. El frame que sigue pasa las marcas del suelo (0x5fc365), que no sacan
// rand(), y los objetos, con sus 4 + 8 y uno por objeto visto: Lorencia corta en el 0x69 (0x600a4a) y Devias en
// los efectos del suelo (0x6374d3).
TEST_F(WorldEntryTest, TheFirstGameStepReadsTheTrapezoidOfTheLastSelectionDraw)
{
    const Client::World::TerrainPolygon selected = Client::World::TerrainPolygonOf(
        Client::World::CullingScene::CharacterSelect, 54, Client::Scenes::SelectionCullingPoint,
        Client::Scenes::SelectionCamera().angles.z);
    for (const ReferenceMap& probe : {Lorencia, Devias})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        const bool seen = probe.map == Lorencia.map;
        Entry tested(m_dataRoot, Seed);
        Client::Tests::FakeTextureStore store;
        const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                   .textures = store,
                                                   .log = tested.log,
                                                   .cloth = &tested.cloth,
                                                   .camera = &tested.camera,
                                                   .cameraInput = &tested.cameraInput,
                                                   .world = &tested.world};
        const Client::Scenes::GameData data;
        Client::Scenes::CharacterSelectScene selection(context, data, tested.models, nullptr);
        ASSERT_TRUE(selection.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;
        selection.Render(target.BeginFrame({}), 0.0f);
        target.Present();
        ASSERT_EQ(selection.Failure(), std::nullopt);
        selection.Leave();
        ASSERT_EQ(tested.world.Runtime().terrainPolygon.x, selected.x);
        ASSERT_EQ(tested.world.Runtime().terrainPolygon.y, selected.y);
        const Client::Data::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        RecordingPublisher publisher;
        const auto entered =
            ApplyWorldEntry(ReplyAt(probe.map, probe.firstX, probe.firstY, 0), PlayerKey, resources, publisher);
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        EXPECT_EQ(tested.world.Runtime().terrainPolygon.x, selected.x) << "la entrada no toca el trapecio";
        EXPECT_EQ(tested.world.Runtime().terrainPolygon.y, selected.y);
        // Lo que dejó la carga del 54 de la selección (0x633f30): el mundo cargado.
        tested.world.Runtime().drawingEnabled = true;
        Client::Scenes::GameScene game(context, nullptr, &tested.models);
        ASSERT_TRUE(game.Enter().HasValue());
        Client::Render::Models::ClothWorld reference = tested.cloth;
        const int body = OwnRecordClose + 1;
        reference.random = Advanced(tested.cloth.random, seen ? 8 + 8 + body + 8 + 1 : 8 + 8 + body);
        Client::Render::Models::StepTheClothWorld(reference);

        game.Step(Client::Input::InputState{});

        ASSERT_EQ(game.Failure(), std::nullopt);
        EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Scenes::WaitDrawThreshold - 1);
        EXPECT_EQ(tested.Hero().seen, seen);
        EXPECT_EQ(tested.cloth.random.State(), reference.random.State());

        game.PrepareFrame();
        ASSERT_FALSE(game.HoldsTheLastFrame());
        game.Render(target.BeginFrame({}), 0.0f);

        ASSERT_TRUE(game.Failure().has_value());
        EXPECT_NE(game.Failure()->find(probe.firstCut), std::string::npos) << *game.Failure();
        EXPECT_EQ(tested.cloth.random.State(),
                  Advanced(reference.random, FrameRandoms + probe.firstObjectRandoms).State());
        game.Leave();
    }
}
} // namespace
