#include "Models/IModelLibrary.h"
#include "Combat/CombatReplies.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/ItemTable.h"
#include "Tables/MonsterSkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/TextTable.h"
#include "Protocol/Combat/Combat.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Combat/ActorCombat.h"
#include "Actors/ActorDying.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatReceive.h"
#include "Events/EventMapObject.h"
#include "Kernel/DrawTailState.h"
#include "Sound/MonsterSounds.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/MessageArea.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
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

// 04 § Pegar, el daño y la muerte: the 0x16 and the 0x9C (0x4cf200, 0x4cfce0), the 0x17 (0x4d0a30, with 0x4d07c0 in
// Blood Castle and Chaos Castle), 0x4f4a10 and 0x4f5660 on the world. The keys, texts and names are invented.
namespace
{
using Client::Combat::ActorCombatContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 4321;
constexpr std::uint16_t Monster = 0x10;
constexpr std::uint16_t NoItem = Client::Kernel::NoItemModel;
constexpr std::uint32_t OwnRecordDraws = 0xf08;
constexpr float DeathFrame = std::bit_cast<float>(0x3a83126fu);
constexpr float UnorderedFrame = std::bit_cast<float>(0x7fc00000u);

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// The skeletons of 0x4ea030 for the tests' monsters (Player.bmd goes apart, in the context).
class CombatModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == Monster || model.Value() == 0x172 || model.Value() == 0x19e || model.Value() == 0
                   ? &skeleton
                   : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

class CombatRepliesTest : public ::testing::Test
{
protected:
    CombatRepliesTest()
    {
        Runtime().map = 0;
        Runtime().hero = HeroIndex;
        ActorState& hero = Hero();
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        m_world->OwnCharacter().life = 50;
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }
    [[nodiscard]] ActorState& Hero()
    {
        return m_world->Actors()[HeroIndex];
    }
    [[nodiscard]] ActorState& Actor(std::size_t index)
    {
        return m_world->Actors()[index];
    }
    ActorState& MonsterAt(std::size_t index, std::uint16_t key)
    {
        ActorState& actor = Actor(index);
        actor.alive = true;
        actor.type = Monster;
        actor.key = key;
        actor.byte88 = 2;
        return actor;
    }
    ActorState& PlayerAt(std::size_t index, std::uint16_t key, std::uint8_t characterClass = 0)
    {
        ActorState& actor = Actor(index);
        actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.key = key;
        actor.byte88 = 1;
        return actor;
    }
    [[nodiscard]] ActorCombatContext Combat()
    {
        return ActorCombatContext{.world = *m_world,
                                  .random = m_random,
                                  .playerSkeleton = &m_playerSkeleton,
                                  .models = &m_models,
                                  .items = &m_items,
                                  .terrain = &m_terrain};
    }
    // `sounds` as the table of the Monster model (that of 0x612c80) and none for the rest.
    [[nodiscard]] ActorCombatContext CombatWithSounds(const Client::Sound::ModelSounds& sounds)
    {
        ActorCombatContext combat = Combat();
        combat.modelSounds = [sounds](std::uint16_t model) -> std::optional<Client::Sound::ModelSounds>
        {
            if (model != Monster)
                return std::nullopt;
            return sounds;
        };
        return combat;
    }
    [[nodiscard]] Client::Chat::ChatReceiveContext Chat()
    {
        return Client::Chat::ChatReceiveContext{
            .world = *m_world, .random = m_random, .measure = &m_letters, .texts = &m_texts};
    }
    void UseTerrain()
    {
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    }
    std::uint16_t& Wall(std::uint8_t x, std::uint8_t y)
    {
        return m_terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }
    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }
    // A copy of the rand() after `count`, to draw the same values as the code.
    [[nodiscard]] static CrtRandom DrawnAfter(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected;
    }

    static constexpr std::size_t HeroIndex = 5;
    static constexpr std::uint16_t HeroKey = 0x2a;
    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    Client::Terrain::WorldTerrain m_terrain;
    CrtRandom m_random{Seed};
    Client::Models::Skeleton m_playerSkeleton = Client::Tests::PlayerActionsSkeleton();
    CombatModels m_models;
    Client::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{486, "Experiencia %d"}});
    LetterMeasure m_letters;
};

[[nodiscard]] bool HasError(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().find(address) != std::string::npos;
}

// 0x4cf200 without bit 15: the hero records the kill (+0x380 at 2, the damage and the slot), the damage number comes
// out with the light (1; 0.6; 0) and scale 15, the dead one is left dying and the experience is added to the own record
// (3848 rand() when it closes) and goes to the chat with text 486.
TEST_F(CombatRepliesTest, AKillWithoutShockMarksTheVictimOnTheHeroAndAddsTheExperience)
{
    ActorState& dead = MonsterAt(12, 0x123);
    dead.position = {1000.0f, 2000.0f, 30.0f};
    dead.movement = 1;
    m_world->OwnCharacter().experience = 1000;
    const Client::Protocol::Combat::KillExperience reply{.key = 0x123, .shock = false, .amount = 500, .damage = 77};

    const auto applied = Client::Combat::ApplyKillExperience(reply, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    EXPECT_EQ(Hero().byte380, 2);
    EXPECT_EQ(Hero().word382, 77);
    EXPECT_EQ(Hero().attackTarget, 12);
    EXPECT_EQ(dead.dying, 1);
    EXPECT_EQ(dead.movement, 0);
    EXPECT_EQ(dead.animation.action, 0);
    const Client::Kernel::DamageNumberState& number = m_world->DrawTail().damage[0];
    EXPECT_TRUE(number.alive);
    EXPECT_EQ(number.number, 77);
    EXPECT_EQ(number.position, glm::vec3(1000.0f, 2000.0f, 170.0f));
    EXPECT_EQ(number.light, glm::vec3(1.0f, 0.6f, 0.0f));
    EXPECT_EQ(number.scale, 15.0f);
    EXPECT_EQ(number.speed, 10.0f);
    EXPECT_TRUE(number.rise);
    EXPECT_EQ(m_world->OwnCharacter().experience, 1500u);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "Experiencia 500");
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::Ui::SystemMessage);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// 0x4cfca6
TEST_F(CombatRepliesTest, AKillWithoutExperienceStillClosesTheOwnRecordAndAddsNoMessage)
{
    MonsterAt(12, 0x123);
    const auto applied =
        Client::Combat::ApplyKillExperience({.key = 0x123, .shock = false, .amount = 0, .damage = 3}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(m_world->OwnCharacter().experience, 0u);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// 0x4cfce0, the PMSG_KILLPLAYER_EXT of Blood Castle's reward (0x49af90): the 32-bit experience, as the GameServer
// builds it (two WORDs), goes whole into the own record.
TEST_F(CombatRepliesTest, TheExtendedKillAddsTheWholeThirtyTwoBitExperience)
{
    MonsterAt(30, 0x456);
    m_world->OwnCharacter().experience = 7;
    const auto applied = Client::Combat::ApplyKillExperience(
        {.key = 0x456, .shock = false, .amount = 0x10000u + 20000u, .damage = 9}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(m_world->OwnCharacter().experience, 7u + 0x10000u + 20000u);
    EXPECT_EQ(Runtime().chat.log.entries.at(0).text, "Experiencia 85536");
    EXPECT_EQ(Hero().attackTarget, 30);
}

// With bit 15, 0x4f5660 on the dead one: an elf in Devias takes action 0xc3, sound 0x6e and frame 0.001. The hero
// records nothing.
TEST_F(CombatRepliesTest, AShockKillsAnotherPlayerWithTheDeathActionAndSound)
{
    Runtime().map = 2;
    ActorState& dead = PlayerAt(20, 0x55, 2);
    const auto applied =
        Client::Combat::ApplyKillExperience({.key = 0x55, .shock = true, .amount = 10, .damage = 4}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(dead.animation.action, 0xc3);
    EXPECT_EQ(dead.animation.frame, DeathFrame);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x6e});
    EXPECT_EQ(dead.dying, 1);
    EXPECT_EQ(Hero().byte380, 0);
    EXPECT_EQ(Hero().attackTarget, 0);
    EXPECT_EQ(m_world->OwnCharacter().life, 50);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// 0x4f5a09..0x4f5ae8
TEST_F(CombatRepliesTest, ThePlayerDeathSoundFollowsTheClassAndFenrir)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t pet;
        std::int32_t sound;
    };
    for (const Case probe : {Case{0, NoItem, 0x6c}, Case{1, NoItem, 0x6c}, Case{2, NoItem, 0x6e}, Case{3, NoItem, 0x6c},
                             Case{4, NoItem, 0x1c7}, Case{0x12, NoItem, 0x6e}, Case{4, 0x1c37, 0x278}})
    {
        Runtime().lastSoundId.reset();
        ActorState& dead = PlayerAt(20, 0x55, probe.characterClass);
        dead.pet.model = probe.pet;
        ASSERT_TRUE(Client::Combat::SetActorDie(dead, Combat()).HasValue());
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{probe.sound}) << int{probe.characterClass};
    }
}

