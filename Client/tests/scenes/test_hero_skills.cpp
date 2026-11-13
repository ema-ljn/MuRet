#include "Scenes/HeroSkills.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
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
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"

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
// Una lectura de +0x38e: un rand() al cerrarse en 0.
constexpr int Byte38eDraws = 1;
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
// La cabeza de la rama del Wizard (0x5897b7..0x58a08a) sin espera: el registro propio y, con él abierto, las entradas
// del costo, de la energía y de la espera.
constexpr int WizardHeadDraws = OwnDraws + 3 * EntryDraws;

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
    std::uint8_t mana = 0;
};

// Skill.bmd con el alcance (+0x26) y el byte de grupo (+0x31) de cada skill de `skills`.
Client::Data::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Data::Tables::SkillRecordSize;
        plain[base + 0x22] = static_cast<std::byte>(invented.mana);
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

// El esqueleto de Player.bmd para cualquier modelo: el 0x4f4a10 de un héroe transformado (0x4ea030) pide sus acciones.
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
                                .playerSkeleton = &skeleton,
                                .models = models,
                                .tickCount = [this] { return ticks; },
                                .worldTime = worldTime,
                                .send = [this](Client::Protocol::Outgoing outgoing)
                                { sent.push_back(std::move(outgoing)); }};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return UseTheHeroSkill(Hero(), Context());
    }

    Client::Scenes::WorldState world;
    CrtRandom random;
    float worldTime = 5000.0f;
    std::uint32_t ticks = 100000;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    // nullptr: el 0x4f4a10 de un héroe transformado corta (0x4ea030).
    const Client::Scenes::IModelLibrary* models = nullptr;
    std::vector<Client::Protocol::Outgoing> sent;
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
                                                          {0x36, 3},
                                                          {0x3c, 3},
                                                          {0x3d, 3, 0, 0x40},
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

// La rama del Wizard apuntó (0x58a118) al elegido del lugar 5, en la celda (184, 117).
void ExpectTheWizardAimed(SkillHero& tested)
{
    EXPECT_EQ(tested.Movement().targetX, 184u);
    EXPECT_EQ(tested.Movement().targetY, 117u);
}

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
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
        // La rama del Wizard la suelta en el mismo paso: el 19 de la 0x3a (0x58a141).
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(std::to_integer<int>(tested->sent[0].packet.Bytes()[2]), 0x19);
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
    ASSERT_TRUE(released.HasValue()) << released.Error();
    EXPECT_EQ(tested->sent.size(), 1u);
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
        ASSERT_TRUE(result.HasValue()) << result.Error();
        if (here)
        {
            ExpectTheWizardAimed(*tested);
        }
        else
        {
            EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);
            EXPECT_EQ(tested->Movement().targetX, 0u);
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
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectTheWizardAimed(*tested);
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
        // Sin elegido, la rama del Wizard apunta al terreno (0x57ceea), que el contexto no trae.
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x57ceea")) << result.Error();
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
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectTheWizardAimed(*tested);
    // 0x58c16e: la 0x2c va por el alcance y pide la skill del arma con el elegido.
    EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
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
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectTheWizardAimed(*tested);
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
        if (tried.goesOn)
        {
            ExpectTheWizardAimed(*tested);
        }
        else
        {
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_EQ(tested->Movement().targetX, 0u);
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
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectTheWizardAimed(*tested);
    }
}

