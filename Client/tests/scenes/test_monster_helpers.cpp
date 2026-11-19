#include "Scenes/MonsterHelpers.h"

#include "Scenes/GameDrawTail.h"
#include <algorithm>

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
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "World/ParticleOwner.h"
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
#include <tuple>
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

    void ResizeBones(std::size_t count)
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.actions.assign(8, std::vector<BoneKey>(10));
        m_skeleton.bones.resize(count, bone);
    }

    void SetBoneActionPosition(std::size_t bone, std::size_t action, const glm::vec3& position)
    {
        for (auto& key : m_skeleton.bones.at(bone).actions.at(action))
            key.position = position;
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
        m_world->PlaceModel(actor);
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

    void FillModelEffects()
    {
        for (auto& effect : m_world->Map().effects.Effects())
            effect.alive = true;
    }

    void ClearModelEffects()
    {
        for (auto& effect : m_world->Map().effects.Effects())
            effect.alive = false;
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

// 0x441688..0x4416a5 y 0x442159..0x442179: al morir (la 6), el 0x1a4 pide 0x4ee770(0, 0x13c, 0) y el 0x1a6
// 0x4ee770(0, 0x13c, 1) sobre las mallas del cuerpo, los dos dejan +0 en 0 y piden su sonido (0x209 y 0x1ff); el 0x1a6
// sigue con sus venenos. Entre frames del original, sin efectos ni sonido. Sin las mallas, cortan en la llamada.
TEST_F(MonsterHelpersTest, The1a4AndThe1a6DieInStones)
{
    for (const char* bone : {"Monster84_PoisonTop", "Monster84_PoisonRight", "Monster84_PoisonLeft"})
        Name(bone, 53);
    const std::uint32_t calm =
        FirstSeedWhere([](CrtRandom& random)
                       { return random.Next() % 10 != 0 && random.Next() % 10 != 0 && random.Next() % 10 != 0; });
    struct Death
    {
        std::uint16_t type;
        std::int32_t subtype;
        std::int32_t sound;
        int draws;
    };
    for (const Death& death : {Death{0x1a4, 0, 0x209, 0}, Death{0x1a6, 1, 0x1ff, 3}})
    {
        SCOPED_TRACE(death.type);
        for (const bool original : {true, false})
        {
            SCOPED_TRACE(original);
            m_world->Runtime().lastSoundId.reset();
            ActorState& actor = Monster(death.type, 6);
            CrtRandom random(calm);
            MonsterHelperPass pass = Pass(Lorencia, random, original);
            std::vector<std::pair<std::int32_t, std::int32_t>> asked;
            pass.vertexEffects = [&asked](std::int32_t type,
                                          std::int32_t subtype) -> Client::Core::Expected<void, std::string>
            {
                asked.emplace_back(type, subtype);
                return {};
            };
            const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_FALSE(actor.alive);
            EXPECT_EQ(random.State(), AfterDraws(calm, death.draws).State());
            if (original)
            {
                ASSERT_EQ(asked.size(), 1u);
                EXPECT_EQ(asked[0], std::make_pair(std::int32_t{0x13c}, death.subtype));
                EXPECT_EQ(Sound(), death.sound);
            }
            else
            {
                EXPECT_TRUE(asked.empty());
                EXPECT_FALSE(Sound().has_value());
            }
        }
    }
    Monster(0x1a6, 6);
    CrtRandom random(calm);
    MonsterHelperPass bare = Pass(Lorencia, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(bare), "0x442165"));
}

// Lo que los monstruos sueltan en sus huesos: el tipo, el subtipo, la escala, el punto, la luz, si la partícula lleva
// al personaje de dueño y, si no son los de todas, sus ángulos.
struct BoneParticle
{
    std::int32_t type;
    std::int32_t subtype;
    float scale;
    glm::vec3 point;
    glm::vec3 light;
    bool owned;
    std::optional<glm::vec3> angles = std::nullopt;
};

// Las partículas de `made` en los primeros lugares de `particles`, contra un pool gemelo con el mismo rand() desde
// `expected`; devuelve el rand() de después.
CrtRandom ExpectTheBoneParticles(const Client::World::Particles& particles, std::span<const BoneParticle> made,
                                 const glm::vec3& angles, CrtRandom expected)
{
    auto twin = std::make_unique<Client::World::Particles>();
    const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::Actor, Slot};
    for (std::size_t index = 0; index < made.size(); ++index)
    {
        SCOPED_TRACE(index);
        const BoneParticle& bone = made[index];
        const glm::vec3 turned = bone.angles.value_or(angles);
        const auto replayed = bone.owned ? twin->CreateOwnedChecked(bone.type, bone.point, turned, bone.light,
                                                                    bone.subtype, bone.scale, 0.0f, expected, 0, owner)
                                         : twin->CreateChecked(bone.type, bone.point, turned, bone.light, bone.subtype,
                                                               bone.scale, 0.0f, expected);
        EXPECT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
        const Client::World::Particle& particle = particles.All()[index];
        const Client::World::Particle& reference = twin->All()[index];
        EXPECT_TRUE(particle.alive);
        EXPECT_EQ(particle.type, bone.type);
        EXPECT_EQ(particle.subtype, bone.subtype);
        EXPECT_EQ(particle.start, bone.point);
        EXPECT_EQ(particle.position, reference.position);
        EXPECT_EQ(particle.scale, reference.scale);
        EXPECT_EQ(particle.life, reference.life);
        EXPECT_EQ(particle.velocity, reference.velocity);
        EXPECT_EQ(particle.light, reference.light);
        EXPECT_EQ(particle.typedOwner, bone.owned ? owner : Client::World::ParticleOwner{});
    }
    EXPECT_FALSE(particles.All()[made.size()].alive);
    return expected;
}

// 0x441669..0x441d7d: el 0x1a4 corta en 0x4ee770 al morir sin las mallas del cuerpo. Si no, 0x4416aa..0x441a02, con la
// luz (1, 1, 1): en el punto (5, 10, 5) de la mano izquierda la fanta 0x6ec/3 de 3,4f con él de dueño y el humo
// 0x5f2/21 de 2,5f, en la mano derecha la 0x6ec/4 y otro humo, en el punto (−20, −2, −10) del ojo la 0x6ec/0 y el humo
// de 0,8f sin dueño y en la espalda otro humo. Con la tabla llena sigue: rand() % 20 en 0 pide 0x205 más rand() % 2, y
// en la 3 de 8,4 a 10,2 con +4 en 0, +4 = 1 y el corte de sus efectos.
TEST_F(MonsterHelpersTest, The1a4LeavesFantasAndSmokesOnItsBones)
{
    Name("Monster82_LHand", 34);
    Name("Monster82_RHand", 45);
    Name("Monster82_Eye", 48);
    Name("Monster82_Back", 46);
    Monster(0x1a4, 6);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    EXPECT_TRUE(CutAt(Client::Scenes::DrawTheEffectsOf1a3To1a9(pass), "0x441696"));
    const ActorState& walking = Monster(0x1a4, 0);
    const auto walked = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(walked.HasValue()) << walked.Error();
    const float small = std::bit_cast<float>(0x3f4ccccdu);
    const float hand = std::bit_cast<float>(0x4059999au);
    const glm::vec3 white(1.0f);
    const std::array<BoneParticle, 7> made{{{0x6ec, 3, hand, At(34, {5, 10, 5}), white, true},
                                            {0x5f2, 21, 2.5f, At(34, {5, 10, 5}), white, false},
                                            {0x6ec, 4, hand, At(45), white, true},
                                            {0x5f2, 21, 2.5f, At(45), white, false},
                                            {0x6ec, 0, small, At(48, {-20, -2, -10}), white, false},
                                            {0x5f2, 21, small, At(48, {-20, -2, -10}), white, false},
                                            {0x5f2, 21, small, At(46), white, false}}};
    CrtRandom replayed = ExpectTheBoneParticles(*m_particles, made, walking.angles, CrtRandom(1));
    if (replayed.Next() % 20 == 0)
        static_cast<void>(replayed.Next());
    EXPECT_EQ(random.State(), replayed.State());

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
    const auto struck = Client::Scenes::DrawTheEffectsOf1a3To1a9(attacking);
    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), 0x207);
    EXPECT_EQ(m_world->Map().effects.Effects()[0].type, 0x636);
}