// The hero's death opens the own record to set the life to 0 (3848 rand()) and then the experience's draws them again.
TEST_F(CombatRepliesTest, AShockOnTheHeroZeroesTheOwnLifeInsideTheOwnRecord)
{
    const auto applied = Client::Combat::ApplyKillExperience({.key = HeroKey, .shock = true, .amount = 1, .damage = 1},
                                                             Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(m_world->OwnCharacter().life, 0);
    EXPECT_EQ(Hero().animation.action, 0xc3);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x6c});
    EXPECT_EQ(Hero().dying, 1);
    EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws));
    EXPECT_EQ(Runtime().ownRecordDepth, 0);
}

// In Chaos Castle (18 to 23) the death sound is rand() & 1 + 0x5e, for everyone.
TEST_F(CombatRepliesTest, InChaosCastleTheDeathSoundIsDrawn)
{
    Runtime().map = 18;
    ActorState& dead = PlayerAt(20, 0x55, 2);
    CrtRandom expected = DrawnAfter(0);
    const std::int32_t sound = (expected.Next() & 1) + 0x5e;
    ASSERT_TRUE(
        Client::Combat::ApplyKillExperience({.key = 0x55, .shock = true, .amount = 0, .damage = 1}, Combat(), Chat())
            .HasValue());
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{sound});
    EXPECT_EQ(dead.animation.frame, DeathFrame);
    EXPECT_EQ(m_random.State(), After(1 + OwnRecordDraws));
}

// The monster dying with the frame at 0 requests the sound of WORD 4 of its table (0x5769ad8 + type · 0xe0 + 0xb2),
// signed, plus 5 with subtype 9 and plus 0xd2, without rand(); with 0xffff, none; without the table, the WORD at 0 of
// 0x60dc80: the 0xd2 (0xd7 with subtype 9). The frame ends at 0.001 anyway. One already in action 6 with another frame
// requests no sound.
TEST_F(CombatRepliesTest, AMonsterDeathSoundComesFromItsTableOnlyWithTheFrameAtZero)
{
    struct Case
    {
        std::int16_t word;
        std::int32_t subtype;
        std::optional<Client::Audio::SoundId> sound;
    };
    for (const Case& tested :
         {Case{0x7d, 0, Client::Audio::SoundId{0x7d + 0xd2}}, Case{0x7d, 9, Client::Audio::SoundId{0x7d + 5 + 0xd2}},
          Case{-1, 0, std::nullopt}, Case{-3, 0, Client::Audio::SoundId{-3 + 0xd2}}})
    {
        SCOPED_TRACE(tested.word);
        Runtime().lastSoundId.reset();
        ActorState& dead = MonsterAt(12, 0x123);
        dead.subtype = tested.subtype;
        dead.animation = {};
        Client::Sound::ModelSounds sounds{};
        sounds.fill(0x11);
        sounds[4] = tested.word;
        const auto applied = Client::Combat::ApplyKillExperience(
            {.key = 0x123, .shock = true, .amount = 5, .damage = 1}, CombatWithSounds(sounds), Chat());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(dead.animation.action, 6);
        EXPECT_EQ(dead.animation.frame, DeathFrame);
        EXPECT_EQ(Runtime().lastSoundId, tested.sound);
    }

    for (const std::int32_t subtype : {0, 9})
    {
        SCOPED_TRACE(subtype);
        Runtime().lastSoundId.reset();
        Actor(14) = ActorState{};
        MonsterAt(14, 0x125).subtype = subtype;
        ASSERT_TRUE(Client::Combat::ApplyKillExperience({.key = 0x125, .shock = true, .amount = 5, .damage = 1},
                                                        Combat(), Chat())
                        .HasValue());
        EXPECT_EQ(Actor(14).animation.action, 6);
        EXPECT_EQ(Actor(14).animation.frame, DeathFrame);
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{subtype == 9 ? 0xd7 : 0xd2});
    }

    Runtime().lastSoundId.reset();

    ActorState& dying = MonsterAt(13, 0x124);
    dying.animation.action = 6;
    dying.animation.frame = 2.0f;
    const auto applied =
        Client::Combat::ApplyKillExperience({.key = 0x124, .shock = true, .amount = 5, .damage = 1}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(dying.animation.frame, 2.0f);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(dying.dying, 1);
    // Seven closes of the own record, one per experience, and no rand() from the sounds.
    EXPECT_EQ(m_random.State(), After(7 * OwnRecordDraws));
}