// 0x57d4b6: el Wizard no apunta ni mira las armas: va directo a su rama (HeroSkillsWizard), que apunta después de su
// cabeza; la 0x13 no hace nada más (0x58a13a).
TEST(HeroSkillsTest, TheWizardGoesStraightToItsBranch)
{
    const auto tested = AHeroClicking(0x13, Wizard);
    tested->Movement().targetX = 9;
    const CrtRandom expected = Advanced(tested->random, PartyDraws + WizardHeadDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    ExpectTheWizardAimed(*tested);
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    EXPECT_TRUE(tested->sent.empty());
}

// 0x57ce80 y 0x57d532..0x57d61b: el caballero apunta al elegido (su celda y su posición) y, por cada mano sin la opción
// de la skill, pide la del arma con el lugar y el elegido; el registro abierto en cada mano cierra con 3.848 rand().
// Sin arma derecha, la rama (0x581894) termina sin la 0x13; el Magic Gladiator sigue en la del Wizard (0x5897b7), que
// con la 0x13 no hace nada después de su cabeza.
TEST(HeroSkillsTest, AKnightAimsAtTheChosenAndAsksForTheWeaponSkill)
{
    for (const std::uint8_t heroClass : {Knight, Gladiator, Lord})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(heroClass);
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(0x13, heroClass, map);
            const int wizard = heroClass == Gladiator ? WizardHeadDraws : 0;
            const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws + wizard);
            const auto result = tested->Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
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

// 0x5897a7..0x5897b1: el Magic Gladiator pasa por la rama de las clases 1, 3 y 4 (sin arma derecha, la 0xa no es de su
// lista) y sigue en la del Wizard: Hellfire (0x58c1b6) manda el 1E con su celda lógica (la Y y la X), el rumbo y la
// clave 0xffff, y pone la acción 0x96. El caballero y el Dark Lord terminan en 0x5808da sin nada.
TEST(HeroSkillsTest, TheMagicGladiatorGoesOnThroughTheKnightIntoTheWizardBranch)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0xa, Gladiator, map);
        tested->skills = SkillsWith({{0xa}});
        tested->Hero().movement = 1;
        constexpr int SendDraws = 2;
        // 0x4f3f80 (las velocidades) abre y cierra el registro propio.
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws +
                                                                WizardHeadDraws + 2 * CellDraws + SendDraws + OwnDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x0a, 182, 115, 0, 0, 0, 0xff, 0xff, 0}));
        EXPECT_EQ(tested->Movement().lastShotSkill, 0xau);
        EXPECT_EQ(tested->Hero().animation.action, 0x96);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    for (const std::uint8_t heroClass : {Knight, Lord})
    {
        SCOPED_TRACE(heroClass);
        const auto tested = AHeroClicking(0xa, heroClass);
        tested->skills = SkillsWith({{0xa}});
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
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

// 0x5599db..0x559afc: las skills especiales del arma van a 0x559afc y las opciones del arma a 0x55a368; las que
// llegan a 0x55b6f0 sueltan el brillo 0x5fe (0x55be23).
TEST(HeroSkillsTest, TheWeaponSkillsGoToTheirTails)
{
    {
        const auto tested = AHeroClicking(0x31, Knight);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->world.Particles().All()[0].type, 0x5fe);
    }
    {
        // 0x559a84..0x559a9f: la 0x39 con un arma derecha de 0x212 a 0x411; su alcance de 0 deja al elegido lejos y
        // 0x559510 arma el camino (el caso 5 de la llegada).
        const auto tested = AHeroClicking(0x13, Knight, 30);
        tested->world.Runtime().siegeActive = true;
        tested->world.OwnCharacter().SetSkillAt(0, 0x39);
        tested->Hero().hands[0].model = 0x300;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Hero().movementType, 5);
        EXPECT_EQ(tested->Hero().movement, 1);
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
        // 0x55a340..0x55a362: la segunda mano con la opción de la skill, que 0x559510 lanza sobre el elegido.
        const auto tested = AHeroClicking(0x13, Knight);
        auto& item = tested->world.OwnCharacter().equipment[1];
        item.optionCount = 2;
        item.optionBytes[0] = 0x12;
        item.optionBytes[1] = 0x13;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->world.Particles().All()[0].type, 0x5fe);
        // El D3, el 19 y, con la 0x13, el DF con la celda del blanco.
        ASSERT_EQ(tested->sent.size(), 3u);
        EXPECT_EQ(ClearBytesOf(tested->sent[1])[3], 0x13);
        EXPECT_EQ(ClearBytesOf(tested->sent[2]), (std::vector<std::uint8_t>{0xc1, 0x05, 0xdf, 184, 117}));
    }
    {
        // 0x561f60: la del elfo, sin la lista; fuera de la 0x18 y la 0x36 no tira y sigue a la rama de la clase
        // (0x57d790), que con la 0x13 no manda nada.
        const auto tested = AHeroClicking(0x13, Elf);
        auto& item = tested->world.OwnCharacter().equipment[0];
        item.optionCount = 1;
        item.optionBytes[0] = 0x13;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->sent.empty());
    }
}

