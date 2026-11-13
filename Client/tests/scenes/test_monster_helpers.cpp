#include "Scenes/MonsterHelpers.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Sprites.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los helpers de los monstruos (0x441400, 0x434180, 0x4271d0 y 0x432bd0, CL9 hito 2, fase 6d): vectores literales del
// ASM c32bbd5f, sin ejecutar el original. Los huesos propios no giran y el k está en (0, 0, k): el punto de un hueso
// con nombre deja ver de qué hueso sale. En Lorencia (0), Devias (2) y Crywolf (34).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::MonsterHelperPass;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

constexpr std::size_t Slot = 7;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Crywolf = 34;
const glm::vec3 Origin{100.0f, 200.0f, 10.0f};
const glm::vec3 RedEyes{0.9f, 0.2f, 0.1f};

// Un esqueleto de veinte huesos sin padre y ocho acciones de diez keys, cada una a 0,5 por paso.
class SwingModels final : public Client::Scenes::IModelLibrary
{
public:
    SwingModels()
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.actions.assign(8, std::vector<BoneKey>(10));
        m_skeleton.bones.assign(20, bone);
        m_skeleton.actions.assign(8, ActionInfo{.repeats = true, .speed = 0.5f, .keyCount = 10});
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }

private:
    Client::Render::Models::Skeleton m_skeleton;
};

class MonsterHelpersTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_world->FaunaResources().Bind(&m_models);
    }

    void TearDown() override
    {
        m_world->FaunaResources().Bind(nullptr);
    }

    ActorState& Monster(std::uint16_t type, std::uint8_t action, float frame = 0.0f)
    {
        ActorState& actor = m_world->Actors()[Slot];
        actor.alive = true;
        actor.type = type;
        actor.animation.action = action;
        actor.animation.frame = frame;
        actor.position = Origin;
        actor.scale = 1.0f;
        actor.light = {0.1f, 0.2f, 0.3f};
        actor.bodyPose.assign(120, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < actor.bodyPose.size(); ++bone)
            actor.bodyPose[bone][3] = glm::vec3(0.0f, 0.0f, static_cast<float>(bone));
        return actor;
    }

    void Name(std::string_view name, std::uint32_t bone)
    {
        Client::Scenes::NameABone(m_world->Runtime(), Slot, name, bone);
    }

    MonsterHelperPass Pass(std::int32_t map, CrtRandom& random, bool originalFrame = true)
    {
        return {.actor = m_world->Actors()[Slot],
                .slot = Slot,
                .world = *m_world,
                .random = random,
                .sprites = &m_sprites,
                .worldClock = 0.0f,
                .map = map,
                .originalFrame = originalFrame};
    }

    std::vector<Client::World::Sprite> LiveSprites()
    {
        std::vector<Client::World::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    std::optional<std::int32_t> Sound() const
    {
        return m_world->Runtime().lastSoundId;
    }

    SwingModels m_models;
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::World::FrameSprites m_sprites;
};

