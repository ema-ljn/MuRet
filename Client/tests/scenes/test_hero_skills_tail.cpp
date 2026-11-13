#include "Scenes/HeroSkillsTail.h"

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
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// La cola de 0x57cfc0 con las skills 0x43 a 0x48 (0x5808da..0x599d5d, CL9 hito 4, fase 2c), llamada directo y desde
// 0x57cfc0: el héroe en el lugar 0 en (182, 115), un monstruo elegido en el lugar 5 en (184, 117) con la clave 0x0a1b
// y un Skill.bmd inventado. Cada envío con sus bytes y cada camino con sus rand(), en Lorencia (0), Devias (2) y, desde
// 0x57cfc0, en Blood Castle (11), Chaos Castle (18) y el asedio (30).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// La cabeza de la cola (0x5808ef..0x594002) sin espera: el registro propio y, con él abierto, las entradas del costo,
// de la espera (0x552c50) y de la energía (0x551f40).
constexpr int TailHeadDraws = OwnDraws + 3 * EntryDraws;
// Sin el maná o sin la stamina, sin la espera.
constexpr int UnpaidHeadDraws = OwnDraws + 2 * EntryDraws;
// La celda lógica del héroe: la Y y la X (o la X y la Y).
constexpr int HeroCellDraws = 2 * CellDraws;
// La 0x43 entera: la celda del rumbo, la del 1E y el relleno y el serial del C3. 0x4f3f80 corre con el registro propio
// abierto, sin rand().
constexpr int SternDraws = TailHeadDraws + 2 * HeroCellDraws + SendDraws;
constexpr int MagicAttackDraws = TailHeadDraws + SendDraws;
// La cabeza de 0x57cfc0 hasta la skill de cada mano: el registro propio con la skill del lugar, la entrada del alcance,
// la del +0x31 (0x57d2f1) y las dos celdas lógicas (0x57d4d6, 0x57d4f4).
constexpr int AimDraws = OwnDraws + 2 * EntryDraws + 2 * CellDraws;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint32_t Ticks = 100000;
constexpr std::uint8_t StandingAction = 0x3f;
constexpr std::uint8_t CancelAction = 0x87;

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

std::uint8_t DefaultActionOf(std::uint8_t skill)
{
    return skill == 0x44 || skill == 0x47 ? CancelAction : StandingAction;
}

// El héroe de la clase `heroClass` con la skill en su lugar 0 y el blanco que 0x57cfc0 le dejó: +0x3a8 en el elegido y
// 0x7758588 en su lugar.
struct TailHero
{
    explicit TailHero(std::uint8_t used, std::int32_t map = 0, std::uint8_t heroClass = 5) : skill(used)
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
        hero.movement = 1;
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
        hero.attackPosition = chosen.position;
        runtime.selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
        runtime.movement.weaponSkill.target = static_cast<std::int32_t>(ChosenSlot);
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
    void CallIt(const std::string& name)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            Hero().name[i] = static_cast<std::byte>(name[i]);
    }
    HeroSkillContext Context()
    {
        return HeroSkillContext{.world = world,
                                .random = random,
                                .skills = &skills,
                                .playerSkeleton = &skeleton,
                                .tickCount = [this] { return ticks; },
                                .worldTime = 5000.0f,
                                .send = [this](Client::Protocol::Outgoing outgoing)
                                { sent.push_back(std::move(outgoing)); },
                                .playerKey = PlayerKey};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return Client::Scenes::UseTheSharedTail(Hero(), skill, Context());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        return ClearBytesOf(sent.at(index));
    }

    std::uint8_t skill;
    Client::Scenes::WorldState world;
    CrtRandom random;
    std::uint32_t ticks = Ticks;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Data::Tables::SkillTable skills =
        SkillsWith({{0x43}, {0x44}, {0x45}, {0x46}, {0x47}, {0x48}, {0x13}, {0x49}});
};

std::unique_ptr<TailHero> AHero(std::uint8_t skill, std::int32_t map = 0, std::uint8_t heroClass = 5)
{
    return std::make_unique<TailHero>(skill, map, heroClass);
}