// 0x441a8a..0x441c75, en Lorencia y en Devias: en la 3 de 8,4 a 10,2 con +4 en 0, el 0x1a4 golpea en su mano derecha
// (Monster82_RHand) con su z: el cráter 0x636/2 y el joint 0x61c/6 (ancho 200, sin dueño, clave −1) con los ángulos
// (−90, 0, z) y la columna 0x5de/3 con sus ángulos y su luz (vida 15). Después, lo mismo en cada jugador visto y vivo
// a más de 200 y menos de 1100 en el plano, la columna con los ángulos de él y en blanco; el sonido 0x207. Los rand()
// del joint, repetidos en un pool gemelo con el mismo CrtRandom; el cráter y la columna no sacan.
TEST_F(MonsterHelpersTest, The1a4StrikesTheGroundUnderItsHandAndItsVictims)
{
    Name("Monster82_LHand", 34);
    Name("Monster82_RHand", 45);
    Name("Monster82_Eye", 48);
    Name("Monster82_Back", 46);
    const std::uint32_t quiet = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 20 != 0; });
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        for (auto& effect : m_world->Map().effects.Effects())
            effect.alive = false;
        for (auto& joint : m_world->DrawTail().joints)
            joint.alive = false;
        m_world->Runtime().lastSoundId.reset();
        FillParticles();
        ActorState& actor = Monster(0x1a4, 3, 9.0f);
        actor.subtype = 0;
        actor.angles = {1.0f, 2.0f, 30.0f};
        const auto player = [this](std::size_t slot, const glm::vec3& offset, bool seen) -> ActorState&
        {
            ActorState& other = m_world->Actors()[slot];
            other = ActorState{};
            other.alive = true;
            other.seen = seen;
            other.byte88 = 1;
            other.type = 0x20a;
            other.position = Origin + offset;
            other.angles = {0.0f, 0.0f, static_cast<float>(slot)};
            return other;
        };
        const ActorState& victim = player(20, {600.0f, 0.0f, 50.0f}, true);
        static_cast<void>(player(21, {150.0f, 0.0f, 0.0f}, true));
        static_cast<void>(player(22, {0.0f, 1100.0f, 0.0f}, true));
        static_cast<void>(player(23, {600.0f, 0.0f, 0.0f}, false));
        CrtRandom attack(quiet);
        MonsterHelperPass attacking = Pass(map, attack);

        const auto struck = Client::Scenes::DrawTheEffectsOf1a3To1a9(attacking);

        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        EXPECT_EQ(actor.subtype, 1);
        EXPECT_EQ(Sound(), 0x207);
        CrtRandom replay = AfterDraws(quiet, 1);
        std::vector<Client::Scenes::JointState> twin(m_world->DrawTail().joints.size());
        const glm::vec3 hand{Origin.x, Origin.y, Origin.z};
        const glm::vec3 down{-90.0f, 0.0f, 30.0f};
        for (const glm::vec3& at : {hand, victim.position})
        {
            glm::vec3 point = at;
            glm::vec3 angles = down;
            ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(twin,
                                                         {.type = 0x61c,
                                                          .firstPosition = point,
                                                          .secondPosition = point,
                                                          .angles = angles,
                                                          .subtype = 6,
                                                          .ownerContext = nullptr,
                                                          .scale = 200.0f,
                                                          .key = -1,
                                                          .skill = 0,
                                                          .skillSerial = 0,
                                                          .target = -1},
                                                         replay)
                            .HasValue());
        }
        EXPECT_EQ(attack.State(), replay.State());
        const auto effects = m_world->Map().effects.Effects();
        const auto& joints = m_world->DrawTail().joints;
        const std::array<glm::vec3, 2> points{hand, victim.position};
        const std::array<glm::vec3, 2> columnAngles{actor.angles, victim.angles};
        const std::array<glm::vec3, 2> columnLights{actor.light, glm::vec3(1.0f)};
        for (std::size_t strike = 0; strike < 2; ++strike)
        {
            SCOPED_TRACE(strike);
            const Client::World::ModelEffect& crater = effects[2 * strike];
            ASSERT_TRUE(crater.alive);
            EXPECT_EQ(crater.type, 0x636);
            EXPECT_EQ(crater.subtype, 2);
            EXPECT_EQ(crater.transform.angles, down);
            EXPECT_EQ(crater.ownerContext, nullptr);
            const Client::World::ModelEffect& column = effects[(2 * strike) + 1];
            ASSERT_TRUE(column.alive);
            EXPECT_EQ(column.type, 0x5de);
            EXPECT_EQ(column.subtype, 3);
            EXPECT_EQ(column.transform.position, points[strike]);
            EXPECT_EQ(column.transform.angles, columnAngles[strike]);
            EXPECT_EQ(column.light, columnLights[strike]);
            EXPECT_EQ(column.life, 15);
            EXPECT_EQ(column.ownerContext, nullptr);
            const auto& joint = joints[strike];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x61c);
            EXPECT_EQ(joint.subtype, 6);
            EXPECT_EQ(joint.width, 200.0f);
            EXPECT_EQ(joint.ownerContext, nullptr);
            EXPECT_EQ(joint.position, twin[strike].position);
            EXPECT_EQ(joint.angles, twin[strike].angles);
        }
        EXPECT_FALSE(effects[4].alive);
        EXPECT_FALSE(joints[2].alive);
    }
}

// 0x441d7d consume cuarenta rand() aunque no haya slots; 0x441d89 pide 0x208 sin sortear.
TEST_F(MonsterHelpersTest, The1a4AttackBurstKeepsItsRandomsAndSoundWithTheParticlePoolFull)
{
    Name("Monster82_LHand", 34);
    Name("Monster82_RHand", 45);
    Name("Monster82_Eye", 48);
    Name("Monster82_Back", 46);
    FillParticles();
    const std::uint32_t quiet = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 20 != 0; });
    for (const float frame : {4.99f, 5.0f, 5.7f, 5.71f})
    {
        SCOPED_TRACE(frame);
        ActorState& actor = Monster(0x1a4, 4, frame);
        actor.subtype = 0;
        m_world->Runtime().lastSoundId.reset();
        CrtRandom random(quiet);
        MonsterHelperPass pass = Pass(Lorencia, random);
        const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const bool bursts = frame >= 5.0f && frame <= 5.7f;
        EXPECT_EQ(actor.subtype, bursts ? 1 : 0);
        EXPECT_EQ(Sound(), bursts ? std::optional<std::int32_t>(0x208) : std::nullopt);
        EXPECT_EQ(random.State(), AfterDraws(quiet, bursts ? 41 : 1).State());
        if (bursts)
        {
            const auto again = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
            ASSERT_TRUE(again.HasValue()) << again.Error();
            CrtRandom expected = AfterDraws(quiet, 41);
            if (expected.Next() % 20 == 0)
                static_cast<void>(expected.Next());
            EXPECT_EQ(random.State(), expected.State());
        }
    }
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

