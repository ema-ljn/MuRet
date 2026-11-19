#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/MonsterAttackStep.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/Particles.h"
#include "World/ModelEffects.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::StepTheAttackCases: los casos de los monstruos de 0x4f5b70 (CL9 hito 3, fase 2e-B) sobre monstruos
// del alta (0x5141e0) con el Data del repo: los ayudantes de Kalima (24 y 36), del asedio (30), del 33 y el 54, del 35
// y de Crywolf (34), el switch por +0x376 y los dos de la cola (0x11 y 3), con los mismos tipos en Lorencia (0) y
// Devias (2). Las claves son inventadas; el alta carga los modelos del Data del repo (0x612d90, CL9 hito 2) con una
// tabla de texturas falsa, y los que no carga toman el esqueleto del 0x20a de la biblioteca del mapa. 06, "Los casos de
// los monstruos (0x4f5b70)".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;

constexpr std::uint32_t Seed = 0x0c19;
constexpr std::int32_t MonsterKey = 0x0456;
// Una lectura de +0x38e (0x517630 y sus copias en línea): un rand() al cerrarse en 0.
constexpr int SkillRead = 1;
constexpr int OwnRecord = Client::Render::Models::OwnRecordCloseRandoms;
// La cola con el blanco en el pool, sin 0x11 ni 3 en +0x38e: las lecturas de 0x4f811b y 0x4f8a2c.
constexpr int Tail = 2 * SkillRead;
// '2' en +0x38e, la habilidad de los casos.
constexpr std::uint8_t CaseSkill = 0x32;
constexpr std::uint16_t NoTarget = 0xffff;
// Un lugar del héroe lejos del monstruo del alta (el primero libre del pool).
constexpr std::size_t HeroAway = 399;
// El alta del 0xf5 (0x5192ed): la escala, tres rand() del punto y uno de la caída antes del joint 0x618/5, que corta
// (0x5193d6, lote joints de la CL11).
constexpr int BlastDraws = 5;
// 0x41ed10(0x7a) del hijo 0x105/1 de cada 0x106/1 (0x51a08f).
constexpr std::int32_t StabSound = 0x7a;

// El estallido de 0x51fff0 (ocho 0x40) y los cinco 0x106/1 de 0x4f79db, cada uno con el sonido de su hijo.
std::vector<std::int32_t> FiveStabSounds()
{
    std::vector<std::int32_t> sounds(8, 0x40);
    sounds.insert(sounds.end(), 5, StabSound);
    return sounds;
}

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

std::uint32_t SeedForSecondRandomParity(bool odd)
{
    for (std::uint32_t seed = 1; seed < 1000; ++seed)
    {
        CrtRandom probe(seed);
        static_cast<void>(probe.Next());
        if (((probe.Next() & 1) != 0) == odd)
            return seed;
    }
    return Seed;
}

// El esqueleto del 0x20a para los modelos que el alta no carga (la biblioteca del mapa, detrás de FaunaResources).
class AnyModels final : public Client::Scenes::IModelLibrary
{
public:
    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t /*model*/) const override
    {
        return &skeleton;
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
    // 0x612d90 sube las texturas de los modelos que carga por la tabla que liga el juego (CL9 hito 2).
    Game()
    {
        world.FaunaResources().TextureTable().Bind(store);
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
    // Antes que el mundo: FaunaModels suelta sus texturas al destruirse.
    Client::Tests::FakeTextureStore store;
    Client::Scenes::WorldState world;
    std::filesystem::path dataRoot;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    AnyModels models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::World::Particles particles;
    std::vector<Client::Scenes::JointState> joints{};
};

// Un monstruo del alta con +0x381, la acción (+0x111), +0x38e y el blanco (+0x3a4), y lo que hace 0x4f5b70 sin su
// rand(): el corte (el prefijo del error, vacío sin corte), los rand() y los sonidos. `kind` y `model` pisan el +0x376
// y el tipo de los que el alta corta (los joints 0x618 de los tipos 0x35 a 0x3f, CL11).
struct Probe
{
    std::string_view name{};
    std::int32_t type = 0;
    std::uint8_t step = 0;
    std::uint8_t action = 3;
    std::uint8_t skill = 0;
    std::string_view cut{};
    int draws = 0;
    std::vector<std::int32_t> sounds{};
    std::uint16_t target = 0;
    std::optional<std::uint16_t> kind{};
    std::optional<std::uint16_t> model{};
    std::optional<std::uint16_t> right{};
    std::optional<std::uint8_t> bone{};
    // Los lugares libres de la tabla de joints (0x74a8d78) del paso: los 0x106/1 del lote switch (CL11) piden los
    // joints 0x610/0..3 de su hijo 0x105/1, y el 0xf5 el 0x618/5. Sin tabla, el paso corta ahí.
    std::size_t joints = 0;
};

// Los lugares de la tabla de joints del juego (GameDrawTailState::joints).
constexpr std::size_t JointTable = 500;

class MonsterAttackCasesTest : public Client::Tests::GoldenDataTest
{
protected:
    // El terreno de `terrain` con el mapa en `map`: el Data del repo no tiene el World36 del 35.
    void Load(std::uint8_t map, std::uint8_t terrain)
    {
        game = std::make_unique<Game>();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, terrain);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        game->dataRoot = m_dataRoot;
    }
    void Load(std::uint8_t map)
    {
        Load(map, map);
    }

    // El alta de `type` en (100, 100), solo en el pool.
    ActorState& Create(std::int32_t type)
    {
        for (ActorState& actor : game->world.Actors())
            actor.alive = false;
        const auto created = Client::Scenes::CreateMonster(type, 100, 100, MonsterKey, game->Resources());
        EXPECT_TRUE(created.HasValue()) << (created.HasValue() ? "" : created.Error());
        slot = created.HasValue() ? created.Value() : 0;
        return game->world.Actors()[slot];
    }

    // El monstruo de `probe`, listo para el paso.
    ActorState& Prepare(const Probe& probe)
    {
        ActorState& monster = Create(probe.type);
        if (probe.kind)
            monster.word376 = *probe.kind;
        if (probe.model)
            monster.type = *probe.model;
        if (probe.right)
            monster.hands[0].model = *probe.right;
        if (probe.bone)
            monster.hands[0].bone = *probe.bone;
        monster.byte381 = probe.step;
        monster.animation.action = probe.action;
        monster.byte38e = probe.skill;
        monster.byte38eAccesses = 0;
        monster.attackTarget = probe.target;
        game->joints.assign(probe.joints, Client::Scenes::JointState{});
        return monster;
    }

    Client::Core::Expected<void, std::string> Step(ActorState& monster)
    {
        sounds.clear();
        soundDraws.clear();
        before = game->cloth.random.State();
        const Client::Scenes::CharacterStepContext context{.world = game->world,
                                                           .random = game->cloth.random,
                                                           .models = &game->models,
                                                           .particles = withParticles ? &game->particles : nullptr,
                                                           .joints = game->joints,
                                                           .soundRequest = [this](std::int32_t sound)
                                                           {
                                                               sounds.push_back(sound);
                                                               soundDraws.push_back(Draws());
                                                           }};
        return Client::Scenes::StepTheAttackCases(monster, slot, context);
    }

    [[nodiscard]] int Draws() const
    {
        return DrawsSince(before, game->cloth.random);
    }

    void FillModelEffectPoolExceptLast(std::size_t freeSlots)
    {
        auto effects = game->world.Map().effects.Effects();
        for (Client::World::ModelEffect& effect : effects)
            effect.alive = true;
        for (std::size_t effectSlot = 0; effectSlot < freeSlots; ++effectSlot)
            effects[effects.size() - effectSlot - 1].alive = false;
    }

    void SaturatePools()
    {
        FillModelEffectPoolExceptLast(0);
        for (Client::World::Particle& particle : game->particles.All())
            particle.alive = true;
        game->joints.assign(1, Client::Scenes::JointState{});
        game->joints.front().alive = true;
    }

    void SetPoolAvailability(bool modelsFull, bool particlesFull, bool jointsFull)
    {
        FillModelEffectPoolExceptLast(modelsFull ? 0 : 1);
        for (Client::World::Particle& particle : game->particles.All())
            particle.alive = particlesFull;
        game->joints.assign(1, Client::Scenes::JointState{});
        game->joints.front().alive = jointsFull;
    }

    void SetFreeParticleSlots(std::size_t freeSlots)
    {
        auto particles = game->particles.All();
        for (Client::World::Particle& particle : particles)
            particle.alive = true;
        for (std::size_t freeSlot = 0; freeSlot < freeSlots; ++freeSlot)
            particles[particles.size() - freeSlot - 1].alive = false;
    }

    void ExpectWithOnlyModelPoolFull(const Probe& probe)
    {
        SCOPED_TRACE(probe.name);
        ActorState& monster = Prepare(probe);
        ASSERT_NE(game->world.FaunaResources().Find(static_cast<std::uint16_t>(monster.type)), nullptr);
        SetPoolAvailability(true, false, false);
        EXPECT_TRUE(std::all_of(game->world.Map().effects.Effects().begin(), game->world.Map().effects.Effects().end(),
                                [](const auto& effect) { return effect.alive; }));
        EXPECT_TRUE(std::any_of(game->particles.All().begin(), game->particles.All().end(),
                                [](const auto& particle) { return !particle.alive; }));
        ASSERT_EQ(game->joints.size(), 1u);
        EXPECT_FALSE(game->joints.front().alive);
        const auto stepped = Step(monster);
        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.sounds);
    }

    void ExpectWithOnlyJointPoolFull(const Probe& probe)
    {
        SCOPED_TRACE(probe.name);
        ActorState& monster = Prepare(probe);
        ASSERT_NE(game->world.FaunaResources().Find(static_cast<std::uint16_t>(monster.type)), nullptr);
        SetPoolAvailability(false, false, true);
        EXPECT_TRUE(std::any_of(game->world.Map().effects.Effects().begin(), game->world.Map().effects.Effects().end(),
                                [](const auto& effect) { return !effect.alive; }));
        EXPECT_TRUE(std::any_of(game->particles.All().begin(), game->particles.All().end(),
                                [](const auto& particle) { return !particle.alive; }));
        ASSERT_EQ(game->joints.size(), 1u);
        EXPECT_TRUE(game->joints.front().alive);
        const auto stepped = Step(monster);
        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.sounds);
    }

    void ExpectSaturated(const Probe& probe)
    {
        SCOPED_TRACE(probe.name);
        ActorState& monster = Prepare(probe);
        ASSERT_NE(game->world.FaunaResources().Find(static_cast<std::uint16_t>(monster.type)), nullptr);
        SaturatePools();

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.sounds);
    }

    void Expect(const Probe& probe)
    {
        SCOPED_TRACE(probe.name);
        ActorState& monster = Prepare(probe);
        const auto stepped = Step(monster);
        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with(probe.cut))
                << (stepped.HasValue() ? std::string("sin corte") : stepped.Error());
        }
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.sounds);
    }

    // Los mismos tipos en Lorencia y Devias, sin el ayudante de su mapa: sin caso por +0x376, van a la cola.
    void ExpectTheTailIn(std::uint8_t map, const std::vector<std::int32_t>& types)
    {
        Load(map);
        for (const std::int32_t type : types)
            Expect(Probe{.name = "en Lorencia o Devias", .type = type, .step = 14, .draws = Tail});
    }

    std::unique_ptr<Game> game;
    std::size_t slot = 0;
    std::uint32_t before = 0;
    std::vector<std::int32_t> sounds;
    std::vector<int> soundDraws;
    bool withParticles = true;
};

// 0x43a7a0 en Kalima (24 a 29 y 36): todos sus casos salen sin la cola; los efectos y la partícula 0x600 cortan después
// de la lectura de +0x38e y de los rand() de su ayudante (0x439bd0: rand() % 360; 0x43b18f: dos rand() % 200). Afuera,
// los mismos tipos van a la cola.
TEST_F(MonsterAttackCasesTest, TheKalimaMonstersLeaveWithoutTheTail)
{
    const std::vector<Probe> probes{
        {.name = "0x90 en 13", .type = 0x90, .step = 13},
        {.name = "0x90 en 14", .type = 0x90, .step = 14, .cut = "0x43a861"},
        {.name = "0x92 en 14", .type = 0x92, .step = 14, .cut = "0x43abd9"},
        {.name = "0x91 con 1 en 13", .type = 0x91, .step = 13, .skill = 1, .cut = "0x439ca6", .draws = SkillRead + 1},
        {.name = "0x91 con 0x37 en 13", .type = 0x91, .step = 13, .skill = 0x37, .cut = "0x439ba5", .draws = SkillRead},
        {.name = "0x91 con 0xc8 en 13", .type = 0x91, .step = 13, .skill = 0xc8, .cut = "0x439d09", .draws = SkillRead},
        {.name = "0x91 con 1 en 12", .type = 0x91, .step = 12, .skill = 1, .draws = SkillRead},
        {.name = "0x91 en la 4 y en 14",
         .type = 0x91,
         .step = 14,
         .action = 4,
         .cut = "0x43ab5f",
         .draws = SkillRead + 1 + 0xf08},
        {.name = "0x93 en la 4 y en 14", .type = 0x93, .step = 14, .action = 4, .cut = "0x43ac29"},
        {.name = "0x93 en la 3 y en 14", .type = 0x93, .step = 14},
        {.name = "0x94 con 0x11 en 14", .type = 0x94, .step = 14, .skill = 0x11, .draws = SkillRead + 1 + 0xf08},
        {.name = "0x94 con 0x11 en 13", .type = 0x94, .step = 13, .skill = 0x11, .draws = SkillRead},
        {.name = "0x95 con 1 en 14", .type = 0x95, .step = 14, .skill = 1, .cut = "0x43b1f4", .draws = SkillRead + 2},
        {.name = "0x95 con 0x11 en 14", .type = 0x95, .step = 14, .skill = 0x11, .cut = "0x43b0ed", .draws = SkillRead},
        {.name = "0x95 con 0x11 en 14 sin blanco",
         .type = 0x95,
         .step = 14,
         .skill = 0x11,
         .draws = SkillRead,
         .target = NoTarget},
        {.name = "0xa0 en la 3 y en 13", .type = 0xa0, .step = 13},
        {.name = "0xa0 en la 4 y en 13", .type = 0xa0, .step = 13, .action = 4},
        {.name = "0xa1 en la 3 y en 7", .type = 0xa1, .step = 7, .draws = SkillRead + 1 + 0xf08},
        {.name = "0xa1 en la 3 y en 13", .type = 0xa1, .step = 13, .draws = SkillRead},
        {.name = "0xa1 en la 4 y en 13", .type = 0xa1, .step = 13, .action = 4, .draws = SkillRead},
        {.name = "0x113 con 1 en 13", .type = 0x113, .step = 13, .skill = 1, .cut = "0x439ca6", .draws = SkillRead + 1},
        {.name = "0x113 en la 3 y en 13", .type = 0x113, .step = 13, .draws = SkillRead}};
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{36}})
    {
        SCOPED_TRACE(map);
        Load(map);
        // El héroe lejos del lugar del monstruo: el 0xf6/0 de 0x43b5ac compara su dueño con [0x743a170].
        game->world.Runtime().hero = HeroAway;
        for (const Probe& probe : probes)
            Expect(probe);
    }
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        ExpectTheTailIn(map, {0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0xa0, 0xa1, 0x113});
    }
}

TEST_F(MonsterAttackCasesTest, Kalima91CreatesTheStateOneEarthquakeBeforeThe615Boundary)
{
    constexpr std::uint8_t InitialStep = 14;
    constexpr std::uint8_t StoredStep = 15;
    constexpr std::uint8_t InitialOwnCount = 0x17;
    for (const std::size_t freeSlots : {0u, 1u, 2u})
    {
        for (const std::uint8_t initialDepth : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}})
        {
            SCOPED_TRACE(testing::Message()
                         << "slots libres " << freeSlots << ", depth " << static_cast<int>(initialDepth));
            Load(24);
            ActorState& monster = Prepare(
                Probe{.name = "0x91, acción 4, paso 14", .type = 0x91, .step = InitialStep, .action = 4, .target = 0});
            game->world.Runtime().ownRecordDepth = initialDepth;
            game->world.OwnCharacter().bytef04 = InitialOwnCount;
            FillModelEffectPoolExceptLast(freeSlots);

            CrtRandom expectedRandom(game->cloth.random.State());
            static_cast<void>(expectedRandom.Next());
            const auto expectedSubtype = expectedRandom.Next() % 100;
            const auto stepped = Step(monster);

            if (freeSlots == 2)
                EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x43ab5f")) << stepped.Error();
            else
                EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(monster.byte381, freeSlots == 2 ? InitialStep : StoredStep);
            EXPECT_EQ(game->world.Runtime().ownRecordDepth, initialDepth);
            EXPECT_EQ(game->world.OwnCharacter().bytef04,
                      static_cast<std::uint8_t>(InitialOwnCount + (freeSlots == 0 ? 0 : 1)));
            EXPECT_EQ(Draws(), SkillRead + (freeSlots == 0 ? 0 : 1 + (initialDepth == 0 ? 0xf08 : 0)));

            const auto effects = game->world.Map().effects.Effects();
            const auto earthquake = std::find_if(effects.begin(), effects.end(), [](const auto& effect)
                                                 { return effect.alive && effect.type == 0xfa; });
            if (freeSlots == 0)
            {
                EXPECT_EQ(earthquake, effects.end());
            }
            else
            {
                ASSERT_NE(earthquake, effects.end());
                EXPECT_EQ(earthquake->state, 1);
                EXPECT_EQ(earthquake->subtype, expectedSubtype);
                EXPECT_TRUE(earthquake->hasOwner);
                EXPECT_EQ(earthquake->owner.type, &monster.type);
                EXPECT_EQ(earthquake->owner.angles, &monster.angles);
                EXPECT_EQ(Client::World::OwnerTypeOf(earthquake->owner), std::optional<std::uint16_t>{monster.type});
                const std::uint16_t initialType = monster.type;
                monster.type = 0x1777;
                EXPECT_EQ(Client::World::OwnerTypeOf(earthquake->owner),
                          std::optional<std::uint16_t>{std::uint16_t{0x1777}});
                monster.type = initialType;
            }
        }
    }
}

