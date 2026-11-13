#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/MonsterAttackStep.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/Particles.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

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
// Devias (2). Las claves son inventadas y el esqueleto de todos los modelos es el del 0x20a (P-C176). 06, "Los casos de
// los monstruos (0x4f5b70)".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;

constexpr std::uint32_t Seed = 0x0c19;
constexpr std::int32_t MonsterKey = 0x0456;
// Una lectura de +0x38e (0x517630 y sus copias en línea): un rand() al cerrarse en 0.
constexpr int SkillRead = 1;
// La cola con el blanco en el pool, sin 0x11 ni 3 en +0x38e: las lecturas de 0x4f811b y 0x4f8a2c.
constexpr int Tail = 2 * SkillRead;
// '2' en +0x38e, la habilidad de los casos.
constexpr std::uint8_t CaseSkill = 0x32;
constexpr std::uint16_t NoTarget = 0xffff;

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

// El esqueleto del 0x20a para todos los modelos: 0x4ea030 del alta pide sus acciones.
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
};

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
        monster.byte381 = probe.step;
        monster.animation.action = probe.action;
        monster.byte38e = probe.skill;
        monster.byte38eAccesses = 0;
        monster.attackTarget = probe.target;
        return monster;
    }

    Client::Core::Expected<void, std::string> Step(ActorState& monster)
    {
        sounds.clear();
        before = game->cloth.random.State();
        const Client::Scenes::CharacterStepContext context{.world = game->world,
                                                           .random = game->cloth.random,
                                                           .models = &game->models,
                                                           .particles = withParticles ? &game->particles : nullptr,
                                                           .soundRequest = [this](std::int32_t sound)
                                                           { sounds.push_back(sound); }};
        return Client::Scenes::StepTheAttackCases(monster, slot, context);
    }

    [[nodiscard]] int Draws() const
    {
        return DrawsSince(before, game->cloth.random);
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
        {.name = "0x91 en la 4 y en 14", .type = 0x91, .step = 14, .action = 4, .cut = "0x43ab42", .draws = SkillRead},
        {.name = "0x93 en la 4 y en 14", .type = 0x93, .step = 14, .action = 4, .cut = "0x43ac29"},
        {.name = "0x93 en la 3 y en 14", .type = 0x93, .step = 14},
        {.name = "0x94 con 0x11 en 14", .type = 0x94, .step = 14, .skill = 0x11, .cut = "0x43ae45", .draws = SkillRead},
        {.name = "0x94 con 0x11 en 13", .type = 0x94, .step = 13, .skill = 0x11, .draws = SkillRead},
        {.name = "0x95 con 1 en 14", .type = 0x95, .step = 14, .skill = 1, .cut = "0x43b1f4", .draws = SkillRead + 2},
        {.name = "0x95 con 0x11 en 14", .type = 0x95, .step = 14, .skill = 0x11, .cut = "0x43b0ed", .draws = SkillRead},
        {.name = "0x95 con 0x11 en 14 sin blanco",
         .type = 0x95,
         .step = 14,
         .skill = 0x11,
         .draws = SkillRead,
         .target = NoTarget},
        {.name = "0xa0 en la 3 y en 13", .type = 0xa0, .step = 13, .cut = "0x43b241"},
        {.name = "0xa0 en la 4 y en 13", .type = 0xa0, .step = 13, .action = 4},
        {.name = "0xa1 en la 3 y en 7", .type = 0xa1, .step = 7, .cut = "0x43b570", .draws = SkillRead},
        {.name = "0xa1 en la 3 y en 13", .type = 0xa1, .step = 13, .cut = "0x43b5ac", .draws = SkillRead},
        {.name = "0xa1 en la 4 y en 13", .type = 0xa1, .step = 13, .action = 4, .draws = SkillRead},
        {.name = "0x113 con 1 en 13", .type = 0x113, .step = 13, .skill = 1, .cut = "0x439ca6", .draws = SkillRead + 1},
        {.name = "0x113 en la 3 y en 13", .type = 0x113, .step = 13, .draws = SkillRead}};
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{36}})
    {
        SCOPED_TRACE(map);
        Load(map);
        for (const Probe& probe : probes)
            Expect(probe);
    }
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        ExpectTheTailIn(map, {0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0xa0, 0xa1, 0x113});
    }
}

