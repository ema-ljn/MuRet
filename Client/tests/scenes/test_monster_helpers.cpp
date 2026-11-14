#include "Scenes/MonsterHelpers.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainDecal.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
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
                .particles = m_particles.get(),
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

    // La tabla de 0x53fe70 llena: el original no crea nada ni saca rand().
    void FillParticles()
    {
        for (auto& particle : m_particles->All())
            particle.alive = true;
    }

    void ClearParticles()
    {
        for (auto& particle : m_particles->All())
            particle = {};
        for (auto& particle : m_reference->All())
            particle = {};
    }

    static std::vector<Client::World::Particle> Live(std::span<const Client::World::Particle> all)
    {
        std::vector<Client::World::Particle> live;
        for (const auto& particle : all)
        {
            if (particle.alive)
                live.push_back(particle);
        }
        return live;
    }

    std::vector<Client::World::Particle> LiveParticles() const
    {
        return Live(std::as_const(*m_particles).All());
    }

    // La misma alta en una tabla aparte con el rand() esperado: saca los rand() de la partícula en su orden.
    void Expect(CrtRandom& expected, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                const glm::vec3& angles, const glm::vec3& light, float scale = 1.0f, float clock = 0.0f)
    {
        const auto created = m_reference->CreateChecked(type, position, angles, light, subtype, scale, clock, expected);
        ASSERT_TRUE(created.HasValue());
        EXPECT_TRUE(created.Value().has_value());
    }

    // Las partículas vivas son las de la tabla de referencia, en el mismo orden.
    void ExpectTheParticles() const
    {
        const auto live = LiveParticles();
        const auto reference = Live(std::as_const(*m_reference).All());
        ASSERT_EQ(live.size(), reference.size());
        for (std::size_t index = 0; index < live.size(); ++index)
        {
            SCOPED_TRACE(index);
            EXPECT_EQ(live[index].type, reference[index].type);
            EXPECT_EQ(live[index].subtype, reference[index].subtype);
            EXPECT_EQ(live[index].position, reference[index].position);
            EXPECT_EQ(live[index].angles, reference[index].angles);
            EXPECT_EQ(live[index].light, reference[index].light);
            EXPECT_EQ(live[index].scale, reference[index].scale);
            EXPECT_EQ(live[index].rotation, reference[index].rotation);
            EXPECT_EQ(live[index].life, reference[index].life);
        }
    }

    SwingModels m_models;
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::World::FrameSprites m_sprites;
    std::unique_ptr<Client::World::Particles> m_particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::Particles> m_reference = std::make_unique<Client::World::Particles>();
};

// FLD T; FMUL tasa; FSIN; FMUL factor con un solo R53 (ADR0014), como el original.
double ClockSine(float clock, float rate, float factor)
{
    const auto product =
        Client::Render::Models::Detail::SineProductPC53(static_cast<double>(clock) * static_cast<double>(rate), factor);
    EXPECT_TRUE(product.has_value());
    return product.value_or(0.0);
}

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

// 0x441669..0x441d7d: el 0x1a4 corta en 0x4ee770 al morir y, con lugar en la tabla, en su partícula 0x6ec/3 después
// del punto de la mano. Con la tabla llena sigue: rand() % 20 en 0 pide 0x205 más rand() % 2, y en la 3 de 8,4 a 10,2
// con +4 en 0, +4 = 1 y el corte de sus efectos.
TEST_F(MonsterHelpersTest, The1a4CutsInItsParticleOnlyWithRoom)
{
    Name("Monster82_LHand", 34);
    Monster(0x1a4, 6);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x441696"));
    Monster(0x1a4, 0);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x441774"));
    EXPECT_EQ(random.State(), 1u);

    FillParticles();
    const std::uint32_t growl = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 20 == 0; });
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().lastSoundId.reset();
        Monster(0x1a4, 2);
        CrtRandom full(growl);
        MonsterHelperPass crowded = Pass(map, full);
        const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(crowded);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(growl);
        static_cast<void>(expected.Next());
        EXPECT_EQ(Sound(), 0x205 + (expected.Next() & 1));
        EXPECT_EQ(full.State(), expected.State());
    }
    const std::uint32_t quiet = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 20 != 0; });
    ActorState& actor = Monster(0x1a4, 3, 9.0f);
    actor.subtype = 0;
    CrtRandom attack(quiet);
    MonsterHelperPass attacking = Pass(Devias, attack);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(attacking), "0x441b33"));
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(attack.State(), AfterDraws(quiet, 1).State());
}

