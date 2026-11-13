#include "Scenes/CharacterStep.h"

#include "Core/Expected.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/Particles.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/Bytes.h"
#include "support/PlayerActors.h"
#include "support/SameBytes.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
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

// Client::Scenes::StepCharacters: 0x508f00. El TerrainWall, si cada personaje se ve, el 0x508ba0 de los que no
// son el héroe, el cuerpo (0x4fd5f0) por el camino del 0x20a quieto y 0x505de0 entero, con la cabeza que sortea en
// los que no son el héroe. 06 § El paso de los personajes, § El paso de los personajes en la selección y el login,
// § El cuerpo de los personajes (0x4fd5f0), § Los efectos de los personajes (0x505de0) y § Caminar.
namespace
{
using Client::Render::Models::ActionInfo;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Skeleton;
using Client::Scenes::ActorState;
using Client::Scenes::CharacterHead;
using Client::Scenes::CharacterSeen;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::SceneId;
using Client::Scenes::StepCharacter;
using Client::Scenes::StepCharacterHead;
using Client::Scenes::StepCharacters;
using Client::Scenes::WorldState;
using Client::World::TerrainPolygon;

constexpr std::uint32_t Seed = 0x2468ace1u;
// Los dos accesos de 0x508f00 a la celda lógica (4 rand() por cierre).
constexpr int WallDraws = 8;
// 0x4fbe10 con la celda del héroe igual a la guardada: abre y cierra la X y la Y, cuatro rand() cada cierre.
constexpr int HeroCellReads = 2 * Client::Render::Models::SmallLogicalCloseRandoms;
// 0x4fc450 con el héroe: el registro propio, que se cierra en 0 con los 3.848 de 0x40b4d0.
constexpr int OwnRecordDraws = Client::Render::Models::OwnRecordCloseRandoms;
// El rand() % 6 de 0x4fd7e4.
constexpr int BodyDraws = 1;
// 0x505de0 con el héroe visto: los dos accesos de la celda y el rand() & 7 de 0x506cac.
constexpr int SeenEffectsDraws = 8 + 1;
// El héroe visto, quieto, sin terminar la acción y antes del frame 3: sin el rand() de 0x432bf1 (0x4fcee0).
constexpr int SeenHeroDraws = WallDraws + OwnRecordDraws + BodyDraws + SeenEffectsDraws;
// El rand() de 0x432bf1, desde el frame 3 (0x4fcee0), y el del sufijo de 0x4f2250 al terminar la acción.
constexpr int TrailDraws = 1;
constexpr int StopDraws = 1;
// Los dos accesos de 0x508ba0 con otro que el héroe, la X (0x412890) y la Y (0x40b800).
constexpr int OtherCellDraws = 8;
// Los dos accesos de 0x505de0 con otro visto (0x506362 y 0x506833).
constexpr int OtherSeenCellDraws = 8;
// El rand() & 7 de 0x506cac, después de la cabeza.
constexpr int EffectsRandomDraws = 1;
// 0x5068fd: la cabeza de otro que el héroe sortea con rand() % 32 en 0.
constexpr int HeadOneIn = 32;
// La selección (0x633f20, World55) y el login (0x6349a0, World56).
constexpr std::uint8_t SelectionMap = 54;
constexpr std::uint8_t LoginMap = 55;
// El cetro del personaje del login en la mano derecha (LoginModels.cpp; el caso 0x620 de 0x509340).
constexpr std::uint16_t LoginScepter = 0x620;

// 0x634303 pasa el objeto suelto después de los 400 registros: también avanza con la ventana cerrada.
TEST(CharacterStepTest, TheSelectionPreviewRunsTheCommonBodyOutsideThePool)
{
    WorldState world;
    world.Runtime().map = 54;
    auto& terrain = world.Map().terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState preview;
    preview.alive = true;
    preview.type = 0xa9;
    preview.byte88 = 0;
    preview.animation.action = 1;
    preview.animation.previousAction = 1;
    preview.hands[0].model = Client::Scenes::NoItemModel;
    preview.hands[1].model = Client::Scenes::NoItemModel;
    Skeleton face;
    face.actions.assign(2, ActionInfo{.repeats = true, .speed = 0.3f, .keyCount = 12});
    CrtRandom random(Seed);

    const auto stepped = StepCharacter(
        preview, {.world = world, .random = random, .scene = SceneId::CharacterSelect, .newFaceSkeleton = &face});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FLOAT_EQ(preview.animation.frame, 0.6f);
    EXPECT_EQ(preview.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    // X/Y de 0x508ba0, el rand del cuerpo, Y/X de efectos, cabeza común, rand de efectos.
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 19).State());
    EXPECT_EQ(terrain.attributes->cells[0] & Client::World::CharacterWallBit, 0u);
    EXPECT_FALSE(world.Actors()[0].alive);
}

TEST(CharacterStepTest, TheNewFaceHeadHasItsOwnDrawBeforeTheCommonHeadAndUsesModuloFortyEight)
{
    WorldState world;
    world.Runtime().map = 54;
    world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState actor;
    actor.alive = true;
    actor.type = 0xa8;
    actor.animation.frame = 4.0f;
    actor.hands[0].model = Client::Scenes::NoItemModel;
    actor.hands[1].model = Client::Scenes::NoItemModel;
    Skeleton face;
    face.actions.assign(1, ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 100});
    constexpr std::uint32_t HeadSeed = 35;
    CrtRandom random(HeadSeed);

    const auto stepped = StepCharacter(
        actor, {.world = world, .random = random, .scene = SceneId::CharacterSelect, .newFaceSkeleton = &face});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // Rand 19..22: 1536, 18843, 27303 y 16668; %48 da Y23, mientras que %32 daría Y351.
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{323.0f, 23.0f}));
    EXPECT_FLOAT_EQ(actor.headTurn.angles.x, 352.6f);
    EXPECT_FLOAT_EQ(actor.headTurn.angles.y, 4.6f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(HeadSeed, 23).State());
}

CrtRandom RandomAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

void Skip(CrtRandom& random, int draws)
{
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
}

// Lo que saca otro que el héroe, visto, quieto y sin terminar la acción, después de su TerrainWall: su
// 0x508ba0, su cuerpo (el rand() % 6), la celda de su 0x505de0, su cabeza (0x5068fd: rand() % 32 y, si da 0,
// dos más) y el rand() & 7. La cabeza va a `head` como la mueve StepCharacterHead. Devuelve si sorteó.
bool DrawASeenOther(CrtRandom& random, CharacterHead& head)
{
    Skip(random, OtherCellDraws + BodyDraws + OtherSeenCellDraws);
    CrtRandom probe = random;
    const bool sorts = probe.Next() % HeadOneIn == 0;
    StepCharacterHead(head, true, random);
    Skip(random, EffectsRandomDraws);
    return sorts;
}

bool DrawASeenOther(CrtRandom& random)
{
    CharacterHead head;
    return DrawASeenOther(random, head);
}

glm::vec3 CellCentre(std::uint8_t x, std::uint8_t y)
{
    return {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 0.0f};
}

// El trapecio del juego alrededor de `centre`, como el que deja 0x5fb570 con la cámara de arranque.
TerrainPolygon GamePolygon(std::uint8_t map, const glm::vec3& centre)
{
    return Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, map, centre, -45.0f);
}

bool HasError(const Client::Core::Expected<void, std::string>& stepped, std::string_view address)
{
    return !stepped.HasValue() && stepped.Error().find(address) != std::string::npos;
}

std::string ErrorOf(const Client::Core::Expected<void, std::string>& stepped)
{
    return stepped.HasValue() ? std::string() : stepped.Error();
}

// Player.bmd con las 205 acciones sin keys y la acción `action` con `keys` keys a `speed` por paso.
Skeleton SkeletonWith(std::uint8_t action, std::int16_t keys, float speed)
{
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    skeleton.actions[action] = ActionInfo{.repeats = true, .speed = speed, .keyCount = keys, .lockPositions = false};
    return skeleton;
}

// Un mundo con el héroe vivo en el lugar HeroSlot, de pie (la acción 1 de 0x511290), con el PK de 0x511290, y
// Player.bmd con sus acciones sin keys: el cuerpo no avanza ni termina. El registro propio, con el equipo vacío
// que deja la respuesta del F3 03 (0x4c47c8): 0x4fc450 no pide Item.bmd.
struct GameWorld
{
    explicit GameWorld(std::int32_t map = 0)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Hero() = Client::Tests::UnarmedPlayer(1);
        Hero().alive = true;
        Hero().byte88 = 1;
        Hero().pk = 3;
        Hero().animation.action = 1;
    }

    ActorState& Hero()
    {
        return world.Actors()[HeroSlot];
    }

    // Otro jugador vivo y de pie en `slot`, con la celda lógica en (x, y), +0x88 en 1 y el PK de 0x511290.
    ActorState& Other(std::size_t slot, std::uint8_t x = 3, std::uint8_t y = 4)
    {
        ActorState& other = world.Actors()[slot];
        other = Client::Tests::UnarmedPlayer(2);
        other.alive = true;
        other.byte88 = 1;
        other.pk = 3;
        other.animation.action = 1;
        // Quieto: 0x511290 deja el blanco +0x39a/b igual a la celda lógica, antes del primer D3.
        other.cellX = other.targetX = x;
        other.cellY = other.targetY = y;
        return other;
    }

    std::uint16_t& Cell(std::uint8_t x, std::uint8_t y)
    {
        return world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // En `map` (18 a 23), el héroe en (5; 6), la celda que 0x4fbe10 guardó hace 10 s, con el bit 8.
    void StandOnTheReportedCell(std::int32_t map)
    {
        world.Runtime().map = map;
        Hero().cellX = 5;
        Hero().cellY = 6;
        world.Runtime().reportedCellX = 5;
        world.Runtime().reportedCellY = 6;
        world.Runtime().reportedCellTime = -10000.0f;
        Cell(5, 6) = 8;
    }

    // Con `polygon` en el global que deja el último 0x5fb570 (WorldRuntime::terrainPolygon).
    Client::Core::Expected<void, std::string> Step(const TerrainPolygon& polygon, SceneId scene = SceneId::Game)
    {
        world.Runtime().terrainPolygon = polygon;
        return Step(scene);
    }

    // Con el global como esté.
    Client::Core::Expected<void, std::string> Step(SceneId scene = SceneId::Game)
    {
        return StepCharacters(
            CharacterStepContext{.world = world, .random = random, .scene = scene, .playerSkeleton = player});
    }

    static constexpr std::size_t HeroSlot = 42;
    // En el heap: el WorldState pasa de 340 KB y hay tests con tres GameWorld vivos, más de lo que entra en el
    // stack de 1 MB de Windows. Mover el GameWorld mueve el puntero, y la referencia sigue en el mismo WorldState.
    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    const Skeleton* player = &skeleton;
};

const TerrainPolygon Everything{.everything = true};

