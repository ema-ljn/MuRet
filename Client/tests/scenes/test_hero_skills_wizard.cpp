#include "Scenes/HeroSkillsWizard.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "GameLogic/Path.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill55cbc0.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/OriginalFtol.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// La rama del Wizard de 0x57cfc0 (0x5897aa..0x599d5d, CL9 hito 4, fase 2c) y 0x55cbc0, llamadas directo: el héroe en
// el lugar 0 en (182, 115), un monstruo elegido en el lugar 5 en (184, 117) con la clave 0x0a1b y un Skill.bmd
// inventado. Cada envío con sus bytes y cada camino con sus rand().
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// La cabeza (0x5897b7..0x58a08a) sin espera: el registro propio y, con él abierto, el costo, la energía y la espera.
constexpr int HeadDraws = OwnDraws + 3 * EntryDraws;
// La celda lógica del héroe: la Y y la X.
constexpr int HeroCellDraws = 2 * CellDraws;
// 0x4f5290 sobre un jugador sin montura: 0x4f3f80 (el registro propio) y el rand() de la acción.
constexpr int HitDraws = OwnDraws + 1;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint32_t Ticks = 100000;

enum HeroClass : std::uint8_t
{
    Wizard = 0,
    Knight = 1,
    Elf = 2,
    Gladiator = 3,
    Lord = 4,
};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

struct InventedSkill
{
    std::uint8_t skill = 0;
    std::uint8_t mana = 0;
    std::uint8_t stamina = 0;
    std::uint8_t level = 0;
    std::uint8_t energy = 0;
    std::uint16_t delay = 0;
};

// Skill.bmd con el maná (+0x22), la stamina (+0x24), la espera (+0x28), el nivel (+0x20) y la energía (+0x2c).
Client::Data::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Data::Tables::SkillRecordSize;
        plain[base + 0x20] = static_cast<std::byte>(invented.level);
        plain[base + 0x22] = static_cast<std::byte>(invented.mana);
        plain[base + 0x24] = static_cast<std::byte>(invented.stamina);
        plain[base + 0x28] = static_cast<std::byte>(invented.delay & 0xff);
        plain[base + 0x29] = static_cast<std::byte>(invented.delay >> 8);
        plain[base + 0x2c] = static_cast<std::byte>(invented.energy);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

struct WizardHero
{
    explicit WizardHero(std::uint8_t used, std::uint8_t heroClass = Wizard, std::int32_t map = 0) : skill(used)
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
        world.OwnCharacter().SetSkillAt(0, used);
        auto& chosen = Chosen();
        chosen.alive = true;
        chosen.type = 0x20;
        chosen.byte88 = 2;  // un monstruo: 0x558bb0 lo deja atacar
        chosen.byte370 = 2; // y 0x558eb0 da su clave
        chosen.key = ChosenKey;
        chosen.alpha = 1.0f;
        chosen.position = {18450.0f, 11750.0f, 0.0f};
        runtime.selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
        runtime.movement.selectedSkillRange = 4.0f;
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Scenes::ActorState& Chosen()
    {
        return world.Actors()[ChosenSlot];
    }
    Client::Scenes::GameMovementState& Movement()
    {
        return world.Runtime().movement;
    }
    // Un aliado: un jugador del lugar 5 que 0x558bb0 no deja atacar.
    void ChooseAnAlly()
    {
        auto& chosen = Chosen();
        chosen.type = Client::Data::Models::PlayerSkeletonModel;
        chosen.byte88 = 1;
        chosen.byte370 = 0;
        chosen.hands[0].model = chosen.hands[1].model = Client::Scenes::NoItemModel;
    }
    // Sin elegido: 0x57ce80 apunta al terreno, a (186, 119).
    void AimAtTheTerrain()
    {
        world.Runtime().selectedTargets[2] = -1;
        terrainPoint = glm::vec3{18650.0f, 11950.0f, 0.0f};
    }
    HeroSkillContext Context()
    {
        HeroSkillContext context{.world = world,
                                 .random = random,
                                 .skills = &skills,
                                 .playerSkeleton = &skeleton,
                                 .tickCount = [this] { return ticks; },
                                 .worldTime = 5000.0f,
                                 .send = [this](Client::Protocol::Outgoing outgoing)
                                 { sent.push_back(std::move(outgoing)); },
                                 .playerKey = PlayerKey};
        if (terrainPoint)
            context.pickTerrainPoint = [this]() -> Client::Core::Expected<std::optional<glm::vec3>, std::string>
            { return terrainPoint; };
        if (!withTicks)
            context.tickCount = {};
        return context;
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return Client::Scenes::UseTheWizardSkill(Hero(), skill, Context());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        return ClearBytesOf(sent.at(index));
    }

