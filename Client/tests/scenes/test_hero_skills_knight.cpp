#include "Scenes/HeroSkillsKnight.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
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
#include "Scenes/Skill4e9920.h"
#include "Scenes/Skill4e9c50.h"
#include "Scenes/Skill551f40.h"
#include "Scenes/Skill57cf90.h"
#include "Scenes/SkillDelays.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
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
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La rama de las clases 1, 3 y 4 de 0x57cfc0 (CL9 hito 4, fase 2c; 06 § Las skills del caballero): el héroe en el
// lugar 0 con una espada a la derecha, un monstruo elegido en el 5 con una clave inventada y un Skill.bmd inventado.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;
using Client::Scenes::UseTheKnightSkill;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int C3Draws = 2;
// 0x551c70, 0x552c50 (su entrada; el registro ya está abierto) y 0x551f40, y el cierre del registro propio.
constexpr int PaidDraws = 3 * EntryDraws + OwnDraws;
// Sin pasar por 0x552c50 (sin poder usarla).
constexpr int UnpaidDraws = 2 * EntryDraws + OwnDraws;
// Las dos celdas lógicas del héroe, la Y antes que la X.
constexpr int CellsDraws = 2 * CellDraws;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint16_t Sword = 0x213;
constexpr std::uint8_t CellX = 0xb6; // 182
constexpr std::uint8_t CellY = 0x73; // 115

enum HeroClass : std::uint8_t
{
    Knight = 1,
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
    std::uint8_t range = 4;
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
    std::uint8_t level = 0;
    std::uint8_t energy = 0;
    std::uint16_t leadership = 0;
};

void PutWord(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8);
}

// Skill.bmd con el nivel (+0x20), el maná (+0x22), la stamina (+0x24), el alcance (+0x26), la energía (+0x2c) y el
// liderazgo (+0x2e) de cada skill.
Client::Data::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Data::Tables::SkillRecordSize;
        plain[base + 0x20] = static_cast<std::byte>(invented.level);
        PutWord(plain, base + 0x22, invented.mana);
        PutWord(plain, base + 0x24, invented.stamina);
        plain[base + 0x26] = static_cast<std::byte>(invented.range);
        plain[base + 0x2c] = static_cast<std::byte>(invented.energy);
        PutWord(plain, base + 0x2e, invented.leadership);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

void Rename(Client::Scenes::ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t index = 0; index < name.size() && index + 1 < actor.name.size(); ++index)
        actor.name[index] = static_cast<std::byte>(name[index]);
}

// Un héroe jugador en (182, 115) con una espada a la derecha que ya apuntó (0x57ce80) al elegido, un monstruo en
// (184, 117) con la clave 0x0a1b; el registro propio con 100 de maná, stamina y energía.
struct KnightHero
{
    explicit KnightHero(std::uint8_t used, std::uint8_t heroClass = Knight, std::int32_t map = 0) : skill(used)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero = Client::Tests::UnarmedPlayer(heroClass);
        hero.alive = true;
        hero.cellX = CellX;
        hero.cellY = CellY;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = Sword;
        hero.animation.action = 1;
        hero.attackPosition = {18450.0f, 11750.0f, 0.0f};
        Rename(hero, "Arandu");
        world.OwnCharacter().SetSkillAt(0, skill);
        auto& own = world.OwnCharacter();
        own.mana = 100;
        own.bp = 100;
        own.energy = 100;
        auto& chosen = world.Actors()[ChosenSlot];
        chosen.alive = true;
        chosen.byte88 = 2;
        chosen.byte370 = 2;
        chosen.key = ChosenKey;
        chosen.position = {18450.0f, 11750.0f, 0.0f};
        auto& movement = Movement();
        movement.targetX = 184;
        movement.targetY = 117;
        movement.selectedSkillRange = 4.0f;
        movement.weaponSkill = {.active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
        movement.lastActionTick = 0;
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[0];
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
                                .tickCount = [this] { return ticks; },
                                .send = [this](Client::Protocol::Outgoing outgoing)
                                { sent.push_back(std::move(outgoing)); },
                                .playerKey = PlayerKey};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return UseTheKnightSkill(Hero(), Hero().characterClass, skill, Context());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(sent.at(index).packet);
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : clear.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        return bytes;
    }

    std::uint8_t skill;
    Client::Scenes::WorldState world;
    CrtRandom random;
    std::uint32_t ticks = 10'000;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Data::Tables::SkillTable skills = SkillsWith({{.skill = 0x29},
                                                          {.skill = 0x2a},
                                                          {.skill = 0x30},
                                                          {.skill = 0x37},
                                                          {.skill = 0x38},
                                                          {.skill = 0x3e},
                                                          {.skill = 0x3f},
                                                          {.skill = 0x40},
                                                          {.skill = 0x41},
                                                          {.skill = 0x4b},
                                                          {.skill = 0x4c},
                                                          {.skill = 0x13},
                                                          {.skill = 0x31}});
    std::vector<Client::Protocol::Outgoing> sent;
};