TEST_F(MonsterAttackCasesTest, KalimaA1BindsTheLiveEarthquakeOwnerForDrawing)
{
    Load(24);
    ActorState& monster = Prepare(Probe{.name = "Kalima a1, acción 3, paso 7", .type = 0xa1, .step = 7});
    const auto stepped = Step(monster);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto effects = game->world.Map().effects.Effects();
    const auto quake = std::find_if(effects.begin(), effects.end(), [](const auto& effect)
                                    { return effect.alive && effect.type == Client::World::EarthquakeEffect; });
    ASSERT_NE(quake, effects.end());
    EXPECT_EQ(quake->state, 0);
    EXPECT_EQ(quake->ownerContext, &monster);
    EXPECT_EQ(quake->itemType, &monster.word8c);
    EXPECT_EQ(quake->itemLevel, &monster.byte8e);
    monster.word8c = 0x123;
    monster.byte8e = 7;
    EXPECT_EQ(*quake->itemType, 0x123);
    EXPECT_EQ(*quake->itemLevel, 7);
}

TEST_F(MonsterAttackCasesTest, KalimaSkipsEffectsWhenTheirOwnPoolIsFull)
{
    for (const std::uint16_t kind : {std::uint16_t{0x90}, std::uint16_t{0x92}})
    {
        SCOPED_TRACE(kind);
        Load(24);
        ActorState& monster = Prepare(Probe{.name = "Kalima 14 con partículas llenas", .type = kind, .step = 14});
        for (Client::World::Particle& particle : game->particles.All())
            particle.alive = true;

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, 14);
        EXPECT_EQ(Draws(), 0);
        EXPECT_TRUE(sounds.empty());
    }
}

TEST_F(MonsterAttackCasesTest, Kalima95SkipsJointsWhenTheirPoolIsFullAndChecksTheTargetRange)
{
    struct Case
    {
        std::uint16_t target;
        bool jointsFull;
    };
    constexpr std::array cases{Case{0, true}, Case{399, true}, Case{400, false}, Case{NoTarget, false}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << "target " << probe.target << ", joints llenos " << probe.jointsFull);
        Load(24);
        ActorState& monster = Prepare(Probe{
            .name = "Kalima 95 skill 0x11 en 14", .type = 0x95, .step = 14, .skill = 0x11, .target = probe.target});
        if (probe.jointsFull)
        {
            game->joints.assign(1, Client::Scenes::JointState{});
            game->joints.front().alive = true;
        }

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, 14);
        EXPECT_EQ(Draws(), SkillRead);
        EXPECT_TRUE(sounds.empty());
    }
}

TEST_F(MonsterAttackCasesTest, Kalima95UsesTheStepAndModelPoolForItsSkillOneEffects)
{
    Load(24);
    ActorState& early =
        Prepare(Probe{.name = "Kalima 95 skill 1 en 13", .type = 0x95, .step = 13, .skill = 1, .target = 0});

    const auto earlyStep = Step(early);

    EXPECT_TRUE(earlyStep.HasValue()) << earlyStep.Error();
    EXPECT_EQ(early.byte381, 13);
    EXPECT_EQ(Draws(), SkillRead);
    EXPECT_TRUE(sounds.empty());

    Load(24);
    ActorState& full =
        Prepare(Probe{.name = "Kalima 95 skill 1 en 14 con modelos llenos", .type = 0x95, .step = 14, .skill = 1});
    FillModelEffectPoolExceptLast(0);

    const auto fullStep = Step(full);

    EXPECT_TRUE(fullStep.HasValue()) << fullStep.Error();
    EXPECT_EQ(full.byte381, 14);
    EXPECT_EQ(Draws(), SkillRead + 6);
    EXPECT_TRUE(sounds.empty());
}

TEST_F(MonsterAttackCasesTest, KalimaSkillEffectsContinueWhenTheModelPoolIsFull)
{
    struct Case
    {
        std::uint8_t skill;
        int draws;
        std::vector<std::int32_t> sounds;
    };
    const std::array cases{Case{1, 2, {0x1e0}}, Case{0x37, 1, {0x1df}}, Case{0xc8, 1, {}}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.skill));
        Load(24);
        ActorState& monster = Prepare(Probe{.name = "0x91 paso 13", .type = 0x91, .step = 13, .skill = probe.skill});
        FillModelEffectPoolExceptLast(0);

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, 13);
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.sounds);
    }
}

TEST_F(MonsterAttackCasesTest, KalimaOtherFamiliesSkipFullModelEffectsAndKeepTheirNativeStepStores)
{
    struct Case
    {
        std::int32_t type;
        std::uint8_t step;
        std::uint8_t action;
        std::uint8_t skill;
        std::uint8_t storedStep;
        int draws;
    };
    const std::array cases{Case{0xa0, 13, 3, 0, 15, 0}, Case{0xa1, 13, 3, 0, 15, SkillRead}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.type);
        Load(24);
        ActorState& monster = Prepare(Probe{.name = "efectos de Kalima llenos",
                                            .type = probe.type,
                                            .step = probe.step,
                                            .action = probe.action,
                                            .skill = probe.skill});
        FillModelEffectPoolExceptLast(0);

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, probe.storedStep);
        EXPECT_EQ(Draws(), probe.draws);
    }

    Load(24);
    ActorState& earthquake =
        Prepare(Probe{.name = "0xa1 acción 3 paso 7", .type = 0xa1, .step = 7, .action = 3, .skill = 0});
    FillModelEffectPoolExceptLast(1);
    const auto made = Step(earthquake);
    ASSERT_TRUE(made.HasValue()) << made.Error();
    EXPECT_EQ(earthquake.byte381, 7);
    EXPECT_EQ(Draws(), SkillRead + 1 + 0xf08);
    const auto effects = game->world.Map().effects.Effects();
    const auto stateZero = std::find_if(effects.begin(), effects.end(),
                                        [](const auto& effect) { return effect.alive && effect.type == 0xfa; });
    ASSERT_NE(stateZero, effects.end());
    EXPECT_EQ(stateZero->state, 0);

    Load(24);
    ActorState& skillEleven =
        Prepare(Probe{.name = "0x94 skill 0x11 paso 14 acción 3", .type = 0x94, .step = 14, .skill = 0x11});
    FillModelEffectPoolExceptLast(1);
    const auto stateOne = Step(skillEleven);
    ASSERT_TRUE(stateOne.HasValue()) << stateOne.Error();
    EXPECT_EQ(skillEleven.byte381, 14);
    EXPECT_EQ(Draws(), SkillRead + 1 + 0xf08);
    const auto afterSkill = game->world.Map().effects.Effects();
    const auto quake = std::find_if(afterSkill.begin(), afterSkill.end(),
                                    [](const auto& effect) { return effect.alive && effect.type == 0xfa; });
    ASSERT_NE(quake, afterSkill.end());
    EXPECT_EQ(quake->state, 1);

    Load(24);
    ActorState& noStore = Prepare(Probe{.name = "0x93 acción 4 paso 14", .type = 0x93, .step = 14, .action = 4});
    FillModelEffectPoolExceptLast(0);
    const auto skipped = Step(noStore);
    EXPECT_TRUE(skipped.HasValue()) << skipped.Error();
    EXPECT_EQ(noStore.byte381, 14);
    EXPECT_EQ(Draws(), 0);
}

// Las habilidades 0xc9 y 0xca de los tres switch de Kalima (0x43aabf, 0x43b4f4 y 0x43b878): desde 13, +0x78 con 0x100
// o 0x8, +0x381 en 15 y el sonido 0x1e1. Lo que sigue lee el +0x381 nuevo: el 0x91 en la 4 ya no ve el 14 y el 0xa1 en
// la 3 suelta desde 13 sus 0xf6/0 y 0x5de/1 (CL11).
TEST_F(MonsterAttackCasesTest, TheKalimaWardsSetTheFlagsAndEndTheCount)
{
    struct Case
    {
        std::string_view name;
        std::int32_t type;
        std::uint8_t step;
        std::uint8_t action;
        std::uint8_t skill;
        std::uint32_t flags;
        std::string_view cut;
    };
    constexpr std::uint32_t Earlier = 0x40;
    const std::vector<Case> cases{{"0x91 con 0xc9 en 13", 0x91, 13, 3, 0xc9, 0x100, ""},
                                  {"0x91 con 0xca en 14 y en la 4", 0x91, 14, 4, 0xca, 0x8, ""},
                                  {"0xa1 con 0xca en 13 y en la 3", 0xa1, 13, 3, 0xca, 0x8, ""},
                                  {"0x113 con 0xc9 en 14", 0x113, 14, 3, 0xc9, 0x100, ""}};
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{36}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().hero = HeroAway;
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(probe.name);
            ActorState& monster = Prepare(Probe{.name = probe.name,
                                                .type = probe.type,
                                                .step = probe.step,
                                                .action = probe.action,
                                                .skill = probe.skill});
            monster.flags78 = Earlier;
            const auto stepped = Step(monster);
            EXPECT_EQ(stepped.HasValue(), probe.cut.empty());
            if (!probe.cut.empty() && !stepped.HasValue())
            {
                EXPECT_TRUE(stepped.Error().starts_with(probe.cut)) << stepped.Error();
            }
            EXPECT_EQ(monster.flags78, Earlier | probe.flags);
            EXPECT_EQ(monster.byte381, 15);
            EXPECT_EQ(sounds, (std::vector<std::int32_t>{0x1e1}));
            EXPECT_EQ(Draws(), SkillRead);
        }
        // En 12, nada.
        ActorState& early = Prepare(Probe{.name = "en 12", .type = 0x91, .step = 12, .skill = 0xc9});
        early.flags78 = Earlier;
        ASSERT_TRUE(Step(early).HasValue());
        EXPECT_EQ(early.flags78, Earlier);
        EXPECT_EQ(early.byte381, 12);
        EXPECT_TRUE(sounds.empty());
    }
}

// 42dcf0 en30: el68 emite sólo en5 y el120 siempre. Las señales propias conservan ese caller.
// 429110 en33/54 conserva sus fronteras en10 y en la acción que corresponde.
TEST_F(MonsterAttackCasesTest, TheSiegeAndTheTrialMonstersLeaveOrCut)
{
    Load(30);
    for (const Probe& probe : std::vector<Probe>{{.name = "0x68 en 4", .type = 0x68, .step = 4},
                                                 {.name = "0x68 en 5", .type = 0x68, .step = 5},
                                                 {.name = "0x120", .type = 0x120, .step = 1, .draws = 1}})
        Expect(probe);
    const std::vector<Probe> trials{
        {.name = "0x135 en la 3 y en 10", .type = 0x135, .step = 10, .cut = "0x42919a"},
        {.name = "0x135 en la 4 y en 10", .type = 0x135, .step = 10, .action = 4},
        {.name = "0x134 en la 4 y en 10", .type = 0x134, .step = 10, .action = 4, .cut = "0x429202"},
        {.name = "0x134 en la 4 y en 9", .type = 0x134, .step = 9, .action = 4},
        {.name = "0x130 en la 4 y en 10", .type = 0x130, .step = 10, .action = 4, .cut = "0x42925a"},
        {.name = "0x130 en la 3 y en 10", .type = 0x130, .step = 10}};
    for (const std::uint8_t map : {std::uint8_t{33}, std::uint8_t{54}})
    {
        SCOPED_TRACE(map);
        Load(map);
        for (const Probe& probe : trials)
            Expect(probe);
    }
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        ExpectTheTailIn(map, {0x68, 0x120, 0x130, 0x134, 0x135});
    }
}

// 42dd23..42dda4: entradas completas, dueñoNULL/keyffff y salida antes de la cola del ataque.
TEST_F(MonsterAttackCasesTest, SiegeCuesKeepTheCallerInputsAndUseTheFirstFreeEffectSlot)
{
    Load(30);
    withParticles = false;
    for (const std::uint16_t kind : {std::uint16_t{0x68}, std::uint16_t{0x120}})
    {
        SCOPED_TRACE(kind);
        ActorState& monster = Prepare(Probe{.name = "señal del asedio", .type = kind, .step = 5, .target = 0});
        monster.position = {1125.0f, 2250.0f, 37.25f};
        monster.angles = {12.0f, 34.0f, 56.0f};
        monster.light = {.2f, .4f, .6f};
        monster.flags78 = 0x12345678;
        FillModelEffectPoolExceptLast(2);
        auto effects = game->world.Map().effects.Effects();
        const auto unused = effects.back();
        const std::size_t firstFree = effects.size() - 2;
        const auto original = monster.position;

        const auto result = Step(monster);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& cue = effects[firstFree];
        EXPECT_TRUE(cue.alive);
        EXPECT_EQ(cue.type, kind == 0x68 ? 0x19f : 0x61c);
        EXPECT_EQ(cue.subtype, 0);
        EXPECT_EQ(cue.transform.position, (glm::vec3{1125.0f, 2250.0f, kind == 0x68 ? 537.25f : 37.25f}));
        EXPECT_EQ(cue.transform.angles, monster.angles);
        EXPECT_EQ(cue.light, monster.light);
        EXPECT_EQ(cue.ownerContext, nullptr);
        EXPECT_FALSE(cue.hasOwner);
        EXPECT_EQ(cue.disturbanceState, 0xffff);
        EXPECT_FALSE(effects.back().alive);
        EXPECT_EQ(effects.back().type, unused.type);
        EXPECT_EQ(effects.back().life, unused.life);
        EXPECT_EQ(effects.back().transform.position, unused.transform.position);
        EXPECT_EQ(monster.position, original);
        EXPECT_EQ(monster.flags78, 0x12345678u);
        EXPECT_EQ(monster.byte381, 5);
        EXPECT_EQ(Draws(), kind == 0x120 ? 1 : 0);
        EXPECT_TRUE(sounds.empty());
        EXPECT_TRUE(game->joints.empty());
    }
}

TEST_F(MonsterAttackCasesTest, SiegeCueGatesUseTheMapAndKindFrameWithoutAnActionOrTargetRequirement)
{
    Load(30);
    withParticles = false;
    for (const std::uint8_t map :
         {std::uint8_t{0}, std::uint8_t{29}, std::uint8_t{30}, std::uint8_t{31}, std::uint8_t{34}, std::uint8_t{35}})
        for (const std::uint16_t kind : {std::uint16_t{0x68}, std::uint16_t{0x120}})
            for (const std::uint8_t frame :
                 {std::uint8_t{0}, std::uint8_t{4}, std::uint8_t{5}, std::uint8_t{6}, std::uint8_t{255}})
            {
                SCOPED_TRACE(testing::Message() << "map=" << static_cast<int>(map) << " kind=" << kind
                                                << " frame=" << static_cast<int>(frame));
                // El alta usa el30; este caso varía sólo el gate del caller, sin cargar mundos de otros mapas.
                game->world.Runtime().map = 30;
                ActorState& monster = Prepare(Probe{.name = "gate de la señal",
                                                    .type = kind,
                                                    .step = frame,
                                                    .action = 0xff,
                                                    .target = NoTarget,
                                                    .model = std::uint16_t{0x160}});
                game->world.Map().effects.Clear();
                game->world.Runtime().map = map;

                const auto result = Step(monster);

                ASSERT_TRUE(result.HasValue()) << result.Error();
                const bool emitted = map == 30 && (kind == 0x120 || frame == 5);
                EXPECT_EQ(std::ranges::count_if(game->world.Map().effects.Effects(),
                                                [](const auto& effect) { return effect.alive; }),
                          emitted ? 1 : 0);
                EXPECT_EQ(Draws(), emitted && kind == 0x120 ? 1 : 0);
                EXPECT_EQ(monster.byte381, frame);
                EXPECT_EQ(monster.animation.action, 0xff);
                EXPECT_TRUE(sounds.empty());
            }
}