// 0x5808da..0x5808e9: fuera de 0x43 a 0x48, nada, ni rand().
TEST(HeroSkillsTailTest, OutsideTheTailNothing)
{
    for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x42}, std::uint8_t{0x49}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AHero(skill);
        const CrtRandom expected = tested->random;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
        EXPECT_EQ(tested->Hero().movement, 1);
    }
}

// 0x580916..0x580922: sin un jugador (+2 en 0x20a), el costo de 0x551c70 y el cierre del registro propio, nada más.
TEST(HeroSkillsTailTest, OnlyAPlayerGoesOn)
{
    for (const std::uint8_t skill : {std::uint8_t{0x43}, std::uint8_t{0x46}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AHero(skill);
        tested->Hero().type = 0x20;
        const CrtRandom expected = Advanced(tested->random, OwnDraws + EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
        EXPECT_EQ(tested->Hero().movement, 1);
    }
}

// 0x580928..0x593fe0: sin el maná, la poción (sin ella, sigue; con ella, el hito 5); sin la stamina, sin la espera;
// con ellos, la espera de 0x552c50, cuyo resultado nadie mira; la energía de 0x551f40, que nadie compara. Nada de eso
// frena la skill (Q-C1226).
TEST(HeroSkillsTailTest, TheHeadNeverStopsTheSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHero(0x44, map);
        tested->skills = SkillsWith({{0x44, 5}});
        const CrtRandom expected = Advanced(tested->random, UnpaidHeadDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x44, 0x0a, 0x1b}));
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        tested->ticks += 1000;
        const auto again = tested->Use();
        ASSERT_FALSE(again.HasValue());
        EXPECT_TRUE(Contains(again.Error(), "0x580954")) << again.Error();
    }
    {
        const auto tested = AHero(0x45);
        tested->skills = SkillsWith({{0x45, 0, 5}});
        const CrtRandom expected = Advanced(tested->random, UnpaidHeadDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    {
        // 10 · 5 · 4 / 100 + 20 = 22 de energía, sin ella en el registro.
        const auto tested = AHero(0x43);
        tested->skills = SkillsWith({{0x43, 0, 0, 10, 5}});
        const CrtRandom expected = Advanced(tested->random, SternDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    // Con la espera corriendo, 0x552c50 dice que no y la skill sale igual.
    const auto tested = AHero(0x48, 2);
    tested->skills = SkillsWith({{0x48, 0, 0, 0, 0, 1000}});
    ASSERT_TRUE(tested->Use().HasValue());
    tested->ticks += 1000;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 2u);
}

// 0x594009..0x594de9: la 0x43 gira hacia +0x3a8 (135°), manda el 1E con la celda lógica (la X y la Y), el rumbo ·
// 256/360, la celda del blanco desde la celda lógica (0x5ae300), la de 0x4e9920 desde la posición, la clave de
// 0x558eb0 con 0x7758588 y la clave mágica 0 de 0x57cf90(0); 0x5767948 en 0x43, sin el freno ni el contador 0x77f3bad.
// Después 0x4f3f80 y la acción 0x3f, con +0x378 en 0.
TEST(HeroSkillsTailTest, TheSternSendsTheDurationMagic)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHero(0x43, map);
        tested->Movement().magicKeyCounter = 7;
        const CrtRandom expected = Advanced(tested->random, SternDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 182, 115, 0x60, 0xaa, 0xaa, 0x0a, 0x1b, 0}));
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Movement().lastShotSkill, 0x43u);
        EXPECT_EQ(tested->Movement().lastActionTick, 0u);
        EXPECT_EQ(tested->Movement().magicKeyCounter, 7);
        EXPECT_EQ(tested->Hero().magicKey, 0);
        EXPECT_EQ(tested->Hero().animation.action, StandingAction);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    {
        // El byte 7 sale de la celda lógica (+0x41c, aquí 180) y el 8 de la posición: con ellas distintas, difieren.
        const auto tested = AHero(0x43);
        tested->Hero().cellX = 180;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 180, 115, 0x60, 0xca, 0xaa, 0x0a, 0x1b, 0}));
    }
    {
        // Lejos: 0x5ae300 recorta a 7 (0xf8); 0x4e9920 no recorta (0x94).
        const auto tested = AHero(0x43, 2);
        tested->Hero().attackPosition = {19450.0f, 11550.0f, 0.0f};
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0)[7], 0xf8);
        EXPECT_EQ(tested->Sent(0)[8], 0x94);
    }
    {
        // Sin el blanco de la skill del arma (0x7758588 en −1), la clave 0xffff.
        const auto tested = AHero(0x43);
        tested->Movement().weaponSkill.target = -1;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0)[9], 0xff);
        EXPECT_EQ(tested->Sent(0)[10], 0xff);
    }
    // Con "webzen", sin el 1E ni 0x5767948 (y sin la segunda lectura de la celda), pero con el giro y la acción.
    const auto tested = AHero(0x43, 2);
    tested->CallIt("awebzen");
    const CrtRandom expected = Advanced(tested->random, TailHeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Movement().lastShotSkill, 0u);
    EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
    EXPECT_EQ(tested->Hero().animation.action, StandingAction);
    EXPECT_EQ(tested->Hero().movement, 0);
}

