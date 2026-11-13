#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Combat.h"
#include "Protocol/Movement.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActionReply.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorDying.h"
#include "Scenes/ActorState.h"
#include "Scenes/AttackStep.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/CombatReplies.h"
#include "Scenes/MonsterSounds.h"
#include "Scenes/MonsterViewport.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Los monstruos del 0x13 en combate (CL9 hito 3, fase 2c): el alta escribe la tabla de sonidos de su modelo (0x612d90),
// el 0x18 con la 0x78 los hace pegar (0x4f4a10 con sus ayudantes) y contar en el paso (0x4fd5f0 con 0x4f97b0 y
// 0x4f5b70), y el D7, el 0x16 y el 0x17 los lastiman y los matan (0x4f5660 y 0x4fb9e0). Con el Data del laboratorio
// (Lorencia y Devias); las claves son inventadas y el esqueleto del monstruo es el del 0x20a (MuRet no carga los
// modelos de los monstruos, P-C176).
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::ModelSounds;
using Client::Tests::MonsterRecordBytes;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr std::int32_t HeroSlot = 5;
constexpr std::uint16_t MonsterKey = 0x0123;
// El tipo 3 de Lorencia: Monster10.bmd (0x612d90(9)) en el modelo 0x15b.
constexpr std::uint8_t LorenciaType = 3;
constexpr std::uint16_t LorenciaModel = 0x15b;

int DrawsSince(std::uint32_t state, const CrtRandom& random)
{
    constexpr int Limit = 100'000;
    CrtRandom probe(state);
    int draws = 0;
    while (probe.State() != random.State() && draws < Limit)
    {
        static_cast<void>(probe.Next());
        ++draws;
    }
    return draws;
}

// Los esqueletos de 0x4ea030: el de Player.bmd también para los monstruos.
class MonsterModels final : public Client::Scenes::IModelLibrary
{
public:
    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == PlayerSkeletonModel || model == LorenciaModel ? &skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

// El mundo del juego, en el heap: WorldState pesa unos 350 KB.
struct Game
{
    Game()
    {
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        world.Runtime().hero = 0;
        world.Runtime().heroSlot = HeroSlot;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = dataRoot,
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log};
    }
    Client::Scenes::CharacterStepContext Step()
    {
        return {.world = world, .random = cloth.random, .playerSkeleton = &models.skeleton, .models = &models};
    }
    Client::Scenes::WorldState world;
    std::filesystem::path dataRoot;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    MonsterModels models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class MonsterCombatTest : public Client::Tests::GoldenDataTest
{
protected:
    void Load(std::uint8_t map)
    {
        game = std::make_unique<Game>();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        game->dataRoot = m_dataRoot;
    }

    // Da de alta un monstruo del tipo `type` en la primera celda libre desde (100, 100) y devuelve su lugar.
    std::size_t Create(std::uint8_t type)
    {
        const auto& cells = game->world.Map().terrain.attributes->cells;
        std::uint8_t x = 100;
        std::uint8_t y = 100;
        while (cells[Client::Data::Terrain::CellIndex(x, y)] != 0)
            ++x;
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = MonsterKey, .typeHigh = 0x00, .typeLow = type, .x = x, .y = y, .targetX = x, .targetY = y}}};
        auto read = Client::Protocol::Viewport::ReadMonsters(Client::Tests::MonstersPacket(records));
        EXPECT_TRUE(read.HasValue());
        const auto applied = Client::Scenes::ApplyViewportMonsters(read.Value(), game->Resources());
        EXPECT_TRUE(applied.HasValue()) << (applied.HasValue() ? "" : applied.Error());
        for (std::size_t slot = 0; slot < game->world.Actors().size(); ++slot)
        {
            if (game->world.Actors()[slot].alive && game->world.Actors()[slot].key == MonsterKey)
                return slot;
        }
        ADD_FAILURE() << "el monstruo no está en el pool";
        return 0;
    }

    std::unique_ptr<Game> game;
};

ModelSounds Sounds(std::int16_t first, std::int16_t second, std::int16_t third, std::int16_t fourth, std::int16_t fifth)
{
    return {first, second, third, fourth, fifth, -1, -1, -1, -1, -1};
}

// El alta (0x612d90 desde 0x5141e0) escribe la tabla de sonidos del modelo: el 3 de Lorencia la del caso 9 (0x15b) y
// el 19 de Devias, el caso 0xc, las de los dos modelos del yeti (0x15e y 0x15f).
TEST_F(MonsterCombatTest, TheCreationWritesTheModelSounds)
{
    Load(0);
    static_cast<void>(Create(LorenciaType));
    const auto& lorencia = game->world.Runtime().modelSounds;
    ASSERT_EQ(lorencia.size(), 1u);
    EXPECT_EQ(lorencia.at(LorenciaModel), Sounds(0xd, 0xd, 0xd, 0xd, 0xd));

    Load(2);
    static_cast<void>(Create(19));
    const auto& devias = game->world.Runtime().modelSounds;
    ASSERT_EQ(devias.size(), 2u);
    EXPECT_EQ(devias.at(0x15e), Sounds(0x44, 0x44, 0x46, 0x46, 0x47));
    EXPECT_EQ(devias.at(0x15f), Sounds(0x44, 0x45, 0x46, 0x46, 0x47));
}