// El elfo mira las dos manos con 0x561f60 y, sin la opción, va a su rama (0x57d790): la cabeza (el registro propio y
// las entradas del costo, de la espera y de la energía), el blanco y, en alcance, la skill del arma con el elegido y la
// celda lógica para la línea de vista; la 0x18 no es de las que mandan ahí (HeroSkillsElf, con sus tests).
TEST(HeroSkillsTest, AnElfLooksAtBothHandsAndGoesToItsBranch)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x18, Elf, map);
        const CrtRandom expected =
            Advanced(tested->random, AimDraws + 2 * OwnDraws + OwnDraws + 3 * EntryDraws + 2 * CellDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
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

// 0x5897aa y 0x5808da: las clases 5 a 7 (sin rama) apuntan y siguen solo con las 0x43 a 0x48 (HeroSkillsTail): la
// 0x43, con el registro propio, las entradas del costo, la espera y la energía, la celda dos veces y el 1E.
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
    constexpr int SendDraws = 2;
    const CrtRandom expected =
        Advanced(tested->random, AimDraws + OwnDraws + 3 * EntryDraws + 4 * CellDraws + SendDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(ClearBytesOf(tested->sent[0])[2], 0x1e);
    EXPECT_EQ(ClearBytesOf(tested->sent[0])[3], 0x43);
    EXPECT_EQ(tested->Hero().animation.action, 0x3f);
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

// Lo que gasta 0x559afc hasta 0x55b6f0 con la 0x3c en alcance: la cabeza y el blanco (AimDraws), el costo (0x559b0b) y
// la espera (0x552c50) con sus entradas, las celdas del héroe publicado del D3, los dos del C3 19, la entrada de
// 0x559510 al cerrarse y el registro propio de la mano.
constexpr int ListedCastDraws = AimDraws + 2 * EntryDraws + 2 * CellDraws + 2 + EntryDraws + OwnDraws;

// 0x559afc → 0x559510 → 0x55b6f0 con la 0x3c (sin la partícula 0x5fe): el D3 de girar con la celda del héroe y su
// rumbo, el C3 19 con la clave del elegido, el blanco en +0x3a8, el rumbo hacia él y +0x381; el DF no sale con la 0x3c.
// En Lorencia y en Devias.
TEST(HeroSkillsTest, AListedWeaponSkillCastsAtTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x3c, Knight, map);
        tested->world.Actors()[ChosenSlot].key = 0x0a1b;
        const CrtRandom expected = Advanced(tested->random, ListedCastDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Clear);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
        EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(ClearBytesOf(tested->sent[1]), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x3c, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().attackPosition, tested->world.Actors()[ChosenSlot].position);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Hero().byte381, 1);
        EXPECT_EQ(tested->Movement().weaponSkill.slot, 0x3cu);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Movement().lastActionTick, tested->ticks);
    }
}