// 0x4420f7..0x44281f: en la 3, rand() % 2 en 1 corta en la piedra 0x12e/3, que saldría en un punto que la llamada no
// escribió (Q-C1580); en 0, otro elige el veneno y rand() % 32 + 50 por 0,025f es la escala de su 0x5f2/11, y la
// piedra 0x12e/3 sale en ese punto con sus tres rand() (0x442397) antes de los venenos. En la 4 con el frame de 3,5 a
// 4,2, la 0x5f2/11 de cada mano en el piso, de escala rand() % 32 + 50 por 0,05f. Descansando, tres rand() % 10 sin
// acertar no hacen nada más que +4 a 0; cada uno que acierta crea su 0x5f2/0x17 con sus rand().
TEST_F(MonsterHelpersTest, The1a6PoisonDrawsItsRandomsInOrder)
{
    Name("Monster84_PoisonTop", 52);
    Name("Monster84_PoisonRight", 53);
    Name("Monster84_PoisonLeft", 54);
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
    const auto thrown = Client::Scenes::DrawTheEffectsOf1a3To1a9(poisoned);
    CrtRandom expected(even);
    static_cast<void>(expected.Next());
    const std::uint32_t side = expected.Next() % 2 == 0 ? 53 : 54;
    const auto dusty = static_cast<float>(static_cast<double>(expected.Next() % 32 + 50) * static_cast<double>(0.025f));
    Expect(expected, 0x5f2, 11, At(side), actor.angles, poison, dusty);
    // 0x51d438: la vida, la escala y la caída de la piedra; después los tres venenos, cada uno con su rand() % 10 y, si
    // acierta, su 0x5f2/0x17 con sus rand().
    const std::int32_t life = (expected.Next() & 15) + 32;
    const auto stoneScale = static_cast<float>(
        static_cast<double>(expected.Next() % 3 + 2) * static_cast<double>(0.12f) + static_cast<double>(1.2f));
    const auto stoneDrop = static_cast<float>(-20 - expected.Next() % 5);
    std::size_t falling = 0;
    for (const std::uint32_t bone : {52u, 53u, 54u})
    {
        if (expected.Next() % 10 != 0)
            continue;
        Expect(expected, 0x5f2, 23, At(bone), actor.angles, poison);
        ++falling;
    }
    ASSERT_TRUE(thrown.HasValue()) << thrown.Error();
    EXPECT_EQ(second.State(), expected.State());
    const auto& effects = m_world->Map().effects.Effects();
    const auto stone = std::ranges::find_if(effects, [](const auto& effect) { return effect.alive; });
    ASSERT_NE(stone, effects.end());
    EXPECT_EQ(stone->type, 0x12e);
    EXPECT_EQ(stone->subtype, 3);
    EXPECT_EQ(stone->transform.position, At(side));
    EXPECT_EQ(stone->transform.angles, glm::vec3(-55.0f, 0.0f, actor.angles.z));
    EXPECT_EQ(stone->light, poison);
    EXPECT_EQ(stone->life, life);
    EXPECT_EQ(stone->transform.scale, stoneScale);
    EXPECT_EQ(stone->velocity, glm::vec3(0.0f, 0.0f, stoneDrop));
    EXPECT_EQ(stone->speed, std::bit_cast<float>(0x3fe66666u));
    m_world->Map().effects.Effects()[static_cast<std::size_t>(stone - effects.begin())].alive = false;
    ExpectTheParticles();
    ASSERT_EQ(LiveParticles().size(), 1u + falling);
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

    // 0x4425bb..0x442817: con su rand() % 10 en 0, cada veneno crea su 0x5f2/23 en el punto de su hueso, con los
    // ángulos del objeto, la luz de 0x442107 y escala 1, y saca los dos rand() de su alta antes del rand() % 10 del
    // siguiente. Sólo el derecho (0x44274b):
    const std::uint32_t right = FirstSeedWhere(
        [](CrtRandom& random)
        {
            if (random.Next() % 10 == 0 || random.Next() % 10 != 0)
                return false;
            static_cast<void>(random.Next());
            static_cast<void>(random.Next());
            return random.Next() % 10 != 0;
        });
    ClearParticles();
    CrtRandom dripping(right);
    MonsterHelperPass drip = Pass(Lorencia, dripping);
    const auto dripped = Client::Scenes::DrawTheEffectsOf1a3To1a9(drip);
    ASSERT_TRUE(dripped.HasValue()) << dripped.Error();
    CrtRandom drops(right);
    static_cast<void>(drops.Next());
    static_cast<void>(drops.Next());
    Expect(drops, 0x5f2, 23, At(53), actor.angles, poison);
    static_cast<void>(drops.Next());
    EXPECT_EQ(dripping.State(), drops.State());
    EXPECT_EQ(dripping.State(), AfterDraws(right, 5).State());
    ExpectTheParticles();
    ASSERT_EQ(LiveParticles().size(), 1u);
    EXPECT_EQ(LiveParticles()[0].life, 16);

    // Los tres, en su orden: arriba (0x44267f), derecha (0x44274b) e izquierda (0x442817).
    const std::uint32_t all = FirstSeedWhere(
        [](CrtRandom& random)
        {
            for (int drop = 0; drop < 3; ++drop)
            {
                if (random.Next() % 10 != 0)
                    return false;
                static_cast<void>(random.Next());
                static_cast<void>(random.Next());
            }
            return true;
        });
    ClearParticles();
    CrtRandom pouring(all);
    MonsterHelperPass pour = Pass(Devias, pouring);
    const auto poured = Client::Scenes::DrawTheEffectsOf1a3To1a9(pour);
    ASSERT_TRUE(poured.HasValue()) << poured.Error();
    CrtRandom streams(all);
    for (const std::uint32_t bone : {52u, 53u, 54u})
    {
        static_cast<void>(streams.Next());
        Expect(streams, 0x5f2, 23, At(bone), actor.angles, poison);
    }
    EXPECT_EQ(pouring.State(), streams.State());
    EXPECT_EQ(pouring.State(), AfterDraws(all, 9).State());
    ExpectTheParticles();
    EXPECT_EQ(LiveParticles().size(), 3u);

    // Con la tabla llena, ningún veneno crea nada ni saca los rand() de su alta: quedan los tres rand() % 10.
    FillParticles();
    CrtRandom crowded(all);
    MonsterHelperPass full = Pass(Devias, crowded);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(full).HasValue());
    EXPECT_EQ(crowded.State(), AfterDraws(all, 3).State());
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

// 0x442a62..0x442dbc: dos 0x5ac por ojo en (0, −2, 0); en la 4, la primera vez +4 = 1, el efecto 0x13f (0x442cb9,
// sin rand(): vida 20 y el monstruo de dueño) y el sonido 0x20d; después, en el punto (0, 0, −10) de la mano
// izquierda, la fanta 0x6ec/3 de 3 con él de dueño y el humo 0x5f2/21 de 2, con la luz de los ojos. Con la tabla
// llena, ninguna crea nada ni saca rand().
TEST_F(MonsterHelpersTest, The1a9AttackMarksItsFirstFrame)
{
    Name("Monster87_LeftEye", 8);
    Name("Monster87_RightEye", 9);
    Name("Monster87_LeftHand", 16);
    ActorState& actor = Monster(0x1a9, 4);
    actor.angles = {0.0f, 0.0f, 45.0f};
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    const auto first = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), 0x20d);
    const auto& call = m_world->Map().effects.Effects()[0];
    ASSERT_TRUE(call.alive);
    EXPECT_EQ(call.type, 0x13f);
    EXPECT_EQ(call.life, 20);
    EXPECT_EQ(call.transform.position, actor.position);
    EXPECT_EQ(call.transform.angles, actor.angles);
    EXPECT_EQ(call.light, actor.light);
    EXPECT_EQ(call.ownerContext, &actor);
    EXPECT_FALSE(m_world->Map().effects.Effects()[1].alive);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 4u);
    EXPECT_EQ(sprites[3].position, At(9, {0.0f, -2.0f, 0.0f}));
    const std::array<BoneParticle, 2> made{
        {{0x6ec, 3, 3.0f, At(16, {0, 0, -10}), RedEyes, true}, {0x5f2, 21, 2.0f, At(16, {0, 0, -10}), RedEyes, false}}};
    EXPECT_EQ(random.State(), ExpectTheBoneParticles(*m_particles, made, actor.angles, CrtRandom(1)).State());
    for (auto& particle : m_particles->All())
        particle.alive = false;
    // La segunda vez, con +4 en 1, ni el efecto ni el sonido; las mismas dos partículas.
    m_world->Runtime().lastSoundId.reset();
    random = CrtRandom(1);
    const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(Sound().has_value());
    EXPECT_FALSE(m_world->Map().effects.Effects()[1].alive);
    EXPECT_EQ(random.State(), ExpectTheBoneParticles(*m_particles, made, actor.angles, CrtRandom(1)).State());
    for (auto& particle : m_particles->All())
        particle.alive = false;
    random = CrtRandom(1);
    // Con la tabla llena, la 0x6ec/3 y la 0x5f2/21 no crean nada.
    FillParticles();
    MonsterHelperPass crowded = Pass(Devias, random);
    ASSERT_TRUE(Client::Scenes::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
    EXPECT_EQ(random.State(), 1u);
}