    std::uint8_t skill;
    Client::Scenes::WorldState world;
    CrtRandom random;
    std::uint32_t ticks = Ticks;
    bool withTicks = true;
    std::optional<glm::vec3> terrainPoint;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Data::Tables::SkillTable skills =
        SkillsWith({{1},    {5},    {6},    {8},    {9},    {0xa},  {0xc},  {0xd},  {0xe},  {0xf},
                    {0x10}, {0x13}, {0x26}, {0x27}, {0x28}, {0x2f}, {0x3a}, {0x43}, {0x49}, {0x4c}});
};

std::unique_ptr<WizardHero> AWizard(std::uint8_t skill, std::uint8_t heroClass = Wizard, std::int32_t map = 0)
{
    return std::make_unique<WizardHero>(skill, heroClass, map);
}

// 0x5897a7..0x5897b1: otra clase que 0 y 3 va directo a 0x5808da, sin rand(); ahí, fuera de 0x43 a 0x48, nada, y con
// ellas la cola (HeroSkillsTail).
TEST(HeroSkillsWizardTest, TheOtherClassesGoStraightToTheSharedTail)
{
    for (const std::uint8_t heroClass : {Knight, Elf, Lord})
    {
        SCOPED_TRACE(heroClass);
        {
            const auto tested = AWizard(0x13, heroClass);
            const CrtRandom expected = tested->random;
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_EQ(tested->Movement().targetX, 0u);
        }
        const auto tested = AWizard(0x44, heroClass);
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x44, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, 0x87);
    }
}

// 0x589885..0x5898a5: la energía de 0x551f40 (nivel · energía · 4 / 100 + 20) contra +0x1e + +0x30 del registro.
TEST(HeroSkillsWizardTest, TheEnergyStopsTheHead)
{
    const auto tested = AWizard(0x13);
    tested->skills = SkillsWith({{0x13, 0, 0, 10, 5}});
    const CrtRandom expected = Advanced(tested->random, OwnDraws + 2 * EntryDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().targetX, 0u);

    // 10 · 5 · 4 / 100 + 20 = 22.
    tested->world.OwnCharacter().energy = 12;
    tested->world.OwnCharacter().word30 = 10;
    const CrtRandom passed = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), passed.State());
    EXPECT_EQ(tested->Movement().targetX, 184u);
    EXPECT_EQ(tested->Movement().targetY, 117u);
}

// 0x58990f..0x589936: sin el maná, la poción de maná (sin ella, nada; con ella, el hito 5).
TEST(HeroSkillsWizardTest, WithoutManaTheHeadLooksForAPotion)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x13, Wizard, map);
        tested->skills = SkillsWith({{0x13, 5}});
        const CrtRandom expected = Advanced(tested->random, OwnDraws + 2 * EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 0u);
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        const auto again = tested->Use();
        ASSERT_FALSE(again.HasValue());
        EXPECT_TRUE(Contains(again.Error(), "0x589936")) << again.Error();
        tested->world.OwnCharacter().mana = 5;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->Movement().targetX, 184u);
    }
}

// 0x58a036..0x58a06f: sin la stamina, nada; la 0x3a suelta además el derecho. La 0x28 no pide stamina, y la 0x3a cuesta
// lo de la 0x28.
TEST(HeroSkillsWizardTest, WithoutStaminaTheChargeLetsTheRightButtonGo)
{
    for (const std::uint8_t skill : {std::uint8_t{0x3a}, std::uint8_t{0x13}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AWizard(skill);
        tested->skills = SkillsWith({{0x28, 0, 5}, {0x13, 0, 5}, {0x3a}});
        auto& ui = tested->world.Runtime().ui;
        ui.rightReleased = ui.rightClick = ui.rightDown = true;
        ui.rightHoldFrames = 3;
        const CrtRandom expected = Advanced(tested->random, OwnDraws + 2 * EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        const bool charged = skill == 0x3a;
        EXPECT_EQ(ui.rightReleased, !charged);
        EXPECT_EQ(ui.rightClick, !charged);
        EXPECT_EQ(ui.rightDown, !charged);
        EXPECT_EQ(ui.rightHoldFrames, charged ? 0 : 3);
    }
    const auto tested = AWizard(0x28);
    tested->skills = SkillsWith({{0x28, 0, 5}});
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 1u);
}

// 0x58a071..0x58a08a: con la espera del lugar corriendo (0x552c50), nada; sin ella, la pone.
TEST(HeroSkillsWizardTest, ADelayedSlotStopsTheHead)
{
    const auto tested = AWizard(0x13);
    tested->skills = SkillsWith({{0x13, 0, 0, 0, 0, 1000}});
    tested->world.OwnCharacter().skillDelays[0] = 500;
    const CrtRandom expected = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().targetX, 0u);

    tested->world.OwnCharacter().skillDelays[0] = 0;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->world.OwnCharacter().skillDelays[0], 1000);
    EXPECT_EQ(tested->Movement().targetX, 184u);
}

