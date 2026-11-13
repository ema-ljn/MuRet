#include "Scenes/LetterPortrait.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Character/Opcodes.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/HelperTable.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
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

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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
                .models = models,
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
} // namespace
