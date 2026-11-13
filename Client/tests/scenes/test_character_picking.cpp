#include "Scenes/CharacterPicking.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameUi.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"
#include "World/TerrainPickingSegment.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// El picking de los personajes del juego (0x59e700) y lo que 0x59ec80 hace con él. Los mundos van en el heap.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::PickTheCharacter;
using Client::Scenes::WorldState;
using Client::World::TerrainPickingSegment;

constexpr std::uint8_t Players = 1;
constexpr std::uint8_t Monsters = 0x22;
constexpr std::uint8_t Npcs = 4;
constexpr std::int32_t NoPick = -1;

// Un segmento horizontal a lo largo de X, en Y 1.000 y a 60 de altura: toca la caja de quien esté en Y 1.000 (de −60 a
// 40 alrededor de la posición, Q-C100).
const TerrainPickingSegment Ray{.origin = {0.0f, 1000.0f, 60.0f}, .end = {20000.0f, 1000.0f, 60.0f}};

void Place(WorldState& world, std::size_t slot, float x, std::uint8_t kind)
{
    ActorState& actor = world.Actors()[slot];
    actor.alive = true;
    actor.seen = true;
    actor.alpha = 1.0f;
    actor.byte42c = 1;
    actor.byte88 = kind;
    actor.position = {x, 1000.0f, 0.0f};
}

struct Picking
{
    Picking()
    {
        world->Runtime().hero = 0;
        Place(*world, 0, 500.0f, Players);
    }
    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
};

// 0x59e737..0x59e7f6: los de la máscara, vivos, a la vista y con alpha se tiñen por +0x1d6 y por el PK.
TEST(CharacterPickingTest, TheMaskedOnesAreTintedByLevelAndPk)
{
    Picking tested;
    WorldState& world = *tested.world;
    for (std::size_t slot = 1; slot <= 6; ++slot)
    {
        Place(world, slot, 0.0f, Players);
        world.Actors()[slot].level = static_cast<std::uint16_t>(slot - 1);
        world.Actors()[slot].light = glm::vec3(9.0f);
    }
    world.Actors()[7] = world.Actors()[1];
    world.Actors()[7].pk = 6;
    world.Actors()[8] = world.Actors()[1];
    world.Actors()[8].byte88 = 2;
    world.Actors()[9] = world.Actors()[1];
    world.Actors()[9].alpha = 0.0f;

    EXPECT_EQ(PickTheCharacter(world, Players, std::nullopt), NoPick);
    const auto light = [&world](std::size_t slot) { return world.Actors()[slot].light; };
    EXPECT_EQ(light(1), glm::vec3(0.2f));
    EXPECT_EQ(light(2), glm::vec3(-0.4f));
    EXPECT_EQ(light(3), glm::vec3(0.2f, -0.6f, -0.6f));
    EXPECT_EQ(light(4), glm::vec3(1.5f));
    EXPECT_EQ(light(5), glm::vec3(0.3f, 0.2f, -0.5f));
    EXPECT_EQ(light(6), glm::vec3(9.0f));
    EXPECT_EQ(light(7), glm::vec3(-0.4f));
    EXPECT_EQ(light(8), glm::vec3(9.0f));
    EXPECT_EQ(light(9), glm::vec3(9.0f));
}

// 0x59e7fc..0x59ea95: el más cercano a la cámara cuya caja toca el segmento; sin el héroe publicado, el que se muere,
// el escondido (+0x7c & 0x10000000), el de +0x42c en 0 ni el de otra máscara.
TEST(CharacterPickingTest, TheNearestBoxOnTheSegmentIsPicked)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 3000.0f, Players);
    Place(world, 2, 2000.0f, Players);
    Place(world, 3, 900.0f, Players);
    world.Actors()[3].dying = 1;
    Place(world, 4, 1000.0f, Players);
    world.Actors()[4].flags7c = 0x10000000;
    Place(world, 5, 1100.0f, Players);
    world.Actors()[5].byte42c = 0;
    Place(world, 6, 1200.0f, Monsters & 2);
    // Fuera del segmento: en Y 2.000.
    Place(world, 7, 600.0f, Players);
    world.Actors()[7].position.y = 2000.0f;
    EXPECT_EQ(PickTheCharacter(world, Players, Ray), 2);
    EXPECT_EQ(PickTheCharacter(world, Monsters, Ray), 6);
    EXPECT_EQ(PickTheCharacter(world, Npcs, Ray), NoPick);
    // Empatados, gana el primero.
    Place(world, 8, 2000.0f, Players);
    EXPECT_EQ(PickTheCharacter(world, Players, Ray), 2);
}