// 0x58a141..0x58a899: la 0x3a soltada manda el 19 con la clave del jugador (el freno de 300 ms), y siempre las
// velocidades, la acción 0x44 y +0x378 en 0. Solo un jugador; el Magic Gladiator igual.
TEST(HeroSkillsWizardTest, TheReleasedChargeSendsTheMagicAttackWithThePlayerKey)
{
    for (const std::uint8_t heroClass : {Wizard, Gladiator})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(heroClass);
            SCOPED_TRACE(map);
            const auto tested = AWizard(0x3a, heroClass, map);
            tested->Hero().movement = 1;
            const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + OwnDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x3a, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 0x44);
            EXPECT_EQ(tested->Hero().movement, 0);
            EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
        }
    }
    {
        // Con el freno, sin el 19.
        const auto tested = AWizard(0x3a);
        tested->Movement().lastActionTick = Ticks - 300;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 0x44);
    }
    {
        // Con "webzen" en el nombre del héroe publicado, sin el 19 ni el freno.
        const auto tested = AWizard(0x3a);
        const std::string name = "awebzen";
        for (std::size_t i = 0; i < name.size(); ++i)
            tested->Hero().name[i] = static_cast<std::byte>(name[i]);
        const CrtRandom expected = Advanced(tested->random, HeadDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().lastActionTick, 0u);
    }
    {
        // Otro tipo, nada después de la cabeza.
        const auto tested = AWizard(0x3a);
        tested->Hero().type = 0x20;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
    {
        // Sin GetTickCount, el freno corta.
        const auto tested = AWizard(0x3a);
        tested->withTicks = false;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x58a1be")) << result.Error();
    }
}

// 0x58a89a..0x58b025: la 0x28 manda el 19 con la clave de 0x558eb0 o, sin ella, la del jugador; el freno no la frena.
TEST(HeroSkillsWizardTest, TheChargedShotUsesTheChosenKeyOrThePlayerKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x28, Wizard, map);
        tested->Movement().lastActionTick = Ticks - 10;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x28, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, 0x45);
        EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
    }
    const auto tested = AWizard(0x28);
    tested->Chosen().byte370 = 0;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x28, 0x12, 0x34}));
}

// 0x58c1b6.. (Hellfire, 0xa) y 0x58cf37.. (Inferno, 0xe): el 1E con la celda del héroe, su rumbo y la clave 0xffff;
// después, las velocidades, la acción (0x96 y 0x95) y +0x378 en 0. Solo un jugador.
TEST(HeroSkillsWizardTest, HellfireAndInfernoSendTheirCellAndDirection)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint8_t action;
    };
    for (const Case& tried : {Case{0xa, 0x96}, Case{0xe, 0x95}})
    {
        for (const std::uint8_t heroClass : {Wizard, Gladiator})
        {
            for (const std::int32_t map : {0, 2})
            {
                SCOPED_TRACE(tried.skill);
                SCOPED_TRACE(heroClass);
                SCOPED_TRACE(map);
                const auto tested = AWizard(tried.skill, heroClass, map);
                tested->Hero().angles.z = 90.0f;
                tested->Hero().movement = 1;
                const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + SendDraws + OwnDraws);
                ASSERT_TRUE(tested->Use().HasValue());
                EXPECT_EQ(tested->random.State(), expected.State());
                ASSERT_EQ(tested->sent.size(), 1u);
                EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
                EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, tried.skill, 182, 115, 0x40, 0,
                                                                      0, 0xff, 0xff, 0}));
                EXPECT_EQ(tested->Movement().lastShotSkill, std::uint32_t{tried.skill});
                EXPECT_EQ(tested->Hero().animation.action, tried.action);
                EXPECT_EQ(tested->Hero().movement, 0);
            }
        }
    }
    const auto tested = AWizard(0xa);
    tested->Hero().type = 0x20;
    const CrtRandom expected = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
}

