#include "Scenes/LetterPortrait.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Character/Opcodes.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Letters.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Pass3D.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameLetters.h"
#include "Scenes/GameUi.h"
#include "Scenes/SceneId.h"
#include "Scenes/HelperTable.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SelectionState.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "UI/LetterModel.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El personaje del retrato de las cartas (CL9 hito 10, P-C212): 0x4961d0, 0x4977d0 con 0x513100, 0x497810 con 0x5139f0,
// 0x496530 con sus dos copias y 0x495a20, con los rand() en el orden del ASM, en Lorencia (0) y Devias (2).
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::GameLogic::BodyPart;
using Client::GameLogic::ClassPartModel;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::LetterPortrait;
using Client::Scenes::NoItemModel;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x1e77e5;
// 0x4961d0 desde cero: las dos entradas del personaje (4 + 4), la celda del héroe (4 + 4) y 0x511290 (28).
constexpr int BuildDraws = 44;
// El cierre del registro propio (0x40b4d0 y sus copias en línea): un rand() por byte.
constexpr int OwnRecordDraws = 0xf08;
constexpr std::uint8_t HeroX = 0x88;
constexpr std::uint8_t HeroY = 0x82;
constexpr std::uint8_t DarkKnight = 1 << 5;

using Equipment = std::array<std::byte, Client::Protocol::Character::EquipmentSize>;

// Sin nada: las manos 0xfff, las alas de b[4] en 3 con b[8] & 7 en 0 y la mascota de b[4] en 3 con b[9] & 1 en 0.
Equipment NothingWorn()
{
    Equipment equipment{};
    equipment.fill(std::byte{0xff});
    equipment[8] = std::byte{0xf8};
    equipment[9] = std::byte{0};
    equipment[10] = std::byte{0};
    equipment[11] = std::byte{0xf0};
    equipment[15] = std::byte{0};
    return equipment;
}

// Cuántos rand() hay de `from` a `to` (hasta 20.000).
int DrawsBetween(CrtRandom from, const CrtRandom& to)
{
    for (int draws = 0; draws < 20000; ++draws)
    {
        if (from.State() == to.State())
            return draws;
        static_cast<void>(from.Next());
    }
    return -1;
}

// Lo que dibuja el retrato: Player.bmd con un hueso y una key en cada acción, el caballo oscuro (0xbf) con sus siete
// acciones de 10 keys en un hueso, y un triángulo para el caballo, la sombra (0x20b) y la armadura del Dark Knight.
class PortraitModels final : public Client::Scenes::IModelLibrary
{
public:
    PortraitModels()
    {
        Client::Render::Models::SkeletonBone body;
        body.actions.assign(Client::Tests::PlayerActionCount, {Client::Render::Models::BoneKey{}});
        m_player.bones.push_back(body);
        m_player.actions.assign(Client::Tests::PlayerActionCount, {.keyCount = 1});
        constexpr std::size_t HorseActions = 7;
        constexpr std::int16_t HorseKeys = 10;
        Client::Render::Models::SkeletonBone horse;
        Client::Render::Models::BoneKey still;
        still.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        horse.actions.assign(HorseActions,
                             std::vector<Client::Render::Models::BoneKey>(static_cast<std::size_t>(HorseKeys), still));
        m_horse.bones.push_back(horse);
        m_horse.actions.assign(HorseActions, {.repeats = true, .speed = 0.0f, .keyCount = HorseKeys});
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "retrato.jpg";
        m_triangle.meshes.push_back(mesh);
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        const bool drawn = model == Client::Scenes::DarkHorseModel ||
                           model == Client::Data::Models::PlayerShadowModel ||
                           model == ClassPartModel(BodyPart::Armor, DarkKnight);
        return drawn ? &m_triangle : nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == PlayerSkeletonModel)
            return &m_player;
        return model == Client::Scenes::DarkHorseModel ? &m_horse : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return m_textures;
    }

private:
    Client::Render::Models::Skeleton m_player;
    Client::Render::Models::Skeleton m_horse;
    Client::Data::Models::Model m_triangle;
    std::array<Client::Render::TextureSlot, 1> m_textures{Client::Render::TextureSlot{0x300}};
};

// El mundo del juego, en el heap: WorldState pesa unos 350 KB.
struct Game
{
    Game()
    {
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer(DarkKnight);
        hero.alive = true;
        hero.cellX = HeroX;
        hero.cellY = HeroY;
        hero.scale = 0.9f;
        hero.head = ClassPartModel(BodyPart::Helm, DarkKnight);
        for (std::size_t part = 0; part < hero.body.size(); ++part)
            hero.body[part].model = ClassPartModel(static_cast<BodyPart>(part), DarkKnight);
        world.Runtime().hero = 0;
        for (auto& item : world.OwnCharacter().equipment)
            item.id = NoItemModel;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = drawable ? static_cast<const Client::Scenes::IModelLibrary&>(drawn)
                                   : static_cast<const Client::Scenes::IModelLibrary&>(models),
                .items = &items,
                .itemSetTables = &sets,
                .log = log};
    }
    ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    // Con `drawable`, los modelos de lo que dibuja el retrato en vez de los de arriba.
    bool drawable = false;
    PortraitModels drawn;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    LetterPortrait portrait;
};

class LetterPortraitTest : public Client::Tests::GoldenDataTest
{
protected:
    std::unique_ptr<Game> Load(std::uint8_t map)
    {
        auto game = std::make_unique<Game>();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (loaded.HasValue())
            game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        return game;
    }

    // 0x4961d0 y, como 0x4977d0 desde la carta, la clase del héroe.
    std::unique_ptr<Game> Built(std::uint8_t map)
    {
        auto game = Load(map);
        const auto resources = game->Resources();
        const auto built = Client::Scenes::BuildThePortrait(game->portrait, resources);
        EXPECT_TRUE(built.HasValue()) << built.Error();
        return game;
    }

    // El retrato armado con los modelos que dibuja (PortraitModels) y el caballo del héroe (0x1c16): 0x496530 le arma
    // el caballo oscuro vivo (0x497810).
    std::unique_ptr<Game> Riding(std::uint8_t map)
    {
        auto game = Load(map);
        game->drawable = true;
        const auto resources = game->Resources();
        const auto built = Client::Scenes::BuildThePortrait(game->portrait, resources);
        EXPECT_TRUE(built.HasValue()) << built.Error();
        game->Hero().pet.model = 0x1c16;
        std::int32_t action = 0;
        const auto copied = Client::Scenes::CopyTheHeroToThePortrait(game->portrait, action, resources);
        EXPECT_TRUE(copied.HasValue()) << copied.Error();
        EXPECT_TRUE(game->portrait.helper.alive);
        return game;
    }
};