// 0x4f58a7, 0x4f5942 and 0x4f599e clear the alive byte before trying the effects; with the pool full they are not
// created and the sound 0x7c, the frame gate and 0x4f5a09 follow. The action does not change and no rand() is spent
// except in Chaos Castle.
TEST_F(CombatRepliesTest, SpecialEffectDeathsContinueWhenTheModelEffectPoolIsFull)
{
    struct Case
    {
        std::uint16_t model;
        std::int32_t subtype;
        std::uint8_t characterClass;
        std::optional<std::int16_t> deathSound;
    };
    const std::array cases{
        Case{0x20a, 0xd1, 0, std::int16_t{-1}}, Case{0x20a, 0xd2, 2, std::int16_t{0x7d}},
        Case{0x20a, 0xd3, 4, std::nullopt},     Case{0x16b, 9, 1, std::int16_t{0x7d}},
        Case{0x170, 0, 3, std::nullopt},
    };
    constexpr std::array<float, 3> Frames{1.0f, 0.0f, UnorderedFrame};
    constexpr std::array<std::int32_t, 7> Maps{0, 18, 19, 20, 21, 22, 23};

    for (const std::int32_t map : Maps)
    {
        for (const Case& tested : cases)
        {
            for (std::size_t frameIndex = 0; frameIndex < Frames.size(); ++frameIndex)
            {
                SCOPED_TRACE(map);
                SCOPED_TRACE(tested.model);
                SCOPED_TRACE(tested.subtype);
                SCOPED_TRACE(frameIndex);
                Runtime().map = map;
                Runtime().ownRecordDepth = 0;
                Runtime().lastSoundId.reset();
                Runtime().modelSounds.clear();
                if (tested.deathSound)
                {
                    Client::Sound::ModelSounds sounds{};
                    sounds.fill(0);
                    sounds[4] = *tested.deathSound;
                    Runtime().modelSounds[tested.model] = sounds;
                }

                auto effectPool = m_world->Map().effects.Effects();
                for (Client::Effects::ModelEffect& effect : effectPool)
                    effect.alive = true;

                ActorState& dead = Actor(20);
                dead = ActorState{};
                dead.alive = true;
                dead.type = tested.model;
                dead.subtype = tested.subtype;
                dead.characterClass = tested.characterClass;
                dead.animation.action = 7;
                dead.animation.frame = Frames[frameIndex];
                const std::uint32_t oldFrame = std::bit_cast<std::uint32_t>(dead.animation.frame);
                CrtRandom expected = m_random;
                std::optional<std::int32_t> sound = 0x7c;
                std::uint32_t expectedFrame = oldFrame;
                if (frameIndex != 0)
                {
                    expectedFrame = std::bit_cast<std::uint32_t>(DeathFrame);
                    if (map >= 18 && map <= 23)
                    {
                        sound = static_cast<std::int32_t>((expected.Next() & 1) + 0x5e);
                    }
                    else
                    {
                        const std::int16_t word = tested.deathSound.value_or(0);
                        if (word != -1)
                            sound = word + (tested.subtype == 9 ? 5 : 0) + 0xd2;
                    }
                }

                const auto died = Client::Combat::SetActorDie(dead, Combat());
                ASSERT_TRUE(died.HasValue()) << died.Error();
                EXPECT_FALSE(dead.alive);
                EXPECT_EQ(dead.animation.action, 7);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(dead.animation.frame), expectedFrame);
                EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{*sound});
                EXPECT_EQ(m_random.State(), expected.State());
                EXPECT_TRUE(std::ranges::all_of(effectPool, [](const Client::Effects::ModelEffect& effect)
                                                { return effect.alive; }));
            }
        }
    }
}

// 0x4f589d..0x4f58f6 and 0x4f5938..0x4f5991: with room, the player of subtype 0xd1..0xd3 and the 0x170 drop a bone
// 0xd5/0 and ten ownerless 0xd6/0 (150 and 100 higher, 0x51b398), each with its five rand() (0x51b72b), and go on with
// sound 0x7c. The 0x16b still cuts at its 0xe5 (CL11, switch batch).
TEST_F(CombatRepliesTest, ThePlayerAndThe170ScatterTheirBones)
{
    constexpr std::size_t Bones = 11;
    constexpr std::size_t BoneDraws = 5;
    for (const auto& [model, subtype] : {std::pair{std::uint16_t{0x20a}, 0xd2}, std::pair{std::uint16_t{0x170}, 0}})
    {
        SCOPED_TRACE(model);
        Runtime().map = 0;
        Runtime().lastSoundId.reset();
        auto effectPool = m_world->Map().effects.Effects();
        for (Client::Effects::ModelEffect& effect : effectPool)
            effect.alive = false;
        ActorState& dead = Actor(20);
        dead = ActorState{};
        dead.alive = true;
        dead.type = model;
        dead.subtype = subtype;
        dead.animation.action = 7;
        dead.animation.frame = 1.0f;
        dead.position = {1000.0f, 2000.0f, 30.0f};
        CrtRandom expected = m_random;
        expected.Discard(Bones * BoneDraws);

        const auto died = Client::Combat::SetActorDie(dead, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        EXPECT_FALSE(dead.alive);
        EXPECT_EQ(dead.animation.action, 7);
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x7c});
        EXPECT_EQ(m_random.State(), expected.State());
        for (std::size_t slot = 0; slot < Bones; ++slot)
        {
            SCOPED_TRACE(slot);
            const Client::Effects::ModelEffect& bone = effectPool[slot];
            EXPECT_TRUE(bone.alive);
            EXPECT_EQ(bone.type, slot == 0 ? Client::Effects::FirstBoneEffect : Client::Effects::SecondBoneEffect);
            EXPECT_EQ(bone.subtype, 0);
            EXPECT_EQ(bone.ownerContext, nullptr);
            EXPECT_EQ(bone.transform.position.z, slot == 0 ? 180.0f : 130.0f);
            EXPECT_GE(bone.life, 32);
            EXPECT_LE(bone.life, 47);
        }
        EXPECT_FALSE(effectPool[Bones].alive);
    }
}

// The own record's close comes before the rand() of Chaos Castle's sound, and only the access that leaves it at 0
// closes it. Even if the first effect creation cuts, the hero is already alive=false with its life at 0.
TEST_F(CombatRepliesTest, AHeroDeathClosesItsOwnRecordBeforeTheChaosSound)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0xff}})
    {
        SCOPED_TRACE(depth);
        Runtime().map = 18;
        Runtime().ownRecordDepth = depth;
        Runtime().lastSoundId.reset();
        m_world->OwnCharacter().life = 50;
        for (Client::Effects::ModelEffect& effect : m_world->Map().effects.Effects())
            effect.alive = true;

        ActorState& hero = Hero();
        hero = ActorState{};
        hero.alive = true;
        hero.type = 0x16b;
        hero.animation.action = 7;
        hero.animation.frame = 0.0f;
        CrtRandom expected = m_random;
        if (depth == 0)
            expected.Discard(OwnRecordDraws);
        const std::int32_t sound = static_cast<std::int32_t>((expected.Next() & 1) + 0x5e);

        const auto died = Client::Combat::SetActorDie(hero, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        EXPECT_FALSE(hero.alive);
        EXPECT_EQ(hero.animation.action, 7);
        EXPECT_EQ(hero.animation.frame, DeathFrame);
        EXPECT_EQ(m_world->OwnCharacter().life, 0);
        EXPECT_EQ(Runtime().ownRecordDepth, depth);
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{sound});
        EXPECT_EQ(m_random.State(), expected.State());
    }
}