// 0x441d96..0x4420f2: en la 4, el sonido 0x202 más rand() % 2, tres rand() % 12 − 6 para el punto de la cola, un 0x5fd
// a 2,5 con la mezcla 1 y un 0x61e a 0,8, los dos con la luz propia, y dos 0x5ca/0 con (1; 0,3; 0); después doce
// vueltas de tres rand() para el punto de la panza, con los mismos dos sprites y una 0x5ca/0. Con la tabla llena, los
// mismos sprites sin las partículas ni sus rand().
TEST_F(MonsterHelpersTest, The1a5TailAndBellyGlowWithTheirThunder)
{
    Name("Monster83_Tail", 62);
    Name("Monster83_Belly", 40);
    const glm::vec3 thunder{1.0f, 0.3f, 0.0f};
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ClearParticles();
        m_sprites.Clear();
        ActorState& actor = Monster(0x1a5, 4);
        actor.subtype = 0;
        actor.angles = {0.0f, 0.0f, 45.0f};
        CrtRandom random(77);
        MonsterHelperPass pass = Pass(map, random);

        const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(77);
        EXPECT_EQ(Sound(), 0x202 + (expected.Next() & 1));
        glm::vec3 offset{};
        for (int axis = 0; axis < 3; ++axis)
            offset[axis] = static_cast<float>(expected.Next() % 12 - 6);
        Expect(expected, 0x5ca, 0, At(62, offset), actor.angles, thunder);
        Expect(expected, 0x5ca, 0, At(62, offset), actor.angles, thunder);
        glm::vec3 belly{};
        for (int turn = 0; turn < 12; ++turn)
        {
            belly.x = static_cast<float>(expected.Next() % 40 - 20);
            belly.y = static_cast<float>(expected.Next() % 40 - 20);
            belly.z = static_cast<float>(expected.Next() % 48 - 24);
            Expect(expected, 0x5ca, 0, At(40, belly), actor.angles, thunder);
        }
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheParticles();
        EXPECT_EQ(LiveParticles().size(), 14u);
        EXPECT_EQ(actor.subtype, 1);
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 26u);
        EXPECT_EQ(sprites[0].texture, 0x5fd);
        EXPECT_EQ(sprites[0].position, At(62, offset));
        EXPECT_FLOAT_EQ(sprites[0].scale, 2.5f);
        EXPECT_EQ(sprites[0].blend, 1);
        EXPECT_EQ(sprites[0].light, actor.light);
        EXPECT_EQ(sprites[1].texture, 0x61e);
        EXPECT_FLOAT_EQ(sprites[1].scale, 0.8f);
        EXPECT_EQ(sprites[1].blend, 0);
        EXPECT_EQ(sprites[24].texture, 0x5fd);
        EXPECT_EQ(sprites[24].position, At(40, belly));
        EXPECT_EQ(sprites[25].texture, 0x61e);
    }

    // Con la tabla llena: 1 + 3 + 12 · 3 rand(), sin partículas nuevas.
    FillParticles();
    m_sprites.Clear();
    Monster(0x1a5, 4).subtype = 0;
    CrtRandom full(77);
    MonsterHelperPass crowded = Pass(Devias, full);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
    EXPECT_EQ(full.State(), AfterDraws(77, 40).State());
    EXPECT_EQ(LiveSprites().size(), 26u);
}