// 0x59e9ec..0x59ea6d: en Lorencia la capa 4 y en Devias la 3 cuentan solo con la capa de la celda del héroe; en los
// demás mapas, cualquiera.
TEST(CharacterPickingTest, TheUpperFloorsCountOnlyOnTheHerosLayer)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t upper;
    };
    for (const Case tried : {Case{0, 4}, Case{2, 3}})
    {
        SCOPED_TRACE(tried.map);
        Picking tested;
        WorldState& world = *tested.world;
        world.Runtime().map = tried.map;
        auto& mapping = world.Map().terrain.mapping;
        mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        Place(world, 1, 1250.0f, Players);
        Place(world, 2, 3000.0f, Players);
        // La celda (12, 10) del primero.
        mapping->layer1[Client::Data::Terrain::CellIndex(12, 10)] = tried.upper;
        EXPECT_EQ(PickTheCharacter(world, Players, Ray), 2);
        world.Runtime().heroCellLayer = tried.upper;
        EXPECT_EQ(PickTheCharacter(world, Players, Ray), 1);
        // En otro mapa la capa no cuenta.
        world.Runtime().heroCellLayer = 0;
        world.Runtime().map = 1;
        EXPECT_EQ(PickTheCharacter(world, Players, Ray), 1);
    }
}

// 0x59ec80 con el segmento del dibujo.
struct Frame
{
    Frame()
    {
        world->Runtime().hero = 0;
        Place(*world, 0, 500.0f, Players);
    }
    Client::Core::Expected<void, std::string> Pick(bool alt = false)
    {
        return Client::Scenes::PickInTheFrame({.world = *world,
                                               .random = random,
                                               .ownRecordDepth = world->Runtime().ownRecordDepth,
                                               .targets = world->Runtime().selectedTargets,
                                               .altDown = alt,
                                               .ray = Ray,
                                               .mouse = mouse});
    }
    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random;
    Client::UI::Point mouse{};
};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int draw = 0; draw < count; ++draw)
        static_cast<void>(random.Next());
    return random;
}

// Un nombre inventado en los bytes de un personaje o de un lugar de la party.
void Name(std::span<std::byte> name, std::string_view text)
{
    std::ranges::fill(name, std::byte{0});
    for (std::size_t at = 0; at < text.size(); ++at)
        name[at] = static_cast<std::byte>(text[at]);
}

// La party de 0x77f3cd0 con estos nombres, cada uno con +0x1c en `index`.
void Party(WorldState& world, std::initializer_list<std::string_view> names, std::int32_t index)
{
    Client::Scenes::PartyState& party = world.Runtime().social.party;
    std::size_t at = 0;
    for (const std::string_view name : names)
    {
        Name(party.slots[at].name, name);
        party.slots[at].actorIndex = index;
        party.slots[at].actorFlags = 0x55;
        ++at;
    }
    world.Runtime().hud.partyCount = static_cast<std::int32_t>(names.size());
}

std::array<std::int32_t, 4> Indices(const WorldState& world)
{
    const auto& slots = world.Runtime().social.party.slots;
    return {slots[0].actorIndex, slots[1].actorIndex, slots[2].actorIndex, slots[3].actorIndex};
}