// 0x4961d0: borra el ayudante y el personaje, arma un 0x20a en la celda del héroe con 0x511290 y lo esconde en
// (−300, −300, −300). Desde cero, 44 rand().
TEST_F(LetterPortraitTest, BuildingDrawsFortyFourAndHidesTheCharacter)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Load(map);
        game->portrait.helper.alive = true;
        game->portrait.actor.characterClass = 3;
        const auto resources = game->Resources();
        const auto built = Client::Scenes::BuildThePortrait(game->portrait, resources);
        ASSERT_TRUE(built.HasValue()) << built.Error();
        const ActorState& actor = game->portrait.actor;
        EXPECT_FALSE(game->portrait.helper.alive);
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(actor.type, PlayerSkeletonModel);
        EXPECT_EQ(actor.characterClass, 0);
        EXPECT_EQ(actor.cellX, HeroX);
        EXPECT_EQ(actor.cellY, HeroY);
        EXPECT_EQ(actor.position.x, -300.0f);
        EXPECT_EQ(actor.position.y, -300.0f);
        EXPECT_EQ(actor.position.z, -300.0f);
        EXPECT_EQ(actor.cellXAccesses, 0);
        EXPECT_EQ(actor.cellYAccesses, 0);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, BuildDraws).State());
    }
}

// 0x513100: la parte 0 mira la suya, pero las partes 1 a 4 miran el word de la siguiente (Q-C1024). Con la armadura
// base en los pantalones, la armadura pasa a la de la clase entera aunque ella no sea la base.
TEST_F(LetterPortraitTest, TheClassPartsReadTheWordOfTheNextPart)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        ActorState& actor = game->portrait.actor;
        // La acción 0x30 está entre 0x22 y 0x97: sin 0x4f2250.
        actor.animation.action = 0x30;
        constexpr std::uint8_t Base = DarkKnight & 7;
        actor.body[0].model = ClassPartModel(BodyPart::Helm, Base);
        actor.body[0].level = 7;
        actor.body[1].model = 0x0300;
        actor.body[1].level = 5;
        actor.body[1].flags1b = 1;
        actor.body[2].model = ClassPartModel(BodyPart::Armor, Base);
        actor.body[3].model = 0x0400;
        const CrtRandom before = game->cloth.random;
        const auto resources = game->Resources();
        const auto set = Client::Scenes::SetThePortraitClass(game->portrait, DarkKnight, resources);
        ASSERT_TRUE(set.HasValue()) << set.Error();
        EXPECT_EQ(actor.characterClass, DarkKnight);
        EXPECT_EQ(actor.body[0].model, ClassPartModel(BodyPart::Helm, DarkKnight));
        EXPECT_EQ(actor.body[0].level, 0);
        EXPECT_EQ(actor.body[1].model, ClassPartModel(BodyPart::Armor, DarkKnight));
        EXPECT_EQ(actor.body[1].level, 0);
        EXPECT_EQ(actor.body[1].flags1b, 0);
        // Los pantalones con la armadura base quedan: su comparación leyó los guantes.
        EXPECT_EQ(actor.body[2].model, ClassPartModel(BodyPart::Armor, Base));
        EXPECT_EQ(game->cloth.random.State(), before.State());
    }
}

// 0x497810 con 0x5139f0(0, equipo, personaje, ayudante): el Unilia arma el ayudante propio y el caballo oscuro, que
// va después, ya lo encuentra vivo y no lo toca (Q-C1026); la mascota queda la del caballo. +0x688 es 0,9 · 0,7 entre
// la escala del héroe.
TEST_F(LetterPortraitTest, DressingKeepsTheFirstHelperOnly)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        Equipment equipment = NothingWorn();
        // b[4] & 3 en 2 (Unilia, alas en 3 con b[8] & 7 en 0) y b[11] & 1 (el caballo oscuro).
        equipment[4] = std::byte{0xfe};
        equipment[11] = std::byte{0xf1};
        const auto resources = game->Resources();
        const auto dressed = Client::Scenes::DressThePortrait(game->portrait, equipment, 0x7a, resources);
        ASSERT_TRUE(dressed.HasValue()) << dressed.Error();
        const LetterPortrait& portrait = game->portrait;
        EXPECT_TRUE(portrait.helper.alive);
        EXPECT_EQ(portrait.helper.type, 0xc6);
        EXPECT_EQ(portrait.helper.rider, Client::Scenes::ActorPoolSize);
        EXPECT_EQ(portrait.actor.pet.model, 0x1c16);
        EXPECT_EQ(portrait.helperScale,
                  static_cast<float>(static_cast<double>(portrait.helper.scale) * static_cast<double>(0.7f) / 0.9));
        EXPECT_EQ(portrait.actor.safeZone, 0);
        for (const auto& helper : game->world.Helpers())
            EXPECT_FALSE(helper.alive);
    }
}

// 0x49784a..0x497869: con alas y la acción pasada de 0x82, la zona segura.
TEST_F(LetterPortraitTest, DressingWithWingsAndAFlyingActionMarksTheSafeZone)
{
    auto game = Built(0);
    Equipment equipment = NothingWorn();
    // Las alas 0x1a12 (b[4] con los bits 2 y 3 en 0) y sin mascota (b[4] & 3 en 3).
    equipment[4] = std::byte{0xf3};
    const auto resources = game->Resources();
    ASSERT_TRUE(Client::Scenes::DressThePortrait(game->portrait, equipment, 0x83, resources).HasValue());
    EXPECT_EQ(game->portrait.actor.wings.model, 0x1a12);
    EXPECT_EQ(game->portrait.actor.safeZone, 1);
    ASSERT_TRUE(Client::Scenes::DressThePortrait(game->portrait, equipment, 0x82, resources).HasValue());
    EXPECT_EQ(game->portrait.actor.safeZone, 0);
}

