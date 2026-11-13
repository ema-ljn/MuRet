#include "Scenes/HeroSkills.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// La cabeza de 0x57cfc0 (CL9 hito 4, fase 2b; 06 § Usar una skill): el héroe en el lugar 0, un elegido en el 5 y un
// Skill.bmd inventado. La rama de las clases 1, 3 y 4 (HeroSkillsKnight, con sus tests) termina sin arma derecha con
// las skills que no son de su lista; las otras ramas todavía cortan con su dirección.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;
using Client::Scenes::UseTheHeroSkill;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
// El registro propio con la skill del lugar y la entrada del alcance (0x5526f0): lo que gasta toda vuelta con uso.
constexpr int HeadDraws = OwnDraws + EntryDraws;
// Más la entrada del +0x31 (0x57d2f1).
constexpr int PartyDraws = HeadDraws + EntryDraws;
// Más las dos celdas lógicas (0x57d4d6, 0x57d4f4).
constexpr int AimDraws = PartyDraws + 2 * CellDraws;
// La rama de las clases 1, 3 y 4 (HeroSkillsKnight) sin arma derecha y con una skill fuera de su lista: el maná y la
// stamina de 0x551c70 y el cierre del registro propio, abierto desde 0x581894.
constexpr int KnightBranchDraws = EntryDraws + OwnDraws;
constexpr std::size_t ChosenSlot = 5;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

struct InventedSkill
{
    std::uint8_t skill = 0;
    std::uint8_t range = 0;
    std::uint8_t party = 0;
};

// Skill.bmd con el alcance (+0x26) y el byte de grupo (+0x31) de cada skill de `skills`.
Client::Data::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Data::Tables::SkillRecordSize;
        plain[base + 0x26] = static_cast<std::byte>(invented.range);
        plain[base + 0x31] = static_cast<std::byte>(invented.party);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

enum HeroClass : std::uint8_t
{
    Wizard = 0,
    Knight = 1,
    Elf = 2,
    Gladiator = 3,
    Lord = 4,
};

// Un héroe jugador en (182, 115) con la skill `skill` en su lugar 0 y la acción 1, que deja pasar 0x57d45d.
struct SkillHero
{
    explicit SkillHero(std::uint8_t skill, std::uint8_t heroClass = Knight, std::int32_t map = 0)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero.alive = true;
        hero.type = Client::Data::Models::PlayerSkeletonModel;
        hero.characterClass = heroClass;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Scenes::NoItemModel;
        hero.alpha = 1.0f;
        hero.animation.action = 1;
        hero.skillIndex = 0;
        world.OwnCharacter().SetSkillAt(0, skill);
        auto& chosen = world.Actors()[ChosenSlot];
        chosen.alive = true;
        chosen.byte88 = 2; // un monstruo: 0x558bb0 lo deja atacar
        chosen.position = {18450.0f, 11750.0f, 0.0f};
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::UI::GameUiState& Ui()
    {
        return world.Runtime().ui;
    }
    Client::Scenes::GameMovementState& Movement()
    {
        return world.Runtime().movement;
    }
    HeroSkillContext Context()
    {
        return HeroSkillContext{.world = world,
                                .random = random,
                                .skills = &skills,
                                .worldTime = worldTime,
                                .tickCount = [] { return 10'000u; },
                                .playerSkeleton = &skeleton};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return UseTheHeroSkill(Hero(), Context());
    }

    Client::Scenes::WorldState world;
    CrtRandom random;
    float worldTime = 5000.0f;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Data::Tables::SkillTable skills = SkillsWith({{0x11, 3},
                                                          {0x13, 4},
                                                          {0x16, 6},
                                                          {0x18, 6},
                                                          {0x1a, 2},
                                                          {0x28, 7},
                                                          {0x2c, 3},
                                                          {0x2f, 2},
                                                          {0x30, 2},
                                                          {0x31, 3},
                                                          {0x3a, 7},
                                                          {0x43, 3},
                                                          {0x4b, 3},
                                                          {0x1d, 5, 2}});
};

std::unique_ptr<SkillHero> AHeroClicking(std::uint8_t skill, std::uint8_t heroClass = Knight, std::int32_t map = 0)
{
    auto tested = std::make_unique<SkillHero>(skill, heroClass, map);
    tested->Ui().rightClick = true;
    tested->world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
    return tested;
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

// 0x57cfe2..0x57d00f: con el modo del editor, el mouse en la UI o 0x1000 en +0x78, nada, ni rand().
TEST(HeroSkillsTest, TheGuardsDoNothing)
{
    for (int guard = 0; guard < 3; ++guard)
    {
        SCOPED_TRACE(guard);
        const auto tested = AHeroClicking(0x11);
        if (guard == 0)
            tested->Movement().inputBlocked = true;
        else if (guard == 1)
            tested->Ui().mouseOnUi = 1;
        else
            tested->Hero().flags78 = 0x1000;
        tested->Hero().byte80 = 1;
        const auto before = tested->random.State();
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), before);
        EXPECT_TRUE(tested->Ui().rightClick);
        EXPECT_FLOAT_EQ(tested->Hero().alpha, 1.0f);
    }
}

