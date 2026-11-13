#include "Scenes/HeroSkillsElf.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill55faa0.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

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

// La rama del elfo de 0x57cfc0 (0x57d790..0x58187c, CL9 hito 4, fase 2c) y 0x55faa0, llamadas directo: el elfo en el
// lugar 0 en (182, 115), un monstruo elegido en el lugar 5 en (184, 117) con la clave 0x0a1b y un Skill.bmd inventado.
// Cada envío con sus bytes y cada camino con sus rand(), en Lorencia (0), Devias (2) y los mapas de cada caso.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// La cabeza (0x57d790..0x57e12a) sin espera: el registro propio y, con él abierto, el costo, la espera y la energía.
constexpr int HeadDraws = OwnDraws + 3 * EntryDraws;
// La celda lógica del héroe: la Y y la X.
constexpr int HeroCellDraws = 2 * CellDraws;
// 0x4f5290 sobre el elfo: 0x4f3f80 (el registro propio) y la acción 0x92, sin rand().
constexpr int ElfHitDraws = OwnDraws;
// 0x55faa0 antes de su caso: la celda del héroe publicado para el D3 (la X y la Y) y el registro propio con la skill.
constexpr int ReachedDraws = HeroCellDraws + OwnDraws;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint32_t Ticks = 100000;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint16_t BowModel = 0xa12; // un arco (0x800) en la izquierda

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
    std::uint8_t range = 4;
};

// Skill.bmd con el nivel (+0x20), el maná (+0x22), la stamina (+0x24), el alcance (+0x26), la espera (+0x28) y la
// energía (+0x2c).
Client::Data::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Data::Tables::SkillRecordSize;
        plain[base + 0x20] = static_cast<std::byte>(invented.level);
        plain[base + 0x22] = static_cast<std::byte>(invented.mana);
        plain[base + 0x24] = static_cast<std::byte>(invented.stamina);
        plain[base + 0x26] = static_cast<std::byte>(invented.range);
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

struct ElfHero
{
    explicit ElfHero(std::uint8_t used, std::int32_t map = 0) : skill(used)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero.alive = true;
        hero.type = Client::Data::Models::PlayerSkeletonModel;
        hero.characterClass = ElfClass;
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
    // Sin elegido: 0x57ce80 apunta al terreno, a (183, 116).
    void AimAtTheTerrain()
    {
        world.Runtime().selectedTargets[2] = -1;
        terrainPoint = glm::vec3{18350.0f, 11650.0f, 0.0f};
    }
    void CallIt(const std::string& name)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            Hero().name[i] = static_cast<std::byte>(name[i]);
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
        return context;
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return Client::Scenes::UseTheElfSkill(Hero(), skill, Context());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        return ClearBytesOf(sent.at(index));
    }

    std::uint8_t skill;
    Client::Scenes::WorldState world;
    CrtRandom random;
    std::uint32_t ticks = Ticks;
    std::optional<glm::vec3> terrainPoint;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Data::Tables::SkillTable skills =
        SkillsWith({{0x13}, {0x1a}, {0x1b}, {0x1c}, {0x1e}, {0x24}, {0x2e}, {0x33}, {0x34}, {0x35}, {0x43}, {0x4c}});
};

std::unique_ptr<ElfHero> AnElf(std::uint8_t skill, std::int32_t map = 0)
{
    return std::make_unique<ElfHero>(skill, map);
}

// 0x57d808..0x57d854: sin el maná, la poción de maná (sin ella, nada; con ella, el hito 5); con la stamina por encima
// de +0x38 o la energía de 0x551f40 por encima de +0x1e + +0x30, nada.
TEST(HeroSkillsElfTest, TheHeadPaysForTheSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x1a, map);
        tested->skills = SkillsWith({{0x1a, 5}});
        const CrtRandom expected = Advanced(tested->random, OwnDraws + EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().targetX, 0u);
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        const auto again = tested->Use();
        ASSERT_FALSE(again.HasValue());
        EXPECT_TRUE(Contains(again.Error(), "0x57d837")) << again.Error();
    }
    {
        const auto tested = AnElf(0x1a);
        tested->skills = SkillsWith({{0x1a, 0, 5}});
        const CrtRandom expected = Advanced(tested->random, OwnDraws + EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        tested->world.OwnCharacter().bp = 5;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    // 10 · 5 · 4 / 100 + 20 = 22.
    const auto tested = AnElf(0x1a);
    tested->skills = SkillsWith({{0x1a, 0, 0, 10, 5}});
    const CrtRandom expected = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    tested->world.OwnCharacter().energy = 12;
    tested->world.OwnCharacter().word30 = 10;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 1u);
}