// 0x4f59ad..0x4f59f4: with room, the death of the 0x16b (the hero) leaves eight pairs 0xe5/0 and 0xe6/0 at its
// position, with its angles and light, after the own record's close; each draws its eight rand() (0x51b659), repeated
// in a twin pool with the same CrtRandom. With two free slots, only the first pair; then the sound and the frame.
TEST_F(CombatRepliesTest, AHeroSpecialEffectDeathScattersItsSplinters)
{
    for (const std::size_t free : {std::size_t{2}, std::size_t{200}})
    {
        SCOPED_TRACE(free);
        Runtime().map = 0;
        Runtime().ownRecordDepth = 0;
        Runtime().lastSoundId.reset();
        m_world->OwnCharacter().life = 50;
        auto effectPool = m_world->Map().effects.Effects();
        for (std::size_t slot = 0; slot < effectPool.size(); ++slot)
            effectPool[slot].alive = slot >= free;

        ActorState& hero = Hero();
        hero = ActorState{};
        hero.alive = true;
        hero.type = 0x16b;
        hero.position = glm::vec3(1000.0f, 2000.0f, 300.0f);
        hero.angles = glm::vec3(0.0f, 0.0f, 45.0f);
        hero.light = glm::vec3(0.5f, 0.25f, 1.0f);
        hero.animation.action = 7;
        hero.animation.frame = 0.0f;
        CrtRandom expected = m_random;
        expected.Discard(OwnRecordDraws);
        CrtRandom counted = expected;

        const auto died = Client::Combat::SetActorDie(hero, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        auto twin = std::make_unique<Client::Effects::ModelEffects>();
        for (std::size_t slot = free; slot < twin->Effects().size(); ++slot)
            twin->Effects()[slot].alive = true;
        for (int pair = 0; pair < 8; ++pair)
        {
            for (const std::int16_t type :
                 {Client::Effects::FirstSplinterEffect, Client::Effects::SecondSplinterEffect})
                static_cast<void>(twin->CreateStoneChip(type, hero.position, hero.angles, hero.light, expected));
        }
        for (std::size_t slot = 0; slot < std::min<std::size_t>(free, 16); ++slot)
        {
            SCOPED_TRACE(slot);
            const Client::Effects::ModelEffect& made = effectPool[slot];
            const Client::Effects::ModelEffect& twinEffect = twin->Effects()[slot];
            ASSERT_TRUE(made.alive);
            EXPECT_EQ(made.type, slot % 2 == 0 ? 0xe5 : 0xe6);
            EXPECT_EQ(made.subtype, 0);
            EXPECT_EQ(made.transform.position, twinEffect.transform.position);
            EXPECT_EQ(made.transform.angles, twinEffect.transform.angles);
            EXPECT_EQ(made.velocity, twinEffect.velocity);
            EXPECT_EQ(made.life, twinEffect.life);
            EXPECT_EQ(made.light, twinEffect.light);
        }
        if (free > 16)
        {
            EXPECT_FALSE(effectPool[16].alive);
        }
        EXPECT_FALSE(hero.alive);
        EXPECT_EQ(hero.animation.action, 7);
        EXPECT_EQ(hero.animation.frame, DeathFrame);
        EXPECT_EQ(m_world->OwnCharacter().life, 0);
        EXPECT_EQ(Runtime().ownRecordDepth, 0);
        EXPECT_EQ(m_random.State(), expected.State());
        int draws = 0;
        while (counted.State() != m_random.State() && draws < 1000)
        {
            static_cast<void>(counted.Next());
            ++draws;
        }
        EXPECT_EQ(draws, free > 16 ? 128 : 16);
    }
}

// The 0x9C that GameServer 0.97 sends (0x49af90) carries the key 0xFFFF: 0x7fff with bit 15. Nobody has 0x7fff and
// 0x5110d0 returns slot 400, of type 0, which dies with sound 0xd2 (nobody writes model 0's table and the REP STOSD of
// 0x60dc80 left it as ten zero WORDs) and adds the experience (Q-C1160, P-C252).
TEST_F(CombatRepliesTest, TheBloodCastleRewardLandsOnSlotFourHundred)
{
    const auto applied = Client::Combat::ApplyKillExperience({.key = 0x7fff, .shock = true, .amount = 100, .damage = 0},
                                                             Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(400).animation.action, 6);
    EXPECT_EQ(Actor(400).dying, 1);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
    EXPECT_EQ(m_world->OwnCharacter().experience, 100u);
}

// 0x4d0a30 outside the castles and without a push skill: dying, still, with the skill recorded and without rand().
TEST_F(CombatRepliesTest, AnOrdinaryDeathOnlyMarksTheActor)
{
    for (const std::int32_t map : {0, 2})
    {
        Runtime().map = map;
        ActorState& dead = MonsterAt(12, 0x123);
        dead.movement = 1;
        dead.byte1a1 = 1;
        ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = 0x10, .killer = 0}, Combat()).HasValue());
        EXPECT_EQ(dead.dying, 1);
        EXPECT_EQ(dead.movement, 0);
        EXPECT_EQ(dead.deathSkill, 0x10);
        EXPECT_EQ(dead.byte1a1, 0);
        EXPECT_EQ(dead.floatD0, 0.0f);
    }
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4d0ac1..0x4d0b98: skill 0x28 (and 0x3b) pushes it with two rand() and turns it toward its killer (0x4e93f0).
TEST_F(CombatRepliesTest, APushingSkillThrowsTheDeadAwayFromTheKiller)
{
    for (const std::uint8_t skill : {std::uint8_t{0x28}, std::uint8_t{0x3b}})
    {
        m_random = CrtRandom(Seed);
        ActorState& dead = MonsterAt(12, 0x123);
        dead.position = {1000.0f, 2000.0f, 0.0f};
        dead.angles = {3.0f, 4.0f, 5.0f};
        dead.floatCC = 9.0f;
        ActorState& killer = PlayerAt(20, 0x55);
        killer.position = {1300.0f, 2400.0f, 0.0f};
        ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = skill, .killer = 0x55}, Combat()).HasValue());

        CrtRandom expected = DrawnAfter(0);
        const std::int32_t first = expected.Next() % 5;
        const std::int32_t second = expected.Next() % 15;
        EXPECT_EQ(dead.byte1a1, 1);
        EXPECT_EQ(dead.byte398, 0);
        EXPECT_EQ(dead.floatCC, 0.0f);
        EXPECT_EQ(dead.floatD8, static_cast<float>((first + 10.0) * static_cast<double>(0.1f)));
        EXPECT_EQ(dead.floatD4, 0.0f);
        EXPECT_EQ(dead.floatD0, static_cast<float>(second + 40.0));
        EXPECT_EQ(dead.pushOrigin, dead.position);
        const auto angle = static_cast<float>(Client::Math::CreateAngle(1000.0f, 2000.0f, 1300.0f, 2400.0f));
        EXPECT_EQ(dead.angles.z, angle);
        EXPECT_EQ(dead.angle30, angle);
        EXPECT_EQ(dead.headTurn.angles.x, 3.0f);
        EXPECT_EQ(dead.headTurn.angles.y, 4.0f);
        EXPECT_EQ(m_random.State(), After(2));
    }
}

// Another player's 17 with skill 0x3b and then its dying step (0x4fb9e0): up to step 14, +0xd0 drops by +0xd8, +0xd8
// rises 1 and 0x51fd20 moves it along the heading toward its killer; no rand() outside map 7. From 15 on it stays
// where it got.
TEST_F(CombatRepliesTest, AnotherPlayerPushedToDeathFliesThroughItsDyingSteps)
{
    ActorState& dead = PlayerAt(20, 0x77);
    dead.position = {1000.0f, 2000.0f, 0.0f};
    ActorState& killer = PlayerAt(21, 0x55);
    killer.position = {1300.0f, 2000.0f, 0.0f};
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x77, .skill = 0x3b, .killer = 0x55}, Combat()).HasValue());
    ASSERT_EQ(dead.dying, 1);
    const std::uint32_t afterTheDeath = m_random.State();
    const Client::Actors::ActorDyingContext dying{.map = 0, .hero = false, .random = m_random};

    const float d0 = dead.floatD0;
    const float d8 = dead.floatD8;
    ASSERT_TRUE(Client::Actors::StepActorDying(dead, 20, dying).HasValue());
    EXPECT_EQ(dead.dyingTime, 0.02f);
    EXPECT_EQ(dead.floatD0, static_cast<float>(static_cast<double>(d0) - static_cast<double>(d8)));
    EXPECT_EQ(dead.floatD8, d8 + 1.0f);
    EXPECT_NE(dead.position, glm::vec3(1000.0f, 2000.0f, 0.0f));
    EXPECT_TRUE(dead.alive);

    for (std::uint8_t step = 2; step <= 14; ++step)
    {
        dead.dying = step;
        ASSERT_TRUE(Client::Actors::StepActorDying(dead, 20, dying).HasValue());
    }
    EXPECT_EQ(dead.floatD0, 0.0f);
    const glm::vec3 landed = dead.position;
    dead.dying = 15;
    ASSERT_TRUE(Client::Actors::StepActorDying(dead, 20, dying).HasValue());
    EXPECT_EQ(dead.position, landed);
    EXPECT_EQ(m_random.State(), afterTheDeath);
}

