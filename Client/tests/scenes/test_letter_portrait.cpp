#include "Models/IModelLibrary.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Messenger/LetterPortrait.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Kernel/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Tables/TextTable.h"
#include "Protocol/Messenger/Letters.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Pass3D.h"
#include "Kernel/ActorState.h"
#include "Effects/ActorEffectHooks.h"
#include "Chat/ChatReceive.h"
#include "Pets/DarkHorse.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Messenger/GameFriends.h"
#include "Hud/GameHud.h"
#include "Messenger/GameLetters.h"
#include "Hud/GameUi.h"
#include "Kernel/SceneId.h"
#include "Pets/HelperTable.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "FrontEnd/SelectionState.h"
#include "Tooltips/TooltipLines.h"
#include "Session/CharacterStepHooks.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The letters' portrait character (CL9 milestone 10, P-C212): 0x4961d0, 0x4977d0 with 0x513100, 0x497810 with
// 0x5139f0, 0x496530 with its two copies and 0x495a20, with rand() in the ASM's order, in Lorencia (0) and Devias (2).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::BodyPart;
using Client::Kernel::ClassPartModel;
using Client::Kernel::NoItemModel;
using Client::Messenger::LetterPortrait;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x1e77e5;
// 0x4961d0 from scratch: the character's two entries (4 + 4), the hero's cell (4 + 4) and 0x511290 (28).
constexpr int BuildDraws = 44;
// The own record's close (0x40b4d0 and its inline copies): one rand() per byte.
constexpr int OwnRecordDraws = 0xf08;
constexpr std::uint8_t HeroX = 0x88;
constexpr std::uint8_t HeroY = 0x82;
constexpr std::uint8_t DarkKnight = 1 << 5;

using Equipment = std::array<std::byte, Client::Protocol::FrontEnd::EquipmentSize>;

// Nothing worn: hands 0xfff, the wings of b[4] at 3 with b[8] & 7 at 0 and the pet of b[4] at 3 with b[9] & 1 at 0.
Equipment NothingWorn()
{
    Equipment equipment{};
    equipment.fill(std::byte{0xff});
    equipment[8] = std::byte{0xf8};
    equipment[9] = std::byte{0};
    equipment[10] = std::byte{0};
    equipment[11] = std::byte{0xf0};
    equipment[15] = std::byte{0};
    return equipment;
}

// How many rand() there are from `from` to `to` (up to 20000).
int DrawsBetween(CrtRandom from, const CrtRandom& to)
{
    for (int draws = 0; draws < 20000; ++draws)
    {
        if (from.State() == to.State())
            return draws;
        static_cast<void>(from.Next());
    }
    return -1;
}

// What the portrait draws: Player.bmd with one bone and one key per action, the dark horse (0xbf) with its seven
// actions of 10 keys on one bone, and a triangle for the horse, the shadow (0x20b) and the Dark Knight's armor.
class PortraitModels final : public Client::Models::IModelLibrary
{
public:
    PortraitModels()
    {
        Client::Models::SkeletonBone body;
        body.actions.assign(Client::Tests::PlayerActionCount, {Client::Models::BoneKey{}});
        m_player.bones.push_back(body);
        m_player.actions.assign(Client::Tests::PlayerActionCount, {.keyCount = 1});
        constexpr std::size_t HorseActions = 7;
        constexpr std::int16_t HorseKeys = 10;
        Client::Models::SkeletonBone horse;
        Client::Models::BoneKey still;
        still.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        horse.actions.assign(HorseActions,
                             std::vector<Client::Models::BoneKey>(static_cast<std::size_t>(HorseKeys), still));
        m_horse.bones.push_back(horse);
        m_horse.actions.assign(HorseActions, {.repeats = true, .speed = 0.0f, .keyCount = HorseKeys});
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "retrato.jpg";
        m_triangle.meshes.push_back(mesh);
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        const bool drawn = model.Value() == Client::Pets::DarkHorseModel ||
                           model.Value() == Client::Tables::PlayerShadowModel ||
                           model.Value() == ClassPartModel(BodyPart::Armor, DarkKnight);
        return drawn ? &m_triangle : nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == PlayerSkeletonModel)
            return &m_player;
        return model.Value() == Client::Pets::DarkHorseModel ? &m_horse : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return m_textures;
    }

private:
    Client::Models::Skeleton m_player;
    Client::Models::Skeleton m_horse;
    Client::Data::Models::Model m_triangle;
    std::array<Client::Render::TextureSlot, 1> m_textures{Client::Render::TextureSlot{0x300}};
};

// The game's world, on the heap: WorldState weighs about 350 KB.
struct Game
{
    Game()
    {
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer(DarkKnight);
        hero.alive = true;
        hero.cellX = HeroX;
        hero.cellY = HeroY;
        hero.scale = 0.9f;
        hero.head = ClassPartModel(BodyPart::Helm, DarkKnight);
        for (std::size_t part = 0; part < hero.body.size(); ++part)
            hero.body[part].model = ClassPartModel(static_cast<BodyPart>(part), DarkKnight);
        world.Runtime().hero = 0;
        for (auto& item : world.OwnCharacter().equipment)
            item.id = NoItemModel;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = drawable ? static_cast<const Client::Models::IModelLibrary&>(drawn)
                                   : static_cast<const Client::Models::IModelLibrary&>(models),
                .items = &items,
                .itemSetTables = &sets,
                .log = log,
                .states = &states,
                .characterStepHooks = &stepHooks};
    }
    ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::Session::CharacterStepHooks stepHooks;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::FrontEnd::SingleModelLibrary models;
    // With `drawable`, the models of what the portrait draws instead of the ones above.
    bool drawable = false;
    PortraitModels drawn;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    LetterPortrait portrait;
};