// Entre frames del original el monstruo es una copia: +4 pasa a 1 en ella, pero el 0x13f no nace en la tabla del mundo
// ni suena el 0x20d.
TEST_F(MonsterHelpersTest, The1a9CallsItsMeteorOnlyInAnOriginalFrame)
{
    Name("Monster87_LeftEye", 8);
    Name("Monster87_RightEye", 9);
    Name("Monster87_LeftHand", 16);
    ActorState& actor = Monster(0x1a9, 4);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random, false);
    const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_FALSE(Sound().has_value());
    EXPECT_FALSE(m_world->Map().effects.Effects()[0].alive);
}

// 0x518cf0 no inicia el efecto con la tabla llena: sigue el sonido de 0x442cc5 y la mano.
TEST_F(MonsterHelpersTest, The1a9FullEffectPoolStillPlaysItsAttackSound)
{
    Name("Monster87_LeftEye", 8);
    Name("Monster87_RightEye", 9);
    Name("Monster87_LeftHand", 16);
    ActorState& actor = Monster(0x1a9, 4);
    FillModelEffects();
    FillParticles();
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    const auto drawn = Client::Scenes::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), 0x20d);
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

// 0x434e3f..0x434ecf: el 0x1b3 en la4 y frame[4,6] crea115/1 desde la posición, y luego suena.
TEST_F(MonsterHelpersTest, The1b3CreatesThePiercingInItsAttackWindowBeforeSounding)
{
    ActorState& actor = Monster(0x1b3, 4, 6.0f);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Crywolf, random);
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    const auto& effect = m_world->Map().effects.Effects()[0];
    EXPECT_EQ(effect.type, 0x115);
    EXPECT_EQ(effect.life, 6);
    EXPECT_EQ(effect.start, actor.position);
    EXPECT_EQ(effect.transform.position.z, actor.position.z + 130);
    EXPECT_EQ(effect.light, (glm::vec3{1, 0, 0.5f}));
    EXPECT_EQ(Sound(), 0x21d);
    actor.animation.frame = 6.5f;
    ASSERT_TRUE(Client::Scenes::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(Sound(), 0x21d);
}

// 0x42720d..0x42769f: el 0x1b6 saca rand() % 30 para la luz de sus cinco 0x5ad y suelta en el punto (0, 0, −65) del
// pie la 0x5ad/4 de 4 (vida 4, textura 0x5fd, dos rand()) con la luz (0,7f; 0,5; 0,7f); quieto en la 2, sigue con su
// rand() % 15.
// 0x427764..0x427a4d: en la 3 desde el frame 6, en el punto (70, 0, 0) del pie, la 0x5f2/0x1b de 2 con la luz (0,5;
// 0,2f; 0,5), la 0x5ad/2 de 1 y la 0x5ad/3 de 1,3f, y con el blanco, la 0x5ad/3 100 más arriba de él; en la 4 desde el
// frame 4, sólo las dos primeras. Todas con los ángulos (0, 0, 0).
TEST_F(MonsterHelpersTest, The1b6GlowsAndSparksAtItsFootAndTarget)
{
    Name("Monster100_L_Hand", 76);
    for (const auto& [bone, index] :
         {std::pair{"Monster100_z02", 107u}, std::pair{"Monster100_z03", 108u}, std::pair{"Monster100_z04", 109u},
          std::pair{"Monster100_z05", 110u}, std::pair{"Monster100_Footstepst", 0u}})
        Name(bone, index);
    Monster(0x1b6, 2);
    CrtRandom random(11);
    MonsterHelperPass pass = Pass(Devias, random);
    const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const double v =
        static_cast<double>(CrtRandom(11).Next() % 30) * static_cast<double>(0.01f) + static_cast<double>(0.6f);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 5u);
    EXPECT_EQ(sprites[0].light,
              glm::vec3(static_cast<float>(v + 0.5), static_cast<float>(v - static_cast<double>(0.05f)),
                        static_cast<float>(v + 0.5)));
    EXPECT_EQ(sprites[3].position, At(109));
    EXPECT_FLOAT_EQ(sprites[3].scale, 1.7f);
    const glm::vec3 footLight{0.7f, 0.5f, 0.7f};
    const std::array<BoneParticle, 1> blink{{{0x5ad, 4, 4.0f, At(0, {0, 0, -65}), footLight, false}}};
    CrtRandom idle = ExpectTheBoneParticles(*m_particles, blink, {}, AfterDraws(11, 1));
    EXPECT_EQ(m_particles->All()[0].life, 4);
    EXPECT_EQ(m_particles->All()[0].texture, 0x5fd);
    if (idle.Next() % 15 == 0)
        static_cast<void>(idle.Next());
    EXPECT_EQ(random.State(), idle.State());

    const glm::vec3 white(1.0f);
    const glm::vec3 dust{0.5f, 0.2f, 0.5f};
    const float flash = std::bit_cast<float>(0x3fa66666u);
    ActorState& victim = m_world->Actors()[3];
    victim.position = {300.0f, 400.0f, 50.0f};
    for (const std::uint8_t action : {std::uint8_t{3}, std::uint8_t{4}})
    {
        SCOPED_TRACE(int{action});
        for (auto& particle : m_particles->All())
            particle.alive = false;
        m_sprites.Clear();
        for (Client::Scenes::JointState& joint : m_world->DrawTail().joints)
            joint.alive = false;
        ActorState& actor = Monster(0x1b6, action, 6.0f);
        actor.subtype = 1;
        actor.attackTarget = 3;
        CrtRandom attacking(11);
        MonsterHelperPass hit = Pass(Devias, attacking);
        const auto sparked = Client::Scenes::DrawTheMonsterHelpers(hit);
        ASSERT_TRUE(sparked.HasValue()) << sparked.Error();
        std::vector<BoneParticle> made{{0x5ad, 4, 4.0f, At(0, {0, 0, -65}), footLight, false},
                                       {0x5f2, 0x1b, 2.0f, At(0, {70, 0, 0}), dust, false},
                                       {0x5ad, 2, 1.0f, At(0, {70, 0, 0}), white, false}};
        if (action == 3)
        {
            made.push_back({0x5ad, 3, flash, At(0, {70, 0, 0}), white, false});
            made.push_back({0x5ad, 3, flash, {300.0f, 400.0f, 150.0f}, white, false});
        }
        CrtRandom expected = ExpectTheBoneParticles(*m_particles, made, {}, AfterDraws(11, 1));
        if (action == 4)
        {
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            EXPECT_TRUE(m_world->DrawTail().joints[0].alive);
        }
        EXPECT_EQ(attacking.State(), expected.State());
    }
}

// 0x4276a4..0x427a92 con la tabla llena: el 0x1b6 suena (en la 4, 0x9c más rand() % 2 y después 0x263, con +4 = 1) y,
// en la 4 desde el frame 6 con el blanco +0x3a4 en el pool, crea el joint 0x614/0x10 sin dueño desde el blanco hasta
// 100 sobre él (0x427a8d, dos rand()). Sin blanco (un WORD negativo o desde 0x190), o en la 3, sus partículas no crean
// nada.
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
        actor.attackTarget = 3;
        CrtRandom random(11);
        MonsterHelperPass pass = Pass(map, random);
        for (Client::Scenes::JointState& joint : m_world->DrawTail().joints)
            joint.alive = false;
        ActorState& victim = m_world->Actors()[3];
        victim.position = {300.0f, 400.0f, 50.0f};
        actor.angles = {1.0f, 2.0f, 3.0f};
        ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(pass).HasValue());
        EXPECT_EQ(Sound(), 0x263);
        EXPECT_EQ(actor.subtype, 1);
        CrtRandom expected = AfterDraws(11, 2);
        const float speed = static_cast<float>(static_cast<double>(expected.Next() % 10) + 20.0);
        const std::int32_t life = expected.Next() % 2 + 2;
        EXPECT_EQ(random.State(), expected.State());
        const Client::Scenes::JointState& joint = m_world->DrawTail().joints[0];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x614);
        EXPECT_EQ(joint.subtype, 0x10);
        EXPECT_EQ(joint.ownerContext, nullptr);
        EXPECT_EQ(joint.position, victim.position);
        EXPECT_EQ(joint.targetPosition, (glm::vec3{300.0f, 400.0f, 150.0f}));
        EXPECT_EQ(joint.angles, actor.angles);
        EXPECT_EQ(joint.width, 10.0f);
        EXPECT_EQ(joint.speed, speed);
        EXPECT_EQ(joint.life, life);
        EXPECT_FALSE(m_world->DrawTail().joints[1].alive);
        EXPECT_EQ(LiveSprites().size(), 5u);
        m_sprites.Clear();

        for (const std::uint16_t target : {std::uint16_t{0xffff}, std::uint16_t{0x190}})
        {
            actor.attackTarget = target;
            CrtRandom untargeted(11);
            MonsterHelperPass free = Pass(map, untargeted);
            ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(free).HasValue());
            EXPECT_EQ(untargeted.State(), AfterDraws(11, 1).State());
        }
        actor.attackTarget = 3;
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