// Las habilidades 0xc9 y 0xca de los tres switch de Kalima (0x43aabf, 0x43b4f4 y 0x43b878): desde 13, +0x78 con 0x100
// o 0x8, +0x381 en 15 y el sonido 0x1e1. Lo que sigue lee el +0x381 nuevo: el 0x91 en la 4 ya no ve el 14 y el 0xa1 en
// la 3 corta desde 13.
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
                                  {"0xa1 con 0xca en 13 y en la 3", 0xa1, 13, 3, 0xca, 0x8, "0x43b5ac"},
                                  {"0x113 con 0xc9 en 14", 0x113, 14, 3, 0xc9, 0x100, ""}};
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{36}})
    {
        SCOPED_TRACE(map);
        Load(map);
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

// 0x42dcf0 en el 30 (el 0x68 sale fuera del 5; el 0x120 corta siempre) y 0x429110 en el 33 y el 54 (en 10 y en su
// acción, el efecto; si no, sale).
TEST_F(MonsterAttackCasesTest, TheSiegeAndTheTrialMonstersLeaveOrCut)
{
    Load(30);
    for (const Probe& probe : std::vector<Probe>{{.name = "0x68 en 4", .type = 0x68, .step = 4},
                                                 {.name = "0x68 en 5", .type = 0x68, .step = 5, .cut = "0x42dd9c"},
                                                 {.name = "0x120", .type = 0x120, .step = 1, .cut = "0x42dd48"}})
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

// 0x42fce0 en el 35 (con el terreno de Lorencia: el Data del repo no tiene el World36) y 0x4324c0 en Crywolf, por el
// tipo: los efectos en 14 (0x1ad y 0x1b2) y en 15 (0x1b5); si no, siguen a la cola. El 0x17c y el 0x17e los crean los
// tipos 0x3a, 0x3b, 0x3e, 0x52, 0x53 y 0x12c, que el alta corta: el tipo y el +0x376 van a mano.
TEST_F(MonsterAttackCasesTest, TheFortressAndCrywolfLookAtTheModel)
{
    Load(35, 0);
    EXPECT_EQ(Create(0x136).type, 0x1ad);
    EXPECT_EQ(Create(0x13c).type, 0x1b2);
    for (const Probe& probe : std::vector<Probe>{{.name = "0x1ad en 14", .type = 0x136, .step = 14, .cut = "0x42fd76"},
                                                 {.name = "0x1ad en 13", .type = 0x136, .step = 13, .draws = Tail},
                                                 {.name = "0x1b2 en 14", .type = 0x13c, .step = 14, .cut = "0x42fd37"}})
        Expect(probe);

    Load(34);
    EXPECT_EQ(Create(0x138).type, 0x1b2);
    EXPECT_EQ(Create(0x13c).type, 0x1ad);
    EXPECT_EQ(Create(0x15c).type, 0x1b5);
    const std::vector<Probe> crywolf{{.name = "0x1b2 en 14", .type = 0x138, .step = 14, .cut = "0x432532"},
                                     {.name = "0x1b2 en 13", .type = 0x138, .step = 13, .draws = Tail},
                                     {.name = "0x1ad en 14", .type = 0x13c, .step = 14, .cut = "0x43257a"},
                                     {.name = "0x1b5 en 15", .type = 0x15c, .step = 15, .cut = "0x432b63"},
                                     {.name = "0x1b5 en 14", .type = 0x15c, .step = 14, .draws = Tail},
                                     {.name = "0x17c en 1",
                                      .type = 3,
                                      .step = 1,
                                      .cut = "0x43259e",
                                      .kind = std::uint16_t{0x3b},
                                      .model = std::uint16_t{0x17c}},
                                     {.name = "0x17c con '2' en 14",
                                      .type = 3,
                                      .step = 14,
                                      .skill = CaseSkill,
                                      .cut = "0x4329db",
                                      .draws = SkillRead,
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

// El switch por +0x376 (0x4f5c7e): cada caso con su +0x381, su acción y '2' en +0x38e, el corte después de sus
// lecturas y rand(), o la cola. En Lorencia y en Devias (los mismos en cualquier mapa fuera de los ayudantes).
TEST_F(MonsterAttackCasesTest, TheKindCasesCutAfterTheirDrawsOrGoOnToTheTail)
{
    const std::vector<Probe> probes{
        {.name = "0x46 en 5", .type = 0x46, .step = 5, .cut = "0x4f5d13"},
        {.name = "0x46 en 5 sin blanco", .type = 0x46, .step = 5, .target = NoTarget},
        {.name = "0x46 en 4", .type = 0x46, .step = 4, .draws = Tail},
        {.name = "0x3d en 1", .type = 3, .step = 1, .cut = "0x4f6145", .kind = std::uint16_t{0x3d}},
        {.name = "0x3d en 2", .type = 3, .step = 2, .draws = Tail, .kind = std::uint16_t{0x3d}},
        {.name = "0x3f en 1", .type = 3, .step = 1, .cut = "0x4f5d3c", .kind = std::uint16_t{0x3f}},
        {.name = "0x3f con '2'",
         .type = 3,
         .step = 2,
         .skill = CaseSkill,
         .cut = "0x4f609d",
         .draws = SkillRead + 2,
         .kind = std::uint16_t{0x3f}},
        {.name = "0x3f en 2", .type = 3, .step = 2, .draws = SkillRead + Tail, .kind = std::uint16_t{0x3f}},
        {.name = "0x42 con '2' en 1",
         .type = 0x42,
         .step = 1,
         .skill = CaseSkill,
         .cut = "0x4f6422",
         .draws = SkillRead},
        {.name = "0x42 con '2' en 2", .type = 0x42, .step = 2, .skill = CaseSkill, .draws = SkillRead + Tail},
        {.name = "0x35 en 1", .type = 3, .step = 1, .cut = "0x4f6450", .kind = std::uint16_t{0x35}},
        {.name = "0x3b con '2' en 14",
         .type = 3,
         .step = 14,
         .skill = CaseSkill,
         .cut = "0x4f6780",
         .draws = SkillRead,
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
        {.name = "0x47 en la 3 y en 5", .type = 0x47, .step = 5, .cut = "0x4f7962"},
        {.name = "0x4a en la 4 y en 5", .type = 0x4a, .step = 5, .action = 4, .cut = "0x4f7962"},
        {.name = "0x12d en la 5 y en 5", .type = 0x12d, .step = 5, .action = 5, .draws = Tail},
        {.name = "0x49 en la 3 y en 11", .type = 0x49, .step = 11, .cut = "0x4f79af"},
        {.name = "0x49 en la 3 y en 13", .type = 0x49, .step = 13, .draws = Tail},
        {.name = "0x4b en la 4 y en 13", .type = 0x4b, .step = 13, .action = 4, .cut = "0x4f80cf"},
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
        {.name = "0x36 en 1", .type = 0x36, .step = 1, .cut = "0x4f7fd2"},
        {.name = "0x97 en 1", .type = 0x97, .step = 1, .cut = "0x4f7fd2"},
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

// 0x4f75b5 (0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82 y 0x8f): con '2', rand() & 1; con 1, dos rand() más y el 0xc2
// (0x4f7930); con 0, en 1 el 0xf6/1 (0x4f789b) y si no la cola.
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
                    EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x4f789b"));
                    EXPECT_EQ(Draws(), SkillRead + 1);
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
        const auto cut = Step(dry);
        EXPECT_TRUE(!cut.HasValue() && cut.Error().starts_with("0x4f71ed"));
        EXPECT_EQ(Draws(), 3);
        withParticles = true;
    }
}

// 0x4f7ab9 (+0x376 0x49 y 0x4b fuera de la 3, en 13): el punto (−50, 100, 0) del hueso 11 de los huesos propios va a
// +0x17c..+0x184 antes del efecto 0x106/1, que corta.
TEST_F(MonsterAttackCasesTest, TheTrailStartsAtTheEleventhBone)
{
    constexpr std::size_t Bones = 16;
    constexpr std::size_t TrailBone = 11;
    const glm::vec3 untouched(-1.0f);
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        ActorState& monster = Prepare(Probe{.name = "0x49", .type = 0x49, .step = 13, .action = 4});
        monster.bodyPose.assign(Bones, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < Bones; ++bone)
            monster.bodyPose[bone][3] = glm::vec3(static_cast<float>(bone), 2.0f * static_cast<float>(bone), 30.0f);
        monster.trailOrigin = untouched;

        const auto stepped = Step(monster);

        EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with("0x4f80cf"));
        const glm::vec3 expected = Client::Render::Models::OriginalTransformPosition(
            Client::Render::Models::OriginalMatrixOf(monster.bodyPose[TrailBone]), glm::vec3(-50.0f, 100.0f, 0.0f),
            monster.scale, monster.position);
        EXPECT_EQ(monster.trailOrigin, expected);
        EXPECT_NE(monster.trailOrigin, monster.position);
        EXPECT_EQ(Draws(), 0);

        // En 9, el mismo 0x4ec110 sin escribir nada.
        ActorState& later = Prepare(Probe{.name = "0x4b en 9", .type = 0x4b, .step = 9, .action = 4});
        later.trailOrigin = untouched;
        ASSERT_TRUE(Step(later).HasValue());
        EXPECT_EQ(later.trailOrigin, untouched);
    }
}

// La cola con 0x11 en +0x38e (0x4f8147): los casos salen siempre, sin la segunda lectura; los sonidos 0x75 y 0x50 en
// 1 salen antes del joint que corta. El 0x4f8493 del 0x49 y el 0x4b no llega: su caso de +0x376 corta antes. Los
// impares de 0xa3 a 0xad sacan en el alta (0x51443f) la derecha: 0xa25 (el arco) o 0xc17; acá va escrita. Con el arco,
// en 8, la flecha de 0x4f816f (0x53e2b0 → 0x53d9a0 con las manos) da el 0x111, cuya alta corta en su joint (0x51bf35).
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
        {.name = "0x59 en 14", .type = 0x59, .step = 14, .skill = 0x11, .cut = "0x4f8326", .draws = 2 * SkillRead},
        {.name = "0x8f en 13", .type = 0x8f, .step = 13, .skill = 0x11, .draws = 2 * SkillRead},
        {.name = "0x57 en 13", .type = 0x57, .step = 13, .skill = 0x11, .cut = "0x4f83e5", .draws = SkillRead},
        {.name = "0x8d en 14", .type = 0x8d, .step = 14, .skill = 0x11, .draws = SkillRead},
        {.name = "0x4d en 14", .type = 0x4d, .step = 14, .skill = 0x11, .cut = "0x4f845e", .draws = 2 * SkillRead},
        {.name = "0x4b en la 4 y en 13", .type = 0x4b, .step = 13, .action = 4, .skill = 0x11, .cut = "0x4f80cf"},
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
        constexpr std::size_t HeroAway = 399;
        game->world.Runtime().hero = HeroAway;
        for (const Probe& probe : probes)
            Expect(probe);
        // Sin el arco 0xa25, en la 4 y en 8: rand() & 1 elige 0xf5 (0x4f8227) o 0xc2/6 (0x4f8287).
        for (const std::int32_t type : {0xa5, 0xa7, 0xad, 0xa3})
        {
            ActorState& monster = Prepare(
                Probe{.name = "sin el arco", .type = type, .step = 8, .action = 4, .skill = 0x11, .right = Staff});
            CrtRandom expected(game->cloth.random.State());
            expected.Discard(SkillRead);
            const bool odd = (expected.Next() & 1) != 0;
            const auto stepped = Step(monster);
            EXPECT_TRUE(!stepped.HasValue() && stepped.Error().starts_with(odd ? "0x4f8287" : "0x4f8227"));
            EXPECT_EQ(Draws(), SkillRead + 1);
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
        {.name = "0x27", .type = 0x27, .step = 2, .skill = 3, .cut = "0x4f8f07", .draws = Tail},
        {.name = "sin caso", .type = 3, .step = 2, .skill = 3, .draws = Tail}};
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        for (const Probe& probe : probes)
            Expect(probe);
    }
}
} // namespace