std::unique_ptr<KnightHero> AKnight(std::uint8_t skill, std::uint8_t heroClass = Knight, std::int32_t map = 0)
{
    return std::make_unique<KnightHero>(skill, heroClass, map);
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

// El rumbo hacia el blanco (0x4e93f0 en float) y su _ftol: ((+0x24 + `offset`) · `first`) · `second`, cada producto
// redondeado a doble como la x87 con PC53.
std::uint8_t ExpectedDirection(double first, double second = 1.0, double offset = 0.0)
{
    const auto angle = static_cast<float>(Client::Render::Models::CreateAngle(18250.0f, 11550.0f, 18450.0f, 11750.0f));
    const auto value = Client::World::OriginalFtolLow((static_cast<double>(angle) + offset) * first * second);
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value.value_or(-1)));
}

const double TurnsOf256 = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f360b61}));
const double TurnFraction = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3b360b61}));

std::vector<std::uint8_t> DurationMagic(std::uint8_t skill, std::uint8_t direction, std::uint8_t byte7,
                                        std::uint8_t byte8, std::uint16_t key, std::uint8_t byte11 = 0)
{
    return {0xc1,
            0x0c,
            0x1e,
            skill,
            CellX,
            CellY,
            direction,
            byte7,
            byte8,
            static_cast<std::uint8_t>(key >> 8),
            static_cast<std::uint8_t>(key & 0xffu),
            byte11};
}

// 0x582393..0x5830dc: la 0x29 manda el 1E con las celdas lógicas, el rumbo · 256/360 y la clave del elegido; las
// velocidades, la acción 0x3d y +0x378 en 0. En Lorencia y en Devias.
TEST(HeroSkillsKnightTest, Skill29SendsThe1E)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x29, Knight, map);
        tested->Hero().movement = 1;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws + C3Draws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x29, ExpectedDirection(TurnsOf256), 0, 0, ChosenKey));
        EXPECT_EQ(tested->Movement().sentSkill, 0x29);
        EXPECT_EQ(tested->Hero().animation.action, 0x3d);
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, static_cast<float>(Client::Render::Models::CreateAngle(
                                                     18250.0f, 11550.0f, 18450.0f, 11750.0f)));
    }
}

// 0x5823b1..0x5823ce: sin elegido de la skill del arma, la clave 0xffff; con un elegido que 0x558eb0 no deja atacar
// (+0x370 que no es 2 ni 8, sin PK ni Ctrl), también.
TEST(HeroSkillsKnightTest, WithoutAnAttackableChosenTheKeyIsFfff)
{
    for (int variant = 0; variant < 2; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x29);
        if (variant == 0)
            tested->Movement().weaponSkill.target = -1;
        else
            tested->world.Actors()[ChosenSlot].byte370 = 0;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x29, ExpectedDirection(TurnsOf256), 0, 0, 0xffff));
    }
}

// El strstr en línea con "webzen" en el nombre del héroe publicado: no manda ni lee las celdas, pero hace el resto.
// Sin la sesión, el C3 saca igual sus dos rand().
TEST(HeroSkillsKnightTest, TheGameMasterAndTheClosedSocketSendNothing)
{
    {
        const auto tested = AKnight(0x29);
        Rename(tested->Hero(), "gmwebzen");
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().sentSkill, 0);
        EXPECT_EQ(tested->Hero().animation.action, 0x3d);
    }
    {
        const auto tested = AKnight(0x29);
        auto context = tested->Context();
        context.send = nullptr;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws + C3Draws);
        ASSERT_TRUE(UseTheKnightSkill(tested->Hero(), Knight, 0x29, context).HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().sentSkill, 0x29);
    }
}