// 0x4420f7..0x44281f: en la 3, rand() % 2 en 1 corta en el efecto 0x12e; en 0, otro elige el veneno y rand() % 32 + 50
// por 0,025f es la escala de su 0x5f2/11, antes del mismo corte. En la 4 con el frame de 3,5 a 4,2, la 0x5f2/11 de cada
// mano en el piso, de escala rand() % 32 + 50 por 0,05f. Descansando, tres rand() % 10 sin acertar no hacen nada más
// que +4 a 0; uno que acierta corta en su 0x5f2/0x17 solo con lugar en la tabla.
TEST_F(MonsterHelpersTest, The1a6PoisonDrawsItsRandomsInOrder)
{
    for (const char* bone : {"Monster84_PoisonTop", "Monster84_PoisonRight", "Monster84_PoisonLeft"})
        Name(bone, 53);
    Name("Monster84_RightHand", 50);
    Name("Monster84_LeftHand", 51);
    const glm::vec3 poison{0.4f, 1.0f, 0.7f};
    ActorState& actor = Monster(0x1a6, 3);
    actor.bodyPose[51][3] = glm::vec3(7.0f, 0.0f, 51.0f);
    const std::uint32_t odd = FirstSeedWhere([](CrtRandom& random) { return (random.Next() & 1) != 0; });
    CrtRandom first(odd);
    MonsterHelperPass pass = Pass(Lorencia, first);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x442397"));
    EXPECT_EQ(first.State(), AfterDraws(odd, 1).State());
    EXPECT_EQ(Sound(), 0x1fe);
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_TRUE(LiveParticles().empty());

    const std::uint32_t even = FirstSeedWhere([](CrtRandom& random) { return (random.Next() & 1) == 0; });
    CrtRandom second(even);
    MonsterHelperPass poisoned = Pass(Lorencia, second);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(poisoned), "0x442397"));
    CrtRandom expected(even);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    const auto dusty = static_cast<float>(static_cast<double>(expected.Next() % 32 + 50) * static_cast<double>(0.025f));
    Expect(expected, 0x5f2, 11, At(53), actor.angles, poison, dusty);
    EXPECT_EQ(second.State(), expected.State());
    ExpectTheParticles();
    ASSERT_EQ(LiveParticles().size(), 1u);
    EXPECT_EQ(LiveParticles()[0].scale, dusty);

    // En la 4 con el frame de 3,5 a 4,2: 0x1fc más rand() % 2 y la 0x5f2/11 de cada mano, en la z del objeto.
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ClearParticles();
        actor.animation.action = 4;
        actor.animation.frame = 4.2f;
        actor.subtype = 0;
        const std::uint32_t dry = FirstSeedWhere(
            [](CrtRandom& random)
            {
                for (int draw = 0; draw < 17; ++draw)
                    static_cast<void>(random.Next());
                return random.Next() % 10 != 0 && random.Next() % 10 != 0 && random.Next() % 10 != 0;
            });
        CrtRandom third(dry);
        MonsterHelperPass attack = Pass(map, third);
        const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(attack);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom hands(dry);
        EXPECT_EQ(Sound(), 0x1fc + (hands.Next() & 1));
        for (const glm::vec3& floor : {Origin, Origin + glm::vec3(7.0f, 0.0f, 0.0f)})
        {
            const auto scale =
                static_cast<float>(static_cast<double>(hands.Next() % 32 + 50) * static_cast<double>(0.05f));
            Expect(hands, 0x5f2, 11, floor, actor.angles, poison, scale);
        }
        for (int drop = 0; drop < 3; ++drop)
            static_cast<void>(hands.Next());
        EXPECT_EQ(third.State(), hands.State());
        EXPECT_EQ(third.State(), AfterDraws(dry, 20).State());
        ExpectTheParticles();
        EXPECT_EQ(LiveParticles().size(), 2u);
        EXPECT_EQ(actor.subtype, 1);
    }

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

    // Con la tabla llena, el veneno derecho no crea nada y sigue con el izquierdo.
    FillParticles();
    CrtRandom crowded(right);
    MonsterHelperPass full = Pass(Devias, crowded);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(full).HasValue());
    EXPECT_EQ(crowded.State(), AfterDraws(right, 3).State());
}

// 0x442824..0x442a5d: los ojos de Monster85 a 0,7; en la 2, rand() % 10 en 0 es la partícula 0x5f3/0 en la posición,
// con los ángulos del objeto y la luz de los ojos, y el sonido 0x1f0 más rand() % 2. Con la tabla llena, el sonido sin
// los rand() de la partícula.
TEST_F(MonsterHelpersTest, The1a7RaisesDustWhenIdle)
{
    Name("Monster85_LeftEye", 18);
    Name("Monster85_RightEye", 19);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 10 == 0; });
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ClearParticles();
        m_sprites.Clear();
        ActorState& actor = Monster(0x1a7, 2);
        actor.angles = {0.0f, 0.0f, 30.0f};
        CrtRandom random(seed);
        MonsterHelperPass pass = Pass(map, random);
        const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 2u);
        EXPECT_EQ(sprites[0].position, At(18));
        EXPECT_FLOAT_EQ(sprites[0].scale, 0.7f);
        CrtRandom expected(seed);
        static_cast<void>(expected.Next());
        Expect(expected, 0x5f3, 0, Origin, actor.angles, RedEyes);
        EXPECT_EQ(Sound(), 0x1f0 + (expected.Next() & 1));
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheParticles();
        EXPECT_EQ(LiveParticles().size(), 1u);
    }
    FillParticles();
    Monster(0x1a7, 10);
    CrtRandom full(seed);
    MonsterHelperPass crowded = Pass(Devias, full);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
    CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    EXPECT_EQ(Sound(), 0x1f0 + (expected.Next() & 1));
    EXPECT_EQ(full.State(), expected.State());
}

