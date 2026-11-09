#include "Scenes/WorldState.h"

#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Pass3D.h"
#include "Scenes/ActorState.h"
#include "Protocol/Character/CharacterList.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <type_traits>

namespace
{
static_assert(!std::is_copy_constructible_v<Client::Scenes::ActorState>);
static_assert(!std::is_move_constructible_v<Client::Scenes::WorldState>);

void PopulateCloth(Client::Scenes::ActorState& actor)
{
    std::array<Client::Render::Models::BoneTransform, 18> bones;
    bones.fill(Client::Render::Models::BoneTransform(1.0f));
    Client::Render::Models::ClothWorld shared{.random = Client::Render::Models::CrtRandom(12345)};
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::ClothWearer wearer;
    wearer.characterClass = 4;
    actor.cloth.Frame(batch, wearer, Client::Render::Models::ClothBody{.bones = bones}, shared, 55, 0.0f, true);
    ASSERT_FALSE(actor.cloth.Cloths().empty());
}

TEST(WorldStateTest, SelectionAppearancePublishesTheSameRawPetUsedByTheGameplayConstructor)
{
    Client::Scenes::WorldState world;
    Client::Protocol::Character::CharacterEntry entry;
    entry.slot = 2;
    entry.equipment[4] = std::byte{3};
    entry.equipment[9] = std::byte{1};
    auto& actor = world.ReconstructSelectionActor(entry, {});
    EXPECT_EQ(actor.pet.model, 0x1c15);
    EXPECT_TRUE(actor.previousHad1c15);
    entry.withoutEquipment = true;
    world.ReconstructSelectionActor(entry, {});
    EXPECT_EQ(actor.pet.model, 0xffff);
    EXPECT_FALSE(actor.previousHad1c15);
}

TEST(WorldStateTest, ResourceCleanupVisits400AndPreservesHistoryAndTheAdditionalRecord)
{
    Client::Scenes::WorldState world;
    // 5 y 399 distinguen la proyección de 400 registros de los cinco slots de selección.
    constexpr std::array<std::size_t, 4> Indices{3, 5, 399, 400};
    for (const std::size_t index : Indices)
    {
        auto& actor = world.Actors()[index];
        actor.previousHad1c15 = true;
        actor.position = {7.0f, 8.0f, 9.0f};
        actor.active = true;
        actor.revision = 123;
        actor.visualRevision = 122;
        actor.animation.body = {.action = 4, .frame = 5.5f, .previousAction = 2, .previousKey = 3};
        actor.animation.pet.state.frame = 6.5f;
        actor.animation.wings.state.frame = 7.5f;
        ASSERT_NO_FATAL_FAILURE(PopulateCloth(actor));
    }
    world.ReleaseActorResources();
    for (const std::size_t index : Indices)
    {
        SCOPED_TRACE(index);
        const auto& actor = world.Actors()[index];
        EXPECT_EQ(actor.cloth.Cloths().empty(), index != 400);
        EXPECT_TRUE(actor.previousHad1c15);
        EXPECT_EQ(actor.position.x, 7.0f);
        EXPECT_EQ(actor.position.y, 8.0f);
        EXPECT_EQ(actor.position.z, 9.0f);
        EXPECT_TRUE(actor.active);
        EXPECT_EQ(actor.revision, 123u);
        EXPECT_EQ(actor.visualRevision, 122u);
        EXPECT_EQ(actor.animation.body.action, 4);
        EXPECT_EQ(actor.animation.body.frame, 5.5f);
        EXPECT_EQ(actor.animation.body.previousAction, 2);
        EXPECT_EQ(actor.animation.body.previousKey, 3);
        EXPECT_EQ(actor.animation.pet.state.frame, 6.5f);
        EXPECT_EQ(actor.animation.wings.state.frame, 7.5f);
    }
}

TEST(WorldStateTest, InitializesThe401WindowAndKeepsThe400TraversalDomainDistinct)
{
    Client::Scenes::WorldState world;
    EXPECT_EQ(world.Actors().size(), 401u);
    EXPECT_EQ(Client::Scenes::ActorTraversalCount, 400u);
    for (const auto& actor : world.Actors())
    {
        EXPECT_FALSE(actor.previousHad1c15);
        EXPECT_FALSE(actor.active);
    }
    EXPECT_FALSE(world.SelectedActor().has_value());
}

TEST(WorldStateTest, OwnRecordBootstrapHasTwelveEmptyIdsAndZeroFields)
{
    Client::Scenes::WorldState world;
    const auto& own = world.OwnCharacter();
    EXPECT_EQ(own.name, (std::array<std::byte, 11>{}));
    EXPECT_EQ(own.characterClass, 0);
    EXPECT_EQ(own.skin, 0);
    EXPECT_EQ(own.level, 0);
    ASSERT_EQ(own.equipment.size(), 12u);
    for (const Client::World::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0xffff);
        EXPECT_EQ(item.byte0a, 0);
        EXPECT_EQ(item.optionCount, 0);
    }
}

TEST(WorldStateTest, PreparingUsesTheActorAndPreservesOwnSuffixAndEquipment)
{
    Client::Scenes::WorldState world;
    auto& actor = world.Actors()[3];
    actor.active = true;
    actor.name = {std::byte{'A'}, std::byte{'B'}, std::byte{'C'}, std::byte{'D'}, std::byte{'E'}, std::byte{'F'},
                  std::byte{'G'}, std::byte{'H'}, std::byte{'I'}, std::byte{'J'}, std::byte{0}};
    actor.level = 0x3456;
    actor.appearance.characterClass = 0x0c;
    actor.appearance.skin = 2;
    actor.appearance.pet = 0x1c15;
    auto& own = world.OwnCharacter();
    own.equipment[4].id = 0x1234;
    own.equipment[4].byte0a = 0x9a;
    world.SelectActor(3);
    ASSERT_TRUE(world.PrepareSelectedActor());
    EXPECT_EQ(own.name, actor.name);
    EXPECT_EQ(own.level, 0x3456);
    EXPECT_EQ(own.characterClass, 0x0c);
    EXPECT_EQ(own.skin, 2);
    actor.name[0] = std::byte{'X'};
    actor.name[1] = std::byte{0};
    actor.level = 7;
    actor.appearance.characterClass = 1;
    actor.appearance.skin = 0;
    ASSERT_TRUE(world.PrepareSelectedActor());
    const std::array<std::byte, 11> expected{std::byte{'X'}, std::byte{0},   std::byte{'C'}, std::byte{'D'},
                                             std::byte{'E'}, std::byte{'F'}, std::byte{'G'}, std::byte{'H'},
                                             std::byte{'I'}, std::byte{'J'}, std::byte{0}};
    EXPECT_EQ(own.name, expected);
    EXPECT_EQ(own.level, 7);
    EXPECT_EQ(own.characterClass, 1);
    EXPECT_EQ(own.skin, 0);
    EXPECT_EQ(own.equipment[4].id, 0x1234);
    EXPECT_EQ(own.equipment[4].byte0a, 0x9a);
    EXPECT_EQ(own.equipment[0].id, 0xffff);
}
} // namespace