bool CutAt(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

bool CutAt(const Client::Core::Expected<bool, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

glm::vec3 At(std::uint32_t bone, const glm::vec3& offset = {})
{
    return Origin + glm::vec3(0.0f, 0.0f, static_cast<float>(bone)) + offset;
}

// Fuera de sus tipos, ninguno toca nada: ni rand(), ni +4, ni sprites. El 0x1a8 sale de 0x441400 sin la cola, y la
// cadena de 0x434180 es solo del mapa 34.
TEST_F(MonsterHelpersTest, OtherTypesAndMapsDoNothing)
{
    for (const auto& [type, map] :
         {std::pair{std::uint16_t{0x15b}, Lorencia}, std::pair{std::uint16_t{0x15e}, Devias},
          std::pair{std::uint16_t{0x1a8}, Lorencia}, std::pair{std::uint16_t{0x1ab}, Lorencia},
          std::pair{std::uint16_t{0x204}, Devias}, std::pair{std::uint16_t{0x1ac}, Crywolf}})
    {
        SCOPED_TRACE(type);
        ActorState& actor = Monster(type, 0);
        actor.subtype = 1;
        CrtRandom random(1);
        MonsterHelperPass pass = Pass(map, random);
        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(actor.subtype, 1);
        EXPECT_EQ(random.State(), 1u);
        EXPECT_TRUE(LiveSprites().empty());
        EXPECT_FALSE(Sound().has_value());
    }
}

// 0x441440..0x44164b: los ojos de Monster81, el derecho primero; en la 2, rand() % 15 en 0 pide 0x1f5 más rand() % 2.
TEST_F(MonsterHelpersTest, The1a3LightsItsEyesAndGrowlsWhenIdle)
{
    Name("Monster81_EyeRight", 29);
    Name("Monster81_EyeLeft", 30);
    ActorState& actor = Monster(0x1a3, 2);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 15 == 0; });
    CrtRandom random(seed);
    MonsterHelperPass pass = Pass(Lorencia, random);

    const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(drawn.Value());
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].position, At(29));
    EXPECT_EQ(sprites[1].position, At(30));
    EXPECT_FLOAT_EQ(sprites[1].scale, 0.5f);
    EXPECT_EQ(sprites[1].light, RedEyes);
    EXPECT_EQ(sprites[1].blend, 0);
    CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    EXPECT_EQ(Sound(), 0x1f5 + (expected.Next() & 1));
    EXPECT_EQ(random.State(), AfterDraws(seed, 2).State());
    EXPECT_EQ(actor.subtype, 0);
}

// 0x4415fd..0x44164b y 0x442dd2: el ataque suena una vez (+4 en 1) y la acción 0 lo vuelve a 0. Entre frames del
// original, el pedido no queda.
TEST_F(MonsterHelpersTest, The1a3AttackSoundsOnceUntilItRests)
{
    Name("Monster81_EyeRight", 29);
    Name("Monster81_EyeLeft", 30);
    ActorState& actor = Monster(0x1a3, 3);
    CrtRandom random(5);
    MonsterHelperPass pass = Pass(Devias, random);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass).HasValue());
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), 0x1f7 + (CrtRandom(5).Next() & 1));
    EXPECT_EQ(random.State(), AfterDraws(5, 1).State());

    m_world->Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass).HasValue());
    EXPECT_FALSE(Sound().has_value());
    EXPECT_EQ(random.State(), AfterDraws(5, 1).State());

    actor.animation.action = 6;
    actor.subtype = 0;
    MonsterHelperPass between = Pass(Devias, random, false);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(between).HasValue());
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_FALSE(Sound().has_value());

    actor.animation.action = 0;
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass).HasValue());
    EXPECT_EQ(actor.subtype, 0);
}

// Sin el hueso con nombre que registra el alta (0x401000), 0x4010b0 dejaría el punto sin escribir: corta en la llamada.
TEST_F(MonsterHelpersTest, AMissingNamedBoneCutsAtItsCall)
{
    Name("Monster81_EyeRight", 29);
    Monster(0x1a3, 0);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
    EXPECT_TRUE(CutAt(drawn, "0x441552")) << drawn.Error();
    EXPECT_EQ(LiveSprites().size(), 1u);
}

// 0x441669..0x441774: el 0x1a4 corta en 0x4ee770 al morir y, si no, en sus partículas después del punto de la mano.
TEST_F(MonsterHelpersTest, The1a4CutsInItsEffects)
{
    Name("Monster82_LHand", 34);
    Monster(0x1a4, 6);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x441696"));
    Monster(0x1a4, 0);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x441774"));
    EXPECT_EQ(random.State(), 1u);
}