// 0x587a8d..0x58886f: la 0x2a en alcance manda la celda de 0x4e9920 en el byte 8 y hace la 0x3e, sin 0x4f3f80; fuera
// de alcance, el otro elegido (0x6c8e38) en −1 y nada más.
TEST(HeroSkillsKnightTest, Skill2aSendsTheRelativeCellOrForgetsTheTarget)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x2a, Knight, map);
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x2a, ExpectedDirection(TurnsOf256), 0, 0xaa, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x3e);
    }
    const auto tested = AKnight(0x2a);
    tested->Movement().selectedSkillRange = 2.0f;
    tested->world.Runtime().selectedTargets[4] = 2;
    const CrtRandom expected = Advanced(tested->random, PaidDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
    EXPECT_EQ(tested->Hero().animation.action, 1);
}

// 0x5830e1..0x583ece: la 0x37 lee las celdas dos veces y manda la posición del blanco (cada lado entre −8 y 7) en el
// byte 7, sin mirar el alcance.
TEST(HeroSkillsKnightTest, Skill37SendsTheClampedTargetPosition)
{
    struct Case
    {
        std::uint32_t targetX;
        std::uint32_t targetY;
        std::uint8_t position;
    };
    for (const Case& tried : {Case{184, 117, 0xaa}, Case{200, 100, 0xf0}, Case{100, 200, 0x0f}, Case{182, 115, 0x88}})
    {
        SCOPED_TRACE(tried.targetX);
        const auto tested = AKnight(0x37);
        tested->Movement().targetX = tried.targetX;
        tested->Movement().targetY = tried.targetY;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x37, ExpectedDirection(TurnsOf256), tried.position, 0, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x3d);
    }
}

// 0x583ed3..0x584d6e: la 0x38, a pie y en alcance, manda el rumbo en la escala de 255 y el opuesto en el byte 8 y hace
// la 0x8d. Montado (Uniria, Dinorant, el caballo oscuro o Fenrir) o fuera de alcance, nada.
TEST(HeroSkillsKnightTest, Skill38OnlyOnFootAndInRange)
{
    {
        const auto tested = AKnight(0x38);
        const CrtRandom expected = Advanced(tested->random, PaidDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        const double byteScale = 255.0;
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x38, ExpectedDirection(TurnFraction, byteScale), 0xaa,
                                                 ExpectedDirection(TurnFraction, byteScale, 180.0), ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x8d);
    }
    for (const std::uint16_t pet :
         {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}, std::uint16_t{0x1c16}, std::uint16_t{0x1c37}})
    {
        SCOPED_TRACE(pet);
        const auto tested = AKnight(0x38);
        tested->Hero().pet.model = pet;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
    const auto tested = AKnight(0x38);
    tested->Movement().selectedSkillRange = 2.0f;
    tested->world.Runtime().selectedTargets[4] = 2;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], 2);
}

// 0x586436..0x587317: la 0x41 manda la celda del blanco contra la del héroe en el byte 7 y la de 0x4e9920 en el 8;
// la acción por la montura fuera de la zona segura: 0x51 en el caballo oscuro, 0x5b en Fenrir y si no 0x94.
TEST(HeroSkillsKnightTest, Skill41ActsByTheMount)
{
    struct Case
    {
        std::uint16_t pet;
        std::uint8_t safeZone;
        std::uint8_t action;
    };
    for (const Case& tried : {Case{Client::Scenes::NoItemModel, 0, 0x94}, Case{0x1c16, 0, 0x51}, Case{0x1c37, 0, 0x5b},
                              Case{0x1c16, 1, 0x94}, Case{0x1c37, 1, 0x94}, Case{0x1c14, 0, 0x94}})
    {
        SCOPED_TRACE(tried.pet);
        SCOPED_TRACE(tried.safeZone);
        const auto tested = AKnight(0x41);
        tested->Hero().pet.model = tried.pet;
        tested->Hero().safeZone = tried.safeZone;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x41, ExpectedDirection(TurnsOf256), 0xaa, 0xaa, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, tried.action);
        EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    }
    // La celda del blanco sale de +0x3a8/+0x3ac, no de 0x77e1ba8: el byte 7 la sigue; 0x4e9920 deja pisar el nibble.
    const auto tested = AKnight(0x41);
    tested->Hero().attackPosition = {19450.0f, 11850.0f, 0.0f};
    tested->Movement().selectedSkillRange = 20.0f;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[7], 0xfb);
    EXPECT_EQ(tested->Sent(0)[8], 0xb4);
}

