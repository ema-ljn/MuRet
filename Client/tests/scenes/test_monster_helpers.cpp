#include "Models/IModelLibrary.h"
#include "Monsters/Drawing/MonsterHelpers.h"

#include "Kernel/DrawTailState.h"
#include "Audio/SoundId.h"
#include "Effects/ModelEffects.h"
#include <algorithm>

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/ActorCreation.h"
#include "Kernel/ActorState.h"
#include "Joints/FaunaJointCreation.h"
#include "Assets/FaunaModels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldMap.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainDecal.h"
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

// The monster helpers (0x441400, 0x434180, 0x4271d0 and 0x432bd0): literal vectors from the ASM c32bbd5f, without
// running the original. The own bones do not rotate and bone k is at (0, 0, k), so a named bone's point shows which
// bone it comes from.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Monsters::Drawing::MonsterHelperPass;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

constexpr std::size_t Slot = 7;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Crywolf = 34;
const glm::vec3 Origin{100.0f, 200.0f, 10.0f};
const glm::vec3 RedEyes{0.9f, 0.2f, 0.1f};

class SwingModels final : public Client::Models::IModelLibrary
{
public:
    SwingModels()
    {
        using namespace Client::Models;
        SkeletonBone bone;
        bone.actions.assign(8, std::vector<BoneKey>(10));
        m_skeleton.bones.assign(20, bone);
        m_skeleton.actions.assign(8, ActionInfo{.repeats = true, .speed = 0.5f, .keyCount = 10});
    }

    void ResizeBones(std::size_t count)
    {
        using namespace Client::Models;
        SkeletonBone bone;
        bone.actions.assign(8, std::vector<BoneKey>(10));
        m_skeleton.bones.resize(count, bone);
    }

    void SetBoneActionPosition(std::size_t bone, std::size_t action, const glm::vec3& position)
    {
        for (auto& key : m_skeleton.bones.at(bone).actions.at(action))
            key.position = position;
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }

private:
    Client::Models::Skeleton m_skeleton;
};

class MonsterHelpersTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_faunaModels.Bind(&m_models);
    }

    void TearDown() override
    {
        m_faunaModels.Bind(nullptr);
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
        actor.bodyPose.assign(120, Client::Math::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < actor.bodyPose.size(); ++bone)
            actor.bodyPose[bone][3] = glm::vec3(0.0f, 0.0f, static_cast<float>(bone));
        m_world->PlaceModel(actor);
        return actor;
    }

    void Name(std::string_view name, std::uint32_t bone)
    {
        Client::Actors::NameABone(m_world->Runtime(), Slot, name, bone);
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

    std::vector<Client::Particles::Sprite> LiveSprites()
    {
        std::vector<Client::Particles::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    std::optional<Client::Audio::SoundId> Sound() const
    {
        return m_world->Runtime().lastSoundId;
    }

    // 0x53fe70's table full: the original creates nothing and draws no rand().
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

    static std::vector<Client::Particles::Particle> Live(std::span<const Client::Particles::Particle> all)
    {
        std::vector<Client::Particles::Particle> live;
        for (const auto& particle : all)
        {
            if (particle.alive)
                live.push_back(particle);
        }
        return live;
    }

    std::vector<Client::Particles::Particle> LiveParticles() const
    {
        return Live(std::as_const(*m_particles).All());
    }

    // The same creation in a separate table, drawing the particle's rand() in order from `expected`.
    void Expect(CrtRandom& expected, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                const glm::vec3& angles, const glm::vec3& light, float scale = 1.0f, float clock = 0.0f)
    {
        const auto created = m_reference->CreateChecked(type, position, angles, light, subtype, scale, clock, expected);
        ASSERT_TRUE(created.HasValue());
        EXPECT_TRUE(created.Value().has_value());
    }

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
    Client::Assets::FaunaModels m_faunaModels;
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::Particles::FrameSprites m_sprites;
    std::unique_ptr<Client::Particles::Particles> m_particles = std::make_unique<Client::Particles::Particles>();
    std::unique_ptr<Client::Particles::Particles> m_reference = std::make_unique<Client::Particles::Particles>();
};

// FLD T; FMUL rate; FSIN; FMUL factor with a single R53 (ADR0014), like the original.
double ClockSine(float clock, float rate, float factor)
{
    const auto product =
        Client::Math::Detail::SineProductPC53(static_cast<double>(clock) * static_cast<double>(rate), factor);
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

// 0x1a8 leaves 0x441400 without the tail, and the 0x434180 chain is for map 34 only.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(actor.subtype, 1);
        EXPECT_EQ(random.State(), 1u);
        EXPECT_TRUE(LiveSprites().empty());
        EXPECT_FALSE(Sound().has_value());
    }
}

// 0x441440..0x44164b: Monster81's eyes, the right one first; in action 2, rand() % 15 == 0 requests 0x1f5 plus
// rand() % 2.
TEST_F(MonsterHelpersTest, The1a3LightsItsEyesAndGrowlsWhenIdle)
{
    Name("Monster81_EyeRight", 29);
    Name("Monster81_EyeLeft", 30);
    ActorState& actor = Monster(0x1a3, 2);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 15 == 0; });
    CrtRandom random(seed);
    MonsterHelperPass pass = Pass(Lorencia, random);

    const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);

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
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x1f5 + (expected.Next() & 1)});
    EXPECT_EQ(random.State(), AfterDraws(seed, 2).State());
    EXPECT_EQ(actor.subtype, 0);
}

// 0x4415fd..0x44164b and 0x442dd2: the attack sounds once (+4 at 1) and action 0 resets it to 0. Between original
// frames, the request is not kept.
TEST_F(MonsterHelpersTest, The1a3AttackSoundsOnceUntilItRests)
{
    Name("Monster81_EyeRight", 29);
    Name("Monster81_EyeLeft", 30);
    ActorState& actor = Monster(0x1a3, 3);
    CrtRandom random(5);
    MonsterHelperPass pass = Pass(Devias, random);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass).HasValue());
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x1f7 + (CrtRandom(5).Next() & 1)});
    EXPECT_EQ(random.State(), AfterDraws(5, 1).State());

    m_world->Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass).HasValue());
    EXPECT_FALSE(Sound().has_value());
    EXPECT_EQ(random.State(), AfterDraws(5, 1).State());

    actor.animation.action = 6;
    actor.subtype = 0;
    MonsterHelperPass between = Pass(Devias, random, false);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(between).HasValue());
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_FALSE(Sound().has_value());

    actor.animation.action = 0;
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass).HasValue());
    EXPECT_EQ(actor.subtype, 0);
}