// 0x4d07c0 in Blood Castle (11): on TerrainWall 0x20 it is pushed off the bridge toward the side of the neighbor with
// bit 8, with five rand().
TEST_F(CombatRepliesTest, OnTheBloodCastleBridgeTheDeadIsPushedTowardsTheFall)
{
    Runtime().map = 11;
    UseTerrain();
    ActorState& dead = MonsterAt(12, 0x123);
    dead.position = {1250.0f, 2350.0f, 0.0f};
    Wall(12, 23) = 0x20;
    Wall(13, 23) = 0x08;
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat()).HasValue());

    CrtRandom expected = DrawnAfter(0);
    const std::int32_t e4 = expected.Next() % 10;
    const std::int32_t d8 = expected.Next() % 20;
    const std::int32_t push = expected.Next() % 10;
    const std::int32_t cc = expected.Next() % 6;
    const std::int32_t side = (-expected.Next()) % 2;
    EXPECT_EQ(dead.byte1a1, 1);
    EXPECT_EQ(dead.floatE4, static_cast<float>(e4 + 10.0));
    EXPECT_EQ(dead.floatD8, static_cast<float>(d8 + 20.0));
    EXPECT_EQ(dead.push, glm::vec3(0.0f, 0.0f, -static_cast<float>(push + 85.0)));
    EXPECT_EQ(dead.angles.z, dead.push.z);
    EXPECT_EQ(dead.floatCC, static_cast<float>(cc + 8.0));
    EXPECT_EQ(dead.floatD4, 5.0f);
    EXPECT_EQ(dead.floatD0, static_cast<float>(side + 13.0));
    EXPECT_EQ(dead.pushOrigin, dead.position);
    EXPECT_EQ(m_random.State(), After(5));

    // With the left neighbor, toward the other side.
    m_random = CrtRandom(Seed);
    Wall(13, 23) = 0;
    Wall(11, 23) = 0x08;
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(dead.push, glm::vec3(0.0f, 0.0f, static_cast<float>(push + 85.0)));
}

// Blood Castle's bridge at the grid's edge: at cell 0xffff the right neighbor (0x4d0949) and at 0 without bit 8 at 1
// the left one (0x4d0979) fall outside TerrainWall. MuRet draws the three rand() before that read and discards the
// push (+0x1a1 at 0) without cutting the handler (Q-C1180); at cell 0 with bit 8 at 1, the push goes out with its five
// rand(). In Lorencia and Devias the same cell does not push.
TEST_F(CombatRepliesTest, OnTheEdgeOfTheGridTheBridgePushIsDiscarded)
{
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        glm::vec3 position;
        std::uint8_t x;
        std::uint8_t y;
        bool rightFalls;
        int draws;
        std::uint8_t pushed;
    };
    const std::array cases{Case{"la celda 0xffff", 11, {25550.0f, 25550.0f, 0.0f}, 255, 255, false, 3, 0},
                           Case{"la celda 0", 11, {50.0f, 50.0f, 0.0f}, 0, 0, false, 3, 0},
                           Case{"la celda 0 con la 1 que cae", 11, {50.0f, 50.0f, 0.0f}, 0, 0, true, 5, 1},
                           Case{"la celda 0 en Lorencia", 0, {50.0f, 50.0f, 0.0f}, 0, 0, false, 0, 0},
                           Case{"la celda 0xffff en Devias", 2, {25550.0f, 25550.0f, 0.0f}, 255, 255, false, 0, 0}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        m_random = CrtRandom(Seed);
        Runtime().map = probe.map;
        UseTerrain();
        Actor(12) = ActorState{};
        ActorState& dead = MonsterAt(12, 0x123);
        dead.position = probe.position;
        Wall(probe.x, probe.y) = 0x20;
        Wall(1, 0) = probe.rightFalls ? std::uint16_t{0x08} : std::uint16_t{0};

        const auto died = Client::Combat::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        EXPECT_EQ(dead.dying, 1);
        EXPECT_EQ(dead.byte1a1, probe.pushed);
        EXPECT_EQ(m_random.State(), After(static_cast<std::uint32_t>(probe.draws)));
        if (probe.pushed != 0)
        {
            CrtRandom expected = DrawnAfter(0);
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            const std::int32_t push = expected.Next() % 10;
            EXPECT_EQ(dead.push, glm::vec3(0.0f, 0.0f, -static_cast<float>(push + 85.0)));
        }
    }
}

// 0x4d0848..0x4d08dc: in Chaos Castle (18) on TerrainWall 8 it falls into the void, with sound 0x60 and two rand().
TEST_F(CombatRepliesTest, InChaosCastleTheDeadFallsIntoTheVoid)
{
    Runtime().map = 18;
    UseTerrain();
    ActorState& dead = MonsterAt(12, 0x123);
    dead.position = {1250.0f, 2350.0f, 0.0f};
    dead.floatD8 = 1.0f;
    Wall(12, 23) = 0x08;
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = 0x28, .killer = 0}, Combat()).HasValue());

    CrtRandom expected = DrawnAfter(0);
    const std::int32_t e4 = expected.Next() % 10;
    const std::int32_t drop = expected.Next() % 3;
    EXPECT_EQ(dead.byte1a1, 1);
    EXPECT_EQ(dead.floatE4, static_cast<float>(e4 + 10.0));
    EXPECT_EQ(dead.floatD4, 1.0f);
    EXPECT_EQ(dead.floatD8, static_cast<float>(1.0 - (drop + 3.0)));
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x60});
    EXPECT_EQ(m_random.State(), After(2));

    // The same TerrainWall in Blood Castle does not make it fall; the skill does not push in the castles either.
    m_random = CrtRandom(Seed);
    Runtime().map = 11;
    Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = 0x28, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(dead.byte1a1, 0);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4d0a0c..0x4d0a25: the event object goes back to 0x40ba20.
TEST_F(CombatRepliesTest, TheHeroDyingInACastleResetsTheEventObject)
{
    Runtime().map = 11;
    UseTerrain();
    Client::Events::EventMapObject object;
    object.field10 = 5;
    object.field14 = {1, 2, 3, 4};
    Runtime().eventObject = object;
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = HeroKey, .skill = 0, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(Runtime().eventObject->field10, 0);
    EXPECT_EQ(Runtime().eventObject->field14, (std::array<std::int32_t, 4>{-1, -1, -1, -1}));
    EXPECT_EQ(Hero().dying, 1);

    Runtime().eventObject = object;
    MonsterAt(12, 0x123);
    ASSERT_TRUE(Client::Combat::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(Runtime().eventObject->field10, 5);
}

// Without the loaded world's attributes, 0x4d07c0 has no TerrainWall to read.
TEST_F(CombatRepliesTest, ACastleDeathWithoutAttributesCuts)
{
    Runtime().map = 23;
    MonsterAt(12, 0x123);
    const auto cut = Client::Combat::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat());
    EXPECT_TRUE(HasError(cut, "0x4d07ff"));
}

// 0x4f4a10 on a player: 0x4f3f80 first (3848 rand()), the action by the weapons and the sound with the frame at 0.
TEST_F(CombatRepliesTest, AnUnarmedPlayerPunchesWithoutSound)
{
    ActorState& player = PlayerAt(20, 0x55);
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x22);
    EXPECT_EQ(player.attackCount, 1);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// A sword without the +0x1e mark alternates 0x23 and 0x24 by +0x38f & 1 and the sound comes from rand() & 1 + 0x3c;