// 4329ec devuelve AL=0 después del primer abanico. 4f6442 vuelve a pasar por la rama común.
TEST_F(MonsterAttackCasesTest, CrywolfRunsItsRadialVolleyBeforeTheCommonVolley)
{
    withParticles = false;
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{34}})
    {
        Load(map);
        for (const std::size_t free : {0u, 1u, 40u})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << static_cast<int>(map) << ", lugares " << free);
            auto& actor = Prepare(Probe{.name = "abanico de ataque",
                                        .type = 3,
                                        .step = 14,
                                        .skill = CaseSkill,
                                        .kind = std::uint16_t{0x3b},
                                        .model = std::uint16_t{0x17c}});
            actor.position = {1100, 2100, 40};
            actor.angles = {3, 5, 10};
            actor.light = {0.1f, 0.3f, 0.5f};
            FillModelEffectPoolExceptLast(free);

            const auto result = Step(actor);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            constexpr std::size_t PerVolley = 18;
            const std::size_t made = std::min(free, map == 34 ? 2 * PerVolley : PerVolley);
            const auto effects = game->world.Map().effects.Effects().last(free);
            for (std::size_t i = 0; i < made; ++i)
            {
                const auto& shot = effects[i];
                EXPECT_TRUE(shot.alive);
                EXPECT_EQ(shot.type, 0xc1a);
                EXPECT_EQ(shot.subtype, 0);
                EXPECT_EQ(shot.transform.position, (glm::vec3{1100, 2100, 320}));
                EXPECT_EQ(shot.transform.angles, (glm::vec3{23, 5, 10.0f + 20.0f * static_cast<float>(i % PerVolley)}));
                EXPECT_EQ(shot.light, actor.light);
                EXPECT_EQ(shot.ownerContext, nullptr);
                EXPECT_EQ(shot.disturbanceState, 0xffffu);
            }
            EXPECT_TRUE(std::ranges::none_of(effects.subspan(made), [](const auto& effect) { return effect.alive; }));
            EXPECT_EQ(Draws(), (map == 34 ? 2 : 1) * SkillRead + Tail);
        }
    }
}

TEST_F(MonsterAttackCasesTest, CrywolfAndFortressLaunchF8AtFrameFourteenAndPreserveTheOwnerAndKey)
{
    withParticles = false;
    for (const std::uint8_t map : {std::uint8_t{34}, std::uint8_t{35}})
    {
        Load(map, map == 35 ? std::uint8_t{0} : map);
        for (const std::size_t free : {0u, 1u})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << static_cast<int>(map) << ", lugares " << free);
            auto& actor =
                Prepare(Probe{.name = "flecha de Crywolf o Fortress", .type = map == 34 ? 0x13c : 0x136, .step = 14});
            actor.angles = {15, 25, 35};
            actor.word8a = 0x1234;
            FillModelEffectPoolExceptLast(free);
            const auto result = Step(actor);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(Draws(), free == 0 ? 0 : OwnRecord);
            EXPECT_EQ(actor.angles, (free == 0 ? glm::vec3{15, 25, 35} : glm::vec3{0, 0, 35}));
            if (free == 0)
                continue;
            const auto& arrow = game->world.Map().effects.Effects().back();
            EXPECT_TRUE(arrow.alive);
            EXPECT_EQ(arrow.type, 0xf8);
            EXPECT_EQ(arrow.subtype, 1);
            EXPECT_EQ(arrow.life, 30);
            EXPECT_EQ(arrow.ownerContext, &actor);
            EXPECT_EQ(arrow.disturbanceState, 0x1234u);
            EXPECT_EQ(arrow.transform.angles, (glm::vec3{15, 25, 35}));
        }
    }
}

// 0x42fce0 en el 35 (con el terreno de Lorencia: el Data del repo no tiene el World36) y 0x4324c0 en Crywolf, por el
// tipo: los efectos en 14 (0x1ad y 0x1b2) y en 15 (0x1b5); si no, siguen a la cola. El 0x17c y el 0x17e los crean los
// tipos 0x3a, 0x3b, 0x3e, 0x52, 0x53 y 0x12c, que el alta corta: el tipo y el +0x376 van a mano.
TEST_F(MonsterAttackCasesTest, TheFortressAndCrywolfLookAtTheModel)
{
    Load(35, 0);
    EXPECT_EQ(Create(0x136).type, 0x1ad);
    EXPECT_EQ(Create(0x13c).type, 0x1b2);
    for (const Probe& probe :
         std::vector<Probe>{{.name = "0x1ad en 14", .type = 0x136, .step = 14, .draws = OwnRecord},
                            {.name = "0x1ad en 13", .type = 0x136, .step = 13, .draws = Tail},
                            {.name = "0x1b2 en 14", .type = 0x13c, .step = 14, .draws = OwnRecord}})
        Expect(probe);

    Load(34);
    EXPECT_EQ(Create(0x138).type, 0x1b2);
    EXPECT_EQ(Create(0x13c).type, 0x1ad);
    EXPECT_EQ(Create(0x15c).type, 0x1b5);
    const std::vector<Probe> crywolf{{.name = "0x1b2 en 14", .type = 0x138, .step = 14, .draws = OwnRecord},
                                     {.name = "0x1b2 en 13", .type = 0x138, .step = 13, .draws = Tail},
                                     {.name = "0x1ad en 14", .type = 0x13c, .step = 14, .draws = OwnRecord},
                                     {.name = "0x1b5 en 15", .type = 0x15c, .step = 15, .draws = OwnRecord},
                                     {.name = "0x1b5 en 14", .type = 0x15c, .step = 14, .draws = Tail},
                                     {.name = "0x17c en 1",
                                      .type = 3,
                                      .step = 1,
                                      .draws = 2418,
                                      .sounds = std::vector<std::int32_t>(16, 0x40),
                                      .kind = std::uint16_t{0x3b},
                                      .model = std::uint16_t{0x17c}},
                                     {.name = "0x17c con '2' en 14",
                                      .type = 3,
                                      .step = 14,
                                      .skill = CaseSkill,
                                      .draws = 2 * SkillRead + Tail,
                                      .kind = std::uint16_t{0x3b},
                                      .model = std::uint16_t{0x17c}},
                                     // 0x4324c0 lee +0x38e y sigue; el caso 0x3b de 0x4f5b70 la vuelve a leer en 14.
                                     {.name = "0x17c en 14",
                                      .type = 3,
                                      .step = 14,
                                      .draws = 2 * SkillRead + Tail,
                                      .kind = std::uint16_t{0x3b},
                                      .model = std::uint16_t{0x17c}},
                                     {.name = "0x17c del 0x3a con '2' en 14",
                                      .type = 3,
                                      .step = 14,
                                      .skill = CaseSkill,
                                      .draws = Tail,
                                      .kind = std::uint16_t{0x3a},
                                      .model = std::uint16_t{0x17c}},
                                     {.name = "0x17e en 1",
                                      .type = 3,
                                      .step = 1,
                                      .cut = "0x432acc",
                                      .draws = 6,
                                      .sounds = {0x75},
                                      .kind = std::uint16_t{0x3e},
                                      .model = std::uint16_t{0x17e}},
                                     {.name = "0x17e en 2",
                                      .type = 3,
                                      .step = 2,
                                      .cut = "0x432acc",
                                      .draws = 6,
                                      .kind = std::uint16_t{0x3e},
                                      .model = std::uint16_t{0x17e}}};
    for (const Probe& probe : crywolf)
        Expect(probe);

    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        ExpectTheTailIn(map, {0x136, 0x138, 0x13c, 0x15c});
    }
}

TEST_F(MonsterAttackCasesTest, TheSiegeTrialAndFortressHelpersExitWhenTheirModelEffectPoolIsFull)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t terrain;
        std::int32_t type;
        std::uint8_t step;
        std::uint8_t action;
        std::string_view cut;
    };
    const std::array cases{Case{30, 30, 0x120, 1, 3, ""},          Case{30, 30, 0x68, 5, 3, ""},
                           Case{33, 33, 0x135, 10, 3, "0x42919a"}, Case{54, 54, 0x134, 10, 4, "0x429202"},
                           Case{33, 33, 0x130, 10, 4, "0x42925a"}, Case{35, 0, 0x136, 14, 3, ""},
                           Case{35, 0, 0x13c, 14, 3, ""}};
    withParticles = false;
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << "map " << static_cast<int>(probe.map) << ", type " << probe.type);
        Load(probe.map, probe.terrain);
        ActorState& monster = Prepare(Probe{.name = "ayudante con efecto omitido",
                                            .type = probe.type,
                                            .step = probe.step,
                                            .action = probe.action,
                                            .target = 0});
        FillModelEffectPoolExceptLast(0);

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 0);
        EXPECT_TRUE(sounds.empty());
        ASSERT_EQ(game->world.Map().effects.Effects().size(), Client::World::ModelEffectCount);
        EXPECT_TRUE(std::all_of(game->world.Map().effects.Effects().begin(), game->world.Map().effects.Effects().end(),
                                [](const Client::World::ModelEffect& effect) { return effect.alive; }));

        FillModelEffectPoolExceptLast(1);
        const auto freeSlot = Step(monster);
        if (probe.map == 30)
        {
            EXPECT_TRUE(freeSlot.HasValue()) << freeSlot.Error();
            EXPECT_EQ(Draws(), probe.type == 0x120 ? 1 : 0);
            const auto& cue = game->world.Map().effects.Effects().back();
            EXPECT_TRUE(cue.alive);
            EXPECT_EQ(cue.type, probe.type == 0x120 ? 0x61c : 0x19f);
        }
        else if (probe.cut.empty())
        {
            EXPECT_TRUE(freeSlot.HasValue()) << freeSlot.Error();
            EXPECT_EQ(Draws(), OwnRecord);
        }
        else
        {
            EXPECT_TRUE(!freeSlot.HasValue() && freeSlot.Error().starts_with(probe.cut)) << freeSlot.Error();
            EXPECT_EQ(Draws(), 0);
        }
    }
    withParticles = true;
}

TEST_F(MonsterAttackCasesTest, Crywolf17cScattersAndContinuesThroughTheCommonKindCase)
{
    struct Case
    {
        std::uint16_t kind;
        std::uint16_t target;
        bool particlesFull;
        int draws;
        std::size_t sounds;
    };
    constexpr std::uint16_t NoKind = 0x2345;
    const std::array cases{Case{NoKind, 0, true, 338, 0},        Case{NoKind, 0, false, 1138, 8},
                           Case{0x3a, 0, true, 674, 0},          Case{0x3a, NoTarget, true, 672, 0},
                           Case{0x3b, 0, true, 674, 0},          Case{0x3a, 0, false, 2274, 16},
                           Case{0x3a, NoTarget, false, 2272, 16}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << "kind " << probe.kind << ", target " << probe.target << ", particles full "
                                        << probe.particlesFull);
        Load(34);
        ActorState& monster = Prepare(Probe{.name = "0x17c en paso 1",
                                            .type = 3,
                                            .step = 1,
                                            .target = probe.target,
                                            .kind = probe.kind,
                                            .model = std::uint16_t{0x17c}});
        FillModelEffectPoolExceptLast(0);
        if (probe.particlesFull)
            SaturatePools();

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, std::vector<std::int32_t>(probe.sounds, 0x40));
        EXPECT_TRUE(std::all_of(game->world.Map().effects.Effects().begin(), game->world.Map().effects.Effects().end(),
                                [](const Client::World::ModelEffect& effect) { return effect.alive; }));
    }
}

TEST_F(MonsterAttackCasesTest, CrywolfScattersPairedStonesInSlotOrderBeforeTheCommonTail)
{
    struct Capacity
    {
        std::size_t freeSlots;
        int draws;
    };
    constexpr std::array capacities{Capacity{0, 674}, Capacity{1, 678}, Capacity{3, 687}, Capacity{32, 818}};
    constexpr std::array firstTypes{0xc9, 0xc8, 0xc8};
    constexpr std::array firstLives{34, 35, 38};
    constexpr std::array firstYaws{120.0f, 35.0f, 151.0f};
    constexpr std::array firstVerticalSpeeds{18.0f, 17.0f, 11.0f};
    for (const auto [freeSlots, draws] : capacities)
    {
        SCOPED_TRACE(freeSlots);
        Load(34);
        ActorState& monster = Prepare(
            Probe{.type = 3, .step = 1, .target = 0, .kind = std::uint16_t{0x3b}, .model = std::uint16_t{0x17c}});
        monster.position = {1000, 2000, 350};
        FillModelEffectPoolExceptLast(freeSlots);
        for (Client::World::Particle& particle : game->particles.All())
            particle.alive = true;
        game->cloth.random = CrtRandom(Seed);

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), draws);
        EXPECT_TRUE(sounds.empty());
        const auto effects = game->world.Map().effects.Effects().last(freeSlots);
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            SCOPED_TRACE(index);
            const auto& effect = effects[index];
            ASSERT_TRUE(effect.alive);
            EXPECT_EQ(effect.subtype, index % 2 == 0 ? 1 : 0);
            EXPECT_FALSE(effect.hasOwner);
            EXPECT_EQ(effect.transform.position.z, 350.0f);
            if (index < firstTypes.size())
            {
                EXPECT_EQ(effect.type, firstTypes[index]);
                EXPECT_EQ(effect.life, firstLives[index]);
                EXPECT_EQ(effect.transform.angles.z, firstYaws[index]);
                EXPECT_EQ(effect.turnDirection, firstVerticalSpeeds[index]);
            }
            if (index < 2 || index == 16)
            {
                EXPECT_EQ(effect.transform.position, (glm::vec3{1000, 1780, 350}));
            }
            if (index == 2)
            {
                EXPECT_NEAR(effect.transform.position.x, 1155.5635f, 0.001f);
                EXPECT_NEAR(effect.transform.position.y, 1844.4365f, 0.001f);
            }
            if (effect.subtype == 1)
            {
                EXPECT_EQ(effect.velocity, glm::vec3(0));
            }
        }
    }
}

TEST_F(MonsterAttackCasesTest, CrywolfFlamesWithAFullParticlePoolReachTheCommonTail)
{
    Load(34);
    constexpr std::uint16_t NoKind = 0x2345;
    for (const std::uint8_t step : {std::uint8_t{1}, std::uint8_t{2}})
    {
        ActorState& monster =
            Prepare(Probe{.type = 3, .step = step, .target = 0, .kind = NoKind, .model = std::uint16_t{0x17e}});
        for (auto& particle : game->particles.All())
        {
            particle.alive = true;
            particle.type = 0x5c5;
        }

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 6 + 4 + Tail);
        EXPECT_EQ(sounds.size(), static_cast<std::size_t>(step == 1));
        if (step == 1)
        {
            ASSERT_FALSE(sounds.empty());
            EXPECT_EQ(sounds.front(), 0x75);
            EXPECT_EQ(soundDraws.front(), 6);
        }
        EXPECT_TRUE(std::ranges::all_of(game->particles.All(),
                                        [](const auto& particle) { return particle.alive && particle.type == 0x5c5; }));
    }
}

TEST_F(MonsterAttackCasesTest, Crywolf17cStepFourteenContinuesPastTheFull18c1aPool)
{
    for (const bool allPoolsFull : {false, true})
    {
        for (const std::uint16_t target : {std::uint16_t{0}, NoTarget})
        {
            SCOPED_TRACE(testing::Message() << "pools llenos " << allPoolsFull << ", target " << target);
            Load(34);
            ActorState& monster = Prepare(Probe{.name = "0x17c paso 14 con '2'",
                                                .type = 3,
                                                .step = 14,
                                                .skill = CaseSkill,
                                                .target = target,
                                                .kind = std::uint16_t{0x3b},
                                                .model = std::uint16_t{0x17c}});
            if (allPoolsFull)
                SaturatePools();
            else
                FillModelEffectPoolExceptLast(0);

            const auto stepped = Step(monster);

            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(monster.byte381, 14);
            EXPECT_EQ(Draws(), target == NoTarget ? 2 : 4);
            EXPECT_TRUE(sounds.empty());
        }
    }
}