// El 0x18 con la 0x78: 0x4f4a10 sin ayudantes (Lorencia) pone la 3, cuenta +0x38f y pide el sonido de pegar de su
// tabla (rand() % 2 entre la WORD 2 y la 3, más 0xd2); después +0x381 en 1. El paso del ataque cuenta como el de un
// jugador y, en el frame 5, corre la habilidad sobre el héroe, con el sonido de las manos del registro propio.
TEST_F(MonsterCombatTest, AMonsterAttacksAndStepsItsAttack)
{
    Load(0);
    const std::size_t slot = Create(LorenciaType);
    ActorState& monster = game->world.Actors()[slot];
    const std::uint32_t created = game->cloth.random.State();

    const Client::Protocol::Movement::Action attack{.key = MonsterKey, .direction = 0, .action = 0x78};
    const auto attacked = Client::Scenes::ApplyActionReply(attack, game->Resources(), {});
    ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
    EXPECT_EQ(monster.animation.action, 3);
    EXPECT_EQ(monster.attackCount, 2);
    EXPECT_EQ(monster.byte381, 1);
    EXPECT_EQ(game->world.Runtime().lastSoundId, std::optional<std::int32_t>(0xd + 0xd2));
    EXPECT_EQ(DrawsSince(created, game->cloth.random), 1);

    monster.attackTarget = 0;
    const std::uint32_t before = game->cloth.random.State();
    std::vector<std::int32_t> sounds;
    Client::Scenes::CharacterStepContext context = game->Step();
    context.soundRequest = [&sounds](std::int32_t sound) { sounds.push_back(sound); };
    for (int step = 1; step < 4; ++step)
    {
        const auto stepped = Client::Scenes::StepTheAttack(monster, slot, context);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, step + 1);
    }
    // Cada paso: la lectura de 0x4f97b0, el rand() de 0x4f5b97 y las dos lecturas de 0x4f811b y 0x4f8a2c.
    EXPECT_EQ(DrawsSince(before, game->cloth.random), 3 * 4);

    monster.animation.frame = 5.0f;
    const auto struck = Client::Scenes::StepTheAttack(monster, slot, context);
    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(monster.byte381, 0);
    // El paso con el registro propio (4 + 3.848) y la habilidad con blanco (3.853).
    EXPECT_EQ(DrawsSince(before, game->cloth.random), 3 * 4 + 4 + 0xf08 + 0xf08 + 5);
    EXPECT_EQ(sounds.size(), 1u);
}

// El D7, el 0x16 con el bit 15 y el 0x17 sobre el monstruo: el daño con el golpe (0x4f53a0, el sonido de su tabla),
// 0x4f5660 (la acción de morir y el sonido de la WORD 4) y el que se muere, que 0x4fb9e0 lleva hasta borrarlo.
TEST_F(MonsterCombatTest, AMonsterIsHurtKilledAndDies)
{
    Load(0);
    const std::size_t slot = Create(LorenciaType);
    ActorState& monster = game->world.Actors()[slot];

    const Client::Protocol::Movement::Damage hurt{
        .characterId = MonsterKey, .shock = false, .amount = 12, .type = 0, .flag = false};
    const std::uint32_t created = game->cloth.random.State();
    const auto damaged = Client::Scenes::ApplyDamageReply(hurt, HeroKey, game->Resources());
    ASSERT_TRUE(damaged.HasValue()) << damaged.Error();
    // 0x4f53a0: la acción 5 y, con el frame en 0, el golpe de la WORD 2 o la 3 de su tabla (0x4f553d..0x4f55ac).
    EXPECT_EQ(monster.animation.action, 5);
    EXPECT_EQ(game->world.Runtime().lastSoundId, std::optional<std::int32_t>(0xd + 0xd2));
    EXPECT_GE(DrawsSince(created, game->cloth.random), 1);
    game->world.Runtime().lastSoundId.reset();

    Client::Scenes::ChatReceiveContext chat{.world = game->world, .random = game->cloth.random};
    const Client::Protocol::Combat::KillExperience kill{.key = MonsterKey, .shock = true, .amount = 0, .damage = 12};
    const auto killed =
        Client::Scenes::ApplyKillExperience(kill, Client::Scenes::CombatContextOf(game->Resources()), chat);
    ASSERT_TRUE(killed.HasValue()) << killed.Error();
    EXPECT_EQ(game->world.Runtime().lastSoundId, std::optional<std::int32_t>(0xd + 0xd2));

    const Client::Protocol::Combat::Death death{.key = MonsterKey, .skill = 0, .killer = HeroKey};
    const auto died = Client::Scenes::ApplyDeath(death, Client::Scenes::CombatContextOf(game->Resources()));
    ASSERT_TRUE(died.HasValue()) << died.Error();
    EXPECT_NE(monster.dying, 0);

    int steps = 0;
    while (monster.alive && steps < 200)
    {
        const auto stepped = Client::Scenes::StepActorDying(
            monster, slot, {.map = 0, .scene = Client::Scenes::SceneId::Game, .random = game->cloth.random});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ++steps;
    }
    EXPECT_FALSE(monster.alive);
    EXPECT_GT(steps, 50);
}
} // namespace