// 0x442a62..0x442da1: dos 0x5ac por ojo en (0, −2, 0); en la 4, la primera vez +4 = 1 y el efecto 0x13f; después, la
// mano izquierda y su partícula 0x6ec/3, que corta solo con lugar en la tabla.
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
    // Con la tabla llena, la 0x6ec/3 y la 0x5f2/21 no crean nada.
    FillParticles();
    MonsterHelperPass crowded = Pass(Devias, random);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
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

// Las dos marcas 0x69b de un altar en el lote de referencia: la primera de 2,8 con `spin` y la segunda de 3,6 con
// `backSpin`, si la hay, las dos con la luz al doble.
struct AltarMarks
{
    glm::vec3 light{};
    float spin = 0.0f;
    std::optional<float> backSpin;
};

void ExpectTheAltarMarks(const Client::Render::ModelBatch& batch, const Client::Data::Terrain::TerrainHeights& heights,
                         const AltarMarks& marks)
{
    Client::Render::ModelBatch reference;
    reference.Begin(Client::Render::Pass3D{});
    Client::World::TerrainDecal decal{.slot = 0x69b,
                                      .x = 12850.0f,
                                      .y = 12850.0f,
                                      .width = 2.8f,
                                      .height = 2.8f,
                                      .color = marks.light + marks.light,
                                      .angle = marks.spin,
                                      .alpha = 1.0f,
                                      .lift = 5.0f};
    ASSERT_TRUE(Client::World::DrawTerrainDecal(reference, heights, decal).HasValue());
    if (marks.backSpin)
    {
        decal.width = 3.6f;
        decal.height = 3.6f;
        decal.angle = *marks.backSpin;
        ASSERT_TRUE(Client::World::DrawTerrainDecal(reference, heights, decal).HasValue());
    }
    ASSERT_EQ(batch.Vertices().size(), reference.Vertices().size());
    ASSERT_FALSE(batch.Vertices().empty());
    for (std::size_t vertex = 0; vertex < batch.Vertices().size(); ++vertex)
    {
        EXPECT_EQ(batch.Vertices()[vertex].position, reference.Vertices()[vertex].position) << vertex;
        EXPECT_EQ(batch.Vertices()[vertex].uv.x, reference.Vertices()[vertex].uv.x) << vertex;
        EXPECT_EQ(batch.Vertices()[vertex].color.red, reference.Vertices()[vertex].color.red) << vertex;
        EXPECT_EQ(batch.Vertices()[vertex].color.green, reference.Vertices()[vertex].color.green) << vertex;
        EXPECT_EQ(batch.Vertices()[vertex].color.blue, reference.Vertices()[vertex].color.blue) << vertex;
    }
    const auto& command = batch.Commands().back();
    EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
    ASSERT_TRUE(command.state.texture.has_value());
    EXPECT_EQ(command.state.texture->value, 0x69b);
}

// b y g del reloj: sin(T · 0,002f) por 0,1f más 0,28f y por 0,04f más 0,2f, con el seno retenido (ADR0014).
constexpr float AltarClock = 1000.0f;

float AltarBright()
{
    return static_cast<float>(ClockSine(AltarClock, 0.002f, 0.1f) + static_cast<double>(0.28f));
}

float AltarSoft()
{
    return static_cast<float>(ClockSine(AltarClock, 0.002f, 0.04f) + static_cast<double>(0.2f));
}