TEST(CharacterStepTest, TheSceneTerrainSuppliesItsWallsAndSafeZone)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2);
    tested.world.Runtime().terrainPolygon = Everything;
    tested.world.Map().terrain.attributes.reset();
    Client::World::WorldTerrain sceneTerrain;
    sceneTerrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    const auto cell = Client::Data::Terrain::CellIndex(3, 4);
    sceneTerrain.attributes->cells[cell] = 1;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .scene = SceneId::Login,
                                         .playerSkeleton = tested.player,
                                         .terrain = &sceneTerrain});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(actor.safeZone, 1);
    EXPECT_NE(sceneTerrain.attributes->cells[cell] & Client::World::CharacterWallBit, 0);
    EXPECT_FALSE(tested.world.Map().terrain.attributes);
}

TEST(CharacterStepTest, TheRavenRunsInsideTheBodyBeforeTheCellAndHead)
{
    GameWorld tested(LoginMap);
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2, 0, 0);
    actor.position = {10603.0f, 14112.0f, 580.0f};
    actor.darkSpirit.emplace();
    actor.darkSpirit->position = {50.0f, 50.0f, 300.0f};
    tested.world.Runtime().terrainPolygon = Everything;
    Skeleton raven;
    raven.actions.resize(4);
    Client::World::Particles particles;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .scene = SceneId::Login,
                                         .playerSkeleton = tested.player,
                                         .darkSpiritSkeleton = &raven,
                                         .particles = &particles});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // 8 paredes + 8 del otro + 1 cuerpo + 7 cuervo + 8 celda + 1 cabeza + 1 sufijo.
    EXPECT_EQ(tested.random.State(), RandomAfter(34).State());
    EXPECT_FLOAT_EQ(actor.darkSpirit->direction.y, -7.55f);
    EXPECT_FLOAT_EQ(actor.darkSpirit->direction.z, 2.9f);
    EXPECT_EQ(actor.headTurn.target, glm::vec2(0.0f));
}

// Q-C412: el cuervo posado (el estado 3, 0x4178a6) en la zona segura va al hueso 37 de los huesos propios de su
// dueño con (−10, 0, 10), por la escala y más la posición del modelo compartido de Player.bmd: las que copió el
// 0x505ec6 del último 0x20a que pasó antes (el del lugar 2), no las del dueño, que las copia después.
TEST(CharacterStepTest, ThePerchedRavenUsesTheSharedModelOfThePreviousPlayer)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& first = tested.Other(2, 0, 0);
    first.position = {1000.0f, 2000.0f, 300.0f};
    first.scale = 1.5f;
    ActorState& owner = tested.Other(5, 0, 0);
    owner.position = {10603.0f, 14112.0f, 580.0f};
    owner.scale = 1.25f;
    owner.angles = {3.0f, 0.0f, 188.0f};
    owner.safeZone = 1;
    tested.Cell(0, 0) = 1;
    owner.bodyPose.assign(60, Client::Render::Models::BoneTransform(1.0f));
    owner.bodyPose[37][3] = glm::vec3(7.0f, 8.0f, 9.0f);
    owner.darkSpirit.emplace();
    owner.darkSpirit->state = 3;
    Skeleton raven;
    raven.actions.resize(4);
    Client::World::Particles particles;
    tested.world.Runtime().terrainPolygon = Everything;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .playerSkeleton = tested.player,
                                         .darkSpiritSkeleton = &raven,
                                         .particles = &particles});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(owner.darkSpirit->state, 3);
    // 1,5 · ((7, 8, 9) + (−10, 0, 10)) + (1.000, 2.000, 300).
    EXPECT_EQ(owner.darkSpirit->position, glm::vec3(995.5f, 2012.0f, 328.5f));
    EXPECT_EQ(owner.darkSpirit->angles, glm::vec3(3.0f, 0.0f, 68.0f));
    // El último 0x20a del paso deja lo suyo para el próximo.
    EXPECT_EQ(tested.world.PlayerModel().scale, 1.25f);
    EXPECT_EQ(tested.world.PlayerModel().origin, owner.position);
}

// 0x416a88..0x4176e6: en los estados 2, 5 y 6, con el personaje de +0x3b0 escondido (0x4000), el cuervo manda el A7
// que corta el ataque (C1 07 A7 00 78 FF FF, con el XOR32) y deja +0x3b0 en 0xffff. Sin 0x4000, no manda nada.
TEST(CharacterStepTest, AHiddenRavenTargetSendsTheA7ThatStopsTheAttack)
{
    for (const std::int32_t map : {0, 2, std::int32_t{LoginMap}})
    {
        for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{5}, std::uint8_t{6}})
        {
            for (const std::uint32_t flags : {0u, 0x4000u})
            {
                SCOPED_TRACE(testing::Message() << map << " " << int{state} << " " << flags);
                GameWorld tested(map);
                tested.Hero().alive = false;
                ActorState& actor = tested.Other(2, 0, 0);
                ActorState& target = tested.Other(7, 1, 1);
                target.flags78 = flags;
                target.position = {300.0f, 400.0f, 0.0f};
                actor.darkSpirit.emplace();
                actor.darkSpirit->state = state;
                actor.darkSpirit->target3b0 = 7;
                Skeleton raven;
                raven.actions.resize(4);
                Client::World::Particles particles;
                tested.world.Runtime().terrainPolygon = Everything;
                std::vector<Client::Protocol::Outgoing> sent;
                const auto stepped = StepCharacters(
                    {.world = tested.world,
                     .random = tested.random,
                     .scene = SceneId::Login,
                     .playerSkeleton = tested.player,
                     .darkSpiritSkeleton = &raven,
                     .particles = &particles,
                     .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }});
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                if (flags == 0)
                {
                    EXPECT_TRUE(sent.empty());
                    EXPECT_EQ(actor.darkSpirit->target3b0, 7);
                    continue;
                }
                ASSERT_EQ(sent.size(), 1u);
                EXPECT_EQ(sent[0].delivery, Client::Network::Delivery::Clear);
                EXPECT_EQ(Client::Network::Crypto::Xor32Decode(sent[0].packet).Bytes().size(), 7u);
                EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(sent[0].packet).Bytes(),
                                                     Client::Tests::Bytes({0xc1, 7, 0xa7, 0, 0x78, 0xff, 0xff})));
                EXPECT_EQ(actor.darkSpirit->target3b0, 0xffff);
            }
        }
    }
}

// 0x416a8b: +0x3b0 entre 0 y 399 mira las banderas de ese personaje; fuera (el 400 de una clave que no estaba), solo
// las del dueño.
TEST(CharacterStepTest, TheRavenLooksAtTheAttackTargetOnlyInsideTheFourHundred)
{
    GameWorld tested(LoginMap);
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2, 0, 0);
    tested.world.Actors()[400].flags78 = 0x4000;
    actor.darkSpirit.emplace();
    actor.darkSpirit->state = 6;
    actor.darkSpirit->target3b0 = 400;
    Skeleton raven;
    raven.actions.resize(4);
    Client::World::Particles particles;
    tested.world.Runtime().terrainPolygon = Everything;
    std::vector<Client::Protocol::Outgoing> sent;
    const auto stepped =
        StepCharacters({.world = tested.world,
                        .random = tested.random,
                        .scene = SceneId::Login,
                        .playerSkeleton = tested.player,
                        .darkSpiritSkeleton = &raven,
                        .particles = &particles,
                        .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(actor.darkSpirit->state, 6);

    actor.flags78 = 0x4000;
    const auto hidden =
        StepCharacters({.world = tested.world,
                        .random = tested.random,
                        .scene = SceneId::Login,
                        .playerSkeleton = tested.player,
                        .darkSpiritSkeleton = &raven,
                        .particles = &particles,
                        .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }});
    ASSERT_TRUE(hidden.HasValue()) << hidden.Error();
    EXPECT_EQ(sent.size(), 1u);
}

TEST(CharacterStepTest, AMissingRavenResourceCutsAfterTheBodyRandoms)
{
    GameWorld tested(LoginMap);
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2, 0, 0);
    actor.darkSpirit.emplace();
    const auto stepped = tested.Step(Everything, SceneId::Login);
    EXPECT_TRUE(HasError(stepped, "0x416a20"));
    EXPECT_EQ(tested.random.State(), RandomAfter(17).State());
    EXPECT_EQ(actor.headTurn.target, glm::vec2(0.0f));
}

class GoldenCharacterStep : public Client::Tests::GoldenDataTest
{
protected:
    // Item.bmd del Data\ del repo.
    std::optional<Client::Data::Tables::LoadedItemTable> LoadItems() const
    {
        const auto path = DataFile("Local\\Item.bmd");
        if (!path)
        {
            return std::nullopt;
        }
        const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
        if (!file)
        {
            return std::nullopt;
        }
        auto loaded = Client::Data::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        return std::move(loaded).Value();
    }
};

// Player.bmd del Data\ del repo, con sus 205 acciones (P-C14 a).
std::optional<Skeleton> LoadPlayerSkeleton(const std::filesystem::path& dataRoot)
{
    for (const Client::Data::Models::ModelFile& file : Client::Data::Models::PlayerModelFiles())
    {
        if (file.name != Client::Data::Models::PlayerSkeletonName)
        {
            continue;
        }
        auto loaded = Client::Data::Models::LoadModelFile(dataRoot, file);
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        auto skeleton = Client::Render::Models::MakePlayerSkeleton(loaded.Value());
        if (!skeleton.HasValue())
        {
            return std::nullopt;
        }
        return std::move(skeleton).Value();
    }
    return std::nullopt;
}