// Without the named bone the registration records (0x401000), 0x4010b0 would leave the point unwritten: it cuts at the
// call.
TEST_F(MonsterHelpersTest, AMissingNamedBoneCutsAtItsCall)
{
    Name("Monster81_EyeRight", 29);
    Monster(0x1a3, 0);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
    EXPECT_TRUE(CutAt(drawn, "0x441552")) << drawn.Error();
    EXPECT_EQ(LiveSprites().size(), 1u);
}

// 0x441688..0x4416a5 and 0x442159..0x442179: when dying (action 6), 0x1a4 calls 0x4ee770(0, 0x13c, 0) and 0x1a6
// 0x4ee770(0, 0x13c, 1) on the body meshes; both set +0 to 0 and request their sound (0x209 and 0x1ff); 0x1a6 goes on
// with its poisons. Between original frames, no effects or sound. Without the meshes, they cut at the call.
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
            const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_FALSE(actor.alive);
            EXPECT_EQ(random.State(), AfterDraws(calm, death.draws).State());
            if (original)
            {
                ASSERT_EQ(asked.size(), 1u);
                EXPECT_EQ(asked[0], std::make_pair(std::int32_t{0x13c}, death.subtype));
                EXPECT_EQ(Sound(), Client::Audio::SoundId{death.sound});
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
    EXPECT_TRUE(CutAt(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(bare), "0x442165"));
}

// A particle a monster drops on a bone; without `angles`, it takes the shared ones.
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

// Checks `made` against the first slots of `particles` through a twin pool replaying `expected`; returns the rand()
// after.
CrtRandom ExpectTheBoneParticles(const Client::Particles::Particles& particles, std::span<const BoneParticle> made,
                                 const glm::vec3& angles, CrtRandom expected)
{
    auto twin = std::make_unique<Client::Particles::Particles>();
    const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::Actor, Slot};
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
        const Client::Particles::Particle& particle = particles.All()[index];
        const Client::Particles::Particle& reference = twin->All()[index];
        EXPECT_TRUE(particle.alive);
        EXPECT_EQ(particle.type, bone.type);
        EXPECT_EQ(particle.subtype, bone.subtype);
        EXPECT_EQ(particle.start, bone.point);
        EXPECT_EQ(particle.position, reference.position);
        EXPECT_EQ(particle.scale, reference.scale);
        EXPECT_EQ(particle.life, reference.life);
        EXPECT_EQ(particle.velocity, reference.velocity);
        EXPECT_EQ(particle.light, reference.light);
        EXPECT_EQ(particle.typedOwner, bone.owned ? owner : Client::Particles::ParticleOwner{});
    }
    EXPECT_FALSE(particles.All()[made.size()].alive);
    return expected;
}

// 0x441669..0x441d7d: 0x1a4 cuts at 0x4ee770 when dying without the body meshes. Otherwise, 0x4416aa..0x441a02, with
// light (1, 1, 1): at point (5, 10, 5) of the left hand the 0x6ec/3 fanta of 3.4f owned by it and the 0x5f2/21 smoke
// of 2.5f, at the right hand the 0x6ec/4 and another smoke, at point (−20, −2, −10) of the eye the 0x6ec/0 and the 0.8f
// smoke with no owner, and at the back another smoke. With the table full it goes on: rand() % 20 == 0 requests 0x205
// plus rand() % 2, and in action 3 from 8.4 to 10.2 with +4 at 0, +4 = 1 and the cut at its effects.
TEST_F(MonsterHelpersTest, The1a4LeavesFantasAndSmokesOnItsBones)
{
    Name("Monster82_LHand", 34);
    Name("Monster82_RHand", 45);
    Name("Monster82_Eye", 48);
    Name("Monster82_Back", 46);
    Monster(0x1a4, 6);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    EXPECT_TRUE(CutAt(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass), "0x441696"));
    const ActorState& walking = Monster(0x1a4, 0);
    const auto walked = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
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
        const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(crowded);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(growl);
        static_cast<void>(expected.Next());
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x205 + (expected.Next() & 1)});
        EXPECT_EQ(full.State(), expected.State());
    }
    const std::uint32_t quiet = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 20 != 0; });
    ActorState& actor = Monster(0x1a4, 3, 9.0f);
    actor.subtype = 0;
    CrtRandom attack(quiet);
    MonsterHelperPass attacking = Pass(Devias, attack);
    const auto struck = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(attacking);
    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x207});
    EXPECT_EQ(m_world->Map().effects.Effects()[0].type, 0x636);
}