// 0x43421a..0x43439a, con +0x7c & 0x40000: la luz propia (0,09; 0,09; 0,04), las partículas 0x729/0 y /1 en la
// posición con esa luz, y las dos marcas con los giros T · 0,01f y T · −0,01f; la luz es (g; g; 0,05f), o la de los
// bits del color si también están. Con la tabla llena, las marcas sin las partículas ni sus rand(). Después, 0x620980.
TEST_F(MonsterHelpersTest, TheCrywolfAltarsWithTheirOwnBitDrawTwoMarksAndTwoLights)
{
    const float b = AltarBright();
    const float g = AltarSoft();
    const float spin = static_cast<float>(static_cast<double>(AltarClock) * static_cast<double>(0.01f));
    const float backSpin = static_cast<float>(static_cast<double>(AltarClock) * static_cast<double>(-0.01f));
    const glm::vec3 own{std::bit_cast<float>(0x3db851ecu), std::bit_cast<float>(0x3db851ecu),
                        std::bit_cast<float>(0x3d23d70au)};
    struct Case
    {
        std::uint32_t flags;
        glm::vec3 light;
        bool full;
    };
    const std::array cases{Case{0x40000, {g, g, 0.05f}, false}, Case{0x40000 | 0x100000, {b, 0.1f, 0.1f}, false},
                           Case{0x40000 | 0x80000, {0.15f, 0.15f, b}, false}, Case{0x40000, {g, g, 0.05f}, true}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.flags);
        ClearParticles();
        if (value.full)
            FillParticles();
        ActorState& actor = Monster(0x206, 0);
        actor.position = {12850.0f, 12850.0f, 10.0f};
        actor.angles = {0.0f, 0.0f, 60.0f};
        actor.flags7c = value.flags;
        CrtRandom random(1);
        auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        MonsterHelperPass pass = Pass(Crywolf, random);
        pass.worldClock = AltarClock;
        pass.batch = &batch;
        pass.heights = heights.get();

        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(actor.light, own);
        CrtRandom expected(1);
        if (!value.full)
        {
            Expect(expected, 0x729, 0, actor.position, actor.angles, own, 1.0f, AltarClock);
            Expect(expected, 0x729, 1, actor.position, actor.angles, own, 1.0f, AltarClock);
            ExpectTheParticles();
            EXPECT_EQ(LiveParticles().size(), 2u);
        }
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheAltarMarks(batch, *heights, {.light = value.light, .spin = spin, .backSpin = backSpin});
        EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Opaque);
    }
}

// 0x4342a2..0x4343a2 sin 0x40000: con 0x100000 o 0x20000 la primera marca 0x69b de 2,8 con (b; 0,1; 0,1) al doble y
// el giro g; con 0x10000 u 0x80000, (0,15; 0,15; b) al doble y el giro T · 0,01f. La segunda no se dibuja: su giro
// ([ebp−0x1c]) es la pila sin escribir (Q-C1100). Sin ningún bit, tampoco la primera (su luz es la pila): solo se liga
// la textura. Ninguno saca rand(), crea partículas ni toca la luz propia.
TEST_F(MonsterHelpersTest, TheCrywolfAltarsSkipTheMarksThatReadTheStack)
{
    struct Case
    {
        std::uint32_t flags;
        bool pale;
    };
    constexpr std::array cases{Case{0x100000, false}, Case{0x20000, false}, Case{0x10000, true}, Case{0x80000, true},
                               Case{0x100000 | 0x80000, true}};
    const float b = AltarBright();
    const float g = AltarSoft();
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.flags);
        ActorState& actor = Monster(0x204, 0);
        actor.position = {12850.0f, 12850.0f, 10.0f};
        actor.flags7c = value.flags;
        CrtRandom random(1);
        auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        MonsterHelperPass pass = Pass(Crywolf, random);
        pass.worldClock = AltarClock;
        pass.batch = &batch;
        pass.heights = heights.get();

        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const glm::vec3 light = value.pale ? glm::vec3{0.15f, 0.15f, b} : glm::vec3{b, 0.1f, 0.1f};
        const float spin =
            value.pale ? static_cast<float>(static_cast<double>(AltarClock) * static_cast<double>(0.01f)) : g;
        ExpectTheAltarMarks(batch, *heights, {.light = light, .spin = spin, .backSpin = std::nullopt});
        EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Opaque);
        EXPECT_EQ(random.State(), 1u);
        EXPECT_TRUE(LiveParticles().empty());
        EXPECT_EQ(actor.light, glm::vec3(0.1f, 0.2f, 0.3f));
    }

    ActorState& actor = Monster(0x208, 0);
    actor.flags7c = 0;
    CrtRandom random(1);
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MonsterHelperPass pass = Pass(Crywolf, random);
    pass.batch = &batch;
    pass.heights = heights.get();
    const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(batch.Vertices().empty());
    ASSERT_TRUE(batch.State().texture.has_value());
    EXPECT_EQ(batch.State().texture->value, 0x69b);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ(random.State(), 1u);
    EXPECT_TRUE(LiveParticles().empty());

    // Sin el lote o las alturas, corta en la llamada.
    actor.flags7c = 0x100000;
    pass.heights = nullptr;
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), "0x43436c"));
    EXPECT_EQ(random.State(), 1u);
}

