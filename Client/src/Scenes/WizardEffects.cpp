#include "Scenes/WizardEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SkillHits.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/TerrainHeight.h"
#include "World/WizardModelEffects.h"
#include "World/WorldTerrain.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
World::WizardEffectServices WizardEffectServicesOf(WorldState& world, Render::Models::CrtRandom& random,
                                                   const WizardSceneParts& parts)
{
    WorldRuntime& runtime = world.Runtime();
    std::function<double(float, float)> terrainHeight;
    const World::WorldTerrain* terrain = parts.terrain;
    if (terrain != nullptr && terrain->heights && terrain->attributes)
        terrainHeight = [terrain, fixed = parts.fixedHeight](float x, float y)
        { return World::TerrainHeightAt(*terrain->heights, *terrain->attributes, x, y, fixed); };
    return World::WizardEffectServices{
        .particles = parts.particles,
        .worldClock = parts.worldClock,
        .sprites = parts.sprites,
        .lighting = &world.DynamicLighting().Lighting(),
        .effects = &world.Map().effects,
        .terrainHeight = terrainHeight,
        .terrainAttributes = terrain != nullptr ? terrain->attributes.get() : nullptr,
        .map = &runtime.map,
        .cameraShake = parts.cameraShake,
        .siegeActive = runtime.siegeActive,
        .sound = parts.sound,
        .joint = [&world, &random,
                  joints = parts.joints](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created = CreateFaunaJoint(
                joints.empty() ? std::span<JointState>(world.DrawTail().joints) : joints, request, random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .ownerOf = [&world](const void* owner) -> std::optional<World::WizardOwnerView>
        {
            for (const ActorState& actor : world.Actors())
            {
                if (&actor != owner)
                    continue;
                return World::WizardOwnerView{.alive = actor.alive,
                                              .type = actor.type,
                                              .position = actor.position,
                                              .flags78 = actor.flags78,
                                              .action = actor.animation.action,
                                              .seen = actor.seen,
                                              .byte1bc = actor.byte1bc};
            }
            return std::nullopt;
        },
        .hero = &world.Actors()[runtime.hero],
        .skillHits = [&world, &random, send = parts.send](const World::ModelEffect& effect,
                                                          float radius) -> Core::Expected<void, std::string>
        {
            // 0x517a20(+0x89, la posición, `radius`, el byte +0x8c, +0x8a, 0).
            const SkillHitsRequest request{.skillIndex = effect.byte89,
                                           .point = effect.transform.position,
                                           .radius = radius,
                                           .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                           .key = effect.disturbanceState,
                                           .extra = 0};
            const auto hit = HitTheTargetsAround(request, world, random, send);
            if (!hit.HasValue())
                return Core::Unexpected(hit.Error());
            return {};
        },
        .ownRecordDepth = &runtime.ownRecordDepth,
        .ownRecordCount = &world.OwnCharacter().bytef04,
        .publishAction =
            [&world](std::uint16_t type, std::uint8_t action)
        {
            auto placement = world.ModelPlacement(type);
            placement.action = action;
            world.PlaceModel(type, placement);
        }};
}

World::WizardEffectServices WizardEffectServicesOf(const CharacterStepContext& context)
{
    return WizardEffectServicesOf(
        context.world, context.random,
        WizardSceneParts{.particles = context.particles,
                         .worldClock = context.worldClock,
                         .sprites = context.sprites,
                         .terrain = context.terrain != nullptr ? context.terrain : &context.world.Map().terrain,
                         .fixedHeight = context.fixedHeight,
                         .cameraShake = nullptr,
                         .sound = context.soundRequest,
                         .send = context.send,
                         .joints = context.joints});
}
} // namespace Client::Scenes