// 0x441a8a..0x441c75, in Lorencia and Devias: in action 3 from 8.4 to 10.2 with +4 at 0, 0x1a4 strikes at its right
// hand (Monster82_RHand, a bone translated in x, y and z) with the bone's x and y and the character's z: the 0x636/2
// crater (white light), the 0x61c/6 joint (width 200, no owner, key −1) with angles (−90, 0, z) and the 0x5de/3 column
// with its angles and light (life 15). Then the same on each of the first 400 players seen and alive at more than 200
// (exactly 200 no; 313.0001f − 113 yes) and less than 1100 in the plane, the column with that player's angles and in
// white; record 400, even a valid player, no (0x441c60: 0x69780 = 400 · 0x438). The sound 0x207. The crater and the
// column draw no rand().
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
        // Bone 45 at (13, −7, 45): the hand ends at (113, 193, 55) and the strike at (113, 193, 10).
        actor.bodyPose[45][3] = glm::vec3(13.0f, -7.0f, 45.0f);
        const glm::vec3 hand{113.0f, 193.0f, Origin.z};
        const auto player = [this, &hand](std::size_t slot, const glm::vec3& offset, bool seen) -> ActorState&
        {
            ActorState& other = m_world->Actors()[slot];
            other = ActorState{};
            other.alive = true;
            other.seen = seen;
            other.byte88 = 1;
            other.type = 0x20a;
            other.position = hand + offset;
            other.angles = {0.0f, 0.0f, static_cast<float>(slot)};
            return other;
        };
        const ActorState& victim = player(20, {600.0f, 0.0f, 50.0f}, true);
        static_cast<void>(player(21, {150.0f, 0.0f, 0.0f}, true));
        static_cast<void>(player(22, {0.0f, 1100.0f, 0.0f}, true));
        static_cast<void>(player(23, {600.0f, 0.0f, 0.0f}, false));
        static_cast<void>(player(24, {200.0f, 0.0f, 0.0f}, true));
        ActorState& edge = player(25, {0.0f, 0.0f, 0.0f}, true);
        edge.position.x = 313.0001f; // 200.0000915… from the strike
        static_cast<void>(player(400, {600.0f, 0.0f, 0.0f}, true));
        CrtRandom attack(quiet);
        MonsterHelperPass attacking = Pass(map, attack);

        const auto struck = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(attacking);

        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        EXPECT_EQ(actor.subtype, 1);
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x207});
        const std::array<glm::vec3, 3> points{hand, victim.position, edge.position};
        CrtRandom replay = AfterDraws(quiet, 1);
        std::vector<Client::Kernel::JointState> twin(m_world->DrawTail().joints.size());
        const glm::vec3 down{-90.0f, 0.0f, 30.0f};
        for (const glm::vec3& at : points)
        {
            glm::vec3 point = at;
            glm::vec3 angles = down;
            ASSERT_TRUE(Client::Joints::CreateFaunaJoint(twin,
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
        const std::array<glm::vec3, 3> columnAngles{actor.angles, victim.angles, edge.angles};
        const std::array<glm::vec3, 3> columnLights{actor.light, glm::vec3(1.0f), glm::vec3(1.0f)};
        for (std::size_t strike = 0; strike < points.size(); ++strike)
        {
            SCOPED_TRACE(strike);
            const Client::Effects::ModelEffect& crater = effects[2 * strike];
            ASSERT_TRUE(crater.alive);
            EXPECT_EQ(crater.type, 0x636);
            EXPECT_EQ(crater.subtype, 2);
            EXPECT_EQ(crater.transform.position, points[strike]);
            EXPECT_EQ(crater.transform.angles, down);
            EXPECT_EQ(crater.light, glm::vec3(1.0f));
            EXPECT_EQ(crater.ownerContext, nullptr);
            const Client::Effects::ModelEffect& column = effects[(2 * strike) + 1];
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
        EXPECT_FALSE(effects[6].alive);
        EXPECT_FALSE(joints[3].alive);
        m_world->Actors()[400] = ActorState{};
    }
}

// 0x441d7d consumes forty rand() even with no free slots; 0x441d89 requests 0x208 without a draw.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const bool bursts = frame >= 5.0f && frame <= 5.7f;
        EXPECT_EQ(actor.subtype, bursts ? 1 : 0);
        EXPECT_EQ(Sound(), bursts ? std::optional{Client::Audio::SoundId{0x208}} : std::nullopt);
        EXPECT_EQ(random.State(), AfterDraws(quiet, bursts ? 41 : 1).State());
        if (bursts)
        {
            const auto again = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
            ASSERT_TRUE(again.HasValue()) << again.Error();
            CrtRandom expected = AfterDraws(quiet, 41);
            if (expected.Next() % 20 == 0)
                static_cast<void>(expected.Next());
            EXPECT_EQ(random.State(), expected.State());
        }
    }
}

// 0x441d96..0x4420f2: in action 4, the sound 0x202 plus rand() % 2, three rand() % 12 − 6 for the tail point, a 0x5fd
// at 2.5 with blend 1 and a 0x61e at 0.8, both with the own light, and two 0x5ca/0 with (1; 0.3; 0); then twelve loops
// of three rand() for the belly point, with the same two sprites and one 0x5ca/0. With the table full, the same
// sprites without the particles or their rand().
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

        const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom expected(77);
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x202 + (expected.Next() & 1)});
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

    // With the table full: 1 + 3 + 12 · 3 rand(), no new particles.
    FillParticles();
    m_sprites.Clear();
    Monster(0x1a5, 4).subtype = 0;
    CrtRandom full(77);
    MonsterHelperPass crowded = Pass(Devias, full);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
    EXPECT_EQ(full.State(), AfterDraws(77, 40).State());
    EXPECT_EQ(LiveSprites().size(), 26u);
}

// 0x4420f7..0x44281f: in action 3, rand() % 2 == 1 cuts at the 0x12e/3 stone, which would come out at a point the call
// did not write (Q-C1580); at 0, another one picks the poison and rand() % 32 + 50 times 0.025f is the scale of its
// 0x5f2/11, and the 0x12e/3 stone comes out at that point with its three rand() (0x442397) before the poisons. In
// action 4 with frame from 3.5 to 4.2, the 0x5f2/11 of each hand on the floor, scale rand() % 32 + 50 times 0.05f. At
// rest, three missed rand() % 10 do nothing but set +4 to 0; each hit creates its 0x5f2/0x17 with its rand().
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
    EXPECT_TRUE(CutAt(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass), "0x442397"));
    EXPECT_EQ(first.State(), AfterDraws(odd, 1).State());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x1fe});
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_TRUE(LiveParticles().empty());

    const std::uint32_t even = FirstSeedWhere([](CrtRandom& random) { return (random.Next() & 1) == 0; });
    CrtRandom second(even);
    MonsterHelperPass poisoned = Pass(Lorencia, second);
    const auto thrown = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(poisoned);
    CrtRandom expected(even);
    static_cast<void>(expected.Next());
    const std::uint32_t side = expected.Next() % 2 == 0 ? 53 : 54;
    const auto dusty = static_cast<float>(static_cast<double>(expected.Next() % 32 + 50) * static_cast<double>(0.025f));
    Expect(expected, 0x5f2, 11, At(side), actor.angles, poison, dusty);
    // 0x51d438: the stone's life, scale and fall; then the three poisons, each with its rand() % 10 and, on a hit, its
    // 0x5f2/0x17 with its rand().
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

    // In action 4 with frame from 3.5 to 4.2: 0x1fc plus rand() % 2 and the 0x5f2/11 of each hand, at the object's z.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(attack);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        CrtRandom hands(dry);
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x1fc + (hands.Next() & 1)});
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
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(rest).HasValue());
    EXPECT_EQ(resting.State(), AfterDraws(calm, 3).State());
    EXPECT_EQ(actor.subtype, 0);

    // 0x4425bb..0x442817: with its rand() % 10 == 0, each poison creates its 0x5f2/23 at its bone's point, with the
    // object's angles, the 0x442107 light and scale 1, and draws the two rand() of its creation before the next one's
    // rand() % 10. Only the right one (0x44274b):
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
    const auto dripped = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(drip);
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

    // The three, in order: top (0x44267f), right (0x44274b) and left (0x442817).
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
    const auto poured = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pour);
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

    // With the table full, no poison creates anything or draws its creation's rand(): the three rand() % 10 remain.
    FillParticles();
    CrtRandom crowded(all);
    MonsterHelperPass full = Pass(Devias, crowded);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(full).HasValue());
    EXPECT_EQ(crowded.State(), AfterDraws(all, 3).State());
}