// 0x58c15e..0x58eecc: en alcance y con el elegido atacable, la skill del arma y 0x55cbc0; la 8 y la 9 mandan el 1E con
// la celda del héroe y la clave mágica (0x77f3bad, que da la vuelta de 0x32 a 1, en +0x1bd), y 0x4f5290.
TEST(HeroSkillsWizardTest, TheMagicKeySkillsCountTheirKey)
{
    for (const std::uint8_t skill : {std::uint8_t{8}, std::uint8_t{9}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AWizard(skill, Wizard, map);
            const CrtRandom expected =
                Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + HeroCellDraws + SendDraws + HitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            const auto& weapon = tested->Movement().weaponSkill;
            EXPECT_EQ(weapon.active, 1u);
            EXPECT_EQ(weapon.slot, 0u);
            EXPECT_EQ(weapon.target, static_cast<std::int32_t>(ChosenSlot));
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0),
                      (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, skill, 182, 115, 0x60, 0, 0, 0xff, 0xff, 1}));
            EXPECT_EQ(tested->Hero().magicKey, 1);
            const std::uint8_t action = tested->Hero().animation.action;
            EXPECT_TRUE(action == 0x8e || action == 0x8f) << int{action};
        }
    }
    const auto tested = AWizard(8);
    tested->Movement().magicKeyCounter = 0x32;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[11], 1);
    EXPECT_EQ(tested->Movement().magicKeyCounter, 1);
}

// 0x58fced..0x590b17: Aqua Beam como Hellfire, con la acción 0x94 y 0x77f3ba4 en 0.
TEST(HeroSkillsWizardTest, AquaBeamCastsAroundTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0xc, Wizard, map);
        tested->Movement().dword77f3ba4 = 7;
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + HeroCellDraws + SendDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0xc, 182, 115, 0x60, 0, 0, 0xff, 0xff, 0}));
        EXPECT_EQ(tested->Hero().animation.action, 0x94);
        EXPECT_EQ(tested->Movement().dword77f3ba4, 0);
    }
}

// 0x590b18..0x591858 (Flame, 5) y 0x591859..0x5925ad (0x26 y 0x27): el 1E con la celda de +0x3a8, que 0x55cbc0 dejó en
// la del elegido; la 0x26 y la 0x27 con la clave del blanco de la skill del arma.
TEST(HeroSkillsWizardTest, FlameAndTheWeaponTargetSkillsCastAtTheTargetCell)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint8_t keyHigh;
        std::uint8_t keyLow;
    };
    for (const Case& tried : {Case{5, 0xff, 0xff}, Case{0x26, 0x0a, 0x1b}, Case{0x27, 0x0a, 0x1b}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(tried.skill);
            SCOPED_TRACE(map);
            const auto tested = AWizard(tried.skill, Wizard, map);
            tested->Hero().movement = 1;
            const CrtRandom expected =
                Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + SendDraws + HitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, tried.skill, 184, 117, 0x60, 0, 0,
                                                                  tried.keyHigh, tried.keyLow, 0}));
            EXPECT_EQ(tested->Movement().lastShotSkill, std::uint32_t{tried.skill});
            EXPECT_EQ(tested->Hero().movement, tried.skill == 5 ? 1 : 0);
        }
    }
}