// with the mark, 0x27 + (+0x38f % 3).
TEST_F(CombatRepliesTest, ASwordAlternatesByTheAttackCountAndDrawsItsSound)
{
    ActorState& player = PlayerAt(20, 0x55);
    player.hands[0].model = 0x213;
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x23);
    CrtRandom expected = DrawnAfter(OwnRecordDraws);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{(expected.Next() & 1) + 0x3c});
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws + 1));

    // 0x4ea030 resets the frame to 0 when the action changes: the second one requests a sound too.
    player.animation.frame = 1.0f;
    Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x24);
    EXPECT_EQ(player.attackCount, 2);
    EXPECT_TRUE(Runtime().lastSoundId.has_value());

    m_items = Client::Tests::ItemsMarking({0x213});
    player.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x27 + 2);
}

// A bow in the left hand shoots with 0x2e (0x30 with wings) and sounds 0x55, without a draw.
TEST_F(CombatRepliesTest, ABowShootsByTheWingsAndSoundsWithoutDrawing)
{
    ActorState& player = PlayerAt(20, 0x55, 2);
    player.hands[1].model = 0xa12;
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x2e);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x55});
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    player.wings.model = 0x1a00;
    player.animation.frame = 0.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x30);
}

// 0x4f4a4e..0x4f4af0: with Fenrir outside the safe zone, the Dark Lord overwrites Fenrir's action with 0x59; in the
// safe zone it strikes on foot.
TEST_F(CombatRepliesTest, OnFenrirTheDarkLordOverridesTheAction)
{
    ActorState& player = PlayerAt(20, 0x55, 4);
    player.pet.model = 0x1c37;
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x59);
    EXPECT_EQ(player.animation.previousAction, 0x56);

    ActorState& knight = PlayerAt(21, 0x56, 1);
    knight.pet.model = 0x1c37;
    ASSERT_TRUE(Client::Combat::SetActorAttack(knight, Combat()).HasValue());
    EXPECT_EQ(knight.animation.action, 0x56);

    ActorState& safe = PlayerAt(22, 0x57, 4);
    safe.pet.model = 0x1c37;
    safe.safeZone = 1;
    ASSERT_TRUE(Client::Combat::SetActorAttack(safe, Combat()).HasValue());
    EXPECT_EQ(safe.animation.action, 0x22);
}

// 0x4f4fd7..0x4f502f: the 0x172 draws rand() & 7 and, from 3 on, rand() & 1 + 3.
TEST_F(CombatRepliesTest, TheModel172DrawsItsAttack)
{
    CrtRandom expected = DrawnAfter(0);
    const std::int32_t first = expected.Next() & 7;
    std::uint32_t draws = 1;
    std::int32_t action = first == 0 ? 9 : 8;
    if (first >= 3)
    {
        action = (expected.Next() & 1) + 3;
        ++draws;
    }
    // Already in that action and with the frame advanced, so it does not request its table's sound.
    ActorState& monster = MonsterAt(12, 0x123);
    monster.type = 0x172;
    monster.animation.action = static_cast<std::uint8_t>(action);
    monster.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(monster.animation.action, action);
    EXPECT_EQ(monster.animation.frame, 1.0f);
    EXPECT_EQ(monster.attackCount, 1);
    EXPECT_EQ(m_random.State(), After(draws));
}

// 0x4f5034..0x4f50b2: the common monster strikes with 3 if +0x38f % 3 is 0 and with 4 otherwise, and adds one more at
// 0x4f50b2; with the frame at 0 (on an action change, 0x4ea030 resets it to 0) the sound is WORD 2 or 3 of its table
// by rand() % 2, plus 0xd2 (and 5 with subtype 9); with WORD 2 at −1, neither sound nor rand(); without the table, the
// zero WORDs of 0x60dc80: the rand() and the 0xd2.
TEST_F(CombatRepliesTest, AnOrdinaryMonsterCountsTwiceAndSoundsFromItsTable)
{
    ActorState& monster = MonsterAt(12, 0x123);
    monster.animation.action = 3;
    monster.animation.frame = 1.5f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(monster.animation.action, 3);
    EXPECT_EQ(monster.attackCount, 2);
    monster.animation.action = 4;
    ASSERT_TRUE(Client::Combat::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(monster.animation.action, 4);
    EXPECT_EQ(monster.animation.frame, 1.5f);
    EXPECT_EQ(monster.attackCount, 4);
    EXPECT_EQ(m_random.State(), After(0));

    ActorState& still = MonsterAt(13, 0x124);
    ASSERT_TRUE(Client::Combat::SetActorAttack(still, Combat()).HasValue());
    EXPECT_EQ(still.animation.action, 3);
    EXPECT_EQ(still.attackCount, 2);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
    EXPECT_EQ(m_random.State(), After(1));

    Client::Sound::ModelSounds sounds{};
    sounds.fill(-1);
    sounds[2] = 0x7c;
    sounds[3] = -0x10;
    CrtRandom expected = DrawnAfter(1);
    for (const std::int32_t subtype : {0, 9, 0})
    {
        SCOPED_TRACE(subtype);
        ActorState& next = MonsterAt(14, 0x125);
        next.subtype = subtype;
        next.animation = {};
        const std::int32_t drawn = expected.Next() % 2;
        ASSERT_TRUE(Client::Combat::SetActorAttack(next, CombatWithSounds(sounds)).HasValue());
        EXPECT_EQ(Runtime().lastSoundId,
                  Client::Audio::SoundId{sounds[static_cast<std::size_t>(2 + drawn)] + (subtype == 9 ? 5 : 0) + 0xd2});
    }
    EXPECT_EQ(m_random.State(), expected.State());

    Runtime().lastSoundId.reset();
    sounds[2] = -1;
    ActorState& silent = MonsterAt(15, 0x126);
    ASSERT_TRUE(Client::Combat::SetActorAttack(silent, CombatWithSounds(sounds)).HasValue());
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x4ea030 is a no-op if it does not find the model; 0x4f50b2 and 0x4f5265 keep counting, and the sound only draws
// with the frame at 0 or unordered (0x4f50b8..0x4f51ee).
TEST_F(CombatRepliesTest, AnAttackWithoutAModelKeepsItsAnimationButStillCountsAndSounds)
{
    const std::array<float, 3> frames{0.0f, 1.5f, UnorderedFrame};
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        SCOPED_TRACE(index);
        ActorState& monster = MonsterAt(12 + index, static_cast<std::uint16_t>(0x130 + index));
        monster.type = 0x15b;
        monster.animation.action = 7;
        monster.animation.frame = frames[index];
        const std::uint32_t frameBits = std::bit_cast<std::uint32_t>(frames[index]);
        Runtime().lastSoundId.reset();
        CrtRandom expected = m_random;
        const bool frameIsZeroOrUnordered = index != 1;
        if (frameIsZeroOrUnordered)
            static_cast<void>(expected.Next());

        const auto attacked = Client::Combat::SetActorAttack(monster, Combat());
        ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
        EXPECT_EQ(monster.animation.action, 7);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(monster.animation.frame), frameBits);
        EXPECT_EQ(monster.attackCount, 2);
        EXPECT_EQ(Runtime().lastSoundId.has_value(), frameIsZeroOrUnordered);
        if (frameIsZeroOrUnordered)
        {
            EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
        }
        EXPECT_EQ(m_random.State(), expected.State());
    }
}