// 0x442824..0x442a5d: Monster85's eyes at 0.7; in action 2, rand() % 10 == 0 is the 0x5f3/0 particle at the position,
// with the object's angles and the eyes' light, and the sound 0x1f0 plus rand() % 2. With the table full, the sound
// without the particle's rand().
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
        const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 2u);
        EXPECT_EQ(sprites[0].position, At(18));
        EXPECT_FLOAT_EQ(sprites[0].scale, 0.7f);
        CrtRandom expected(seed);
        static_cast<void>(expected.Next());
        Expect(expected, 0x5f3, 0, Origin, actor.angles, RedEyes);
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x1f0 + (expected.Next() & 1)});
        EXPECT_EQ(random.State(), expected.State());
        ExpectTheParticles();
        EXPECT_EQ(LiveParticles().size(), 1u);
    }
    FillParticles();
    Monster(0x1a7, 10);
    CrtRandom full(seed);
    MonsterHelperPass crowded = Pass(Devias, full);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
    CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x1f0 + (expected.Next() & 1)});
    EXPECT_EQ(full.State(), expected.State());
}

// 0x442a62..0x442dbc: two 0x5ac per eye at (0, −2, 0); in action 4, the first time +4 = 1, the 0x13f effect (0x442cb9,
// no rand(): life 20 and the monster as owner) and the sound 0x20d; then, at point (0, 0, −10) of the left hand, the
// 0x6ec/3 fanta of 3 owned by it and the 0x5f2/21 smoke of 2, with the eyes' light. With the table full, neither
// creates anything or draws rand().
TEST_F(MonsterHelpersTest, The1a9AttackMarksItsFirstFrame)
{
    Name("Monster87_LeftEye", 8);
    Name("Monster87_RightEye", 9);
    Name("Monster87_LeftHand", 16);
    ActorState& actor = Monster(0x1a9, 4);
    actor.angles = {0.0f, 0.0f, 45.0f};
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random);
    const auto first = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x20d});
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
    // The second time, with +4 at 1, neither the effect nor the sound; the same two particles.
    m_world->Runtime().lastSoundId.reset();
    random = CrtRandom(1);
    const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(Sound().has_value());
    EXPECT_FALSE(m_world->Map().effects.Effects()[1].alive);
    EXPECT_EQ(random.State(), ExpectTheBoneParticles(*m_particles, made, actor.angles, CrtRandom(1)).State());
    for (auto& particle : m_particles->All())
        particle.alive = false;
    random = CrtRandom(1);
    FillParticles();
    MonsterHelperPass crowded = Pass(Devias, random);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(crowded).HasValue());
    EXPECT_EQ(random.State(), 1u);
}

// Between original frames the monster is a copy: +4 becomes 1 on it, but the 0x13f is not born in the world table and
// the 0x20d does not sound.
TEST_F(MonsterHelpersTest, The1a9CallsItsMeteorOnlyInAnOriginalFrame)
{
    Name("Monster87_LeftEye", 8);
    Name("Monster87_RightEye", 9);
    Name("Monster87_LeftHand", 16);
    ActorState& actor = Monster(0x1a9, 4);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Lorencia, random, false);
    const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_FALSE(Sound().has_value());
    EXPECT_FALSE(m_world->Map().effects.Effects()[0].alive);
}

// 0x518cf0 does not start the effect with the table full: the 0x442cc5 sound and the hand follow.
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
    const auto drawn = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x20d});
    EXPECT_EQ(random.State(), 1u);
}

// 0x4343ba..0x434580 in Crywolf: 0x1ab in action 8 sounds 0x233 and returns 0 (the chain goes on); 0x1b0 in action 2
// draws rand() % 15 and returns 1; 0x1b5 in action 4 sounds and resets +4 to 0, and in action 3 leaves it.
TEST_F(MonsterHelpersTest, TheCrywolfMonstersSoundByAction)
{
    ActorState& actor = Monster(0x1ab, 8);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Crywolf, random);
    const auto first = Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_FALSE(first.Value());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x233});
    EXPECT_EQ(actor.subtype, 1);

    Monster(0x1b0, 2);
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 15 == 0; });
    CrtRandom idle(seed);
    MonsterHelperPass idling = Pass(Crywolf, idle);
    const auto second = Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(idling);
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_TRUE(second.Value());
    CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x224 + (expected.Next() & 1)});
    EXPECT_EQ(idle.State(), expected.State());

    actor.type = 0x1b5;
    actor.animation.action = 4;
    actor.subtype = 0;
    m_world->Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x241});
    EXPECT_EQ(actor.subtype, 0);
    actor.animation.action = 3;
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(actor.subtype, 1);
    EXPECT_EQ(random.State(), 1u);
}

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
    Client::Terrain::TerrainDecal decal{.slot = 0x69b,
                                        .x = 12850.0f,
                                        .y = 12850.0f,
                                        .width = 2.8f,
                                        .height = 2.8f,
                                        .color = marks.light + marks.light,
                                        .angle = marks.spin,
                                        .alpha = 1.0f,
                                        .lift = 5.0f};
    ASSERT_TRUE(Client::Terrain::DrawTerrainDecal(reference, heights, decal).HasValue());
    if (marks.backSpin)
    {
        decal.width = 3.6f;
        decal.height = 3.6f;
        decal.angle = *marks.backSpin;
        ASSERT_TRUE(Client::Terrain::DrawTerrainDecal(reference, heights, decal).HasValue());
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

// b and g from the clock, with the retained sine (ADR0014).
constexpr float AltarClock = 1000.0f;

float AltarBright()
{
    return static_cast<float>(ClockSine(AltarClock, 0.002f, 0.1f) + static_cast<double>(0.28f));
}

float AltarSoft()
{
    return static_cast<float>(ClockSine(AltarClock, 0.002f, 0.04f) + static_cast<double>(0.2f));
}

// 0x43421a..0x43439a, with +0x7c & 0x40000: own light (0.09; 0.09; 0.04), the 0x729/0 and /1 particles at the position
// with that light, and the two marks with spins T · 0.01f and T · −0.01f; their light is (g; g; 0.05f), or the color
// bits' light if those are set too. With the table full, the marks without the particles or their rand(). Then,
// 0x620980.
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

        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);

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