// 0x57d23c..0x57d260: sin el derecho ni el ataque automático, el reloj vuelve a empezar después del registro propio y
// de la entrada del alcance; el alcance queda en el local de las ramas.
TEST(HeroSkillsTest, WithoutAUseTheClockStartsAgain)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Knight, map);
        tested->Movement().actionInputTime = 10.0f;
        tested->Movement().actionInputElapsed = 7.0f;
        tested->Movement().followTarget = 3;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputElapsed, 0.0f);
        EXPECT_FLOAT_EQ(tested->Movement().selectedSkillRange, 4.0f);
        EXPECT_EQ(tested->Movement().followTarget, 3);
    }
}

// 0x5526f0: montado en el 0x1c16, el alcance suma 2.
TEST(HeroSkillsTest, TheThirdMountAddsTwoToTheRange)
{
    auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13});
    tested->Hero().pet.model = 0x1c16;
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(tested->Movement().selectedSkillRange, 6.0f);
}

// 0x57d02b..0x57d04a: con +0x80 en 1, el desvanecido baja 0,1 por vuelta y, debajo de 0,1, +0x80 pasa a 2.
TEST(HeroSkillsTest, TheTeleportFadeLowersTheAlpha)
{
    auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13});
    tested->Hero().byte80 = 1;
    tested->Hero().alpha = 0.5f;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_FLOAT_EQ(tested->Hero().alpha, 0.4f);
    EXPECT_EQ(tested->Hero().byte80, 1);
    tested->Hero().alpha = 0.15f;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_NEAR(tested->Hero().alpha, 0.05f, 1e-6f);
    EXPECT_EQ(tested->Hero().byte80, 2);
}

// 0x57d179..0x57d1b8: el derecho sobre la 0x28 la carga como 0x3a (los frames en 1 y +0x1bc del publicado en 0) si
// +0x80 es 0 o 3; con otro, se va sin gastar el derecho.
TEST(HeroSkillsTest, TheRightClickChargesTheSkill28As3a)
{
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().byte1bc = 9;
        tested->Hero().byte80 = 3;
        tested->Ui().autoAttack = true;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
        EXPECT_FALSE(tested->Ui().rightClick);
        EXPECT_EQ(tested->Ui().rightHoldFrames, 1);
        EXPECT_EQ(tested->Hero().byte1bc, 0);
        // 0x57d419: la del Wizard ya es la 0x3a; el alcance es el de la 0x28 (0x57d114, antes de cargarla).
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_FLOAT_EQ(tested->Movement().selectedSkillRange, 7.0f);
    }
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().byte80 = 2;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->Ui().rightClick);
        EXPECT_EQ(tested->Ui().rightHoldFrames, 0);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 0.0f);
        EXPECT_EQ(tested->random.State(), expected.State());
    }
}