class LetterPortraitTest : public Client::Tests::GoldenDataTest
{
protected:
    std::unique_ptr<Game> Load(std::uint8_t map)
    {
        auto game = std::make_unique<Game>();
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, map);
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (loaded.HasValue())
            game->states.terrain.current = std::move(loaded).Value();
        game->world.Runtime().map = map;
        return game;
    }

    // 0x4961d0 and, like 0x4977d0 from the letter, the hero's class.
    std::unique_ptr<Game> Built(std::uint8_t map)
    {
        auto game = Load(map);
        const auto resources = game->Resources();
        const auto built = Client::Messenger::BuildThePortrait(game->portrait, resources);
        EXPECT_TRUE(built.HasValue()) << built.Error();
        return game;
    }

    // The portrait built with the models it draws (PortraitModels) and the hero's horse (0x1c16): 0x496530 builds the
    // live dark horse for it (0x497810).
    std::unique_ptr<Game> Riding(std::uint8_t map)
    {
        auto game = Load(map);
        game->drawable = true;
        const auto resources = game->Resources();
        const auto built = Client::Messenger::BuildThePortrait(game->portrait, resources);
        EXPECT_TRUE(built.HasValue()) << built.Error();
        game->Hero().pet.model = 0x1c16;
        std::int32_t action = 0;
        const auto copied = Client::Messenger::CopyTheHeroToThePortrait(game->portrait, action, resources);
        EXPECT_TRUE(copied.HasValue()) << copied.Error();
        EXPECT_TRUE(game->portrait.helper.alive);
        return game;
    }
};

// 0x4961d0: deletes the helper and the character, builds a 0x20a in the hero's cell with 0x511290 and hides it at
// (−300, −300, −300).
TEST_F(LetterPortraitTest, BuildingDrawsFortyFourAndHidesTheCharacter)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Load(map);
        game->portrait.helper.alive = true;
        game->portrait.actor.characterClass = 3;
        const auto resources = game->Resources();
        const auto built = Client::Messenger::BuildThePortrait(game->portrait, resources);
        ASSERT_TRUE(built.HasValue()) << built.Error();
        const ActorState& actor = game->portrait.actor;
        EXPECT_FALSE(game->portrait.helper.alive);
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(actor.type, PlayerSkeletonModel);
        EXPECT_EQ(actor.characterClass, 0);
        EXPECT_EQ(actor.cellX, HeroX);
        EXPECT_EQ(actor.cellY, HeroY);
        EXPECT_EQ(actor.position.x, -300.0f);
        EXPECT_EQ(actor.position.y, -300.0f);
        EXPECT_EQ(actor.position.z, -300.0f);
        EXPECT_EQ(actor.cellXAccesses, 0);
        EXPECT_EQ(actor.cellYAccesses, 0);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, BuildDraws).State());
    }
}

// 0x513100: part 0 looks at its own, but parts 1 to 4 look at the next one's word (Q-C1024). With the base armor in the
// pants, the armor becomes the whole class's even though it is not the base.
TEST_F(LetterPortraitTest, TheClassPartsReadTheWordOfTheNextPart)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        ActorState& actor = game->portrait.actor;
        // Action 0x30 is between 0x22 and 0x97: without 0x4f2250.
        actor.animation.action = 0x30;
        constexpr std::uint8_t Base = DarkKnight & 7;
        actor.body[0].model = ClassPartModel(BodyPart::Helm, Base);
        actor.body[0].level = 7;
        actor.body[1].model = 0x0300;
        actor.body[1].level = 5;
        actor.body[1].flags1b = 1;
        actor.body[2].model = ClassPartModel(BodyPart::Armor, Base);
        actor.body[3].model = 0x0400;
        const CrtRandom before = game->cloth.random;
        const auto resources = game->Resources();
        const auto set = Client::Messenger::SetThePortraitClass(game->portrait, DarkKnight, resources);
        ASSERT_TRUE(set.HasValue()) << set.Error();
        EXPECT_EQ(actor.characterClass, DarkKnight);
        EXPECT_EQ(actor.body[0].model, ClassPartModel(BodyPart::Helm, DarkKnight));
        EXPECT_EQ(actor.body[0].level, 0);
        EXPECT_EQ(actor.body[1].model, ClassPartModel(BodyPart::Armor, DarkKnight));
        EXPECT_EQ(actor.body[1].level, 0);
        EXPECT_EQ(actor.body[1].flags1b, 0);
        // The pants with the base armor stay: their comparison read the gloves.
        EXPECT_EQ(actor.body[2].model, ClassPartModel(BodyPart::Armor, Base));
        EXPECT_EQ(game->cloth.random.State(), before.State());
    }
}

// 0x497810 with 0x5139f0(0, equipment, character, helper): the Unilia builds its own helper and the dark horse, which
// comes after, finds it already alive and does not touch it (Q-C1026); the pet stays the horse's. +0x688 is 0.9 · 0.7
// divided by the hero's scale.
TEST_F(LetterPortraitTest, DressingKeepsTheFirstHelperOnly)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        Equipment equipment = NothingWorn();
        // b[4] & 3 at 2 (Unilia, wings at 3 with b[8] & 7 at 0) and b[11] & 1 (the dark horse).
        equipment[4] = std::byte{0xfe};
        equipment[11] = std::byte{0xf1};
        const auto resources = game->Resources();
        const auto dressed = Client::Messenger::DressThePortrait(game->portrait, equipment, 0x7a, resources);
        ASSERT_TRUE(dressed.HasValue()) << dressed.Error();
        const LetterPortrait& portrait = game->portrait;
        EXPECT_TRUE(portrait.helper.alive);
        EXPECT_EQ(portrait.helper.type, 0xc6);
        EXPECT_EQ(portrait.helper.rider, Client::Kernel::ActorPoolSize);
        EXPECT_EQ(portrait.actor.pet.model, 0x1c16);
        EXPECT_EQ(portrait.helperScale,
                  static_cast<float>(static_cast<double>(portrait.helper.scale) * static_cast<double>(0.7f) / 0.9));
        EXPECT_EQ(portrait.actor.safeZone, 0);
        for (const auto& helper : game->world.Helpers())
            EXPECT_FALSE(helper.alive);
    }
}

// 0x49784a..0x497869: with wings and the action past 0x82, the safe zone.
TEST_F(LetterPortraitTest, DressingWithWingsAndAFlyingActionMarksTheSafeZone)
{
    auto game = Built(0);
    Equipment equipment = NothingWorn();
    // The wings 0x1a12 (b[4] with bits 2 and 3 at 0) and no pet (b[4] & 3 at 3).
    equipment[4] = std::byte{0xf3};
    const auto resources = game->Resources();
    ASSERT_TRUE(Client::Messenger::DressThePortrait(game->portrait, equipment, 0x83, resources).HasValue());
    EXPECT_EQ(game->portrait.actor.wings.model, 0x1a12);
    EXPECT_EQ(game->portrait.actor.safeZone, 1);
    ASSERT_TRUE(Client::Messenger::DressThePortrait(game->portrait, equipment, 0x82, resources).HasValue());
    EXPECT_EQ(game->portrait.actor.safeZone, 0);
}