// 0x4342a2..0x4343a2 without 0x40000: with 0x100000 or 0x20000 the first 0x69b mark of 2.8 with (b; 0.1; 0.1) doubled
// and spin g; with 0x10000 or 0x80000, (0.15; 0.15; b) doubled and spin T · 0.01f. The second is not drawn: its spin
// ([ebp−0x1c]) is unwritten stack (Q-C1100). With no bit, not the first either (its light is the stack): only the
// texture is bound. None draws rand(), creates particles or touches the own light.
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

        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);

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
    const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(batch.Vertices().empty());
    ASSERT_TRUE(batch.State().texture.has_value());
    EXPECT_EQ(batch.State().texture->value, 0x69b);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ(random.State(), 1u);
    EXPECT_TRUE(LiveParticles().empty());

    // Without the batch or the heights, it cuts at the call.
    actor.flags7c = 0x100000;
    pass.heights = nullptr;
    EXPECT_TRUE(CutAt(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass), "0x43436c"));
    EXPECT_EQ(random.State(), 1u);
}

// Outside Crywolf (0x4303c0 returns 0), 0x434180 does not look at the type.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_EQ(random.State(), 1u);
        EXPECT_TRUE(LiveParticles().empty());
        EXPECT_EQ(actor.light, glm::vec3(0.1f, 0.2f, 0.3f));
    }
}

// 0x434602..0x434a70: 0x1af sounds, then draws rand() % 14, rand() % 360 and rand() % 2 for its seven sprites.
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

    const auto drawn = Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass);

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

// 0x434bf3..0x434e2e and 0x434f8a..0x4354a1: 0x1b1's head outside action 6 and, in action 2 with rand() % 10 == 0, its
// 0x5f3/0 particle at the position with the eyes' light, before the sound draw; 0x1b2's center and tips, half in
// actions 3 and 4, with the clock's retained sine (ADR0014).
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
    const auto dusty = Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass);
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
    // With the table full, the sound draw goes on without the particle's rand().
    FillParticles();
    m_sprites.Clear();
    CrtRandom crowded(seed);
    MonsterHelperPass full = Pass(Crywolf, crowded);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(full).HasValue());
    CrtRandom sorted(seed);
    static_cast<void>(sorted.Next());
    if (sorted.Next() % 15 == 0)
        static_cast<void>(sorted.Next());
    EXPECT_EQ(crowded.State(), sorted.State());

    actor.animation.action = 6;
    m_sprites.Clear();
    const auto died = Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass);
    ASSERT_TRUE(died.HasValue()) << died.Error();
    EXPECT_TRUE(LiveSprites().empty());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x214});

    Name("Monster96_Center", 28);
    Name("Monster96_Top", 27);
    Name("Monster96_Bottom", 29);
    actor.type = 0x1b2;
    actor.animation.action = 3;
    actor.subtype = 0;
    CrtRandom spinning(3);
    MonsterHelperPass glow = Pass(Crywolf, spinning);
    glow.worldClock = 1000.0f;
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(glow).HasValue());
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
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x221});
    EXPECT_EQ(spinning.State(), AfterDraws(3, 1).State());
}

// The original draws once per frame, after 0 to N steps (ADR 0005): between original frames the character is a
// projected copy and the world tables are not touched. The 0x1a4 strike (0x441b33..0x441c4c: the 0x636/2 crater, the
// 0x61c/6 joint and the 0x5de/3 column, in that order, with no other player near), 0x1b3's 0x115/1 (0x434eb0) and
// 0x1b7's 0x618/0x10 (0x427fd3) are born only in an original frame, and nothing else.
TEST_F(MonsterHelpersTest, BetweenOriginalFramesTheHelpersLeaveTheWorldTablesAlone)
{
    struct Born
    {
        bool joint = false;
        std::int32_t type = 0;
        std::int32_t subtype = 0;
        bool operator==(const Born&) const = default;
    };
    const auto born = [this]
    {
        std::vector<Born> made;
        for (const auto& effect : m_world->Map().effects.Effects())
            if (effect.alive)
                made.push_back(Born{false, effect.type, effect.subtype});
        for (const auto& joint : m_world->DrawTail().joints)
            if (joint.alive)
                made.push_back(Born{true, joint.type, joint.subtype});
        return made;
    };
    const auto clear = [this]
    {
        for (auto& effect : m_world->Map().effects.Effects())
            effect.alive = false;
        for (auto& joint : m_world->DrawTail().joints)
            joint.alive = false;
    };
    const auto expected = [](bool original, std::vector<Born> made) { return original ? made : std::vector<Born>{}; };
    Name("Monster82_LHand", 34);
    Name("Monster82_RHand", 45);
    Name("Monster82_Eye", 48);
    Name("Monster82_Back", 46);
    Name("Monster101_L_Arm", 12);
    Name("Monster101_R_Arm", 13);
    Name("Monster101_Head", 14);
    const std::uint32_t quiet = FirstSeedWhere([](CrtRandom& draw) { return draw.Next() % 20 != 0; });
    for (const bool original : {false, true})
    {
        SCOPED_TRACE(original);
        clear();
        FillParticles();
        ActorState& striker = Monster(0x1a4, 3, 9.0f);
        striker.subtype = 0;
        CrtRandom attack(quiet);
        MonsterHelperPass striking = Pass(Devias, attack, original);
        const auto struck = Client::Monsters::Drawing::DrawTheEffectsOf1a3To1a9(striking);
        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        EXPECT_EQ(born(), expected(original, {Born{false, 0x636, 2}, Born{false, 0x5de, 3}, Born{true, 0x61c, 6}}));

        clear();
        Monster(0x1b3, 4, 6.0f);
        CrtRandom piercing(1);
        MonsterHelperPass crywolf = Pass(Crywolf, piercing, original);
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(crywolf).HasValue());
        EXPECT_EQ(born(), expected(original, {Born{false, 0x115, 1}}));

        clear();
        ActorState& caster = Monster(0x1b7, 3, 6.9f);
        caster.attackTarget = 0x18f;
        m_world->Actors()[0x18f].position = {500.0f, 600.0f, 70.0f};
        CrtRandom beam(21);
        MonsterHelperPass lorencia = Pass(Lorencia, beam, original);
        const auto beamed = Client::Monsters::Drawing::DrawTheMonsterHelpers(lorencia);
        ASSERT_TRUE(beamed.HasValue()) << beamed.Error();
        EXPECT_EQ(born(), expected(original, {Born{true, 0x618, 0x10}}));
    }
}