// 0x58dcc4..0x58eca3: la 0x4c en alcance manda el 1E con la posición relativa (0x4e9920), la clave del elegido y la
// clave mágica, después de la espera (0x552990); +0x434 queda en el elegido y la acción es la de las manos (0x4e9c50).
TEST(HeroSkillsWizardTest, TheSkill4cCastsWithThePositionAndTheMagicKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x4c, Wizard, map);
        tested->Hero().movement = 1;
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + OwnDraws + EntryDraws + HeroCellDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x4c, 182, 115, 0x60, 0, 0xaa, 0x0a, 0x1b, 1}));
        EXPECT_EQ(tested->Hero().skillTarget4c, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Hero().animation.action, 0x62);
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    }
    {
        // Fuera de alcance, el camino hasta el alcance · 1,2, sin +0x379.
        const auto tested = AWizard(0x4c);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 0);
        EXPECT_GT(tested->Hero().path.count, 0);
    }
    {
        // Sin elegido y fuera de alcance, 0x6c8e38 en −1.
        const auto tested = AWizard(0x4c);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 1.0f;
        tested->world.Runtime().selectedTargets[4] = 2;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// 0x58ecff..0x58edff: fuera de alcance con un elegido atacable, el camino hasta el alcance con +0x379 en 5; salvo las
// 0x43 a 0x47 y la 0x4c, nada más.
TEST(HeroSkillsWizardTest, OutOfReachTheSkillWalksToTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(1, Wizard, map);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 5);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
    }
    {
        // Un elegido que no se puede atacar: ni camino ni blanco de la skill del arma.
        const auto tested = AWizard(1);
        tested->ChooseAnAlly();
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
    }
    {
        // La 0x48 camina y termina ahí.
        const auto tested = AWizard(0x48, Wizard, 2);
        tested->Movement().selectedSkillRange = 1.0f;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
    }
    // La 0x43 sigue fuera de alcance, después del camino, hasta la cola (0x5808da, HeroSkillsTail): el 1E y +0x378 en
    // 0, que deja el camino armado sin andar.
    const auto tested = AWizard(0x43);
    tested->Movement().selectedSkillRange = 1.0f;
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_FALSE(tested->sent.empty());
    const std::vector<std::uint8_t> last = tested->Sent(tested->sent.size() - 1);
    EXPECT_EQ(last[2], 0x1e);
    EXPECT_EQ(last[3], 0x43);
    EXPECT_EQ(tested->Hero().movement, 0);
    EXPECT_EQ(tested->Hero().movementType, 5);
    EXPECT_GT(tested->Hero().path.count, 0);
}

// P-C256: fuera de alcance, un elegido con la celda fuera de 0..255 (que PathFinding2 toma como índice
// lineal) no lleva camino: después de las lecturas de la celda del héroe, el camino se descarta sin cortar, en la 0x4c
// (0x58ec65) y en las demás (0x58edc6). La X a la izquierda del mapa o la Y arriba de 255.
TEST(HeroSkillsWizardTest, AWalkToACellOffTheGridIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x4c}, std::uint8_t{1}})
        {
            for (const glm::vec3 position : {glm::vec3{-1000.0f, 11750.0f, 0.0f}, glm::vec3{18450.0f, 30000.0f, 0.0f}})
            {
                SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill) + " " + std::to_string(position.x));
                const auto tested = AWizard(skill, Wizard, map);
                tested->Chosen().position = position;
                const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
                const auto result = tested->Use();
                ASSERT_TRUE(result.HasValue()) << result.Error();
                EXPECT_EQ(tested->random.State(), expected.State());
                EXPECT_TRUE(tested->sent.empty());
                EXPECT_EQ(tested->Hero().movement, 0);
                EXPECT_EQ(tested->Hero().movementType, 0);
                EXPECT_EQ(tested->Hero().path.count, 0);
            }
        }
    }
    // La 0x43 sigue a la línea de vista (0x58eea8), que con la Y en 300 sale de TerrainWall (0x55886d): MuRet la toma
    // como sin línea y la skill sigue sin cortar.
    const auto tested = AWizard(0x43, Wizard, 2);
    tested->Chosen().position = glm::vec3{18450.0f, 30000.0f, 0.0f};
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->Hero().path.count, 0);
}

// 0x55cbc0 desde 0x58eecc: la 1 (de la lista del 19) pega al elegido con el 19, 0x4f5290 y el D3 de girar con la celda
// lógica del héroe publicado; después la rama sigue hasta 0x5808da, que con la 1 no hace nada.
TEST(HeroSkillsWizardTest, InReachTheChosenTakesTheWeaponSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(1, Wizard, map);
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x01, 0x0a, 0x1b}));
        EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Clear);
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x40}));
        EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
    }
    // Con el freno, solo el D3.
    const auto tested = AWizard(1);
    tested->Movement().lastActionTick = Ticks;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[2], 0xd3);
}

// 0x55e67e..0x55f7dd: la 0xd manda el 1E con la celda de +0x3a8 de p1, el rumbo de p2 y la clave del blanco.
TEST(HeroSkillsWizardTest, InReachTheSkill0dSendsItsDurationAtTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0xd, Wizard, map);
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0xd, 184, 117, 0x60, 0, 0, 0x0a, 0x1b, 0}));
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x40}));
        EXPECT_EQ(tested->Movement().lastShotSkill, 0xdu);
    }
}