// 0x496530 with the hero's +0x3e3 at 0: the class (0x513100), the hero's head and body as they are, and 0x495a20 with
// the last action (0 is not in the list: the 0x7a). With nothing different, no rand().
TEST_F(LetterPortraitTest, CopyingTheHeroTakesTheBodyAsItIs)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        auto twin = Built(map);
        ActorState& hero = game->Hero();
        hero.body[1].model = 0x0313;
        hero.body[1].level = 9;
        hero.body[1].flags1b = 1;
        twin->Hero() = hero;
        std::int32_t action = 0x83;
        const auto resources = game->Resources();
        const auto copied = Client::Messenger::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(copied.HasValue()) << copied.Error();
        EXPECT_TRUE(copied.Value());
        const ActorState& actor = game->portrait.actor;
        EXPECT_EQ(actor.characterClass, DarkKnight);
        EXPECT_EQ(actor.head, hero.head);
        EXPECT_EQ(actor.body[1].model, 0x0313);
        EXPECT_EQ(actor.body[1].level, 9);
        EXPECT_EQ(game->portrait.shownAction, 0x7a);
        EXPECT_EQ(action, 0x83);
        // The twin does the same by hand: the class with 0x4977d0, the copy and 0x495a20(0x7a, 0).
        const auto twinResources = twin->Resources();
        ASSERT_TRUE(Client::Messenger::SetThePortraitClass(twin->portrait, DarkKnight, twinResources).HasValue());
        twin->portrait.actor.head = hero.head;
        twin->portrait.actor.body = hero.body;
        ASSERT_TRUE(Client::Messenger::ShowThePortraitAction(twin->portrait, 0, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());

        const CrtRandom before = game->cloth.random;
        const auto again = Client::Messenger::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(again.HasValue()) << again.Error();
        EXPECT_FALSE(again.Value());
        EXPECT_EQ(game->cloth.random.State(), before.State());
    }
}

// 0x496530 with the hero's +0x3e3 other than 0: compares with the own record (one access) and copies the wings from
// it (another), with the 3848 rand() of each close before 0x495a20.
TEST_F(LetterPortraitTest, CopyingATransformedHeroReadsTheOwnRecord)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        auto twin = Built(map);
        for (Game* world : {game.get(), twin.get()})
        {
            world->Hero().byte3e3 = 1;
            // The body like the class's: the head and the five parts already match.
            ASSERT_TRUE(
                Client::Messenger::SetThePortraitClass(world->portrait, DarkKnight, world->Resources()).HasValue());
            for (std::size_t part = 0; part < world->portrait.actor.body.size(); ++part)
                world->portrait.actor.body[part] = {.model = ClassPartModel(static_cast<BodyPart>(part), DarkKnight)};
            auto& wings = world->world.OwnCharacter().equipment[7];
            wings.id = 0x1802;
            wings.scalar1Low = 3 << 3;
        }
        const CrtRandom start = game->cloth.random;
        std::int32_t action = 0x7a;
        const auto resources = game->Resources();
        const auto copied = Client::Messenger::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(copied.HasValue()) << copied.Error();
        EXPECT_TRUE(copied.Value());
        EXPECT_EQ(game->portrait.actor.wings.model, 0x1802 + 0x212);
        EXPECT_EQ(game->portrait.actor.wings.level, 3);

        twin->cloth.random.Discard(2 * OwnRecordDraws);
        twin->portrait.actor.wings = game->portrait.actor.wings;
        const auto twinResources = twin->Resources();
        ASSERT_TRUE(Client::Messenger::ShowThePortraitAction(twin->portrait, 0, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_GE(DrawsBetween(start, game->cloth.random), 2 * OwnRecordDraws);
    }
}

// 0x496530's tail with a different pet: 0x495a20(0x7a, 0), +0x6a4 at 0x7a and the helper again by the pet (0x1c16:
// the 0xbf), with its alpha at 0 and +0x688 with the character's own scale.
TEST_F(LetterPortraitTest, ANewPetRebuildsTheHelper)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        game->Hero().pet.model = 0x1c16;
        std::int32_t action = 0x83;
        const auto resources = game->Resources();
        const auto copied = Client::Messenger::CopyTheHeroToThePortrait(game->portrait, action, resources);
        ASSERT_TRUE(copied.HasValue()) << copied.Error();
        const LetterPortrait& portrait = game->portrait;
        EXPECT_EQ(action, 0x7a);
        EXPECT_EQ(portrait.shownAction, 0x7a);
        EXPECT_TRUE(portrait.helper.alive);
        EXPECT_EQ(portrait.helper.type, 0xbf);
        EXPECT_EQ(portrait.helper.horse.alpha, 0.0f);
        EXPECT_EQ(portrait.helperScale,
                  static_cast<float>(static_cast<double>(portrait.helper.scale) * static_cast<double>(0.7f) /
                                     static_cast<double>(portrait.actor.scale)));
    }
}

// 0x495a20: the gestures (0x6c7b14) on foot, forward from 0x7a the 0x83 with animation 0x99; backward, the 0x78
// (0x495b00..0x495b33): 0x4f4a10 with the world at 0 and, after, +0x381 at 1, the frame at 0 and the target at 0xffff.
TEST_F(LetterPortraitTest, TurningWalksTheGestureList)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        const auto resources = game->Resources();
        const auto next = Client::Messenger::ShowThePortraitAction(game->portrait, 0x7a, 1, resources);
        ASSERT_TRUE(next.HasValue()) << next.Error();
        EXPECT_EQ(next.Value(), 0x83);
        EXPECT_EQ(game->portrait.shownAction, 0x83);
        EXPECT_EQ(game->portrait.actor.animation.action, 0x99);
        EXPECT_EQ(game->world.Runtime().map, map);
        game->portrait.actor.animation.frame = 3.0f;
        const auto previous = Client::Messenger::ShowThePortraitAction(game->portrait, 0x7a, -1, resources);
        ASSERT_TRUE(previous.HasValue()) << previous.Error();
        EXPECT_EQ(previous.Value(), 0x78);
        EXPECT_EQ(game->portrait.shownAction, 0x78);
        // The 1 of 0x495b19, and 0x495daf's step (0x4fd5f0, 0x4fdb9a..0x4fdbd4) raises it to 2.
        EXPECT_EQ(game->portrait.actor.byte381, 2);
        EXPECT_EQ(game->portrait.actor.animation.frame, 0.0f);
        EXPECT_EQ(game->portrait.actor.attackTarget, 0xffff);
        EXPECT_EQ(game->world.Runtime().map, map);
    }
}