// 0x57d12c..0x57d165: con la carga, los frames cuentan hasta el soltado, que limpia el derecho y la usa.
TEST(HeroSkillsTest, WhileChargingTheFramesCountUntilTheRelease)
{
    const auto tested = AHeroClicking(0x3a, Wizard);
    tested->Ui().rightHoldFrames = 4;
    tested->Movement().actionInputTime = 1000.0f;
    const auto held = tested->Use();
    ASSERT_TRUE(held.HasValue()) << held.Error();
    EXPECT_EQ(tested->Ui().rightHoldFrames, 5);
    EXPECT_TRUE(tested->Ui().rightClick);
    EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);

    tested->Ui().rightReleased = true;
    tested->Ui().rightDown = true;
    const auto released = tested->Use();
    ASSERT_FALSE(released.HasValue());
    EXPECT_TRUE(Contains(released.Error(), "0x5897b7")) << released.Error();
    EXPECT_FALSE(tested->Ui().rightReleased);
    EXPECT_FALSE(tested->Ui().rightClick);
    EXPECT_FALSE(tested->Ui().rightDown);
    EXPECT_EQ(tested->Ui().rightHoldFrames, 0);
    EXPECT_FLOAT_EQ(tested->Movement().actionInputElapsed, 0.0f);
}

// 0x57d1bc..0x57d1ef: el ataque automático usa la skill sin el derecho con 0x6c8e38 en 2 y un elegido, salvo en el mapa
// 6 y en Chaos Castle.
TEST(HeroSkillsTest, TheAutoAttackUsesTheSkillWithTheOtherChosenInTwo)
{
    for (const std::int32_t map : {0, 2, 6, 18, 23})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Wizard, map);
        tested->Ui().autoAttack = true;
        auto& targets = tested->world.Runtime().selectedTargets;
        targets[2] = static_cast<std::int32_t>(ChosenSlot);
        targets[4] = 2;
        const auto result = tested->Use();
        const bool here = map != 6 && map != 18 && map != 23;
        EXPECT_EQ(result.HasValue(), !here);
        if (here)
        {
            EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
        }
        else
        {
            EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);
        }
    }
    {
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Wizard);
        tested->Ui().autoAttack = true;
        tested->world.Runtime().selectedTargets[4] = 2;
        ASSERT_TRUE(tested->Use().HasValue());
    }
}

// 0x57d1f8..0x57d22d: el tiempo desde la última vuelta sin uso, en s; a las 3.600 suelta el derecho y se va.
TEST(HeroSkillsTest, AfterAnHourTheRightButtonIsLetGo)
{
    {
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Ui().rightClick = false;
        tested->Ui().rightDown = true;
        tested->Movement().followTarget = 7;
        tested->worldTime = 3600000.0f;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(tested->Ui().rightDown);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputElapsed, 3600.0002f);
        // 0x6c8e50 se toca recién en 0x57d294.
        EXPECT_EQ(tested->Movement().followTarget, 7);
    }
    {
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Movement().followTarget = 7;
        tested->worldTime = 3599000.0f;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
        EXPECT_NEAR(tested->Movement().actionInputElapsed, 3599.0f, 1e-3f);
        EXPECT_EQ(tested->Movement().followTarget, -1);
    }
}

// 0x57d261..0x57d282: con el movimiento trabado (0x77f3bac), solo el clic izquierdo (que se gasta) o el botón abajo; el
// derecho no cuenta ni se gasta y el reloj no se mira.
TEST(HeroSkillsTest, ABlockedHeroUsesTheSkillWithTheLeftButton)
{
    {
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Movement().movementBlocked = true;
        tested->Movement().actionInputTime = 1.0f;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->Ui().rightClick);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 1.0f);
    }
    for (const bool click : {true, false})
    {
        SCOPED_TRACE(click);
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Wizard);
        tested->Movement().movementBlocked = true;
        tested->Movement().pendingClick = click;
        tested->Movement().leftHeld = !click;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
        EXPECT_FALSE(tested->Movement().pendingClick);
    }
}

// 0x5a5540: las skills del asedio solo en el mapa 30 con el asedio; si no, se va antes de la entrada del +0x31.
TEST(HeroSkillsTest, TheSiegeSkillsNeedTheSiege)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x2c, Wizard, map);
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().followTarget, -1);
    }
    const auto tested = AHeroClicking(0x2c, Wizard, 30);
    tested->world.Runtime().siegeActive = true;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
}