// El plan del hito 1, T11, y la observación 8: cruzando el borde del pueblo, +0x3e2 sigue a la celda lógica
// (+0x41c, +0x420), la que va una adelante, y no a la que se dibuja. En Lorencia (el 0), el borde del este
// en (172, 128) y (173, 128); en Devias (el 2), el del norte en (220, 76) y (220, 77). Con el héroe visto:
// los 8 rand() del TerrainWall, los 3.848 del registro propio y el rand() % 6 de 0x4fd5f0, los 8 de la celda
// de 0x505de0 y el rand() & 7.
TEST_F(GoldenCharacterStep, TheSafeZoneFollowsTheLogicalCellAhead)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t insideX;
        std::uint8_t insideY;
        std::uint8_t outsideX;
        std::uint8_t outsideY;
    };
    for (const Case& border : {Case{0, 172, 128, 173, 128}, Case{2, 220, 76, 220, 77}})
    {
        struct Walk
        {
            bool leaving;
            std::uint8_t drawnX;
            std::uint8_t drawnY;
            std::uint8_t logicalX;
            std::uint8_t logicalY;
            std::uint8_t safeZone;
        };
        for (const Walk& walk : {Walk{true, border.insideX, border.insideY, border.outsideX, border.outsideY, 0},
                                 Walk{false, border.outsideX, border.outsideY, border.insideX, border.insideY, 1}})
        {
            SCOPED_TRACE(::testing::Message()
                         << "mapa " << static_cast<int>(border.map) << (walk.leaving ? ", sale" : ", entra"));
            GameWorld tested(border.map);
            auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, border.map);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            tested.world.Map().terrain = std::move(loaded).Value();
            ASSERT_EQ(tested.Cell(border.insideX, border.insideY) & 1, 1);
            ASSERT_EQ(tested.Cell(border.outsideX, border.outsideY) & 1, 0);
            ActorState& hero = tested.Hero();
            hero.position = CellCentre(walk.drawnX, walk.drawnY);
            hero.cellX = walk.logicalX;
            hero.cellY = walk.logicalY;
            // Lo que dejó 0x511290 con la celda dibujada.
            hero.safeZone = static_cast<std::uint8_t>(1 - walk.safeZone);
            const TerrainPolygon polygon = GamePolygon(border.map, hero.position);
            ASSERT_TRUE(CharacterSeen(polygon, hero.position));

            const auto stepped = tested.Step(polygon);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(hero.seen);
            EXPECT_EQ(hero.safeZone, walk.safeZone);
            EXPECT_EQ(tested.Cell(walk.logicalX, walk.logicalY) & Client::World::CharacterWallBit,
                      Client::World::CharacterWallBit);
            EXPECT_EQ(tested.Cell(walk.drawnX, walk.drawnY) & Client::World::CharacterWallBit, 0);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// Lo que saca por paso el héroe quieto en la zona segura, con Player.bmd del repo: la acción 1 (6 keys, 0,28 por
// paso, repite) de un jugador sin armas. Por paso, los 8 del TerrainWall, los 3.848 del registro propio
// (0x4fc450), el rand() % 6 (0x4fd7e4), y los 8 de la celda y el rand() & 7 de 0x505de0: 3.866. Desde el paso 11
// el frame pasa de 3 y 0x432bf1 saca uno más; en el 22 la acción termina (6,16), vuelve al frame 0,16 y 0x4f2250
// saca el de su sufijo, sin el de 0x432bf1. En Lorencia y en Devias.
TEST_F(GoldenCharacterStep, TheHeroAtRestDrawsTheSameBudgetOnLorenciaAndDevias)
{
    const std::optional<Skeleton> player = LoadPlayerSkeleton(m_dataRoot);
    ASSERT_TRUE(player.has_value());
    ASSERT_EQ(player->actions[1].keyCount, 6);
    ASSERT_EQ(player->actions[1].speed, 0.28f);
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
    };
    for (const Case& place : {Case{0, 172, 128}, Case{2, 220, 76}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << static_cast<int>(place.map));
        GameWorld tested(place.map);
        tested.player = &*player;
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, place.map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        tested.world.Map().terrain = std::move(loaded).Value();
        ASSERT_EQ(tested.Cell(place.x, place.y) & 1, 1);
        ActorState& hero = tested.Hero();
        hero.position = CellCentre(place.x, place.y);
        hero.cellX = place.x;
        hero.cellY = place.y;
        hero.safeZone = 1;
        hero.run = 1;
        const TerrainPolygon polygon = GamePolygon(place.map, hero.position);
        CrtRandom expected(Seed);
        for (int step = 1; step <= 22; ++step)
        {
            SCOPED_TRACE(::testing::Message() << "paso " << step);
            const int draws = SeenHeroDraws + (step >= 11 ? TrailDraws : 0);
            for (int i = 0; i < draws; ++i)
            {
                static_cast<void>(expected.Next());
            }

            const auto stepped = tested.Step(polygon);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested.random.State(), expected.State());
            EXPECT_EQ(hero.animation.action, 1);
            EXPECT_EQ(hero.run, step < 22 ? 1 : 0) << "0x4f2250 pone Run en 0 al terminar";
        }
        EXPECT_LT(hero.animation.frame, 1.0f);
        EXPECT_EQ(hero.safeZone, 1);
    }
}

// 0x505ef2: sin verse, 0x505de0 no lee la celda ni toca +0x3e2; quedan los 8 rand() del TerrainWall y los del
// cuerpo, que corre igual.
TEST(CharacterStepTest, AnUnseenHeroKeepsItsSafeZone)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.position = CellCentre(100, 100);
    hero.cellX = 100;
    hero.cellY = 100;
    hero.safeZone = 1;
    hero.seen = true;
    tested.Cell(100, 100) = 0;
    const TerrainPolygon polygon = GamePolygon(0, CellCentre(200, 200));
    ASSERT_FALSE(CharacterSeen(polygon, hero.position));

    const auto stepped = tested.Step(polygon);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(hero.seen);
    EXPECT_EQ(hero.safeZone, 1);
    EXPECT_EQ(tested.Cell(100, 100), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OwnRecordDraws + BodyDraws).State());
}

// 0x505de0 mira el bit 0x01 del byte bajo del WORD: el 0x02 del héroe y los bits altos no cuentan.
TEST(CharacterStepTest, TheSafeZoneIsTheLowBitOfTheCell)
{
    struct Case
    {
        std::uint16_t cell;
        std::uint8_t safeZone;
    };
    for (const Case& probe : {Case{0x0001, 1}, Case{0x0005, 1}, Case{0x0100, 0}, Case{0x0102, 0}, Case{0x0004, 0}})
    {
        SCOPED_TRACE(probe.cell);
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.cellX = 7;
        hero.cellY = 9;
        hero.safeZone = static_cast<std::uint8_t>(1 - probe.safeZone);
        tested.Cell(7, 9) = probe.cell;

        ASSERT_TRUE(tested.Step(Everything).HasValue());

        EXPECT_EQ(hero.safeZone, probe.safeZone);
    }
}

// 0x508f14..0x5091a0: el 0x02 sale de toda la grilla y vuelve en la celda lógica (los ocho bits bajos) de
// cada personaje vivo, sin morirse (+0x389) y sin el 8 de +0x88, con sus dos accesos; si se ve, de los 400
// primeros, vivos o no; el 401 no. Después, el primer vivo que no es el héroe, el que se muere, sale de 0x508ba0
// sin leer su celda (0x508bb6) y el paso corta en su cuerpo, en 0x4fb9e0.
TEST(CharacterStepTest, TheWallsAndTheViewCoverTheFirstFourHundred)
{
    GameWorld tested;
    auto actors = tested.world.Actors();
    actors[1].alive = true;
    actors[1].dying = 1;
    actors[1].cellX = 1;
    actors[1].cellY = 1;
    actors[2].alive = true;
    actors[2].byte88 = Client::World::NonBlockingKind;
    actors[2].cellX = 2;
    actors[2].cellY = 2;
    actors[3].alive = true;
    actors[3].cellX = 3;
    actors[3].cellY = 4;
    tested.Hero().cellX = 30;
    tested.Hero().cellY = 40;
    actors[400].alive = true;
    actors[400].cellX = 50;
    actors[400].cellY = 50;
    tested.Cell(200, 201) = 0x0106;

    const auto stepped = tested.Step(Everything);

    ASSERT_TRUE(HasError(stepped, "0x4fb9e0")) << ErrorOf(stepped);
    EXPECT_NE(stepped.Error().find("lugar 1 "), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.Cell(200, 201), 0x0104);
    EXPECT_EQ(tested.Cell(1, 1), 0);
    EXPECT_EQ(tested.Cell(2, 2), 0);
    EXPECT_EQ(tested.Cell(3, 4), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.Cell(30, 40), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.Cell(50, 50), 0);
    EXPECT_TRUE(actors[0].seen);
    EXPECT_TRUE(actors[399].seen);
    EXPECT_FALSE(actors[400].seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(2 * WallDraws).State());
}

// Una cuenta abierta en la tabla de protección no se vuelve a cifrar al cerrar: la celda no gasta, y quedan el
// registro propio, el rand() % 6 del cuerpo y el rand() & 7.
TEST(CharacterStepTest, AnOpenAccessSpendsNothingOnClosing)
{
    GameWorld tested;
    tested.Hero().cellXAccesses = 1;
    tested.Hero().cellYAccesses = 1;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_EQ(tested.Hero().cellXAccesses, 1);
    EXPECT_EQ(tested.Hero().cellYAccesses, 1);
    EXPECT_EQ(tested.random.State(), RandomAfter(OwnRecordDraws + BodyDraws + 1).State());
}

// Sin vivos, el paso borra el TerrainWall, escribe +0x16c y no gasta rand().
TEST(CharacterStepTest, WithoutLiveCharactersOnlyTheWallsAndTheViewChange)
{
    GameWorld tested;
    tested.Hero().alive = false;
    tested.Cell(5, 5) = Client::World::CharacterWallBit;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_EQ(tested.Cell(5, 5), 0);
    EXPECT_TRUE(tested.Hero().seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(0).State());
}

// Q-C217: antes de cualquier 0x5fb570 el global tiene los ceros de la .bss, un trapecio sin área en (0, 0): los
// cuatro lados dan 0, que pasa el margen de −20, y se ven todos, en cualquier lugar, también el muerto del lugar
// 0. Con un WorldState nuevo (lo que deja WinMain) el paso sigue entero.
TEST(CharacterStepTest, WithTheTrapezoidOfTheBssEveryoneIsSeen)
{
    GameWorld tested;
    ASSERT_EQ(tested.world.Runtime().terrainPolygon.x, (std::array<float, 4>{}));
    ASSERT_EQ(tested.world.Runtime().terrainPolygon.y, (std::array<float, 4>{}));
    ASSERT_FALSE(tested.world.Runtime().terrainPolygon.everything);
    tested.Hero().position = CellCentre(250, 3);
    tested.world.Actors()[0].position = CellCentre(7, 200);

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(tested.Hero().seen);
    EXPECT_TRUE(tested.world.Actors()[0].seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x412140 en el mapa 30, antes del lazo: con el puesto del gremio del arranque (0x6c649a en 0xff) arma el panel
// del asedio de la clase 0 y el paso sigue como en Lorencia, con los mismos rand() y las mismas paredes; con otro
// puesto corta en 0x412193 antes de tocar nada. Fuera del 30, 0x412560 suelta el panel. Sin el mapa cargado no
// hay TerrainWall: corta antes de tocar nada.
TEST(CharacterStepTest, TheCastleSiegePanelAndAnUnloadedMapComeBeforeTheWalls)
{
    GameWorld castle(30);
    GameWorld lorencia(0);
    for (GameWorld* tested : {&castle, &lorencia})
        tested->Cell(5, 5) = Client::World::CharacterWallBit;
    const auto inCastle = castle.Step(Everything);
    ASSERT_TRUE(inCastle.HasValue()) << ErrorOf(inCastle);
    ASSERT_TRUE(lorencia.Step(Everything).HasValue());
    EXPECT_EQ(castle.world.Runtime().siegePanelKind, std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_FALSE(lorencia.world.Runtime().siegePanelKind.has_value());
    EXPECT_EQ(castle.Cell(5, 5), lorencia.Cell(5, 5));
    EXPECT_EQ(castle.random.State(), lorencia.random.State());

    castle.world.Runtime().map = 0;
    ASSERT_TRUE(castle.Step(Everything).HasValue());
    EXPECT_FALSE(castle.world.Runtime().siegePanelKind.has_value());

    GameWorld guild(30);
    guild.world.Runtime().heroGuildPost = 0x80;
    guild.Cell(5, 5) = Client::World::CharacterWallBit;
    const auto inGuild = guild.Step(Everything);
    EXPECT_TRUE(HasError(inGuild, "0x412193")) << ErrorOf(inGuild);
    EXPECT_FALSE(guild.world.Runtime().siegePanelKind.has_value());
    EXPECT_EQ(guild.Cell(5, 5), Client::World::CharacterWallBit);
    EXPECT_EQ(guild.random.State(), RandomAfter(0).State());

    GameWorld unloaded;
    unloaded.world.Map().terrain.attributes.reset();
    const auto withoutMap = unloaded.Step(Everything);
    EXPECT_TRUE(HasError(withoutMap, "0x508f14")) << ErrorOf(withoutMap);
    EXPECT_EQ(unloaded.random.State(), RandomAfter(0).State());
}

// 0x4fc450, solo con el héroe publicado: con el registro propio abierto, cada WORD de +0x3e, +0x40 y +0x42
// baja si es positivo y, en 0, apaga su bit de +0x3c y llama a sus helpers; con +0x40 en 0, las 16 marcas de
// la lista de [0x6eed3c] vuelven a 0. Al cerrar, los 3.848 rand() si la cuenta queda en 0; con el registro ya
// abierto, ninguno.
TEST(CharacterStepTest, TheOwnRecordTimersRunWithTheHero)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(::testing::Message() << "con la cuenta en " << static_cast<int>(depth));
        GameWorld tested;
        Client::Scenes::OwnCharacterRecord& own = tested.world.OwnCharacter();
        own.word3e = 2;
        own.word40 = 1;
        own.word42 = 0;
        own.speedAdjustBits = 0x0b;
        tested.world.ItemSets().list.used[3] = 1;
        tested.world.Runtime().ownRecordDepth = depth;

        ASSERT_TRUE(tested.Step(Everything).HasValue());

        EXPECT_EQ(own.word3e, 1);
        EXPECT_EQ(own.word40, 0);
        EXPECT_EQ(own.word42, 0);
        EXPECT_EQ(own.speedAdjustBits, 0x01);
        EXPECT_EQ(tested.world.ItemSets().list.used, (std::array<std::uint8_t, 16>{}));
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, depth);
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws - (depth == 0 ? 0 : OwnRecordDraws)).State());
    }
}