// 0x427ab0..0x427fad: el 0x1b7 suelta la neblina 0x6f1/1 en los puntos de los brazos y la cabeza (ángulos 0, luz 1 y
// escala 1; siete rand() cada una) y, después de su sonido, en la 3, en el punto (30, 0, 0) del brazo izquierdo con los
// ángulos (0, 45, 45): desde el frame 4 el humo 0x5f2/0x19 de 1 con la luz 1 y, de 5 a 5,5, la onda 0x6eb/3 de 0,5 con
// la luz 5. Sin sprites.
TEST_F(MonsterHelpersTest, The1b7MistsItsArmsAndHeadAndWavesItsArm)
{
    Name("Monster101_L_Arm", 12);
    Name("Monster101_R_Arm", 13);
    Name("Monster101_Head", 14);
    const glm::vec3 white(1.0f);
    const glm::vec3 tilt{0.0f, 45.0f, 45.0f};
    const std::array<BoneParticle, 5> all{{{0x6f1, 1, 1.0f, At(12), white, false},
                                           {0x6f1, 1, 1.0f, At(13), white, false},
                                           {0x6f1, 1, 1.0f, At(14), white, false},
                                           {0x5f2, 0x19, 1.0f, At(12, {30, 0, 0}), white, false, tilt},
                                           {0x6eb, 3, 0.5f, At(12, {30, 0, 0}), glm::vec3(5.0f), false, tilt}}};
    struct Case
    {
        std::uint8_t action;
        float frame;
        std::size_t made;
        std::int32_t sound;
    };
    for (const Case& value :
         {Case{3, 5.25f, 5, 0x249}, Case{3, 5.0f, 5, 0x249}, Case{3, 5.5f, 5, 0x249}, Case{3, 4.0f, 4, 0x249},
          Case{3, 5.75f, 4, 0x249}, Case{3, 3.5f, 3, 0x249}, Case{4, 5.25f, 3, 0x24a}})
    {
        SCOPED_TRACE(testing::Message() << int{value.action} << " " << value.frame);
        ClearParticles();
        m_sprites.Clear();
        m_world->Runtime().lastSoundId.reset();
        ActorState& actor = Monster(0x1b7, value.action, value.frame);
        actor.subtype = 0;
        actor.attackTarget = 0x190;
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(Lorencia, random);
        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const CrtRandom expected =
            ExpectTheBoneParticles(*m_particles, std::span(all).first(value.made), {}, CrtRandom(21));
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_TRUE(LiveSprites().empty());
        EXPECT_EQ(Sound(), value.sound);
        EXPECT_EQ(actor.subtype, 1);
    }
}

// 0x42803b..0x428222: el 0x1b8 crea la chispa verde 0x5c6/7 (0x428164) en el punto (60, −30, 0) de la cabeza, con
// ángulos (0, 0, 0), la luz (0; rand() % 10 · 0,01f + 0,2f; 0) de su 0x5ad a 3 y escala 1, antes de sus sonidos.
TEST_F(MonsterHelpersTest, The1b8SparksGreenAtItsHead)
{
    Name("Monster102_Head", 6);
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        ClearParticles();
        m_sprites.Clear();
        m_world->Runtime().lastSoundId.reset();
        ActorState& actor = Monster(0x1b8, 3);
        actor.subtype = 0;
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(map, random);
        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(21);
        const float green = static_cast<float>(static_cast<double>(expected.Next() % 10) * static_cast<double>(0.01f) +
                                               static_cast<double>(0.2f));
        const glm::vec3 head = At(6, {60.0f, -30.0f, 0.0f});
        Expect(expected, 0x5c6, 7, head, {}, {0.0f, green, 0.0f});
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheParticles();
        ASSERT_EQ(LiveSprites().size(), 1u);
        EXPECT_EQ(LiveSprites()[0].light, glm::vec3(0.0f, green, 0.0f));
        EXPECT_EQ(LiveSprites()[0].position, head);
        EXPECT_FLOAT_EQ(LiveSprites()[0].scale, 3.0f);
        EXPECT_EQ(Sound(), 0x24e);
        EXPECT_EQ(actor.subtype, 1);
    }
}