// 0x495a20 with a live ridden helper: the short list (0x6c7b08), from 0x7a to 0x7c (0x4f37c0).
TEST_F(LetterPortraitTest, ARiddenHelperUsesTheShortList)
{
    auto game = Built(2);
    game->portrait.helper.alive = true;
    game->portrait.helper.type = 0xc6;
    const auto resources = game->Resources();
    const auto next = Client::Messenger::ShowThePortraitAction(game->portrait, 0x7a, 1, resources);
    ASSERT_TRUE(next.HasValue()) << next.Error();
    EXPECT_EQ(next.Value(), 0x7c);
    game->portrait.helper.alive = false;
    const auto walking = Client::Messenger::ShowThePortraitAction(game->portrait, 0x7c, 1, resources);
    ASSERT_TRUE(walking.HasValue()) << walking.Error();
    EXPECT_EQ(walking.Value(), 0x94);
}

// 0x497890: the character's angles at (−20, 5, 60).
TEST_F(LetterPortraitTest, TurningThePortraitSetsItsAngles)
{
    auto game = Built(0);
    Client::Messenger::TurnThePortrait(game->portrait);
    EXPECT_EQ(game->portrait.actor.angles.x, -20.0f);
    EXPECT_EQ(game->portrait.actor.angles.y, 5.0f);
    EXPECT_EQ(game->portrait.actor.angles.z, 60.0f);
}

// 0x495760: 0x620d10's viewport (GL's y from the bottom, 141 high) scaled and truncated, and the view that puts the
// character 10000 ahead of the camera: the portrait's center lands in the middle of the viewport.
TEST_F(LetterPortraitTest, ThePortraitPassLooksAtTheCharacterFromAfar)
{
    auto game = Built(0);
    Client::Ui::OldWindowBase base;
    base.x = 100;
    base.y = 50;
    base.width = 0x77;
    ActorState& actor = game->portrait.actor;
    actor.position = {1234.0f, 5678.0f, 90.0f};
    const auto small = Client::Messenger::PortraitPass({640, 480}, base, actor);
    EXPECT_EQ(small.viewport, glm::ivec4(100, 480 - 50 - 141, 0x77, 141));
    const auto large = Client::Messenger::PortraitPass({1280, 960}, base, actor);
    EXPECT_EQ(large.viewport, glm::ivec4(200, 960 - 100 - 282, 0x77 * 2, 282));
    // The character, 75 above its feet, lands at the image's center.
    const glm::vec4 eye = small.view * glm::vec4(actor.position.x, actor.position.y, actor.position.z + 75.0f, 1.0f);
    const glm::vec4 clip = small.projection * eye;
    EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-4f);
    EXPECT_NEAR(clip.y / clip.w, 0.0f, 1e-4f);
    EXPECT_LT(eye.z, -9999.0f);
    // With the dark horse, 50 lower.
    actor.pet.model = 0x1c16;
    const auto riding = Client::Messenger::PortraitPass({640, 480}, base, actor);
    const glm::vec4 lifted =
        riding.view * glm::vec4(actor.position.x, actor.position.y, actor.position.z + 125.0f, 1.0f);
    EXPECT_NEAR(lifted.y, eye.y, 1e-3f);
}

// 0x497c10 and 0x495760 before the draw: with the frame below +0x6a8 and still, an action outside 0x7a..0x82 goes back
// to 0x7a without drawing the rand() % 4; the pass sets the angles, the scale by the zoom, the lights and +0x3e4.
TEST_F(LetterPortraitTest, PreparingTheDrawStepsTheCharacter)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Built(map);
        LetterPortrait& portrait = game->portrait;
        Client::Ui::OldPortraitPart part;
        part.action = 0x83;
        part.angle = 45.0f;
        part.zoom = 0.9f;
        portrait.idle = 1;
        portrait.frameMark = 5;
        portrait.actor.animation.frame = 2.0f;
        const auto resources = game->Resources();
        const auto pass = Client::Messenger::PrepareThePortraitDraw(portrait, part, {640, 480}, resources);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        EXPECT_EQ(portrait.idle, 0);
        EXPECT_EQ(portrait.shownAction, 0x7a);
        EXPECT_EQ(part.action, 0x83);
        EXPECT_EQ(portrait.frameMark, 0);
        EXPECT_EQ(game->world.Runtime().map, map);
        EXPECT_EQ(portrait.actor.angles.z, 45.0f);
        EXPECT_EQ(portrait.actor.scale, static_cast<float>(0.9 * static_cast<double>(0.7f)));
        EXPECT_EQ(portrait.actor.light, glm::vec3(1.0f));
        EXPECT_EQ(portrait.actor.safeZone, 0);
        EXPECT_EQ(portrait.actor.byte3e4, 1);

        // Without passing the mark, +0x6a8 follows the frame.
        portrait.actor.animation.frame = 7.5f;
        ASSERT_TRUE(Client::Messenger::PrepareThePortraitDraw(portrait, part, {640, 480}, resources).HasValue());
        EXPECT_EQ(portrait.frameMark, 7);
    }
}

// 0x495796: 0x444010(helper, 1) after 0x4fd5f0 and 0x505de0, with the world at 0: the horse follows the character even
// though its cell (that of (−300, −300) by 0.01, with the low byte) is safe, with its rand() after the character's.
// It is the same as the step without the horse and 0x444010(helper, 1) by hand after.
TEST_F(LetterPortraitTest, PreparingTheDrawStepsTheLiveHelperAfterTheCharacter)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Riding(map);
        auto twin = Riding(map);
        ASSERT_FALSE(HasFatalFailure());
        for (Game* each : {game.get(), twin.get()})
        {
            const auto& position = each->portrait.actor.position;
            const auto cell = Client::Data::Terrain::CellIndex(
                static_cast<std::uint8_t>(static_cast<std::int32_t>(position.x * 0.01f)),
                static_cast<std::uint8_t>(static_cast<std::int32_t>(position.y * 0.01f)));
            each->states.terrain.current.attributes->cells[cell] |= 1;
        }
        Client::Ui::OldPortraitPart part;
        part.action = 0x7a;
        part.zoom = 1.0f;
        game->portrait.actor.animation.frame = 1.0f;
        twin->portrait.actor.animation.frame = 1.0f;

        const auto resources = game->Resources();
        ASSERT_TRUE(Client::Messenger::PrepareThePortraitDraw(game->portrait, part, {640, 480}, resources).HasValue());

        const auto twinResources = twin->Resources();
        twin->portrait.helper.alive = false;
        ASSERT_TRUE(
            Client::Messenger::PrepareThePortraitDraw(twin->portrait, part, {640, 480}, twinResources).HasValue());
        twin->portrait.helper.alive = true;
        twin->world.Runtime().map = 0;
        const auto stepped = Client::Pets::StepTheHelperOutsideTheTable(
            twin->portrait.helper, twin->portrait.actor,
            Client::Pets::HelperStepContext{.world = twin->world,
                                            .models = &twin->drawn,
                                            .random = twin->cloth.random,
                                            .worldClock = 0.0f,
                                            .scene = Client::Kernel::SceneId::Game,
                                            .terrain = &twin->states.terrain.current,
                                            .logicSteps = twin->cloth.logicSteps,
                                            .cameraShake = &twin->input.shake});
        twin->world.Runtime().map = map;
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        const auto& helper = game->portrait.helper;
        EXPECT_TRUE(helper.alive);
        EXPECT_EQ(helper.position, game->portrait.actor.position);
        EXPECT_GT(helper.horse.alpha, 0.0f);
        EXPECT_EQ(helper.horse.alpha, twin->portrait.helper.horse.alpha);
        EXPECT_EQ(helper.horse.animation.action, twin->portrait.helper.horse.animation.action);
        EXPECT_EQ(helper.horse.animation.frame, twin->portrait.helper.horse.animation.frame);
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(game->world.Runtime().map, map);
    }
}

