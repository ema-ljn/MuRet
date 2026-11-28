#include "Effects/WizardEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Tables/PlayerModels.h"
#include "Tables/PlayerActions.h"
#include "Models/Animation.h"
#include "Models/Skeleton.h"
#include "Actors/ActorShock.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/CharacterStep.h"
#include "Kernel/DrawTailState.h"
#include "Actors/SkillHits.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/BlastModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainHeight.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Assets/FaunaModels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Effects
{
Effects::WizardEffectServices WizardEffectServicesOf(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                     const WizardSceneParts& parts)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    std::function<double(float, float)> terrainHeight;
    const Terrain::WorldTerrain* terrain = parts.terrain;
    if (terrain != nullptr && terrain->heights && terrain->attributes)
        terrainHeight = [terrain, fixed = parts.fixedHeight](float x, float y)
        { return Terrain::TerrainHeightAt(*terrain->heights, *terrain->attributes, x, y, fixed); };
    return Effects::WizardEffectServices{
        .particles = parts.particles,
        .worldClock = parts.worldClock,
        .sprites = parts.sprites,
        .lighting = parts.dynamicLighting,
        .effects = &world.Map().effects,
        .terrainHeight = terrainHeight,
        .terrainAttributes = terrain != nullptr ? terrain->attributes.get() : nullptr,
        .map = &runtime.map,
        .cameraShake = parts.cameraShake,
        .siegeActive = runtime.castleSiege.siegeActive,
        .sound = parts.sound,
        .joint = [&world, &random,
                  joints = parts.joints](const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created = world.CreateJoint(
                joints.empty() ? std::span<Kernel::JointState>(world.DrawTail().joints) : joints, request, random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .ownerOf = [&world](const void* owner) -> std::optional<Effects::WizardOwnerView>
        {
            for (const Kernel::ActorState& actor : world.Actors())
            {
                if (&actor != owner)
                    continue;
                return Effects::WizardOwnerView{.alive = actor.alive,
                                                .type = actor.type,
                                                .position = actor.position,
                                                .flags78 = actor.flags78,
                                                .action = actor.animation.action,
                                                .seen = actor.seen,
                                                .byte1bc = actor.byte1bc};
            }
            return std::nullopt;
        },
        .hero = &world.Hero(),
        .skillHits = [&world, &random, terrain, send = parts.send](const Effects::ModelEffect& effect,
                                                                   float radius) -> Core::Expected<void, std::string>
        {
            // 0x517a20(+0x89, the position, `radius`, the byte +0x8c, +0x8a, 0).
            const Actors::SkillHitsRequest request{.skillIndex = effect.byte89,
                                                   .point = effect.transform.position,
                                                   .radius = radius,
                                                   .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                                   .key = effect.disturbanceState,
                                                   .extra = 0};
            const auto hit =
                Actors::HitTheTargetsAround(request, world, Terrain::TerrainOrEmpty(terrain), random, send);
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
        },
        .otherEffect = [&world, &random, sound = parts.sound](
                           const Kernel::ModelEffectArguments& arguments) -> Core::Expected<void, std::string>
        {
            // 0x524d38: the 0xf6/2 (Effects/BlastModelEffects) with the meteor that drops it as owner (+0x108), which
            // must be an effect of the table. Its step (0x521324) does not read it: it only adds 0.04f to the scale
            // (+0xc).
            const std::span<Effects::ModelEffect> table = world.Map().effects.Effects();
            const auto owner = std::ranges::find_if(table, [&arguments](const Effects::ModelEffect& effect)
                                                    { return &effect == arguments.owner; });
            if (owner == table.end())
                return Core::Unexpected(
                    std::format("0x518cf0: el dueño del efecto {:#x}/{} no es un efecto de la tabla", arguments.type,
                                arguments.subtype));
            Kernel::WorldRuntime& state = world.Runtime();
            const Effects::BlastEffectServices services{.ownRecordDepth = &state.ownRecordDepth,
                                                        .ownRecordCount = &world.OwnCharacter().bytef04,
                                                        .hero = &world.Hero(),
                                                        .sound = sound};
            const auto created = Effects::CreateBlastModelEffect(
                world.Map().effects, arguments, Effects::ModelEffectOwner{.effect = &*owner}, random, services);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .pushAround = [&world, hero = runtime.hero, models = parts.models,
                       faunaModels = parts.faunaModels](float x, float y) -> Core::Expected<void, std::string>
        {
            constexpr std::size_t PooledActors = 400;                      // 0x524b1b: 0x69780 / 0x438.
            constexpr double PushRadius = 200.0;                           // 0x6b8928
            constexpr std::uint16_t FenrirPet = 0x1c37;                    // 0x524aef: the 0x20a's +0x318 with Fenrir
            constexpr std::uint8_t PlayerPushAction = Tables::ShockAction; // 0x524b03
            constexpr std::uint8_t MonsterPushAction = 5;                  // 0x524b0a
            const auto actors = world.Actors().first<PooledActors>();
            for (std::size_t index = 0; index < actors.size(); ++index)
            {
                Kernel::ActorState& actor = actors[index];
                // 0x524a93..0x524aa9: dx and dy are stored in float before looking at the character.
                const auto dx = static_cast<float>(static_cast<double>(x) - static_cast<double>(actor.position.x));
                const auto dy = static_cast<float>(static_cast<double>(y) - static_cast<double>(actor.position.y));
                if (!actor.alive || !actor.seen || index == hero || actor.dying != 0)
                    continue;
                const double distance = std::sqrt(static_cast<double>(dy) * static_cast<double>(dy) +
                                                  static_cast<double>(dx) * static_cast<double>(dx));
                if (distance > PushRadius)
                    continue;
                // 0x4ea030 reads the actions of 0x5769ad8 + type · 0xe0: first the map's models (FaunaModels) and then
                // the game's, as MonsterSkeletonOf. A model that is not there is the record with no actions.
                const Models::Skeleton* skeleton =
                    Assets::FaunaModelsOrEmpty(faunaModels).SkeletonOf(Models::ModelId{actor.type});
                if (skeleton == nullptr && models != nullptr)
                    skeleton = models->SkeletonOf(Models::ModelId{actor.type});
                if (actor.type != Tables::PlayerSkeletonModel)
                {
                    // 0x524b0a: 0x4ea030(personaje, 5).
                    if (skeleton != nullptr)
                        Models::SetAction(actor.animation, *skeleton, MonsterPushAction);
                    continue;
                }
                if (skeleton == nullptr)
                    return Core::Unexpected(std::string("0x524afc: falta Player.bmd para el personaje que empuja el "
                                                        "0xc2/4"));
                // 0x524afa: 0x4e9cd0(character, character), Fenrir's hand hit action; otherwise, 0xc2.
                Models::SetAction(actor.animation, *skeleton,
                                  actor.pet.model == FenrirPet ? Actors::FenrirHitActionOf(actor) : PlayerPushAction);
            }
            return {};
        }};
}

Effects::WizardEffectServices WizardEffectServicesOf(const Actors::CharacterStepContext& context)
{
    return WizardEffectServicesOf(context.world, context.random,
                                  WizardSceneParts{.particles = context.particles,
                                                   .worldClock = context.worldClock,
                                                   .sprites = context.sprites,
                                                   .terrain = &Terrain::TerrainOrEmpty(context.terrain),
                                                   .fixedHeight = context.fixedHeight,
                                                   .cameraShake = nullptr,
                                                   .sound = context.soundRequest,
                                                   .send = context.send,
                                                   .joints = context.joints,
                                                   .models = context.models,
                                                   .dynamicLighting = context.dynamicLighting,
                                                   .faunaModels = context.faunaModels});
}

Kernel::ModelEffectArguments WizardArguments(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                             const glm::vec3& light, std::int32_t subtype, void* owner,
                                             std::uint16_t key)
{
    return Kernel::ModelEffectArguments{.type = type,
                                        .position = position,
                                        .angles = angles,
                                        .light = light,
                                        .subtype = subtype,
                                        .owner = owner,
                                        .key = key};
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateWizardEffect(const Actors::CharacterStepContext& context, const Kernel::ModelEffectArguments& arguments,
                   glm::vec3* callerLight)
{
    return Effects::CreateWizardModelEffect(context.world.Map().effects, arguments, context.random,
                                            WizardEffectServicesOf(context), callerLight);
}
} // namespace Client::Effects