// 0x55be0d..0x55be49: la 0x31 (en Lorencia, Devias y Noria, el mapa 8 de la acción 0x41) suelta el brillo 0x5fe sobre
// el héroe, con su dueño en el pool y en el hueso de la mano derecha (sin dibujo, la matriz en cero: el origen del
// modelo, que pasa a ser la posición del héroe), y el sonido rand() % 2 + 0x3c, su único rand(); después, el 19 y
// ningún DF.
TEST(HeroSkillsTest, AWeaponSkillShinesOnTheHero)
{
    for (const std::int32_t map : {0, 2, 8})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x31, Knight, map);
        tested->world.Actors()[ChosenSlot].key = 0x0c2d;
        CrtRandom expected = Advanced(tested->random, ListedCastDraws - 2);
        const int sound = expected.Next() % 2 + 0x3c;
        expected = Advanced(expected, 2);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().lastSoundId, sound);
        const auto& shine = tested->world.Particles().All()[0];
        EXPECT_TRUE(shine.alive);
        EXPECT_EQ(shine.type, 0x5fe);
        EXPECT_EQ(shine.life, 18);
        EXPECT_EQ(shine.typedOwner.kind, Client::World::ParticleOwnerKind::Actor);
        EXPECT_EQ(shine.typedOwner.identity, 0u);
        EXPECT_EQ(shine.position, tested->Hero().position);
        EXPECT_EQ(tested->world.PlayerModel().origin, tested->Hero().position);
        EXPECT_FALSE(tested->world.Particles().All()[1].alive);
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(ClearBytesOf(tested->sent[1]), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x31, 0x0c, 0x2d}));
        EXPECT_EQ(tested->Hero().animation.action, map == 8 ? 0x41 : 0x40);
    }
}

// 0x55bf0c..0x55bf29: dentro de los 300 ms del último envío el 19 no sale, pero sí el D3 y lo demás; con "webzen" en el
// nombre del héroe publicado, tampoco (0x55be9a).
TEST(HeroSkillsTest, TheBrakeAndTheNameHoldTheMagicAttack)
{
    {
        const auto tested = AHeroClicking(0x3c, Knight);
        tested->Movement().lastActionTick = tested->ticks - 300;
        const CrtRandom expected = Advanced(tested->random, ListedCastDraws - 2);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0])[2], 0xd3);
        EXPECT_EQ(tested->Hero().byte381, 1);
    }
    {
        const auto tested = AHeroClicking(0x3c, Knight, 2);
        const char name[] = "xwebzenx";
        for (std::size_t i = 0; i + 1 < sizeof(name); ++i)
            tested->Hero().name[i] = static_cast<std::byte>(name[i]);
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Movement().lastActionTick, 0u);
    }
}

// 0x559b29..0x559b3b: sin maná y sin poción de maná, 0x559890 da 0 y la skill sigue a la rama de la clase; con una
// poción, 0x5af520 y el C1 26 cortan (hito 5). Nada sale.
TEST(HeroSkillsTest, WithoutManaTheListedSkillLooksForAPotion)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x3d, Knight, map);
        // La rama del caballero (0x581894) termina: sin arma derecha, la 0x3d no sigue.
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->sent.empty());
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        tested->Ui().rightClick = true;
        const auto again = tested->Use();
        ASSERT_FALSE(again.HasValue());
        EXPECT_TRUE(Contains(again.Error(), "0x559b41")) << again.Error();
        EXPECT_TRUE(Contains(again.Error(), "hito 5")) << again.Error();
    }
    // Con el maná del registro propio, la 0x3d llega al 19.
    const auto tested = AHeroClicking(0x3d, Knight);
    tested->world.OwnCharacter().mana = 0x40;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(ClearBytesOf(tested->sent[1])[3], 0x3d);
}

// 0x5596f2..0x559822: fuera de alcance, el camino de PathFinding2 hasta el alcance desde las celdas lógicas del héroe
// (la Y antes que la X), +0x378 en 1 y +0x379 en 5; no sale nada. En Blood Castle (0x55969e) las 0x13 a 0x17 tienen
// 1,8 de alcance.
TEST(HeroSkillsTest, AFarChosenGetsAPathForTheSkill)
{
    {
        const auto tested = AHeroClicking(0x3c, Knight, 2);
        tested->world.Actors()[ChosenSlot].position = {18950.0f, 11550.0f, 0.0f};
        const CrtRandom expected =
            Advanced(tested->random, AimDraws + 2 * EntryDraws + 2 * CellDraws + EntryDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 5);
        EXPECT_GT(tested->Hero().path.count, 0);
    }
    {
        // La 0x13 de una opción del arma: 4 · 1,2 en Lorencia la deja en alcance (con el brillo y el 19), y 1,8 en
        // Blood Castle no llega a los 283 del elegido.
        for (const std::int32_t map : {0, 11})
        {
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(0x13, Knight, map);
            auto& item = tested->world.OwnCharacter().equipment[0];
            item.optionCount = 1;
            item.optionBytes[0] = 0x13;
            const auto result = tested->Use();
            if (map == 0)
            {
                ASSERT_TRUE(result.HasValue()) << result.Error();
                EXPECT_EQ(tested->Hero().movementType, 0);
                EXPECT_EQ(tested->world.Particles().All()[0].type, 0x5fe);
                continue;
            }
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->Hero().movementType, 5);
        }
    }
}