// 0x57f252..0x57f9af: las invocaciones (0x1e a 0x24) mandan el 19 con la clave del jugador y ponen la acción 0x92
// (0x4f5290 del elfo), sin mirar el alcance. En el mapa 10 y en Chaos Castle (18 a 23), nada después de la cabeza.
TEST(HeroSkillsElfTest, TheSummonsSendTheMagicAttackWithThePlayerKey)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1e}, std::uint8_t{0x24}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AnElf(skill, map);
            tested->Movement().selectedSkillRange = 1.0f;
            const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + ElfHitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 0x92);
            EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
            EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
            EXPECT_EQ(tested->Movement().targetX, 184u);
        }
    }
    for (const std::int32_t map : {10, 18, 23})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x1e, map);
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
    {
        // Con el freno o con "webzen" en el nombre, sin el 19, pero con la acción.
        const auto braked = AnElf(0x1e);
        braked->Movement().lastActionTick = Ticks - 300;
        ASSERT_TRUE(braked->Use().HasValue());
        EXPECT_TRUE(braked->sent.empty());
        EXPECT_EQ(braked->Hero().animation.action, 0x92);
        const auto named = AnElf(0x1e, 2);
        named->CallIt("awebzen");
        ASSERT_TRUE(named->Use().HasValue());
        EXPECT_TRUE(named->sent.empty());
        EXPECT_EQ(named->Movement().lastActionTick, 0u);
        EXPECT_EQ(named->Hero().animation.action, 0x92);
    }
}

// 0x57e14d..0x57f005: la 0x34 con un arco del héroe publicado, flechas (sin arma de tiro en el registro propio,
// 0x561c50 da 1) y la celda del blanco al alcance: el rumbo hacia el blanco (135°), el 1E con la celda lógica, el rumbo
// · 256/360, dos ceros y la clave de 0x558eb0 con 0x7758588, y 0x4f4a10. Después sigue por el alcance (0x57f008), que
// con un monstruo y la 0x34 no hace nada más.
TEST(HeroSkillsElfTest, Skill34ShootsAtTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x34, map);
        tested->Hero().hands[1].model = BowModel;
        tested->Movement().weaponSkill.target = static_cast<std::int32_t>(ChosenSlot);
        // 0x561c50 y 0x4f3f80 (en 0x4f4a10) abren y cierran el registro propio; la línea de vista, la celda otra vez.
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + OwnDraws + HeroCellDraws + SendDraws + OwnDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x34, 182, 115, 0x60, 0, 0, 0x0a, 0x1b, 0}));
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Movement().lastShotSkill, 0x34u);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
    }
    {
        // Sin el blanco de la skill del arma, la clave 0xffff.
        const auto tested = AnElf(0x34);
        tested->Hero().hands[1].model = BowModel;
        tested->Movement().weaponSkill.target = -1;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0)[9], 0xff);
        EXPECT_EQ(tested->Sent(0)[10], 0xff);
    }
    {
        // Sin arma de tiro o fuera de alcance, sin el 1E: sigue por el alcance.
        const auto unarmed = AnElf(0x34);
        const CrtRandom expected = Advanced(unarmed->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(unarmed->Use().HasValue());
        EXPECT_EQ(unarmed->random.State(), expected.State());
        EXPECT_TRUE(unarmed->sent.empty());
        const auto far = AnElf(0x34, 2);
        far->Hero().hands[1].model = BowModel;
        far->Movement().selectedSkillRange = 1.0f;
        ASSERT_TRUE(far->Use().HasValue());
        EXPECT_TRUE(far->sent.empty());
        EXPECT_FLOAT_EQ(far->Hero().angles.z, 0.0f);
    }
    {
        // Con "webzen", sin el 1E ni 0x5767948, pero con 0x4f4a10.
        const auto tested = AnElf(0x34);
        tested->Hero().hands[1].model = BowModel;
        tested->CallIt("webzen");
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().lastShotSkill, 0u);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
    }
}

// 0x57fa46..0x580187 y 0x580188..0x5808d3: las 0x1a a 0x1c sin un jugador elegido (sobre el terreno o un monstruo) y
// la 0x35 mandan el 19 con la clave del jugador y ponen 0x4f5290; la 0x35, además, +0x378 en 0.
TEST(HeroSkillsElfTest, TheSupportSkillsSendTheMagicAttackWithThePlayerKey)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1a}, std::uint8_t{0x1c}, std::uint8_t{0x35}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AnElf(skill, map);
            tested->Hero().movement = 1;
            const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + SendDraws + ElfHitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 0x92);
            EXPECT_EQ(tested->Hero().movement, skill == 0x35 ? 0 : 1);
            EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
        }
    }
    // Sin elegido, sobre el punto del terreno.
    const auto tested = AnElf(0x1b);
    tested->AimAtTheTerrain();
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x1b, 0x12, 0x34}));
    EXPECT_EQ(tested->Movement().targetX, 183u);
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
}

// 0x57f203..0x57f251 → 0x55faa0: las 0x1a a 0x1c con un jugador elegido a la vista: el D3 de girar primero (con el
// ángulo de antes, 0: 0x10), el 19 con la clave del elegido y 0x4f5290; nada más.
TEST(HeroSkillsElfTest, TheSupportSkillsOnAnAllyGoThroughTheTarget)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x1b, map);
        tested->ChooseAnAlly();
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + ReachedDraws + SendDraws + ElfHitDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Clear);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x1b, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Hero().animation.action, 0x92);
    }
    // Fuera de alcance, el camino hasta el aliado con +0x379 en 5 (0x57f0e3..0x57f15e), y nada más.
    const auto tested = AnElf(0x1a, 2);
    tested->ChooseAnAlly();
    tested->Movement().selectedSkillRange = 1.0f;
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Hero().movement, 1);
    EXPECT_EQ(tested->Hero().movementType, 5);
    EXPECT_GT(tested->Hero().path.count, 0);
}