// 0x441d96..0x441f7f: en la 4, el sonido 0x202 más rand() % 2, tres rand() % 12 − 6 para el punto de la cola, un 0x5fd
// a 2,5 con la mezcla 1 y un 0x61e a 0,8, los dos con la luz propia, y corta en las partículas 0x5ca.
TEST_F(MonsterHelpersTest, The1a5TailGlowsWithARandomOffset)
{
    Name("Monster83_Tail", 62);
    ActorState& actor = Monster(0x1a5, 4);
    CrtRandom random(77);
    MonsterHelperPass pass = Pass(Devias, random);

    const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);

    EXPECT_TRUE(CutAt(drawn, "0x441f7f")) << drawn.Error();
    CrtRandom expected(77);
    EXPECT_EQ(Sound(), 0x202 + (expected.Next() & 1));
    glm::vec3 offset{};
    for (int axis = 0; axis < 3; ++axis)
        offset[axis] = static_cast<float>(expected.Next() % 12 - 6);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(actor.subtype, 1);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].texture, 0x5fd);
    EXPECT_EQ(sprites[0].position, At(62, offset));
    EXPECT_FLOAT_EQ(sprites[0].scale, 2.5f);
    EXPECT_EQ(sprites[0].blend, 1);
    EXPECT_EQ(sprites[0].light, actor.light);
    EXPECT_EQ(sprites[1].texture, 0x61e);
    EXPECT_FLOAT_EQ(sprites[1].scale, 0.8f);
    EXPECT_EQ(sprites[1].blend, 0);
}

// 0x4420f7..0x44281f: en la 3, rand() % 2 en 1 corta en el efecto 0x12e; en 0, otro elige el veneno y rand() % 32 es la
// escala de la partícula. Descansando, tres rand() % 10 sin acertar no hacen nada más que +4 a 0.
TEST_F(MonsterHelpersTest, The1a6PoisonDrawsItsRandomsInOrder)
{
    for (const char* bone : {"Monster84_PoisonTop", "Monster84_PoisonRight", "Monster84_PoisonLeft"})
        Name(bone, 53);
    Name("Monster84_RightHand", 50);
    ActorState& actor = Monster(0x1a6, 3);
    const std::uint32_t odd = FirstSeedWhere([](CrtRandom& random) { return (random.Next() & 1) != 0; });
    CrtRandom first(odd);
    MonsterHelperPass pass = Pass(Lorencia, first);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x442397"));
    EXPECT_EQ(first.State(), AfterDraws(odd, 1).State());
    EXPECT_EQ(Sound(), 0x1fe);
    EXPECT_EQ(actor.subtype, 1);

    const std::uint32_t even = FirstSeedWhere([](CrtRandom& random) { return (random.Next() & 1) == 0; });
    CrtRandom second(even);
    MonsterHelperPass poisoned = Pass(Lorencia, second);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(poisoned), "0x442370"));
    EXPECT_EQ(second.State(), AfterDraws(even, 3).State());

    // En la 4 con el frame de 3,5 a 4,2: la mano derecha y rand() % 32.
    actor.animation.action = 4;
    actor.animation.frame = 4.2f;
    actor.subtype = 1;
    CrtRandom third(9);
    MonsterHelperPass attack = Pass(Lorencia, third);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(attack), "0x4424d7"));
    EXPECT_EQ(third.State(), AfterDraws(9, 1).State());

    actor.animation.action = 1;
    const std::uint32_t calm =
        FirstSeedWhere([](CrtRandom& random)
                       { return random.Next() % 10 != 0 && random.Next() % 10 != 0 && random.Next() % 10 != 0; });
    CrtRandom resting(calm);
    MonsterHelperPass rest = Pass(Lorencia, resting);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(rest).HasValue());
    EXPECT_EQ(resting.State(), AfterDraws(calm, 3).State());
    EXPECT_EQ(actor.subtype, 0);

    const std::uint32_t right =
        FirstSeedWhere([](CrtRandom& random) { return random.Next() % 10 != 0 && random.Next() % 10 == 0; });
    CrtRandom dripping(right);
    MonsterHelperPass drip = Pass(Lorencia, dripping);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(drip), "0x44274b"));
    EXPECT_EQ(dripping.State(), AfterDraws(right, 2).State());
}

// 0x442824..0x442a5d: los ojos de Monster85 a 0,7; en la 2, rand() % 10 en 0 corta en la partícula 0x5f3.
TEST_F(MonsterHelpersTest, The1a7CutsInItsIdleParticle)
{
    Name("Monster85_LeftEye", 18);
    Name("Monster85_RightEye", 19);
    Monster(0x1a7, 2);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 10 == 0; });
    CrtRandom random(seed);
    MonsterHelperPass pass = Pass(Devias, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x4429d8"));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].position, At(18));
    EXPECT_FLOAT_EQ(sprites[0].scale, 0.7f);
    EXPECT_EQ(random.State(), AfterDraws(seed, 1).State());
}