// 0x59e86a..0x59e948 y 0x59ea9b..0x59eb66: cada jugador (+0x88 en 1) que pasa las primeras condiciones busca su lugar
// en −2 de la party, aun sin segmento, con su +0x78; el héroe también. Al final, el héroe que no se ve queda en −3 y
// los demás sin personaje en −1, con +0x20 en 0.
TEST(CharacterPickingTest, ThePlayersFindTheirPartySlotsWhilePicking)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Picking tested;
        WorldState& world = *tested.world;
        world.Runtime().map = map;
        Name(world.Actors()[0].name, "Tejon");
        Place(world, 3, 2000.0f, Players);
        Name(world.Actors()[3].name, "Brezal");
        world.Actors()[3].flags78 = 0x48;
        Place(world, 4, 2500.0f, 2);
        Name(world.Actors()[4].name, "Zorzal");
        Party(world, {"Brezal", "Tejon", "Zorzal", "Mirlo"}, -2);
        EXPECT_EQ(PickTheCharacter(world, Npcs, std::nullopt), NoPick);
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{3, 0, -1, -1}));
        const auto& slots = world.Runtime().social.party.slots;
        EXPECT_EQ(slots[0].actorFlags, 0x48U);
        EXPECT_EQ(slots[2].actorFlags, 0U);
        // Lo encontrado se queda: solo se busca en −2.
        Name(world.Actors()[3].name, "Otro");
        EXPECT_EQ(PickTheCharacter(world, Players, Ray), 3);
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{3, 0, -1, -1}));

        // El héroe que no se ve.
        Party(world, {"Brezal", "Tejon", "Zorzal", "Mirlo"}, -2);
        world.Actors()[0].seen = false;
        static_cast<void>(PickTheCharacter(world, Players, Ray));
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{-1, -3, -1, -1}));
        EXPECT_EQ(slots[1].actorFlags, 0U);
    }
}

// 0x59ef2f → 0x59e3b0: con el mouse en la UI solo busca a la party, entre los de tipo 0x20a.
TEST(CharacterPickingTest, OverTheUiOnlyThePartyIsLookedFor)
{
    Frame tested;
    WorldState& world = *tested.world;
    world.Runtime().map = 2;
    world.Runtime().ui.mouseOnUi = 1;
    Name(world.Actors()[0].name, "Tejon");
    Place(world, 3, 2000.0f, Players);
    Name(world.Actors()[3].name, "Brezal");
    world.Actors()[3].type = 0x20a;
    Place(world, 4, 2500.0f, Players);
    Name(world.Actors()[4].name, "Zorzal");
    Party(world, {"Brezal", "Tejon", "Zorzal"}, -2);
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{3, -3, -1, 0}));
    EXPECT_EQ(world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    EXPECT_EQ(tested.random.State(), CrtRandom().State());
}