// Con otro que el héroe, 0x4fc450 sale en 0x4fc464: el registro propio queda como estaba y no gasta.
TEST(CharacterStepTest, TheOwnRecordTimersSkipTheOthers)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& other = tested.world.Actors()[3];
    other = Client::Tests::UnarmedPlayer(1);
    other.alive = true;
    other.byte88 = 1;
    other.position = CellCentre(10, 10);
    tested.world.OwnCharacter().word3e = 2;
    const TerrainPolygon polygon = GamePolygon(0, CellCentre(200, 200));
    ASSERT_FALSE(CharacterSeen(polygon, other.position));

    const auto stepped = tested.Step(polygon);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.world.OwnCharacter().word3e, 2);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OtherCellDraws + BodyDraws).State());
}

// 0x4e9980 desde 0x4fd72e: con +0x16d en 1 (0x511290) el alpha sube de a 0,05f, en doble, y llega a 1 en el paso
// 20, el de la entrada (la respuesta del F3 03 lo deja en 0); la luz del blendMesh (+0x68) baja con él.
TEST(CharacterStepTest, TheHeroAppearsInTwentySteps)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.alphaMode = 1;
    hero.alphaTarget = 1.0f;
    hero.alpha = 0.0f;
    hero.blendMeshLight = 1.0f;

    ASSERT_TRUE(tested.Step(Everything).HasValue());
    EXPECT_EQ(hero.alpha, static_cast<float>(static_cast<double>(0.05f)));
    EXPECT_EQ(hero.blendMeshLight, hero.alpha);
    for (int step = 2; step <= 19; ++step)
    {
        ASSERT_TRUE(tested.Step(Everything).HasValue());
    }
    EXPECT_LT(hero.alpha, 1.0f);
    ASSERT_TRUE(tested.Step(Everything).HasValue());
    EXPECT_EQ(hero.alpha, 1.0f);
    EXPECT_LT(hero.blendMeshLight, 0.1f) << "la luz del blendMesh no sube (Q-C112)";
}

// 0x4f9580: la velocidad de la acción (0,4 por paso acá) por 1,5 en las 3 y 4 con +0x3e3, por 0,5 en la 0x3f
// pasado el frame 6 y en la 0x44, por 0,1f en las 0x4d, 0x4f y 0x5a pasado el 5,5, y de nuevo por 0,5 en la 0x44.
// En la clase 4, las 0x94, 0x51 y 0x5b entre el frame 1 y el 3 cortan en 0x59e680 sin avanzar.
TEST(CharacterStepTest, TheSpeedOfTheActionGoesThroughItsFactors)
{
    constexpr float Speed = 0.4f;
    struct Case
    {
        std::uint8_t action;
        std::uint8_t byte3e3;
        std::uint8_t characterClass;
        float frame;
        float after;
        std::string_view cut;
    };
    for (const Case& probe :
         {Case{3, 0, 1, 0.0f, Speed, ""}, Case{3, 1, 1, 0.0f, Speed * 1.5f, ""}, Case{4, 1, 1, 0.0f, Speed * 1.5f, ""},
          Case{5, 1, 1, 0.0f, Speed, ""}, Case{0x3f, 0, 1, 6.0f, 6.0f + Speed, ""},
          Case{0x3f, 0, 1, 6.5f, 6.5f + (Speed * 0.5f), ""}, Case{0x4d, 0, 1, 5.0f, 5.0f + Speed, ""},
          Case{0x5a, 0, 1, 5.6f, 5.6f + (Speed * 0.1f), "0x4fdb70"}, Case{0x44, 0, 1, 0.0f, Speed * 0.5f, "0x506e5d"},
          Case{0x51, 0, 4, 2.0f, 2.0f, "0x4f969d"}, Case{0x51, 0, 4, 3.5f, 3.5f + Speed, ""},
          Case{0x51, 0, 0x14, 0.5f, 0.5f + Speed, ""}})
    {
        SCOPED_TRACE(::testing::Message() << "acción " << static_cast<int>(probe.action) << ", frame " << probe.frame
                                          << ", clase " << static_cast<int>(probe.characterClass));
        GameWorld tested;
        tested.skeleton = SkeletonWith(probe.action, 20, Speed);
        ActorState& hero = tested.Hero();
        hero.animation.action = probe.action;
        hero.animation.frame = probe.frame;
        hero.byte3e3 = probe.byte3e3;
        hero.characterClass = probe.characterClass;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(hero.animation.frame, probe.after);
    }
}