// 432b30..432b63 y42fd04..42fd37: los callers no sortean ni pasan por la cola del ataque.
TEST_F(MonsterAttackCasesTest, CrywolfAndFortressMissilesPreserveTheirNativeArgumentsAndOwner)
{
    struct Missile
    {
        std::uint8_t map;
        std::int32_t kind;
        std::uint8_t step;
        std::int16_t effect;
    };
    const std::array cases{Missile{34, 0x15c, 15, 0xed}, Missile{35, 0x13c, 14, 0xe1}};
    for (const auto& missile : cases)
    {
        SCOPED_TRACE(missile.map);
        Load(missile.map, 0);
        auto& monster = Prepare({.type = missile.kind, .step = missile.step, .skill = 0x11});
        monster.position = {2100, 3100, 450};
        monster.angles = {10, 20, 30};
        monster.light = {.2f, .4f, .6f};
        monster.word8a = -0x1234;
        game->world.Map().effects.Clear();
        for (auto& joint : game->world.DrawTail().joints)
            joint.alive = false;
        game->world.OwnCharacter().bytef04 = 0x5a;
        const auto stepped = Step(monster);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto effects = game->world.Map().effects.Effects();
        ASSERT_TRUE(effects[0].alive);
        const auto& effect = effects[0];
        EXPECT_EQ(effect.type, missile.effect);
        EXPECT_EQ(effect.subtype, 1);
        EXPECT_EQ(effect.ownerContext, &monster);
        EXPECT_EQ(effect.disturbanceState, static_cast<std::uint16_t>(monster.word8a));
        EXPECT_EQ(effect.word8c, 0x5a);
        EXPECT_EQ(effect.light, monster.light);
        EXPECT_EQ(effect.life, 30);
        const auto offset = Client::Render::Models::OriginalVectorRotate(
            {-10, -60, 135}, Client::Render::Models::OriginalAngleMatrix(monster.angles));
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_FLOAT_EQ(effect.transform.position[axis],
                            static_cast<float>(static_cast<double>(offset[axis]) + monster.position[axis]));
        EXPECT_FLOAT_EQ(effect.transform.angles.x, missile.effect == 0xed ? -80.f : 10.f);
        EXPECT_EQ(effect.byte89, 0);
        EXPECT_EQ(effect.state, 0);
        EXPECT_EQ(effect.byte1bd, 0);
        EXPECT_EQ(Draws(), OwnRecord);
        EXPECT_EQ(monster.byte38eAccesses, 0);
        EXPECT_TRUE(sounds.empty());
        const auto& joint = game->world.DrawTail().joints[0];
        EXPECT_EQ(joint.alive, missile.effect == 0xe1);
        if (missile.effect == 0xe1)
        {
            EXPECT_EQ(joint.type, 0x610);
            EXPECT_EQ(joint.subtype, 13);
            EXPECT_EQ(joint.ownerContext, &effect);
        }
    }
}
TEST_F(MonsterAttackCasesTest, CrywolfAndFortressMissilesExitWithoutReadingTheCountWithAFullModelPool)
{
    for (const bool fortress : {false, true})
    {
        Load(fortress ? 35 : 34, 0);
        auto& monster = Prepare(
            {.type = fortress ? 0x13c : 0x15c, .step = static_cast<std::uint8_t>(fortress ? 14 : 15), .skill = 0x11});
        FillModelEffectPoolExceptLast(0);
        for (auto& joint : game->world.DrawTail().joints)
            joint.alive = false;
        const auto stepped = Step(monster);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 0);
        EXPECT_EQ(monster.byte38eAccesses, 0);
        EXPECT_FALSE(game->world.DrawTail().joints[0].alive);
        EXPECT_TRUE(sounds.empty());
    }
}
TEST_F(MonsterAttackCasesTest, CrywolfAndFortressMissilesStillCopyTheCountWithAFullJointPool)
{
    for (const bool fortress : {false, true})
    {
        Load(fortress ? 35 : 34, 0);
        auto& monster = Prepare(
            {.type = fortress ? 0x13c : 0x15c, .step = static_cast<std::uint8_t>(fortress ? 14 : 15), .skill = 0x11});
        game->world.Map().effects.Clear();
        for (auto& joint : game->world.DrawTail().joints)
            joint.alive = true;
        auto& joint = game->world.DrawTail().joints[0];
        joint.alive = true;
        joint.type = 0x777;
        const auto stepped = Step(monster);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(game->world.Map().effects.Effects()[0].type, fortress ? 0xe1 : 0xed);
        EXPECT_EQ(joint.type, 0x777);
        EXPECT_EQ(Draws(), OwnRecord);
        EXPECT_EQ(monster.byte38eAccesses, 0);
        EXPECT_TRUE(sounds.empty());
    }
}

TEST_F(MonsterAttackCasesTest, CrywolfAttacksSkipTheirFullModelEffectAndExit)
{
    const std::array cases{Probe{.name = "0x1b2 en 14", .type = 0x138, .step = 14},
                           Probe{.name = "0x1ad en 14", .type = 0x13c, .step = 14},
                           Probe{.name = "0x1b5 en 15", .type = 0x15c, .step = 15}};
    for (const Probe& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        Load(34);
        ActorState& monster = Prepare(probe);
        FillModelEffectPoolExceptLast(0);

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, probe.step);
        EXPECT_EQ(Draws(), 0);
        EXPECT_TRUE(sounds.empty());
    }
}

// El switch por +0x376 (0x4f5c7e): cada caso con su +0x381, su acción y '2' en +0x38e, el corte después de sus
// lecturas y rand(), o la cola. En Lorencia y en Devias (los mismos en cualquier mapa fuera de los ayudantes).
TEST_F(MonsterAttackCasesTest, TheKindCasesCutAfterTheirDrawsOrGoOnToTheTail)
{
    const std::vector<Probe> probes{
        {.name = "0x46 en 5", .type = 0x46, .step = 5, .cut = "0x4f5d13"},
        {.name = "0x46 en 5 sin blanco", .type = 0x46, .step = 5, .target = NoTarget},
        {.name = "0x46 en 4", .type = 0x46, .step = 4, .draws = Tail},
        {.name = "0x3d en 1", .type = 3, .step = 1, .draws = Tail, .kind = std::uint16_t{0x3d}},
        {.name = "0x3d en 2", .type = 3, .step = 2, .draws = Tail, .kind = std::uint16_t{0x3d}},
        {.name = "0x3f en 1",
         .type = 3,
         .step = 1,
         .draws = 1208 + SkillRead + Tail,
         .sounds = std::vector<std::int32_t>(8, 0x40),
         .kind = std::uint16_t{0x3f}},
        {.name = "0x3f con '2'",
         .type = 3,
         .step = 2,
         .skill = CaseSkill,
         .cut = "0x5193d6",
         .draws = SkillRead + 2 + BlastDraws,
         .kind = std::uint16_t{0x3f},
         .joints = JointTable},
        {.name = "0x3f en 2", .type = 3, .step = 2, .draws = SkillRead + Tail, .kind = std::uint16_t{0x3f}},
        {.name = "0x42 con '2' en 1", .type = 0x42, .step = 1, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x42 con '2' en 2", .type = 0x42, .step = 2, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x35 en 1",
         .type = 3,
         .step = 1,
         .draws = 1210,
         .sounds = std::vector<std::int32_t>(8, 0x40),
         .kind = std::uint16_t{0x35}},
        {.name = "0x3b con '2' en 14",
         .type = 3,
         .step = 14,
         .skill = CaseSkill,
         .draws = SkillRead + Tail,
         .kind = std::uint16_t{0x3b}},
        {.name = "0x3a con '2' en 14",
         .type = 3,
         .step = 14,
         .skill = CaseSkill,
         .draws = Tail,
         .kind = std::uint16_t{0x3a}},
        {.name = "0x31 en 6", .type = 0x31, .step = 6, .cut = "0x4f67fb"},
        {.name = "0x31 con '2' en 2", .type = 0x31, .step = 2, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x2a con '2' en 1",
         .type = 0x2a,
         .step = 1,
         .skill = CaseSkill,
         .cut = "0x4f6ec5",
         .draws = SkillRead},
        {.name = "0x2a con '2' en 2",
         .type = 0x2a,
         .step = 2,
         .skill = CaseSkill,
         .cut = "0x4f6fc5",
         .draws = SkillRead + 2},
        {.name = "0x2a en 2", .type = 0x2a, .step = 2, .draws = SkillRead + Tail},
        {.name = "0x23 con '2' en 1",
         .type = 0x23,
         .step = 1,
         .skill = CaseSkill,
         .cut = "0x4f7050",
         .draws = SkillRead},
        {.name = "0x23 con '2' en 2", .type = 0x23, .step = 2, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x26 con '2' en 1",
         .type = 0x26,
         .step = 1,
         .skill = CaseSkill,
         .cut = "0x4f70b8",
         .draws = SkillRead},
        {.name = "0x43 con '2' en 2",
         .type = 0x43,
         .step = 2,
         .skill = CaseSkill,
         .cut = "0x4f80cf",
         .draws = SkillRead + 2},
        {.name = "0x48 con '2' en 14",
         .type = 0x48,
         .step = 14,
         .skill = CaseSkill,
         .cut = "0x4f756a",
         .draws = SkillRead + 3},
        {.name = "0x48 con '2' en 13", .type = 0x48, .step = 13, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x47 en la 3 y en 5",
         .type = 0x47,
         .step = 5,
         .draws = 1208 + Tail,
         .sounds = std::vector<std::int32_t>(8, 0x40)},
        {.name = "0x4a en la 4 y en 5",
         .type = 0x4a,
         .step = 5,
         .action = 4,
         .draws = 1208 + Tail,
         .sounds = std::vector<std::int32_t>(8, 0x40)},
        {.name = "0x12d en la 5 y en 5", .type = 0x12d, .step = 5, .action = 5, .draws = Tail},
        {.name = "0x49 en la 3 y en 11",
         .type = 0x49,
         .step = 11,
         .draws = 1208 + 5 * 2 + Tail,
         .sounds = FiveStabSounds(),
         .joints = JointTable},
        {.name = "0x49 en la 3 y en 13", .type = 0x49, .step = 13, .draws = Tail},
        {.name = "0x4b en la 4 y en 13",
         .type = 0x4b,
         .step = 13,
         .action = 4,
         .draws = Tail,
         .sounds = {StabSound, 0x42},
         .joints = JointTable},
        {.name = "0x49 en la 4 y en 9", .type = 0x49, .step = 9, .action = 4, .draws = Tail},
        {.name = "0x4d con '2' en 2",
         .type = 0x4d,
         .step = 2,
         .skill = CaseSkill,
         .cut = "0x4f7f3c",
         .draws = SkillRead + 3},
        {.name = "0x4d con '2' en 6",
         .type = 0x4d,
         .step = 6,
         .skill = CaseSkill,
         .cut = "0x4f7f3c",
         .draws = SkillRead + 3},
        {.name = "0x4d con '2' en 3", .type = 0x4d, .step = 3, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x36 en 1", .type = 0x36, .step = 1, .draws = 0xf08 + Tail},
        {.name = "0x97 en 1", .type = 0x97, .step = 1, .draws = 0xf08 + Tail},
        {.name = "0x36 en 2", .type = 0x36, .step = 2, .draws = Tail},
        {.name = "0x67 con '2'",
         .type = 0x67,
         .step = 2,
         .skill = CaseSkill,
         .cut = "0x4f80cf",
         .draws = SkillRead + 2},
        {.name = "0x67", .type = 0x67, .step = 2, .draws = SkillRead + Tail},
        {.name = "sin caso", .type = 3, .step = 1, .draws = Tail},
        {.name = "sin caso ni blanco", .type = 3, .step = 1, .target = NoTarget}};
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        for (const Probe& probe : probes)
            Expect(probe);
    }
}

TEST_F(MonsterAttackCasesTest, SaturatedPoolsPreserveTheCallersOfTheKindAndSkillCases)
{
    const std::vector<Probe> probes{
        {.name = "0x46 en 5", .type = 3, .step = 5, .draws = 2, .kind = std::uint16_t{0x46}},
        {.name = "0x3d en 1", .type = 3, .step = 1, .draws = 2, .kind = std::uint16_t{0x3d}},
        {.name = "0x3f en 1 con '2'",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 341,
         .kind = std::uint16_t{0x3f}},
        {.name = "0x35 en 1", .type = 3, .step = 1, .draws = 338, .kind = std::uint16_t{0x35}},
        {.name = "0x3a en 1", .type = 3, .step = 1, .draws = 338, .kind = std::uint16_t{0x3a}},
        {.name = "0x3b en 1", .type = 3, .step = 1, .draws = 338, .kind = std::uint16_t{0x3b}},
        {.name = "0x31 en 1 con '2'",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 3,
         .kind = std::uint16_t{0x31}},
        {.name = "0x31 en 2 sin habilidad de caso", .type = 3, .step = 2, .draws = 3, .kind = std::uint16_t{0x31}},
        {.name = "0x2a en 1 con '2'",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x42, 0x42},
         .kind = std::uint16_t{0x2a}},
        {.name = "0x23 en 1 con '2'",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 3,
         .sounds = {0x42},
         .kind = std::uint16_t{0x23}},
        {.name = "0x26 en 1 con '2'",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x77, 0x42},
         .kind = std::uint16_t{0x26}},
        {.name = "0x43 alias de 0x26 en 1 con '2'",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x77, 0x42},
         .kind = std::uint16_t{0x43}},
        {.name = "0x2d en 3", .type = 3, .step = 3, .draws = 14, .kind = std::uint16_t{0x2d}},
        {.name = "0x48 en 14 con '2'",
         .type = 3,
         .step = 14,
         .skill = CaseSkill,
         .draws = 111,
         .kind = std::uint16_t{0x48}},
        {.name = "0x47 en la 3 y en 5", .type = 3, .step = 5, .draws = 338, .kind = std::uint16_t{0x47}},
        {.name = "0x12d alias de 0x47 en 5", .type = 3, .step = 5, .draws = 338, .kind = std::uint16_t{0x12d}},
        {.name = "0x4a en la 4 y en 5", .type = 3, .step = 5, .action = 4, .draws = 338, .kind = std::uint16_t{0x4a}},
        {.name = "0x12d alias de 0x4a en 5",
         .type = 3,
         .step = 5,
         .action = 4,
         .draws = 338,
         .kind = std::uint16_t{0x12d}},
        {.name = "0x49 en la 3 y en 11", .type = 3, .step = 11, .draws = 348, .kind = std::uint16_t{0x49}},
        {.name = "0x4b en la 3 y en 11", .type = 3, .step = 11, .draws = 348, .kind = std::uint16_t{0x4b}},
        {.name = "0x4d en 2 con '2'",
         .type = 3,
         .step = 2,
         .skill = CaseSkill,
         .draws = 123,
         .kind = std::uint16_t{0x4d}},
        {.name = "0x4d skill 3 en 8", .type = 3, .step = 8, .skill = 3, .draws = 7, .kind = std::uint16_t{0x4d}},
        {.name = "0x3b en 14 con '2'",
         .type = 3,
         .step = 14,
         .skill = CaseSkill,
         .draws = 3,
         .kind = std::uint16_t{0x3b}},
        {.name = "0x36 en 1", .type = 3, .step = 1, .draws = 2, .kind = std::uint16_t{0x36}},
        {.name = "0x67 en 2 con '2'",
         .type = 3,
         .step = 2,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x42},
         .kind = std::uint16_t{0x67}},
        {.name = "0x3d skill 0x11",
         .type = 3,
         .step = 1,
         .skill = 0x11,
         .draws = 11,
         .sounds = {0x75},
         .kind = std::uint16_t{0x3d}},
        {.name = "0x3d skill 0x11 en 2 sin sonido",
         .type = 3,
         .step = 2,
         .skill = 0x11,
         .draws = 11,
         .kind = std::uint16_t{0x3d}},
        {.name = "0x25 skill 0x11",
         .type = 3,
         .step = 1,
         .skill = 0x11,
         .draws = 5,
         .sounds = {0x75},
         .kind = std::uint16_t{0x25}},
        {.name = "0x25 skill 0x11 en 2 sin sonido",
         .type = 3,
         .step = 2,
         .skill = 0x11,
         .draws = 5,
         .kind = std::uint16_t{0x25}},
        {.name = "0x25 skill 3",
         .type = 3,
         .step = 1,
         .skill = 3,
         .draws = 6,
         .sounds = {0x75},
         .kind = std::uint16_t{0x25}},
        {.name = "0x25 skill 3 en 2 sin sonido",
         .type = 3,
         .step = 2,
         .skill = 3,
         .draws = 6,
         .kind = std::uint16_t{0x25}},
        {.name = "0x2e skill 0x11",
         .type = 3,
         .step = 1,
         .skill = 0x11,
         .draws = 1,
         .sounds = {0x75},
         .kind = std::uint16_t{0x2e}},
        {.name = "0x2e skill 0x11 en 2 sin sonido",
         .type = 3,
         .step = 2,
         .skill = 0x11,
         .draws = 1,
         .kind = std::uint16_t{0x2e}},
        {.name = "0x42 skill 0x11",
         .type = 3,
         .step = 1,
         .skill = 0x11,
         .draws = 2,
         .sounds = {0x50},
         .kind = std::uint16_t{0x42}},
        {.name = "0x42 skill 0x11 en 2 sin sonido",
         .type = 3,
         .step = 2,
         .skill = 0x11,
         .draws = 2,
         .kind = std::uint16_t{0x42}},
        {.name = "0x45 skill 0x11 en 1", .type = 3, .step = 1, .skill = 0x11, .draws = 1, .kind = std::uint16_t{0x45}},
        {.name = "0x59 skill 0x11 en 1", .type = 3, .step = 1, .skill = 0x11, .draws = 2, .kind = std::uint16_t{0x59}},
        {.name = "0x8f skill 0x11 en 1", .type = 3, .step = 1, .skill = 0x11, .draws = 2, .kind = std::uint16_t{0x8f}},
        {.name = "0x22 skill 3", .type = 3, .step = 2, .skill = 3, .draws = 6, .kind = std::uint16_t{0x22}},
        {.name = "0x30 skill 3", .type = 3, .step = 2, .skill = 3, .draws = 8, .kind = std::uint16_t{0x30}},
        {.name = "0x27 skill 3", .type = 3, .step = 2, .skill = 3, .draws = 2, .kind = std::uint16_t{0x27}}};
    Load(0);
    for (const Probe& probe : probes)
        ExpectSaturated(probe);
}