// A player with subtype 0xd1 to 0xd3 requests rand() & 1 + 0x3c and then the one from model 0x20a's table, which
// nobody writes: its WORDs stay at 0 (0x60dc80), so the second rand() picks between two 0s and the last request is the
// 0xd2.
TEST_F(CombatRepliesTest, APlayerWithAModelSoundSubtypeDrawsTwice)
{
    ActorState& player = PlayerAt(20, 0x55);
    player.subtype = 0xd2;
    CrtRandom expected = DrawnAfter(OwnRecordDraws);
    const std::int32_t sound = (expected.Next() & 1) + 0x3c;
    static_cast<void>(expected.Next());
    ASSERT_TRUE(Client::Combat::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x22);
    EXPECT_NE(sound, 0xd2);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x43a2f0, on any map: the Kalima ones that read +0x38e close its entry (one rand()) before choosing; the last
// group's draw rand() & 1. None adds to +0x38f. With the new action the frame stays at 0 and 0x4f4a10 draws the rand()
// of the table's sound, which nobody wrote for this model (the zero WORDs of 0x60dc80).
TEST_F(CombatRepliesTest, TheKalimaMonstersChooseByTheirSkillByte)
{
    struct Case
    {
        std::uint16_t kind;
        std::uint8_t skill;
        // 0: none; the monster keeps the action it had, 0 (Q-C1172).
        std::uint8_t action;
    };
    const std::array cases{Case{0x91, 0x37, 4}, Case{0x10d, 0, 3},    Case{0xc1, 0x11, 4}, Case{0x110, 2, 3},
                           Case{0x95, 1, 4},    Case{0x111, 0x11, 3}, Case{0xbb, 2, 0}};
    std::uint32_t draws = 0;
    std::size_t slot = 12;
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.kind);
        ActorState& kalima = MonsterAt(slot++, 0x100);
        kalima.word376 = tested.kind;
        kalima.byte38e = tested.skill;
        kalima.animation.frame = 1.0f;
        ASSERT_TRUE(Client::Combat::SetActorAttack(kalima, Combat()).HasValue());
        EXPECT_EQ(kalima.animation.action, tested.action);
        EXPECT_EQ(kalima.attackCount, 1);
        draws += tested.action != 0 ? 2 : 1;
        EXPECT_EQ(m_random.State(), After(draws));
    }

    CrtRandom expected = DrawnAfter(draws);
    ActorState& drawing = MonsterAt(slot, 0x100);
    drawing.word376 = 0x113;
    drawing.animation.frame = 1.0f;
    const std::uint8_t action = static_cast<std::uint8_t>((expected.Next() & 1) + 3);
    static_cast<void>(expected.Next());
    ASSERT_TRUE(Client::Combat::SetActorAttack(drawing, Combat()).HasValue());
    EXPECT_EQ(drawing.animation.action, action);
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x443750: the 0x123 on map 31 chooses with +0x38e open ('2' gives 3, anything else 4) and closes afterwards. Outside
// 31, the usual 3 or 4, by +0x38f.
TEST_F(CombatRepliesTest, TheTrialsMonsterChoosesWithItsSkillByteOpen)
{
    Runtime().map = 0x1f;
    for (const std::uint8_t skill : {std::uint8_t{0x32}, std::uint8_t{0x11}})
    {
        SCOPED_TRACE(skill);
        ActorState& trials = MonsterAt(12, 0x100);
        trials.word376 = 0x123;
        trials.byte38e = skill;
        trials.attackCount = 0;
        trials.animation.action = skill == 0x32 ? 3 : 4;
        trials.animation.frame = 1.0f;
        ASSERT_TRUE(Client::Combat::SetActorAttack(trials, Combat()).HasValue());
        EXPECT_EQ(trials.animation.action, skill == 0x32 ? 3 : 4);
        EXPECT_EQ(trials.animation.frame, 1.0f);
        EXPECT_EQ(trials.attackCount, 1);
    }
    EXPECT_EQ(m_random.State(), After(2));

    Runtime().map = 0;
    ActorState& elsewhere = MonsterAt(13, 0x101);
    elsewhere.word376 = 0x123;
    elsewhere.animation.action = 3;
    elsewhere.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(elsewhere, Combat()).HasValue());
    EXPECT_EQ(elsewhere.attackCount, 2);
    EXPECT_EQ(m_random.State(), After(2));
}

// 0x429270 and 0x437580 with 0x4f92f0: +0x42e (unsigned) looked up among the five MonsterSkill.bmd skills of the
// +0x376; in slots 0 to 4, actions 3, 4, 8, 9 and 0xb and the helper handles it. Found or not, +0x42e ends at 0xff and
// its entry closes (one rand()). Not found, 3 or 4 by +0x38f.
TEST_F(CombatRepliesTest, TheSkillMonstersLookTheirSkillUpInTheTable)
{
    Client::Tables::MonsterSkills crywolf;
    crywolf.skills = {6, 7, 8, 0xc8, -1};
    Runtime().monsterSkills.records[0x154] = crywolf;
    Runtime().map = 0x22;
    struct Case
    {
        std::uint8_t skill;
        std::uint8_t action;
    };
    std::size_t slot = 12;
    for (const Case tested : {Case{6, 3}, Case{7, 4}, Case{8, 8}, Case{0xc8, 9}})
    {
        SCOPED_TRACE(tested.skill);
        ActorState& monster = MonsterAt(slot++, 0x100);
        monster.word376 = 0x154;
        monster.byte42e = tested.skill;
        monster.animation.action = tested.action;
        monster.animation.frame = 1.0f;
        ASSERT_TRUE(Client::Combat::SetActorAttack(monster, Combat()).HasValue());
        EXPECT_EQ(monster.animation.action, tested.action);
        EXPECT_EQ(monster.byte42e, 0xff);
        EXPECT_EQ(monster.attackCount, 1);
    }
    EXPECT_EQ(m_random.State(), After(4));

    // 0xff is not −1: it does not find it, and the one striking again already has it at 0xff.
    ActorState& again = MonsterAt(slot++, 0x100);
    again.word376 = 0x154;
    again.byte42e = 0xff;
    again.animation.action = 3;
    again.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(again, Combat()).HasValue());
    EXPECT_EQ(again.attackCount, 2);
    EXPECT_EQ(m_random.State(), After(5));

    // Maps 0x21 and 0x36 with 0x130, 0x131 and 0x135 (0x429270); without a record, the zeroed .bss finds the 0.
    Runtime().map = 0x36;
    ActorState& doppelganger = MonsterAt(slot++, 0x100);
    doppelganger.word376 = 0x131;
    doppelganger.animation.action = 3;
    doppelganger.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(doppelganger, Combat()).HasValue());
    EXPECT_EQ(doppelganger.attackCount, 1);
    EXPECT_EQ(doppelganger.byte42e, 0xff);
    EXPECT_EQ(m_random.State(), After(6));

    // On another map, it does not even check it.
    Runtime().map = 0;
    ActorState& elsewhere = MonsterAt(slot, 0x100);
    elsewhere.word376 = 0x154;
    elsewhere.byte42e = 6;
    elsewhere.animation.action = 3;
    elsewhere.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(elsewhere, Combat()).HasValue());
    EXPECT_EQ(elsewhere.byte42e, 6);
    EXPECT_EQ(elsewhere.attackCount, 2);
    EXPECT_EQ(m_random.State(), After(6));
}

// The 0x19e sets +0x110 to 3 and action 5 (0x42dcb0) without counting.
TEST_F(CombatRepliesTest, TheSpecialMonsterSetsItsActionWithoutCountingTwice)
{
    ActorState& special = MonsterAt(15, 0x126);
    special.type = 0x19e;
    special.animation.action = 5;
    special.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Combat::SetActorAttack(special, Combat()).HasValue());
    EXPECT_EQ(special.byte110, 3);
    EXPECT_EQ(special.animation.action, 5);
    EXPECT_EQ(special.attackCount, 1);
}

