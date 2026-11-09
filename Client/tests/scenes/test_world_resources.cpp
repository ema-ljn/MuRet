#include "Scenes/WorldState.h"
#include "World/WorldObjects.h"
#include "World/WorldObjectStepError.h"
#include "World/WorldObjectStepContext.h"
#include "World/WorldObjectStepHook.h"
#include "World/WorldObjectDrawingModel.h"
#include "World/TerrainLighting.h"
#include "World/IWorldObjectStepServices.h"
#include "World/IWorldObjectStepModels.h"
#include "Scenes/WorldObjectRenderState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/CharacterDrawing.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Cloth.h"
#include "Data/Models/Model.h"
#include "Core/Expected.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldResources.h"
#include "World/ModelEffects.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <type_traits>
#include <memory>
#include <functional>
#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace
{
using MetadataView = Client::World::ModelEffectAnimationBinding;
static_assert(!std::is_default_constructible_v<MetadataView>);
static_assert(std::is_constructible_v<std::reference_wrapper<std::uint8_t>, std::uint8_t&>);
static_assert(!std::is_constructible_v<std::reference_wrapper<std::uint8_t>, std::uint8_t>);
static_assert(
    !std::is_assignable_v<decltype(std::declval<MetadataView&>().action)&, std::reference_wrapper<std::uint8_t>>);
static_assert(!std::is_move_constructible_v<Client::Scenes::WorldResources>);

// El caso135/mapa0 no emite servicios; cualquier llamada sería un error de la prueba.
class RejectMetadataServices final : public Client::World::IWorldObjectStepServices
{
public:
    using Result = Client::Core::Expected<void, Client::World::WorldObjectStepError>;
    Result CreateParticle(const Client::World::WorldObjectParticleRequest&, Client::Render::Models::CrtRandom&) override
    {
        ADD_FAILURE() << "partícula inesperada";
        return Client::Core::Unexpected(Client::World::WorldObjectStepError::UnsupportedParticle);
    }
    Result CreateModelEffect(const Client::World::WorldObjectModelEffectRequest&, Client::World::WorldObject&,
                             Client::Render::Models::CrtRandom&) override
    {
        ADD_FAILURE() << "efecto inesperado";
        return Client::Core::Unexpected(Client::World::WorldObjectStepError::UnsupportedModelEffect);
    }
    Result PlaySound(std::int32_t, std::int32_t, std::int32_t) override
    {
        ADD_FAILURE() << "sonido inesperado";
        return Client::Core::Unexpected(Client::World::WorldObjectStepError::UnsupportedSound);
    }
    Result PlayObjectSound(std::int32_t, Client::World::WorldObject&, std::int32_t) override
    {
        ADD_FAILURE() << "sonido de objeto inesperado";
        return Client::Core::Unexpected(Client::World::WorldObjectStepError::UnsupportedSound);
    }
    Client::Core::Expected<std::uint32_t, Client::World::WorldObjectStepError>
    HandleMap(Client::World::WorldObjectStepHook, Client::World::WorldObject&,
              Client::Render::Models::CrtRandom&) override
    {
        ADD_FAILURE() << "handler inesperado";
        return Client::Core::Unexpected(Client::World::WorldObjectStepError::UnsupportedMapHandler);
    }
};

// Recurso sintético declarado: prueba lifetime/alias, no acredita el BMD nativo del tipo135.
void SetMetadataModel(Client::Scenes::SingleModelLibrary& library, std::int16_t keys)
{
    Client::Render::Models::Skeleton skeleton;
    skeleton.actions = {{.keyCount = 1}, {.keyCount = keys}};
    library.Set(0x135, Client::Data::Models::Model{}, std::move(skeleton), {});
}

class WorldResourcesTest : public Client::Tests::GoldenDataTest
{
};

TEST_F(WorldResourcesTest, StepDrawAndEffectsBorrowTheSameSelectorIncludingFfAndFallback)
{
    Client::Tests::FakeTextureStore textures;
    Client::Scenes::WorldResources resources(textures);
    SetMetadataModel(resources.loginLogo, 3);
    auto& state = resources.objectRenderState;
    const auto draw = state.Model(0x135, resources.loginLogo);
    const auto animation = state.EffectAnimation(0x135, resources.loginLogo);
    ASSERT_TRUE(draw.HasValue());
    ASSERT_TRUE(animation.HasValue());
    auto& binding = animation.Value().get();
    auto& step = state.Step(0x135);
    EXPECT_EQ(&draw.Value().get().step, &step);
    EXPECT_EQ(&binding.action.get(), &step.action);
    step.flag88 = 0x81;
    EXPECT_EQ(draw.Value().get().step.flag88, 0x81);
    Client::Render::Models::CrtRandom random(1);
    std::int32_t map = 0, tile = 0, editor = 0, value = 0;
    const Client::World::ObjectScene scene = Client::World::ObjectScene::Other;
    const float time = 0.0f;
    const glm::vec3 hero{};
    const std::uint32_t global = 0;
    const std::uint8_t heroAction = 0;
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    RejectMetadataServices services;
    Client::World::WorldObjectStepContext context{
        map,       scene, time,   tile,      hero,     editor,
        global,    value, random, *lighting, services, std::ref<Client::World::IWorldObjectStepModels>(state),
        heroAction};
    Client::World::WorldObject object;
    object.type = 0x135;
    object.animation.action = 1;
    object.speed = 0.0f;
    ASSERT_TRUE(Client::World::StepWorldObject(object, context).HasValue());
    EXPECT_EQ(binding.action.get(), 1);
    EXPECT_EQ(draw.Value().get().step.action, 1);
    EXPECT_EQ(random.State(), 1u);
    EXPECT_TRUE(state.Bones().empty()); // Lookup/Step no prepara pose por iniciativa propia.

    std::array<Client::Render::Models::ActionInfo, 2> replacement{};
    replacement[1].keyCount = -7;
    std::size_t advances = 0;
    Client::World::ModelEffectStepBindings effects;
    effects.readClock = [] { return 0.0f; };
    effects.animationOf = [&](std::int16_t type)
    {
        EXPECT_EQ(type, 0x135);
        return &binding;
    };
    effects.advance = [&](auto& liveBinding, auto&, float)
    {
        ++advances;
        EXPECT_EQ(&liveBinding.action.get(), &step.action);
        EXPECT_EQ(step.action, 0);
        step.action = 1;
        liveBinding.actions = replacement;
        return false;
    };
    Client::World::ModelEffect effect;
    effect.type = 0x135;
    effect.alive = 1;
    effect.animation.action = 0;
    effect.life = 99;
    ASSERT_TRUE(Client::World::StepConstructedModelEffect(effect, random, effects).HasValue());
    EXPECT_EQ(advances, 1u);
    EXPECT_EQ(effect.animation.frame, -7.0f);
    EXPECT_EQ(effect.animation.action, 0xff);
    EXPECT_EQ(step.action, 1); // El fallback no recopiaff a M en esta misma cola.
    EXPECT_EQ(draw.Value().get().step.action, 1);
    EXPECT_EQ(random.State(), 0x0029e2c0u);
    ASSERT_TRUE(Client::World::StepConstructedModelEffect(effect, random, effects).HasValue());
    EXPECT_EQ(advances, 1u);
    EXPECT_EQ(step.action, 0xff); // El siguiente step publicaff ANTES del early return.
    EXPECT_EQ(random.State(), 0xc823f683u);
}

TEST_F(WorldResourcesTest, ReleaseInvalidatesModelLoansAndReloadKeepsTheCanonicalNode)
{
    Client::Tests::FakeTextureStore textures;
    Client::Scenes::WorldResources resources(textures);
    SetMetadataModel(resources.loginLogo, 3);
    auto& state = resources.objectRenderState;
    const auto view = state.EffectAnimation(0x135, resources.loginLogo);
    const auto draw = state.Model(0x135, resources.loginLogo);
    ASSERT_TRUE(view.HasValue());
    ASSERT_TRUE(draw.HasValue());
    auto* stable = &view.Value().get();
    auto& step = state.Step(0x135);
    step.action = 7;
    step.flag88 = 0x91;
    state.Frame(0x135) = 37;
    EXPECT_EQ(stable->actions[1].keyCount, 3);
    resources.Release();
    EXPECT_TRUE(stable->actions.empty());
    EXPECT_EQ(step.skeleton, nullptr);
    EXPECT_TRUE(draw.Value().get().actionKeys.empty());
    EXPECT_EQ(step.action, 7);
    EXPECT_EQ(step.flag88, 0x91);
    EXPECT_EQ(state.Frame(0x135), 37);
    EXPECT_EQ(state.Current(0x135), nullptr);
    EXPECT_FALSE(state.EffectAnimation(0x135, resources.loginLogo).HasValue());
    SetMetadataModel(resources.loginLogo, 9);
    const auto reloaded = state.EffectAnimation(0x135, resources.loginLogo);
    ASSERT_TRUE(reloaded.HasValue());
    EXPECT_EQ(&reloaded.Value().get(), stable);
    EXPECT_EQ(&reloaded.Value().get().action.get(), &step.action);
    ASSERT_EQ(stable->actions.size(), 2u);
    EXPECT_EQ(stable->actions[1].keyCount, 9);
    EXPECT_EQ(stable->actions.data(), resources.loginLogo.SkeletonOf(0x135)->actions.data());
    EXPECT_EQ(step.action, 7);
    EXPECT_EQ(step.flag88, 0x91);
    EXPECT_TRUE(state.Bones().empty());
}

TEST_F(WorldResourcesTest, LeavingTheOldLoginDoesNotReleaseTheReplacementWorldSlots)
{
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Scenes::WorldState world(textures);
    Client::Scenes::SelectionState selection(world);
    Client::Scenes::GameModels models;
    const Client::Scenes::GameData data;
    const Client::Scenes::SceneContext context{
        .dataRoot = m_dataRoot, .textures = textures, .log = log, .selection = &selection, .world = &world};
    Client::Scenes::LoginScene oldScene(context, data, models, nullptr, "test", 12345);
    const auto entered = oldScene.Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    auto* resources = world.Resources();
    ASSERT_NE(resources, nullptr);
    ASSERT_EQ(resources->terrain.LoadedMap(), 55);
    resources->Release();
    const auto uploadsBeforeReplacing = textures.loaded.size();
    const auto worldModels = resources->models.Load(m_dataRoot, 0, textures, log, nullptr);
    ASSERT_TRUE(worldModels.HasValue()) << worldModels.Error();
    ASSERT_GT(resources->models.Count(), 0u);
    std::vector<std::string> skipped;
    const auto loaded = resources->terrain.Load(m_dataRoot, 0, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(textures.Find(Client::Render::TextureSlot{0x23}).has_value());
    const auto releasesBeforeLeaving = textures.released;
    const auto effect = resources->effects.Create(Client::World::DragonEffect, {}, {}, {}, 1.0f);
    ASSERT_TRUE(effect.has_value());
    oldScene.Leave();
    EXPECT_TRUE(resources->effects.Effects()[*effect].alive);
    for (std::size_t i = uploadsBeforeReplacing; i < textures.loaded.size(); ++i)
        EXPECT_TRUE(textures.Find(Client::Render::TextureSlot{textures.loaded[i]}).has_value()) << textures.loaded[i];
    EXPECT_EQ(resources->terrain.LoadedMap(), 0);
    EXPECT_TRUE(textures.Find(Client::Render::TextureSlot{0x23}).has_value());
    EXPECT_EQ(selection.Terrain(), resources->terrain.Terrain());
    EXPECT_EQ(selection.Map(), 0);
    for (std::size_t i = releasesBeforeLeaving.size(); i < textures.released.size(); ++i)
    {
        EXPECT_NE(textures.released[i], 0x23);
        EXPECT_NE(textures.released[i], 0x122);
    }
}

TEST_F(WorldResourcesTest, MapReleasePreservesTheFivePlanesFromThePreviousScene)
{
    Client::Tests::FakeTextureStore textures;
    Client::Scenes::WorldState world(textures);
    auto& resources = *world.Resources();
    resources.modelEffectPlanes.normals[3] = {7.0f, 11.0f, 13.0f};
    resources.modelEffectPlanes.distances[3] = 19.0f;
    resources.Release();
    EXPECT_EQ(resources.modelEffectPlanes.normals[3], glm::vec3(7.0f, 11.0f, 13.0f));
    EXPECT_EQ(resources.modelEffectPlanes.distances[3], 19.0f);
}

TEST_F(WorldResourcesTest, PolygonLevelSurvivesMapReleaseAndAdoptsOnlyAfterTheTruncatedCameraComparison)
{
    Client::Tests::FakeTextureStore textures;
    Client::Scenes::WorldResources resources(textures);
    resources.terrainPolygonState.effectiveLevel = 2;
    Client::World::GameCamera camera;
    camera.distance = 2000.0f;
    camera.targetDistance = 1000.0f;
    camera.level = 1;
    const Client::World::TerrainPolygonInput input{.scene = 5, .map = 55, .cameraYaw = 0.0f};
    ASSERT_TRUE(resources.UpdateTerrainPolygon(input, camera).HasValue());
    EXPECT_EQ(resources.terrainPolygonState.effectiveLevel, 2);
    EXPECT_FLOAT_EQ(resources.terrainPolygon.x[0], -14.3f);
    EXPECT_FLOAT_EQ(resources.terrainPolygon.y[0], 15.9f);
    resources.Release();
    EXPECT_EQ(resources.terrainPolygonState.effectiveLevel, 2);
    camera.distance = 1000.9f;
    camera.targetDistance = 1000.1f;
    ASSERT_TRUE(resources.UpdateTerrainPolygon(input, camera).HasValue());
    EXPECT_EQ(resources.terrainPolygonState.effectiveLevel, 1);
    EXPECT_FLOAT_EQ(resources.terrainPolygon.x[0], -13.2f);
    EXPECT_FLOAT_EQ(resources.terrainPolygon.y[0], 14.31f);
}

TEST_F(WorldResourcesTest, TheDurableOwnerReleasesLoadedMapSlotsOnDestruction)
{
    Client::Tests::FakeTextureStore textures;
    {
        Client::Scenes::WorldState world(textures);
        std::vector<std::string> skipped;
        const auto loaded = world.Resources()->terrain.Load(m_dataRoot, 0, skipped);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        ASSERT_GT(textures.Size(), 0u);
    }
    EXPECT_EQ(textures.Size(), 0u);
}
} // namespace