// 0x4fcd00 con la acción que terminó (+0x78 & 0x1000: 0x4f9580 devuelve 0 sin avanzar, en cada paso) va a
// 0x4fcb10: las de antes de la 0x0d, las 0xbd a 0xbf, las 0x54 y 0x55 y, de la 0x22 a la 0xc2, las que no
// están en la lista vuelven a la de parado (0x4f2250: la 1 del jugador sin armas, con el rand() de su sufijo);
// las demás quedan; la 0x44 vuelve al frame 0; las 0xc3 y 0xc4 cortan en 0x52e810.
TEST(CharacterStepTest, AnEndedActionGoesBackToStanding)
{
    struct Case
    {
        std::uint8_t action;
        std::uint8_t after;
        std::string_view cut;
        int draws;
    };
    constexpr int Stopped = SeenHeroDraws + StopDraws;
    for (const Case& probe :
         {Case{1, 1, "", Stopped}, Case{0x0c, 1, "", Stopped}, Case{0xbd, 1, "", Stopped}, Case{0xbf, 1, "", Stopped},
          Case{0x54, 1, "", Stopped}, Case{0x55, 1, "", Stopped}, Case{0x22, 1, "", Stopped},
          Case{0xc2, 1, "", Stopped}, Case{0x0d, 0x0d, "", SeenHeroDraws}, Case{0x8c, 0x8c, "", SeenHeroDraws},
          Case{0x46, 0x46, "", SeenHeroDraws}, Case{0x6a, 0x6a, "", SeenHeroDraws}, Case{0x75, 0x75, "", SeenHeroDraws},
          Case{0x76, 1, "", Stopped}, Case{0xc5, 0xc5, "", SeenHeroDraws},
          Case{0xc3, 0xc3, "0x4fcb87", WallDraws + OwnRecordDraws},
          Case{0xc4, 0xc4, "0x4fcb87", WallDraws + OwnRecordDraws}})
    {
        SCOPED_TRACE(::testing::Message() << "acción " << static_cast<int>(probe.action));
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.flags78 = 0x1000;
        hero.animation.action = probe.action;
        hero.animation.frame = 2.5f;
        hero.run = 1;
        tested.Cell(0, 0) = 1;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(hero.animation.action, probe.after);
        EXPECT_EQ(hero.run, probe.draws == Stopped ? 0 : 1);
        EXPECT_EQ(hero.animation.frame, probe.after != probe.action ? 0.0f : 2.5f);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// 0x4fcb71: la 0x44 que terminó se queda y vuelve al frame 0; después, 0x505de0 corta en sus efectos.
TEST(CharacterStepTest, AnEndedActionFortyFourRestartsItsFrame)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.flags78 = 0x1000;
    hero.animation.action = 0x44;
    hero.animation.frame = 2.5f;

    const auto stepped = tested.Step(Everything);

    EXPECT_TRUE(HasError(stepped, "0x506e5d")) << ErrorOf(stepped);
    EXPECT_EQ(hero.animation.action, 0x44);
    EXPECT_EQ(hero.animation.frame, 0.0f);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x4fcee0: antes del frame 3, salvo en la 0x8d, copia la posición a +0x17c y sale; si no, con el 0x20a, la estela
// según la acción y el arma de la mano (la izquierda con las 0x25 y 0x26) corta en 0x52e470, y sin estela 0x432bd0
// saca su rand() sin condición (0x432bf1).
TEST(CharacterStepTest, TheWeaponTrailDrawsFromFrameThree)
{
    constexpr std::uint16_t NoItem = Client::Scenes::NoItemModel;
    struct Case
    {
        std::uint8_t action;
        float frame;
        std::uint16_t right;
        std::uint16_t left;
        bool trail;
        int draws;
    };
    constexpr int Trailed = WallDraws + OwnRecordDraws + BodyDraws;
    constexpr int Drawn = SeenHeroDraws + TrailDraws;
    for (const Case& probe :
         {Case{1, 2.99f, NoItem, NoItem, false, SeenHeroDraws}, Case{1, 3.0f, NoItem, NoItem, false, Drawn},
          Case{0x8d, 0.0f, NoItem, NoItem, false, Drawn}, Case{0x8d, 0.0f, 0x212, NoItem, true, Trailed},
          Case{0x84, 3.0f, NoItem, NoItem, true, Trailed}, Case{0x4c, 3.0f, NoItem, NoItem, true, Trailed},
          Case{0x39, 3.0f, NoItem, NoItem, true, Trailed}, Case{0x3c, 3.0f, NoItem, NoItem, true, Trailed},
          Case{0x23, 3.0f, 0x212, NoItem, true, Trailed}, Case{0x29, 3.0f, 0x411, NoItem, true, Trailed},
          Case{0x2a, 3.0f, 0x411, NoItem, false, Drawn}, Case{0x25, 3.0f, 0x212, NoItem, false, Drawn},
          Case{0x25, 3.0f, NoItem, 0x212, true, Trailed}, Case{0x38, 3.0f, 0x415, NoItem, true, Trailed},
          Case{0x38, 3.0f, 0x416, NoItem, false, Drawn}, Case{0x38, 3.0f, 0x811, NoItem, true, Trailed},
          Case{0x2a, 3.0f, 0x812, NoItem, true, Trailed}, Case{0x2d, 3.0f, 0xa11, NoItem, true, Trailed},
          Case{0x2a, 3.0f, 0xa12, NoItem, false, Drawn}})
    {
        SCOPED_TRACE(::testing::Message() << "acción " << static_cast<int>(probe.action) << ", frame " << probe.frame
                                          << ", manos " << probe.right << " y " << probe.left);
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.animation.action = probe.action;
        hero.animation.frame = probe.frame;
        hero.hands[0].model = probe.right;
        hero.hands[1].model = probe.left;
        const glm::vec3 retained{1.0f, 2.0f, 3.0f};
        hero.trailOrigin = retained;

        const auto stepped = tested.Step(Everything);

        const bool copies = !(probe.frame >= 3.0f) && probe.action != 0x8d;
        EXPECT_EQ(hero.trailOrigin, copies ? hero.position : retained);

        if (probe.trail)
        {
            EXPECT_TRUE(HasError(stepped, "0x4fd236")) << ErrorOf(stepped);
        }
        else
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// Lo que el cuerpo (0x4fd5f0) no tiene corta, en su orden y después de los rand() de antes: los mapas 18 a 23 con
// el héroe 10 s en una celda con el bit 8 (0x4fbe10 manda C1 06 AF 01, CL9.5), el 0xc5 (0x4fd603), el 0xf1 (0x4fb910),
// muriéndose (0x4fb9e0), +0x88 en 4 (0x4fcd0c), el nombre de 0x6c88b0 (0x4fcda2), otro modelo, Player.bmd sin cargar o
// una acción fuera de él (0x4f9580, 0x4f95c0) y la partícula de las 0x4d, 0x4f y 0x5a (0x4fdb70).
TEST(CharacterStepTest, TheBodyStopsWhereMuRetHasNothing)
{
    struct Case
    {
        std::string_view name;
        void (*prepare)(GameWorld&);
        std::string_view cut;
        int draws;
    };
    constexpr int AfterTheTimers = WallDraws + OwnRecordDraws;
    const std::array cases{
        Case{"mapa 18", [](GameWorld& tested) { tested.StandOnTheReportedCell(18); }, "0x4fbee8",
             WallDraws + HeroCellReads},
        Case{"mapa 23", [](GameWorld& tested) { tested.StandOnTheReportedCell(23); }, "0x4fbee8",
             WallDraws + HeroCellReads},
        Case{"el 0xc5", [](GameWorld& tested) { tested.Hero().type = 0xc5; }, "0x4fd603", WallDraws},
        Case{"el 0xf1", [](GameWorld& tested) { tested.Hero().type = 0xf1; }, "0x4fb910", AfterTheTimers},
        Case{"se muere", [](GameWorld& tested) { tested.Hero().dying = 1; }, "0x4fb9e0", OwnRecordDraws},
        Case{"+0x88 en 4", [](GameWorld& tested) { tested.Hero().byte88 = 4; }, "0x4fcd0c", AfterTheTimers},
        Case{"el nombre",
             [](GameWorld& tested)
             {
                 constexpr std::array<std::uint8_t, 11> Name{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7,
                                                             0x20, 0xb1, 0xc3, 0xbc, 0xf6};
                 for (std::size_t i = 0; i < tested.Hero().name.size(); ++i)
                 {
                     tested.Hero().name[i] = std::byte{Name[i]};
                 }
                 tested.Hero().byte1e3 = Name[10];
             },
             "0x4fcda2", AfterTheTimers},
        Case{"otro modelo", [](GameWorld& tested) { tested.Hero().type = 0x100; }, "0x4f9580", AfterTheTimers},
        Case{"sin Player.bmd", [](GameWorld& tested) { tested.player = nullptr; }, "0x4f9580", AfterTheTimers},
        Case{"una acción fuera",
             [](GameWorld& tested)
             {
                 tested.skeleton.actions.resize(2);
                 tested.Hero().animation.action = 5;
             },
             "0x4f95c0", AfterTheTimers},
        Case{"la partícula",
             [](GameWorld& tested)
             {
                 tested.Hero().animation.action = 0x4f;
                 tested.Hero().animation.frame = 5.6f;
             },
             "0x4fdb70", AfterTheTimers + BodyDraws},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested;
        probe.prepare(tested);

        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// Los bordes de esos cortes: los mapas 17 y 24, el nombre con +0x1e3 en 0 (el que deja la respuesta del F3 03) y
// la partícula en el frame 5,5 justo siguen el camino entero.
TEST(CharacterStepTest, TheBordersOfTheBodyCutsGoOn)
{
    for (const std::int32_t map : {17, 24})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        const auto stepped = tested.Step(Everything);
        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    }
    GameWorld named;
    constexpr std::array<std::uint8_t, 10> Name{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7, 0x20, 0xb1, 0xc3, 0xbc};
    for (std::size_t i = 0; i < Name.size(); ++i)
    {
        named.Hero().name[i] = std::byte{Name[i]};
    }
    const auto withName = named.Step(Everything);
    EXPECT_TRUE(withName.HasValue()) << withName.Error();
    GameWorld particle;
    particle.Hero().animation.action = 0x4f;
    particle.Hero().animation.frame = 5.5f;
    const auto atTheBorder = particle.Step(Everything);
    EXPECT_TRUE(atTheBorder.HasValue()) << atTheBorder.Error();
    EXPECT_EQ(particle.random.State(), RandomAfter(SeenHeroDraws + TrailDraws).State());
}

// 0x4fbe10 en los mapas 18 a 23, sin mandar nada: con otra celda guarda la del héroe y el reloj del mundo, y
// abre y cierra la X dos veces y la Y una (12 rand()); con la misma y menos de 10 s, no toca nada y lee las dos (8);
// con 10 s en una celda sin el bit 8, guarda el reloj. El resto del paso es el del mapa 17.
TEST(CharacterStepTest, ChaosCastleKeepsTheHeroCellAndItsClock)
{
    GameWorld plain(17);
    plain.Hero().cellX = 5;
    plain.Hero().cellY = 6;
    ASSERT_TRUE(plain.Step(Everything).HasValue());
    const auto after = [&](int extra)
    {
        CrtRandom expected = plain.random;
        for (int draw = 0; draw < extra; ++draw)
            static_cast<void>(expected.Next());
        return expected.State();
    };
    const auto step = [](GameWorld& tested, float clock)
    {
        return StepCharacters(CharacterStepContext{.world = tested.world,
                                                   .random = tested.random,
                                                   .scene = SceneId::Game,
                                                   .playerSkeleton = tested.player,
                                                   .worldClock = clock});
    };
    {
        SCOPED_TRACE("otra celda");
        GameWorld moved(18);
        moved.Hero().cellX = 5;
        moved.Hero().cellY = 6;
        moved.world.Runtime().terrainPolygon = Everything;
        const auto stepped = step(moved, 1234.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(moved.world.Runtime().reportedCellX, 5);
        EXPECT_EQ(moved.world.Runtime().reportedCellY, 6);
        EXPECT_EQ(moved.world.Runtime().reportedCellTime, 1234.0f);
        EXPECT_EQ(moved.random.State(), after(3 * Client::Render::Models::SmallLogicalCloseRandoms));
    }
    {
        SCOPED_TRACE("la misma, 9,999 s después");
        GameWorld waiting(18);
        waiting.StandOnTheReportedCell(18);
        waiting.world.Runtime().reportedCellTime = 0.0f;
        waiting.world.Runtime().terrainPolygon = Everything;
        const auto stepped = step(waiting, 9999.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(waiting.world.Runtime().reportedCellTime, 0.0f);
        EXPECT_EQ(waiting.random.State(), after(HeroCellReads));
    }
    {
        SCOPED_TRACE("la misma, 10 s después y sin el bit 8");
        GameWorld still(18);
        still.StandOnTheReportedCell(18);
        still.Cell(5, 6) = 0;
        still.world.Runtime().reportedCellTime = 0.0f;
        still.world.Runtime().terrainPolygon = Everything;
        const auto stepped = step(still, 10000.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(still.world.Runtime().reportedCellTime, 10000.0f);
        EXPECT_EQ(still.random.State(), after(HeroCellReads));
    }
}

// La celda (x, y) del buffer dinámico 0x7b4ccc4.
glm::vec3 DynamicLightAt(WorldState& world, std::uint8_t x, std::uint8_t y)
{
    const auto& cell = world.DynamicLighting().Lighting().cells[Client::Data::Terrain::CellIndex(x, y)];
    return {cell[0], cell[1], cell[2]};
}

// 0x506acf..0x506b62: con +0x375 debajo de 6, un arma con luz en cualquiera de las dos manos suma al buffer
// dinámico (0x5f9780) con la x y la y del objeto, sin rand(); el paso sigue hasta el final. El 0x21e da
// (0,8; 0,64; 0,4) con radio 3 y los otros cuatro (0,64; 0,4; 0,24) con radio 2, con los bits de los DWORD de
// 0x506af7..0x506b05 y 0x506b2d..0x506b3b. Con 6 o más, no hay luces.
TEST(CharacterStepTest, TheHandLightsAddToTheDynamicBufferBelowPkSix)
{
    struct Case
    {
        std::uint16_t model;
        std::array<std::uint32_t, 3> color;
        bool radiusThree;
    };
    constexpr std::array<std::uint32_t, 3> Bright{0x3f4ccccdu, 0x3f23d70bu, 0x3ecccccdu};
    constexpr std::array<std::uint32_t, 3> Dim{0x3f23d70bu, 0x3ecccccdu, 0x3e75c290u};
    for (const Case& probe : {Case{0x21e, Bright, true}, Case{0x225, Dim, false}, Case{0xa24, Dim, false},
                              Case{0xc1c, Dim, false}, Case{0x61f, Dim, false}})
    {
        for (const std::size_t hand : {std::size_t{0}, std::size_t{1}})
        {
            SCOPED_TRACE(::testing::Message() << probe.model << " en la mano " << hand);
            GameWorld tested;
            tested.Hero().hands[hand].model = probe.model;
            // En (0; 0): la celda del centro recibe el color entero, con peso 1.
            tested.Cell(0, 0) = 1;

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
            EXPECT_EQ(tested.Hero().safeZone, 1);
            const glm::vec3 center = DynamicLightAt(tested.world, 0, 0);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center.r), probe.color[0]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center.g), probe.color[1]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center.b), probe.color[2]);
            // A dos celdas, el peso de radio 2 es 0 y no toca la celda; el de radio 3 deja un tercio.
            const glm::vec3 two = DynamicLightAt(tested.world, 2, 0);
            if (probe.radiusThree)
            {
                EXPECT_GT(two.r, 0.0f);
            }
            else
            {
                EXPECT_EQ(two, glm::vec3(0.0f));
            }
            EXPECT_EQ(DynamicLightAt(tested.world, 3, 0), glm::vec3(0.0f));

            GameWorld outlaw;
            outlaw.Hero().hands[hand].model = probe.model;
            outlaw.Hero().pk = 6;
            EXPECT_TRUE(outlaw.Step(Everything).HasValue());
            EXPECT_EQ(outlaw.random.State(), RandomAfter(SeenHeroDraws).State());
            EXPECT_EQ(DynamicLightAt(outlaw.world, 0, 0), glm::vec3(0.0f));
        }
    }
}

// Con luz en las dos manos, primero la derecha y después la izquierda: cada canal suma en double y guarda en
// float, sobre lo que dejó la otra.
TEST(CharacterStepTest, BothHandsAddTheirLightsRightFirst)
{
    GameWorld tested;
    tested.Hero().hands[0].model = 0x21e;
    tested.Hero().hands[1].model = 0x61f;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    const glm::vec3 center = DynamicLightAt(tested.world, 0, 0);
    const auto sum = [](std::uint32_t first, std::uint32_t second)
    {
        return static_cast<float>(static_cast<double>(std::bit_cast<float>(second)) +
                                  static_cast<double>(std::bit_cast<float>(first)));
    };
    EXPECT_EQ(center.r, sum(0x3f4ccccdu, 0x3f23d70bu));
    EXPECT_EQ(center.g, sum(0x3f23d70bu, 0x3ecccccdu));
    EXPECT_EQ(center.b, sum(0x3ecccccdu, 0x3e75c290u));
}

// El caso del 0x20a en 0x506cf8 y los pasos del héroe (0x50862f), después del rand() & 7: el mapa 7 y las
// acciones 0x44, 0x45 y 0x96 cortan (el 7 sin el pool de la burbuja); el 9 tiene su test. Los pasos ya producen
// solicitudes CL10 sin cortar ni agregar RNG.
TEST(CharacterStepTest, ThePlayerEffectsAndStepsStopAfterTheRandom)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t action;
        std::string_view cut;
    };
    for (const Case& probe : {Case{7, 1, "0x506d77"}, Case{0, 0x44, "0x506e5d"}, Case{0, 0x45, "0x506e5d"},
                              Case{0, 0x96, "0x506f62"}, Case{0, 0x0d, ""}, Case{0, 0x21, ""}, Case{2, 0x8b, ""},
                              Case{2, 0x8c, ""}, Case{2, 0x4b, ""}, Case{2, 0x0c, ""}, Case{0, 0x22, ""}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", acción " << static_cast<int>(probe.action));
        GameWorld tested(probe.map);
        tested.Hero().animation.action = probe.action;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
    }
}

// 0x508ba0 con otro que el héroe, quieto: la X lógica y la Y en dos accesos que se cierran (8 rand()), iguales a
// la copia en claro (0x511290 escribe las dos), así que no busca camino (Q-C236). Su cuerpo (0x4fd5f0) saca el
// rand() % 6, sin el registro propio, y su 0x505de0, visto, la celda (8 más), la cabeza que sortea (0x5068fd) y el
// rand() & 7. Después, el héroe del lugar 42 hace su paso entero. En Lorencia y en Devias.
TEST(CharacterStepTest, AnotherCharacterRunsItsWholeStepBeforeTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& other = tested.Other(3);
        CrtRandom expected(Seed);
        Skip(expected, 2 * WallDraws);
        CharacterHead head;
        static_cast<void>(DrawASeenOther(expected, head));
        Skip(expected, OwnRecordDraws + BodyDraws + SeenEffectsDraws);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(other.seen);
        EXPECT_EQ(other.cellXAccesses, 0);
        EXPECT_EQ(other.cellYAccesses, 0);
        EXPECT_EQ(tested.Cell(3, 4), Client::World::CharacterWallBit);
        EXPECT_EQ(other.headTurn.angles, head.angles);
        EXPECT_EQ(other.headTurn.target, head.target);
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// 0x5068eb..0x5069ab con otro que el héroe, paso a paso: rand() % 32 en 0 sortea el blanco con dos rand() más
// (+0x34 y +0x38), y los dos ángulos giran hacia él (+0x28 y +0x2c), como en el login. Con el héroe muerto, cada
// paso saca el TerrainWall del otro y lo suyo, con 1 o 3 en la cabeza. En Lorencia y en Devias.
TEST(CharacterStepTest, TheHeadOfAnotherCharacterSortsOneStepInThirtyTwo)
{
    constexpr int Steps = 200;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        tested.Hero().alive = false;
        ActorState& other = tested.Other(3);
        CrtRandom expected(Seed);
        CharacterHead head;
        int sorted = 0;
        for (int step = 1; step <= Steps; ++step)
        {
            SCOPED_TRACE(::testing::Message() << "paso " << step);
            Skip(expected, WallDraws);
            sorted += DrawASeenOther(expected, head) ? 1 : 0;

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested.random.State(), expected.State());
            ASSERT_EQ(other.headTurn.angles, head.angles);
            ASSERT_EQ(other.headTurn.target, head.target);
        }
        EXPECT_GT(sorted, 0) << "la cabeza tiene que sortear al menos una vez";
        EXPECT_NE(other.headTurn.angles, glm::vec2(0.0f));
    }
}

// Con el héroe la cabeza no sortea ni gasta: gira hacia el blanco (+0x34 y +0x38), que MoveHero escribe con el
// mouse (CL7.4), a lo sumo el 20 % de lo que falta por paso. En Lorencia y en Devias.
TEST(CharacterStepTest, TheHeroTurnsItsHeadWithoutRandoms)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& hero = tested.Hero();
        hero.headTurn.target = glm::vec2(40.0f, 350.0f);
        CharacterHead head = hero.headTurn;
        CrtRandom untouched(Seed);
        StepCharacterHead(head, false, untouched);
        ASSERT_EQ(untouched.State(), RandomAfter(0).State());
        ASSERT_NE(head.angles, glm::vec2(0.0f));

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(hero.headTurn.angles, head.angles);
        EXPECT_EQ(hero.headTurn.target, glm::vec2(40.0f, 350.0f));
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
    }
}