// 0x57d30c..0x57d31e: con el +0x31 en 2, la skill pide 0x200 en +0x78 del héroe.
TEST(HeroSkillsTest, APartySkillNeedsThe200Flag)
{
    {
        const auto tested = AHeroClicking(0x1d, Wizard);
        const CrtRandom expected = Advanced(tested->random, PartyDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
    }
    const auto tested = AHeroClicking(0x1d, Wizard);
    tested->Hero().flags78 = 0x200;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
}

// 0x57d324..0x57d337: con +0x389, nada después de la entrada.
TEST(HeroSkillsTest, ADyingHeroStopsAfterTheEntry)
{
    const auto tested = AHeroClicking(0x13, Wizard);
    tested->Hero().dying = 1;
    const CrtRandom expected = Advanced(tested->random, PartyDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x57d33d..0x57d3a5: en la zona segura, nada en Lorencia ni en Devias; en Blood Castle y Chaos Castle, solo las skills
// de la lista.
TEST(HeroSkillsTest, TheSafeZoneOnlyLetsTheListInTheEventMaps)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t skill;
        bool goesOn;
    };
    for (const Case& tried : {Case{0, 0x1a, false}, Case{2, 0x1a, false}, Case{11, 0x1a, true}, Case{17, 0x13, false},
                              Case{18, 0x4b, true}, Case{18, 0x43, true}, Case{23, 0x11, false}, Case{24, 0x1a, false}})
    {
        SCOPED_TRACE(tried.map);
        SCOPED_TRACE(tried.skill);
        const auto tested = AHeroClicking(tried.skill, Wizard, tried.map);
        tested->Hero().safeZone = 1;
        const CrtRandom expected = Advanced(tested->random, PartyDraws);
        const auto result = tested->Use();
        EXPECT_EQ(result.HasValue(), !tried.goesOn);
        if (!tried.goesOn)
        {
            EXPECT_EQ(tested->random.State(), expected.State());
        }
    }
}

// 0x57d3ab..0x57d458: con el ataque automático, 0x6c8e38 por la clase y la skill.
TEST(HeroSkillsTest, TheAutoAttackChoosesTheOtherTargetByClassAndSkill)
{
    struct Case
    {
        std::uint8_t heroClass;
        std::uint8_t skill;
        std::int32_t target;
    };
    for (const Case& tried : {Case{Elf, 0x18, 2}, Case{Elf, 0x13, -1}, Case{Elf, 0x43, -1}, Case{Knight, 0x30, -1},
                              Case{Knight, 0x13, 2}, Case{Lord, 0x40, -1}, Case{Lord, 0x3f, -1}, Case{Lord, 0x30, 2},
                              Case{Wizard, 0x3a, -1}, Case{Wizard, 0x13, 2}, Case{Gladiator, 0x30, 2},
                              Case{Gladiator, 0x4b, -1}, Case{Gladiator, 0x48, -1}, Case{5, 0x3a, 2}})
    {
        SCOPED_TRACE(tried.heroClass);
        SCOPED_TRACE(tried.skill);
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(tried.skill, tried.heroClass, map);
            tested->Ui().autoAttack = true;
            static_cast<void>(tested->Use());
            EXPECT_EQ(tested->world.Runtime().selectedTargets[4], tried.target);
        }
    }
    // En Chaos Castle no se elige.
    const auto tested = AHeroClicking(0x13, Wizard, 18);
    tested->Ui().autoAttack = true;
    static_cast<void>(tested->Use());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
}

// 0x57d45d..0x57d4b0: un jugador con una acción fuera de la lista no la usa y olvida los frames del derecho (la carga
// que acaba de empezar); otro tipo, con la acción 2 o más, no la usa y los deja.
TEST(HeroSkillsTest, TheActionOfTheHeroGatesTheUse)
{
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().animation.action = 0x15;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Ui().rightHoldFrames, 0);
        EXPECT_FALSE(tested->Ui().rightClick);
    }
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().type = 0x1f;
        tested->Hero().animation.action = 2;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Ui().rightHoldFrames, 1);
    }
    for (const std::uint8_t action : {std::uint8_t{0xc}, std::uint8_t{0x8a}, std::uint8_t{0x44}, std::uint8_t{0x48},
                                      std::uint8_t{0x4a}, std::uint8_t{0x76}, std::uint8_t{0x79}})
    {
        SCOPED_TRACE(action);
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Hero().animation.action = action;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
    }
}