// 0x55cc9a..0x55cccc y 0x55da5b..: la 0x49 pide la derecha del héroe publicado de 0xc12 a 0xe11; con ella, el 19, la
// acción 0x86 y el D3.
TEST(HeroSkillsWizardTest, TheThrustNeedsItsWeapon)
{
    for (const auto& [map, right] : {std::pair{0, std::uint16_t{0xc12}}, std::pair{2, std::uint16_t{0xc12}},
                                     std::pair{0, std::uint16_t{0xe11}}, std::pair{2, std::uint16_t{0xe12}}})
    {
        SCOPED_TRACE(std::to_string(map) + " " + std::to_string(right));
        const auto tested = AWizard(0x49, Wizard, map);
        tested->Hero().hands[0].model = right;
        tested->Movement().weaponSkill = {.active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
        const bool thrusts = right != 0xe12;
        const CrtRandom expected =
            Advanced(tested->random, OwnDraws + (thrusts ? SendDraws + OwnDraws + HeroCellDraws : 0));
        ASSERT_TRUE(
            Client::Scenes::UseTheSkillOnItsTarget(tested->Hero(), tested->Hero(), tested->Context()).HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), thrusts ? 2u : 0u);
        if (thrusts)
        {
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x49, 0x0a, 0x1b}));
            EXPECT_EQ(tested->Hero().animation.action, 0x86);
            EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Clear);
            EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x40}));
        }
    }
}

// 0x55cbc0(p1, p2) con dos personajes distintos (los de la rama los pasan iguales): p1 (el lugar 8) toma la posición
// del blanco en +0x3a8, 0x4f5290, la celda del 1E y la clave por 0x558eb0(p1, blanco), que con un p1 que no es el héroe
// publicado da −1 (0xffff); p2 (el lugar 9) gira hacia el blanco, da el rumbo del 1E y hace la 0x86 de la 0x49. El D3
// sale del héroe publicado (el lugar 0), que no gira (el ángulo 0: 0x10). Con "webzen" en el nombre del héroe
// publicado, el 19 y el 1E no salen (ni +0x1bd en la 0xd), pero sí 0x4f5290 o la 0x86 y el D3 (0x55cd47, 0x55d9da,
// 0x55e69c). La 0xd no mira el freno de 300 ms (0x55e70f no lee GetTickCount): sale con +0x3a8 recién puesto. En
// Lorencia y Devias.
TEST(HeroSkillsWizardTest, TheSkillOnItsTargetSplitsTheUserAndTheTurned)
{
    constexpr std::size_t UserSlot = 8;
    constexpr std::size_t TurnedSlot = 9;
    const auto direction = [](float angle)
    {
        const double scale = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f360b61}));
        const auto truncated = Client::World::OriginalFtolLow(static_cast<double>(angle) * scale);
        return static_cast<std::uint8_t>(truncated.value_or(-1) & 0xff);
    };
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{1}, std::uint8_t{0x49}, std::uint8_t{0xd}})
        {
            for (const bool silent : {false, true})
            {
                SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill) + (silent ? " webzen" : ""));
                const auto tested = AWizard(skill, Wizard, map);
                tested->Hero().hands[0].model = 0xc12;
                if (silent)
                {
                    const std::string name = "webzen";
                    for (std::size_t i = 0; i < name.size(); ++i)
                        tested->Hero().name[i] = static_cast<std::byte>(name[i]);
                }
                auto& user = tested->world.Actors()[UserSlot];
                auto& turned = tested->world.Actors()[TurnedSlot];
                for (Client::Scenes::ActorState* actor : {&user, &turned})
                {
                    actor->alive = true;
                    actor->type = Client::Data::Models::PlayerSkeletonModel;
                    actor->hands[0].model = actor->hands[1].model = actor->wings.model = actor->pet.model =
                        Client::Scenes::NoItemModel;
                    actor->animation.action = 1;
                }
                user.position = {18250.0f, 11550.0f, 0.0f};
                turned.position = {18050.0f, 11950.0f, 0.0f};
                turned.angles.z = 7.0f;
                tested->Movement().weaponSkill = {
                    .active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
                // La 0xd no frena: con la 0x1 o la 0x49 este último uso las frenaría.
                tested->Movement().lastActionTick = skill == 0xd ? Ticks : 0;
                const bool sends = !silent;
                const int hit = skill == 0x49 ? OwnDraws : HitDraws;
                const CrtRandom expected =
                    Advanced(tested->random, OwnDraws + (sends ? SendDraws : 0) + hit + HeroCellDraws);
                const auto used = Client::Scenes::UseTheSkillOnItsTarget(user, turned, tested->Context());
                ASSERT_TRUE(used.HasValue()) << used.Error();
                EXPECT_EQ(tested->random.State(), expected.State());

                EXPECT_EQ(user.attackPosition, tested->Chosen().position);
                EXPECT_EQ(turned.attackPosition, glm::vec3(0.0f));
                const auto toward = static_cast<float>(Client::Render::Models::CreateAngle(
                    18050.0f, 11950.0f, tested->Chosen().position.x, tested->Chosen().position.y));
                EXPECT_EQ(turned.angles.z, toward);
                EXPECT_EQ(user.angles.z, 0.0f);
                EXPECT_EQ(tested->Hero().angles.z, 0.0f);
                if (skill == 0x49)
                {
                    EXPECT_EQ(turned.animation.action, 0x86);
                    EXPECT_EQ(user.animation.action, 1);
                }
                else
                {
                    EXPECT_GE(user.animation.action, 0x8e);
                    EXPECT_LE(user.animation.action, 0x8f);
                    EXPECT_EQ(turned.animation.action, 1);
                }
                EXPECT_EQ(tested->Movement().lastShotSkill, skill == 0xd && sends ? 0xdu : 0u);

                ASSERT_EQ(tested->sent.size(), sends ? 2u : 1u);
                if (sends && skill == 0xd)
                {
                    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0xd, 184, 117,
                                                                          direction(toward), 0, 0, 0xff, 0xff, 0}));
                }
                else if (sends)
                {
                    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x0a, 0x1b}));
                }
                const std::size_t turn = tested->sent.size() - 1;
                EXPECT_EQ(tested->sent[turn].delivery, Client::Network::Delivery::Clear);
                EXPECT_EQ(tested->Sent(turn), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
            }
        }
    }
}