// 0x55abfa..0x55b45a: la opción 0x12 del arma pone la acción 0x98 y manda el C3 19 con la clave del héroe publicado; si
// 0x558bb0 no deja atacar al elegido, la 0x3c de la lista también va a las opciones (0x55a2c0).
TEST(HeroSkillsTest, TheOption12CastsOnTheHeroItself)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x12, Knight, map);
        tested->Hero().key = 0x0123;
        auto& item = tested->world.OwnCharacter().equipment[0];
        item.optionCount = 1;
        item.optionBytes[0] = 0x12;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->Hero().animation.action, 0x98);
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x12, 0x01, 0x23}));
    }
    const auto tested = AHeroClicking(0x3c, Knight);
    tested->world.Actors()[ChosenSlot].byte88 = 0;
    // Después, la rama del caballero (0x581894), que sin arma derecha termina sin la 0x3c.
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested->sent.empty());
}

namespace
{
// Un elfo con la opción `skill` en el arma de la derecha, el elegido de 0x7758588 en ChosenSlot y su clave.
std::unique_ptr<SkillHero> AnElfWithTheOption(std::uint8_t skill, std::int32_t map = 0)
{
    auto tested = AHeroClicking(skill, Elf, map);
    auto& item = tested->world.OwnCharacter().equipment[0];
    item.optionCount = 1;
    item.optionBytes[0] = skill;
    tested->Movement().weaponSkill.target = static_cast<std::int32_t>(ChosenSlot);
    auto& chosen = tested->world.Actors()[ChosenSlot];
    chosen.key = 0x0a1b;
    chosen.byte370 = 2;
    return tested;
}

// El costo, la espera y el alcance (una entrada cada uno), la celda lógica del 1E y el C3; el registro de la mano.
constexpr int ElfShotDraws = AimDraws + 3 * EntryDraws + 2 * CellDraws + 2 + OwnDraws;
} // namespace

// 0x563df6..0x564f37: la 0x18 en alcance gira al héroe hacia +0x3a8 y manda el 1E con la celda lógica, el rumbo ·
// 256/360, dos ceros y la clave de 0x558eb0 con 0x7758588; 0x5767948 queda en la skill y la usó (0x57df46).
TEST(HeroSkillsTest, TheElfTripleShotSendsA1E)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElfWithTheOption(0x18, map);
        const CrtRandom expected = Advanced(tested->random, ElfShotDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x18, 182, 115, 0x60, 0, 0, 0x0a, 0x1b, 0}));
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Movement().lastShotSkill, 0x18u);
    }
}

// 0x56291f..0x563b8f: la 0x36 suma la celda lógica hacia el blanco (2 y 2: 0xaa) y la espalda (315 · 255/360); el
// rumbo es · 255/360. Sin elegido en 0x7758588, la clave es 0xffff.
TEST(HeroSkillsTest, TheElfPenetrationSendsWhereTheTargetIs)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElfWithTheOption(0x36, map);
        tested->Movement().weaponSkill.target = -1;
        const CrtRandom expected = Advanced(tested->random, ElfShotDraws + 2 * CellDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x36, 182, 115, 0x5f, 0xaa, 0xdf, 0xff, 0xff, 0}));
    }
}