// Fuera de Crywolf (0x4303c0 da 0), 0x434180 no mira el tipo: en Lorencia y Devias el 0x204 no dibuja nada, ni con
// 0x40000 crea sus partículas.
TEST_F(MonsterHelpersTest, OutsideCrywolfTheAltarsDrawNothing)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ActorState& actor = Monster(0x204, 0);
        actor.flags7c = 0x40000 | 0x100000;
        CrtRandom random(1);
        auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        MonsterHelperPass pass = Pass(map, random);
        pass.batch = &batch;
        pass.heights = heights.get();
        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_EQ(random.State(), 1u);
        EXPECT_TRUE(LiveParticles().empty());
        EXPECT_EQ(actor.light, glm::vec3(0.1f, 0.2f, 0.3f));
    }
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

// 0x434bf3..0x434e2e y 0x434f8a..0x4354a1: la cabeza del 0x1b1 fuera de la 6 y, en la 2 con rand() % 10 en 0, su
// partícula 0x5f3/0 en la posición con la luz de los ojos, antes del sorteo del sonido; el centro y las puntas del
// 0x1b2, la mitad en la 3 y la 4, con el seno retenido del reloj (ADR0014).
TEST_F(MonsterHelpersTest, The1b1And1b2DrawTheirSprites)
{
    Name("Monster95_Head", 6);
    ActorState& actor = Monster(0x1b1, 2);
    actor.angles = {0.0f, 0.0f, 120.0f};
    const std::uint32_t seed = FirstSeedWhere(
        [](CrtRandom& random)
        {
            if (random.Next() % 10 != 0)
                return false;
            for (int draw = 0; draw < 4; ++draw)
                static_cast<void>(random.Next());
            return random.Next() % 15 != 0;
        });
    CrtRandom random(seed);
    MonsterHelperPass pass = Pass(Crywolf, random);
    const auto dusty = Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass);
    ASSERT_TRUE(dusty.HasValue()) << dusty.Error();
    EXPECT_TRUE(dusty.Value());
    ASSERT_EQ(LiveSprites().size(), 1u);
    EXPECT_FLOAT_EQ(LiveSprites()[0].scale, 3.5f);
    EXPECT_EQ(LiveSprites()[0].light, RedEyes);
    CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    Expect(expected, 0x5f3, 0, Origin, actor.angles, RedEyes);
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    ExpectTheParticles();
    EXPECT_EQ(LiveParticles().size(), 1u);
    // Con la tabla llena, el sorteo del sonido sigue sin los rand() de la partícula.
    FillParticles();
    m_sprites.Clear();
    CrtRandom crowded(seed);
    MonsterHelperPass full = Pass(Crywolf, crowded);
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(full).HasValue());
    CrtRandom sorted(seed);
    static_cast<void>(sorted.Next());
    if (sorted.Next() % 15 == 0)
        static_cast<void>(sorted.Next());
    EXPECT_EQ(crowded.State(), sorted.State());

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
    const float lum = static_cast<float>(ClockSine(1000.0f, 0.0012f, 0.8f) + static_cast<double>(1.3f));
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

// 0x4276a4..0x427a92 con la tabla llena: el 0x1b6 suena (en la 4, 0x9c más rand() % 2 y después 0x263, con +4 = 1) y,
// en la 4 desde el frame 6 con el blanco +0x3a4 en el pool, corta en el joint 0x614. Sin blanco (un WORD negativo o
// desde 0x190), o en la 3, sus partículas no crean nada.
TEST_F(MonsterHelpersTest, The1b6FollowsItsTailWithAFullTable)
{
    Name("Monster100_L_Hand", 76);
    for (const auto& [bone, index] :
         {std::pair{"Monster100_z02", 107u}, std::pair{"Monster100_z03", 108u}, std::pair{"Monster100_z04", 109u},
          std::pair{"Monster100_z05", 110u}, std::pair{"Monster100_Footstepst", 0u}})
        Name(bone, index);
    FillParticles();
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ActorState& actor = Monster(0x1b6, 4, 6.0f);
        actor.subtype = 0;
        actor.word3a4 = 3;
        CrtRandom random(11);
        MonsterHelperPass pass = Pass(map, random);
        EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), "0x427a8d"));
        EXPECT_EQ(Sound(), 0x263);
        EXPECT_EQ(actor.subtype, 1);
        EXPECT_EQ(random.State(), AfterDraws(11, 2).State());
        EXPECT_EQ(LiveSprites().size(), 5u);
        m_sprites.Clear();

        for (const std::uint16_t target : {std::uint16_t{0xffff}, std::uint16_t{0x190}})
        {
            actor.word3a4 = target;
            CrtRandom untargeted(11);
            MonsterHelperPass free = Pass(map, untargeted);
            ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(free).HasValue());
            EXPECT_EQ(untargeted.State(), AfterDraws(11, 1).State());
        }
        actor.word3a4 = 3;
        actor.animation.frame = 5.9f;
        CrtRandom early(11);
        MonsterHelperPass before = Pass(map, early);
        ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(before).HasValue());
        actor.animation.action = 3;
        actor.animation.frame = 8.0f;
        actor.subtype = 0;
        m_world->Runtime().lastSoundId.reset();
        CrtRandom swing(11);
        MonsterHelperPass third = Pass(map, swing);
        ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(third).HasValue());
        EXPECT_EQ(Sound(), 0x264);
        EXPECT_EQ(swing.State(), AfterDraws(11, 1).State());
        m_sprites.Clear();
    }
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