// 0x4282ea..0x4285a9: el 0x1ba, después de sus sonidos, crea en cada cuerno la chispa 0x5c6/6 (0x42849c) con ángulos
// (0, 0, 0), la luz (0,4; rand() % 10 · 0,01f + 0,8f; 0,5) de su 0x5ad y escala 1; en la 4, el humo 0x5f2/26
// (0x4285a4) en el punto (0, −100, 20) de Monster104_Footsteps, con los ángulos del objeto, (0,5; 0,7f; 0,5) y escala
// 1. En la 3, sin el humo.
TEST_F(MonsterHelpersTest, The1baSparksAtItsHornsAndSmokesAtItsFoot)
{
    for (std::uint32_t horn = 0; horn < 6; ++horn)
        Name("Monster104_Horn" + std::to_string(horn), 30 + horn);
    Name("Monster104_Footsteps", 40);
    const glm::vec3 foot{0.5f, std::bit_cast<float>(0x3f333333u), 0.5f};
    for (const std::uint8_t action : {std::uint8_t{4}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        ClearParticles();
        m_sprites.Clear();
        m_world->Runtime().lastSoundId.reset();
        ActorState& actor = Monster(0x1ba, action);
        actor.subtype = 0;
        actor.angles = {0.0f, 0.0f, 45.0f};
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(Devias, random);
        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(21);
        for (std::uint32_t horn = 0; horn < 6; ++horn)
        {
            const float green = static_cast<float>(
                static_cast<double>(expected.Next() % 10) * static_cast<double>(0.01f) + static_cast<double>(0.8f));
            Expect(expected, 0x5c6, 6, At(30 + horn), {}, {0.4f, green, 0.5f});
        }
        if (action == 4)
            Expect(expected, 0x5f2, 26, At(40, {0.0f, -100.0f, 20.0f}), actor.angles, foot);
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheParticles();
        EXPECT_EQ(LiveParticles().size(), action == 4 ? 7u : 6u);
        EXPECT_EQ(LiveSprites().size(), 6u);
        EXPECT_EQ(Sound(), action == 4 ? 0x254 : 0x253);
    }
}

// 0x4285b1..0x428daf: el 0x1bb crea una fanta 0x6ec/6 en cada mano, la izquierda primero, con ángulos (0, 0, 0) y la
// luz (rand() % 8 · 0,1f + 0,6f; 0; 0) de sus 0x5ad: en la 4, las dos a 5 en el punto de la mano (0x428adc y
// 0x428d56); en las demás, la izquierda a 2,5 (0x428c52) y la derecha a 2,1f en el punto (0, 0, −10) de la mano
// (0x428d56). En la 4 de 9 a 10, después de las dos, el rayo blanco 0x5e8/1 en el punto de la mano derecha (0x428da7),
// sin rand().
TEST_F(MonsterHelpersTest, The1bbLightsAFantaInEachHand)
{
    for (const char* bone :
         {"Monster105_R_Eye", "Monster105_L_Eye", "Monster105_L_Arm00", "Monster105_L_Arm01", "Monster105_L_Arm02"})
        Name(bone, 20);
    Name("Monster105_L_Hand", 21);
    Name("Monster105_R_Hand", 22);
    struct Case
    {
        std::uint8_t action;
        float frame;
        float left;
        float right;
        glm::vec3 offset;
        std::optional<std::string_view> address;
        std::int32_t sound;
    };
    const float slight = std::bit_cast<float>(0x40066666u);
    for (const Case& value :
         {Case{4, 0.0f, 5.0f, 5.0f, {}, std::nullopt, 0x25e}, Case{4, 9.0f, 5.0f, 5.0f, {}, std::nullopt, 0x25e},
          Case{8, 9.5f, 2.5f, slight, {0.0f, 0.0f, -10.0f}, std::nullopt, 0x25d},
          Case{3, 0.0f, 2.5f, slight, {0.0f, 0.0f, -10.0f}, std::nullopt, 0x25f}})
    {
        SCOPED_TRACE(testing::Message() << static_cast<int>(value.action) << " " << value.frame);
        ClearParticles();
        m_sprites.Clear();
        m_world->Runtime().lastSoundId.reset();
        Monster(0x1bb, value.action, value.frame).subtype = 0;
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(Lorencia, random);
        const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
        if (value.address)
            EXPECT_TRUE(CutAt(drawn, *value.address)) << (drawn.HasValue() ? "" : drawn.Error());
        else
            EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(21);
        const float red = static_cast<float>(static_cast<double>(expected.Next() % 8) * static_cast<double>(0.1f) +
                                             static_cast<double>(0.6f));
        const glm::vec3 light{red, 0.0f, 0.0f};
        Expect(expected, 0x6ec, 6, At(21), {}, light, value.left);
        Expect(expected, 0x6ec, 6, At(22, value.offset), {}, light, value.right);
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheParticles();
        EXPECT_EQ(LiveParticles().size(), 2u);
        EXPECT_EQ(LiveSprites().size(), 5u);
        EXPECT_EQ(Sound(), value.sound);
        auto& ray = m_world->Map().effects.Effects()[0];
        EXPECT_EQ(ray.alive, value.action == 4 && value.frame >= 9.0f);
        if (ray.alive)
        {
            EXPECT_EQ(ray.type, 0x5e8);
            EXPECT_EQ(ray.transform.position, At(22));
            ray.alive = false;
        }
    }
}

// Con la tabla llena, sus partículas no crean nada ni sacan rand() y siguen: el 0x1b7 suena y en la 3 desde el frame
// 6,9 con el blanco en el pool crea el joint 0x618/0x10 (0x428036 salta a 0x427a8d: sus tres rand()); el 0x1b8
// suena; el 0x1ba da sus seis cuernos, cada uno con su rand() % 10, y en la 4 igual pide el punto del pie; el 0x1bb
// pide los puntos de las dos manos y en la 4 de 9 a 10 crea el rayo blanco 0x5e8/1 en la mano derecha, sin rand().
TEST_F(MonsterHelpersTest, The1b7To1bbFollowTheirTailWithAFullTable)
{
    Name("Monster101_L_Arm", 12);
    Name("Monster101_R_Arm", 13);
    Name("Monster101_Head", 14);
    Name("Monster102_Head", 6);
    for (std::uint32_t horn = 0; horn < 6; ++horn)
        Name("Monster104_Horn" + std::to_string(horn), 30 + horn);
    Name("Monster104_Footsteps", 40);
    for (const char* bone : {"Monster105_R_Eye", "Monster105_L_Eye", "Monster105_L_Arm00", "Monster105_L_Arm01",
                             "Monster105_L_Arm02", "Monster105_L_Hand", "Monster105_R_Hand"})
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
         {Case{0x1b7, 3, 6.9f, std::nullopt, 0, 3, 0x249}, Case{0x1b7, 3, 6.8f, std::nullopt, 0, 0, 0x249},
          Case{0x1b8, 4, 0.0f, std::nullopt, 1, 1, 0x24f}, Case{0x1ba, 4, 0.0f, std::nullopt, 6, 6, 0x254},
          Case{0x1bb, 4, 9.0f, std::nullopt, 5, 1, 0x25e}, Case{0x1bb, 4, 10.0f, std::nullopt, 5, 1, 0x25e},
          Case{0x1bb, 4, 10.5f, std::nullopt, 5, 1, 0x25e}, Case{0x1bb, 8, 9.5f, std::nullopt, 5, 1, 0x25d}})
    {
        for (const std::int32_t map : {Lorencia, Devias})
        {
            SCOPED_TRACE(testing::Message() << value.type << " " << value.frame << " " << map);
            m_sprites.Clear();
            m_world->Runtime().lastSoundId.reset();
            ActorState& actor = Monster(value.type, value.action, value.frame);
            actor.subtype = 0;
            actor.attackTarget = 0x18f;
            CrtRandom random(21);
            MonsterHelperPass pass = Pass(map, random);
            const auto drawn = Client::Scenes::DrawTheMonsterHelpers(pass);
            if (value.address)
            {
                EXPECT_TRUE(CutAt(drawn, *value.address)) << (drawn.HasValue() ? "" : drawn.Error());
            }
            else
            {
                EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
            }
            EXPECT_EQ(LiveSprites().size(), value.sprites);
            EXPECT_EQ(random.State(), AfterDraws(21, value.draws).State());
            EXPECT_EQ(Sound(), value.sound);
            if (value.type == 0x1bb && value.action == 4 && value.frame <= 10.0f)
            {
                const auto& ray = m_world->Map().effects.Effects()[0];
                EXPECT_TRUE(ray.alive);
                EXPECT_EQ(ray.type, 0x5e8);
                EXPECT_EQ(ray.subtype, 1);
                EXPECT_EQ(ray.transform.scale, 3.0f);
                EXPECT_EQ(ray.light, glm::vec3(1.0f));
                EXPECT_EQ(ray.transform.position, At(20));
                m_world->Map().effects.Effects()[0].alive = false;
            }
        }
    }
    // El 0x618/0x10 del 0x1b7 (0x427fea..0x428036): desde el blanco, 100 más arriba, con sus ángulos, el actor de dueño
    // y ancho 20; el sentido (rand() impar: 1) y los ángulos x e y en rand() % 45 (0x53217c..0x5321d8).
    {
        auto& joints = m_world->DrawTail().joints;
        for (auto& joint : joints)
            joint.alive = false;
        ActorState& actor = Monster(0x1b7, 3, 6.9f);
        actor.attackTarget = 0x18f;
        ActorState& target = m_world->Actors()[0x18f];
        target.position = {500.0f, 600.0f, 70.0f};
        target.angles = {0.0f, 0.0f, 45.0f};
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(Lorencia, random);
        ASSERT_TRUE(Client::Scenes::DrawTheMonsterHelpers(pass).HasValue());
        const auto& joint = joints[0];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x618);
        EXPECT_EQ(joint.subtype, 0x10);
        EXPECT_EQ(joint.ownerContext, &actor);
        EXPECT_EQ(joint.position, glm::vec3(500.0f, 600.0f, 170.0f));
        EXPECT_EQ(joint.width, 20.0f);
        CrtRandom expected(21);
        EXPECT_EQ(joint.field9ec, expected.Next() % 2 != 0 ? 1 : -1);
        EXPECT_EQ(joint.angles.x, static_cast<float>(expected.Next() % 45));
        EXPECT_EQ(joint.angles.y, static_cast<float>(expected.Next() % 45));
        EXPECT_EQ(joint.angles.z, 45.0f);
        EXPECT_EQ(random.State(), expected.State());
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

// Con la tabla llena, el 0x1ba en la 4 y el 0x1bb igual piden el punto del pie (0x428540) y el de la mano derecha
// (0x428b32 en la 4, 0x428cf8 en las demás): sin el hueso, cortan ahí.
TEST_F(MonsterHelpersTest, The1baAnd1bbAskForTheirPointsWithAFullTable)
{
    for (std::uint32_t horn = 0; horn < 6; ++horn)
        Name("Monster104_Horn" + std::to_string(horn), 30 + horn);
    for (const char* bone : {"Monster105_R_Eye", "Monster105_L_Eye", "Monster105_L_Arm00", "Monster105_L_Arm01",
                             "Monster105_L_Arm02", "Monster105_L_Hand"})
        Name(bone, 20);
    FillParticles();
    for (const auto& [type, action, address] :
         {std::tuple{std::uint16_t{0x1ba}, std::uint8_t{4}, std::string_view{"0x428540"}},
          std::tuple{std::uint16_t{0x1bb}, std::uint8_t{4}, std::string_view{"0x428b32"}},
          std::tuple{std::uint16_t{0x1bb}, std::uint8_t{8}, std::string_view{"0x428cf8"}}})
    {
        SCOPED_TRACE(address);
        Monster(type, action).subtype = 0;
        CrtRandom random(21);
        MonsterHelperPass pass = Pass(Lorencia, random);
        EXPECT_TRUE(CutAt(Client::Scenes::DrawTheMonsterHelpers(pass), address));
    }
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

// 0x432bd0: las estelas publican diez poses y agregan las muestras a las cintas del GameDrawTail.
TEST_F(MonsterHelpersTest, OtherSwingsEmitTheirRibbonSamplesAndKeepPoolLimits)
{
    Name("Left_Hand", 17);
    m_models.ResizeBones(90);
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::string_view address;
        bool particlePoolFull;
        std::size_t ribbons;
        bool effectPoolFull = false;
    };
    for (const Case& value :
         {Case{0x1ab, 3, 4.0f, "", false, 1}, Case{0x1ab, 3, 4.0f, "", true, 1}, Case{0x1ab, 3, 4.5f, "", false, 1},
          Case{0x1ae, 8, 4.0f, "", false, 0}, Case{0x1ae, 8, 4.0f, "", true, 0}, Case{0x1ae, 4, 4.7f, "", false, 1},
          Case{0x1ae, 3, 4.7f, "", false, 1}, Case{0x1af, 4, 3.0f, "", false, 1}, Case{0x1af, 4, 4.7f, "", false, 1},
          Case{0x1af, 4, 4.7f, "", false, 1, true}, Case{0x1b1, 4, 3.0f, "", false, 2},
          Case{0x1b3, 3, 3.0f, "", false, 1}, Case{0x1b4, 3, 3.0f, "", false, 2}})
    {
        SCOPED_TRACE(testing::Message() << value.address << (value.particlePoolFull ? " llena" : ""));
        ClearParticles();
        ClearModelEffects();
        m_world->DrawTail().ribbons = {};
        if (value.particlePoolFull)
            FillParticles();
        if (value.effectPoolFull)
            FillModelEffects();
        m_world->PoseCarry() = {};
        m_world->Runtime().map = value.type % 2 == 0 ? Lorencia : Devias;
        ActorState& actor = Monster(value.type, value.action, value.frame);
        actor.scale = 2.0f;
        actor.hands[0].bone = 7;
        if (value.action < 8)
            m_models.SetBoneActionPosition(7, value.action, {0.0f, 0.0f, 7.0f});
        m_world->PlaceModel(actor);
        CrtRandom random(1);
        const auto stepped =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0.0f);
        if (value.address.empty())
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        else
            EXPECT_TRUE(CutAt(stepped, value.address)) << (stepped.HasValue() ? "" : stepped.Error());
        const int draws = value.particlePoolFull                                                ? 1
                          : value.type == 0x1ab && value.frame == 4                             ? 31
                          : value.type == 0x1ae && value.action == 8                            ? 6
                          : value.type == 0x1af && value.frame == 4.7f && !value.effectPoolFull ? 7
                                                                                                : 1;
        EXPECT_EQ(random.State(), AfterDraws(1, draws).State());
        if (!value.particlePoolFull && value.type == 0x1ab && value.frame == 4)
        {
            const auto sparks = LiveParticles();
            ASSERT_EQ(sparks.size(), 10u);
            EXPECT_EQ(sparks.front().type, 0x640);
            EXPECT_EQ(sparks.front().light, glm::vec3(1));
            EXPECT_EQ(sparks.back().light, (glm::vec3{0, 0, 5}));
        }
        if (!value.particlePoolFull && value.type == 0x1ae && value.action == 8)
        {
            const auto hand = LiveParticles();
            ASSERT_EQ(hand.size(), 3u);
            EXPECT_EQ(hand[0].type, 0x5f2);
            EXPECT_EQ(hand[0].subtype, 27);
            EXPECT_EQ(hand[0].texture, 0x6b1);
            EXPECT_EQ(hand[0].position, (glm::vec3{100, 200, 44}));
            EXPECT_EQ(hand[0].light, (glm::vec3{0.2f, 0.2f, 0.7f}));
            EXPECT_EQ(hand[1].type, 0x5ad);
            EXPECT_EQ(hand[1].subtype, 2);
            EXPECT_EQ(hand[1].scale, 0.8f);
            EXPECT_EQ(hand[2].type, 0x5ad);
            EXPECT_EQ(hand[2].subtype, 2);
            EXPECT_EQ(hand[2].scale, 0.6f);
        }
        const bool posed = !(value.type == 0x1ae && value.action == 8);
        EXPECT_EQ(m_world->PoseCarry().models.count(value.type), posed ? 1u : 0u);
        std::vector<const Client::Scenes::RibbonState*> ribbons;
        for (const auto& ribbon : m_world->DrawTail().ribbons)
        {
            if (ribbon.alive && ribbon.ownerContext == &actor)
                ribbons.push_back(&ribbon);
        }
        EXPECT_EQ(ribbons.size(), value.ribbons);
        for (const auto* ribbon : ribbons)
        {
            EXPECT_EQ(ribbon->count, 10);
            EXPECT_EQ(ribbon->type, 3);
            EXPECT_EQ(ribbon->ownerEquipment, &actor.level);
            EXPECT_EQ(ribbon->ownerContext, &actor);
            EXPECT_EQ(ribbon->life, 15);
            EXPECT_EQ(ribbon->light, value.type == 0x1ab   ? glm::vec3(0.0f, 0.0f, 5.0f)
                                     : value.type == 0x1af ? glm::vec3(0.2f, 1.0f, 0.4f)
                                                           : glm::vec3(1.0f));
        }
        if (!ribbons.empty())
        {
            const auto& first = *ribbons.front();
            const auto transformOffset = [this](std::size_t bone, const glm::vec3& offset)
            {
                const auto& matrix = m_world->PoseCarry().bones[bone];
                return Client::Render::Models::OriginalVectorTransform(
                    offset, Client::Render::Models::OriginalMatrixOf(matrix));
            };
            std::size_t firstBone = 0;
            std::size_t secondBone = 0;
            glm::vec3 firstOffset{};
            glm::vec3 secondOffset{};
            if (value.type == 0x1ab || value.type == 0x1ae)
            {
                firstBone = secondBone = 7;
                firstOffset = {0.0f, 0.0f, -60.0f};
                secondOffset = {0.0f, 0.0f, -150.0f};
            }
            else if (value.type == 0x1af)
            {
                firstBone = secondBone = 27;
                firstOffset = {0.0f, 250.0f, 0.0f};
            }
            else if (value.type == 0x1b1)
            {
                firstBone = secondBone = 80;
                firstOffset = {0.0f, 0.0f, -90.0f};
            }
            else if (value.type == 0x1b3)
            {
                firstBone = secondBone = 25;
                firstOffset = {0.0f, 0.0f, 120.0f};
            }
            else
            {
                firstBone = 12;
                secondBone = 16;
                firstOffset = {20.0f, 0.0f, 0.0f};
                secondOffset = {60.0f, 0.0f, 0.0f};
            }
            EXPECT_EQ(first.first[0], transformOffset(firstBone, firstOffset));
            EXPECT_EQ(first.second[0], transformOffset(secondBone, secondOffset));
            EXPECT_EQ(first.key, value.type == 0x1b3 ? 25 : value.type == 0x1b4 ? 23 : 80);
        }
        if (value.ribbons > 0)
        {
            const auto& transform = m_world->PoseCarry().models.at(value.type);
            EXPECT_EQ(transform.position, Origin);
            EXPECT_FLOAT_EQ(transform.scale, 2.0f);
            float lastFrame = value.frame - 0.5f;
            for (int sample = 0; sample < 9; ++sample)
                lastFrame = static_cast<float>(static_cast<double>(lastFrame) + static_cast<double>(0.05f));
            EXPECT_EQ(m_world->FaunaResources().PoseKey(value.type), static_cast<std::int16_t>(lastFrame));
        }
    }
}