// 0x496530 con +0x3e3 del héroe en 0: la clase (0x513100), la cabeza y el cuerpo del héroe tal cual, y 0x495a20 con la
// última acción (la 0 no está en la lista: la 0x7a). Sin nada distinto, ningún rand().
TEST_F(LetterPortraitTest, CopyingTheHeroTakesTheBodyAsItIs)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        auto twin = Built(map);
        ActorState& hero = game->Hero();
        hero.body[1].model = 0x0313;
        hero.body[1].level = 9;
        hero.body[1].flags1b = 1;
        twin->Hero() = hero;
        std::int32_t action = 0x83;
        const auto resources = game->Resources();
        const auto copied = Client::Scenes::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(copied.HasValue()) << copied.Error();
        EXPECT_TRUE(copied.Value());
        const ActorState& actor = game->portrait.actor;
        EXPECT_EQ(actor.characterClass, DarkKnight);
        EXPECT_EQ(actor.head, hero.head);
        EXPECT_EQ(actor.body[1].model, 0x0313);
        EXPECT_EQ(actor.body[1].level, 9);
        EXPECT_EQ(game->portrait.shownAction, 0x7a);
        EXPECT_EQ(action, 0x83);
        // El gemelo hace lo mismo a mano: la clase con 0x4977d0, la copia y 0x495a20(0x7a, 0).
        const auto twinResources = twin->Resources();
        ASSERT_TRUE(Client::Scenes::SetThePortraitClass(twin->portrait, DarkKnight, twinResources).HasValue());
        twin->portrait.actor.head = hero.head;
        twin->portrait.actor.body = hero.body;
        ASSERT_TRUE(Client::Scenes::ShowThePortraitAction(twin->portrait, 0, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());

        const CrtRandom before = game->cloth.random;
        const auto again = Client::Scenes::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(again.HasValue()) << again.Error();
        EXPECT_FALSE(again.Value());
        EXPECT_EQ(game->cloth.random.State(), before.State());
    }
}

// 0x496530 con +0x3e3 del héroe distinto de 0: compara con el registro propio (un acceso) y copia las alas de él
// (otro), con los 3.848 rand() de cada cierre antes de 0x495a20.
TEST_F(LetterPortraitTest, CopyingATransformedHeroReadsTheOwnRecord)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        auto twin = Built(map);
        for (Game* world : {game.get(), twin.get()})
        {
            world->Hero().byte3e3 = 1;
            // El cuerpo como el de la clase: la cabeza y las cinco partes ya coinciden.
            ASSERT_TRUE(
                Client::Scenes::SetThePortraitClass(world->portrait, DarkKnight, world->Resources()).HasValue());
            for (std::size_t part = 0; part < world->portrait.actor.body.size(); ++part)
                world->portrait.actor.body[part] = {.model = ClassPartModel(static_cast<BodyPart>(part), DarkKnight)};
            auto& wings = world->world.OwnCharacter().equipment[7];
            wings.id = 0x1802;
            wings.scalar1Low = 3 << 3;
        }
        const CrtRandom start = game->cloth.random;
        std::int32_t action = 0x7a;
        const auto resources = game->Resources();
        const auto copied = Client::Scenes::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(copied.HasValue()) << copied.Error();
        EXPECT_TRUE(copied.Value());
        EXPECT_EQ(game->portrait.actor.wings.model, 0x1802 + 0x212);
        EXPECT_EQ(game->portrait.actor.wings.level, 3);

        twin->cloth.random.Discard(2 * OwnRecordDraws);
        twin->portrait.actor.wings = game->portrait.actor.wings;
        const auto twinResources = twin->Resources();
        ASSERT_TRUE(Client::Scenes::ShowThePortraitAction(twin->portrait, 0, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_GE(DrawsBetween(start, game->cloth.random), 2 * OwnRecordDraws);
    }
}

// La cola de 0x496530 con la mascota distinta: 0x495a20(0x7a, 0), +0x6a4 en 0x7a y el ayudante de nuevo por la
// mascota (0x1c16: el 0xbf), con su alpha en 0 y +0x688 con la escala del propio personaje.
TEST_F(LetterPortraitTest, ANewPetRebuildsTheHelper)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        game->Hero().pet.model = 0x1c16;
        std::int32_t action = 0x83;
        const auto resources = game->Resources();
        const auto copied = Client::Scenes::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(copied.HasValue()) << copied.Error();
        const LetterPortrait& portrait = game->portrait;
        EXPECT_EQ(action, 0x7a);
        EXPECT_EQ(portrait.shownAction, 0x7a);
        EXPECT_TRUE(portrait.helper.alive);
        EXPECT_EQ(portrait.helper.type, 0xbf);
        EXPECT_EQ(portrait.helper.horse.alpha, 0.0f);
        EXPECT_EQ(portrait.helperScale,
                  static_cast<float>(static_cast<double>(portrait.helper.scale) * static_cast<double>(0.7f) /
                                     static_cast<double>(portrait.actor.scale)));
    }
}

// 0x495a20: los gestos (0x6c7b14) a pie, adelante desde 0x7a la 0x83 con la animación 0x99; atrás, la 0x78, que va a
// 0x4f4a10 (hito 3) y corta.
TEST_F(LetterPortraitTest, TurningWalksTheGestureList)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        const auto resources = game->Resources();
        const auto next = Client::Scenes::ShowThePortraitAction(game->portrait, 0x7a, 1, resources);
        ASSERT_TRUE(next.HasValue()) << next.Error();
        EXPECT_EQ(next.Value(), 0x83);
        EXPECT_EQ(game->portrait.shownAction, 0x83);
        EXPECT_EQ(game->portrait.actor.animation.action, 0x99);
        EXPECT_EQ(game->world.Runtime().map, map);
        const auto previous = Client::Scenes::ShowThePortraitAction(game->portrait, 0x7a, -1, resources);
        ASSERT_FALSE(previous.HasValue());
        EXPECT_NE(previous.Error().find("0x4f4a10"), std::string::npos);
        EXPECT_EQ(game->world.Runtime().map, map);
    }
}

// 0x495a20 con un ayudante montado vivo: la lista corta (0x6c7b08), de 0x7a a 0x7c (0x4f37c0).
TEST_F(LetterPortraitTest, ARiddenHelperUsesTheShortList)
{
    auto game = Built(2);
    game->portrait.helper.alive = true;
    game->portrait.helper.type = 0xc6;
    const auto resources = game->Resources();
    const auto next = Client::Scenes::ShowThePortraitAction(game->portrait, 0x7a, 1, resources);
    ASSERT_TRUE(next.HasValue()) << next.Error();
    EXPECT_EQ(next.Value(), 0x7c);
    game->portrait.helper.alive = false;
    const auto walking = Client::Scenes::ShowThePortraitAction(game->portrait, 0x7c, 1, resources);
    ASSERT_TRUE(walking.HasValue()) << walking.Error();
    EXPECT_EQ(walking.Value(), 0x94);
}