// 0x5068be..0x5068d4: con +0x78 & 0x20 o & 0x1000 la cabeza no se mueve ni sortea, y el paso sigue con el
// rand() & 7. Con 0x1000 la acción además termina sin avanzar y vuelve a la de parado (0x4f2250, con el rand() de
// su sufijo). En Lorencia y en Devias.
TEST(CharacterStepTest, TheFlagsOf78KeepTheHeadStill)
{
    struct Case
    {
        std::int32_t map;
        std::uint32_t flags;
        int stopDraws;
    };
    for (const Case& probe :
         {Case{0, 0x20, 0}, Case{2, 0x1000, StopDraws}, Case{0, 0x1020, StopDraws}, Case{2, 0x20, 0}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", +0x78 = " << probe.flags);
        GameWorld tested(probe.map);
        tested.Hero().alive = false;
        ActorState& other = tested.Other(3);
        other.flags78 = probe.flags;
        other.headTurn.target = glm::vec2(30.0f, 10.0f);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(other.headTurn.angles, glm::vec2(0.0f));
        EXPECT_EQ(other.headTurn.target, glm::vec2(30.0f, 10.0f));
        EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OtherCellDraws + BodyDraws + probe.stopDraws +
                                                     OtherSeenCellDraws + EffectsRandomDraws)
                                             .State());
    }
}

// 0x50862f..0x508641: los pasos (0x505770) son solo del héroe publicado. Otro en las acciones de los pasos no
// corta y sigue hasta el final. En Lorencia y en Devias.
TEST(CharacterStepTest, TheStepSoundsAreOnlyTheHeros)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t action : std::array<std::uint8_t, 6>{0x0d, 0x15, 0x21, 0x8b, 0x8c, 0x4b})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", acción " << static_cast<int>(action));
            GameWorld tested(map);
            tested.Hero().alive = false;
            tested.Other(3).animation.action = action;
            CrtRandom expected(Seed);
            Skip(expected, WallDraws);
            static_cast<void>(DrawASeenOther(expected));

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(tested.random.State(), expected.State());
        }
    }
}

// 0x5086cf..0x5087fc: en Kalima (los mapas 24 a 29 y el 36), el 0x20a en las acciones 0x15, 0x1d a 0x21 y 0x4b
// saca dos rand() % 64 y el 0x6f4, que corta. En los mapas de al lado, o con otra acción, sigue. El héroe ya
// atraviesa la solicitud de sonido 0x508668 y alcanza la misma partícula, con sus dos rand adicionales.
TEST(CharacterStepTest, KalimaParticleCutsAfterItsTwoRandoms)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t action;
        bool cuts;
    };
    for (const Case& probe :
         {Case{24, 0x15, true}, Case{25, 0x1d, true}, Case{26, 0x1e, true}, Case{27, 0x1f, true}, Case{28, 0x20, true},
          Case{29, 0x21, true}, Case{36, 0x4b, true}, Case{35, 0x15, false}, Case{37, 0x4b, false},
          Case{24, 0x16, false}, Case{29, 0x1c, false}, Case{36, 0x0d, false}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", acción " << static_cast<int>(probe.action));
        GameWorld tested(probe.map);
        tested.Hero().alive = false;
        tested.Other(3).animation.action = probe.action;
        CrtRandom expected(Seed);
        Skip(expected, WallDraws);
        static_cast<void>(DrawASeenOther(expected));
        Skip(expected, probe.cuts ? 2 : 0);

        const auto stepped = tested.Step(Everything);

        if (probe.cuts)
        {
            EXPECT_TRUE(HasError(stepped, "0x5087fc")) << ErrorOf(stepped);
            EXPECT_NE(stepped.Error().find("lugar 3 "), std::string::npos) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(tested.random.State(), expected.State());
    }
    GameWorld hero(24);
    hero.Hero().animation.action = 0x15;
    EXPECT_TRUE(HasError(hero.Step(Everything), "0x5087fc"));
    EXPECT_EQ(hero.random.State(), RandomAfter(SeenHeroDraws + 2).State());
}