TEST_F(MonsterAttackCasesTest, FullModelPoolSkipsItsEffectsAndContinuesTheCallers)
{
    Load(0);
    const std::vector<Probe> probes{
        {.name = "0x46 en 5", .type = 3, .step = 5, .draws = 2, .kind = std::uint16_t{0x46}},
        {.name = "0x3d skill 0x32 en 1 (cola neutra)",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 2,
         .kind = std::uint16_t{0x3d}},
        {.name = "0x42 skill 0x32 en 1 (cola neutra)",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 3,
         .kind = std::uint16_t{0x42}},
        {.name = "0x31 skill 0x32 en 1",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 3,
         .kind = std::uint16_t{0x31}},
        {.name = "0x2a skill 0x32 en 1",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x42, 0x42},
         .kind = std::uint16_t{0x2a}},
        {.name = "0x23 skill 0x32 en 1",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 3,
         .sounds = {0x42},
         .kind = std::uint16_t{0x23}},
        {.name = "0x26 skill 0x32 en 1",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x77, 0x42},
         .kind = std::uint16_t{0x26}},
        {.name = "0x43 alias skill 0x32 en 1",
         .type = 3,
         .step = 1,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x77, 0x42},
         .kind = std::uint16_t{0x43}},
        {.name = "0x67 skill 0x32 en 2",
         .type = 3,
         .step = 2,
         .skill = CaseSkill,
         .draws = 5,
         .sounds = {0x42},
         .kind = std::uint16_t{0x67}}};
    for (const Probe& probe : probes)
        ExpectWithOnlyModelPoolFull(probe);

    const std::vector<std::uint16_t> oddGroups{0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82, 0x8f};
    for (const std::uint16_t kind : oddGroups)
    {
        SCOPED_TRACE(testing::Message() << "grupo/alias " << kind);
        game->cloth.random = CrtRandom(SeedForSecondRandomParity(true));
        ExpectWithOnlyModelPoolFull(Probe{.name = "skill 0x32, selector impar",
                                          .type = 3,
                                          .step = 1,
                                          .skill = CaseSkill,
                                          .draws = 6,
                                          .sounds = {0x42},
                                          .kind = kind});
    }
}

TEST_F(MonsterAttackCasesTest, FullJointPoolSkipsItsEffectsAndContinuesTheCallers)
{
    Load(0);
    const std::vector<Probe> probes{{.name = "0x48 skill 0x32 en 14",
                                     .type = 3,
                                     .step = 14,
                                     .skill = CaseSkill,
                                     .draws = 111,
                                     .kind = std::uint16_t{0x48}},
                                    {.name = "0x4d skill 0x32 en 2",
                                     .type = 3,
                                     .step = 2,
                                     .skill = CaseSkill,
                                     .draws = 123,
                                     .kind = std::uint16_t{0x4d}}};
    for (const Probe& probe : probes)
        ExpectWithOnlyJointPoolFull(probe);
}

TEST_F(MonsterAttackCasesTest, AFreeJointAfterTheFullModelPoolKeepsTheJointCutBoundary)
{
    Load(0);
    const std::vector<Probe> probes{{.name = "0x48, primer joint",
                                     .type = 3,
                                     .step = 14,
                                     .skill = CaseSkill,
                                     .cut = "0x4f756a",
                                     .draws = 4,
                                     .kind = std::uint16_t{0x48}},
                                    {.name = "0x4d, primer joint",
                                     .type = 3,
                                     .step = 2,
                                     .skill = CaseSkill,
                                     .cut = "0x4f7f3c",
                                     .draws = 4,
                                     .kind = std::uint16_t{0x4d}}};
    for (const Probe& probe : probes)
    {
        SCOPED_TRACE(probe.name);
        ActorState& monster = Prepare(probe);
        SetPoolAvailability(true, false, false);
        const auto stepped = Step(monster);
        EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with(probe.cut)) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        ASSERT_EQ(game->joints.size(), 1u);
        EXPECT_FALSE(game->joints.front().alive);
    }
}

TEST_F(MonsterAttackCasesTest, AFullModelPoolReachesTheNextTailJointCall)
{
    struct Case
    {
        Probe probe;
        std::string_view nextJointCut;
        int draws;
        std::vector<std::int32_t> expectedSounds;
    };
    const std::array cases{Case{Probe{.name = "grupo 0x59, skill 0x11, paso 14",
                                      .type = 3,
                                      .step = 14,
                                      .skill = 0x11,
                                      .target = 0,
                                      .kind = std::uint16_t{0x59}},
                                "0x4f834c",
                                2,
                                {}},
                           Case{Probe{.name = "0x4d, skill 0x11, paso 14",
                                      .type = 3,
                                      .step = 14,
                                      .skill = 0x11,
                                      .target = 0,
                                      .kind = std::uint16_t{0x4d}},
                                "0x4f8484",
                                2,
                                {}},
                           Case{Probe{.name = "0x49, acción 4, skill 0x11, paso 13",
                                      .type = 3,
                                      .step = 13,
                                      .action = 4,
                                      .skill = 0x11,
                                      .target = 0,
                                      .kind = std::uint16_t{0x49}},
                                "0x4f8553",
                                1,
                                {0x42}},
                           Case{Probe{.name = "0x4b, acción 4, skill 0x11, paso 13",
                                      .type = 3,
                                      .step = 13,
                                      .action = 4,
                                      .skill = 0x11,
                                      .target = 0,
                                      .kind = std::uint16_t{0x4b}},
                                "0x4f8553",
                                1,
                                {0x42}}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.probe.name);
        Load(0);
        ActorState& monster = Prepare(probe.probe);
        constexpr glm::vec3 UntouchedTrail(-1.0f);
        monster.trailOrigin = UntouchedTrail;
        monster.bodyPose.assign(16, Client::Render::Models::BoneTransform(1.0f));
        game->world.PlaceModel(
            static_cast<std::uint16_t>(monster.type),
            Client::Scenes::SharedModelPlacement{.scale = monster.scale, .origin = monster.position});
        SetPoolAvailability(true, false, false);

        const auto stepped = Step(monster);

        EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with(probe.nextJointCut)) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.expectedSounds);
        if (!probe.expectedSounds.empty())
        {
            EXPECT_NE(monster.trailOrigin, UntouchedTrail);
            EXPECT_EQ(soundDraws, std::vector<int>{0});
        }
        ASSERT_EQ(game->joints.size(), 1u);
        EXPECT_FALSE(game->joints.front().alive);
    }
}

TEST_F(MonsterAttackCasesTest, FullJointPoolsStillCreateTheSupportedFireParticlesFromSharedBonePoses)
{
    struct Case
    {
        std::uint16_t kind;
        std::uint8_t step;
        std::uint8_t skill;
        int draws;
        std::optional<std::int32_t> sound;
        int soundDraw;
        bool alternatingHands;
    };
    constexpr std::array cases{
        Case{0x3d, 1, 0x11, 23, 0x75, 7, false},     Case{0x25, 1, 0x11, 17, 0x75, 1, false},
        Case{0x42, 1, 0x11, 14, 0x50, 2, true},      Case{0x25, 2, 3, 18, std::nullopt, 0, false},
        Case{0x59, 2, 3, 15, std::nullopt, 0, true},
    };
    constexpr std::size_t Bones = 8;
    const glm::vec3 ModelOrigin(10.0f, 20.0f, 30.0f);
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << "kind " << probe.kind);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "cola con 5d9/0 y joints llenos",
                                            .type = 3,
                                            .step = probe.step,
                                            .skill = probe.skill,
                                            .target = 0,
                                            .kind = probe.kind});
        monster.position = {100.0f, 200.0f, 300.0f};
        monster.scale = 4.0f;
        monster.angles = {13.0f, 27.0f, 41.0f};
        monster.light = {0.2f, 0.3f, 0.4f};
        monster.hands[0].bone = 2;
        monster.hands[1].bone = 3;
        monster.bodyPose.assign(Bones, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < Bones; ++bone)
            monster.bodyPose[bone][3] =
                glm::vec3(static_cast<float>(bone), 2.0f * static_cast<float>(bone), 30.0f + static_cast<float>(bone));
        game->world.PlaceModel(static_cast<std::uint16_t>(monster.type),
                               Client::Scenes::SharedModelPlacement{.scale = 2.0f, .origin = ModelOrigin});
        SetPoolAvailability(true, false, true);
        CrtRandom expectedRandom(game->cloth.random.State());
        expectedRandom.Discard(static_cast<std::uint32_t>(probe.draws));

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(game->cloth.random.State(), expectedRandom.State());
        EXPECT_EQ(sounds, probe.sound ? std::vector<std::int32_t>{*probe.sound} : std::vector<std::int32_t>{});
        EXPECT_EQ(soundDraws, probe.sound ? std::vector<int>{probe.soundDraw} : std::vector<int>{});
        std::vector<const Client::World::Particle*> fires;
        for (const Client::World::Particle& particle : game->particles.All())
        {
            if (particle.alive && particle.type == Client::World::FireParticle)
                fires.push_back(&particle);
        }
        ASSERT_EQ(fires.size(), 4u);
        const std::array<std::size_t, 4> bones =
            probe.alternatingHands ? std::array<std::size_t, 4>{2, 3, 2, 3} : std::array<std::size_t, 4>{2, 2, 3, 3};
        for (std::size_t index = 0; index < fires.size(); ++index)
        {
            const auto& fire = *fires[index];
            const glm::vec3 expectedPosition = Client::Render::Models::OriginalTransformPosition(
                Client::Render::Models::OriginalMatrixOf(monster.bodyPose[bones[index]]), glm::vec3(0.0f), 2.0f,
                ModelOrigin);
            EXPECT_EQ(fire.subtype, 0);
            EXPECT_EQ(fire.position, expectedPosition);
            EXPECT_EQ(fire.angles, monster.angles);
            EXPECT_EQ(fire.light, monster.light);
            EXPECT_GT(fire.life, 0);
            EXPECT_GT(fire.scale, 0.0f);
            EXPECT_EQ(fire.owner, 0u);
        }
    }
}

TEST_F(MonsterAttackCasesTest, FullJointPoolsStillCreateTheSupportedThunderParticles)
{
    constexpr std::size_t Bones = 8;
    const glm::vec3 ModelOrigin(10.0f, 20.0f, 30.0f);
    Load(0);
    ActorState& monster = Prepare(Probe{.name = "skill 3, tipo 0x22, partículas 5ca/0",
                                        .type = 3,
                                        .step = 2,
                                        .skill = 3,
                                        .target = 0,
                                        .kind = std::uint16_t{0x22}});
    monster.position = {100.0f, 200.0f, 300.0f};
    monster.scale = 4.0f;
    monster.angles = {13.0f, 27.0f, 41.0f};
    monster.hands[0].bone = 2;
    monster.hands[1].bone = 3;
    monster.bodyPose.assign(Bones, Client::Render::Models::BoneTransform(1.0f));
    for (std::size_t bone = 0; bone < Bones; ++bone)
        monster.bodyPose[bone][3] =
            glm::vec3(static_cast<float>(bone), 2.0f * static_cast<float>(bone), 30.0f + static_cast<float>(bone));
    game->world.PlaceModel(static_cast<std::uint16_t>(monster.type),
                           Client::Scenes::SharedModelPlacement{.scale = 2.0f, .origin = ModelOrigin});
    SetPoolAvailability(true, false, true);
    CrtRandom expectedRandom(game->cloth.random.State());
    expectedRandom.Discard(14);

    const auto stepped = Step(monster);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Draws(), 14);
    EXPECT_EQ(game->cloth.random.State(), expectedRandom.State());
    EXPECT_TRUE(sounds.empty());
    const auto particles = game->particles.All();
    ASSERT_EQ(std::count_if(particles.begin(), particles.end(), [](const auto& particle)
                            { return particle.alive && particle.type == Client::World::ThunderParticle; }),
              4);
    const std::array<std::size_t, 4> bones{2, 2, 3, 3};
    for (std::size_t index = 0; index < bones.size(); ++index)
    {
        const auto& thunder = particles[index];
        const glm::vec3 expectedPosition = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[bones[index]]), glm::vec3(0.0f), 2.0f,
            ModelOrigin);
        EXPECT_EQ(thunder.type, Client::World::ThunderParticle);
        EXPECT_EQ(thunder.subtype, 0);
        EXPECT_EQ(thunder.position, expectedPosition);
        EXPECT_EQ(thunder.angles, monster.angles);
        EXPECT_EQ(thunder.light, glm::vec3(1.0f));
        EXPECT_GT(thunder.life, 0);
        EXPECT_FLOAT_EQ(thunder.verticalSpeed, 20.0f);
        EXPECT_GT(thunder.scale, 0.0f);
    }
}

TEST_F(MonsterAttackCasesTest, ThunderSkillThreeUsesItsNativeBoneOffsetAndLocalLight)
{
    constexpr std::size_t Bones = 2;
    const glm::vec3 ModelOrigin(10.0f, 20.0f, 30.0f);
    const glm::vec3 ActorLight(0.2f, 0.3f, 0.4f);
    const glm::vec3 LocalLight(1.0f);
    struct Case
    {
        std::int32_t type;
        std::optional<std::uint16_t> model;
        std::optional<std::uint16_t> kind;
        glm::vec3 offset;
    };
    const std::array cases{
        Case{3, std::nullopt, std::uint16_t{0x27}, {0.0f, -130.0f, 0.0f}},
        Case{3, Client::Data::Models::PlayerSkeletonModel, std::nullopt, {0.0f, 0.0f, 0.0f}},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.type);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "skill 3, rayo 5ca/0",
                                            .type = probe.type,
                                            .step = 2,
                                            .skill = 3,
                                            .target = 0,
                                            .kind = probe.kind,
                                            .model = probe.model});
        monster.position = {100.0f, 200.0f, 300.0f};
        monster.scale = 4.0f;
        monster.light = ActorLight;
        monster.hands[0].bone = 0;
        monster.bodyPose.assign(Bones, Client::Render::Models::BoneTransform(1.0f));
        monster.bodyPose[0][3] = glm::vec3(1.0f, 2.0f, 3.0f);
        game->world.PlaceModel(static_cast<std::uint16_t>(monster.type),
                               Client::Scenes::SharedModelPlacement{.scale = 2.0f, .origin = ModelOrigin});
        SetPoolAvailability(true, false, true);

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 4);
        EXPECT_TRUE(sounds.empty());
        const auto particles = game->particles.All();
        const auto thunder =
            std::find_if(particles.begin(), particles.end(), [](const auto& particle)
                         { return particle.alive && particle.type == Client::World::ThunderParticle; });
        ASSERT_NE(thunder, particles.end());
        const glm::vec3 expectedPosition = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[0]), probe.offset, 2.0f, ModelOrigin);
        EXPECT_EQ(thunder->position, expectedPosition);
        EXPECT_EQ(thunder->light, LocalLight);
        EXPECT_EQ(monster.light, ActorLight);
        EXPECT_EQ(thunder->angles, monster.angles);
    }
}

TEST_F(MonsterAttackCasesTest, PartialParticlePoolsKeepFireCallerAndConstructorRandomInterleaved)
{
    constexpr int TailSkillReads = 2;
    constexpr int FireAttempts = 4;
    for (const std::size_t freeSlots : {1u, 2u, 3u})
    {
        SCOPED_TRACE(freeSlots);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "skill 3, fuego 0x25, pool parcial",
                                            .type = 3,
                                            .step = 2,
                                            .skill = 3,
                                            .target = 0,
                                            .kind = std::uint16_t{0x25}});
        monster.hands[0].bone = 0;
        monster.hands[1].bone = 1;
        monster.bodyPose.assign(2, Client::Render::Models::BoneTransform(1.0f));
        SetPoolAvailability(true, true, true);
        SetFreeParticleSlots(freeSlots);

        CrtRandom expectedRandom(game->cloth.random.State());
        for (int read = 0; read < TailSkillReads; ++read)
            static_cast<void>(expectedRandom.Next());
        std::vector<float> expectedRotations;
        for (int attempt = 0; attempt < FireAttempts; ++attempt)
        {
            static_cast<void>(expectedRandom.Next());
            if (static_cast<std::size_t>(attempt) < freeSlots)
            {
                static_cast<void>(expectedRandom.Next());
                static_cast<void>(expectedRandom.Next());
                expectedRotations.push_back(static_cast<float>(expectedRandom.Next() % 360));
            }
        }

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), TailSkillReads + FireAttempts + 3 * static_cast<int>(freeSlots));
        EXPECT_EQ(game->cloth.random.State(), expectedRandom.State());
        std::vector<float> rotations;
        for (const Client::World::Particle& particle : game->particles.All())
        {
            if (particle.alive && particle.type == Client::World::FireParticle)
                rotations.push_back(particle.rotation);
        }
        EXPECT_EQ(rotations, expectedRotations);
        EXPECT_TRUE(sounds.empty());
    }
}