// 0x57d4b6: el Wizard no apunta ni mira las armas: va directo a su rama, con el blanco como estaba.
TEST(HeroSkillsTest, TheWizardGoesStraightToItsBranch)
{
    const auto tested = AHeroClicking(0x13, Wizard);
    tested->Movement().targetX = 9;
    const CrtRandom expected = Advanced(tested->random, PartyDraws);
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().targetX, 9u);
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
}

// 0x57ce80 y 0x57d532..0x57d61b: el caballero apunta al elegido (su celda y su posición) y, por cada mano sin la opción
// de la skill, pide la del arma con el lugar y el elegido; el registro abierto en cada mano cierra con 3.848 rand().
// Sin arma derecha, la rama (0x581894) termina sin la 0x13; el Magic Gladiator sigue en la del Wizard (0x5897b7).
TEST(HeroSkillsTest, AKnightAimsAtTheChosenAndAsksForTheWeaponSkill)
{
    for (const std::uint8_t heroClass : {Knight, Gladiator, Lord})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(heroClass);
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(0x13, heroClass, map);
            const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws);
            const auto result = tested->Use();
            if (heroClass == Gladiator)
            {
                ASSERT_FALSE(result.HasValue());
                EXPECT_TRUE(Contains(result.Error(), "0x5897b7")) << result.Error();
            }
            else
            {
                ASSERT_TRUE(result.HasValue()) << result.Error();
            }
            EXPECT_EQ(tested->random.State(), expected.State());
            const auto& movement = tested->Movement();
            EXPECT_EQ(movement.targetX, 184u);
            EXPECT_EQ(movement.targetY, 117u);
            EXPECT_EQ(tested->Hero().attackPosition, glm::vec3(18450.0f, 11750.0f, 0.0f));
            EXPECT_EQ(movement.weaponSkill.active, 1u);
            EXPECT_EQ(movement.weaponSkill.slot, 0u);
            EXPECT_EQ(movement.weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
            EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
        }
    }
    // 0x57d617: un elegido que 0x558bb0 no deja atacar (un NPC) va como −1.
    const auto tested = AHeroClicking(0x13, Knight);
    tested->world.Actors()[ChosenSlot].byte88 = 4;
    static_cast<void>(tested->Use());
    EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
}

// 0x57d564..0x57d57b: montado, la tabla 0x599d70 saltea las armas con las skills que no deja (la 0x30) y las mira con
// las demás (la 0x2f). La 0x30 sigue sin arma derecha: la rama la paga (0x551c70, 0x552c50 y 0x551f40), saca los dos
// rand() del C3 sin la sesión y hace la 0x3f.
TEST(HeroSkillsTest, AMountedHeroSkipsTheWeaponsForSomeSkills)
{
    {
        const auto tested = AHeroClicking(0x30, Knight);
        tested->Hero().pet.model = 0x1c14;
        const CrtRandom expected = Advanced(tested->random, AimDraws + 3 * EntryDraws + 2 + OwnDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
        EXPECT_EQ(tested->Hero().animation.action, 0x3f);
    }
    {
        // 0x5599f2..0x559ad3: con la 0x2f montado y sin arma derecha, 0x559890 da 0 antes de las opciones.
        const auto tested = AHeroClicking(0x2f, Lord);
        tested->Hero().pet.model = 0x1c37;
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
    }
}

// 0x5599db..0x559afc: las skills especiales del arma y la opción del arma con la skill del lugar cortan con su
// dirección (fase 2c).
TEST(HeroSkillsTest, TheWeaponSkillsThatSendCut)
{
    {
        const auto tested = AHeroClicking(0x31, Knight);
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x559afc")) << result.Error();
    }
    {
        // 0x559a84..0x559a9f: la 0x39 con un arma derecha de 0x212 a 0x411.
        const auto tested = AHeroClicking(0x13, Knight, 30);
        tested->world.Runtime().siegeActive = true;
        tested->world.OwnCharacter().SetSkillAt(0, 0x39);
        tested->Hero().hands[0].model = 0x300;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x559afc")) << result.Error();
    }
    {
        // 0x559ad5..0x559afa: la 0x2b con un arma derecha de 0xc12 a 0xe11 da 0 y sigue a la rama, que con el escudo
        // termina.
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.OwnCharacter().SetSkillAt(0, 0x2b);
        tested->Hero().hands[0].model = 0xd00;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
    }
    {
        // 0x55a340..0x55a362: la segunda mano con la opción de la skill.
        const auto tested = AHeroClicking(0x13, Knight);
        auto& item = tested->world.OwnCharacter().equipment[1];
        item.optionCount = 2;
        item.optionBytes[0] = 0x12;
        item.optionBytes[1] = 0x13;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x55a368")) << result.Error();
    }
    {
        // 0x561f60: la del elfo, sin la lista.
        const auto tested = AHeroClicking(0x13, Elf);
        auto& item = tested->world.OwnCharacter().equipment[0];
        item.optionCount = 1;
        item.optionBytes[0] = 0x13;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x562024")) << result.Error();
    }
}

// El elfo mira las dos manos con 0x561f60 y, sin la opción, va a su rama (0x57d790).
TEST(HeroSkillsTest, AnElfLooksAtBothHandsAndGoesToItsBranch)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x18, Elf, map);
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws);
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x57d790")) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    }
}