// 0x442a62..0x442da1: dos 0x5ac por ojo en (0, −2, 0); en la 4, la primera vez +4 = 1 y el efecto 0x13f; después, la
// mano izquierda y sus partículas.
TEST_F(MonsterHelpersTest, The1a9AttackMarksItsFirstFrame)
{
    Name("Monster87_LeftEye", 8);
    Name("Monster87_RightEye", 9);
    Name("Monster87_LeftHand", 16);
    ActorState& actor = Monster(0x1a9, 4);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x442cb9"));
    EXPECT_EQ(actor.subtype, 1);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 4u);
    EXPECT_EQ(sprites[3].position, At(9, {0.0f, -2.0f, 0.0f}));
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x442da1"));
    EXPECT_EQ(random.State(), 1u);
}

// 0x4343ba..0x434580 en Crywolf: el 0x1ab en la 8 suena 0x233 y devuelve 0 (la cadena sigue); el 0x1b0 en la 2 sortea
// con rand() % 15 y devuelve 1; el 0x1b5 en la 4 suena y vuelve +4 a 0, y en la 3 lo deja.
TEST_F(MonsterHelpersTest, TheCrywolfMonstersSoundByAction)
{
    ActorState& actor = Monster(0x1ab, 8);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Crywolf, random);
    const auto first = Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_FALSE(first.Value());
    EXPECT_EQ(Sound(), 0x233);
    EXPECT_EQ(actor.subtype, 1);

    Monster(0x1b0, 2);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 15 == 0; });
    CrtRandom idle(seed);
    MonsterHelperPass idling = Pass(Crywolf, idle);
    const auto second = Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(idling);
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_TRUE(second.Value());
    CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    EXPECT_EQ(Sound(), 0x224 + (expected.Next() & 1));
    EXPECT_EQ(idle.State(), expected.State());

    actor.type = 0x1b5;
    actor.animation.action = 4;
    actor.subtype = 0;
    m_world->Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(Sound(), 0x241);
    EXPECT_EQ(actor.subtype, 0);
    actor.animation.action = 3;
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(random.State(), 1u);
}

// 0x4341d2..0x43436c: del 0x204 al 0x208, con +0x7c & 0x40000 la luz propia y las partículas 0x729; después, con o
// sin ellas, las marcas del piso.
TEST_F(MonsterHelpersTest, TheCrywolfAltarsWriteTheirLightBeforeTheirCut)
{
    ActorState& actor = Monster(0x206, 0);
    actor.flags7c = 0x40000;
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Crywolf, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), "0x434281"));
    EXPECT_EQ(actor.light, glm::vec3(0.09f, 0.09f, 0.04f));
    actor.flags7c = 0;
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), "0x43436c"));
}

// 0x434602..0x434a70: el 0x1af suena y después saca rand() % 14, rand() % 360 y rand() % 2 para sus siete sprites.
TEST_F(MonsterHelpersTest, The1afGlowsAfterItsSounds)
{
    Name("Monster94_zx", 27);
    Name("Monster94_zx01", 28);
    const std::uint32_t seed = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            static_cast<void>(random.Next());
            return (random.Next() & 1) == 0;
        });
    Monster(0x1af, 0);
    CrtRandom random(seed);
    MonsterHelperPass pass = Pass(Crywolf, random);

    const auto drawn = Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(drawn.Value());
    CrtRandom expected(seed);
    const float a = static_cast<float>(static_cast<double>(expected.Next() % 14) * static_cast<double>(0.01f));
    const auto spin = static_cast<float>(expected.Next() % 360);
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 8u);
    EXPECT_EQ(sprites[0].texture, 0x699);
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f));
    EXPECT_FLOAT_EQ(sprites[1].scale, static_cast<float>(static_cast<double>(a) * 5.0 + 3.5));
    EXPECT_EQ(sprites[4].texture, 0x69a);
    EXPECT_FLOAT_EQ(sprites[4].rotation, spin);
    EXPECT_EQ(sprites[5].position, At(28));
    EXPECT_EQ(sprites[5].light, glm::vec3(0.1f, 0.0f, 0.6f));
    EXPECT_FLOAT_EQ(sprites[6].rotation, static_cast<float>(360.0 - static_cast<double>(spin)));
    EXPECT_EQ(sprites[7].texture, 0x5ac);
    EXPECT_FLOAT_EQ(sprites[7].scale, 1.5f);
}