TEST_F(MonsterAttackCasesTest, TailCasesWithoutParticlesContinueWhenTheirJointPoolIsFull)
{
    const std::vector<Probe> probes{
        {.name = "0x45, skill 0x11", .type = 3, .step = 1, .skill = 0x11, .draws = 1, .kind = std::uint16_t{0x45}},
        {.name = "0x2e, skill 0x11",
         .type = 3,
         .step = 1,
         .skill = 0x11,
         .draws = 1,
         .sounds = {0x75},
         .kind = std::uint16_t{0x2e}},
        {.name = "0x30, skill 3", .type = 3, .step = 2, .skill = 3, .draws = 8, .kind = std::uint16_t{0x30}},
        {.name = "0x4d, skill 3, desde 8", .type = 3, .step = 8, .skill = 3, .draws = 7, .kind = std::uint16_t{0x4d}}};
    withParticles = false;
    for (const Probe& probe : probes)
    {
        SCOPED_TRACE(probe.name);
        Load(0);
        ActorState& monster = Prepare(probe);
        SetPoolAvailability(false, false, true);
        const auto stepped = Step(monster);
        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(sounds, probe.sounds);
        EXPECT_TRUE(std::none_of(game->particles.All().begin(), game->particles.All().end(),
                                 [](const auto& particle) { return particle.alive; }));
    }
    withParticles = true;
}

TEST_F(MonsterAttackCasesTest, AFullJointPoolReachesTheMissingFireParticleAfterItsNativePrefix)
{
    Load(0);
    ActorState& monster = Prepare(Probe{.name = "0x3d, skill 0x11, sin partículas",
                                        .type = 3,
                                        .step = 1,
                                        .skill = 0x11,
                                        .target = 0,
                                        .kind = std::uint16_t{0x3d}});
    SetPoolAvailability(true, false, true);
    withParticles = false;

    const auto stepped = Step(monster);

    EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x4f874f")) << stepped.Error();
    EXPECT_EQ(Draws(), 8);
    EXPECT_EQ(sounds, std::vector<std::int32_t>{0x75});
    EXPECT_EQ(soundDraws, std::vector<int>{7});
    withParticles = true;
}

TEST_F(MonsterAttackCasesTest, AFullJointPoolReachesTheMissingThunderParticleAfterItsNativePrefix)
{
    Load(0);
    ActorState& monster = Prepare(Probe{.name = "0x22, skill 3, sin partículas",
                                        .type = 3,
                                        .step = 2,
                                        .skill = 3,
                                        .target = 0,
                                        .kind = std::uint16_t{0x22}});
    SetPoolAvailability(true, false, true);
    withParticles = false;

    const auto stepped = Step(monster);

    EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x4f8dab")) << stepped.Error();
    EXPECT_EQ(Draws(), 3);
    EXPECT_TRUE(sounds.empty());
    withParticles = true;
}

TEST_F(MonsterAttackCasesTest, SaturatedSkillTailsCoverTheFiniteStepCases)
{
    const std::vector<std::uint16_t> oddGroups{0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82, 0x8f};
    Load(0);
    for (const std::uint16_t kind : oddGroups)
    {
        SCOPED_TRACE(testing::Message() << "grupo " << kind);
        ExpectSaturated(
            Probe{.name = "skill 0x11 del grupo en 1", .type = 3, .step = 1, .skill = 0x11, .draws = 2, .kind = kind});
        ExpectSaturated(Probe{.name = "skill 3 del grupo en 1",
                              .type = 3,
                              .step = 1,
                              .skill = 3,
                              .draws = 3,
                              .sounds = {0x50},
                              .kind = kind});
    }
    for (const std::uint16_t kind : {std::uint16_t{0x57}, std::uint16_t{0x5d}, std::uint16_t{0x63}, std::uint16_t{0x74},
                                     std::uint16_t{0x7a}, std::uint16_t{0x80}, std::uint16_t{0x8d}})
    {
        ExpectSaturated(
            Probe{.name = "skill 0x11 del alias en 1", .type = 3, .step = 1, .skill = 0x11, .draws = 1, .kind = kind});
    }
    for (const std::uint16_t kind : {std::uint16_t{0x49}, std::uint16_t{0x4b}, std::uint16_t{0x4d}})
    {
        SCOPED_TRACE(testing::Message() << "tipo " << kind);
        const int draws = kind == 0x4d ? 2 : 1;
        ExpectSaturated(Probe{
            .name = "skill 0x11 sin RNG extra", .type = 3, .step = 2, .skill = 0x11, .draws = draws, .kind = kind});
    }
    for (const std::uint16_t kind : {std::uint16_t{0xa3}, std::uint16_t{0xa5}, std::uint16_t{0xa7}, std::uint16_t{0xa9},
                                     std::uint16_t{0xab}, std::uint16_t{0xad}})
    {
        SCOPED_TRACE(testing::Message() << "tipo " << kind);
        ExpectSaturated(Probe{.name = "skill 0x11 acción 4 paso 8 sin arco",
                              .type = 3,
                              .step = 8,
                              .action = 4,
                              .skill = 0x11,
                              .draws = 2,
                              .kind = kind,
                              .right = std::uint16_t{0xc17}});
    }
    for (const std::uint16_t kind : {std::uint16_t{0xa3}, std::uint16_t{0xa5}, std::uint16_t{0xa7}, std::uint16_t{0xa9},
                                     std::uint16_t{0xab}, std::uint16_t{0xad}})
    {
        ExpectSaturated(Probe{.name = "skill 0x11 acción 3 paso 15",
                              .type = 3,
                              .step = 15,
                              .action = 3,
                              .skill = 0x11,
                              .draws = 1,
                              .kind = kind,
                              .right = std::uint16_t{0xc17}});
    }
    for (const std::size_t hero : {std::size_t{0}, std::size_t{399}})
    {
        game->world.Runtime().hero = hero;
        for (const std::uint8_t step : {std::uint8_t{7}, std::uint8_t{8}})
        {
            const int draws = step == 8 ? (hero == 0 ? 3879 : 31) : 1;
            ExpectSaturated(Probe{.name = "0xa25 fallback de flecha",
                                  .type = 3,
                                  .step = step,
                                  .skill = 0x11,
                                  .draws = draws,
                                  .kind = std::uint16_t{0xa3},
                                  .right = std::uint16_t{0xa25}});
        }
    }
}

TEST_F(MonsterAttackCasesTest, SaturatedSoundsKeepTheirNativeRngOrder)
{
    const std::vector<Probe> probes{{.name = "0x3d sonido después de seis RNG",
                                     .type = 3,
                                     .step = 1,
                                     .skill = 0x11,
                                     .draws = 11,
                                     .sounds = {0x75},
                                     .kind = std::uint16_t{0x3d}},
                                    {.name = "0x25 sonido después de L",
                                     .type = 3,
                                     .step = 1,
                                     .skill = 0x11,
                                     .draws = 5,
                                     .sounds = {0x75},
                                     .kind = std::uint16_t{0x25}},
                                    {.name = "0x25 skill 3 sonido después de dos L",
                                     .type = 3,
                                     .step = 1,
                                     .skill = 3,
                                     .draws = 6,
                                     .sounds = {0x75},
                                     .kind = std::uint16_t{0x25}}};
    const std::vector<int> expectedSoundDraws{7, 1, 2};
    Load(0);
    for (std::size_t index = 0; index < probes.size(); ++index)
    {
        ExpectSaturated(probes[index]);
        EXPECT_EQ(soundDraws, std::vector<int>{expectedSoundDraws[index]});
    }
}

TEST_F(MonsterAttackCasesTest, Saturated49ActionFourStoresTheBoneTrailBeforeTheSoundAndTail)
{
    constexpr std::size_t TrailBone = 11;
    constexpr glm::vec3 Untouched(-1.0f);
    for (const std::uint16_t kind : {std::uint16_t{0x49}, std::uint16_t{0x4b}})
    {
        SCOPED_TRACE(kind);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "paso 13 acción 4",
                                            .type = 3,
                                            .step = 13,
                                            .action = 4,
                                            .skill = 0,
                                            .draws = 2,
                                            .sounds = {0x42},
                                            .kind = kind});
        monster.bodyPose.assign(16, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < monster.bodyPose.size(); ++bone)
            monster.bodyPose[bone][3] = glm::vec3(static_cast<float>(bone), 2.0f * static_cast<float>(bone), 30.0f);
        monster.trailOrigin = Untouched;
        game->world.PlaceModel(
            static_cast<std::uint16_t>(monster.type),
            Client::Scenes::SharedModelPlacement{.scale = monster.scale, .origin = monster.position});
        const glm::vec3 expectedTrail = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[TrailBone]), glm::vec3(-50.0f, 100.0f, 0.0f),
            monster.scale, monster.position);
        SaturatePools();

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 2);
        EXPECT_EQ(soundDraws, std::vector<int>{0});
        EXPECT_NE(monster.trailOrigin, Untouched);
        EXPECT_EQ(monster.trailOrigin, expectedTrail);
        EXPECT_EQ(monster.trailOrigin, Client::Render::Models::OriginalTransformPosition(
                                           Client::Render::Models::OriginalMatrixOf(monster.bodyPose[TrailBone]),
                                           glm::vec3(-50.0f, 100.0f, 0.0f), monster.scale, monster.position));
    }
}

TEST_F(MonsterAttackCasesTest, SaturatedOddGroupPreservesBothSelectorPaths)
{
    for (const std::uint16_t kind : {std::uint16_t{0x59}, std::uint16_t{0x5f}, std::uint16_t{0x70}, std::uint16_t{0x76},
                                     std::uint16_t{0x7c}, std::uint16_t{0x82}, std::uint16_t{0x8f}})
    {
        for (const bool odd : {false, true})
        {
            SCOPED_TRACE(testing::Message() << "tipo " << kind << ", impar " << odd);
            Load(0);
            const Probe probe{.name = "grupo con '2'",
                              .type = 3,
                              .step = 2,
                              .skill = CaseSkill,
                              .draws = odd ? 6 : 4,
                              .sounds = odd ? std::vector<std::int32_t>{0x42} : std::vector<std::int32_t>{},
                              .kind = kind};
            ActorState& monster = Prepare(probe);
            game->cloth.random = CrtRandom(SeedForSecondRandomParity(odd));
            SaturatePools();

            const auto stepped = Step(monster);

            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(Draws(), probe.draws);
            EXPECT_EQ(sounds, probe.sounds);
        }
    }
}

TEST_F(MonsterAttackCasesTest, Saturated49AttackKeepsTheFifthTrailPointAndActorPosition)
{
    for (const std::uint16_t kind : {std::uint16_t{0x49}, std::uint16_t{0x4b}})
    {
        SCOPED_TRACE(kind);
        Load(0);
        const Probe probe{.name = "ataque con rastro", .type = 3, .step = 11, .action = 3, .draws = 348, .kind = kind};
        ActorState& monster = Prepare(probe);
        ASSERT_NE(game->world.FaunaResources().Find(static_cast<std::uint16_t>(monster.type)), nullptr);
        const glm::vec3 actorPosition = monster.position;
        CrtRandom expected(game->cloth.random.State());
        expected.Discard(336);
        glm::vec3 expectedTrail{};
        constexpr int OffsetSpan = 1001;
        constexpr int OffsetCenter = 500;
        constexpr float TrailRise = 500.0f;
        for (int iteration = 0; iteration < 5; ++iteration)
        {
            const auto dx = static_cast<std::int32_t>(expected.Next() % OffsetSpan) - OffsetCenter;
            const auto dy = static_cast<std::int32_t>(expected.Next() % OffsetSpan) - OffsetCenter;
            expectedTrail = {static_cast<float>(static_cast<double>(actorPosition.x) + static_cast<double>(dx)),
                             static_cast<float>(static_cast<double>(actorPosition.y) + static_cast<double>(dy)),
                             static_cast<float>(static_cast<double>(actorPosition.z) + static_cast<double>(TrailRise))};
        }
        expected.Discard(Tail);
        SaturatePools();

        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), probe.draws);
        EXPECT_EQ(monster.position, actorPosition);
        EXPECT_EQ(monster.trailOrigin, expectedTrail);
        EXPECT_EQ(sounds, probe.sounds);
    }
}

// 0x4f75b5 (0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82 y 0x8f): con '2', rand() & 1; con 1, dos rand() más y el 0xc2
// (0x4f7930); con 0, en 1 el 0xf6/1 (0x4f789b, CL11) y la cola.
TEST_F(MonsterAttackCasesTest, TheOddDrawPicksTheEffectOfTheGroup)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        for (const std::int32_t type : {0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82, 0x8f})
        {
            for (const std::uint8_t step : {std::uint8_t{1}, std::uint8_t{2}})
            {
                SCOPED_TRACE(type);
                ActorState& monster = Prepare(Probe{.name = "grupo", .type = type, .step = step, .skill = CaseSkill});
                CrtRandom expected(game->cloth.random.State());
                expected.Discard(SkillRead);
                const bool odd = (expected.Next() & 1) != 0;
                const auto stepped = Step(monster);
                if (odd)
                {
                    EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x4f7930"));
                    EXPECT_EQ(Draws(), SkillRead + 1 + 2);
                }
                else if (step == 1)
                {
                    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
                    EXPECT_EQ(Draws(), SkillRead + 1 + Tail);
                    const auto effects = game->world.Map().effects.Effects();
                    const auto inferno =
                        std::find_if(effects.begin(), effects.end(), [](const auto& effect) { return effect.alive; });
                    ASSERT_NE(inferno, effects.end());
                    EXPECT_EQ(inferno->type, 0xf6);
                    EXPECT_EQ(inferno->subtype, 1);
                    EXPECT_EQ(inferno->transform.position, monster.position);
                    EXPECT_EQ(inferno->ownerContext, nullptr);
                    EXPECT_EQ(inferno->life, 35);
                    inferno->alive = false;
                }
                else
                {
                    EXPECT_TRUE(stepped.HasValue());
                    EXPECT_EQ(Draws(), SkillRead + 1 + Tail);
                }
            }
        }
    }
}

// 0x4f715a (+0x376 0x2d), en cada paso: cuatro veces tres rand() & 31 − 16, el punto del hueso 2 (sin huesos propios,
// la matriz en cero: la posición, Q-C318) y las partículas 0x607/0 (dos rand()) y 0x5e4 (cuatro). Después, la cola.
// Sin el pool corta en la primera, después de las tres del offset.
TEST_F(MonsterAttackCasesTest, TheBleedingMonsterDropsFourPairs)
{
    constexpr int Pairs = 4;
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        ActorState& monster = Prepare(Probe{.name = "0x2d", .type = 0x2d, .step = 3});
        game->world.PlaceModel(monster);
        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), Pairs * (3 + 2 + 4) + Tail);
        int drops = 0;
        int blood = 0;
        for (const Client::World::Particle& particle : game->particles.All())
        {
            if (!particle.alive)
                continue;
            EXPECT_EQ(particle.position, monster.position);
            // La sangre pisa el giro x con rand() % 360 (0x542b3b); la gota deja los ángulos del pedido.
            EXPECT_EQ(particle.angles.y, monster.angles.y);
            EXPECT_EQ(particle.angles.z, monster.angles.z);
            if (particle.type == Client::World::DropParticle)
            {
                EXPECT_EQ(particle.angles.x, monster.angles.x);
            }
            drops += particle.type == Client::World::DropParticle ? 1 : 0;
            blood += particle.type == Client::World::BloodParticle ? 1 : 0;
        }
        EXPECT_EQ(drops, Pairs);
        EXPECT_EQ(blood, Pairs);

        withParticles = false;
        ActorState& dry = Prepare(Probe{.name = "0x2d sin pool", .type = 0x2d, .step = 3});
        game->world.PlaceModel(dry);
        const auto cut = Step(dry);
        EXPECT_TRUE(!cut.HasValue() && cut.Error().starts_with("0x4f71ed"));
        EXPECT_EQ(Draws(), 3);
        withParticles = true;
    }
}