// 0x5e4140: con la party cerrada, la lista de la derecha (640 − 50, de a 19 desde 5). Con la skill 0x1a, el Elf abre el
// registro propio por cada miembro con personaje y elige al del renglón del mouse; después 0x59e3b0 los vuelve a
// buscar. Un miembro en −1 ocupa su renglón y uno en −3, no. El Dark Knight no abre nada.
TEST(CharacterPickingTest, TheListOnTheRightPicksAMember)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Frame tested;
        WorldState& world = *tested.world;
        world.Runtime().map = map;
        world.Actors()[0].characterClass = 2;
        world.Actors()[0].type = 0x20a;
        world.OwnCharacter().words6c[0] = 0x1a;
        Name(world.Actors()[0].name, "Tejon");
        Place(world, 3, 2000.0f, Players);
        world.Actors()[3].type = 0x20a;
        world.Actors()[3].position.y = 5000.0f;
        Name(world.Actors()[3].name, "Brezal");
        Party(world, {"Mirlo", "Tejon", "Zorzal", "Brezal"}, -1);
        auto& slots = world.Runtime().social.party.slots;
        slots[0].actorIndex = -3;
        slots[1].actorIndex = 0;
        slots[3].actorIndex = 3;
        // Tejon en [5, 24], Zorzal en [24, 43] y Brezal en [43, 62].
        tested.mouse = {.x = 587, .y = 62};
        const CrtRandom expected = Advanced(tested.random, 3 * Client::Render::Models::OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], 3);
        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{-1, 0, -1, 3}));

        // Fuera de la lista (x + 50 cuenta, x + 51 no), busca como siempre: sin nadie en el segmento.
        tested.mouse = {.x = 641, .y = 62};
        world.Runtime().selectedTargets = {-1, -1, -1, -1, -1};
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], -1);

        // El Dark Knight no abre el registro ni elige desde la lista.
        world.Actors()[0].characterClass = 1;
        Party(world, {"Mirlo", "Tejon", "Zorzal", "Brezal"}, 3);
        tested.mouse = {.x = 600, .y = 10};
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], -1);
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x59efb9..0x59f509: el Dark Wizard abre el registro propio por su skill (los 3.848 rand() del cierre) y elige al
// jugador de la máscara 1 en 0x6c8e30. Sin elegido de antes, 0x59ef07 ya dejó 0x6c8e38 en −1.
TEST(CharacterPickingTest, TheWizardPicksAPlayerAndDrawsTheOwnRecord)
{
    Frame tested;
    Place(*tested.world, 3, 2000.0f, Players);
    auto& targets = tested.world->Runtime().selectedTargets;
    targets[4] = 9;
    const CrtRandom expected = Advanced(tested.random, Client::Render::Models::OwnRecordCloseRandoms);
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(targets, (std::array<std::int32_t, 5>{-1, -1, 3, -1, -1}));
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x59efda..0x59f489: con la skill 0x1a a 0x1c el Elf busca primero a los jugadores; sin ella, a los monstruos.
TEST(CharacterPickingTest, TheSkillOfTheElfPutsThePlayersFirst)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1a}, std::uint8_t{0x1c}, std::uint8_t{0x1d}})
    {
        SCOPED_TRACE(int{skill});
        Frame tested;
        WorldState& world = *tested.world;
        world.Actors()[0].characterClass = 2;
        world.OwnCharacter().words6c[0] = skill;
        Place(world, 3, 2000.0f, Players);
        Place(world, 4, 3000.0f, 2);
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], skill <= 0x1c ? 3 : 4);
    }
}

// 0x59ef46..0x59efb4: con Alt, primero los de la máscara 4 (0x6c8e2c) y, sin ninguno, los de 0x22 y 1.
TEST(CharacterPickingTest, AltLooksForTheNpcsFirst)
{
    Frame tested;
    WorldState& world = *tested.world;
    world.Actors()[0].characterClass = 1;
    Place(world, 3, 2000.0f, Players);
    Place(world, 4, 3000.0f, Npcs);
    ASSERT_TRUE(tested.Pick(true).HasValue());
    EXPECT_EQ(world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, 4, -1, -1, -1}));
    world.Actors()[4].alive = false;
    ASSERT_TRUE(tested.Pick(true).HasValue());
    EXPECT_EQ(world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, 3, -1, -1}));
    EXPECT_EQ(tested.random.State(), CrtRandom().State());
}

// 0x59ee4b..0x59ef10: el monstruo elegido sigue con el ataque automático y sin botones; con un botón, se suelta.
TEST(CharacterPickingTest, TheChosenMonsterStaysWithoutButtons)
{
    Frame tested;
    WorldState& world = *tested.world;
    world.Actors()[0].characterClass = 1;
    Place(world, 5, 8000.0f, 2);
    world.Actors()[5].position.y = 5000.0f;
    Place(world, 3, 2000.0f, Players);
    auto& targets = world.Runtime().selectedTargets;
    targets = {-1, -1, 5, -1, 5};
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(targets[2], 5);
    EXPECT_EQ(targets[4], 5);
    world.Runtime().movement.leftHeld = true;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(targets[2], 3);
    EXPECT_EQ(targets[4], 5);
}
} // namespace
