#include "Scenes/PetStats.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"

#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// El A9 de las mascotas que llega (0x4e4ca1 → 0x421db0, 0x4221d0, 0x417f30 y 0x420cd0; CL9 hito 4, 06 § El A9 de las
// mascotas), con niveles y experiencias inventados. Las cuentas de rand() salen del ASM: cada acceso al registro propio
// que lo cierra en 0 saca 3.848; lo de 0x550d10 se mide aparte sobre el mismo equipo.
namespace
{
using Client::Protocol::Pet::PetInfoReply;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::ActorState;
using Client::Scenes::DarkSpirit;
using Client::Scenes::PetStats;

constexpr std::uint32_t Seed = 0x2468ace0u;
constexpr std::size_t HeroIndex = 6;
// +0x36 + +0x20 del registro propio: el liderazgo con lo agregado.
constexpr std::uint16_t Leadership = 900;
constexpr std::uint16_t Word36 = 100;
constexpr std::uint8_t RavenDurability = 200;
constexpr std::uint16_t DarkRavenItem = 0x1a05;
constexpr std::uint16_t DarkHorseItem = 0x1a04;
constexpr std::uint8_t HorseDurability = 150;

int DrawsBetween(CrtRandom from, std::uint32_t to)
{
    for (int draws = 0; draws <= 200'000; ++draws)
    {
        if (from.State() == to)
            return draws;
        static_cast<void>(from.Next());
    }
    return -1;
}

Client::Data::Tables::ItemTable EmptyItems()
{
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(
        std::vector<std::byte>(Client::Data::Tables::ItemTableSize), Client::Data::Tables::ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

Client::Data::Tables::ItemSetTables EmptySets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// Un Dark Lord publicado con su cuervo, el equipo vacío con las durabilidades del cuervo y del caballo, nada en el
// cartel y lo que dejó el último 0x505820.
struct Pets
{
    Pets()
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.hero = HeroIndex;
        runtime.fullSetScan = Client::Scenes::Scan505820{};
        runtime.hoveredItemCopy.id = 0xffff;
        ActorState lord = Client::Tests::UnarmedPlayer(4);
        lord.alive = true;
        lord.darkSpirit.emplace();
        world.Actors()[HeroIndex] = lord;
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        Client::World::ItemState empty;
        empty.id = 0xffff;
        own.equipment.fill(empty);
        own.equipment[1].id = DarkRavenItem;
        own.equipment[1].durability = RavenDurability;
        own.equipment[8].id = DarkHorseItem;
        own.equipment[8].durability = HorseDurability;
        own.leadership = Leadership;
        own.word36 = Word36;
        own.strength = 300;
        own.word2a = 20;
        own.agility = 60;
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
                .itemSetTables = sets,
                .log = log};
    }
    Client::Core::Expected<void, std::string> Reply(const PetInfoReply& reply)
    {
        return Client::Scenes::ApplyPetInfo(reply, Resources());
    }
    DarkSpirit& Raven()
    {
        return *world.Actors()[HeroIndex].darkSpirit;
    }
    PetStats& Pet(std::size_t index)
    {
        return world.Runtime().pets[index];
    }
    int Draws() const
    {
        return DrawsBetween(CrtRandom(Seed), cloth.random.State());
    }
    // 0x4e4cbd..0x4e4ce7, lo que no es del A9: el acceso de 0x4e79a0/0x40f370 y 0x550d10 (portado y probado aparte),
    // medido sobre este mismo equipo.
    int RecalculationDraws()
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        const CrtRandom before = cloth.random;
        {
            const Client::Render::Models::LogicalAccess access(runtime.ownRecordDepth, cloth.random,
                                                               OwnRecordCloseRandoms);
            const auto recalculated = Client::Scenes::RecalculateOwnStats(
                world.OwnCharacter(), Client::Scenes::OwnStatsContext{.itemSetTables = sets,
                                                                      .itemSets = &world.ItemSets(),
                                                                      .items = &items,
                                                                      .publishedHero = world.Actors()[runtime.hero],
                                                                      .ownRecordAccesses = runtime.ownRecordDepth,
                                                                      .random = cloth.random,
                                                                      .held = Client::Scenes::HeldItemOf(runtime),
                                                                      .scan505820 = runtime.fullSetScan});
            EXPECT_TRUE(recalculated.HasValue()) << recalculated.Error();
        }
        return DrawsBetween(before, cloth.random.State());
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    const Client::Data::Tables::ItemTable items = EmptyItems();
    const Client::Data::Tables::ItemSetTables setTables = EmptySets();
    const Client::Data::Tables::ItemSetTables* sets = &setTables;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// Lo de 0x417f30 con el nivel 7 y el liderazgo en 1.000: (7 + 11) · 8³ · 100, (7 · 3 + 36) · 5 + 1.000 / 8,
// 1.000 / 4 + 200 + 7 · 15, 20 + 1.000 / 50 + 28 / 5 y 7 · 16 + 1.000.
void ExpectTheRavenOfLevelSeven(const DarkSpirit& raven, std::uint32_t experience)
{
    EXPECT_EQ(raven.word1e2, 7);
    EXPECT_EQ(raven.level, 7);
    EXPECT_EQ(raven.experience, experience);
    EXPECT_EQ(raven.nextExperience, 921'600u);
    EXPECT_EQ(raven.durability, RavenDurability);
    EXPECT_EQ(raven.word454, 410);
    EXPECT_EQ(raven.word456, 555);
    EXPECT_EQ(raven.word458, 45);
    EXPECT_EQ(raven.word45a, 1112);
}

// 0x421ef2..0x421fa1: con la ventana cerrada y nada en el cartel, la tabla no cambia de nivel y 0x4221d0 la recalcula
// con el 0; la clase 0 del cuervo en la casilla 1 llena el cuervo del héroe (0x417f30). Tres accesos y 0x550d10.
TEST(PetStatsTest, TheRavenReplyFillsTheRavenOfTheHero)
{
    auto tested = std::make_unique<Pets>();
    const auto applied = tested->Reply({.pet = 0, .kind = 0, .slot = 1, .level = 7, .experience = 5000});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ExpectTheRavenOfLevelSeven(tested->Raven(), 5000);
    const PetStats& table = tested->Pet(0);
    EXPECT_EQ(table.level, 0);
    EXPECT_EQ(table.experience, 0u);
    EXPECT_EQ(table.nextExperience, 1100u);
    EXPECT_EQ(table.word0c, 12 * 15 + 125);
    EXPECT_EQ(table.word0e, 250 + 200);
    EXPECT_EQ(table.word10, 40);
    EXPECT_EQ(table.word12, 1000);
    EXPECT_EQ(tested->Draws(), 3 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

// 0x421f62..0x421f6a: el cuervo del héroe solo con la casilla 1, y la clase fuera de 0, 0xfe y 0xff no lo toca.
TEST(PetStatsTest, TheRavenOfTheHeroNeedsItsSlotAndAPlainKind)
{
    for (const PetInfoReply reply : {PetInfoReply{.pet = 0, .kind = 0, .slot = 8, .level = 7, .experience = 1},
                                     PetInfoReply{.pet = 0, .kind = 1, .slot = 1, .level = 7, .experience = 1}})
    {
        auto tested = std::make_unique<Pets>();
        ASSERT_TRUE(tested->Reply(reply).HasValue());
        EXPECT_EQ(tested->Raven().level, 0);
        EXPECT_EQ(tested->Draws(), OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
    }
}

// 0x421ef2..0x421f40: con un ítem en el cartel, la tabla toma el nivel, su durabilidad y la experiencia, salvo con el
// entrenador abierto y una casilla del equipo (debajo de 12); el tipo 2 escribe la tercera y 0x4221d0 no la calcula.
TEST(PetStatsTest, TheHoveredItemWritesTheTableUnlessTheTrainerHasTheEquipment)
{
    for (const std::uint8_t slot : {std::uint8_t{1}, std::uint8_t{12}})
    {
        for (const std::int32_t trainer : {0, 1})
        {
            auto tested = std::make_unique<Pets>();
            tested->world.Runtime().hoveredItemCopy.id = 0x1a05;
            tested->world.Runtime().hoveredItemCopy.durability = 77;
            tested->world.Runtime().panels.delegated.trainer = trainer;
            ASSERT_TRUE(tested->Reply({.pet = 0, .kind = 9, .slot = slot, .level = 7, .experience = 42}).HasValue());
            const bool written = trainer == 0 || slot >= 12;
            const PetStats& table = tested->Pet(0);
            EXPECT_EQ(table.level, written ? 7 : 0) << int{slot} << " " << trainer;
            EXPECT_EQ(table.durability, written ? 77 : 0);
            EXPECT_EQ(table.experience, written ? 42u : 0u);
            EXPECT_EQ(table.nextExperience, written ? 921'600u : 1100u);
            EXPECT_EQ(table.word0c, written ? 410 : 305);
        }
    }
    auto third = std::make_unique<Pets>();
    third->world.Runtime().hoveredItemCopy.id = 0x1a04;
    ASSERT_TRUE(third->Reply({.pet = 2, .kind = 0, .slot = 20, .level = 3, .experience = 9}).HasValue());
    EXPECT_EQ(third->Pet(2).level, 3);
    EXPECT_EQ(third->Pet(2).nextExperience, 0u);
}

// Q-C1208: un tipo de 3 o más que el original escribe fuera de 0x723960 (0x421dfd con la ventana abierta, 0x421f2c con
// un ítem en el cartel) se descarta entero, sin rand() ni el cuervo ni 0x550d10, y la sesión sigue. Sin escribir (sin
// nada en el cartel, o con el entrenador y una casilla del equipo), 0x4221d0 hace solo su acceso y sigue 0x550d10.
TEST(PetStatsTest, APetBeyondTheThirdIsDiscardedWhenItWrites)
{
    for (const std::uint8_t pet : {std::uint8_t{3}, std::uint8_t{0xff}})
    {
        SCOPED_TRACE(int{pet});
        auto hovered = std::make_unique<Pets>();
        hovered->world.Runtime().hoveredItemCopy.id = 0x1a05;
        ASSERT_TRUE(hovered->Reply({.pet = pet, .kind = 0, .slot = 20, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(hovered->Draws(), 0);
        EXPECT_EQ(hovered->world.Runtime().ownRecordDepth, 0);

        auto open = std::make_unique<Pets>();
        open->world.Runtime().panels.delegated.pet = 2;
        ASSERT_TRUE(open->Reply({.pet = pet, .kind = 0, .slot = 1, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(open->Draws(), 0);
        EXPECT_EQ(open->Raven().level, 0);

        auto quiet = std::make_unique<Pets>();
        ASSERT_TRUE(quiet->Reply({.pet = pet, .kind = 0, .slot = 20, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(quiet->Draws(), OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());

        auto trainer = std::make_unique<Pets>();
        trainer->world.Runtime().hoveredItemCopy.id = 0x1a05;
        trainer->world.Runtime().panels.delegated.trainer = 1;
        ASSERT_TRUE(trainer->Reply({.pet = pet, .kind = 0, .slot = 1, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(trainer->Draws(), OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
        for (const PetStats& entry : trainer->world.Runtime().pets)
            EXPECT_EQ(entry.level, 0);
    }
}

// 0x421f8f..0x421fae: con 0xfe y 0xff, después del cuervo, los joints de la vtable + 0x14 y + 0x18 son de la CL11.
TEST(PetStatsTest, TheJointsOfTheRavenAreACutAfterItsInfo)
{
    for (const auto& [kind, where] : {std::pair<std::uint8_t, const char*>{std::uint8_t{0xfe}, "0x421f9a"},
                                      std::pair<std::uint8_t, const char*>{std::uint8_t{0xff}, "0x421fae"}})
    {
        auto tested = std::make_unique<Pets>();
        const auto cut = tested->Reply({.pet = 0, .kind = kind, .slot = 1, .level = 7, .experience = 5000});
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(where), std::string::npos) << cut.Error();
        ExpectTheRavenOfLevelSeven(tested->Raven(), 5000);
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
    }
}

// 0x421fb6..0x422196: el caballo en la casilla 8 toma el nivel, la durabilidad del registro propio y la experiencia;
// con 0xfe y 0xff, el +8 del héroe en 1 y 2; 0x4221d0(1) con la fuerza y +0x2a, y la opción 0x96 del Dark Horse
// (0x4225d0) con el registro abierto: 60 / 20 + 2 · 10 + 5.
TEST(PetStatsTest, TheHorseReplyWritesItsStatsTheRiderAndTheOption)
{
    for (const std::uint8_t kind : {std::uint8_t{0}, std::uint8_t{0xfe}, std::uint8_t{0xff}})
    {
        auto tested = std::make_unique<Pets>();
        Client::World::ItemState& horseItem = tested->world.OwnCharacter().equipment[8];
        const auto applied = tested->Reply({.pet = 1, .kind = kind, .slot = 8, .level = 10, .experience = 777});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const PetStats& horse = tested->Pet(1);
        EXPECT_EQ(horse.level, 10);
        EXPECT_EQ(horse.durability, HorseDurability);
        EXPECT_EQ(horse.experience, 777u);
        EXPECT_EQ(horse.nextExperience, 2'795'100u);
        EXPECT_EQ(horse.word0c, 182);
        EXPECT_EQ(horse.word0e, 273);
        EXPECT_EQ(horse.word10, 48);
        EXPECT_EQ(horse.word12, 1160);
        EXPECT_EQ(tested->world.Actors()[HeroIndex].byte8, kind == 0 ? 0 : (kind == 0xfe ? 1 : 2));
        ASSERT_EQ(horseItem.optionCount, 1);
        EXPECT_EQ(horseItem.optionBytes[0], 0x96);
        EXPECT_EQ(horseItem.optionBytes[Client::World::ItemOptionValueOffset], 3 + 20 + 5);
        EXPECT_EQ(tested->Raven().level, 0);
    }
}

// 0x421dc7..0x421ead: con la ventana de la mascota abierta (0x420cc0), el cuervo del héroe con el tipo 0, sea cual sea
// la casilla o la clase, y la tabla con la durabilidad del caballo del registro propio. Cuatro accesos y 0x550d10.
TEST(PetStatsTest, TheOpenWindowFillsTheRavenAndTheTableWithTheHorseDurability)
{
    auto tested = std::make_unique<Pets>();
    tested->world.Runtime().panels.delegated.pet = 2;
    const auto applied = tested->Reply({.pet = 0, .kind = 0xfe, .slot = 5, .level = 7, .experience = 5000});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ExpectTheRavenOfLevelSeven(tested->Raven(), 5000);
    const PetStats& table = tested->Pet(0);
    EXPECT_EQ(table.level, 7);
    EXPECT_EQ(table.durability, HorseDurability);
    EXPECT_EQ(table.experience, 5000u);
    EXPECT_EQ(table.word0c, 410);
    EXPECT_EQ(table.word0e, 555);
    EXPECT_EQ(table.word10, 45);
    EXPECT_EQ(table.word12, 1112);
    EXPECT_EQ(tested->Draws(), 4 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());

    auto horse = std::make_unique<Pets>();
    horse->world.Runtime().panels.delegated.pet = 2;
    ASSERT_TRUE(horse->Reply({.pet = 1, .kind = 0xff, .slot = 8, .level = 10, .experience = 1}).HasValue());
    EXPECT_EQ(horse->Pet(1).level, 10);
    EXPECT_EQ(horse->Raven().level, 0);
    EXPECT_EQ(horse->world.Actors()[HeroIndex].byte8, 0) << "la ventana abierta no toca al jinete";
    EXPECT_EQ(horse->Draws(), 2 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
}

// 0x4e4cd6: sin los conjuntos, 0x550d10 corta en 0x40e1b0 después de la tabla, con el registro cerrado.
TEST(PetStatsTest, WithoutTheSetTablesTheRecalculationCuts)
{
    auto tested = std::make_unique<Pets>();
    tested->sets = nullptr;
    tested->world.Runtime().panels.delegated.pet = 2;
    const auto cut = tested->Reply({.pet = 0, .kind = 0, .slot = 1, .level = 7, .experience = 5000});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x40e1b0"), std::string::npos) << cut.Error();
    EXPECT_EQ(tested->Pet(0).level, 7);
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

// 0x420cd0: la pestaña, 0x723911 y las tres mascotas en 0, sin rand().
TEST(PetStatsTest, ResettingThePetsZeroesTheTableTheTabAndTheWindow)
{
    const auto owned = std::make_unique<Client::Scenes::WorldRuntime>();
    Client::Scenes::WorldRuntime& runtime = *owned;
    runtime.petTab = 1;
    runtime.panels.delegated.pet = 2;
    runtime.pets[2].experience = 5;
    runtime.pets[0].word12 = 9;
    Client::Scenes::ResetThePets(runtime);
    EXPECT_EQ(runtime.petTab, 0);
    EXPECT_EQ(runtime.panels.delegated.pet, 0);
    EXPECT_EQ(runtime.pets[2].experience, 0u);
    EXPECT_EQ(runtime.pets[0].word12, 0);
}
} // namespace