// 0x594e42..0x599d1c: las 0x44 a 0x48 mandan el 19 con la clave del elegido (+0x1f4 de 0x6c8e30) o, sin él, la del
// jugador (0x57678bc), después del freno de 300 ms; la acción 0x87 la 0x44 y la 0x47, 0x3f las demás, y +0x378 en 0.
TEST(HeroSkillsTailTest, TheMagicAttacksSendTheChosenKey)
{
    for (std::uint8_t skill = 0x44; skill <= 0x48; ++skill)
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AHero(skill, map);
            const CrtRandom expected = Advanced(tested->random, MagicAttackDraws);
            const auto result = tested->Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x0a, 0x1b}));
            EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
            EXPECT_EQ(tested->Movement().lastShotSkill, 0u);
            EXPECT_EQ(tested->Hero().animation.action, DefaultActionOf(skill));
            EXPECT_EQ(tested->Hero().movement, 0);
        }
        SCOPED_TRACE(skill);
        const auto tested = AHero(skill);
        tested->world.Runtime().selectedTargets[2] = -1;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
    }
    {
        // La clave del elegido sin mirar si se lo puede atacar: un aliado también.
        const auto tested = AHero(0x46);
        tested->Chosen().byte88 = 1;
        tested->Chosen().byte370 = 0;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x46, 0x0a, 0x1b}));
    }
    // Un elegido fuera del pool corta donde lee su clave.
    const auto tested = AHero(0x44);
    tested->world.Runtime().selectedTargets[2] = 1000;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x595858")) << result.Error();
}

// El freno (300 ms o menos desde 0x576791c) y "webzen" dejan las 0x44 a 0x48 sin el 19, pero con la acción.
TEST(HeroSkillsTailTest, TheBrakeAndTheSilentNameKeepTheAction)
{
    for (const std::uint8_t skill : {std::uint8_t{0x44}, std::uint8_t{0x48}})
    {
        SCOPED_TRACE(skill);
        const auto braked = AHero(skill);
        braked->Movement().lastActionTick = Ticks - 300;
        const CrtRandom expected = Advanced(braked->random, TailHeadDraws);
        ASSERT_TRUE(braked->Use().HasValue());
        EXPECT_EQ(braked->random.State(), expected.State());
        EXPECT_TRUE(braked->sent.empty());
        EXPECT_EQ(braked->Movement().lastActionTick, Ticks - 300);
        EXPECT_EQ(braked->Hero().animation.action, DefaultActionOf(skill));
        const auto named = AHero(skill, 2);
        named->world.Runtime().selectedTargets[2] = -1;
        named->CallIt("webzen");
        ASSERT_TRUE(named->Use().HasValue());
        EXPECT_TRUE(named->sent.empty());
        EXPECT_EQ(named->Movement().lastActionTick, 0u);
        EXPECT_EQ(named->Hero().animation.action, DefaultActionOf(skill));
        EXPECT_EQ(named->Hero().movement, 0);
    }
}