// 0x57d51f y 0x57d760..0x57d774: una pared entre el héroe y el blanco deja la skill del arma en 0 y el elegido en −1.
TEST(HeroSkillsTest, AWallBeforeTheTargetClearsTheWeaponSkill)
{
    const auto tested = AHeroClicking(0x13, Knight);
    tested->world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(183, 116)] = 4;
    tested->Movement().weaponSkill = {.active = 1, .slot = 3, .target = 7};
    const CrtRandom expected = Advanced(tested->random, AimDraws + KnightBranchDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    EXPECT_EQ(tested->Movement().weaponSkill.slot, 0u);
    EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
}

// 0x57ceea: sin elegido, el punto del terreno bajo el mouse; si no acierta, el blanco queda como estaba.
TEST(HeroSkillsTest, WithoutAChosenTheSkillAimsAtTheTerrain)
{
    {
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.Runtime().selectedTargets[2] = -1;
        auto context = tested->Context();
        context.pickTerrainPoint = []() -> Client::Core::Expected<std::optional<glm::vec3>, std::string>
        { return std::optional<glm::vec3>(glm::vec3(18399.0f, 11401.0f, 30.0f)); };
        const auto result = UseTheHeroSkill(tested->Hero(), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Movement().targetX, 183u);
        EXPECT_EQ(tested->Movement().targetY, 114u);
        EXPECT_EQ(tested->Hero().attackPosition, glm::vec3(18399.0f, 11401.0f, 30.0f));
        EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
    }
    {
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.Runtime().selectedTargets[2] = -1;
        tested->Movement().targetX = 182;
        tested->Movement().targetY = 115;
        auto context = tested->Context();
        context.pickTerrainPoint = []() -> Client::Core::Expected<std::optional<glm::vec3>, std::string>
        { return std::optional<glm::vec3>(); };
        const auto result = UseTheHeroSkill(tested->Hero(), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Movement().targetX, 182u);
    }
    {
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.Runtime().selectedTargets[2] = -1;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x57ceea")) << result.Error();
    }
}

// 0x5897aa y 0x5808da: las clases 5 a 7 (sin rama) apuntan y siguen solo con las 0x43 a 0x48.
TEST(HeroSkillsTest, TheOtherClassesOnlyGoOnWith43To48)
{
    {
        const auto tested = AHeroClicking(0x13, 5);
        const CrtRandom expected = Advanced(tested->random, AimDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 184u);
    }
    const auto tested = AHeroClicking(0x43, 7);
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5808ef")) << result.Error();
}

// Un Dark Lord con el cuervo, el botón derecho abajo y el lugar `slot` elegido, que guarda lo que manda.
struct RavenHero
{
    explicit RavenHero(std::uint8_t slot, std::int32_t map = 0) : tested(AHeroClicking(0x13, Lord, map))
    {
        tested->Ui().rightDown = true;
        tested->Ui().rightReleased = true;
        tested->Hero().darkSpirit.emplace();
        tested->Hero().skillIndex = slot;
        tested->world.OwnCharacter().SetSkillAt(slot, 0x13);
        tested->world.Actors()[ChosenSlot].key = 0x0a1b;
    }
    Client::Core::Expected<void, std::string> Use()
    {
        auto context = tested->Context();
        context.send = [this](Client::Protocol::Outgoing outgoing) { sent.push_back(std::move(outgoing)); };
        return UseTheHeroSkill(tested->Hero(), context);
    }
    std::unique_ptr<SkillHero> tested;
    std::vector<Client::Protocol::Outgoing> sent;
};

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