// What the game's draw lends the portrait (LetterPortraitDraw): the 2D batch at 640 × 480 with the fake text (6
// columns per letter, 13 high) and made-up texts 997 to 999, the models' batch, the terrain light at (0.25, 0.5, 0.75)
// and the frame's sprites. Records the closes of both batches, with the vertices of the models' one.
class PortraitDrawing
{
public:
    explicit PortraitDrawing(Game& game) : m_game(game)
    {
        sprites.Begin(Client::Core::PixelSize{640, 480});
        lighting->cells.fill({0.25f, 0.5f, 0.75f});
    }

    PortraitDrawing(const PortraitDrawing&) = delete;
    PortraitDrawing& operator=(const PortraitDrawing&) = delete;
    PortraitDrawing(PortraitDrawing&&) = delete;
    PortraitDrawing& operator=(PortraitDrawing&&) = delete;
    ~PortraitDrawing() = default;

    // With `cloth` of the frame's rand(): the game's or, between frames, its copy.
    Client::Messenger::LetterPortraitDraw Draw(Client::Models::ClothWorld& cloth, bool originalFrame)
    {
        Client::Kernel::WorldRuntime& runtime = m_game.world.Runtime();
        ActorState& hero = m_game.Hero();
        m_hud.emplace(Client::Hud::GameHudContext{.world = m_game.world,
                                                  .random = cloth.random,
                                                  .ownRecordDepth = runtime.ownRecordDepth,
                                                  .cellXDepth = hero.cellXAccesses,
                                                  .cellYDepth = hero.cellYAccesses,
                                                  .hud = runtime.hud,
                                                  .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                                  .sound = sound,
                                                  .sprites = sprites,
                                                  .text = text,
                                                  .texts = &texts});
        Client::Messenger::LetterPortraitDraw draw{.hud = *m_hud, .batch = models};
        draw.flushSprites = [this]() { flushes.emplace_back("2d", models.Vertices().size()); };
        draw.flushModels = [this]() { flushes.emplace_back("3d", models.Vertices().size()); };
        draw.characters.dynamicLighting = lighting.get();
        draw.characters.clothWorld = &cloth;
        draw.characters.sprites = &frameSprites;
        draw.characters.particles = &m_game.world.Particles();
        draw.characters.effects = &m_game.world.Map().effects;
        draw.characters.effectHooks = &effectHooks;
        draw.helperModels = &m_game.drawn;
        draw.originalFrame = originalFrame;
        return draw;
    }

    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Tables::TextTable texts =
        Client::Tests::MakeTextTable({{997, "Arrastrar gira"}, {998, "Rueda acerca"}, {999, "Clic vuelve"}});
    std::optional<Client::Audio::SoundId> sound;
    Client::Render::ModelBatch models;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    Client::Particles::FrameSprites frameSprites;
    Client::Effects::ActorEffectHooks effectHooks;
    std::vector<std::pair<std::string, std::size_t>> flushes;

private:
    Game& m_game;
    std::optional<Client::Hud::GameHudContext> m_hud;
};

struct SpriteQuad
{
    Client::Render::Color color{};
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// The 2D batch's quads with `slot` (or without texture), at 640 × 480 with y down and with the u and v of their
// bottom-right corner.
std::vector<SpriteQuad> QuadsOf(const Client::Render::SpriteBatch& sprites,
                                std::optional<Client::Render::TextureSlot> slot)
{
    std::vector<SpriteQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(SpriteQuad{.color = vertices[first].color,
                                       .left = vertices[first].x,
                                       .top = 480.0f - vertices[first].y,
                                       .right = vertices[first + 2].x,
                                       .bottom = 480.0f - vertices[first + 2].y,
                                       .u = vertices[first + 2].u,
                                       .v = vertices[first + 2].v});
        }
    }
    return quads;
}