// Después de la cabeza, otro que el héroe sigue el camino del 0x20a del héroe: la luz de una mano suma al buffer
// dinámico y sigue; el mapa 7 (solo en el juego) y las acciones 0x44, 0x45 y 0x96 cortan después del rand() & 7.
TEST(CharacterStepTest, AfterItsHeadAnotherCharacterTakesThePathOfTheHero)
{
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        std::uint8_t action;
        std::uint16_t rightHand;
        std::string_view cut;
        bool drawsTheRandom;
    };
    constexpr std::uint16_t NoItem = Client::Scenes::NoItemModel;
    for (const Case& probe :
         {Case{"el mapa 7", 7, 1, NoItem, "0x506d77", true}, Case{"la 0x44", 2, 0x44, NoItem, "0x506e5d", true},
          Case{"la 0x45", 0, 0x45, NoItem, "0x506e5d", true}, Case{"la 0x96", 2, 0x96, NoItem, "0x506f62", true}})
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested(probe.map);
        tested.Hero().alive = false;
        ActorState& other = tested.Other(3);
        other.animation.action = probe.action;
        other.hands[0].model = probe.rightHand;
        CrtRandom expected(Seed);
        Skip(expected, WallDraws);
        static_cast<void>(DrawASeenOther(expected));
        CrtRandom withoutTheRandom(Seed);
        Skip(withoutTheRandom, WallDraws + OtherCellDraws + BodyDraws + OtherSeenCellDraws);
        CharacterHead head;
        StepCharacterHead(head, true, withoutTheRandom);

        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(tested.random.State(), (probe.drawsTheRandom ? expected : withoutTheRandom).State());
    }
    GameWorld selection(7);
    selection.Hero().alive = false;
    static_cast<void>(selection.Other(3));
    CrtRandom expected(Seed);
    Skip(expected, WallDraws);
    static_cast<void>(DrawASeenOther(expected));
    const auto stepped = selection.Step(Everything, SceneId::CharacterSelect);
    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(selection.random.State(), expected.State());

    GameWorld lit;
    lit.Hero().alive = false;
    lit.Other(3).hands[0].model = 0x21e;
    CrtRandom afterTheLight(Seed);
    Skip(afterTheLight, WallDraws);
    static_cast<void>(DrawASeenOther(afterTheLight));
    const auto litStep = lit.Step(Everything);
    EXPECT_TRUE(litStep.HasValue()) << ErrorOf(litStep);
    EXPECT_EQ(lit.random.State(), afterTheLight.State());
    EXPECT_EQ(DynamicLightAt(lit.world, 0, 0).r, std::bit_cast<float>(0x3f4ccccdu));
}

// Las salidas de 0x508ba0 antes de la celda: el que se muere sale (0x508bb6) y corta en su cuerpo (0x4fb9e0),
// salvo el 0x154, que toma la altura del terreno (0x508e89); el que camina sigue su camino (0x508e27); el 0x159
// sale sin leer la celda y corta en su modelo (0x4f9580). Cada uno, con el héroe vivo en el lugar 42, después del
// TerrainWall.
TEST(CharacterStepTest, TheExitsOf508ba0BeforeTheCell)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t type;
        std::uint8_t dying;
        std::uint8_t movement;
        std::string_view cut;
        int draws;
    };
    constexpr std::uint16_t Player = Client::Data::Models::PlayerSkeletonModel;
    for (const Case& probe : {Case{"se muere", Player, 1, 0, "0x4fb9e0", WallDraws},
                              Case{"se muere el 0x154", 0x154, 1, 0, "0x508e89", WallDraws},
                              // Walk ya existe: consume su rand antes de que Position encuentre la altura ausente.
                              Case{"camina", Player, 0, 1, "0x508a80", 2 * WallDraws + 1},
                              Case{"el 0x159", 0x159, 0, 0, "0x4f9580", 2 * WallDraws}})
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested;
        ActorState& other = tested.world.Actors()[3];
        other.alive = true;
        other.byte88 = 1;
        other.type = probe.type;
        other.dying = probe.dying;
        other.movement = probe.movement;

        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// Una cuenta abierta no se vuelve a cifrar al cerrar: ni en el TerrainWall, ni en 0x508ba0, ni en la celda de
// 0x505de0. Al otro le quedan el rand() % 6, la cabeza y el rand() & 7, y después el héroe hace su paso. En
// Lorencia y en Devias.
TEST(CharacterStepTest, AnotherCharacterWithOpenCellAccessesSpendsNothingOnThem)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& other = tested.Other(3);
        other.cellXAccesses = 1;
        other.cellYAccesses = 1;
        CrtRandom expected(Seed);
        Skip(expected, WallDraws + BodyDraws);
        CharacterHead head;
        StepCharacterHead(head, true, expected);
        Skip(expected, EffectsRandomDraws + OwnRecordDraws + BodyDraws + SeenEffectsDraws);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(other.cellXAccesses, 1);
        EXPECT_EQ(other.cellYAccesses, 1);
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// La selección (0x6342f9) y el login (0x634d13) sobre su mapa del repo, con el .att del mapa en el global del
// TerrainWall (0x7d0d348, que 0x61a560 pisa), el mundo en curso y el héroe publicado en `hero`. Los vivos son
// jugadores de pie en sus lugares, con las dos copias de la celda en (0, 0), como los deja 0x511290 desde
// 0x517270 o 0x6349a0, y +0x88 en 1. El registro propio queda como lo deja el login, en cero
// (0x4baf79..0x4bafc2): su equipo es del tipo 0, y 0x54efe0 lee el +0x2d de sus alas en Item.bmd (`items`).
struct SceneCharacters
{
    SceneCharacters(const std::filesystem::path& dataRoot, std::uint8_t map, std::size_t hero)
    {
        auto loaded = Client::World::LoadWorldTerrain(dataRoot, map);
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << loaded.Error();
            return;
        }
        world.Map().terrain = std::move(loaded).Value();
        world.Runtime().map = map;
        world.Runtime().hero = hero;
    }

    ActorState& Place(std::size_t slot, const glm::vec3& position)
    {
        ActorState& actor = world.Actors()[slot];
        actor = Client::Tests::UnarmedPlayer(1);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.position = position;
        return actor;
    }

    // Los cinco lugares de 0x4c20a0, con la z de 0x634680.
    void PlaceTheFiveSlots()
    {
        for (std::size_t slot = 0; slot < Client::Scenes::CharacterSlots.size(); ++slot)
        {
            const glm::vec2 xy = Client::Scenes::CharacterSlots[slot].position;
            Place(slot, glm::vec3(xy.x, xy.y, Client::Scenes::CharacterSlotHeight));
        }
    }

    Client::Core::Expected<void, std::string> Step(SceneId scene)
    {
        return StepCharacters(CharacterStepContext{
            .world = world, .random = random, .scene = scene, .playerSkeleton = &skeleton, .items = items});
    }

    WorldState world;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    const Client::Data::Tables::ItemTable* items = nullptr;
};

// El trapecio que deja el dibujo de la selección (0x63475c), el de su punto fijo.
TerrainPolygon SelectionPolygon()
{
    return Client::World::TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, SelectionMap,
                                           Client::Scenes::SelectionCullingPoint,
                                           Client::Scenes::SelectionCamera().angles.z);
}

// La selección con los cinco lugares y el héroe en el 0, con el trapecio de su dibujo, paso a paso: los cinco se
// ven (P-C46 b, con la cuenta de MuRet) y marcan su celda lógica, la (0, 0), con 8 rand() cada uno. El héroe sale de
// 0x508ba0 sin gastar (0x508bad) y hace el camino del juego: su cuerpo con el registro propio y su 0x505de0 entero.
// Los lugares 1 a 4 gastan su 0x508ba0, su cuerpo, su celda, su cabeza (1 o 3) y el rand() & 7. Por paso, 40 +
// 3.858 + 4 · 19 = 3.974, más 2 por cada cabeza que sortea (*simulado*). El esqueleto de prueba no tiene velocidad:
// la animación no avanza, sin el rand() de 0x432bf1 desde el frame 3 ni el del sufijo al terminar la acción.
TEST_F(GoldenCharacterStep, TheSelectionStepsItsFiveSlots)
{
    constexpr int Steps = 64;
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    SceneCharacters tested(m_dataRoot, SelectionMap, 0);
    tested.items = &items->table;
    tested.PlaceTheFiveSlots();
    tested.world.Runtime().terrainPolygon = SelectionPolygon();
    CrtRandom expected(Seed);
    std::array<CharacterHead, Client::Scenes::CharacterSlots.size()> heads{};
    int sorted = 0;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(::testing::Message() << "paso " << step);
        Skip(expected, (5 * WallDraws) + OwnRecordDraws + BodyDraws + SeenEffectsDraws);
        for (std::size_t slot = 1; slot < heads.size(); ++slot)
        {
            sorted += DrawASeenOther(expected, heads[slot]) ? 1 : 0;
        }

        const auto stepped = tested.Step(SceneId::CharacterSelect);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(tested.random.State(), expected.State());
        for (std::size_t slot = 0; slot < heads.size(); ++slot)
        {
            ASSERT_TRUE(tested.world.Actors()[slot].seen) << "lugar " << slot;
            ASSERT_EQ(tested.world.Actors()[slot].headTurn.target, heads[slot].target) << "lugar " << slot;
        }
    }
    EXPECT_GT(sorted, 0);
    const std::uint16_t origin = tested.world.Map().terrain.attributes->cells[0];
    EXPECT_EQ(origin & Client::World::CharacterWallBit, Client::World::CharacterWallBit);
}

// Sin Item.bmd, el héroe de la selección corta en las alas del tipo 0 de su registro en cero (0x54efe0, desde
// 0x4fc450), después de los TerrainWall y de cerrar el registro propio.
TEST_F(GoldenCharacterStep, WithoutItemBmdTheSelectionHeroStopsAtItsWings)
{
    SceneCharacters tested(m_dataRoot, SelectionMap, 0);
    tested.PlaceTheFiveSlots();
    tested.world.Runtime().terrainPolygon = SelectionPolygon();

    const auto stepped = tested.Step(SceneId::CharacterSelect);

    ASSERT_TRUE(HasError(stepped, "0x54efe0 (0x54f30e)")) << ErrorOf(stepped);
    EXPECT_EQ(tested.random.State(), RandomAfter((5 * WallDraws) + OwnRecordDraws).State());
}

// Después de la vuelta, el héroe publicado sigue en el lugar del juego (Q-C213), que 0x510f70(−1) mató: los cinco
// son otros, sin el registro propio, y cada uno saca su TerrainWall, su 0x508ba0, su cuerpo, su celda, su cabeza y
// el rand() & 7: 40 + 5 · 19 = 135 por paso, más 2 por cada cabeza que sortea (*simulado*), con la animación
// quieta.
TEST_F(GoldenCharacterStep, AfterTheReturnTheFiveSlotsAreOthers)
{
    constexpr int Steps = 64;
    SceneCharacters tested(m_dataRoot, SelectionMap, 42);
    tested.PlaceTheFiveSlots();
    tested.world.Runtime().terrainPolygon = SelectionPolygon();
    CrtRandom expected(Seed);
    std::array<CharacterHead, Client::Scenes::CharacterSlots.size()> heads{};
    int sorted = 0;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(::testing::Message() << "paso " << step);
        Skip(expected, 5 * WallDraws);
        for (CharacterHead& head : heads)
        {
            sorted += DrawASeenOther(expected, head) ? 1 : 0;
        }

        const auto stepped = tested.Step(SceneId::CharacterSelect);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(tested.random.State(), expected.State());
        for (std::size_t slot = 0; slot < heads.size(); ++slot)
        {
            ASSERT_EQ(tested.world.Actors()[slot].headTurn.angles, heads[slot].angles) << "lugar " << slot;
        }
    }
    EXPECT_GT(sorted, 0);
}