// 0x434bf3..0x434e2e y 0x434f8a..0x4354a1: la cabeza del 0x1b1 fuera de la 6 y su partícula de la 2; el centro y las
// puntas del 0x1b2, la mitad en la 3 y la 4, con el seno del reloj.
TEST_F(MonsterHelpersTest, The1b1And1b2DrawTheirSprites)
{
    Name("Monster95_Head", 6);
    ActorState& actor = Monster(0x1b1, 2);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 10 == 0; });
    CrtRandom random(seed);
    MonsterHelperPass pass = Pass(Crywolf, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass), "0x434d84"));
    ASSERT_EQ(LiveSprites().size(), 1u);
    EXPECT_FLOAT_EQ(LiveSprites()[0].scale, 3.5f);
    EXPECT_EQ(LiveSprites()[0].light, RedEyes);

    actor.animation.action = 6;
    m_sprites.Clear();
    const auto died = Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass);
    ASSERT_TRUE(died.HasValue()) << died.Error();
    EXPECT_TRUE(LiveSprites().empty());
    EXPECT_EQ(Sound(), 0x214);

    Name("Monster96_Center", 28);
    Name("Monster96_Top", 27);
    Name("Monster96_Bottom", 29);
    actor.type = 0x1b2;
    actor.animation.action = 3;
    actor.subtype = 0;
    CrtRandom spinning(3);
    MonsterHelperPass glow = Pass(Crywolf, spinning);
    glow.worldClock = 1000.0f;
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(glow).HasValue());
    const auto spin = static_cast<float>(CrtRandom(3).Next() % 360);
    const float lum = static_cast<float>(std::sin(1000.0 * static_cast<double>(0.0012f)) * static_cast<double>(0.8f) +
                                         static_cast<double>(1.3f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 5u);
    EXPECT_FLOAT_EQ(sprites[0].scale, 0.5f);
    EXPECT_EQ(sprites[0].light, glm::vec3(0.0f, static_cast<float>(static_cast<double>(lum) * 0.5),
                                          static_cast<float>(static_cast<double>(lum) * static_cast<double>(0.1f))));
    EXPECT_EQ(sprites[4].position, At(29));
    EXPECT_FLOAT_EQ(sprites[4].scale, 0.25f);
    EXPECT_FLOAT_EQ(sprites[3].rotation, spin);
    EXPECT_FLOAT_EQ(sprites[4].rotation, static_cast<float>(360.0 - static_cast<double>(spin)));
    EXPECT_EQ(Sound(), 0x221);
    EXPECT_EQ(spinning.State(), AfterDraws(3, 1).State());
}

// 0x434e3f..0x434ecf: el 0x1b3 en la 4 con el frame de 4 a 6 corta en el efecto 0x115, antes de sonar.
TEST_F(MonsterHelpersTest, The1b3CutsInItsAttackWindow)
{
    ActorState& actor = Monster(0x1b3, 4, 6.0f);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Crywolf, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass), "0x434ecf"));
    EXPECT_FALSE(Sound().has_value());
    actor.animation.frame = 6.5f;
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(Sound(), 0x21d);
}

// 0x42720d..0x42769f: el 0x1b6 saca rand() % 30 para la luz de sus cinco 0x5ad y corta en la partícula del pie.
TEST_F(MonsterHelpersTest, The1b6GlowsThenCutsAtItsFoot)
{
    Name("Monster100_L_Hand", 76);
    for (const auto& [bone, index] :
         {std::pair{"Monster100_z02", 107u}, std::pair{"Monster100_z03", 108u}, std::pair{"Monster100_z04", 109u},
          std::pair{"Monster100_z05", 110u}, std::pair{"Monster100_Footstepst", 0u}})
        Name(bone, index);
    Monster(0x1b6, 0);
    CrtRandom random(11);
    MonsterHelperPass pass = Pass(Devias, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), "0x42769f"));
    const double v =
        static_cast<double>(CrtRandom(11).Next() % 30) * static_cast<double>(0.01f) + static_cast<double>(0.6f);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 5u);
    EXPECT_EQ(sprites[0].light,
              glm::vec3(static_cast<float>(v + 0.5), static_cast<float>(v - static_cast<double>(0.05f)),
                        static_cast<float>(v + 0.5)));
    EXPECT_EQ(sprites[3].position, At(109));
    EXPECT_FLOAT_EQ(sprites[3].scale, 1.7f);
    EXPECT_EQ(random.State(), AfterDraws(11, 1).State());
}