// 0x497890: los ángulos del personaje en (−20, 5, 60).
TEST_F(LetterPortraitTest, TurningThePortraitSetsItsAngles)
{
    auto game = Built(0);
    Client::Scenes::TurnThePortrait(game->portrait);
    EXPECT_EQ(game->portrait.actor.angles.x, -20.0f);
    EXPECT_EQ(game->portrait.actor.angles.y, 5.0f);
    EXPECT_EQ(game->portrait.actor.angles.z, 60.0f);
}

// 0x495760: el viewport de 0x620d10 (la y de GL desde abajo, 141 de alto) escalado y truncado, y la vista que lleva el
// personaje a 10.000 adelante de la cámara: el centro del retrato queda en el medio del viewport.
TEST_F(LetterPortraitTest, ThePortraitPassLooksAtTheCharacterFromAfar)
{
    auto game = Built(0);
    Client::UI::OldWindowBase base;
    base.x = 100;
    base.y = 50;
    base.width = 0x77;
    ActorState& actor = game->portrait.actor;
    actor.position = {1234.0f, 5678.0f, 90.0f};
    const auto small = Client::Scenes::PortraitPass({640, 480}, base, actor);
    EXPECT_EQ(small.viewport, glm::ivec4(100, 480 - 50 - 141, 0x77, 141));
    const auto large = Client::Scenes::PortraitPass({1280, 960}, base, actor);
    EXPECT_EQ(large.viewport, glm::ivec4(200, 960 - 100 - 282, 0x77 * 2, 282));
    // El personaje, 75 por encima de sus pies, cae en el centro de la imagen.
    const glm::vec4 eye = small.view * glm::vec4(actor.position.x, actor.position.y, actor.position.z + 75.0f, 1.0f);
    const glm::vec4 clip = small.projection * eye;
    EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-4f);
    EXPECT_NEAR(clip.y / clip.w, 0.0f, 1e-4f);
    EXPECT_LT(eye.z, -9999.0f);
    // Con el caballo oscuro, 50 más abajo.
    actor.pet.model = 0x1c16;
    const auto riding = Client::Scenes::PortraitPass({640, 480}, base, actor);
    const glm::vec4 lifted =
        riding.view * glm::vec4(actor.position.x, actor.position.y, actor.position.z + 125.0f, 1.0f);
    EXPECT_NEAR(lifted.y, eye.y, 1e-3f);
}

// 0x497c10 y 0x495760 antes del dibujo: con el frame por debajo de +0x6a8 y quieto, una acción fuera de 0x7a..0x82
// vuelve a la 0x7a sin sacar el rand() % 4; el pase pone los ángulos, la escala por el zoom, las luces y +0x3e4.
TEST_F(LetterPortraitTest, PreparingTheDrawStepsTheCharacter)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        LetterPortrait& portrait = game->portrait;
        Client::UI::OldPortraitPart part;
        part.action = 0x83;
        part.angle = 45.0f;
        part.zoom = 0.9f;
        portrait.idle = 1;
        portrait.frameMark = 5;
        portrait.actor.animation.frame = 2.0f;
        const auto resources = game->Resources();
        const auto pass = Client::Scenes::PrepareThePortraitDraw(portrait, part, {640, 480}, resources);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        EXPECT_EQ(portrait.idle, 0);
        EXPECT_EQ(portrait.shownAction, 0x7a);
        EXPECT_EQ(part.action, 0x83);
        EXPECT_EQ(portrait.frameMark, 0);
        EXPECT_EQ(game->world.Runtime().map, map);
        EXPECT_EQ(portrait.actor.angles.z, 45.0f);
        EXPECT_EQ(portrait.actor.scale, static_cast<float>(0.9 * static_cast<double>(0.7f)));
        EXPECT_EQ(portrait.actor.light, glm::vec3(1.0f));
        EXPECT_EQ(portrait.actor.safeZone, 0);
        EXPECT_EQ(portrait.actor.byte3e4, 1);

        // Sin pasar la marca, +0x6a8 sigue al frame.
        portrait.actor.animation.frame = 7.5f;
        ASSERT_TRUE(Client::Scenes::PrepareThePortraitDraw(portrait, part, {640, 480}, resources).HasValue());
        EXPECT_EQ(portrait.frameMark, 7);
    }
}

// 0x495796: 0x444010(ayudante, 1) después de 0x4fd5f0 y 0x505de0, con el mundo en 0: el caballo sigue al personaje
// aunque su celda (la de (−300, −300) por 0,01, con el byte bajo) sea segura, con sus rand() después de los del
// personaje. Es lo mismo que el paso sin el caballo y 0x444010(ayudante, 1) a mano después.
TEST_F(LetterPortraitTest, PreparingTheDrawStepsTheLiveHelperAfterTheCharacter)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Riding(map);
        auto twin = Riding(map);
        ASSERT_FALSE(HasFatalFailure());
        for (Game* each : {game.get(), twin.get()})
        {
            const auto& position = each->portrait.actor.position;
            const auto cell = Client::Data::Terrain::CellIndex(
                static_cast<std::uint8_t>(static_cast<std::int32_t>(position.x * 0.01f)),
                static_cast<std::uint8_t>(static_cast<std::int32_t>(position.y * 0.01f)));
            each->world.Map().terrain.attributes->cells[cell] |= 1;
        }
        Client::UI::OldPortraitPart part;
        part.action = 0x7a;
        part.zoom = 1.0f;
        game->portrait.actor.animation.frame = 1.0f;
        twin->portrait.actor.animation.frame = 1.0f;

        const auto resources = game->Resources();
        ASSERT_TRUE(Client::Scenes::PrepareThePortraitDraw(game->portrait, part, {640, 480}, resources).HasValue());

        const auto twinResources = twin->Resources();
        twin->portrait.helper.alive = false;
        ASSERT_TRUE(Client::Scenes::PrepareThePortraitDraw(twin->portrait, part, {640, 480}, twinResources).HasValue());
        twin->portrait.helper.alive = true;
        twin->world.Runtime().map = 0;
        const auto stepped = Client::Scenes::StepTheHelperOutsideTheTable(
            twin->portrait.helper, twin->portrait.actor,
            Client::Scenes::HelperStepContext{.world = twin->world,
                                              .models = &twin->drawn,
                                              .random = twin->cloth.random,
                                              .worldClock = 0.0f,
                                              .scene = Client::Scenes::SceneId::Game,
                                              .terrain = nullptr,
                                              .logicSteps = twin->cloth.logicSteps,
                                              .cameraShake = &twin->input.shake});
        twin->world.Runtime().map = map;
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        const auto& helper = game->portrait.helper;
        EXPECT_TRUE(helper.alive);
        EXPECT_EQ(helper.position, game->portrait.actor.position);
        EXPECT_GT(helper.horse.alpha, 0.0f);
        EXPECT_EQ(helper.horse.alpha, twin->portrait.helper.horse.alpha);
        EXPECT_EQ(helper.horse.animation.action, twin->portrait.helper.horse.animation.action);
        EXPECT_EQ(helper.horse.animation.frame, twin->portrait.helper.horse.animation.frame);
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(game->world.Runtime().map, map);
    }
}