// El login con el personaje del lugar 2 en su lugar (0x6349a0, LoginCharacterTransform), con el cetro y el cuervo en
// las manos (sin luz) y en su acción de parado, y el héroe del lugar 0 sin construir, desde el trapecio de la .bss
// del primer paso (Q-C217): por paso, 8 rand() del TerrainWall, 8 de su 0x508ba0, el de su cuerpo, 8 de su celda, su
// cabeza y el rand() & 7: 27, y 29 cuando la cabeza sortea (*simulado*), con la animación quieta (sin el rand() de
// 0x432bf1 desde el frame 3). La cabeza es la que el login corre por su cuenta (LoginScene, 06 § Lo demás del
// login).
TEST_F(GoldenCharacterStep, TheLoginStepsItsCharacter)
{
    constexpr int Steps = 200;
    SceneCharacters tested(m_dataRoot, LoginMap, 0);
    ActorState& login = tested.Place(2, Client::Scenes::LoginCharacterTransform().position);
    login.hands[0].model = LoginScepter;
    login.hands[1].model = Client::Scenes::LoginRavenItem;
    login.animation.action = Client::Scenes::LoginCharacterStandingAction;
    CrtRandom expected(Seed);
    CharacterHead head;
    int sorted = 0;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(::testing::Message() << "paso " << step);
        Skip(expected, WallDraws);
        sorted += DrawASeenOther(expected, head) ? 1 : 0;

        const auto stepped = tested.Step(SceneId::Login);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_TRUE(login.seen);
        ASSERT_EQ(tested.random.State(), expected.State());
        ASSERT_EQ(login.headTurn.angles, head.angles);
        ASSERT_EQ(login.headTurn.target, head.target);
    }
    EXPECT_GT(sorted, 0);
    EXPECT_FALSE(tested.world.Actors()[0].alive);
}

// Fuera del juego el héroe hace el mismo camino: con el héroe solo, en un mapa del juego (el 0 y el 2) y con la
// selección o el login de escena, el cuerpo y 0x505de0 enteros, con los mismos rand().
TEST(CharacterStepTest, OutsideTheGameTheHeroTakesTheSamePath)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const SceneId scene : {SceneId::CharacterSelect, SceneId::Login})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << Client::Scenes::SceneName(scene));
            GameWorld tested(map);
            tested.Hero().safeZone = 1;

            const auto stepped = tested.Step(Everything, scene);

            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(tested.Hero().safeZone, 0);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// 0x506cf8: el 0x607 del mapa 7 es solo de la escena 5; en las otras el mapa 7 sigue de largo. Sin el pool de
// partículas, la burbuja corta.
TEST(CharacterStepTest, TheClockEffectOfMapSevenIsOnlyInTheGame)
{
    GameWorld game(7);
    EXPECT_TRUE(HasError(game.Step(Everything), "0x506d77"));
    GameWorld selection(7);
    const auto stepped = selection.Step(Everything, SceneId::CharacterSelect);
    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(selection.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x506d0a..0x506d7c: en el juego, en el mapa 7, el segundo de cada diez del reloj del mundo (ftol % 10000 debajo
// de 1000) saca la burbuja 0x607/0 (dos rand()) del hueso de la cabeza (20) de los huesos del último dibujo:
// (0, 20, −10) por la escala, más la posición, con los ángulos del personaje y la luz en 1. Sin dibujo todavía,
// de la posición (Q-C318).
TEST(CharacterStepTest, AtlansBlowsABubbleFromTheHeadOneSecondInTen)
{
    struct Case
    {
        float clock;
        bool drawn;
        bool blows;
    };
    for (const Case& probe : {Case{0.0f, true, true}, Case{999.9f, true, true}, Case{1000.0f, true, false},
                              Case{10500.0f, true, true}, Case{19999.0f, true, false}, Case{500.0f, false, true}})
    {
        SCOPED_TRACE(::testing::Message() << "reloj " << probe.clock << (probe.drawn ? ", dibujado" : ", sin dibujo"));
        GameWorld tested(7);
        ActorState& hero = tested.Hero();
        hero.scale = 2.0f;
        hero.angles = {0.0f, 0.0f, 45.0f};
        if (probe.drawn)
        {
            hero.bodyPose.assign(Client::Data::Models::PlayerHeadBone + 1, Client::Render::Models::BoneTransform(0.0f));
            Client::Render::Models::BoneTransform& head = hero.bodyPose[Client::Data::Models::PlayerHeadBone];
            head = Client::Render::Models::BoneTransform(1.0f);
            head[3] = glm::vec3(5.0f, 6.0f, 7.0f);
        }
        tested.world.Runtime().terrainPolygon = Everything;
        Client::World::Particles particles;

        const auto stepped = StepCharacters(CharacterStepContext{.world = tested.world,
                                                                 .random = tested.random,
                                                                 .scene = SceneId::Game,
                                                                 .playerSkeleton = tested.player,
                                                                 .particles = &particles,
                                                                 .worldClock = probe.clock});

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const Client::World::Particle& bubble = particles.All()[0];
        EXPECT_EQ(bubble.alive, probe.blows);
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + (probe.blows ? 2 : 0)).State());
        if (!probe.blows)
            continue;
        EXPECT_EQ(bubble.type, Client::World::DropParticle);
        EXPECT_EQ(bubble.subtype, 0);
        const glm::vec3 local = probe.drawn ? glm::vec3(10.0f, 52.0f, -6.0f) : glm::vec3(0.0f);
        EXPECT_EQ(bubble.position, hero.position + local);
        EXPECT_EQ(bubble.angles, hero.angles);
        EXPECT_EQ(bubble.light, glm::vec3(1.0f));
    }
}

// 0x506d7f..0x506e54: en el mapa 9, en cualquier escena, dos rand() & 3; con 0, una gota 0x68/0 del hueso 26 (el
// primero) y del 35 (el segundo) del último dibujo: (−15, 0, 0) más la traslación del hueso, por la escala, más la
// posición, con los ángulos del personaje y la luz en 1. Sin el pool corta sólo si tenía que crearla.
TEST(CharacterStepTest, DevilSquareRainsFromTwoBonesOfEachCharacter)
{
    struct Drop
    {
        std::size_t bone;
        glm::vec3 local;
        std::string_view address;
    };
    constexpr std::array<Drop, 2> Drops{Drop{26, {-20.0f, 12.0f, 14.0f}, "0x506dff"},
                                        Drop{35, {-28.0f, 4.0f, 6.0f}, "0x506e4f"}};
    std::array<int, 3> rained{};
    for (std::uint32_t seed = 1; seed <= 24; ++seed)
    {
        for (const bool pooled : {true, false})
        {
            SCOPED_TRACE(::testing::Message() << "semilla " << seed << (pooled ? ", con pool" : ", sin pool"));
            GameWorld tested(9);
            tested.random = CrtRandom(seed);
            ActorState& hero = tested.Hero();
            hero.scale = 2.0f;
            hero.angles = {0.0f, 0.0f, 45.0f};
            hero.bodyPose.assign(36, Client::Render::Models::BoneTransform(0.0f));
            hero.bodyPose[26] = Client::Render::Models::BoneTransform(1.0f);
            hero.bodyPose[26][3] = glm::vec3(5.0f, 6.0f, 7.0f);
            hero.bodyPose[35] = Client::Render::Models::BoneTransform(1.0f);
            hero.bodyPose[35][3] = glm::vec3(1.0f, 2.0f, 3.0f);
            tested.world.Runtime().terrainPolygon = Everything;
            Client::World::Particles particles;

            const auto stepped = StepCharacters(CharacterStepContext{.world = tested.world,
                                                                     .random = tested.random,
                                                                     .scene = SceneId::Game,
                                                                     .playerSkeleton = tested.player,
                                                                     .particles = pooled ? &particles : nullptr});

            CrtRandom oracle(seed);
            Skip(oracle, SeenHeroDraws);
            Client::World::Particles expected;
            std::string_view cut;
            int drops = 0;
            for (const Drop& drop : Drops)
            {
                if (oracle.Next() % 4 != 0)
                    continue;
                ++drops;
                if (!pooled)
                {
                    cut = drop.address;
                    break;
                }
                ASSERT_TRUE(expected
                                .CreateChecked(0x68, hero.position + drop.local, hero.angles, glm::vec3(1.0f), 0, 1.0f,
                                               0.0f, oracle, 0)
                                .HasValue());
            }
            EXPECT_EQ(tested.random.State(), oracle.State());
            if (!cut.empty())
            {
                EXPECT_TRUE(HasError(stepped, cut)) << ErrorOf(stepped);
                continue;
            }
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            if (pooled)
                ++rained[static_cast<std::size_t>(drops)];
            for (std::size_t index = 0; index < 2; ++index)
            {
                const Client::World::Particle& made = particles.All()[index];
                const Client::World::Particle& wanted = expected.All()[index];
                EXPECT_EQ(made.alive, wanted.alive);
                if (!wanted.alive)
                    continue;
                EXPECT_EQ(made.type, 0x68);
                EXPECT_EQ(made.subtype, 0);
                EXPECT_EQ(made.position, wanted.position);
                EXPECT_EQ(made.angles, hero.angles);
                EXPECT_EQ(made.light, glm::vec3(1.0f));
                EXPECT_EQ(made.life, wanted.life);
                EXPECT_EQ(made.velocity, wanted.velocity);
            }
        }
    }
    EXPECT_GT(rained[0], 0);
    EXPECT_GT(rained[1], 0);
}

// En la selección (la escena 4) 0x4ec200 decide el final con el frame + 2 (Q-C21): la acción de 6 keys a 1 por
// paso termina en el frame 4 y vuelve a la de parado, con el rand() del sufijo de 0x4f2250; en el juego, no.
TEST(CharacterStepTest, TheSelectionEndsTheActionTwoFramesEarlier)
{
    for (const SceneId scene : {SceneId::Game, SceneId::CharacterSelect})
    {
        SCOPED_TRACE(Client::Scenes::SceneName(scene));
        GameWorld tested;
        tested.skeleton = SkeletonWith(3, 6, 1.0f);
        ActorState& hero = tested.Hero();
        hero.animation.action = 3;
        hero.animation.frame = 3.0f;
        tested.Cell(0, 0) = 1;

        const auto stepped = tested.Step(Everything, scene);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const bool ended = scene == SceneId::CharacterSelect;
        EXPECT_EQ(hero.animation.action, ended ? 1 : 3);
        EXPECT_EQ(hero.animation.frame, ended ? 0.0f : 4.0f);
        const int draws = SeenHeroDraws + (ended ? StopDraws : TrailDraws);
        EXPECT_EQ(tested.random.State(), RandomAfter(draws).State());
    }
}
} // namespace

TEST(CharacterStepTest, KalimaFootstepCallerCreatesSubtypeOneInTheSharedPool)
{
    GameWorld tested(24);
    tested.Hero().animation.action = 0x15;
    tested.world.Runtime().terrainPolygon = Everything;
    Client::World::Particles particles;
    const auto stepped = StepCharacters(
        {.world = tested.world, .random = tested.random, .playerSkeleton = tested.player, .particles = &particles});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto& p = particles.All()[0];
    ASSERT_TRUE(p.alive);
    EXPECT_EQ(p.type, 0x6f4);
    EXPECT_EQ(p.subtype, 1);
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.light, glm::vec3(0.3f));
    EXPECT_EQ(p.position.z, tested.Hero().position.z + 100);
    EXPECT_EQ(p.start.z, tested.Hero().position.z + 50);
    EXPECT_EQ(p.scale, 1);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + 3).State());
}