// 0x42822a..0x4282e5 y 0x428dbd: el 0x1b9 solo suena; en Lorencia y en Devias.
TEST_F(MonsterHelpersTest, The1b9OnlySounds)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        ActorState& actor = Monster(0x1b9, 3);
        actor.subtype = 0;
        CrtRandom random(1);
        MonsterHelperPass pass = Pass(map, random);
        ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(pass).HasValue());
        EXPECT_EQ(Sound(), 0x258);
        EXPECT_EQ(actor.subtype, 1);
        actor.animation.action = 1;
        ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(pass).HasValue());
        EXPECT_EQ(actor.subtype, 0);
        EXPECT_EQ(random.State(), 1u);
    }
}

// 0x427ab0, 0x42803b, 0x4282ea y 0x4285b1: el 0x1b7, el 0x1b8, el 0x1ba y el 0x1bb cortan en su partícula, después de
// sus rand(), sus sprites y (el 0x1ba y el 0x1bb) sus sonidos.
TEST_F(MonsterHelpersTest, The1b7To1bbCutInTheirParticles)
{
    Name("Monster101_L_Arm", 12);
    Name("Monster102_Head", 6);
    Name("Monster104_Horn0", 37);
    for (const char* bone : {"Monster105_R_Eye", "Monster105_L_Eye", "Monster105_L_Arm00", "Monster105_L_Arm01",
                             "Monster105_L_Arm02", "Monster105_L_Hand"})
        Name(bone, 20);
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        std::string_view address;
        std::size_t sprites;
        int draws;
        std::optional<std::int32_t> sound;
    };
    for (const Case& value : {Case{0x1b7, 0, "0x427b8b", 0, 0, std::nullopt},
                              Case{0x1b8, 3, "0x428164", 1, 1, std::nullopt}, Case{0x1ba, 4, "0x42849c", 1, 1, 0x254},
                              Case{0x1bb, 4, "0x428adc", 5, 1, 0x25e}, Case{0x1bb, 8, "0x428c52", 5, 1, 0x25d}})
    {
        SCOPED_TRACE(value.address);
        m_sprites.Clear();
        m_world->Runtime().lastSoundId.reset();
        Monster(value.type, value.action).subtype = 0;
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(Lorencia, random);
        EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), value.address));
        EXPECT_EQ(LiveSprites().size(), value.sprites);
        EXPECT_EQ(random.State(), AfterDraws(21, value.draws).State());
        EXPECT_EQ(Sound(), value.sound);
    }
    // El 0x1b8: (0; rand() % 10 · 0,01f + 0,2f; 0) en el punto (60, −30, 0) de la cabeza.
    m_sprites.Clear();
    Monster(0x1b8, 0);
    CrtRandom random(21);
    MonsterHelperPass pass = Pass(Devias, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), "0x428164"));
    const float green = static_cast<float>(static_cast<double>(CrtRandom(21).Next() % 10) * static_cast<double>(0.01f) +
                                           static_cast<double>(0.2f));
    ASSERT_EQ(LiveSprites().size(), 1u);
    EXPECT_EQ(LiveSprites()[0].light, glm::vec3(0.0f, green, 0.0f));
    EXPECT_EQ(LiveSprites()[0].position, At(6, {60.0f, -30.0f, 0.0f}));
    EXPECT_FLOAT_EQ(LiveSprites()[0].scale, 3.0f);
}