// 0x593051..0x593873: el Teleport manda el 1C a la celda del blanco y pone 0x576794c y la acción 0x93, salvo con el
// cursor, 0x1000, la acción, +0x80, el alpha, la pared (sin 0x20 ni 0x40), 0x576794c, 0x77f3bb0 o 3 s desde el último.
TEST(HeroSkillsWizardTest, TeleportSendsTheTargetCell)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(6, Wizard, map);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 6.0f;
        tested->world.Map().terrain.attributes->cells[(119u << 8) + 186u] = 0x60;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x1c, 0x00, 186, 119}));
        EXPECT_EQ(tested->Movement().gateRequestBlocked, 1);
        EXPECT_EQ(tested->Hero().animation.action, 0x93);
    }
    struct Case
    {
        const char* what;
        void (*prepare)(WizardHero&);
    };
    const Case cases[] = {
        {"cursor", [](WizardHero& hero) { hero.world.Runtime().cursorItemCount = 1; }},
        {"0x1000", [](WizardHero& hero) { hero.Hero().flags78 = 0x1000; }},
        {"action", [](WizardHero& hero) { hero.Hero().animation.action = 0x93; }},
        {"0x80", [](WizardHero& hero) { hero.Hero().byte80 = 2; }},
        {"alpha", [](WizardHero& hero) { hero.Hero().alpha = 0.5f; }},
        {"wall", [](WizardHero& hero) { hero.world.Map().terrain.attributes->cells[(119u << 8) + 186u] = 4; }},
        {"gate", [](WizardHero& hero) { hero.Movement().gateRequestBlocked = 1; }},
        {"warp", [](WizardHero& hero) { hero.Movement().warpPending = true; }},
        {"wait", [](WizardHero& hero) { hero.Movement().lastTeleportMs = Ticks - 2999; }},
        {"gladiator", [](WizardHero& hero) { hero.Hero().characterClass = Gladiator; }},
    };
    for (const Case& tried : cases)
    {
        SCOPED_TRACE(tried.what);
        const auto tested = AWizard(6);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 6.0f;
        tried.prepare(*tested);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        if (std::string(tried.what) != "action")
        {
            EXPECT_NE(tested->Hero().animation.action, 0x93);
        }
    }
    const auto tested = AWizard(6);
    tested->AimAtTheTerrain();
    tested->Movement().selectedSkillRange = 6.0f;
    tested->withTicks = false;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x59313a")) << result.Error();
}

// La celda de al lado que elige el Teleport Ally (0x5927cb..0x5928f1) con los rand() de `random`: dos rand() % 3 por
// vuelta, sin el centro, y en el mapa 30 la X y la Y del héroe publicado (8 rand()) en cada celda que no es el centro.
struct AllyCell
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    int draws = 0;
};
AllyCell FirstAllyCell(CrtRandom random)
{
    AllyCell cell;
    for (;;)
    {
        const int dx = random.Next() % 3;
        const int dy = random.Next() % 3;
        cell.draws += 2;
        if (dx == 1 && dy == 1)
            continue;
        cell.draws += HeroCellDraws;
        cell.x = 182 + dx - 1;
        cell.y = 115 + dy - 1;
        return cell;
    }
}