// 0x4958e2..0x495a0c: closes the 2D, starts the portrait pass with the depth cleared, draws the horse
// (0x445260(helper, 1): seen, with scale 0x3f3ae148 and its rand() % 30 + 70) and then the character (0x50ba30,
// without the shadow), and closes the pass. The rand(): 0x445260's and then 0x50ba30's.
TEST_F(LetterPortraitTest, ThePortraitModelDrawsTheHelperAndThenTheCharacterOverACleanDepth)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Riding(map);
        auto twin = Riding(map);
        ASSERT_FALSE(HasFatalFailure());
        Client::Ui::OldPortraitPart part;
        part.base.x = 100;
        part.base.y = 50;
        part.base.width = 0x77;
        part.action = 0x7a;
        part.angle = 30.0f;
        part.zoom = 1.0f;
        const auto resources = game->Resources();
        const auto pass = Client::Messenger::PrepareThePortraitDraw(game->portrait, part, {640, 480}, resources);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        const auto twinResources = twin->Resources();
        const auto twinPass =
            Client::Messenger::PrepareThePortraitDraw(twin->portrait, part, {640, 480}, twinResources);
        ASSERT_TRUE(twinPass.HasValue()) << twinPass.Error();
        ASSERT_EQ(game->cloth.random.State(), twin->cloth.random.State());

        PortraitDrawing drawing(*game);
        const auto drawn = Client::Messenger::DrawThePortraitModel(game->portrait, pass.Value(),
                                                                   drawing.Draw(game->cloth, true), resources);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(drawing.flushes.size(), 2u);
        EXPECT_EQ(drawing.flushes[0], std::make_pair(std::string("2d"), std::size_t{0}));
        EXPECT_EQ(drawing.flushes[1].first, "3d");
        EXPECT_GT(drawing.flushes[1].second, 0u);
        EXPECT_TRUE(drawing.models.Pass().clearDepth);
        EXPECT_EQ(drawing.models.Pass().viewport, pass.Value().viewport);
        EXPECT_TRUE(game->portrait.helper.seen);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(game->portrait.helper.scale), 0x3f3ae148u);

        // By hand: 0x445260(helper, 1) draws one; 0x50ba30, its own.
        PortraitDrawing manual(*twin);
        const Client::Messenger::LetterPortraitDraw draw = manual.Draw(twin->cloth, true);
        manual.models.Begin(pass.Value());
        const CrtRandom beforeHelper = twin->cloth.random;
        ASSERT_TRUE(Client::Pets::DrawTheHelperOutsideTheTable(
                        twin->portrait.helper, twin->portrait.actor,
                        Client::Pets::HelperDrawContext{.batch = manual.models,
                                                        .models = twin->drawn,
                                                        .world = twin->world,
                                                        .random = twin->cloth.random,
                                                        .lighting = manual.lighting.get(),
                                                        .sprites = &manual.frameSprites})
                        .HasValue());
        EXPECT_EQ(DrawsBetween(beforeHelper, twin->cloth.random), 1);
        ASSERT_TRUE(Client::Actors::Drawing::DrawCharacterOutsideThePool(manual.models, twin->drawn, twin->world,
                                                                         twin->portrait.actor, twin->portrait.cloth,
                                                                         draw.characters, true)
                        .HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(drawing.models.Vertices().size(), manual.models.Vertices().size());
    }
}

// 0x497c1b..0x497ca2: the blank portrait (+0x6c0 at 1, the "webzen" letter), the black box of 119 × 141 and the logo
// (0x653) of 80 × 62 at (x + 0x14, y + 0x26) with v at 0.76171875. Without the character, without pass and without
// rand().
TEST_F(LetterPortraitTest, ABlankPortraitIsABlackBoxWithTheLogo)
{
    auto game = Load(0);
    Client::Ui::OldPortraitPart part;
    part.base.x = 100;
    part.base.y = 50;
    part.blank = 1;
    PortraitDrawing drawing(*game);
    const CrtRandom before = game->cloth.random;
    const auto resources = game->Resources();
    ASSERT_TRUE(Client::Messenger::DrawTheLetterPortrait(part, drawing.Draw(game->cloth, true), resources).HasValue());
    const std::vector<SpriteQuad> black = QuadsOf(drawing.sprites, std::nullopt);
    ASSERT_EQ(black.size(), 1u);
    EXPECT_EQ(black[0].color, (Client::Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f}));
    EXPECT_FLOAT_EQ(black[0].left, 100.0f);
    EXPECT_FLOAT_EQ(black[0].top, 50.0f);
    EXPECT_FLOAT_EQ(black[0].right, 219.0f);
    EXPECT_FLOAT_EQ(black[0].bottom, 191.0f);
    const std::vector<SpriteQuad> logo = QuadsOf(drawing.sprites, Client::Render::TextureSlot{0x653});
    ASSERT_EQ(logo.size(), 1u);
    EXPECT_EQ(logo[0].color, Client::Render::Color{});
    EXPECT_FLOAT_EQ(logo[0].left, 120.0f);
    EXPECT_FLOAT_EQ(logo[0].top, 88.0f);
    EXPECT_FLOAT_EQ(logo[0].right, 200.0f);
    EXPECT_FLOAT_EQ(logo[0].bottom, 150.0f);
    EXPECT_FLOAT_EQ(logo[0].u, 1.0f);
    EXPECT_FLOAT_EQ(logo[0].v, 0.76171875f);
    EXPECT_TRUE(drawing.flushes.empty());
    EXPECT_EQ(game->cloth.random.State(), before.State());
}

// The whole 0x497c10 in Lorencia and Devias: the step (PrepareThePortraitDraw), the model and, with +0x4c & 1, the
// help icon (0x651) of 16 × 16 at (x + 1, y + height − 0x11); with the help in view, gray (0.6) of 15 × 15 at (x + 2,
// y + height − 0x10) with u and v at 0.9375 and texts 997 to 999 in 0x5af090's box with mode 1: centered at x + width
// / 2, from y + height − (13 + 2) · 3 and each against the left edge.
TEST_F(LetterPortraitTest, TheWholePortraitDrawsTheModelAndThenItsHelp)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        for (const std::int32_t help : {0, 1})
        {
            SCOPED_TRACE(testing::Message() << static_cast<int>(map) << " " << help);
            auto game = Riding(map);
            ASSERT_FALSE(HasFatalFailure());
            auto part = std::make_unique<Client::Ui::OldPortraitPart>();
            part->base.x = 100;
            part->base.y = 50;
            part->base.width = 0x77;
            part->base.height = 0x8d;
            part->base.flags = 1;
            part->help = help;
            part->action = 0x7a;
            part->zoom = 1.0f;
            part->actor = std::shared_ptr<void>(&game->portrait, [](void*) {});
            PortraitDrawing drawing(*game);
            const auto resources = game->Resources();
            const auto drawn =
                Client::Messenger::DrawTheLetterPortrait(*part, drawing.Draw(game->cloth, true), resources);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            ASSERT_EQ(drawing.flushes.size(), 2u);
            EXPECT_GT(drawing.models.Vertices().size(), 0u);
            EXPECT_TRUE(game->portrait.helper.seen);
            const std::vector<SpriteQuad> icon = QuadsOf(drawing.sprites, Client::Render::TextureSlot{0x651});
            ASSERT_EQ(icon.size(), 1u);
            if (help == 0)
            {
                EXPECT_EQ(icon[0].color, Client::Render::Color{});
                EXPECT_FLOAT_EQ(icon[0].left, 101.0f);
                EXPECT_FLOAT_EQ(icon[0].top, 50.0f + 0x8d - 0x11);
                EXPECT_FLOAT_EQ(icon[0].right, 117.0f);
                EXPECT_FLOAT_EQ(icon[0].u, 1.0f);
                EXPECT_TRUE(drawing.text.texts.empty());
                continue;
            }
            EXPECT_FLOAT_EQ(icon[0].color.red, 0.6f);
            EXPECT_FLOAT_EQ(icon[0].left, 102.0f);
            EXPECT_FLOAT_EQ(icon[0].top, 50.0f + 0x8d - 0x10);
            EXPECT_FLOAT_EQ(icon[0].right, 117.0f);
            EXPECT_FLOAT_EQ(icon[0].u, 0.9375f);
            EXPECT_FLOAT_EQ(icon[0].v, 0.9375f);
            const auto& texts = drawing.text.texts;
            ASSERT_EQ(texts.size(), 3u);
            EXPECT_EQ(texts[0].text, "Arrastrar gira");
            EXPECT_EQ(texts[1].text, "Rueda acerca");
            EXPECT_EQ(texts[2].text, "Clic vuelve");
            // The box: 14 letters · 6 + 4 = 88 wide, centered at 100 + 59: from 115, and the text from 116.
            EXPECT_EQ(texts[0].x, 116);
            EXPECT_EQ(texts[0].y, 50 + 0x8d - (13 + 2) * 3);
            for (const auto& text : texts)
            {
                EXPECT_EQ(text.firstColumn, 0);
                EXPECT_EQ(text.columns, 86);
            }
            EXPECT_EQ(game->world.Runtime().hud.tooltip.next, 3);
        }
    }
}