// 0x434e3f..0x434ecf: 0x1b3 in action 4 with frame in [4, 6] creates 115/1 from the position, then sounds.
TEST_F(MonsterHelpersTest, The1b3CreatesThePiercingInItsAttackWindowBeforeSounding)
{
    ActorState& actor = Monster(0x1b3, 4, 6.0f);
    CrtRandom random(1);
    MonsterHelperPass pass = Pass(Crywolf, random);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    const auto& effect = m_world->Map().effects.Effects()[0];
    EXPECT_EQ(effect.type, 0x115);
    EXPECT_EQ(effect.life, 6);
    EXPECT_EQ(effect.start, actor.position);
    EXPECT_EQ(effect.transform.position.z, actor.position.z + 130);
    EXPECT_EQ(effect.light, (glm::vec3{1, 0, 0.5f}));
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x21d});
    actor.animation.frame = 6.5f;
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheCrywolfEffectsOf1abTo208(pass).HasValue());
    EXPECT_EQ(Sound(), Client::Audio::SoundId{0x21d});
}

// 0x42720d..0x42769f: 0x1b6 draws rand() % 30 for the light of its five 0x5ad and drops at point (0, 0, −65) of the
// foot the 0x5ad/4 of 4 (life 4, texture 0x5fd, two rand()) with light (0.7f; 0.5; 0.7f); idle in action 2, it goes on
// with its rand() % 15.
// 0x427764..0x427a4d: in action 3 from frame 6, at point (70, 0, 0) of the foot, the 0x5f2/0x1b of 2 with light (0.5;
// 0.2f; 0.5), the 0x5ad/2 of 1 and the 0x5ad/3 of 1.3f, and with the target, the 0x5ad/3 100 above it; in action 4
// from frame 4, only the first two. All with angles (0, 0, 0).
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
    const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
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
        for (Client::Kernel::JointState& joint : m_world->DrawTail().joints)
            joint.alive = false;
        ActorState& actor = Monster(0x1b6, action, 6.0f);
        actor.subtype = 1;
        actor.attackTarget = 3;
        CrtRandom attacking(11);
        MonsterHelperPass hit = Pass(Devias, attacking);
        const auto sparked = Client::Monsters::Drawing::DrawTheMonsterHelpers(hit);
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

// 0x4276a4..0x427a92 with the table full: 0x1b6 sounds (in action 4, 0x9c plus rand() % 2 and then 0x263, with +4 = 1)
// and, in action 4 from frame 6 with the target +0x3a4 in the pool, creates the 0x614/0x10 joint with no owner from the
// target to 100 above it (0x427a8d, two rand()). With no target (a negative WORD or from 0x190 up), or in action 3, its
// particles create nothing.
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
        for (Client::Kernel::JointState& joint : m_world->DrawTail().joints)
            joint.alive = false;
        ActorState& victim = m_world->Actors()[3];
        victim.position = {300.0f, 400.0f, 50.0f};
        actor.angles = {1.0f, 2.0f, 3.0f};
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass).HasValue());
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x263});
        EXPECT_EQ(actor.subtype, 1);
        CrtRandom expected = AfterDraws(11, 2);
        const float speed = static_cast<float>(static_cast<double>(expected.Next() % 10) + 20.0);
        const std::int32_t life = expected.Next() % 2 + 2;
        EXPECT_EQ(random.State(), expected.State());
        const Client::Kernel::JointState& joint = m_world->DrawTail().joints[0];
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
            ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(free).HasValue());
            EXPECT_EQ(untargeted.State(), AfterDraws(11, 1).State());
        }
        actor.attackTarget = 3;
        actor.animation.frame = 5.9f;
        CrtRandom early(11);
        MonsterHelperPass before = Pass(map, early);
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(before).HasValue());
        actor.animation.action = 3;
        actor.animation.frame = 8.0f;
        actor.subtype = 0;
        m_world->Runtime().lastSoundId.reset();
        CrtRandom swing(11);
        MonsterHelperPass third = Pass(map, swing);
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(third).HasValue());
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x264});
        EXPECT_EQ(swing.State(), AfterDraws(11, 1).State());
        m_sprites.Clear();
    }
}

// 0x42822a..0x4282e5 and 0x428dbd.
TEST_F(MonsterHelpersTest, The1b9OnlySounds)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        ActorState& actor = Monster(0x1b9, 3);
        actor.subtype = 0;
        CrtRandom random(1);
        MonsterHelperPass pass = Pass(map, random);
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass).HasValue());
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x258});
        EXPECT_EQ(actor.subtype, 1);
        actor.animation.action = 1;
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass).HasValue());
        EXPECT_EQ(actor.subtype, 0);
        EXPECT_EQ(random.State(), 1u);
    }
}