// Con la tabla llena, sus partículas no crean nada y siguen: el 0x1b7 suena y en la 3 desde el frame 6,9 con el blanco
// en el pool corta en el joint 0x618 (0x428036 salta a 0x427a8d); el 0x1b8 suena; el 0x1ba da sus seis cuernos, cada
// uno con su rand() % 10; el 0x1bb en la 4 de 9 a 10 corta en el efecto 0x5e8.
TEST_F(MonsterHelpersTest, The1b7To1bbFollowTheirTailWithAFullTable)
{
    Name("Monster101_L_Arm", 12);
    Name("Monster102_Head", 6);
    for (std::uint32_t horn = 0; horn < 6; ++horn)
        Name("Monster104_Horn" + std::to_string(horn), 30 + horn);
    for (const char* bone : {"Monster105_R_Eye", "Monster105_L_Eye", "Monster105_L_Arm00", "Monster105_L_Arm01",
                             "Monster105_L_Arm02", "Monster105_L_Hand"})
        Name(bone, 20);
    FillParticles();
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::optional<std::string_view> address;
        std::size_t sprites;
        int draws;
        std::int32_t sound;
    };
    for (const Case& value :
         {Case{0x1b7, 3, 6.9f, "0x427a8d", 0, 0, 0x249}, Case{0x1b7, 3, 6.8f, std::nullopt, 0, 0, 0x249},
          Case{0x1b8, 4, 0.0f, std::nullopt, 1, 1, 0x24f}, Case{0x1ba, 4, 0.0f, std::nullopt, 6, 6, 0x254},
          Case{0x1bb, 4, 9.0f, "0x428da7", 5, 1, 0x25e}, Case{0x1bb, 4, 10.0f, "0x428da7", 5, 1, 0x25e},
          Case{0x1bb, 4, 10.5f, std::nullopt, 5, 1, 0x25e}, Case{0x1bb, 8, 9.5f, std::nullopt, 5, 1, 0x25d}})
    {
        for (const std::int32_t map : {Lorencia, Devias})
        {
            SCOPED_TRACE(testing::Message() << value.type << " " << value.frame << " " << map);
            m_sprites.Clear();
            m_world->Runtime().lastSoundId.reset();
            ActorState& actor = Monster(value.type, value.action, value.frame);
            actor.subtype = 0;
            actor.word3a4 = 0x18f;
            CrtRandom random(21);
            MonsterHelperPass pass = Pass(map, random);
            const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
            if (value.address)
                EXPECT_TRUE(CutAt(drawn, *value.address)) << (drawn.HasValue() ? "" : drawn.Error());
            else
                EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_EQ(LiveSprites().size(), value.sprites);
            EXPECT_EQ(random.State(), AfterDraws(21, value.draws).State());
            EXPECT_EQ(Sound(), value.sound);
        }
    }
    // Los cuernos del 0x1ba, en orden: (0,4; rand() % 10 · 0,01f + 0,8f; 0,5) a 0,4; en la 0, sin sonido ni sorteo.
    m_sprites.Clear();
    Monster(0x1ba, 0);
    CrtRandom random(21);
    MonsterHelperPass pass = Pass(Devias, random);
    ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(pass).HasValue());
    CrtRandom expected(21);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 6u);
    for (std::uint32_t horn = 0; horn < 6; ++horn)
    {
        const float green = static_cast<float>(static_cast<double>(expected.Next() % 10) * static_cast<double>(0.01f) +
                                               static_cast<double>(0.8f));
        EXPECT_EQ(sprites[horn].position, At(30 + horn));
        EXPECT_EQ(sprites[horn].light, glm::vec3(0.4f, green, 0.5f));
        EXPECT_FLOAT_EQ(sprites[horn].scale, 0.4f);
    }
    EXPECT_EQ(random.State(), expected.State());
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
        const auto stepped =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0.0f);
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
        const auto stepped =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0.0f);
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