// 0x594dee..0x594e3d y 0x599d27..0x599d45: fuera de la zona segura, el caballo oscuro 0x52, Uniria 0x40, Dinorant 0x41
// y Fenrir 0x5d (EBX); en ella, la acción sin montura.
TEST(HeroSkillsTailTest, TheMountChoosesTheAction)
{
    struct Case
    {
        std::uint16_t mount;
        std::uint8_t action;
    };
    for (const std::uint8_t skill : {std::uint8_t{0x43}, std::uint8_t{0x44}, std::uint8_t{0x45}})
    {
        for (const Case& tried : {Case{0x1c16, 0x52}, Case{0x1c14, 0x40}, Case{0x1c15, 0x41}, Case{0x1c37, 0x5d},
                                  Case{0x1c13, DefaultActionOf(skill)}})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(tried.mount);
            const auto outside = AHero(skill);
            outside->Hero().pet.model = tried.mount;
            ASSERT_TRUE(outside->Use().HasValue());
            EXPECT_EQ(outside->Hero().animation.action, tried.action);
            EXPECT_EQ(outside->Hero().movement, 0);
            const auto inside = AHero(skill, 2);
            inside->Hero().pet.model = tried.mount;
            inside->Hero().safeZone = 1;
            ASSERT_TRUE(inside->Use().HasValue());
            EXPECT_EQ(inside->Hero().animation.action, DefaultActionOf(skill));
        }
    }
}

// Un héroe (de las clases 5 a 7, sin rama: 0x5897aa..0x5897b1) que usa la skill desde 0x57cfc0 con el derecho.
std::unique_ptr<TailHero> AHeroClicking(std::uint8_t skill, std::int32_t map, std::uint8_t heroClass = 6)
{
    auto tested = AHero(skill, map, heroClass);
    tested->Hero().attackPosition = {};
    tested->Movement().weaponSkill.target = -1;
    tested->world.Runtime().ui.rightClick = true;
    return tested;
}

// Desde 0x57cfc0: el blanco de 0x57ce80 (+0x3a8 en el elegido) llega a la cola; 0x7758588 lo pone la skill de la
// mano de las clases 1, 3 y 4 (0x57d582..0x57d61b), y las clases 5 a 7 mandan el −1 que quedó: la clave 0xffff
// (Q-C1228). Fuera de la zona segura en Lorencia, Devias y el asedio (30, donde 0x5a5540 no mira estas skills); en
// ella, solo en Blood Castle (11 a 17) y Chaos Castle (18 a 23), y la montura no cuenta.
TEST(HeroSkillsTailTest, TheHeroSkillReachesTheTailInEveryMap)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x43, map);
        const CrtRandom expected = Advanced(tested->random, AimDraws + SternDraws);
        const auto result = Client::Scenes::UseTheHeroSkill(tested->Hero(), tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 182, 115, 0x60, 0xaa, 0xaa, 0xff, 0xff, 0}));
        EXPECT_EQ(tested->Hero().animation.action, StandingAction);
    }
    {
        // El caballero: la skill de cada mano (el registro propio dos veces) pone 0x7758588 en el elegido, y su rama
        // sin arma derecha (el costo y el cierre) termina en 0x5808da.
        const auto tested = AHeroClicking(0x43, 2, 1);
        const CrtRandom expected =
            Advanced(tested->random, AimDraws + 2 * OwnDraws + EntryDraws + OwnDraws + SternDraws);
        const auto result = Client::Scenes::UseTheHeroSkill(tested->Hero(), tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 182, 115, 0x60, 0xaa, 0xaa, 0x0a, 0x1b, 0}));
    }
    for (const std::int32_t map : {11, 18})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x47, map);
        tested->Hero().safeZone = 1;
        tested->Hero().pet.model = 0x1c37;
        const auto result = Client::Scenes::UseTheHeroSkill(tested->Hero(), tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x47, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, CancelAction);
    }
    // En la zona segura de Lorencia, 0x57d33d no deja pasar: ni la cola.
    const auto tested = AHeroClicking(0x47, 0);
    tested->Hero().safeZone = 1;
    ASSERT_TRUE(Client::Scenes::UseTheHeroSkill(tested->Hero(), tested->Context()).HasValue());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Hero().animation.action, 1);
}
} // namespace