// 0x427ab0..0x427fad: 0x1b7 drops the 0x6f1/1 mist at the arm and head points (angles 0, light 1, scale 1; seven
// rand() each) and, after its sound, in action 3, at point (30, 0, 0) of the left arm with angles (0, 45, 45): from
// frame 4 the 0x5f2/0x19 smoke of 1 with light 1 and, from 5 to 5.5, the 0x6eb/3 wave of 0.5 with light 5. No sprites.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const CrtRandom expected =
            ExpectTheBoneParticles(*m_particles, std::span(all).first(value.made), {}, CrtRandom(21));
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_TRUE(LiveSprites().empty());
        EXPECT_EQ(Sound(), Client::Audio::SoundId{value.sound});
        EXPECT_EQ(actor.subtype, 1);
    }
}

// 0x42803b..0x428222: 0x1b8 creates the green 0x5c6/7 spark (0x428164) at point (60, −30, 0) of the head, with angles
// (0, 0, 0), the light (0; rand() % 10 · 0.01f + 0.2f; 0) of its 0x5ad at 3 and scale 1, before its sounds.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
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
        EXPECT_EQ(Sound(), Client::Audio::SoundId{0x24e});
        EXPECT_EQ(actor.subtype, 1);
    }
}

// 0x4282ea..0x4285a9: 0x1ba, after its sounds, creates at each horn the 0x5c6/6 spark (0x42849c) with angles (0, 0, 0),
// the light (0.4; rand() % 10 · 0.01f + 0.8f; 0.5) of its 0x5ad and scale 1; in action 4, the 0x5f2/26 smoke
// (0x4285a4) at point (0, −100, 20) of Monster104_Footsteps, with the object's angles, (0.5; 0.7f; 0.5) and scale 1. In
// action 3, no smoke.
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
        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
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
        EXPECT_EQ(Sound(), Client::Audio::SoundId{action == 4 ? 0x254 : 0x253});
    }
}

// 0x4285b1..0x428daf: 0x1bb creates a 0x6ec/6 fanta in each hand, the left first, with angles (0, 0, 0) and the light
// (rand() % 8 · 0.1f + 0.6f; 0; 0) of its 0x5ad: in action 4, both at 5 at the hand's point (0x428adc and 0x428d56); in
// the others, the left at 2.5 (0x428c52) and the right at 2.1f at point (0, 0, −10) of the hand (0x428d56). In action 4
// from 9 to 10, after both, the white 0x5e8/1 ray at the right hand's point (0x428da7), with no rand().
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
        const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
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
        EXPECT_EQ(Sound(), Client::Audio::SoundId{value.sound});
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

// With the table full, its particles create nothing and draw no rand(), and it goes on: 0x1b7 sounds and in action 3
// from frame 6.9 with the target in the pool creates the 0x618/0x10 joint (0x428036 jumps to 0x427a8d: its three
// rand()); 0x1b8 sounds; 0x1ba gives its six horns, each with its rand() % 10, and in action 4 still asks for the foot
// point; 0x1bb asks for both hands' points and in action 4 from 9 to 10 creates the white 0x5e8/1 ray at the right
// hand, with no rand().
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
            const auto drawn = Client::Monsters::Drawing::DrawTheMonsterHelpers(pass);
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
            EXPECT_EQ(Sound(), Client::Audio::SoundId{value.sound});
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
    // 0x1b7's 0x618/0x10 (0x427fea..0x428036); the direction (odd rand(): 1) and the x and y angles at rand() % 45
    // (0x53217c..0x5321d8).
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
        ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass).HasValue());
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
    // 0x1ba's horns in order; in action 0, no sound and no draw.
    m_sprites.Clear();
    Monster(0x1ba, 0);
    CrtRandom random(21);
    MonsterHelperPass pass = Pass(Devias, random);
    ASSERT_TRUE(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass).HasValue());
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

// With the table full, 0x1ba in action 4 and 0x1bb still ask for the foot point (0x428540) and the right hand's
// (0x428b32 in action 4, 0x428cf8 in the others): without the bone, they cut there.
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
        EXPECT_TRUE(CutAt(Client::Monsters::Drawing::DrawTheMonsterHelpers(pass), address));
    }
}

// 0x432bf1: the unconditional rand(), with any type; 0x1ac, 0x1ad and 0x1b2 do nothing else.
TEST_F(MonsterHelpersTest, TheSwingStepDrawsItsRandomWithAnyType)
{
    for (const std::uint16_t type :
         {std::uint16_t{0x15b}, std::uint16_t{0x20a}, std::uint16_t{0x1ac}, std::uint16_t{0x1b2}, std::uint16_t{0x1b0}})
    {
        SCOPED_TRACE(type);
        ActorState& actor = Monster(type, 3, 4.0f);
        CrtRandom random(1);
        const auto stepped = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                                random, m_particles.get(), 0.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), AfterDraws(1, 1).State());
    }
    EXPECT_TRUE(m_world->PoseCarry().models.empty());
}

// 0x432c12..0x432db0: 0x1b0 in action 4 outside (5, 7) poses ten times into the global buffer, from the frame minus
// action 4's speed with a tenth of it as step, with the angles' z 18 degrees less per loop; it draws two rand() per
// loop and leaves the object's angles as they were.
TEST_F(MonsterHelpersTest, The1b0SwingPosesTenTimes)
{
    for (const float frame : {3.0f, 7.0f})
    {
        SCOPED_TRACE(frame);
        m_world->PoseCarry() = {};
        ActorState& actor = Monster(0x1b0, 4, frame);
        actor.angles = {0.0f, 0.0f, 90.0f};
        CrtRandom random(1);
        const auto stepped = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                                random, m_particles.get(), 0.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), AfterDraws(1, 21).State());
        EXPECT_EQ(actor.angles, glm::vec3(0.0f, 0.0f, 90.0f));
        const auto& carry = m_world->PoseCarry();
        ASSERT_EQ(carry.models.count(0x1b0), 1u);
        EXPECT_FLOAT_EQ(carry.models.at(0x1b0).angles.z, 90.0f - 18.0f * 9.0f);
        EXPECT_EQ(carry.models.at(0x1b0).position, Origin);
        EXPECT_EQ(carry.bones.size(), 20u);
        float pose = frame - 0.5f;
        for (int turn = 0; turn < 9; ++turn)
            pose = static_cast<float>(static_cast<double>(pose) + static_cast<double>(0.05f));
        EXPECT_EQ(m_faunaModels.PoseKey(0x1b0), static_cast<std::int16_t>(pose));
    }
}