// 0x432c12..0x432db0 con el frame del objeto en (5, 7): en cada una de las diez poses, la partícula 0x5d9/0 en el punto
// (0, 100, −150) del hueso 16 del buffer global (sin trasladar: ya está en el mundo), con los ángulos de esa vuelta,
// luz blanca y escala 1, antes de los dos rand() de la vuelta. Con la tabla llena, solo los rand() de las vueltas.
TEST_F(MonsterHelpersTest, The1b0SwingSparksInEveryPose)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ClearParticles();
        m_world->PoseCarry() = {};
        m_world->Runtime().map = map;
        ActorState& actor = Monster(0x1b0, 4, 6.0f);
        actor.angles = {0.0f, 0.0f, 90.0f};
        CrtRandom random(1);
        const auto stepped =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(actor.angles, glm::vec3(0.0f, 0.0f, 90.0f));
        const auto sparks = LiveParticles();
        ASSERT_EQ(sparks.size(), 10u);
        // La última vuelta: su punto sale de la pose que quedó en el buffer.
        const auto& bones = m_world->PoseCarry().bones;
        ASSERT_GT(bones.size(), 16u);
        const glm::vec3 last = Client::Render::Models::OriginalVectorTransform(
            {0.0f, 100.0f, -150.0f}, Client::Render::Models::OriginalMatrixOf(bones[16]));
        CrtRandom expected(1);
        static_cast<void>(expected.Next());
        for (std::size_t turn = 0; turn < 10; ++turn)
        {
            const glm::vec3 angles{0.0f, 0.0f, 90.0f - 18.0f * static_cast<float>(turn)};
            Expect(expected, 0x5d9, 0, sparks[turn].position, angles, glm::vec3(1.0f));
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
        }
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(random.State(), AfterDraws(1, 51).State());
        ExpectTheParticles();
        EXPECT_EQ(sparks[9].position, last);
        EXPECT_EQ(sparks[9].angles.z, 90.0f - 18.0f * 9.0f);
    }
    FillParticles();
    Monster(0x1b0, 4, 6.0f);
    CrtRandom full(1);
    const auto stepped =
        Client::Scenes::StepTheSwingsOf1abTo1b4(m_world->Actors()[Slot], Slot, *m_world, full, m_particles.get(), 0.0f);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(full.State(), AfterDraws(1, 21).State());
}

// Las estelas cortan después de su primera pose, y el 0x1ae en la 4 entre 4,5 y 5 antes de posar. Con lugar en la
// tabla, la 0x640 del 0x1ab y la 0x5f2/0x1b del 0x1ae en la 8 cortan; llena, no crean nada: el 0x1ab sigue a su estela
// y el 0x1ae sale. En Lorencia y en Devias da lo mismo.
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
        bool full;
    };
    for (const Case& value :
         {Case{0x1ab, 3, 4.0f, "0x433056", true, false}, Case{0x1ab, 3, 4.0f, "0x433086", true, true},
          Case{0x1ab, 3, 4.5f, "0x433086", true, false}, Case{0x1ae, 8, 4.0f, "0x433248", false, false},
          Case{0x1ae, 8, 4.0f, "", false, true}, Case{0x1ae, 4, 4.7f, "0x433339", false, false},
          Case{0x1ae, 3, 4.7f, "0x433410", true, false}, Case{0x1af, 4, 3.0f, "0x433557", true, false},
          Case{0x1b1, 4, 3.0f, "0x4337cf", true, false}, Case{0x1b3, 3, 3.0f, "0x43397d", true, false},
          Case{0x1b4, 3, 3.0f, "0x432ea3", true, false}})
    {
        SCOPED_TRACE(testing::Message() << value.address << (value.full ? " llena" : ""));
        ClearParticles();
        if (value.full)
            FillParticles();
        m_world->PoseCarry() = {};
        m_world->Runtime().map = value.type % 2 == 0 ? Lorencia : Devias;
        ActorState& actor = Monster(value.type, value.action, value.frame);
        CrtRandom random(1);
        const auto stepped =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0.0f);
        if (value.address.empty())
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        else
            EXPECT_TRUE(CutAt(stepped, value.address)) << (stepped.HasValue() ? "" : stepped.Error());
        EXPECT_EQ(random.State(), AfterDraws(1, 1).State());
        EXPECT_EQ(m_world->PoseCarry().models.count(value.type), value.posed ? 1u : 0u);
    }
}
} // namespace