// 0x5925d9..0x593050: Teleport Ally. Sin asedio, solo con la party (los nombres son del hito 7); en el asedio (mapa
// 30), una celda de al lado sin pared, o cualquiera dentro del castillo, y el B0 con la clave del elegido en little
// endian; el elegido mira a su +0x3a8 y toma la acción 0x93.
TEST(HeroSkillsWizardTest, TeleportAllyMovesTheChosenNextToTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0xf, Wizard, map);
        tested->ChooseAnAlly();
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        tested->world.Runtime().hud.partyCount = 2;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x59272c")) << result.Error();
    }
    for (const bool castle : {true, false})
    {
        SCOPED_TRACE(castle);
        const auto tested = AWizard(0xf, Wizard, 30);
        tested->world.Runtime().siegeActive = true;
        tested->ChooseAnAlly();
        if (castle)
            tested->Hero().cellY = 0xc0;
        const CrtRandom before = Advanced(tested->random, HeadDraws + HeroCellDraws);
        const AllyCell cell = FirstAllyCell(before);
        const CrtRandom expected = Advanced(before, cell.draws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x07, 0xb0, 0x1b, 0x0a, static_cast<std::uint8_t>(cell.x),
                                             static_cast<std::uint8_t>(cell.y)}));
        EXPECT_EQ(tested->Movement().targetX, static_cast<std::uint32_t>(cell.x));
        EXPECT_EQ(tested->Movement().targetY, static_cast<std::uint32_t>(cell.y));
        EXPECT_EQ(tested->Movement().gateRequestBlocked, 1);
        EXPECT_EQ(tested->Chosen().animation.action, 0x93);
    }
    {
        // Con 0x576794c, lo baja y nada.
        const auto tested = AWizard(0xf, Wizard, 30);
        tested->world.Runtime().siegeActive = true;
        tested->ChooseAnAlly();
        tested->Movement().gateRequestBlocked = 1;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().gateRequestBlocked, 0);
    }
    {
        // Con las ocho celdas de al lado tapadas fuera del castillo, nada después de 11 celdas tapadas.
        const auto tested = AWizard(0xf, Wizard, 30);
        tested->world.Runtime().siegeActive = true;
        tested->ChooseAnAlly();
        auto& cells = tested->world.Map().terrain.attributes->cells;
        for (std::uint32_t y = 114; y <= 116; ++y)
            for (std::uint32_t x = 181; x <= 183; ++x)
                cells[(y << 8) + x] = 4;
        CrtRandom probe = Advanced(tested->random, HeadDraws + HeroCellDraws);
        int walls = 0;
        while (walls <= 10)
        {
            const int dx = probe.Next() % 3;
            const int dy = probe.Next() % 3;
            if (dx == 1 && dy == 1)
                continue;
            for (int i = 0; i < HeroCellDraws; ++i)
                static_cast<void>(probe.Next());
            ++walls;
        }
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), probe.State());
        EXPECT_TRUE(tested->sent.empty());
    }
}

// 0x58b026..0x58c15b: Soul Barrier. Sin elegido, el 19 con la clave del jugador, 0x4f5290 y sigue por el alcance; con
// un elegido que no es jugador, 0x6c8e38 en −1; con un jugador, la party (hito 7).
TEST(HeroSkillsWizardTest, SoulBarrierWithoutAChosenGoesOnTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x10, Wizard, map);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 6.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x10, 0x12, 0x34}));
    }
    {
        const auto tested = AWizard(0x10);
        tested->world.Runtime().selectedTargets[4] = 2;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_TRUE(tested->sent.empty());
    }
    const auto tested = AWizard(0x10);
    tested->ChooseAnAlly();
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_TRUE(tested->sent.empty());
    tested->world.Runtime().hud.partyCount = 1;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x58b06e")) << result.Error();
}

// 0x58a13a: las 0x12 a 0x17 y la 0x2f no hacen nada después de apuntar.
TEST(HeroSkillsWizardTest, SomeSkillsDoNothingAfterAiming)
{
    for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x2f}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AWizard(skill);
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 184u);
        EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
        EXPECT_TRUE(tested->sent.empty());
    }
}
} // namespace