// 0x420f70 (0x421599..0x421b20): los lugares 0x78 a 0x7a mandan el A7 con la orden del lugar y la clave 0xffff, por
// send() con el XOR32; el derecho queda suelto y la skill no sigue (ni la entrada del +0x31). En Lorencia y en Devias.
TEST(HeroSkillsTest, TheRavenOrdersSendTheirA7)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t slot : {std::uint8_t{0x78}, std::uint8_t{0x79}, std::uint8_t{0x7a}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(slot);
            RavenHero raven(slot, map);
            raven.tested->Ui().rightHoldFrames = 0;
            const CrtRandom expected = Advanced(raven.tested->random, HeadDraws);
            const auto result = raven.Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(raven.tested->random.State(), expected.State());
            ASSERT_EQ(raven.sent.size(), 1u);
            EXPECT_EQ(raven.sent[0].delivery, Client::Network::Delivery::Clear);
            const std::vector<std::uint8_t> bytes{0xc1, 0x07, 0xa7, 0x00, static_cast<std::uint8_t>(slot - 0x78),
                                                  0xff, 0xff};
            EXPECT_EQ(ClearBytesOf(raven.sent[0]), bytes);
            const auto& ui = raven.tested->Ui();
            EXPECT_FALSE(ui.rightReleased);
            EXPECT_FALSE(ui.rightClick);
            EXPECT_FALSE(ui.rightDown);
            EXPECT_EQ(ui.rightHoldFrames, 0);
            EXPECT_EQ(raven.tested->Movement().followTarget, -1);
            EXPECT_EQ(raven.tested->Movement().targetX, 0u);
        }
    }
}

// 0x420fd6..0x421594: el 0x7b ataca al elegido si 0x558bb0 lo deja y es un monstruo o un jugador: la orden 3 con su
// clave. Si no, nada sale, pero el derecho queda suelto igual y la skill no sigue.
TEST(HeroSkillsTest, TheRavenAttackOrderNeedsAnAttackableChosen)
{
    {
        RavenHero raven(0x7b);
        const auto result = raven.Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(raven.sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(raven.sent[0]), (std::vector<std::uint8_t>{0xc1, 0x07, 0xa7, 0x00, 0x03, 0x0a, 0x1b}));
    }
    for (const std::int32_t chosen : {static_cast<std::int32_t>(ChosenSlot), -1})
    {
        SCOPED_TRACE(chosen);
        RavenHero raven(0x7b);
        raven.tested->world.Actors()[ChosenSlot].byte88 = 4;
        raven.tested->world.Runtime().selectedTargets[2] = chosen;
        const auto result = raven.Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(raven.sent.empty());
        EXPECT_FALSE(raven.tested->Ui().rightDown);
    }
    {
        // Sin la sesión (el socket en −1), tampoco sale y el derecho se suelta.
        const auto tested = AHeroClicking(0x13, Lord);
        tested->Ui().rightDown = true;
        tested->Hero().darkSpirit.emplace();
        tested->Hero().skillIndex = 0x7b;
        tested->world.OwnCharacter().SetSkillAt(0x7b, 0x13);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(tested->Ui().rightDown);
    }
}

// 0x57d29e..0x57d2c3 y 0x420f9a..0x420fc5: sin el cuervo, con 0x4000 en el publicado, con un lugar fuera de 0x78 a 0x7b
// o con solo el clic (ya gastado en 0x57d1b1), no hay orden y la skill sigue a la rama de la clase.
TEST(HeroSkillsTest, WithoutTheRavenOrTheButtonTheSkillGoesOn)
{
    for (int variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        RavenHero raven(variant == 2 ? std::uint8_t{0x7c} : std::uint8_t{0x79});
        if (variant == 0)
            raven.tested->Hero().darkSpirit.reset();
        else if (variant == 1)
            raven.tested->Hero().flags78 = 0x4000;
        else if (variant == 3)
            raven.tested->Ui().rightDown = false;
        raven.tested->Ui().rightReleased = false;
        const auto result = raven.Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(raven.sent.empty());
    }
}
} // namespace