// 0x562a8a y 0x563eae: con "webzen" en el nombre no sale el 1E (y no hay freno), pero la usó; fuera de alcance
// (0x56298f, 0x563e66), ni el 1E ni el rumbo, y sigue a la rama de la clase, que con un monstruo no camina.
TEST(HeroSkillsTest, TheElfShotNeedsTheNameAndTheReach)
{
    {
        const auto tested = AnElfWithTheOption(0x18);
        const char name[] = "webzen";
        for (std::size_t i = 0; i + 1 < sizeof(name); ++i)
            tested->Hero().name[i] = static_cast<std::byte>(name[i]);
        const CrtRandom expected = Advanced(tested->random, ElfShotDraws - 2 * CellDraws - 2);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().lastShotSkill, 0u);
    }
    {
        const auto tested = AnElfWithTheOption(0x36, 2);
        tested->world.Actors()[ChosenSlot].position = {18850.0f, 11550.0f, 0.0f};
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// 0x563b8f..0x564f37 con un héroe transformado (no es el 0x20a; +0x88 en 0): 0x4f4a10 por su modelo (la acción 1, el
// sonido 0x10 del 0x28 y el rand() del sonido del modelo), +0x38e (un rand() al cerrarlo; el registro propio sigue
// abierto desde 0x561f60) y la flecha de 0x53e2b0(c, c, 0, lugar, bandera, 0) (P-C269 c): con la 0x18 la bandera en 1,
// tres tiros de 0x53d9a0 y la cuenta de la tanda; con la 0x36, la 2 (0x563df1) y un tiro. Los tiros dejan [0x5767948]
// en la skill 0; sin arco en el registro propio, sin flecha. Sin la biblioteca de modelos, el 0x4f4a10 corta
// (0x4ea030).
TEST(HeroSkillsTest, TheTransformedElfShootsThroughTheArrowOf53e2b0)
{
    struct Case
    {
        std::uint8_t skill;
        int extraCells;
        std::uint8_t volleys;
    };
    constexpr std::uint16_t TransformedModel = 0x28;
    // 0x4f51f1: el rand() % 2 del sonido de ataque del modelo (DrawModelSound), que el 0x20a no saca.
    constexpr int ModelSoundDraws = 1;
    for (const std::int32_t map : {0, 2})
    {
        for (const Case& probe : {Case{0x18, 0, 1}, Case{0x36, 2, 0}})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", skill " << static_cast<int>(probe.skill));
            const AnyModels library;
            const auto tested = AnElfWithTheOption(probe.skill, map);
            tested->models = &library;
            tested->Hero().type = TransformedModel;
            if (probe.skill == 0x36)
                tested->Movement().weaponSkill.target = -1;
            const CrtRandom expected = Advanced(tested->random, ElfShotDraws + (probe.extraCells * CellDraws) +
                                                                    ModelSoundDraws + Byte38eDraws);

            const auto result = tested->Use();

            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Hero().animation.action, 1);
            EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x10);
            EXPECT_EQ(tested->Movement().lastShotSkill, 0u);
            EXPECT_EQ(tested->world.OwnCharacter().bytef04, probe.volleys);
            // Los tres tiros de la bandera en 1 giran el rumbo 15, −30 y 15: vuelve al de 0x5629b6.
            EXPECT_EQ(tested->Hero().angles.z, 135.0f);
        }
    }
    const auto cut = AnElfWithTheOption(0x36);
    cut->Hero().type = TransformedModel;
    const auto result = cut->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x4ea030")) << result.Error();
}

// 0x56205f..0x562092: sin maná y sin poción, la opción que sigue (y la rama de la clase, que sin maná ni poción no
// sigue, 0x57d821); con una, 0x5af520 (hito 5).
TEST(HeroSkillsTest, TheElfShotWithoutMana)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElfWithTheOption(0x18, map);
        tested->skills = SkillsWith({{0x18, 6, 0, 0x10}});
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        auto& potion = tested->world.Inventory().cells[30];
        potion.id = 0x1c04;
        potion.occupied = 1;
        tested->Ui().rightClick = true;
        const auto again = tested->Use();
        ASSERT_FALSE(again.HasValue());
        EXPECT_TRUE(Contains(again.Error(), "0x562075")) << again.Error();
        EXPECT_TRUE(tested->sent.empty());
    }
}
