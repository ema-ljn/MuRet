#include "Crt/CrtRandom.h"
#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Scenes/CastleWeaponEffects.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Network/Packet.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Protocol/Outgoing.h"
#include "Models/Cloth.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Scenes/CastleBattleReceive.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/SiegeStoneEffects.h"
#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/RecordingSink.h"
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <utility>
namespace
{
class WeaponModels final : public Client::Models::IModelLibrary
{
public:
    WeaponModels()
    {
        skeleton.actions.resize(200);
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton;
};
class CastleWeaponEffectsTest : public testing::Test
{
protected:
    std::unique_ptr<Client::Kernel::WorldState> world = std::make_unique<Client::Kernel::WorldState>();
    Client::Scenes::SelectionState selection{*world};
    Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom{}};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    WeaponModels models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .log = log};
    }
    void SetUp() override
    {
        world->Runtime().hero = 42;
        world->Actors()[42].type = 0x20a;
    }
};
TEST_F(CastleWeaponEffectsTest, ARemoteWeaponStillGivesItsProjectileToHeroAndConsumesOneDraw)
{
    auto& source = world->Actors()[7];
    source.alive = true;
    source.key = 0x0101;
    source.type = 0x19c;
    source.position = {100, 200, 300};
    ASSERT_TRUE(Client::Scenes::ApplyTheSiegeWeaponFire(0x0101, 1, 1, 20, 30, Resources()).HasValue());
    const auto& effect = world->Map().effects.Effects()[0];
    EXPECT_EQ(effect.type, Client::World::SiegeStoneEffect);
    EXPECT_EQ(effect.subtype, 1);
    EXPECT_EQ(effect.ownerContext, &world->Actors()[42]);
    EXPECT_EQ(effect.transform.position, (glm::vec3{100, 200, 500}));
    EXPECT_EQ(effect.siegeTarget, (glm::vec3{2000, 3000, 100}));
    EXPECT_EQ(effect.state, 1);
    EXPECT_EQ(effect.byte89, 1);
    EXPECT_EQ(source.animation.action, 1);
    EXPECT_EQ(world->Runtime().lastSoundId, 0x1ea);
    Client::Crt::CrtRandom expected(1);
    expected.Discard(1);
    EXPECT_EQ(cloth.random.State(), expected.State());
}
TEST_F(CastleWeaponEffectsTest, AFullPoolStillPlaysTheFiringSoundWithoutConstructorDraws)
{
    for (auto& effect : world->Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(Client::Scenes::ApplyTheSiegeWeaponFire(0, 2, 0, 0, 0, Resources()).HasValue());
    EXPECT_EQ(world->Runtime().lastSoundId, 0x1ea);
    EXPECT_EQ(cloth.random.State(), 1u);
}
TEST_F(CastleWeaponEffectsTest, ThePacketAimsTheIceProjectileAndKeepsBothKeyBytes)
{
    auto& source = world->Actors()[7];
    source.alive = true;
    source.key = 0x1234;
    source.type = 0x19c;
    source.position = {100, 200, 300};
    const auto wire = Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 10, 0xb7, 1, 1, 0x12, 0x34, 2, 20, 30}));
    const auto read = Client::Protocol::CastleSiege::ReadCastleBattlePacket(wire);
    ASSERT_TRUE(read.HasValue());
    const auto resources = Resources();
    const auto applied = Client::Scenes::ApplyCastleBattlePacket(
        read.Value(), {.world = *world, .random = cloth.random, .log = log, .entryResources = &resources});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    const auto& projectile = world->Map().effects.Effects()[0];
    ASSERT_TRUE(projectile.alive);
    EXPECT_EQ(projectile.type, Client::World::SiegeIceStoneEffect);
    EXPECT_EQ(projectile.subtype, 1);
    EXPECT_EQ(projectile.state, 0x12);
    EXPECT_EQ(projectile.byte89, 0x34);
    EXPECT_EQ(projectile.siegeTarget, (glm::vec3{2000, 3000, 100}));
    EXPECT_EQ(source.animation.action, 1);
    EXPECT_EQ(world->Runtime().lastSoundId, 0x1ea);
}
TEST_F(CastleWeaponEffectsTest, AnUnknownImpactKindStillCreatesItsWaveBeforeTheSound)
{
    ASSERT_TRUE(Client::Scenes::ApplyTheSiegeWeaponImpact(7, 20, 30, Resources()).HasValue());
    const auto& effect = world->Map().effects.Effects()[0];
    EXPECT_EQ(effect.type, Client::World::ShockWaveEffect);
    EXPECT_EQ(effect.subtype, 6);
    EXPECT_EQ(effect.life, 50);
    EXPECT_EQ(effect.transform.position, (glm::vec3{2000, 3000, 100}));
    EXPECT_EQ(effect.ownerContext, nullptr);
    EXPECT_EQ(world->Runtime().lastSoundId, 0x1ea);
    EXPECT_EQ(cloth.random.State(), 1u);
}
TEST_F(CastleWeaponEffectsTest, FenrirChoosesItsHandActionButSendsNativeAction5d)
{
    auto resources = Resources();
    std::vector<Client::Protocol::Outgoing> sent;
    resources.sendGamePacket = [&](auto packet) { sent.push_back(std::move(packet)); };
    auto& hero = world->Actors()[42];
    hero.pet.model = 0x1c37;
    hero.hands[0].model = 0xffff;
    hero.hands[1].model = 0xffff;
    hero.position = {100, 100, 0};
    hero.angles.z = 0;
    const auto services = Client::Scenes::SiegeStoneServicesOf(resources);
    ASSERT_TRUE(services.heroAction(hero.position, 350).HasValue());
    EXPECT_EQ(hero.animation.action, 0x66);
    ASSERT_EQ(sent.size(), 1u);
    const auto decoded = Client::Network::Xor32Decode(sent[0].packet);
    const auto bytes = decoded.Bytes();
    EXPECT_EQ(bytes[2], std::byte{0x18});
    EXPECT_EQ(bytes[4], std::byte{0x5d});
}
} // namespace