// Lo que el dibujo del juego le presta al retrato (LetterPortraitDraw): el lote 2D en 640 × 480 con el texto falso (6
// columnas por letra, 13 de alto) y los textos 997 a 999 inventados, el lote de los modelos, la luz del terreno en
// (0,25; 0,5; 0,75) y los sprites del frame. Anota los cierres de los dos lotes, con los vértices del de los modelos.
class PortraitDrawing
{
public:
    explicit PortraitDrawing(Game& game) : m_game(game)
    {
        sprites.Begin(Client::App::PixelSize{640, 480});
        lighting->cells.fill({0.25f, 0.5f, 0.75f});
    }

    PortraitDrawing(const PortraitDrawing&) = delete;
    PortraitDrawing& operator=(const PortraitDrawing&) = delete;
    PortraitDrawing(PortraitDrawing&&) = delete;
    PortraitDrawing& operator=(PortraitDrawing&&) = delete;
    ~PortraitDrawing() = default;

    // Con `cloth` de rand() del frame: el del juego o, entre frames, su copia.
    Client::Scenes::LetterPortraitDraw Draw(Client::Render::Models::ClothWorld& cloth, bool originalFrame)
    {
        Client::Scenes::WorldRuntime& runtime = m_game.world.Runtime();
        ActorState& hero = m_game.Hero();
        m_hud.emplace(Client::Scenes::GameHudContext{.world = m_game.world,
                                                     .random = cloth.random,
                                                     .ownRecordDepth = runtime.ownRecordDepth,
                                                     .cellXDepth = hero.cellXAccesses,
                                                     .cellYDepth = hero.cellYAccesses,
                                                     .hud = runtime.hud,
                                                     .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                                     .sound = sound,
                                                     .sprites = sprites,
                                                     .text = text,
                                                     .texts = &texts});
        Client::Scenes::LetterPortraitDraw draw{.hud = *m_hud, .batch = models};
        draw.flushSprites = [this]() { flushes.emplace_back("2d", models.Vertices().size()); };
        draw.flushModels = [this]() { flushes.emplace_back("3d", models.Vertices().size()); };
        draw.characters.dynamicLighting = lighting.get();
        draw.characters.clothWorld = &cloth;
        draw.characters.sprites = &frameSprites;
        draw.characters.particles = &m_game.world.Particles();
        draw.characters.effects = &m_game.world.Map().effects;
        draw.helperModels = &m_game.drawn;
        draw.originalFrame = originalFrame;
        return draw;
    }

    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Data::Text::TextTable texts =
        Client::Tests::MakeTextTable({{997, "Arrastrar gira"}, {998, "Rueda acerca"}, {999, "Clic vuelve"}});
    std::optional<std::int32_t> sound;
    Client::Render::ModelBatch models;
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    Client::World::FrameSprites frameSprites;
    std::vector<std::pair<std::string, std::size_t>> flushes;

private:
    Game& m_game;
    std::optional<Client::Scenes::GameHudContext> m_hud;
};

struct SpriteQuad
{
    Client::Render::Color color{};
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Los quads del lote 2D con `slot` (o sin textura), en 640 × 480 con la y hacia abajo y con la u y la v de su esquina
// de abajo a la derecha.
std::vector<SpriteQuad> QuadsOf(const Client::Render::SpriteBatch& sprites,
                                std::optional<Client::Render::TextureSlot> slot)
{
    std::vector<SpriteQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(SpriteQuad{.color = vertices[first].color,
                                       .left = vertices[first].x,
                                       .top = 480.0f - vertices[first].y,
                                       .right = vertices[first + 2].x,
                                       .bottom = 480.0f - vertices[first + 2].y,
                                       .u = vertices[first + 2].u,
                                       .v = vertices[first + 2].v});
        }
    }
    return quads;
}

// 0x4958e2..0x495a0c: cierra el 2D, empieza el pase del retrato con la profundidad borrada, dibuja el caballo
// (0x445260(ayudante, 1): visto, con la escala 0x3f3ae148 y su rand() % 30 + 70) y después el personaje (0x50ba30, sin
// la sombra), y cierra el pase. Los rand(), los de 0x445260 y después los de 0x50ba30.
TEST_F(LetterPortraitTest, ThePortraitModelDrawsTheHelperAndThenTheCharacterOverACleanDepth)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Riding(map);
        auto twin = Riding(map);
        ASSERT_FALSE(HasFatalFailure());
        Client::UI::OldPortraitPart part;
        part.base.x = 100;
        part.base.y = 50;
        part.base.width = 0x77;
        part.action = 0x7a;
        part.angle = 30.0f;
        part.zoom = 1.0f;
        const auto resources = game->Resources();
        const auto pass = Client::Scenes::PrepareThePortraitDraw(game->portrait, part, {640, 480}, resources);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        const auto twinResources = twin->Resources();
        const auto twinPass = Client::Scenes::PrepareThePortraitDraw(twin->portrait, part, {640, 480}, twinResources);
        ASSERT_TRUE(twinPass.HasValue()) << twinPass.Error();
        ASSERT_EQ(game->cloth.random.State(), twin->cloth.random.State());

        PortraitDrawing drawing(*game);
        const auto drawn = Client::Scenes::DrawThePortraitModel(game->portrait, pass.Value(),
                                                                drawing.Draw(game->cloth, true), resources);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(drawing.flushes.size(), 2u);
        EXPECT_EQ(drawing.flushes[0], std::make_pair(std::string("2d"), std::size_t{0}));
        EXPECT_EQ(drawing.flushes[1].first, "3d");
        EXPECT_GT(drawing.flushes[1].second, 0u);
        EXPECT_TRUE(drawing.models.Pass().clearDepth);
        EXPECT_EQ(drawing.models.Pass().viewport, pass.Value().viewport);
        EXPECT_TRUE(game->portrait.helper.seen);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(game->portrait.helper.scale), 0x3f3ae148u);

        // A mano: 0x445260(ayudante, 1) saca uno; 0x50ba30, los suyos.
        PortraitDrawing manual(*twin);
        const Client::Scenes::LetterPortraitDraw draw = manual.Draw(twin->cloth, true);
        manual.models.Begin(pass.Value());
        const CrtRandom beforeHelper = twin->cloth.random;
        ASSERT_TRUE(Client::Scenes::DrawTheHelperOutsideTheTable(
                        twin->portrait.helper, twin->portrait.actor,
                        Client::Scenes::HelperDrawContext{.batch = manual.models,
                                                          .models = twin->drawn,
                                                          .world = twin->world,
                                                          .random = twin->cloth.random,
                                                          .lighting = manual.lighting.get(),
                                                          .sprites = &manual.frameSprites})
                        .HasValue());
        EXPECT_EQ(DrawsBetween(beforeHelper, twin->cloth.random), 1);
        ASSERT_TRUE(Client::Scenes::DrawCharacterOutsideThePool(manual.models, twin->drawn, twin->world,
                                                                twin->portrait.actor, twin->portrait.cloth,
                                                                draw.characters, true)
                        .HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(drawing.models.Vertices().size(), manual.models.Vertices().size());
    }
}