// 0x57f9b0..0x57fa25 → 0x55faa0: la 0x33 y la 0x2e de un jugador con el arco, sobre un atacable a la vista: el D3, el
// 19 con la clave del elegido y la acción (0x4f4a10 la 0x33; 0x4f48e0 la 0x2e: 0x7e sin alas, 0x80 con ellas); después
// la rama sigue hasta 0x5808da, que con ellas no hace nada.
TEST(HeroSkillsElfTest, TheShotsOfTheWeaponGoThroughTheTarget)
{
    for (const std::uint8_t skill : {std::uint8_t{0x33}, std::uint8_t{0x2e}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AnElf(skill, map);
            tested->Hero().hands[1].model = BowModel;
            // 0x561c50 y 0x4f3f80 abren y cierran el registro propio.
            const CrtRandom expected =
                Advanced(tested->random, HeadDraws + HeroCellDraws + ReachedDraws + OwnDraws + SendDraws + OwnDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 2u);
            EXPECT_EQ(tested->Sent(0)[2], 0xd3);
            EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x0a, 0x1b}));
            if (skill == 0x2e)
            {
                EXPECT_EQ(tested->Hero().animation.action, 0x7e);
            }
        }
    }
    {
        const auto tested = AnElf(0x2e);
        tested->Hero().hands[1].model = BowModel;
        tested->Hero().wings.model = 0xc12;
        const std::uint8_t before = tested->Hero().attackCount;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->Hero().animation.action, 0x80);
        EXPECT_EQ(tested->Hero().attackCount, before + 1);
    }
    // Sin el arco, 0x55faa0 no corre: ni el D3 ni el 19.
    const auto tested = AnElf(0x33);
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
}

// 0x58097a..0x5817d6: la 0x4c en alcance con el elegido atacable: el 1E con la celda lógica, el rumbo, la celda de
// 0x4e9920 en el byte 8, la clave de 0x558eb0 y la clave mágica; +0x434 en el elegido, +0x378 en 0 y la acción por las
// manos (0x62, ninguna). Fuera de alcance, el camino hasta el alcance · 1,2 sin +0x379; sin elegido, 0x6c8e38 en −1.
TEST(HeroSkillsElfTest, TheSkill4cCastsWithThePositionAndTheMagicKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x4c, map);
        tested->Hero().movement = 1;
        // La línea de vista (la celda), la espera de 0x552990 (el registro y la entrada) y la celda otra vez en el 1E.
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + EntryDraws + HeroCellDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x4c, 182, 115, 0x60, 0, 0xaa, 0x0a, 0x1b, 1}));
        EXPECT_EQ(tested->Hero().skillTarget4c, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Hero().magicKey, 1);
        EXPECT_EQ(tested->Hero().animation.action, 0x62);
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().lastShotSkill, 0x4cu);
    }
    {
        const auto tested = AnElf(0x4c, 2);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 0);
        EXPECT_GT(tested->Hero().path.count, 0);
    }
    const auto tested = AnElf(0x4c);
    tested->AimAtTheTerrain();
    tested->Movement().selectedSkillRange = 1.0f;
    tested->terrainPoint = glm::vec3{18850.0f, 11550.0f, 0.0f};
    tested->world.Runtime().selectedTargets[4] = 2;
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
    EXPECT_EQ(tested->Hero().movement, 0);
}

// 0x57f098..0x57f184: fuera de alcance, un monstruo no lleva camino (solo un jugador, +0x88 en 1); las otras skills
// terminan ahí. Las 0x43 a 0x47 siguen hasta 0x5808da, que corta en 0x5808ef.
TEST(HeroSkillsElfTest, OutOfReachOnlyAPlayerIsWalkedTo)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x13, map);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
    }
    const auto tested = AnElf(0x43);
    tested->Movement().selectedSkillRange = 1.0f;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5808ef")) << result.Error();
}

// 0x55faa0 con las demás skills: el D3 y el ángulo, y nada más.
TEST(HeroSkillsElfTest, TheTargetOfTheElfDoesNothingWithOtherSkills)
{
    const auto tested = AnElf(0x13);
    tested->Movement().weaponSkill = {.active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
    const CrtRandom expected = Advanced(tested->random, ReachedDraws);
    ASSERT_TRUE(
        Client::Scenes::UseTheElfSkillOnItsTarget(tested->Hero(), tested->Hero(), tested->Context()).HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[2], 0xd3);
    EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
    // El blanco fuera del pool corta.
    tested->Movement().weaponSkill.target = -1;
    const auto result = Client::Scenes::UseTheElfSkillOnItsTarget(tested->Hero(), tested->Hero(), tested->Context());
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x560080")) << result.Error();
}
} // namespace
