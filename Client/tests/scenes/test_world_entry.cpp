#include "Scenes/WorldEntry.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameScene.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"

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

// Por la sesión: 0x512a30 corta (hoy en 0x550d10, en su primera llamada, 0x40e1b0) y la sesión falla con el
// héroe sin publicar, el contador sin tocar y los rand() que el original sacó hasta ahí.
TEST_F(WorldEntryTest, AnEquipmentFailureLeavesTheHeroUnpublished)
{
    Entry tested(m_dataRoot, Seed);
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    tested.world.Runtime().warpCounter = Client::Scenes::WorldLoadingWait;
    std::vector<std::byte> reply(0x34);
    for (const auto& [at, value] : {std::pair{0, 0xC1}, std::pair{1, 0x34}, std::pair{2, 0xF3}, std::pair{3, 0x03},
                                    std::pair{4, 133}, std::pair{5, 127}, std::pair{0x2c, 4}, std::pair{0x2d, 0x20}})
    {
        reply[static_cast<std::size_t>(at)] = static_cast<std::byte>(value);
    }
    Client::Network::PacketSerials serials;
    online.socket.reads = {Client::Tests::FromTheGameServer(reply, serials)};

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
// Lorencia sale con sus tiles y el dibujo corta en el pase del pasto (0x5fc353), sin presentar.
TEST_F(WorldEntryTest, AZeroStepFrameAfterTheEntryDrawsTheTerrainUpToTheGrass)
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
    for (int draw = 0; draw < 4 + 8; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_NE(scene.Failure()->find("0x5fc353"), std::string::npos) << *scene.Failure();
    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_FALSE(target.frames[0].presented);
    std::size_t vertices = 0;
    for (const auto& event : target.frames[0].events)
    {
        vertices += event.modelVertices.size();
    }
    EXPECT_GT(vertices, 0u);
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
} // namespace