// 0x497c1b..0x497ca2: el retrato en blanco (+0x6c0 en 1, la carta de "webzen"), el cuadro negro de 119 × 141 y el logo
// (0x653) de 80 × 62 en (x + 0x14; y + 0x26) con la v en 0,76171875. Sin el personaje, sin pase y sin rand().
TEST_F(LetterPortraitTest, ABlankPortraitIsABlackBoxWithTheLogo)
{
    auto game = Load(0);
    Client::UI::OldPortraitPart part;
    part.base.x = 100;
    part.base.y = 50;
    part.blank = 1;
    PortraitDrawing drawing(*game);
    const CrtRandom before = game->cloth.random;
    const auto resources = game->Resources();
    ASSERT_TRUE(Client::Scenes::DrawTheLetterPortrait(part, drawing.Draw(game->cloth, true), resources).HasValue());
    const std::vector<SpriteQuad> black = QuadsOf(drawing.sprites, std::nullopt);
    ASSERT_EQ(black.size(), 1u);
    EXPECT_EQ(black[0].color, (Client::Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f}));
    EXPECT_FLOAT_EQ(black[0].left, 100.0f);
    EXPECT_FLOAT_EQ(black[0].top, 50.0f);
    EXPECT_FLOAT_EQ(black[0].right, 219.0f);
    EXPECT_FLOAT_EQ(black[0].bottom, 191.0f);
    const std::vector<SpriteQuad> logo = QuadsOf(drawing.sprites, Client::Render::TextureSlot{0x653});
    ASSERT_EQ(logo.size(), 1u);
    EXPECT_EQ(logo[0].color, Client::Render::Color{});
    EXPECT_FLOAT_EQ(logo[0].left, 120.0f);
    EXPECT_FLOAT_EQ(logo[0].top, 88.0f);
    EXPECT_FLOAT_EQ(logo[0].right, 200.0f);
    EXPECT_FLOAT_EQ(logo[0].bottom, 150.0f);
    EXPECT_FLOAT_EQ(logo[0].u, 1.0f);
    EXPECT_FLOAT_EQ(logo[0].v, 0.76171875f);
    EXPECT_TRUE(drawing.flushes.empty());
    EXPECT_EQ(game->cloth.random.State(), before.State());
}

// 0x497c10 entera en Lorencia y Devias: el paso (PrepareThePortraitDraw), el modelo y, con +0x4c & 1, el ícono de la
// ayuda (0x651) de 16 × 16 en (x + 1; y + alto − 0x11); con la ayuda a la vista, gris (0,6) de 15 × 15 en
// (x + 2; y + alto − 0x10) con la u y la v en 0,9375 y los textos 997 a 999 en la caja de 0x5af090 con el modo 1:
// centrada en x + ancho / 2, desde y + alto − (13 + 2) · 3 y cada uno contra el borde izquierdo.
TEST_F(LetterPortraitTest, TheWholePortraitDrawsTheModelAndThenItsHelp)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        for (const std::int32_t help : {0, 1})
        {
            SCOPED_TRACE(testing::Message() << static_cast<int>(map) << " " << help);
            auto game = Riding(map);
            ASSERT_FALSE(HasFatalFailure());
            auto part = std::make_unique<Client::UI::OldPortraitPart>();
            part->base.x = 100;
            part->base.y = 50;
            part->base.width = 0x77;
            part->base.height = 0x8d;
            part->base.flags = 1;
            part->help = help;
            part->action = 0x7a;
            part->zoom = 1.0f;
            part->actor = std::shared_ptr<void>(&game->portrait, [](void*) {});
            PortraitDrawing drawing(*game);
            const auto resources = game->Resources();
            const auto drawn = Client::Scenes::DrawTheLetterPortrait(*part, drawing.Draw(game->cloth, true), resources);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            ASSERT_EQ(drawing.flushes.size(), 2u);
            EXPECT_GT(drawing.models.Vertices().size(), 0u);
            EXPECT_TRUE(game->portrait.helper.seen);
            const std::vector<SpriteQuad> icon = QuadsOf(drawing.sprites, Client::Render::TextureSlot{0x651});
            ASSERT_EQ(icon.size(), 1u);
            if (help == 0)
            {
                EXPECT_EQ(icon[0].color, Client::Render::Color{});
                EXPECT_FLOAT_EQ(icon[0].left, 101.0f);
                EXPECT_FLOAT_EQ(icon[0].top, 50.0f + 0x8d - 0x11);
                EXPECT_FLOAT_EQ(icon[0].right, 117.0f);
                EXPECT_FLOAT_EQ(icon[0].u, 1.0f);
                EXPECT_TRUE(drawing.text.texts.empty());
                continue;
            }
            EXPECT_FLOAT_EQ(icon[0].color.red, 0.6f);
            EXPECT_FLOAT_EQ(icon[0].left, 102.0f);
            EXPECT_FLOAT_EQ(icon[0].top, 50.0f + 0x8d - 0x10);
            EXPECT_FLOAT_EQ(icon[0].right, 117.0f);
            EXPECT_FLOAT_EQ(icon[0].u, 0.9375f);
            EXPECT_FLOAT_EQ(icon[0].v, 0.9375f);
            const auto& texts = drawing.text.texts;
            ASSERT_EQ(texts.size(), 3u);
            EXPECT_EQ(texts[0].text, "Arrastrar gira");
            EXPECT_EQ(texts[1].text, "Rueda acerca");
            EXPECT_EQ(texts[2].text, "Clic vuelve");
            // La caja: 14 letras · 6 + 4 = 88 de ancho, centrada en 100 + 59: desde 115, y el texto desde 116.
            EXPECT_EQ(texts[0].x, 116);
            EXPECT_EQ(texts[0].y, 50 + 0x8d - (13 + 2) * 3);
            for (const auto& text : texts)
            {
                EXPECT_EQ(text.firstColumn, 0);
                EXPECT_EQ(text.columns, 86);
            }
            EXPECT_EQ(game->world.Runtime().hud.tooltip.next, 3);
        }
    }
}