// 0x586419..0x587346: la 0x3e, en el caballo oscuro y fuera de la zona segura, hace la 0x53 con el sonido 0x1ce. Sin
// el caballo, en la zona segura, sin durabilidad (+0xdc6) o en Chaos Castle, nada.
TEST(HeroSkillsKnightTest, Skill3eNeedsTheDarkHorse)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x3e, Lord, map);
        tested->Hero().pet.model = 0x1c16;
        tested->world.OwnCharacter().equipment[8].durability = 30;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + EntryDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x3e, ExpectedDirection(TurnsOf256), 0xaa, 0xaa, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x53);
        EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x1ce);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    for (int variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x3e, Knight, variant == 3 ? 18 : 0);
        tested->Hero().pet.model = variant == 0 ? std::uint16_t{0x1c37} : std::uint16_t{0x1c16};
        tested->Hero().safeZone = variant == 1 ? 1 : 0;
        tested->world.OwnCharacter().equipment[8].durability = variant == 2 ? 0 : 30;
        const CrtRandom expected = Advanced(tested->random, variant >= 2 ? UnpaidDraws : PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
}

// 0x58734b..0x587a88: la 0x30 manda el 19 con la clave del jugador si pasa el freno de 300 ms y, mande o no, hace la
// 0x3f.
TEST(HeroSkillsKnightTest, Skill30SendsThe19AndActs)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x30, Knight, map);
        tested->Hero().movement = 1;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x30, 0x12, 0x34}));
        EXPECT_EQ(tested->Movement().lastActionTick, 10'000u);
        EXPECT_EQ(tested->Movement().sentSkill, 0);
        EXPECT_EQ(tested->Hero().animation.action, 0x3f);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    for (int variant = 0; variant < 2; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x30);
        if (variant == 0)
            tested->Movement().lastActionTick = 9'800;
        else
            Rename(tested->Hero(), "webzen");
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 0x3f);
    }
    // Sin GetTickCount, el freno corta.
    const auto tested = AKnight(0x30);
    auto context = tested->Context();
    context.tickCount = nullptr;
    const auto result = UseTheKnightSkill(tested->Hero(), Knight, 0x30, context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5873ba")) << result.Error();
}

// 0x584d73.., 0x5854fd.. y 0x585c8a..: las 0x3f, 0x40 y 0x4b mandan el 19 y dejan +0x378 en 0, sin acción. La 0x3f
// pide a alguien en la party (0x78aba48). Las tres siguen sin arma derecha.
TEST(HeroSkillsKnightTest, TheLordSkillsSendThe19)
{
    for (const std::uint8_t skill : {std::uint8_t{0x3f}, std::uint8_t{0x40}, std::uint8_t{0x4b}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AKnight(skill, Lord, map);
            tested->Hero().hands[0].model = Client::Scenes::NoItemModel;
            tested->world.Runtime().hud.partyCount = 2;
            tested->Hero().movement = 1;
            const CrtRandom expected = Advanced(tested->random, PaidDraws + EntryDraws + C3Draws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 1);
            EXPECT_EQ(tested->Hero().movement, 0);
        }
    }
    const auto tested = AKnight(0x3f, Lord);
    const CrtRandom expected = Advanced(tested->random, UnpaidDraws + EntryDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
}

// 0x588874..0x5896da: la 0x4c, con Fenrir en el héroe publicado. En alcance y con elegido: la espera, +0x434, el 1E con
// la celda de 0x4e9920 en el byte 8 y la clave de la magia (0x57cf90) en el 11 y en +0x1bd, +0x378 en 0 y la acción de
// las manos (0x4e9c50: solo la derecha, 0x64).
TEST(HeroSkillsKnightTest, Skill4cSendsTheMagicKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x4c, Knight, map);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().magicKeyCounter = 0x32;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + EntryDraws + CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x4c, ExpectedDirection(TurnsOf256), 0, 0xaa, ChosenKey, 1));
        EXPECT_EQ(tested->Hero().magicKey, 1);
        EXPECT_EQ(tested->Movement().magicKeyCounter, 1);
        EXPECT_EQ(tested->Hero().skillTarget4c, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Hero().animation.action, 0x64);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    {
        // Sin Fenrir, o en Chaos Castle, no se puede.
        for (const std::int32_t map : {0, 18})
        {
            const auto tested = AKnight(0x4c, Knight, map);
            tested->Hero().pet.model = map == 0 ? std::uint16_t{0x1c16} : std::uint16_t{0x1c37};
            const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
        }
    }
    {
        // En alcance sin elegido: +0x434 en −1, sin la espera ni +0x378.
        const auto tested = AKnight(0x4c);
        tested->Hero().pet.model = 0x1c37;
        tested->Hero().movement = 1;
        tested->Movement().weaponSkill.target = -1;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().skillTarget4c, -1);
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_TRUE(tested->sent.empty());
    }
}