TEST_F(MonsterHelpersTest, SwingRibbonPoolFullStillPosesAndMatchingOwnersAppend)
{
    m_models.ResizeBones(90);
    ActorState& actor = Monster(0x1b1, 4, 3.0f);
    m_world->DrawTail().ribbons.fill(Client::Scenes::RibbonState{.alive = true, .ownerContext = m_world.get()});
    CrtRandom full(1);
    const auto fullPoolStep =
        Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, full, m_particles.get(), 0.0f);
    ASSERT_TRUE(fullPoolStep.HasValue()) << fullPoolStep.Error();
    EXPECT_EQ(full.State(), AfterDraws(1, 1).State());
    EXPECT_EQ(m_world->PoseCarry().models.at(actor.type).position, Origin);
    EXPECT_FLOAT_EQ(m_world->PoseCarry().models.at(actor.type).scale, actor.scale);
    EXPECT_EQ(m_world->FaunaResources().PoseKey(actor.type), 2);
    EXPECT_TRUE(std::ranges::all_of(m_world->DrawTail().ribbons,
                                    [](const Client::Scenes::RibbonState& ribbon) { return ribbon.count == 0; }));

    m_world->DrawTail().ribbons = {};
    CrtRandom append(1);
    ASSERT_TRUE(
        Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, append, m_particles.get(), 0.0f).HasValue());
    ASSERT_TRUE(
        Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, append, m_particles.get(), 0.0f).HasValue());
    EXPECT_EQ(append.State(), AfterDraws(1, 2).State());
    std::vector<const Client::Scenes::RibbonState*> own;
    for (const auto& ribbon : m_world->DrawTail().ribbons)
    {
        if (ribbon.alive && ribbon.ownerContext == &actor)
            own.push_back(&ribbon);
    }
    ASSERT_EQ(own.size(), 2u);
    EXPECT_EQ(own[0]->count, 20);
    EXPECT_EQ(own[1]->count, 20);
    EXPECT_EQ(own[0]->key, 80);
    EXPECT_EQ(own[1]->key, 84);
}