// An intermediate MuRet draw draws a copy of the portrait with the copy of the frame's rand(): the character, the horse
// and its mark stay as the original's last frame left them, and the game's rand() does not move.
TEST_F(LetterPortraitTest, AnIntermediateDrawLeavesThePortraitAsTheLastFrameLeftIt)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Riding(map);
        ASSERT_FALSE(HasFatalFailure());
        Client::Ui::OldPortraitPart part;
        part.action = 0x7a;
        part.zoom = 1.0f;
        part.actor = std::shared_ptr<void>(&game->portrait, [](void*) {});
        game->portrait.actor.animation.frame = 3.0f;
        const LetterPortrait before = game->portrait;
        const CrtRandom random = game->cloth.random;
        Client::Models::ClothWorld projected = game->cloth;
        PortraitDrawing drawing(*game);
        const auto resources = game->Resources();
        ASSERT_TRUE(
            Client::Messenger::DrawTheLetterPortrait(part, drawing.Draw(projected, false), resources).HasValue());
        EXPECT_GT(drawing.models.Vertices().size(), 0u);
        EXPECT_EQ(game->cloth.random.State(), random.State());
        EXPECT_EQ(game->portrait.frameMark, before.frameMark);
        EXPECT_EQ(game->portrait.actor.animation.frame, before.actor.animation.frame);
        EXPECT_EQ(game->portrait.actor.byte3e4, before.actor.byte3e4);
        EXPECT_EQ(game->portrait.helper.seen, before.helper.seen);
        EXPECT_EQ(game->portrait.helper.scale, before.helper.scale);
        EXPECT_EQ(game->portrait.helper.position, before.helper.position);
    }
}

// The letter windows with the game's hook (LetterPortraitHook): the texts, the measure of 2 per character and the old
// windows' step.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 2, 13};
    }
};

std::map<std::size_t, std::string> LetterTexts()
{
    return {{1000, "Para"},     {1001, "Mandar"},    {1002, "Cerrar"},          {1003, "Atras"},
            {1004, "Adelante"}, {1010, "Responder"}, {1011, "Borrar"},          {1012, "Antes"},
            {1013, "Despues"},  {1054, "Carta: %s"}, {1071, "Carta nueva (%d)"}};
}

class LetterPortraitWindowsTest : public LetterPortraitTest
{
protected:
    [[nodiscard]] Client::Ui::OldWindowStepView View(Game& game, const Client::Scenes::WorldEntryResources& resources)
    {
        Client::Ui::OldWindowStepView view{.texts = &m_oldTexts, .measure = &m_oldMeasure};
        view.portrait = Client::Messenger::LetterPortraitHook(resources);
        view.field78abaac = game.world.Runtime().ui.field78abaac;
        view.heroName = "Benteveo";
        return view;
    }

    // 0x48ee60(7) at (100, 100): the write window with its portrait (0x497f50).
    Client::Ui::OldWindow* OpenTheWriteWindow(Game& game, const Client::Scenes::WorldEntryResources& resources)
    {
        auto view = View(game, resources);
        auto globals = Client::Hud::OldWindowGlobalsOf(game.world.Runtime());
        auto created = Client::Ui::CreateTheOldWindow(
            game.world.Runtime().oldWindows,
            Client::Ui::OldWindowRequest{
                .kind = Client::Ui::LetterWriteCase, .x = 100, .y = 100, .data = "Carta", .aux = 0, .flags = 0},
            m_oldTexts, m_oldMeasure, &globals, &view);
        EXPECT_TRUE(created.HasValue()) << created.Error();
        if (!created.HasValue())
            return nullptr;
        return &game.world.Runtime().oldWindows.windows.at(created.Value());
    }

    static LetterPortrait& PortraitOf(Client::Ui::OldWindow& window)
    {
        return *std::static_pointer_cast<LetterPortrait>(window.letter.portrait.actor);
    }

    LetterMeasure m_letters;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(LetterTexts());
    Client::Ui::OldWindowTexts m_oldTexts = Client::Messenger::OldWindowTextsOf(&m_texts);
    Client::Ui::OldWindowMeasure m_oldMeasure{.measure = m_letters,
                                              .dcFont = Client::Render::Text::FontKind::Normal,
                                              .screen = Client::Core::PixelSize{640, 480}};
};