// Un dibujo intermedio de MuRet dibuja una copia del retrato con la copia del rand() del frame: el personaje, el
// caballo y su marca quedan como los dejó el último frame del original, y el rand() del juego no se mueve.
TEST_F(LetterPortraitTest, AnIntermediateDrawLeavesThePortraitAsTheLastFrameLeftIt)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Riding(map);
        ASSERT_FALSE(HasFatalFailure());
        Client::UI::OldPortraitPart part;
        part.action = 0x7a;
        part.zoom = 1.0f;
        part.actor = std::shared_ptr<void>(&game->portrait, [](void*) {});
        game->portrait.actor.animation.frame = 3.0f;
        const LetterPortrait before = game->portrait;
        const CrtRandom random = game->cloth.random;
        Client::Render::Models::ClothWorld projected = game->cloth;
        PortraitDrawing drawing(*game);
        const auto resources = game->Resources();
        ASSERT_TRUE(Client::Scenes::DrawTheLetterPortrait(part, drawing.Draw(projected, false), resources).HasValue());
        EXPECT_GT(drawing.models.Vertices().size(), 0u);
        EXPECT_EQ(game->cloth.random.State(), random.State());
        EXPECT_EQ(game->portrait.frameMark, before.frameMark);
        EXPECT_EQ(game->portrait.actor.animation.frame, before.actor.animation.frame);
        EXPECT_EQ(game->portrait.actor.byte3e4, before.actor.byte3e4);
        EXPECT_EQ(game->portrait.helper.seen, before.helper.seen);
        EXPECT_EQ(game->portrait.helper.scale, before.helper.scale);
        EXPECT_EQ(game->portrait.helper.position, before.helper.position);
    }
}

// Las ventanas de las cartas con el gancho del juego (LetterPortraitHook): los textos, la medida de 2 por carácter y el
// paso de las ventanas viejas.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 2, 13};
    }
};

std::map<std::size_t, std::string> LetterTexts()
{
    return {{1000, "Para"},     {1001, "Mandar"},    {1002, "Cerrar"},          {1003, "Atras"},
            {1004, "Adelante"}, {1010, "Responder"}, {1011, "Borrar"},          {1012, "Antes"},
            {1013, "Despues"},  {1054, "Carta: %s"}, {1071, "Carta nueva (%d)"}};
}

class LetterPortraitWindowsTest : public LetterPortraitTest
{
protected:
    [[nodiscard]] Client::UI::OldWindowStepView View(Game& game, const Client::Scenes::WorldEntryResources& resources)
    {
        Client::UI::OldWindowStepView view{.texts = &m_oldTexts, .measure = &m_oldMeasure};
        view.portrait = Client::Scenes::LetterPortraitHook(resources);
        view.field78abaac = game.world.Runtime().ui.field78abaac;
        view.heroName = "Benteveo";
        return view;
    }

    // 0x48ee60(7) en (100, 100): la de escribir con su retrato (0x497f50).
    Client::UI::OldWindow* OpenTheWriteWindow(Game& game, const Client::Scenes::WorldEntryResources& resources)
    {
        auto view = View(game, resources);
        auto globals = Client::Scenes::OldWindowGlobalsOf(game.world.Runtime());
        auto created = Client::UI::CreateTheOldWindow(
            game.world.Runtime().oldWindows,
            Client::UI::OldWindowRequest{
                .kind = Client::UI::LetterWriteCase, .x = 100, .y = 100, .data = "Carta", .aux = 0, .flags = 0},
            m_oldTexts, m_oldMeasure, &globals, &view);
        EXPECT_TRUE(created.HasValue()) << created.Error();
        if (!created.HasValue())
            return nullptr;
        return &game.world.Runtime().oldWindows.windows.at(created.Value());
    }

    static LetterPortrait& PortraitOf(Client::UI::OldWindow& window)
    {
        return *std::static_pointer_cast<LetterPortrait>(window.letter.portrait.actor);
    }

    LetterMeasure m_letters;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(LetterTexts());
    Client::UI::OldWindowTexts m_oldTexts = Client::Scenes::OldWindowTextsOf(&m_texts);
    Client::UI::OldWindowMeasure m_oldMeasure{.measure = m_letters,
                                              .dcFont = Client::Render::Text::FontKind::Normal,
                                              .screen = Client::App::PixelSize{640, 480}};
};

// 0x497f50 de punta a punta: el retrato armado (0x4961d0), el héroe copiado (0x496530, con la clase por 0x513100),
// la acción 0x7a (0x4978f0) y el ángulo 90 (0x497890), con los rand() de cada uno en ese orden. Abrir la ventana no
// saca otros.
TEST_F(LetterPortraitWindowsTest, OpeningTheWriteWindowCopiesTheHero)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Load(map);
        auto twin = Load(map);
        const auto resources = game->Resources();
        Client::UI::OldWindow* write = OpenTheWriteWindow(*game, resources);
        ASSERT_NE(write, nullptr);
        const Client::UI::OldPortraitPart& part = write->letter.portrait;
        EXPECT_EQ(part.built, 1);
        EXPECT_EQ(part.follow, 1);
        EXPECT_EQ(part.action, 0x7a);
        EXPECT_EQ(part.baseAngle, 90.0f);
        EXPECT_EQ(part.angle, 90.0f);
        const LetterPortrait& portrait = PortraitOf(*write);
        EXPECT_EQ(portrait.actor.characterClass, DarkKnight);
        EXPECT_EQ(portrait.actor.body[1].model, ClassPartModel(BodyPart::Armor, DarkKnight));
        EXPECT_EQ(portrait.shownAction, 0x7a);
        EXPECT_EQ(portrait.actor.angles.z, 60.0f);

        const auto twinResources = twin->Resources();
        LetterPortrait& expected = twin->portrait;
        std::int32_t action = 0;
        ASSERT_TRUE(Client::Scenes::BuildThePortrait(expected, twinResources).HasValue());
        ASSERT_TRUE(Client::Scenes::CopyTheHeroToThePortrait(expected, action, twinResources).HasValue());
        ASSERT_TRUE(Client::Scenes::ShowThePortraitAction(expected, 0x7a, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_GT(DrawsBetween(CrtRandom(Seed), game->cloth.random), BuildDraws);
    }
}