// 0x5896ea..0x589779: la 0x4c fuera de alcance camina hasta alcance · 1,2 del blanco (con las celdas leídas) y pone
// +0x378 en 1; sin elegido, el otro elegido en −1.
TEST(HeroSkillsKnightTest, Skill4cOutOfRangeWalks)
{
    {
        const auto tested = AKnight(0x4c);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().selectedSkillRange = 2.0f;
        tested->Hero().attackPosition = {19050.0f, 11550.0f, 0.0f};
        tested->Movement().targetX = 190;
        tested->Movement().targetY = 115;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        ASSERT_GT(tested->Hero().path.count, 0);
        const auto last = tested->Hero().path.cells[tested->Hero().path.count - 1u];
        EXPECT_GE(last.x, 188);
        EXPECT_EQ(last.y, 115);
    }
    {
        const auto tested = AKnight(0x4c);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().selectedSkillRange = 2.0f;
        tested->Movement().weaponSkill.target = -1;
        tested->world.Runtime().selectedTargets[4] = 2;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// 0x581912..0x58197a: sin arma derecha, o con un escudo en una mano, la 0x29 no sigue: solo el maná y el registro.
// Otro tipo que no es un jugador, tampoco.
TEST(HeroSkillsKnightTest, TheHandsAndTheTypeGateTheSkill)
{
    for (int variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x29);
        if (variant == 0)
            tested->Hero().hands[0].model = Client::Scenes::NoItemModel;
        else if (variant == 1)
            tested->Hero().hands[1].model = 0xd00;
        else if (variant == 2)
            tested->Hero().hands[0].model = 0xc12;
        else
            tested->Hero().type = 0x1f;
        const CrtRandom expected = Advanced(tested->random, EntryDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
    // 0xe12 ya no es escudo.
    const auto tested = AKnight(0x29);
    tested->Hero().hands[1].model = 0xe12;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 1u);
}

// 0x581b70..0x58236f: sin el maná, 0x558110(3) busca la poción y no se usa (con ella, 0x5af520 corta: hito 5); sin la
// stamina o la energía, no se usa (sin 0x552c50 con la stamina); el Dark Lord sin el liderazgo termina.
TEST(HeroSkillsKnightTest, TheCostsGateTheSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x29, Knight, map);
        tested->skills = SkillsWith({{.skill = 0x29, .mana = 101}});
        const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x581b9e")) << result.Error();
        EXPECT_TRUE(Contains(result.Error(), "hito 5")) << result.Error();
    }
    {
        const auto tested = AKnight(0x29);
        tested->skills = SkillsWith({{.skill = 0x29, .mana = 100, .stamina = 101}});
        const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
    {
        // 0x551f40: 10 · 70 · 4 / 100 + 20 = 48, contra +0x30 + +0x1e.
        const auto tested = AKnight(0x29);
        tested->skills = SkillsWith({{.skill = 0x29, .level = 10, .energy = 70}});
        tested->world.OwnCharacter().energy = 40;
        tested->world.OwnCharacter().word30 = 7;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        tested->world.OwnCharacter().word30 = 8;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    {
        // 0x5521f0: el Dark Lord con +0x2e por encima de +0x36 + +0x20; el caballero no lo mira.
        for (const std::uint8_t heroClass : {Lord, Knight})
        {
            SCOPED_TRACE(heroClass);
            const auto tested = AKnight(0x29, heroClass);
            tested->skills = SkillsWith({{.skill = 0x29, .leadership = 30}});
            tested->world.OwnCharacter().leadership = 29;
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->sent.size(), heroClass == Lord ? 0u : 1u);
        }
    }
}

// 0x58238c: fuera de la tabla (la 0x13) o con una que salta a 0x5854be (la 0x31), nada después de pagar. En Chaos
// Castle la 0x31 ni se puede.
TEST(HeroSkillsKnightTest, TheOtherSkillsDoNothing)
{
    for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x31}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AKnight(skill);
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
    }
    const auto tested = AKnight(0x31, Knight, 23);
    const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x4e9920: arriba ((y) − (blancoY) + 8) << 4 y abajo (x) − (blancoX) + 8, sin enmascarar el de abajo (Q-C1232).