TEST_F(MonsterHelpersTest, SwingEffectsContinueWithFreeOrFullModelEffectPools)
{
    m_models.ResizeBones(90);
    ActorState& type1af = Monster(0x1af, 4, 4.7f);
    FillParticles();
    CrtRandom firstRandom(1);
    const auto effectAfterRibbon =
        Client::Scenes::StepTheSwingsOf1abTo1b4(type1af, Slot, *m_world, firstRandom, m_particles.get(), 0.0f);
    ASSERT_TRUE(effectAfterRibbon.HasValue()) << effectAfterRibbon.Error();
    EXPECT_EQ(firstRandom.State(), AfterDraws(1, 1).State());
    ASSERT_EQ(m_world->DrawTail().ribbons[0].count, 10);

    ClearParticles();
    m_world->DrawTail().ribbons = {};
    m_world->PoseCarry() = {};
    FillModelEffects();
    CrtRandom fullEffectsRandom(1);
    const auto fullEffects =
        Client::Scenes::StepTheSwingsOf1abTo1b4(type1af, Slot, *m_world, fullEffectsRandom, m_particles.get(), 0.0f);
    ASSERT_TRUE(fullEffects.HasValue()) << fullEffects.Error();
    EXPECT_EQ(fullEffectsRandom.State(), AfterDraws(1, 1).State());
    ASSERT_EQ(m_world->DrawTail().ribbons[0].count, 10);

    ClearModelEffects();
    m_world->DrawTail().ribbons = {};
    m_world->PoseCarry() = {};
    ActorState& type1ae = Monster(0x1ae, 4, 4.7f);
    const auto beforePose =
        Client::Scenes::StepTheSwingsOf1abTo1b4(type1ae, Slot, *m_world, fullEffectsRandom, m_particles.get(), 0.0f);
    ASSERT_TRUE(beforePose.HasValue()) << beforePose.Error();
    const auto& tide = m_world->Map().effects.Effects()[0];
    EXPECT_TRUE(tide.alive);
    EXPECT_EQ(tide.type, 0x145);
    EXPECT_EQ(tide.subtype, 2);
    EXPECT_EQ(tide.life, 30);
    EXPECT_FLOAT_EQ(tide.transform.scale, 0);
    EXPECT_EQ(tide.ownerContext, &type1ae);
    EXPECT_EQ(m_world->DrawTail().ribbons[0].count, 10);
    EXPECT_EQ(m_world->PoseCarry().models.count(type1ae.type), 1u);

    FillModelEffects();
    m_world->DrawTail().ribbons = {};
    m_world->PoseCarry() = {};
    CrtRandom noEffect(1);
    const auto fullEffectPool =
        Client::Scenes::StepTheSwingsOf1abTo1b4(type1ae, Slot, *m_world, noEffect, m_particles.get(), 0.0f);
    ASSERT_TRUE(fullEffectPool.HasValue()) << fullEffectPool.Error();
    EXPECT_EQ(noEffect.State(), AfterDraws(1, 1).State());
    EXPECT_TRUE(m_world->DrawTail().ribbons[0].alive);
    EXPECT_EQ(m_world->DrawTail().ribbons[0].count, 10);
    EXPECT_EQ(m_world->PoseCarry().models.count(type1ae.type), 1u);
}
TEST_F(MonsterHelpersTest, DeathSpiritSwingReadsItsTargetBeforeFourOrderedReservations)
{
    m_models.ResizeBones(90);
    ActorState& actor = Monster(0x1af, 4, 4.7f);
    actor.angles = {10, 20, 30};
    actor.scale = 2;
    actor.attackTarget = 2;
    auto& target = m_world->Actors()[2];
    target.boundsTop = 123;
    target.dword60 = 12;
    for (const std::size_t capacity : {0u, 1u, 2u, 3u, 4u})
    {
        SCOPED_TRACE(capacity);
        ClearParticles();
        FillModelEffects();
        m_world->DrawTail().ribbons = {};
        for (std::size_t i = 0; i < capacity; ++i)
            m_world->Map().effects.Effects()[i].alive = false;
        CrtRandom random(7);
        const auto result =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(random.State(), AfterDraws(7, static_cast<int>(1 + 3 * (capacity / 2))).State());
        EXPECT_EQ(m_world->DrawTail().ribbons[0].count, 10);
        for (std::size_t i = 0; i < capacity; ++i)
        {
            const auto& effect = m_world->Map().effects.Effects()[i];
            EXPECT_EQ(effect.type, 0x147);
            EXPECT_EQ(effect.subtype, static_cast<int>(i % 2));
            EXPECT_EQ(effect.ownerContext, &target);
            EXPECT_EQ(effect.transform.position, (glm::vec3{260, 200, 100}));
            EXPECT_EQ(effect.transform.angles, (glm::vec3{0, 0, i < 2 ? 120.0f : -60.0f}));
            EXPECT_EQ(effect.life, i % 2 == 0 ? 30 : 12);
            if (i % 2 == 0)
            {
                EXPECT_EQ(effect.start, (glm::vec3{0, 0, 123}));
            }
        }
        EXPECT_EQ(LiveParticles().size(), capacity / 2);
    }
    for (const bool full : {false, true})
    {
        ClearModelEffects();
        if (full)
            FillModelEffects();
        actor.attackTarget = 0xffff;
        m_world->DrawTail().ribbons = {};
        CrtRandom random(7);
        const auto result =
            Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0);
        EXPECT_TRUE(CutAt(result, "0x4335f6"));
        EXPECT_EQ(random.State(), AfterDraws(7, 1).State());
        EXPECT_EQ(m_world->DrawTail().ribbons[0].count, 10);
    }
}
TEST_F(MonsterHelpersTest, DeathSpiritSwingUsesBothAttackActionsAndStrictFrameEndpoints)
{
    m_models.ResizeBones(90);
    FillParticles();
    for (const std::uint8_t action : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
        for (const float frame : {4.5f, 4.7f, 5.0f})
        {
            SCOPED_TRACE(testing::Message() << static_cast<int>(action) << ":" << frame);
            ClearModelEffects();
            m_world->DrawTail().ribbons = {};
            ActorState& actor = Monster(0x1af, action, frame);
            actor.attackTarget = 2;
            CrtRandom random(7);
            const auto result =
                Client::Scenes::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, random, m_particles.get(), 0);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            const std::size_t expected = action != 2 && frame > 4.5f && frame < 5 ? 4u : 0u;
            EXPECT_EQ(static_cast<std::size_t>(std::ranges::count_if(m_world->Map().effects.Effects(),
                                                                     [](const auto& effect) { return effect.alive; })),
                      expected);
            EXPECT_EQ(random.State(), AfterDraws(7, 1).State());
        }
}
} // namespace