// 0x497f50 end to end: the portrait built (0x4961d0), the hero copied (0x496530, with the class by 0x513100), action
// 0x7a (0x4978f0) and angle 90 (0x497890), with each one's rand() in that order. Opening the window draws no others.
TEST_F(LetterPortraitWindowsTest, OpeningTheWriteWindowCopiesTheHero)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Load(map);
        auto twin = Load(map);
        const auto resources = game->Resources();
        Client::Ui::OldWindow* write = OpenTheWriteWindow(*game, resources);
        ASSERT_NE(write, nullptr);
        const Client::Ui::OldPortraitPart& part = write->letter.portrait;
        EXPECT_EQ(part.built, 1);
        EXPECT_EQ(part.follow, 1);
        EXPECT_EQ(part.action, 0x7a);
        EXPECT_EQ(part.baseAngle, 90.0f);
        EXPECT_EQ(part.angle, 90.0f);
        const LetterPortrait& portrait = PortraitOf(*write);
        EXPECT_EQ(portrait.actor.characterClass, DarkKnight);
        EXPECT_EQ(portrait.actor.body[1].model, ClassPartModel(BodyPart::Armor, DarkKnight));
        EXPECT_EQ(portrait.shownAction, 0x7a);
        EXPECT_EQ(portrait.actor.angles.z, 60.0f);

        const auto twinResources = twin->Resources();
        LetterPortrait& expected = twin->portrait;
        std::int32_t action = 0;
        ASSERT_TRUE(Client::Messenger::BuildThePortrait(expected, twinResources).HasValue());
        ASSERT_TRUE(Client::Messenger::CopyTheHeroToThePortrait(expected, action, twinResources).HasValue());
        ASSERT_TRUE(Client::Messenger::ShowThePortraitAction(expected, 0x7a, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
        EXPECT_GT(DrawsBetween(CrtRandom(Seed), game->cloth.random), BuildDraws);
    }
}

// The C7 end to end (0x4dcb70): the read window builds its portrait (0x4999d0) and dresses it with pkt[8]'s class
// (0x20: the 1), the equipment (the Unilia, 0xc6) and action 2 + 0x78; direction 0x4f gives angle 15 · 6 and zoom
// ((1 · 5 + 0x28) · 2) · 0.01. The rand(): 0x4961d0, 0x513100, 0x5139f0 and 0x495a20, in that order.
TEST_F(LetterPortraitWindowsTest, ReceivingALetterDressesThePortrait)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        auto game = Load(map);
        auto twin = Load(map);
        const auto resources = game->Resources();
        Client::Ui::OldWindowManager& manager = game->world.Runtime().oldWindows;
        Client::Messenger::AddALetter(manager.letterModel, 7, "Tordo", "Saludos", "2026/11/13", "10:20", 0);
        Equipment equipment = NothingWorn();
        equipment[4] = std::byte{0xfe};
        const Client::Protocol::Messenger::LetterRead read{.index = 7,
                                                           .length = 9,
                                                           .portraitClass = 0x20,
                                                           .equipment = equipment,
                                                           .direction = 0x4f,
                                                           .action = 2,
                                                           .body = "Hola che",
                                                           .copy = {}};
        const Client::Chat::ChatReceiveContext context{.world = game->world,
                                                       .random = game->cloth.random,
                                                       .measure = &m_letters,
                                                       .screen = Client::Core::PixelSize{640, 480},
                                                       .scene = Client::Kernel::SceneId::Game,
                                                       .texts = &m_texts,
                                                       .worldEntry = &resources};
        const auto shown = Client::Messenger::ApplyLetterRead(read, context);
        ASSERT_TRUE(shown.HasValue()) << shown.Error();
        Client::Ui::OldWindow* window = nullptr;
        for (auto& [id, candidate] : manager.windows)
            if (candidate.kind == Client::Ui::OldWindowKind::LetterReadWindow)
                window = &candidate;
        ASSERT_NE(window, nullptr);
        const Client::Ui::OldPortraitPart& part = window->letter.portrait;
        EXPECT_EQ(part.built, 1);
        EXPECT_EQ(part.portraitClass, 1);
        EXPECT_EQ(part.action, 0x7a);
        EXPECT_EQ(part.angle, 90.0f);
        EXPECT_FLOAT_EQ(part.zoom, 0.9f);
        EXPECT_EQ(part.baseZoom, part.zoom);
        const LetterPortrait& portrait = PortraitOf(*window);
        EXPECT_EQ(portrait.actor.characterClass, 1);
        EXPECT_TRUE(portrait.helper.alive);
        EXPECT_EQ(portrait.helper.type, 0xc6);
        EXPECT_EQ(portrait.actor.angles.x, -20.0f);

        const auto twinResources = twin->Resources();
        LetterPortrait& expected = twin->portrait;
        ASSERT_TRUE(Client::Messenger::BuildThePortrait(expected, twinResources).HasValue());
        ASSERT_TRUE(Client::Messenger::SetThePortraitClass(expected, 1, twinResources).HasValue());
        ASSERT_TRUE(Client::Messenger::DressThePortrait(expected, equipment, 0x7a, twinResources).HasValue());
        ASSERT_TRUE(Client::Messenger::ShowThePortraitAction(expected, 0x7a, 0, twinResources).HasValue());
        EXPECT_EQ(game->cloth.random.State(), twin->cloth.random.State());
    }
}

// 0x497970 in the old windows' step: with 0x78abaac at 1 it copies the hero again (with nothing different, no rand())
// and +0x360; the wheel brings it closer up to 1.1; the click grabs it and the next click turns it by how far the mouse
// moved; without a click, it lets go.
TEST_F(LetterPortraitWindowsTest, TheStepFollowsZoomsAndTurns)
{
    auto game = Load(2);
    const auto resources = game->Resources();
    Client::Ui::OldWindow* write = OpenTheWriteWindow(*game, resources);
    ASSERT_NE(write, nullptr);
    Client::Ui::OldPortraitPart& part = write->letter.portrait;
    Client::Ui::OldWindowManager& manager = game->world.Runtime().oldWindows;
    game->Hero().byte360 = 4;
    game->world.Runtime().ui.field78abaac = 1;
    const auto globals = Client::Hud::OldWindowGlobalsOf(game->world.Runtime());
    auto view = View(*game, resources);
    view.mouseX = part.base.x + part.base.width / 2;
    view.mouseY = part.base.y + part.base.height / 3;
    const CrtRandom before = game->cloth.random;
    globals.wheel = 20;
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(PortraitOf(*write).actor.byte360, 4);
    EXPECT_EQ(part.zoom, 1.1f);
    EXPECT_EQ(globals.wheel, 0);
    EXPECT_EQ(game->cloth.random.State(), before.State());

    globals.leftClick = true;
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.base.state, 2);
    EXPECT_EQ(globals.capture, part.base.id);
    view.mouseX += 7;
    globals.leftClick = true;
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.angle, 97.0f);
    globals.leftClick = false;
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(globals.capture, 0);

    // The right click goes back to the starting angle and zoom.
    globals.rightClick = true;
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(manager, globals, view).HasValue());
    EXPECT_EQ(part.angle, 90.0f);
    EXPECT_EQ(part.zoom, 1.0f);
}
} // namespace