// 0x4f4f5b and 0x4f4fb9: if the effect pool has no room, the action does not change; the native sound is still
// requested, 0x4f50b8..0x4f5262 runs with its frame/table rule and +0x38f is added only once at the end.
TEST_F(CombatRepliesTest, EffectsWithoutPoolSpaceKeepTheAnimationAndFinishAttackBookkeeping)
{
    struct Case
    {
        float frame;
        bool frameIsZeroOrUnordered;
        std::int32_t subtype;
        std::optional<Client::Sound::ModelSounds> sounds;
    };
    Client::Sound::ModelSounds silent{};
    silent.fill(-1);
    Client::Sound::ModelSounds zeroed{};
    zeroed.fill(0);
    const std::array<Case, 6> cases{{
        {1.0f, false, 0xd2, zeroed},
        {0.0f, true, 0, silent},
        {0.0f, true, 0xd1, zeroed},
        {0.0f, true, 0xd3, silent},
        {0.0f, true, 9, zeroed},
        {UnorderedFrame, true, 0, std::nullopt},
    }};

    constexpr std::array<std::uint16_t, 2> EffectModels{0x27, 0x33};
    std::size_t slot = 12;
    for (const std::uint16_t model : EffectModels)
    {
        for (const Case& test : cases)
        {
            SCOPED_TRACE(model);
            SCOPED_TRACE(test.subtype);
            SCOPED_TRACE(std::bit_cast<std::uint32_t>(test.frame));
            auto effectPool = m_world->Map().effects.Effects();
            for (Client::Effects::ModelEffect& poolEffect : effectPool)
                poolEffect.alive = true;
            const std::uint16_t key = static_cast<std::uint16_t>(0x200 + slot);
            ActorState& effect = MonsterAt(slot, key);
            ++slot;
            effect.type = model;
            effect.subtype = test.subtype;
            effect.attackCount = 0xff;
            effect.animation.action = 7;
            effect.animation.frame = test.frame;
            const std::uint32_t frameBits = std::bit_cast<std::uint32_t>(test.frame);
            Runtime().lastSoundId.reset();

            ActorCombatContext combat = Combat();
            if (test.sounds)
            {
                const Client::Sound::ModelSounds sounds = *test.sounds;
                combat.modelSounds = [model,
                                      sounds](std::uint16_t requested) -> std::optional<Client::Sound::ModelSounds>
                {
                    if (requested != model)
                        return std::nullopt;
                    return sounds;
                };
            }

            CrtRandom expected = m_random;
            std::optional<std::int32_t> expectedSound = model == 0x27 ? 0x10 : 0x79;
            if (test.frameIsZeroOrUnordered)
            {
                if (test.subtype >= 0xd1 && test.subtype <= 0xd3)
                    expectedSound = static_cast<std::int32_t>((expected.Next() & 1) + 0x3c);
                const Client::Sound::ModelSounds sounds = test.sounds.value_or(Client::Sound::ModelSounds{});
                if (sounds[2] != -1)
                {
                    const auto drawn = static_cast<std::size_t>(expected.Next() % 2);
                    expectedSound = sounds[2 + drawn] + (test.subtype == 9 ? 5 : 0) + 0xd2;
                }
            }

            const auto attacked = Client::Combat::SetActorAttack(effect, combat);
            ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
            EXPECT_EQ(effect.animation.action, 7);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.animation.frame), frameBits);
            EXPECT_EQ(effect.attackCount, 0);
            EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{*expectedSound});
            EXPECT_EQ(m_random.State(), expected.State());
            EXPECT_TRUE(std::ranges::all_of(effectPool, [](const Client::Effects::ModelEffect& poolEffect)
                                            { return poolEffect.alive; }));
        }
    }
}

// 0x4f4f3a..0x4f4f6a: with a free slot, the 0x27 creates the ownerless saw 0xd4/0 at its position (130 higher,
// 0x51a4ee), with its angles and life 10, without rand(), and goes on with sound 0x10 and +0x38f.
TEST_F(CombatRepliesTest, TheSawOf27GoesOutWithTheStrike)
{
    ActorState& saw = MonsterAt(12, 0x20c);
    saw.type = 0x27;
    saw.attackCount = 0xff;
    saw.animation.action = 7;
    saw.animation.frame = 1.0f;
    saw.position = {300.0f, 400.0f, 50.0f};
    saw.angles = {0.0f, 0.0f, 45.0f};
    Runtime().lastSoundId.reset();
    auto effectPool = m_world->Map().effects.Effects();
    for (Client::Effects::ModelEffect& poolEffect : effectPool)
        poolEffect.alive = true;
    effectPool[3].alive = false;
    const CrtRandom expected = m_random;

    ASSERT_TRUE(Client::Combat::SetActorAttack(saw, Combat()).HasValue());

    const Client::Effects::ModelEffect& created = effectPool[3];
    EXPECT_TRUE(created.alive);
    EXPECT_EQ(created.type, Client::Effects::SawEffect);
    EXPECT_EQ(created.subtype, 0);
    EXPECT_EQ(created.ownerContext, nullptr);
    EXPECT_EQ(created.life, 10);
    EXPECT_EQ(created.transform.position, glm::vec3(300.0f, 400.0f, 180.0f));
    EXPECT_EQ(created.transform.angles, saw.angles);
    EXPECT_EQ(saw.animation.action, 7);
    EXPECT_EQ(saw.attackCount, 0);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x10});
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x4f4f98..0x4f4fc4: with a free slot, the 0x33 creates the ownerless strike 0x5da/0 (Effects/SkillBurstEffects):
// life 10 and (0, −60, 0) rotated by its angles (at 0) plus its position, with z 130 higher, without rand(); it goes on
// with sound 0x79 and +0x38f.
TEST_F(CombatRepliesTest, TheFlameOf33GoesOutWithTheStrike)
{
    ActorState& burner = MonsterAt(12, 0x20c);
    burner.type = 0x33;
    burner.attackCount = 0xff;
    burner.animation.action = 7;
    burner.animation.frame = 1.0f;
    burner.position = {300.0f, 400.0f, 50.0f};
    burner.angles = glm::vec3(0.0f);
    burner.light = {0.5f, 0.25f, 1.0f};
    Runtime().lastSoundId.reset();
    auto effectPool = m_world->Map().effects.Effects();
    for (Client::Effects::ModelEffect& poolEffect : effectPool)
        poolEffect.alive = true;
    effectPool[3].alive = false;
    const CrtRandom expected = m_random;

    const auto attacked = Client::Combat::SetActorAttack(burner, Combat());

    ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
    const Client::Effects::ModelEffect& created = effectPool[3];
    EXPECT_TRUE(created.alive);
    EXPECT_EQ(created.type, 0x5da);
    EXPECT_EQ(created.subtype, 0);
    EXPECT_EQ(created.ownerContext, nullptr);
    EXPECT_EQ(created.life, 10);
    EXPECT_EQ(created.transform.position, glm::vec3(300.0f, 340.0f, 180.0f));
    EXPECT_EQ(created.light, burner.light);
    EXPECT_EQ(burner.animation.action, 7);
    EXPECT_EQ(burner.attackCount, 0);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x79});
    EXPECT_EQ(m_random.State(), expected.State());
}

// The sound table the creation wrote (WorldRuntime::modelSounds) is 0x4f4a10's when the context carries none.
TEST_F(CombatRepliesTest, TheModelSoundsComeFromTheRuntimeTable)
{
    Client::Sound::ModelSounds sounds{};
    sounds.fill(-1);
    sounds[2] = 0x14;
    sounds[3] = 0x15;
    Runtime().modelSounds[Monster] = sounds;
    CrtRandom expected = DrawnAfter(0);
    const std::int32_t drawn = expected.Next() % 2;
    ActorState& monster = MonsterAt(12, 0x100);
    ASSERT_TRUE(Client::Combat::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x14 + drawn + 0xd2});
    EXPECT_EQ(m_random.State(), expected.State());
}
} // namespace