// El C7 de punta a punta (0x4dcb70): la de leer arma su retrato (0x4999d0) y lo viste con la clase de pkt[8] (0x20: la
// 1), el equipo (el Unilia, 0xc6) y la acción 2 + 0x78; la dirección 0x4f da el ángulo 15 · 6 y el zoom
// ((1 · 5 + 0x28) · 2) · 0,01. Los rand(): 0x4961d0, 0x513100, 0x5139f0 y 0x495a20, en ese orden.
TEST_F(LetterPortraitWindowsTest, ReceivingALetterDressesThePortrait)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Load(map);
        auto twin = Load(map);
        const auto resources = game->Resources();
        Client::UI::OldWindowManager& manager = game->world.Runtime().oldWindows;
        Client::UI::AddALetter(manager.letterModel, 7, "Tordo", "Saludos", "2026/11/13", "10:20", 0);
        Equipment equipment = NothingWorn();
        equipment[4] = std::byte{0xfe};
        const Client::Protocol::Letters::LetterRead read{.index = 7,
                                                         .length = 9,
                                                         .portraitClass = 0x20,
                                                         .equipment = equipment,
                                                         .direction = 0x4f,
                                                         .action = 2,
                                                         .body = "Hola che",
                                                         .copy = {}};
        const Client::Scenes::ChatReceiveContext context{.world = game->world,
                                                         .random = game->cloth.random,
                                                         .measure = &m_letters,
                                                         .screen = Client::App::PixelSize{640, 480},
                                                         .scene = Client::Scenes::SceneId::Game,
                                                         .texts = &m_texts,
                                                         .worldEntry = &resources};
        const auto shown = Client::Scenes::ApplyLetterRead(read, context);
        ASSERT_TRUE(shown.HasValue()) << shown.Error();
        Client::UI::OldWindow* window = nullptr;
        for (auto& [id, candidate] : manager.windows)
            if (candidate.kind == Client::UI::OldWindowKind::LetterReadWindow)
                window = &candidate;
        ASSERT_NE(window, nullptr);
        const Client::UI::OldPortraitPart& part = window->letter.portrait;
        EXPECT_EQ(part.built, 1);
        EXPECT_EQ(part.portraitClass, 1);
        EXPECT_EQ(part.action, 0x7a);
        EXPECT_EQ(part.angle, 90.0f);
        EXPECT_FLOAT_EQ(part.zoom, 0.9f);
        EXPECT_EQ(part.baseZoom, part.zoom);
        const LetterPortrait& portrait = PortraitOf(*window);
        EXPECT_EQ(portrait.actor.characterClass, 1);
        EXPECT_TRUE(portrait.helper.alive);
        EXPECT_EQ(portrait.helper.type, 0xc6);
        EXPECT_EQ(portrait.actor.angles.x, -20.0f);

        const auto twinResources = twin->Resources();
        LetterPortrait& expected = twin->portrait;
        ASSERT_TRUE(Client::Scenes::BuildThePortrait(expected, twinResources).HasValue());
        ASSERT_TRUE(Client::Scenes::SetThePortraitClass(expected, 1, twinResources).HasValue());
        ASSERT_TRUE(Client::Scenes::DressThePortrait(expected, equipment, 0x7a, twinResources).HasValue());
        ASSERT_TRUE(Client::Scenes::ShowThePortraitAction(expected, 0x7a, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
    }
}

// 0x497970 en el paso de las ventanas viejas: con 0x78abaac en 1 le vuelve a copiar el héroe (sin nada distinto, sin
// rand()) y +0x360; la rueda lo acerca hasta 1,1; el clic lo agarra y el clic siguiente lo gira con lo que corrió el
// mouse; sin clic, lo suelta.
TEST_F(LetterPortraitWindowsTest, TheStepFollowsZoomsAndTurns)
{
    auto game = Load(2);
    const auto resources = game->Resources();
    Client::UI::OldWindow* write = OpenTheWriteWindow(*game, resources);
    ASSERT_NE(write, nullptr);
    Client::UI::OldPortraitPart& part = write->letter.portrait;
    Client::UI::OldWindowManager& manager = game->world.Runtime().oldWindows;
    game->Hero().byte360 = 4;
    game->world.Runtime().ui.field78abaac = 1;
    const auto globals = Client::Scenes::OldWindowGlobalsOf(game->world.Runtime());
    auto view = View(*game, resources);
    view.mouseX = part.base.x + part.base.width / 2;
    view.mouseY = part.base.y + part.base.height / 3;
    const CrtRandom before = game->cloth.random;
    globals.wheel = 20;
    ASSERT_TRUE(Client::UI::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(PortraitOf(*write).actor.byte360, 4);
    EXPECT_EQ(part.zoom, 1.1f);
    EXPECT_EQ(globals.wheel, 0);
    EXPECT_EQ(game->cloth.random.State(), before.State());

    globals.leftClick = true;
    ASSERT_TRUE(Client::UI::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.base.state, 2);
    EXPECT_EQ(globals.capture, part.base.id);
    view.mouseX += 7;
    globals.leftClick = true;
    ASSERT_TRUE(Client::UI::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.angle, 97.0f);
    globals.leftClick = false;
    ASSERT_TRUE(Client::UI::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.base.state, Client::UI::ShownWindow);
    EXPECT_EQ(globals.capture, 0);

    // El clic derecho vuelve al ángulo y al zoom de partida.
    globals.rightClick = true;
    ASSERT_TRUE(Client::UI::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.angle, 90.0f);
    EXPECT_EQ(part.zoom, 1.0f);
}
} // namespace