// 0x432c12..0x432db0 with the object's frame in (5, 7): in each of the ten poses, the 0x5d9/0 particle at point
// (0, 100, −150) of bone 16 of the global buffer (not translated: it is already in the world), with that loop's angles,
// white light and scale 1, before the loop's two rand(). With the table full, only the loops' rand().
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
        const auto stepped = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                                random, m_particles.get(), 0.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(actor.angles, glm::vec3(0.0f, 0.0f, 90.0f));
        const auto sparks = LiveParticles();
        ASSERT_EQ(sparks.size(), 10u);
        // The last loop: its point comes from the pose left in the buffer.
        const auto& bones = m_world->PoseCarry().bones;
        ASSERT_GT(bones.size(), 16u);
        const glm::vec3 last =
            Client::Math::OriginalVectorTransform({0.0f, 100.0f, -150.0f}, Client::Math::OriginalMatrixOf(bones[16]));
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
    const auto stepped = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(
        m_world->Actors()[Slot], Slot, *m_world, &m_faunaModels, full, m_particles.get(), 0.0f);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(full.State(), AfterDraws(1, 21).State());
}

// 0x432bd0: the trails publish ten poses and add the samples to the GameDrawTail ribbons.
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
        const auto stepped = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                                random, m_particles.get(), 0.0f);
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
        std::vector<const Client::Kernel::RibbonState*> ribbons;
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
                return Client::Math::OriginalVectorTransform(offset, Client::Math::OriginalMatrixOf(matrix));
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
            EXPECT_EQ(m_faunaModels.PoseKey(value.type), static_cast<std::int16_t>(lastFrame));
        }
    }
}

TEST_F(MonsterHelpersTest, SwingRibbonPoolFullStillPosesAndMatchingOwnersAppend)
{
    m_models.ResizeBones(90);
    ActorState& actor = Monster(0x1b1, 4, 3.0f);
    m_world->DrawTail().ribbons.fill(Client::Kernel::RibbonState{.alive = true, .ownerContext = m_world.get()});
    CrtRandom full(1);
    const auto fullPoolStep = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                                 full, m_particles.get(), 0.0f);
    ASSERT_TRUE(fullPoolStep.HasValue()) << fullPoolStep.Error();
    EXPECT_EQ(full.State(), AfterDraws(1, 1).State());
    EXPECT_EQ(m_world->PoseCarry().models.at(actor.type).position, Origin);
    EXPECT_FLOAT_EQ(m_world->PoseCarry().models.at(actor.type).scale, actor.scale);
    EXPECT_EQ(m_faunaModels.PoseKey(actor.type), 2);
    EXPECT_TRUE(std::ranges::all_of(m_world->DrawTail().ribbons,
                                    [](const Client::Kernel::RibbonState& ribbon) { return ribbon.count == 0; }));

    m_world->DrawTail().ribbons = {};
    CrtRandom append(1);
    ASSERT_TRUE(Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels, append,
                                                                   m_particles.get(), 0.0f)
                    .HasValue());
    ASSERT_TRUE(Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels, append,
                                                                   m_particles.get(), 0.0f)
                    .HasValue());
    EXPECT_EQ(append.State(), AfterDraws(1, 2).State());
    std::vector<const Client::Kernel::RibbonState*> own;
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
    const auto effectAfterRibbon = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(
        type1af, Slot, *m_world, &m_faunaModels, firstRandom, m_particles.get(), 0.0f);
    ASSERT_TRUE(effectAfterRibbon.HasValue()) << effectAfterRibbon.Error();
    EXPECT_EQ(firstRandom.State(), AfterDraws(1, 1).State());
    ASSERT_EQ(m_world->DrawTail().ribbons[0].count, 10);

    ClearParticles();
    m_world->DrawTail().ribbons = {};
    m_world->PoseCarry() = {};
    FillModelEffects();
    CrtRandom fullEffectsRandom(1);
    const auto fullEffects = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(
        type1af, Slot, *m_world, &m_faunaModels, fullEffectsRandom, m_particles.get(), 0.0f);
    ASSERT_TRUE(fullEffects.HasValue()) << fullEffects.Error();
    EXPECT_EQ(fullEffectsRandom.State(), AfterDraws(1, 1).State());
    ASSERT_EQ(m_world->DrawTail().ribbons[0].count, 10);

    ClearModelEffects();
    m_world->DrawTail().ribbons = {};
    m_world->PoseCarry() = {};
    ActorState& type1ae = Monster(0x1ae, 4, 4.7f);
    const auto beforePose = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(
        type1ae, Slot, *m_world, &m_faunaModels, fullEffectsRandom, m_particles.get(), 0.0f);
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
    const auto fullEffectPool = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(
        type1ae, Slot, *m_world, &m_faunaModels, noEffect, m_particles.get(), 0.0f);
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
        const auto result = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                               random, m_particles.get(), 0);
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
        const auto result = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, Slot, *m_world, &m_faunaModels,
                                                                               random, m_particles.get(), 0);
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
            const auto result = Client::Monsters::Drawing::StepTheSwingsOf1abTo1b4(
                actor, Slot, *m_world, &m_faunaModels, random, m_particles.get(), 0);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            const std::size_t expected = action != 2 && frame > 4.5f && frame < 5 ? 4u : 0u;
            EXPECT_EQ(static_cast<std::size_t>(std::ranges::count_if(m_world->Map().effects.Effects(),
                                                                     [](const auto& effect) { return effect.alive; })),
                      expected);
            EXPECT_EQ(random.State(), AfterDraws(7, 1).State());
        }
}
} // namespace