// 0x4f7ab9 (+0x376 0x49 y 0x4b fuera de la 3, en 13): el punto (−50, 100, 0) del hueso 11 de los huesos propios va a
// +0x17c..+0x184 antes del efecto 0x106/1 (0x4f80cf, CL11), que sale de ahí con c de dueño y 45 más en la x de los
// ángulos; después, el sonido 0x42 y la cola.
TEST_F(MonsterAttackCasesTest, TheTrailStartsAtTheEleventhBone)
{
    constexpr std::size_t Bones = 16;
    constexpr std::size_t TrailBone = 11;
    const glm::vec3 untouched(-1.0f);
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        ActorState& monster =
            Prepare(Probe{.name = "0x49", .type = 0x49, .step = 13, .action = 4, .joints = JointTable});
        monster.bodyPose.assign(Bones, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < Bones; ++bone)
            monster.bodyPose[bone][3] = glm::vec3(static_cast<float>(bone), 2.0f * static_cast<float>(bone), 30.0f);
        monster.trailOrigin = untouched;
        game->world.PlaceModel(
            static_cast<std::uint16_t>(monster.type),
            Client::Scenes::SharedModelPlacement{.scale = monster.scale, .origin = monster.position});

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const glm::vec3 expected = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[TrailBone]), glm::vec3(-50.0f, 100.0f, 0.0f),
            monster.scale, monster.position);
        EXPECT_EQ(monster.trailOrigin, expected);
        EXPECT_NE(monster.trailOrigin, monster.position);
        EXPECT_EQ(Draws(), Tail);
        EXPECT_EQ(sounds, (std::vector<std::int32_t>{StabSound, 0x42}));
        const auto effects = game->world.Map().effects.Effects();
        const auto stab = std::find_if(effects.begin(), effects.end(),
                                       [](const auto& effect) { return effect.alive && effect.type == 0x106; });
        ASSERT_NE(stab, effects.end());
        EXPECT_EQ(stab->subtype, 1);
        EXPECT_EQ(stab->ownerContext, &monster);
        EXPECT_EQ(stab->transform.angles.x, monster.angles.x + 45.0f);
        for (auto& effect : effects)
            effect.alive = false;

        // En 9, el mismo 0x4ec110 sin escribir nada.
        ActorState& later = Prepare(Probe{.name = "0x4b en 9", .type = 0x4b, .step = 9, .action = 4});
        later.trailOrigin = untouched;
        ASSERT_TRUE(Step(later).HasValue());
        EXPECT_EQ(later.trailOrigin, untouched);
    }
}

// La cola con 0x11 en +0x38e (0x4f8147): los casos salen siempre, sin la segunda lectura; los sonidos 0x75 y 0x50 en
// 1 salen antes del joint que corta. Los 0x106/1 del lote switch (CL11) piden los joints de su hijo y su sonido
// 0x7a antes del joint 0x614/2 que corta; el 0x4b en la 4 y en 13 llega al 0x4f8493 después del 0x106/1 de su caso de
// +0x376 (0x4f80cf) y su sonido 0x42. Los impares de 0xa3 a 0xad sacan en el alta (0x51443f) la derecha: 0xa25 (el
// arco) o 0xc17; acá va escrita. Con el arco, en 8, la flecha de 0x4f816f (0x53e2b0 → 0x53d9a0 con las manos) da el
// 0x111, cuya alta corta en su joint (0x51bf35).
TEST_F(MonsterAttackCasesTest, TheSkill0x11CasesLeaveAfterTheirEffect)
{
    constexpr std::uint16_t Bow = 0xa25;
    constexpr std::uint16_t Staff = 0xc17;
    const std::vector<Probe> probes{
        {.name = "0xa3 con el arco en 8",
         .type = 0xa3,
         .step = 8,
         .skill = 0x11,
         .cut = "0x51bf35",
         .draws = SkillRead,
         .right = Bow},
        {.name = "0xa9 con el arco en 9", .type = 0xa9, .step = 9, .skill = 0x11, .draws = SkillRead, .right = Bow},
        {.name = "0xa7 en la 3 y en 15",
         .type = 0xa7,
         .step = 15,
         .skill = 0x11,
         .cut = "0x4f81cc",
         .draws = SkillRead,
         .right = Staff},
        {.name = "0xad en la 3 y en 14", .type = 0xad, .step = 14, .skill = 0x11, .draws = SkillRead, .right = Staff},
        {.name = "0xad en la 4 y en 7",
         .type = 0xad,
         .step = 7,
         .action = 4,
         .skill = 0x11,
         .draws = SkillRead,
         .right = Staff},
        {.name = "0xab con el arco en la 4 y en 8",
         .type = 0xab,
         .step = 8,
         .action = 4,
         .skill = 0x11,
         .cut = "0x51bf35",
         .draws = SkillRead,
         .right = Bow},
        {.name = "0x59 en 14",
         .type = 0x59,
         .step = 14,
         .skill = 0x11,
         .cut = "0x4f834c",
         .draws = 2 * SkillRead,
         .sounds = {StabSound},
         .joints = JointTable},
        {.name = "0x8f en 13", .type = 0x8f, .step = 13, .skill = 0x11, .draws = 2 * SkillRead},
        {.name = "0x57 en 13", .type = 0x57, .step = 13, .skill = 0x11, .cut = "0x4f83e5", .draws = SkillRead},
        {.name = "0x8d en 14", .type = 0x8d, .step = 14, .skill = 0x11, .draws = SkillRead},
        {.name = "0x4d en 14",
         .type = 0x4d,
         .step = 14,
         .skill = 0x11,
         .cut = "0x4f8484",
         .draws = 2 * SkillRead,
         .sounds = {StabSound},
         .joints = JointTable},
        {.name = "0x4b en la 4 y en 13",
         .type = 0x4b,
         .step = 13,
         .action = 4,
         .skill = 0x11,
         .cut = "0x4f8553",
         .draws = SkillRead,
         .sounds = {StabSound, 0x42, StabSound},
         .joints = JointTable},
        {.name = "0x49 en la 4 y en 12", .type = 0x49, .step = 12, .action = 4, .skill = 0x11, .draws = SkillRead},
        {.name = "0x45 en 1", .type = 0x45, .step = 1, .skill = 0x11, .cut = "0x4f8592", .draws = SkillRead},
        {.name = "0x45 en 2", .type = 0x45, .step = 2, .skill = 0x11, .draws = SkillRead},
        {.name = "0x3d en 2",
         .type = 3,
         .step = 2,
         .skill = 0x11,
         .cut = "0x4f8654",
         .draws = SkillRead + 1,
         .kind = std::uint16_t{0x3d}},
        {.name = "0x2e en 1",
         .type = 0x2e,
         .step = 1,
         .skill = 0x11,
         .cut = "0x4f87f8",
         .draws = SkillRead,
         .sounds = {0x75}},
        {.name = "0x2e en 2", .type = 0x2e, .step = 2, .skill = 0x11, .cut = "0x4f87f8", .draws = SkillRead},
        {.name = "0x25 en 1",
         .type = 0x25,
         .step = 1,
         .skill = 0x11,
         .cut = "0x4f88d0",
         .draws = SkillRead + 1,
         .sounds = {0x75}},
        {.name = "0x42 en 1",
         .type = 0x42,
         .step = 1,
         .skill = 0x11,
         .cut = "0x4f89df",
         .draws = 2 * SkillRead,
         .sounds = {0x50}},
        {.name = "sin caso", .type = 3, .step = 1, .skill = 0x11, .draws = SkillRead}};
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        // El héroe lejos del lugar del monstruo: 0x53d9a0 compara o con [0x743a170] y, con el héroe, lee las armas
        // del registro propio.
        game->world.Runtime().hero = HeroAway;
        for (const Probe& probe : probes)
            Expect(probe);
        // Sin el arco 0xa25, en la 4 y en 8: rand() & 1 elige 0xf5 (0x4f8227, CL11), que corta en su joint 0x618/5
        // (0x5193d6), o 0xc2/6 (0x4f8287).
        for (const std::int32_t type : {0xa5, 0xa7, 0xad, 0xa3})
        {
            ActorState& monster = Prepare(Probe{.name = "sin el arco",
                                                .type = type,
                                                .step = 8,
                                                .action = 4,
                                                .skill = 0x11,
                                                .right = Staff,
                                                .joints = JointTable});
            CrtRandom expected(game->cloth.random.State());
            expected.Discard(SkillRead);
            const bool odd = (expected.Next() & 1) != 0;
            const auto stepped = Step(monster);
            EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with(odd ? "0x4f8287" : "0x5193d6"))
                << (stepped.HasValue() ? std::string("sin corte") : stepped.Error());
            EXPECT_EQ(Draws(), SkillRead + 1 + (odd ? 0 : BlastDraws));
        }
    }
}

// La cola con 3 en +0x38e (0x4f8a56): los casos con sus sonidos en 1 y su rand() % 360 antes del joint; sin caso,
// 0x4f8f5b: el hueso de la mano del alta pasa la cuenta del esqueleto de prueba (sin huesos) y sale, sin rand(). Sus
// joints 0x614/0 (0x4f8fef), en test_attack_frame_skills.cpp.
TEST_F(MonsterAttackCasesTest, TheSkill3CasesLeaveAfterTheirJoints)
{
    const std::vector<Probe> probes{
        {.name = "0x59 en 1",
         .type = 0x59,
         .step = 1,
         .skill = 3,
         .cut = "0x4f8b31",
         .draws = SkillRead + Tail,
         .sounds = {0x50}},
        {.name = "0x70 en 2", .type = 0x70, .step = 2, .skill = 3, .cut = "0x4f8b31", .draws = SkillRead + Tail},
        {.name = "0x4d en 8", .type = 0x4d, .step = 8, .skill = 3, .cut = "0x4f8bfb", .draws = SkillRead + Tail + 1},
        {.name = "0x4d en 7", .type = 0x4d, .step = 7, .skill = 3, .draws = SkillRead + Tail},
        {.name = "0x25 en 1",
         .type = 0x25,
         .step = 1,
         .skill = 3,
         .cut = "0x4f8ca6",
         .draws = Tail + 1,
         .sounds = {0x75}},
        {.name = "0x22", .type = 0x22, .step = 2, .skill = 3, .cut = "0x4f8d63", .draws = Tail + 1},
        {.name = "0x30", .type = 0x30, .step = 2, .skill = 3, .cut = "0x4f8e46", .draws = Tail + 1},
        {.name = "0x27", .type = 0x27, .step = 2, .skill = 3, .cut = "0x4f8f07", .draws = Tail}};
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        for (const Probe& probe : probes)
            Expect(probe);

        ActorState& fallback = Prepare(Probe{.name = "skill 3 sin caso",
                                             .type = 3,
                                             .step = 2,
                                             .skill = 3,
                                             .target = 0,
                                             .kind = std::uint16_t{0x2345},
                                             .bone = std::uint8_t{0}});
        ASSERT_NE(game->world.FaunaResources().Find(static_cast<std::uint16_t>(fallback.type)), nullptr);
        ASSERT_NE(game->world.FaunaResources().SkeletonOf(static_cast<std::uint16_t>(fallback.type)), nullptr);
        game->joints.assign(1, Client::Scenes::JointState{});
        const auto stepped = Step(fallback);
        EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x4f8fef")) << stepped.Error();
        EXPECT_EQ(Draws(), Tail);
    }
}

TEST_F(MonsterAttackCasesTest, TheSkill3FallbackUsesLoadedSkeletonMetadataBeforeTheRetainedPose)
{
    Load(0);
    ActorState& monster = Prepare(Probe{.type = 3, .step = 2, .skill = 3, .target = 0, .kind = std::uint16_t{0x2345}});
    const auto* model = game->world.FaunaResources().Find(static_cast<std::uint16_t>(monster.type));
    ASSERT_NE(model, nullptr);
    const auto* skeleton = game->world.FaunaResources().SkeletonOf(static_cast<std::uint16_t>(monster.type));
    ASSERT_NE(skeleton, nullptr);
    ASSERT_LT(skeleton->bones.size() + 1, 256u);
    monster.bodyPose.clear();
    monster.hands[0].bone = static_cast<std::uint8_t>(skeleton->bones.size() + 1);
    EXPECT_LT(monster.bodyPose.size(), monster.hands[0].bone);

    const auto stepped = Step(monster);

    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Draws(), Tail);
}

TEST_F(MonsterAttackCasesTest, SaturatedSkill3FallbackPreservesLogicalDepthAndLeavesAllPoolsAlone)
{
    Load(0);
    ActorState& loaded =
        Prepare(Probe{.name = "skill 3 sin caso", .type = 3, .step = 2, .skill = 3, .kind = std::uint16_t{0x2345}});
    ASSERT_NE(game->world.FaunaResources().Find(static_cast<std::uint16_t>(loaded.type)), nullptr);
    const auto* skeleton = game->world.FaunaResources().SkeletonOf(static_cast<std::uint16_t>(loaded.type));
    ASSERT_NE(skeleton, nullptr);
    ASSERT_GT(skeleton->bones.size(), 0u);
    for (const std::uint8_t logicalDepth : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}})
    {
        SCOPED_TRACE(logicalDepth);
        ActorState& monster =
            Prepare(Probe{.name = "skill 3 sin caso", .type = 3, .step = 2, .skill = 3, .kind = std::uint16_t{0x2345}});
        monster.hands[0].bone = 0;
        monster.byte38eAccesses = logicalDepth;
        SaturatePools();
        const auto stepped = Step(monster);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), logicalDepth == 0 ? Tail : 0);
        EXPECT_EQ(monster.byte38eAccesses, logicalDepth);
        EXPECT_TRUE(std::all_of(game->world.Map().effects.Effects().begin(), game->world.Map().effects.Effects().end(),
                                [](const Client::World::ModelEffect& effect) { return effect.alive; }));
        EXPECT_TRUE(std::all_of(game->particles.All().begin(), game->particles.All().end(),
                                [](const Client::World::Particle& particle) { return particle.alive; }));
        EXPECT_TRUE(std::all_of(game->joints.begin(), game->joints.end(),
                                [](const Client::Scenes::JointState& joint) { return joint.alive; }));
    }
}

// 0x4f7fd2: las E2 ya tienen constructor; un pool lleno omite sólo cada alta y continúa la cola.
TEST_F(MonsterAttackCasesTest, TheAquagoldCasesCreateTheirArrowsAndContinueTheTail)
{
    constexpr int RecordCloseDraws = 0xf08;
    for (const std::uint16_t kind : {std::uint16_t{0x36}, std::uint16_t{0x39}, std::uint16_t{0x97}})
    {
        for (const bool full : {false, true})
        {
            SCOPED_TRACE(testing::Message() << kind << ", lleno " << full);
            Load(0);
            ActorState& monster = Prepare(Probe{.type = 3, .step = 1, .target = 0, .kind = kind});
            monster.angles.z = 100.3f;
            if (full)
                for (Client::World::ModelEffect& effect : game->world.Map().effects.Effects())
                    effect.alive = true;
            const auto stepped = Step(monster);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            const int arrows = kind == 0x39 ? 3 : 1;
            EXPECT_EQ(Draws(), (full ? 0 : arrows * RecordCloseDraws) + Tail);
            if (full)
                continue;
            const auto effects = game->world.Map().effects.Effects();
            const float secondYaw = static_cast<float>(static_cast<double>(100.3f) + 20.0);
            const std::array<float, 3> expectedYaws{100.3f, secondYaw,
                                                    static_cast<float>(static_cast<double>(secondYaw) - 40.0)};
            for (int index = 0; index < arrows; ++index)
            {
                const auto& effect = effects[static_cast<std::size_t>(index)];
                EXPECT_TRUE(effect.alive);
                EXPECT_EQ(effect.type, 0xe2);
                EXPECT_EQ(effect.ownerContext, &monster);
                EXPECT_EQ(effect.disturbanceState, 0xffff);
                EXPECT_EQ(effect.byte89, 0);
                EXPECT_EQ(effect.subtype, 0);
                EXPECT_EQ(effect.life, 40);
                EXPECT_EQ(effect.transform.angles.z, expectedYaws[static_cast<std::size_t>(index)]);
            }
            EXPECT_EQ(monster.angles.z, 100.3f);
        }
    }
}

// 0x51fff0: con modelos llenos, los ocho estallidos y 16 selectores conservan sus sorteos.
TEST_F(MonsterAttackCasesTest, TheScatteredBurstSkipsFullModelPoolsAndKeepsAllCallerDraws)
{
    for (const bool particlesFull : {false, true})
    {
        SCOPED_TRACE(particlesFull);
        Load(0);
        ActorState& monster = Prepare(Probe{.type = 3, .step = 1, .target = NoTarget, .kind = std::uint16_t{0x35}});
        for (Client::World::ModelEffect& effect : game->world.Map().effects.Effects())
            effect.alive = true;
        if (particlesFull)
            for (Client::World::Particle& particle : game->particles.All())
                particle.alive = true;
        const auto stepped = Step(monster);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), particlesFull ? 336 : 1136);
        EXPECT_EQ(sounds.size(), particlesFull ? 0u : 8u);
    }
}

TEST_F(MonsterAttackCasesTest, ACombatBurstWithoutParticlesKeepsItsTwoCallerDrawsBeforeTheDiagnostic)
{
    Load(0);
    ActorState& monster = Prepare(Probe{
        .name = "0x35 en 1 sin pool de partículas", .type = 3, .step = 1, .target = 0, .kind = std::uint16_t{0x35}});
    withParticles = false;

    const auto stepped = Step(monster);

    EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x51fe9e")) << stepped.Error();
    EXPECT_EQ(Draws(), 2);
}