// 0x432bf1: el rand() sin condición, con cualquier tipo; el 0x1ac, el 0x1ad y el 0x1b2 no hacen nada más.
TEST_F(MonsterHelpersTest, TheSwingStepDrawsItsRandomWithAnyType)
{
    for (const std::uint16_t type :
         {std::uint16_t{0x15b}, std::uint16_t{0x20a}, std::uint16_t{0x1ac}, std::uint16_t{0x1b2}, std::uint16_t{0x1b0}})
    {
        SCOPED_TRACE(type);
        ActorState& actor = Monster(type, 3, 4.0f);
        CrtRandom random(1);
        const auto stepped = Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), AfterDraws(1, 1).State());
    }
    EXPECT_TRUE(m_world->PoseCarry().models.empty());
}

// 0x432c12..0x432db0: el 0x1b0 en la 4 fuera de (5, 7) posa diez veces en el buffer global, desde el frame menos la
// velocidad de la 4 y con un décimo de ella de paso, con la z de los ángulos 18 grados menos por vuelta; saca dos
// rand() por vuelta y deja los ángulos del objeto como estaban.
TEST_F(MonsterHelpersTest, The1b0SwingPosesTenTimes)
{
    for (const float frame : {3.0f, 7.0f})
    {
        SCOPED_TRACE(frame);
        m_world->PoseCarry() = {};
        ActorState& actor = Monster(0x1b0, 4, frame);
        actor.angles = {0.0f, 0.0f, 90.0f};
        CrtRandom random(1);
        const auto stepped = Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), AfterDraws(1, 21).State());
        EXPECT_EQ(actor.angles, glm::vec3(0.0f, 0.0f, 90.0f));
        const auto& carry = m_world->PoseCarry();
        ASSERT_EQ(carry.models.count(0x1b0), 1u);
        EXPECT_FLOAT_EQ(carry.models.at(0x1b0).angles.z, 90.0f - 18.0f * 9.0f);
        EXPECT_EQ(carry.models.at(0x1b0).position, Origin);
        EXPECT_EQ(carry.bones.size(), 20u);
        // La última pose: el frame menos 0,5 más nueve pasos de 0,05f.
        float pose = frame - 0.5f;
        for (int turn = 0; turn < 9; ++turn)
            pose = static_cast<float>(static_cast<double>(pose) + static_cast<double>(0.05f));
        EXPECT_EQ(m_world->FaunaResources().PoseKey(0x1b0), static_cast<std::int16_t>(pose));
    }
}

// Con el frame del objeto en (5, 7), la primera pose y la partícula 0x5d9; las estelas de los demás cortan después de
// su primera pose, y el 0x1ae en la 4 entre 4,5 y 5 antes de posar. En Lorencia y en Devias da lo mismo.
TEST_F(MonsterHelpersTest, TheOtherSwingsCutAfterTheirFirstPose)
{
    Name("Left_Hand", 17);
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::string_view address;
        bool posed;
    };
    for (const Case& value : {Case{0x1b0, 4, 6.0f, "0x432d07", true}, Case{0x1ab, 3, 4.0f, "0x433056", true},
                              Case{0x1ab, 3, 4.5f, "0x433086", true}, Case{0x1ae, 8, 4.0f, "0x433248", false},
                              Case{0x1ae, 4, 4.7f, "0x433339", false}, Case{0x1ae, 3, 4.7f, "0x433410", true},
                              Case{0x1af, 4, 3.0f, "0x433557", true}, Case{0x1b1, 4, 3.0f, "0x4337cf", true},
                              Case{0x1b3, 3, 3.0f, "0x43397d", true}, Case{0x1b4, 3, 3.0f, "0x432ea3", true}})
    {
        SCOPED_TRACE(value.address);
        m_world->PoseCarry() = {};
        m_world->Runtime().map = value.type % 2 == 0 ? Lorencia : Devias;
        ActorState& actor = Monster(value.type, value.action, value.frame);
        CrtRandom random(1);
        const auto stepped = Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random);
        EXPECT_TRUE(CutAt(stepped, value.address)) << (stepped.HasValue() ? "" : stepped.Error());
        EXPECT_EQ(random.State(), AfterDraws(1, 1).State());
        EXPECT_EQ(m_world->PoseCarry().models.count(value.type), value.posed ? 1u : 0u);
    }
}
} // namespace