TEST(HeroSkillsKnightHelpersTest, TheRelativeCellBleedsIntoTheHighNibble)
{
    EXPECT_EQ(Client::Scenes::RelativeTargetCell(18250.0f, 11550.0f, 18450, 11750), 0xaa);
    EXPECT_EQ(Client::Scenes::RelativeTargetCell(18250.0f, 11550.0f, 18250, 11550), 0x88);
    // X − blancoX = +9 (en celdas negativas): el 0x11 de abajo prende el bit 4 del 0x80 de arriba.
    EXPECT_EQ(Client::Scenes::RelativeTargetCell(17350.0f, 11550.0f, 18250, 11550), 0x91);
}

// 0x4e9c50: las dos manos 0x63, la derecha 0x64, la izquierda 0x65 y ninguna 0x62.
TEST(HeroSkillsKnightHelpersTest, TheHandsChooseTheAction)
{
    Client::Scenes::ActorState actor = Client::Tests::UnarmedPlayer(1);
    EXPECT_EQ(Client::Scenes::ActionByTheHands(actor), 0x62);
    actor.hands[1].model = 0xc12;
    EXPECT_EQ(Client::Scenes::ActionByTheHands(actor), 0x65);
    actor.hands[0].model = Sword;
    EXPECT_EQ(Client::Scenes::ActionByTheHands(actor), 0x63);
    actor.hands[1].model = Client::Scenes::NoItemModel;
    EXPECT_EQ(Client::Scenes::ActionByTheHands(actor), 0x64);
}

// 0x57cf90: de 1 a 0x32 y otra vez 1.
TEST(HeroSkillsKnightHelpersTest, TheMagicKeyWrapsAfter32)
{
    Client::Scenes::GameMovementState state;
    std::uint8_t key = 0;
    EXPECT_EQ(Client::Scenes::NextMagicKey(state, key), 1);
    EXPECT_EQ(key, 1);
    state.magicKeyCounter = 0x31;
    EXPECT_EQ(Client::Scenes::NextMagicKey(state, key), 0x32);
    EXPECT_EQ(Client::Scenes::NextMagicKey(state, key), 1);
    EXPECT_EQ(state.magicKeyCounter, 1);
}

// 0x551f40: la energía, 0 con +0x2c en 0 y si no (+0x20 · +0x2c · 4) / 100 + 20. Abre y cierra la entrada: 60 rand().
TEST(HeroSkillsKnightHelpersTest, TheEnergyComesFromSkillBmd)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    CrtRandom random;
    const auto skills = SkillsWith(
        {{.skill = 0x29, .range = 3, .mana = 0x123, .stamina = 0x45, .level = 7, .energy = 9}, {.skill = 0x2a}});
    const Client::Scenes::SkillDelayContext context{.world = *world, .random = random, .skills = &skills};
    const CrtRandom expected = Advanced(random, 2 * EntryDraws);
    const auto energy = Client::Scenes::SkillEnergyRequirement(0x29, context);
    ASSERT_TRUE(energy.HasValue()) << energy.Error();
    EXPECT_EQ(energy.Value(), 7 * 9 * 4 / 100 + 20);
    const auto none = Client::Scenes::SkillEnergyRequirement(0x2a, context);
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_EQ(none.Value(), 0);
    EXPECT_EQ(random.State(), expected.State());
    const Client::Scenes::SkillDelayContext missing{.world = *world, .random = random};
    EXPECT_FALSE(Client::Scenes::SkillEnergyRequirement(0x29, missing).HasValue());
}
} // namespace