TEST_F(MonsterAttackCasesTest, TheScatteredBurstKeepsItsRngWithAPartialParticlePool)
{
    for (const int freeParticles : {0, 20, 21})
    {
        SCOPED_TRACE(freeParticles);
        Load(0);
        ActorState& monster = Prepare(Probe{.type = 3, .step = 1, .target = NoTarget, .kind = std::uint16_t{0x35}});
        auto particles = game->particles.All();
        for (std::size_t index = 0; index < particles.size(); ++index)
            particles[index].alive = index >= static_cast<std::size_t>(freeParticles);
        const auto stepped = Step(monster);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 408 + 5 * std::min(freeParticles, 20));
        EXPECT_EQ(sounds.size(), freeParticles == 21 ? 1u : 0u);
    }
}

TEST_F(MonsterAttackCasesTest, FullModelPoolContinuesPastScatterHelpersIntoFollowingEffectsAndStores)
{
    struct Case
    {
        std::uint16_t kind;
        std::uint8_t step;
        std::uint8_t action;
        std::uint8_t skill;
        int drawsAfterScatter;
        bool storesFifthTrailPoint;
    };
    constexpr std::array Cases{
        Case{0x3f, 1, 3, CaseSkill, 3, false}, Case{0x35, 1, 3, 0, 0, false}, Case{0x3a, 1, 3, 0, 0, false},
        Case{0x3b, 1, 3, 0, 0, false},         Case{0x47, 5, 3, 0, 0, false}, Case{0x4a, 5, 4, 0, 0, false},
        Case{0x12d, 5, 3, 0, 0, false},        Case{0x49, 11, 3, 0, 0, true}, Case{0x4b, 11, 3, 0, 0, true}};
    constexpr std::size_t ParticlesPerBurst = 20;
    for (const std::size_t freeParticles : {std::size_t{0}, ParticlesPerBurst, ParticlesPerBurst + 1})
    {
        SCOPED_TRACE(testing::Message() << "slots de partículas libres " << freeParticles);
        for (const Case& probe : Cases)
        {
            SCOPED_TRACE(testing::Message() << "tipo " << probe.kind << ", paso " << static_cast<int>(probe.step));
            Load(0);
            ActorState& monster = Prepare(Probe{.name = "continuación del helper scatter",
                                                .type = 3,
                                                .step = probe.step,
                                                .action = probe.action,
                                                .skill = probe.skill,
                                                .target = 0,
                                                .kind = probe.kind});
            constexpr glm::vec3 UntouchedTrail(-1.0f);
            monster.trailOrigin = UntouchedTrail;
            const glm::vec3 actorPosition = monster.position;
            FillModelEffectPoolExceptLast(0);
            SetFreeParticleSlots(freeParticles);
            game->cloth.random = CrtRandom(Seed);

            const std::size_t createdParticles = std::min(freeParticles, ParticlesPerBurst);
            const int scatterDraws = 336 + static_cast<int>(5 * createdParticles);
            const int soundCount = freeParticles == 0 ? 0 : (freeParticles <= ParticlesPerBurst ? 0 : 1);
            CrtRandom expectedRandom(game->cloth.random.State());
            expectedRandom.Discard(static_cast<std::uint32_t>(scatterDraws + probe.drawsAfterScatter));
            glm::vec3 expectedTrail = UntouchedTrail;
            if (probe.storesFifthTrailPoint)
            {
                constexpr int OffsetSpan = 1001;
                constexpr int OffsetCenter = 500;
                constexpr float TrailRise = 500.0f;
                for (int iteration = 0; iteration < 5; ++iteration)
                {
                    const auto dx = static_cast<std::int32_t>(expectedRandom.Next() % OffsetSpan) - OffsetCenter;
                    const auto dy = static_cast<std::int32_t>(expectedRandom.Next() % OffsetSpan) - OffsetCenter;
                    expectedTrail = {
                        static_cast<float>(static_cast<double>(actorPosition.x) + static_cast<double>(dx)),
                        static_cast<float>(static_cast<double>(actorPosition.y) + static_cast<double>(dy)),
                        static_cast<float>(static_cast<double>(actorPosition.z) + static_cast<double>(TrailRise)),
                    };
                }
            }
            expectedRandom.Discard(Tail);

            const auto stepped = Step(monster);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(Draws(), scatterDraws + probe.drawsAfterScatter + (probe.storesFifthTrailPoint ? 10 : 0) + Tail);
            EXPECT_EQ(sounds, std::vector<std::int32_t>(static_cast<std::size_t>(soundCount), 0x40));
            if (soundCount == 0)
                EXPECT_FALSE(game->world.Runtime().lastSoundId.has_value());
            else
                EXPECT_EQ(game->world.Runtime().lastSoundId, 0x40);
            EXPECT_EQ(monster.position, actorPosition);
            EXPECT_EQ(monster.trailOrigin, expectedTrail);
            EXPECT_EQ(game->cloth.random.State(), expectedRandom.State());
            EXPECT_TRUE(std::none_of(game->particles.All().begin(), game->particles.All().end(),
                                     [](const Client::World::Particle& particle) { return !particle.alive; }));
            EXPECT_TRUE(std::all_of(game->world.Map().effects.Effects().begin(),
                                    game->world.Map().effects.Effects().end(),
                                    [](const Client::World::ModelEffect& effect) { return effect.alive; }));
        }
    }
}

TEST_F(MonsterAttackCasesTest, FullModelPoolStoresTheHandTrailThenRequestsTheSoundAndContinues)
{
    constexpr std::size_t TrailBone = 11;
    constexpr std::size_t Bones = 16;
    constexpr glm::vec3 UntouchedTrail(-1.0f);
    for (const std::uint16_t kind : {std::uint16_t{0x49}, std::uint16_t{0x4b}})
    {
        SCOPED_TRACE(kind);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "acción 4, paso 13 con pool de modelos lleno",
                                            .type = 3,
                                            .step = 13,
                                            .action = 4,
                                            .target = 0,
                                            .kind = kind});
        monster.bodyPose.assign(Bones, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < Bones; ++bone)
            monster.bodyPose[bone][3] = glm::vec3(static_cast<float>(bone), 2.0f * static_cast<float>(bone), 30.0f);
        monster.trailOrigin = UntouchedTrail;
        game->world.PlaceModel(
            static_cast<std::uint16_t>(monster.type),
            Client::Scenes::SharedModelPlacement{.scale = monster.scale, .origin = monster.position});
        const glm::vec3 expectedTrail = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[TrailBone]), glm::vec3(-50.0f, 100.0f, 0.0f),
            monster.scale, monster.position);
        FillModelEffectPoolExceptLast(0);

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), Tail);
        EXPECT_EQ(sounds, std::vector<std::int32_t>{0x42});
        EXPECT_EQ(soundDraws, std::vector<int>{0});
        EXPECT_EQ(monster.trailOrigin, expectedTrail);
    }
}

TEST_F(MonsterAttackCasesTest, TheHandTrailUsesThePublishedSharedModelPlacement)
{
    constexpr std::size_t TrailBone = 11;
    constexpr glm::vec3 UntouchedTrail(-1.0f);
    const glm::vec3 PublishedOrigin(10.0f, 20.0f, 30.0f);
    for (const std::uint16_t kind : {std::uint16_t{0x49}, std::uint16_t{0x4b}})
    {
        SCOPED_TRACE(kind);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "acción 4, paso 13, modelo compartido anterior",
                                            .type = 3,
                                            .step = 13,
                                            .action = 4,
                                            .target = NoTarget,
                                            .kind = kind});
        monster.position = {100.0f, 200.0f, 300.0f};
        monster.scale = 4.0f;
        monster.bodyPose.assign(16, Client::Render::Models::BoneTransform(1.0f));
        monster.trailOrigin = UntouchedTrail;
        game->world.PlaceModel(static_cast<std::uint16_t>(monster.type),
                               Client::Scenes::SharedModelPlacement{.scale = 2.0f, .origin = PublishedOrigin});
        FillModelEffectPoolExceptLast(0);

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const glm::vec3 expectedTrail = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[TrailBone]), glm::vec3(-50.0f, 100.0f, 0.0f),
            2.0f, PublishedOrigin);
        EXPECT_EQ(monster.trailOrigin, expectedTrail);
        EXPECT_EQ(monster.position, glm::vec3(100.0f, 200.0f, 300.0f));
        EXPECT_EQ(monster.scale, 4.0f);
        EXPECT_EQ(Draws(), 0);
        EXPECT_EQ(sounds, std::vector<std::int32_t>{0x42});
        EXPECT_EQ(soundDraws, std::vector<int>{0});
        EXPECT_EQ(game->world.ModelPlacement(monster.type).origin, PublishedOrigin);
        EXPECT_FLOAT_EQ(game->world.ModelPlacement(monster.type).scale, 2.0f);
    }
}

TEST_F(MonsterAttackCasesTest, Kind49And4bStepNineUseTheirOrangeTailLightWithoutWritingATrail)
{
    constexpr glm::vec3 UntouchedTrail(-1.0f);
    const glm::vec3 PublishedOrigin(10.0f, 20.0f, 30.0f);
    const glm::vec3 Orange(1.0f, 0.5f, 0.0f);
    for (const std::uint16_t kind : {std::uint16_t{0x49}, std::uint16_t{0x4b}})
    {
        SCOPED_TRACE(kind);
        Load(0);
        ActorState& monster = Prepare(Probe{.name = "acción 4, paso 9, skill 3",
                                            .type = 3,
                                            .step = 9,
                                            .action = 4,
                                            .skill = 3,
                                            .target = 0,
                                            .kind = kind});
        monster.position = {100.0f, 200.0f, 300.0f};
        monster.scale = 4.0f;
        monster.light = {0.2f, 0.3f, 0.4f};
        monster.hands[0].bone = 0;
        monster.bodyPose.assign(1, Client::Render::Models::BoneTransform(1.0f));
        monster.trailOrigin = UntouchedTrail;
        game->world.PlaceModel(static_cast<std::uint16_t>(monster.type),
                               Client::Scenes::SharedModelPlacement{.scale = 2.0f, .origin = PublishedOrigin});
        SetPoolAvailability(true, false, true);

        const auto stepped = Step(monster);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Draws(), 4);
        EXPECT_TRUE(sounds.empty());
        EXPECT_EQ(monster.trailOrigin, UntouchedTrail);
        EXPECT_EQ(monster.position, glm::vec3(100.0f, 200.0f, 300.0f));
        const auto particles = game->particles.All();
        ASSERT_EQ(std::count_if(particles.begin(), particles.end(), [](const auto& particle)
                                { return particle.alive && particle.type == Client::World::ThunderParticle; }),
                  1);
        const auto thunder =
            std::find_if(particles.begin(), particles.end(), [](const auto& particle)
                         { return particle.alive && particle.type == Client::World::ThunderParticle; });
        ASSERT_NE(thunder, particles.end());
        const glm::vec3 expectedPosition = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[0]), glm::vec3(0.0f, -130.0f, 0.0f), 2.0f,
            PublishedOrigin);
        EXPECT_EQ(thunder->position, expectedPosition);
        EXPECT_EQ(thunder->light, Orange);
        EXPECT_EQ(thunder->angles, monster.angles);
        EXPECT_EQ(thunder->subtype, 0);
        EXPECT_GT(thunder->life, 0);
    }
}

// 0x43b5ac/0x43b5cc y 0x43b241/0x43b25e (CL11): el 0xa1 en la 3 desde 13 suelta 0xf6/0 y 0x5de/1, y el 0xa0 0xf6/5 y
// 0x5de/2, sobre c y con c de dueño, y deja +0x381 en 15. Con el pool lleno, los dos se omiten y el 15 queda igual.
TEST_F(MonsterAttackCasesTest, TheKalimaInfernosLeaveTheirOwnerAndEndTheCount)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t inferno;
        std::int32_t jet;
        int draws;
    };
    for (const Case probe : {Case{0xa1, 0, 1, SkillRead}, Case{0xa0, 5, 2, 0}})
    {
        for (const bool full : {false, true})
        {
            SCOPED_TRACE(testing::Message() << probe.type << ", lleno " << full);
            Load(24);
            game->world.Runtime().hero = HeroAway;
            ActorState& monster = Prepare(Probe{.type = probe.type, .step = 13});
            if (full)
                FillModelEffectPoolExceptLast(0);

            const auto stepped = Step(monster);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(monster.byte381, 15);
            EXPECT_EQ(Draws(), probe.draws);
            EXPECT_TRUE(sounds.empty());
            if (full)
                continue;
            const auto effects = game->world.Map().effects.Effects();
            ASSERT_TRUE(effects[0].alive);
            ASSERT_TRUE(effects[1].alive);
            EXPECT_FALSE(effects[2].alive);
            EXPECT_EQ(effects[0].type, 0xf6);
            EXPECT_EQ(effects[0].subtype, probe.inferno);
            EXPECT_EQ(effects[1].type, Client::World::FlameJetEffect);
            EXPECT_EQ(effects[1].subtype, probe.jet);
            for (const Client::World::ModelEffect* effect : {&effects[0], &effects[1]})
            {
                EXPECT_EQ(effect->transform.position, monster.position);
                EXPECT_EQ(effect->ownerContext, &monster);
                EXPECT_EQ(effect->itemType, &monster.word8c);
                EXPECT_EQ(effect->itemLevel, &monster.byte8e);
            }
            EXPECT_EQ(effects[0].light, glm::vec3(0.8f));
            EXPECT_EQ(effects[0].life, 15);
            EXPECT_EQ(effects[1].life, 10);
        }
    }
}

// 0x4f8227 y 0x4f8244 (CL11): con el par, los dos 0xf5 caen sobre el blanco (+0x3a4) con c de dueño; cada alta saca sus
// cinco rand(), omite el joint 0x618/5 con la tabla llena y cierra el registro propio (3.848 rand()).
TEST_F(MonsterAttackCasesTest, TheEvenDrawDropsTwoBlastsOnTheTarget)
{
    constexpr std::uint16_t Staff = 0xc17;
    constexpr std::uint16_t TargetSlot = 7;
    constexpr int OwnRecordDraws = Client::Render::Models::OwnRecordCloseRandoms;
    Load(0);
    game->world.Runtime().hero = HeroAway;
    ActorState& target = game->world.Actors()[TargetSlot];
    target.position = glm::vec3(1234.0f, 2345.0f, 345.0f);
    const std::uint32_t seed = SeedForSecondRandomParity(false);
    ActorState& monster = Prepare(
        Probe{.type = 0xa5, .step = 8, .action = 4, .skill = 0x11, .target = TargetSlot, .right = Staff, .joints = 1});
    game->joints.front().alive = true;
    game->cloth.random = CrtRandom(seed);

    const auto stepped = Step(monster);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Draws(), SkillRead + 1 + 2 * (BlastDraws + OwnRecordDraws));
    const auto effects = game->world.Map().effects.Effects();
    for (std::size_t blast = 0; blast < 2; ++blast)
    {
        SCOPED_TRACE(blast);
        const Client::World::ModelEffect& effect = effects[blast];
        ASSERT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, 0xf5);
        EXPECT_EQ(effect.subtype, 0);
        EXPECT_EQ(effect.ownerContext, &monster);
        EXPECT_EQ(effect.life, 30);
        // 0x51931b..0x51936a: el punto sube 300 a 799 sobre el blanco, con x en [200, 300) y y en [−50, 50).
        EXPECT_GE(effect.transform.position.x, target.position.x + 200.0f);
        EXPECT_LT(effect.transform.position.x, target.position.x + 300.0f);
        EXPECT_GE(effect.transform.position.z, target.position.z + 300.0f);
    }
    EXPECT_FALSE(effects[2].alive);
}

// 0x4f79db..0x4f7aae (CL11): el 0x49 en la 3 y en 11, después del estallido y del 0xf6/0, suelta cinco 0x106/1 desde
// los puntos de +0x17c con c de dueño; cada uno deja su hijo 0x105/1 y sus cuatro joints 0x610/0..3.
TEST_F(MonsterAttackCasesTest, TheFiveStabsFallFromTheTrailPoints)
{
    Load(0);
    ActorState& monster = Prepare(Probe{.type = 0x49, .step = 11, .joints = JointTable});

    const auto stepped = Step(monster);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    int infernos = 0;
    int stabs = 0;
    int children = 0;
    for (const Client::World::ModelEffect& effect : game->world.Map().effects.Effects())
    {
        if (!effect.alive)
            continue;
        infernos += effect.type == 0xf6 && effect.subtype == 0 ? 1 : 0;
        stabs += effect.type == 0x106 && effect.subtype == 1 && effect.ownerContext == &monster ? 1 : 0;
        children += effect.type == 0x105 && effect.subtype == 1 ? 1 : 0;
    }
    EXPECT_EQ(infernos, 1);
    EXPECT_EQ(stabs, 5);
    EXPECT_EQ(children, 5);
    const auto joints = std::count_if(game->joints.begin(), game->joints.end(),
                                      [](const Client::Scenes::JointState& joint) { return joint.alive; });
    EXPECT_EQ(joints, 5 * 4);
    EXPECT_NE(monster.trailOrigin, monster.position);
}
} // namespace
