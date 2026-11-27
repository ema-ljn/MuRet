#include "Actors/CharacterStep.h"
#include "Maps/MapId.h"
#include "Scenes/SiegeBattleUi.h"
#include "Protocol/Pets/PetOrders.h"

#include "Actors/ActorPickingBox.h"
#include "Math/OriginalRotation.h"
#include "World/Particles.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Math/FloatConstants.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Actors/ActorActions.h"
#include "Actors/ActorCooldownServices.h"
#include "Actors/ActorCreation.h"
#include "Actors/ActorMotion.h"
#include "Actors/ActorRemains.h"
#include "Actors/ActorWalk.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Scenes/ActorCombat.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorDying.h"
#include "Kernel/ActorState.h"
#include "Kernel/ActorSpirit.h"
#include "Scenes/AttackSkillCases.h"
#include "Scenes/AttackStep.h"
#include "Kernel/CharacterHead.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/CrywolfContract.h"
#include "Scenes/DarkSpirit.h"
#include "Terrain/DynamicWorldLight.h"
#include "Assets/FaunaModels.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Actors/Drawing/GameRibbonCreation.h"
#include "Scenes/HealthBars.h"
#include "Player/ItemSetOptionList.h"
#include "Scenes/ModelBody.h"
#include "Scenes/ModelEffects.h"
#include "Scenes/MonsterHelpers.h"
#include "Scenes/MonsterRibbonTrails.h"
#include "Scenes/MonsterStepCases.h"
#include "Scenes/NpcChatter.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnStatHelpers.h"
#include "Scenes/PartyActors.h"
#include "Scenes/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Actors/Drawing/WeaponTrail.h"
#include "Math/Ftol.h"
#include "World/ObjectAlpha.h"
#include "World/IFishStepServices.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "Pathfinding/TerrainWalls.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Actors
{
namespace
{
// 0x50919a and 0x5091c8: the two loops of 0x508f00 walk 400 records (0x69780 / 0x438); not record 401.
constexpr std::size_t SteppedCharacters = 400;
// 0x412162: on map 30 (0x1e), 0x412140 builds the siege panel.
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;

// 0x4fd603: the type that passes its name and level to 0x4381f0.
constexpr std::uint16_t NamedType = 0xc5;
// 0x4fbe2e..0x4fbe41: 0x4fbe10 runs only on maps 18 to 23.
constexpr std::int32_t FirstCellReportMap = 18;
constexpr std::int32_t LastCellReportMap = 23;
// 0x4fb910 and 0x505425: the type that falls and turns in 0x4fb380 and that 0x52f360 then follows.
constexpr std::uint16_t FallingType = 0xf1;
// 0x4f9580 with the 0x20a: times 1.5 (0x6b8b94) on actions 3 and 4 with +0x3e3; times 0.5 (0x6b882c) on 0x3f
// past frame 6 (0x6b87f4) and on 0x44; times 0.1f (0x6b8a34) on 0x4d, 0x4f and 0x5a past 5.5
// (0x6b8a40); in class 4, on 0x94, 0x51 and 0x5b between frame 1 (0x6b87d4) and 3 (0x6b86b8): times 0.125
// (0x6ba170) with the character in the party (0x59e680) and times 0.5 (0x6b882c) otherwise.
constexpr float FasterFactor = 1.5f;
constexpr float HalfFactor = 0.5f;
constexpr float TenthFactor = 0.1f;
constexpr std::uint8_t FirstFasterAction = 3;
constexpr std::uint8_t LastFasterAction = 4;
constexpr std::uint8_t SlowedAction = 0x3f;
constexpr float SlowedFrame = 6.0f;
constexpr std::array<std::uint8_t, 3> ParticleActions{0x4d, 0x4f, 0x5a};
constexpr float ParticleFrame = 5.5f;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ClassFour = 4;
constexpr std::array<std::uint8_t, 3> ClassFourActions{0x94, 0x51, 0x5b};
constexpr float ClassFourFirstFrame = 1.0f;
constexpr float ClassFourLastFrame = 3.0f;
constexpr float PartyClassFourFactor = 0.125f;
constexpr std::uint8_t HalvedAction = 0x44;
// 0x4f973a: with +0x78 & 0x1000 the animation does not advance and 0x4f9580 returns 0, "finished".
constexpr std::uint32_t FrozenFlag = 0x1000;
// 0x4fcb10: what it does with the action that finished.
constexpr std::uint8_t FirstMarkAction = 0xc3;
constexpr std::uint8_t SecondMarkAction = 0xc4;
constexpr std::uint8_t FirstStoppedAfter = 0x0d;
constexpr std::uint8_t FirstSpecialStop = 0xbd;
constexpr std::uint8_t LastSpecialStop = 0xbf;
constexpr std::uint8_t FirstCombatStop = 0x54;
constexpr std::uint8_t LastCombatStop = 0x55;
constexpr std::uint8_t FirstKeptAction = 0x22;
constexpr std::uint8_t LastKeptAction = 0xc2;
constexpr std::array<std::uint8_t, 7> KeptActions{0x8b, 0x8c, 0x46, 0x47, 0x44, 0x49, 0x4b};
constexpr std::uint8_t FirstKeptRange = 0x6a;
constexpr std::uint8_t LastKeptRange = 0x75;
// 0x4fceea..0x4fcf04: before frame 3 (0x6b86b8), except on 0x8d, 0x4fcee0 leaves without a trail.
constexpr float TrailFrame = 3.0f;
constexpr std::uint8_t AlwaysTrailedAction = 0x8d;

// 0x4fc57e, 0x4fc5b0 and 0x4fc606: the bit of +0x3c that 0x4fc450 turns off with each WORD at 0.
constexpr std::uint8_t Word3eBit = 0x01;
constexpr std::uint8_t Word40Bit = 0x02;
constexpr std::uint8_t Word42Bit = 0x08;

// 0x5068b9..0x5068d4: with +0x78 & 0x20 (or & 0x1000, FrozenFlag) the head does not move.
constexpr std::uint32_t StillHeadFlag = 0x20;
// 0x506acf: with +0x375 at 6 or more (unsigned), the hands give no light.
constexpr std::uint8_t FirstPkWithoutLights = 6;
// 0x506aec..0x506b42: the hand models that ask 0x5f9780 for a light, with its color (the three DWORDs
// of [ebp−0x1c]) and its radius. 0x21e (0x506af7..0x506b0c): (0.8, 0.64, 0.4) with 3. 0x225, 0xa24,
// 0xc1c and 0x61f (0x506b2d..0x506b42): (0.64, 0.4, 0.24) with 2. The 0.64 and 0.24 are one ulp above the
// nearest floats (0x3f23d70a and 0x3e75c28f): they go by their bits.
struct HandLight
{
    std::uint16_t model;
    glm::vec3 color;
    std::int32_t radius;
};
constexpr glm::vec3 BrightHandLight{std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f23d70bu),
                                    std::bit_cast<float>(0x3ecccccdu)};
constexpr glm::vec3 DimHandLight{std::bit_cast<float>(0x3f23d70bu), std::bit_cast<float>(0x3ecccccdu),
                                 std::bit_cast<float>(0x3e75c290u)};
constexpr std::array<HandLight, 5> HandLights{
    HandLight{.model = 0x21e, .color = BrightHandLight, .radius = 3},
    HandLight{.model = 0x225, .color = DimHandLight, .radius = 2},
    HandLight{.model = 0xa24, .color = DimHandLight, .radius = 2},
    HandLight{.model = 0xc1c, .color = DimHandLight, .radius = 2},
    HandLight{.model = 0x61f, .color = DimHandLight, .radius = 2},
};
// 0x506d01 and 0x506d8b: the maps of the 0x20a case with effects.
constexpr std::int32_t ClockEffectMap = 7;
constexpr std::int32_t RandomEffectMap = 9;
// 0x506e5d..0x506e63 and 0x506f62: the 0x20a actions with effects.
constexpr std::uint8_t FirstEffectAction = 0x44;
constexpr std::uint8_t SecondEffectAction = 0x45;
constexpr std::uint8_t BoneEffectAction = 0x96;
// 0x5086d6..0x508712: in Kalima (maps 24 to 29 and 36), the 0x20a actions that release 0x6f4.
constexpr std::array<std::uint8_t, 7> KalimaParticleActions{0x20, 0x21, 0x1d, 0x15, 0x1e, 0x1f, 0x4b};
using Maps::IsKalimaMap;

std::string MapText(std::int32_t map)
{
    return "mapa " + std::to_string(map);
}

template <std::size_t N> bool IsOneOf(const std::array<std::uint8_t, N>& actions, std::uint8_t action)
{
    return std::ranges::find(actions, action) != actions.end();
}

void ClearBit(std::uint8_t& bits, std::uint8_t bit)
{
    bits = static_cast<std::uint8_t>(bits & ~bit);
}

// The model of 0x5769ad8 + type · 0xe0 that 0x4f9580, 0x4fcbb0 and 0x505de0 read: Player.bmd, the loose NewFace or
// that of the monsters and NPCs (WorldState::FaunaResources, with the map's library behind it; 08, P-C176).
// A monster's or an NPC's record always exists: that of a model that was not loaded (a .bmd missing in Npc or
// Object35, which 0x60da30 skips, or a type without a load case) is at 0, without actions (+0x26), and 0x4ea030,
// 0x4f9580 and 0x505de0 take it as an empty model.
const Models::Skeleton* ModelSkeletonOf(const Kernel::ActorState& actor, const CharacterStepContext& context)
{
    static const Models::Skeleton ZeroRecord;
    if (actor.type == Tables::PlayerSkeletonModel)
        return context.playerSkeleton;
    if (IsNewFace(actor.type))
        return context.newFaceSkeleton;
    const Models::Skeleton* loaded = context.world.FaunaResources().SkeletonOf(actor.type);
    return loaded != nullptr ? loaded : &ZeroRecord;
}

// 0x5f93c0(x, y) over the step's terrain, with the fixed height of context; empty without the heights or without
// the TerrainWall.
std::function<double(float, float)> TerrainHeightOf(const CharacterStepContext& context)
{
    const Terrain::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return {};
    return [&terrain, fixedHeight = context.fixedHeight](float x, float y)
    { return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, fixedHeight); };
}

// 0x508ba0(c, c) from 0x508ed8 with anyone but the published hero ([0x743a170]; with it, it leaves at 0x508bad).
Core::Expected<void, std::string> StepOtherCharacter(Kernel::ActorState& actor, std::size_t slot,
                                                     const CharacterStepContext& context)
{
    const bool player = actor.type == Tables::PlayerSkeletonModel;
    const Models::Skeleton* own = ModelSkeletonOf(actor, context);
    if (actor.movement != 0 && own == nullptr)
    {
        return Core::Unexpected(std::format("0x4f37c0: falta el esqueleto del personaje del lugar {}", slot));
    }
    const Terrain::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    if (!terrain.attributes)
    {
        return Core::Unexpected(std::string("0x508ba0: falta el TerrainWall para reintentar el camino"));
    }
    const Models::Skeleton empty;
    const Models::Skeleton& skeleton = context.playerSkeleton != nullptr ? *context.playerSkeleton : empty;
    PlayerActionSpeeds& speeds = context.actionSpeeds != nullptr ? *context.actionSpeeds : context.world.ActionSpeeds();
    const std::span<const std::int16_t> modelSounds = context.world.FaunaResources().SoundActions(actor.type);
    const ActorWalkContext walk{
        {context.world.Runtime(), context.scene, skeleton, context.items, context.random, &terrain.attributes->cells},
        speeds,
        false,
        player ? nullptr : own,
        modelSounds};
    ActorMotionContext motion{context.world.Runtime(), context.random, {}, Scenes::CrywolfAltarGoal(context.world)};
    if (terrain.heights)
    {
        motion.height = [&](float x, float y)
        { return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, context.fixedHeight); };
    }
    return StepOtherActorMotion(actor, walk, motion, terrain.attributes->cells);
}

// 0x4fc450(c) from 0x4fd6fe, with the published hero ([0x743a170]; with another it leaves at 0x4fc464): the three
// WORDs of [0x7681eb0], with the own record open (Q-C246).
Core::Expected<void, std::string> StepTheOwnTimers(const CharacterStepContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    Player::ItemSetOptionList& setOptions = world.ItemSets().list;
    // The cursor's item, with whatever is in its globals (HeldItemOf).
    const Player::OwnEquipmentOverride held = Player::HeldItemOf(runtime);
    // 0x4fc46a..0x4fc556: opens the own record inline; 0x4fc617..0x4fc767 closes it inline, with the
    // 3,848 rand() of 0x40b4d0 if the count stays at 0.
    const Models::LogicalAccess access(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    // 0x4fc55b..0x4fc58a: +0x3e drops if positive and, at 0, turns off bit 0x01 of +0x3c and calls 0x54fe10.
    if (own.word3e > 0)
    {
        --own.word3e;
    }
    if (own.word3e == 0)
    {
        ClearBit(own.speedAdjustBits, Word3eBit);
        if (auto updated = Player::UpdateOwnAttackAndMagicSpeeds(own, held, runtime.ownRecordDepth, context.random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    // 0x4fc594..0x4fc5e0: +0x40, bit 0x02, the list's 16 marks ([0x6eed3c] + 0x937c) at 0, 0x54efe0
    // and 0x54f9b0.
    if (own.word40 > 0)
    {
        --own.word40;
    }
    if (own.word40 == 0)
    {
        ClearBit(own.speedAdjustBits, Word40Bit);
        setOptions.used = {};
        if (auto updated = Player::UpdateOwnWords50To56(own, held, context.items, &setOptions); !updated.HasValue())
        {
            return updated;
        }
        if (auto updated = Player::UpdateOwnWords5aAnd5c(own, held, context.items, &setOptions); !updated.HasValue())
        {
            return updated;
        }
    }
    // 0x4fc5ea..0x4fc612: +0x42, bit 0x08 and 0x54fe10 again.
    if (own.word42 > 0)
    {
        --own.word42;
    }
    if (own.word42 == 0)
    {
        ClearBit(own.speedAdjustBits, Word42Bit);
        if (auto updated = Player::UpdateOwnAttackAndMagicSpeeds(own, held, runtime.ownRecordDepth, context.random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    return {};
}

// 0x4f9580(c, c): the action's speed, its factors and 0x4ec200. Returns whether it finished, which 0x4f9580
// returns as 0.
Core::Expected<bool, std::string> AdvanceTheBody(Kernel::ActorState& actor, std::size_t slot,
                                                 const CharacterStepContext& context)
{
    // 0x4f958e..0x4f95a5: the type's shared model.
    const bool player = actor.type == Tables::PlayerSkeletonModel;
    const Models::Skeleton* skeleton = ModelSkeletonOf(actor, context);
    if (skeleton == nullptr)
    {
        return Core::Unexpected(std::format("0x4f9580: el modelo {:#x} del lugar {} (0x5769ad8 + tipo · 0xe0) no está "
                                            "cargado",
                                            actor.type, slot));
    }
    const bool frozen = (actor.flags78 & FrozenFlag) == FrozenFlag;
    // 0x4f95a7: without actions (+0x26 at 0) the speed stays at 0 and 0x4ec200 leaves without advancing (0x4ec214).
    if (skeleton->actions.empty())
    {
        return frozen;
    }
    Models::AnimationState& animation = actor.animation;
    const std::uint8_t action = animation.action;
    const auto publishedAction = context.world.ModelPlacement(actor.type).action;
    if (publishedAction >= skeleton->actions.size())
    {
        return Core::Unexpected(std::format("0x4f95c0: la acción {:#x} del lugar {} lee fuera de las {} del modelo "
                                            "{:#x}",
                                            publishedAction, slot, skeleton->actions.size(), actor.type));
    }
    const Models::ActionInfo& info = skeleton->actions[publishedAction];
    // 0x4f95c0..0x4f95d4: a negative or NaN speed counts as 0. Player.bmd's, with each character's.
    const PlayerActionSpeeds& speeds =
        context.actionSpeeds != nullptr ? *context.actionSpeeds : context.world.ActionSpeeds();
    const float sharedSpeed = player ? speeds.Speed(publishedAction, info.speed) : info.speed;
    float speed = sharedSpeed >= 0.0f ? sharedSpeed : 0.0f;
    if (actor.byte3e3 != 0 && action >= FirstFasterAction && action <= LastFasterAction)
    {
        speed *= FasterFactor;
    }
    const float frame = animation.frame;
    if (action == SlowedAction)
    {
        if (frame > SlowedFrame)
        {
            speed *= HalfFactor;
        }
    }
    else if (IsOneOf(ParticleActions, action))
    {
        if (frame > ParticleFrame)
        {
            speed *= TenthFactor;
        }
    }
    else if ((actor.characterClass & ClassMask) == ClassFour && IsOneOf(ClassFourActions, action) &&
             frame > ClassFourFirstFrame && frame < ClassFourLastFrame)
    {
        // 0x4f969d..0x4f96ba: 0x59e680 with the character.
        speed *= Scenes::IsInTheParty(context.world, actor) ? PartyClassFourFactor : HalfFactor;
    }
    if (action == HalvedAction)
    {
        speed *= HalfFactor;
    }
    // 0x4f96d4..0x4f9737: the other types (the NewFace, 0x192 and 0x1a9).
    speed = Scenes::ModelSpeedOf(actor.type, action, frame, speed);
    // 0x4f973a: with +0x78 & 0x1000 it returns 0 without advancing.
    if (frozen)
    {
        return true;
    }
    // 0x4f9774: 0x4ec200, which returns 0 when it finishes; in scene 4 the frame + 2 decides it (Q-C21). The NewFace
    // is the selection's loose object; 0x4ec200 looks at the current scene, not the type.
    const Models::AnimationScene scene = context.scene == Scenes::SceneId::CharacterSelect
                                             ? Models::AnimationScene::CharacterSelect
                                             : Models::AnimationScene::Other;
    // 0x4ec21b uses the BYTE +0xa0 published before animating, even if the actor changed its action.
    if (info.keyCount <= 1)
        return false;
    auto advanced = animation;
    advanced.action = publishedAction;
    const bool finished = Models::AdvanceAnimation(advanced, info, speed, scene);
    animation.frame = advanced.frame;
    animation.previousAction = advanced.previousAction;
    animation.previousKey = advanced.previousKey;
    const auto whole = Math::OriginalFtolLow(static_cast<double>(animation.frame));
    if (!whole)
        return Core::Unexpected(std::string("0x4ec31f: el frame avanzado no tiene conversión nativa observada"));
    const auto key = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(*whole));
    context.world.FaunaResources().RecordAnimation(actor.type, animation.frame, key);
    return finished;
}

// 0x52e810(c) from 0x4fcb9c and 0x4fcc3c: the remains, with the model of c's type and the scale and position the last
// 0x505ec6 of that type left on it (Q-C1178).
Core::Expected<void, std::string> LeaveTheRemainsOf(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    const Kernel::SharedModelPlacement placement = context.world.ModelPlacement(actor.type);
    return LeaveTheRemains(actor, ActorRemainsContext{.skeleton = ModelSkeletonOf(actor, context),
                                                      .modelScale = placement.scale,
                                                      .modelOrigin = placement.origin,
                                                      .effects = context.world.Map().effects,
                                                      .random = context.random,
                                                      .marks = context.groundMarks});
}

// 0x4fcb10(c, c) from 0x4fce57, with the action that finished.
Core::Expected<void, std::string> StopAtTheEnd(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    // 0x4fcb17..0x4fcb23 and 0x4fcb87..0x4fcba6: with +0x38a at 0, sets it to 1 and runs 0x52e810 once; with or
    // without it, leaves without going back to the standing one.
    if (action == FirstMarkAction || action == SecondMarkAction)
    {
        if (actor.byte38a != 0)
            return {};
        actor.byte38a = 1;
        return LeaveTheRemainsOf(actor, context);
    }
    // 0x4fcb25..0x4fcb63: these go back to the standing one (0x4f2250): those before 0x0d, 0xbd to 0xbf, 0x54
    // and 0x55 and, from 0x22 to 0xc2, those that are not 0x8b, 0x8c, 0x46, 0x47, 0x44, 0x49, 0x4b or from 0x6a to
    // 0x75.
    const bool kept = action < FirstKeptAction || action > LastKeptAction || IsOneOf(KeptActions, action) ||
                      (action >= FirstKeptRange && action <= LastKeptRange);
    const bool stops = action < FirstStoppedAfter || (action >= FirstSpecialStop && action <= LastSpecialStop) ||
                       (action >= FirstCombatStop && action <= LastCombatStop) || !kept;
    if (stops)
    {
        const auto& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
        static const Models::Skeleton emptyPlayerSkeleton;
        const Models::Skeleton* playerSkeleton = context.playerSkeleton;
        if (playerSkeleton == nullptr)
            playerSkeleton = &emptyPlayerSkeleton;
        const bool player = actor.type == Tables::PlayerSkeletonModel;
        const Models::Skeleton* actorSkeleton = player ? nullptr : ModelSkeletonOf(actor, context);
        const std::span<const std::int16_t> modelSounds = context.world.FaunaResources().SoundActions(actor.type);
        const auto stopped = SetActorStop(
            actor, ActorStopContext{.runtime = context.world.Runtime(),
                                    .scene = context.scene,
                                    .playerSkeleton = *playerSkeleton,
                                    .items = context.items,
                                    .random = context.random,
                                    .terrainWalls = terrain.attributes ? &terrain.attributes->cells : nullptr,
                                    .actorSkeleton = actorSkeleton,
                                    .modelSounds = modelSounds});
        if (!stopped.HasValue())
        {
            return Core::Unexpected(stopped.Error());
        }
    }
    // 0x4fcb71..0x4fcb7a: 0x44, also when 0x4f2250 set it, goes back to frame 0.
    if (actor.animation.action == HalvedAction)
    {
        actor.animation.frame = 0.0f;
    }
    return {};
}

// 0x4fcd00(c, c, b) from 0x4fd770.
Core::Expected<void, std::string> StepTheAnimation(Kernel::ActorState& actor, std::size_t slot,
                                                   const CharacterStepContext& context)
{
    // 0x4fcd0c..0x4fcd55: with +0x88 at 4, on maps 0 and 2, the 0x20a of subtype 0xd1 to 0xd3 (type 236) turns.
    const bool turned = Scenes::TurnTheNpc(actor, context.world.Runtime().map);
    // 0x4fcd60: 0x4fc790, the NPC's message.
    if (auto said = Scenes::SayTheNpcLine(actor, slot, context); !said.HasValue())
    {
        return said;
    }
    // 0x4fcd65..0x4fce15: the name message of 0x6c88b0, with its clock.
    if (auto named = Scenes::SayTheNamedLine(actor, slot, context); !named.HasValue())
    {
        return named;
    }
    // 0x4fce1d: 0x4f9580.
    const auto finished = AdvanceTheBody(actor, slot, context);
    if (!finished.HasValue())
    {
        return Core::Unexpected(finished.Error());
    }
    // 0x4fce27: unfinished, only the 0x1f5 part (0x4fce7a).
    if (finished.Value())
    {
        // 0x4fce33/39: both sounds are enabled again when the animation finishes.
        actor.firstFootstepSound = 0;
        actor.secondFootstepSound = 0;
        // 0x4fce3f..0x4fce57: the 0x20a that turned goes to 0x4fc8c0 and the other to 0x4fcb10.
        if (actor.type == Tables::PlayerSkeletonModel)
        {
            return turned ? Scenes::FinishTheTurnedNpc(actor, slot, context) : StopAtTheEnd(actor, context);
        }
        // 0x4fce5e: the other models, to 0x4fcbb0. AdvanceTheBody already found the model.
        const Models::Skeleton& skeleton = *ModelSkeletonOf(actor, context);
        const auto end = Scenes::FinishTheModelAction(actor, skeleton, context.world.Runtime(), context.random);
        if (!end.HasValue())
        {
            return Core::Unexpected(end.Error());
        }
        // 0x4fcc35..0x4fcc3c: the monster that finished dying leaves its remains.
        if (end.Value() == Scenes::ModelActionEnd::LeaveRemains)
        {
            if (auto left = LeaveTheRemainsOf(actor, context); !left.HasValue())
            {
                return left;
            }
        }
        // 0x4fcbe3: with 0x42d890 at 1, 0x4f9580 again; what it returns is not looked at.
        if (end.Value() == Scenes::ModelActionEnd::AdvanceAgain)
        {
            if (auto again = AdvanceTheBody(actor, slot, context); !again.HasValue())
            {
                return Core::Unexpected(again.Error());
            }
        }
        // 0x4fce68..0x4fce70: 0xc5, with the frame at 8.
        if (actor.type == NamedType)
        {
            constexpr float NamedFrame = 8.0f; // 0x41000000
            actor.animation.frame = NamedFrame;
        }
    }
    // 0x4fce7a..0x4fced1: 0x1f5 compares the key the last pose left on its model (+0xa8 of 0x5769ad8 + 0x1f5 · 0xe0,
    // FaunaModels::PoseKey, which the monsters' drawing keeps) with the last key of its action (+0x111, without
    // checking whether the model has it) and, if it is that one, rand() % 32 at 0 sets action 1 with 0x4ea030 and
    // otherwise 0.
    constexpr std::uint16_t TraderModel = 0x1f5;
    if (actor.type == TraderModel)
    {
        const Models::Skeleton& skeleton = *ModelSkeletonOf(actor, context);
        const std::uint8_t action = actor.animation.action;
        if (action >= skeleton.actions.size())
            return Core::Unexpected(std::format("0x4fce9c: la acción {} del 0x1f5 del lugar {} está fuera de su modelo",
                                                static_cast<int>(action), slot));
        const std::int32_t lastKey = skeleton.actions[action].keyCount - 1;
        if (context.world.FaunaResources().PoseKey(TraderModel) == lastKey)
        {
            constexpr int OddsOfTheOtherAction = 32;
            const std::uint8_t next = context.random.Next() % OddsOfTheOtherAction == 0 ? 1 : 0;
            Models::SetAction(actor.animation, skeleton, next);
        }
    }
    return {};
}

// 0x4fcee0(c, c, b) from 0x505410, with the 0x20a. The particles and the clock are the step's (0x432bd0 creates the
// 0x5d9 of 0x1b0).
Core::Expected<void, std::string> StepTheWeaponTrail(Kernel::ActorState& actor, std::size_t slot,
                                                     const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    // 0x4fceea..0x4fcf25: before frame 3 (or with NaN) and outside 0x8d it copies the position to +0x17c and leaves.
    if (!(actor.animation.frame >= TrailFrame) && action != AlwaysTrailedAction)
    {
        actor.trailOrigin = actor.position;
        return {};
    }
    if (auto emitted = Actors::Drawing::EmitWeaponTrail(actor, ModelSkeletonOf(actor, context), context);
        !emitted.HasValue())
        return emitted;
    const auto* skeleton = ModelSkeletonOf(actor, context);
    if (auto emitted = Scenes::EmitEarlyMonsterRibbonTrails(actor, skeleton, context); !emitted.HasValue())
        return emitted;
    // 0x4fd5c1..0x4fd5df: 0x441260 (only 0x1a7), 0x42fa40 (0x1b1 and 0x1b3), 0x432bd0 and 0x426d90 (0x1b8 to
    // 0x1ba). 0x4fd5d7: 0x432bd0, with its unconditional rand() of 0x432bf1 (Scenes::StepTheSwingsOf1abTo1b4).
    if (auto swung = Scenes::StepTheSwingsOf1abTo1b4(actor, slot, context.world, context.random, context.particles,
                                                     context.worldClock);
        !swung.HasValue())
        return swung;
    return Scenes::EmitLateMonsterRibbonTrails(actor, skeleton, context);
}

// A read of the hero's protected cell: opens the entry (0x40b570) and closes it (0x40b800, or inline
// with 0x40b240, 0x40b2f0 and 0x412890), with the four rand() of the last close.
std::int32_t ReadHeroCell(std::uint8_t value, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return value;
}

// 0x4fbe10, for each character that takes a step: compares the hero's logical cell (+0x41c and +0x420) with the
// stored one (0x743a180 and 0x743a184, .bss 0). If it is the same and 10 s passed (the world clock minus 0x743a188,
// times 0.001f, on the x87) on a cell with bit 8 of the attribute, it sends C1 06 AF 02 x y (0x4fbeed..0x4fc39d).
// With the same cell it stores the clock only if that time passed; with another, it stores the cell and the clock.
// Each comparison opens and closes its entry.
Core::Expected<void, std::string> ReportTheHeroCell(const CharacterStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::ActorState& hero = context.world.Hero();
    Crt::CrtRandom& random = context.random;
    if (ReadHeroCell(hero.cellX, hero.cellXAccesses, random) == runtime.reportedCellX &&
        ReadHeroCell(hero.cellY, hero.cellYAccesses, random) == runtime.reportedCellY)
    {
        // 0x4fbea8..0x4fbec5: fld, fsub, fmul and fcomp with 10; less (or unordered), nothing.
        constexpr double Thousandth = Math::ThousandthAsDouble;
        constexpr double ReportDelay = 10.0;
        const double waited =
            (static_cast<double>(context.worldClock) - static_cast<double>(runtime.reportedCellTime)) * Thousandth;
        if (!(waited >= ReportDelay))
            return {};
        const Terrain::WorldTerrain& terrain =
            context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
        if (terrain.attributes == nullptr)
            return Core::Unexpected(std::string("0x4fbed9: falta el .att del mapa (0x7d0d348)"));
        constexpr std::uint16_t ReportedCellBit = 8;
        const auto cell = Data::Terrain::CellIndex(static_cast<std::uint8_t>(runtime.reportedCellX),
                                                   static_cast<std::uint8_t>(runtime.reportedCellY));
        if ((terrain.attributes->cells[cell] & ReportedCellBit) == ReportedCellBit)
        {
            // 0x4fbeed..0x4fc231: C1, the length, AF, 02 and the low byte of 0x743a180 and of 0x743a184, with the XOR
            // of 32 byte by byte from the 02; 0x4fc26c..0x4fc39d sends it (send or the queue of [0x6c82a0]). Without a
            // socket (+0xc at −1) the original sends nothing and goes on; MuRet's session without the GameServer open
            // returns an error, as with the D3.
            if (!context.sendChaosCastleReposition)
                return Core::Unexpected(std::format("0x4fbee8: el héroe lleva 10 s en la celda ({}; {}) con el bit 8 "
                                                    "y no hay sesión para el C1 06 AF 02",
                                                    runtime.reportedCellX, runtime.reportedCellY));
            // 0x4fc30b and 0x4fc355: with the send failed it also stores the clock before leaving.
            auto sent = context.sendChaosCastleReposition(static_cast<std::uint8_t>(runtime.reportedCellX),
                                                          static_cast<std::uint8_t>(runtime.reportedCellY));
            runtime.reportedCellTime = context.worldClock;
            return sent;
        }
        // 0x4fc39d: without the bit, the world clock to 0x743a188.
        runtime.reportedCellTime = context.worldClock;
        return {};
    }
    runtime.reportedCellX = ReadHeroCell(hero.cellX, hero.cellXAccesses, random);
    runtime.reportedCellY = ReadHeroCell(hero.cellY, hero.cellYAccesses, random);
    runtime.reportedCellTime = context.worldClock;
    return {};
}

// 0x4fd787..0x4fd7e1: +0x389 plus one, in a byte; with 15 or more, 0x4f5660 (SetActorDie); in Blood Castle (11 to 17)
// with +0x1a1, 0x4f5660 again, also before 15.
Core::Expected<void, std::string> DieAgain(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    const Scenes::ActorCombatContext combat{.world = context.world,
                                            .random = context.random,
                                            .playerSkeleton = context.playerSkeleton,
                                            .models = context.models,
                                            .items = context.items};
    constexpr std::uint8_t DyingAgain = 15;
    actor.dying = static_cast<std::uint8_t>(actor.dying + 1);
    if (actor.dying >= DyingAgain)
    {
        if (auto died = Scenes::SetActorDie(actor, combat); !died.HasValue())
            return died;
    }
    const std::int32_t map = context.world.Runtime().map;
    using Maps::FirstBloodCastleMap;
    using Maps::LastBloodCastleMap;
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap && actor.byte1a1 != 0)
        return Scenes::SetActorDie(actor, combat);
    return {};
}

Core::Expected<void, std::string> StepTheRaven(Kernel::ActorState& actor, std::size_t slot,
                                               const CharacterStepContext& context);

// 0x4fd5f0(c, c) from 0x508edf, through the path of the still 0x20a (06, "El cuerpo de los personajes
// (0x4fd5f0)"). `hero` is c == [0x743a170].
// 0x417e62: the raven's shout.
constexpr std::int32_t RavenShoutSound = 0x1d1;

// What the raven's step reads from the pool (DarkSpiritTargets): the character of +8 and that of +0x3b0, signed.
// Between 0 and 399 it looks at their flags (0x416a8b..0x416ab9); 6 reads its position with any value other than
// 0xffff (0x417858): the slots the A8 writes (0 to 400) are in MuRet's pool, and another value stops.
Core::Expected<Scenes::DarkSpiritTargets, std::string> RavenTargetsOf(const Kernel::DarkSpirit& spirit,
                                                                      Kernel::WorldState& world, std::size_t slot)
{
    constexpr std::int16_t PoolActors = 400;
    constexpr std::uint16_t NoAttackTarget = 0xffff;
    const auto actors = world.Actors();
    Scenes::DarkSpiritTargets targets;
    if (spirit.target < actors.size())
    {
        targets.target = actors[spirit.target].position;
    }
    const auto attack = static_cast<std::int16_t>(spirit.target3b0);
    if (attack >= 0 && attack < PoolActors)
    {
        targets.attackInPool = true;
        targets.attackFlags = actors[static_cast<std::size_t>(attack)].flags78;
    }
    if (spirit.target3b0 != NoAttackTarget)
    {
        if (attack < 0 || static_cast<std::size_t>(attack) >= actors.size())
        {
            return Core::Unexpected(
                std::format("0x417869: el +0x3b0 del cuervo del lugar {} ({}) cae fuera del pool", slot, attack));
        }
        targets.attackPosition = actors[static_cast<std::size_t>(attack)].position;
    }
    return targets;
}

glm::vec3 OwnBonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset);

// The body published the model BEFORE moving the dying one. Do not replace this placement with the current
// actor.position.
glm::vec3 SharedBonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                          const Kernel::WorldState& world)
{
    const auto placement = world.ModelPlacement(actor.type);
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
}

const Models::Skeleton* BoneEmitterSkeleton(const Kernel::ActorState& actor, const CharacterStepContext& context)
{
    if (actor.type != Tables::PlayerSkeletonModel && !IsNewFace(actor.type) && context.models != nullptr)
    {
        if (const auto* loaded = context.models->SkeletonOf(actor.type); loaded != nullptr)
            return loaded;
    }
    return ModelSkeletonOf(actor, context);
}

// 0x4fd835..0x4fdada: the counter only drops after walking the bones; a full J keeps six selectors per bone.
Core::Expected<void, std::string> StepTheBoneEmitter(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    if (context.world.Runtime().map == CastleMap || actor.byte1a0 == 0)
        return {};
    const auto* skeleton = BoneEmitterSkeleton(actor, context);
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x4fd86e: faltan los huesos del modelo para el emisor 0x614/7"));
    constexpr int OffsetSpan = 41, OffsetCentre = 20;
    constexpr std::int32_t BoneJoint = 0x614, BoneSubtype = 7;
    constexpr float Width = 20.0f;
    for (std::size_t bone = 0; bone < skeleton->bones.size(); ++bone)
    {
        const auto& entry = skeleton->bones[bone];
        if (entry.dummy || entry.parent < 0 || static_cast<std::size_t>(entry.parent) >= skeleton->bones.size())
            continue;
        glm::vec3 first = SharedBonePoint(actor, bone, glm::vec3(0.0f), context.world);
        glm::vec3 second =
            SharedBonePoint(actor, static_cast<std::size_t>(entry.parent), glm::vec3(0.0f), context.world);
        for (auto* point : {&first, &second})
            for (int axis = 0; axis < 3; ++axis)
                (*point)[axis] =
                    static_cast<float>(static_cast<double>((*point)[axis]) +
                                       static_cast<double>(context.random.Next() % OffsetSpan - OffsetCentre));
        if (context.joints.empty())
            return Core::Unexpected(std::string("0x4fdabf: falta el pool de joints del emisor 0x614/7"));
        const World::FishEffectRequest request{.type = BoneJoint,
                                               .firstPosition = first,
                                               .secondPosition = second,
                                               .angles = actor.angles,
                                               .subtype = BoneSubtype,
                                               .ownerContext = nullptr,
                                               .scale = Width,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
        if (auto joint = Scenes::CreateFaunaJoint(context.joints, request, context.random); !joint.HasValue())
            return Core::Unexpected(std::format("0x4fdabf: {}", joint.Error()));
    }
    --actor.byte1a0;
    return {};
}

// 0x4fdb3f: 0x4fd5f0's local light with particle 0x5ac (0.3f is 0x3e99999a); the skill cases read it later
// (Scenes::BodyLight).
constexpr glm::vec3 ActionFlareLight{0.3f, 0.5f, 1.0f};

Core::Expected<void, std::string> CreateTheActionFlare(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::size_t FlareBone = 42;
    constexpr std::int32_t FlareType = 0x5ac;
    constexpr glm::vec3 Light = ActionFlareLight;
    const glm::vec3 point = SharedBonePoint(actor, FlareBone, glm::vec3(0.0f), context.world);
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x4fdb92: falta el pool de partículas de la acción con 0x5ac/0"));
    const auto made = context.particles->CreateChecked(FlareType, point, actor.angles, Light, 0, 1.0f,
                                                       context.worldClock, context.random);
    if (!made.HasValue())
        return Core::Unexpected(
            std::format("0x4fdb92: la partícula 0x5ac/0 falla con error {}", static_cast<unsigned>(made.Error())));
    return {};
}

Core::Expected<void, std::string> StepTheBody(Kernel::ActorState& actor, std::size_t slot, bool hero,
                                              const CharacterStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x4fd603..0x4fd698: 0xc5 passes its name and +0x1d6 to 0x4381f0 (MonsterStepCases.h).
    if (actor.type == NamedType)
    {
        if (auto queued = Scenes::QueueTheGateName(actor, context.texts, runtime.worldTexts); !queued.HasValue())
            return queued;
    }
    // 0x4fd69b..0x4fd6ef: publishes the position, the scale and the action to the shared model (+0x6c, +0x68 and
    // +0xa0).
    context.world.PlaceModel(actor);
    // 0x4fd6f5: 0x4fbe10, the hero's cell on maps 18 to 23.
    if (runtime.map >= FirstCellReportMap && runtime.map <= LastCellReportMap)
    {
        if (auto reported = ReportTheHeroCell(context); !reported.HasValue())
            return reported;
    }
    // 0x4fd6fe: 0x4fc450.
    if (hero)
    {
        if (auto timers = StepTheOwnTimers(context); !timers.HasValue())
        {
            return timers;
        }
    }
    // 0x4fb38b..0x4fb3b1: skills 8 and 9 set +0x398; it turns by the PREVIOUS value first, unnormalized and without
    // RNG.
    if (actor.byte398 != 0)
    {
        constexpr double TurnPerCount = 10.0;
        actor.angles.z =
            static_cast<float>(static_cast<double>(actor.angles.z) + static_cast<double>(actor.byte398) * TurnPerCount);
        --actor.byte398;
    }
    // 0x4fd70e: 0x4fb380. 0xf1 falls (0x4fb910).
    if (actor.type == FallingType)
    {
        const std::function<double(float, float)> height = TerrainHeightOf(context);
        if (!height)
        {
            return Core::Unexpected(
                std::format("0x4fb942: falta la altura del terreno (0x5f93c0) para el 0xf1 del lugar {}", slot));
        }
        Scenes::StepTheFallingBall(actor, height);
    }
    // 0x4fd722: 0x4fb9e0, dying (+0x389). What follows runs even if it erased it.
    if (auto died = StepActorDying(actor, slot,
                                   {.map = runtime.map,
                                    .scene = context.scene,
                                    .hero = hero,
                                    .random = context.random,
                                    .particles = context.particles,
                                    .worldClock = context.worldClock,
                                    .world = &context.world,
                                    .soundRequest = context.soundRequest});
        !died.HasValue())
    {
        return died;
    }
    // 0x4fd72e: 0x4e9980, the alpha toward its goal and the blendMesh light.
    World::StepObjectAlpha(actor.alphaTarget, actor.alpha, actor.blendMeshLight, actor.alphaMode);
    // 0x4fd736..0x4fd75e: +0x3d8 at 0: on characters only this subtraction writes it (*static*).
    // 0x4fd770: 0x4fcd00.
    if (auto animated = StepTheAnimation(actor, slot, context); !animated.HasValue())
    {
        return animated;
    }
    // 0x4fd77b..0x4fd7dc: dying, +0x389 rises (a byte, which wraps) and from 15 calls 0x4f5660; in Blood Castle
    // (11 to 17) with +0x1a1, again.
    if (actor.dying != 0)
    {
        if (auto died = DieAgain(actor, context); !died.HasValue())
            return died;
    }
    // 0x4fd7e4: unconditional rand() % 6; (r + 2) · 0.1f goes to a local this path does not read (Q-C247).
    static_cast<void>(context.random.Next());
    if (auto emitted = StepTheBoneEmitter(actor, context); !emitted.HasValue())
        return emitted;
    // 0x4fdae0..0x4fdb92: 0x4d, 0x4f and 0x5a past frame 5.5 release particle 0x5ac from bone 42.
    // 0x4fd5f0's local light: (1, 1, 1) at 0x4fd81e and the particle's at 0x4fdb3f.
    glm::vec3 bodyLight = Scenes::BodyLight;
    const std::uint8_t action = actor.animation.action;
    if (IsOneOf(ParticleActions, action) && actor.animation.frame > ParticleFrame)
    {
        bodyLight = ActionFlareLight;
        if (auto flare = CreateTheActionFlare(actor, context); !flare.HasValue())
            return flare;
    }
    // 0x4fdb9a..0x50539e: with +0x381 other than 0, 0x4f97b0 and 0x4f5b70 and +0x381 += 1; from [0x6c88ac], the skill
    // of +0x38e with the target of +0x3a4. The attack on arrival (0x5653c1, HeroAttack) and the C1 18 with 0x78 and
    // 0x79 (0x4cb830, ActionReply) set it to 1.
    if (auto attacked = Scenes::StepTheAttack(actor, slot, context, bodyLight); !attacked.HasValue())
    {
        return attacked;
    }
    // 0x5053a2..0x5053f0: counts +0x428 down and, when it expires, runs 0x511070(c, o) without stopping the body.
    if (actor.dword428 > 0)
        --actor.dword428;
    if (actor.dword428 <= 0 && actor.dword428 != Kernel::ActorRemovalCountdownSentinel)
    {
        actor.dword428 = Kernel::ActorRemovalCountdownSentinel;
        RemoveActorAtSlot(context.world, actor, slot);
    }
    // 0x5053fc: 0x42a2b0, the health bar of 0x1a8.
    Scenes::QueueTheHealthBar(actor, context.world.Runtime().healthBars);
    // 0x505410: 0x4fcee0.
    if (auto trailed = StepTheWeaponTrail(actor, slot, context); !trailed.HasValue())
    {
        return trailed;
    }
    // 0x505425..0x505444: 0xf1 throws the class 0 fire of 0x52f360 without an offset, with its angles, its position
    // and its light.
    if (actor.type == FallingType)
    {
        const World::ObjectStepGlobals globals{.worldClock = context.worldClock,
                                               .dynamicLighting = &context.world.DynamicLighting().Lighting(),
                                               .particles = context.particles};
        if (auto fire = World::EmitObjectFire(0, actor.angles, actor.position, actor.light, glm::vec3(0.0f),
                                              context.random, globals);
            !fire.HasValue())
        {
            return Core::Unexpected(std::format("{} (el 0xf1 del lugar {})", fire.Error(), slot));
        }
    }
    // 0x50544b: 0x420dd0, after the trail and before 0x505de0, also outside the trapezoid.
    if (actor.darkSpirit && (actor.flags78 & FrozenFlag) == 0)
    {
        if (auto raven = StepTheRaven(actor, slot, context); !raven.HasValue())
        {
            return raven;
        }
    }
    // 0x505453..0x5054aa: dying, turns off 0x8000 and 0x10000 of +0x78.
    if (actor.dying != 0)
    {
        constexpr std::uint32_t DyingClearedFlags = 0x8000 | 0x10000;
        actor.flags78 &= ~DyingClearedFlags;
    }
    return {};
}

// 0x420dd0: the raven of +0x430, which the caller takes as existing and without +0x78 & 0x1000.
Core::Expected<void, std::string> StepTheRaven(Kernel::ActorState& actor, std::size_t slot,
                                               const CharacterStepContext& context)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (context.darkSpiritSkeleton == nullptr || context.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("0x416a20: faltan el modelo o las partículas del cuervo del lugar {}", slot));
    }
    auto targets = RavenTargetsOf(*actor.darkSpirit, context.world, slot);
    if (!targets.HasValue())
    {
        return Core::Unexpected(targets.Error());
    }
    const Kernel::SharedModelPlacement& player = context.world.PlayerModel();
    const Scenes::DarkSpiritStepped stepped =
        Scenes::StepDarkSpirit(*actor.darkSpirit, *context.darkSpiritSkeleton,
                               {.position = actor.position,
                                .flags = actor.flags78,
                                .angles = actor.angles,
                                .pet = actor.pet.model,
                                .safeZone = actor.safeZone,
                                .pose = actor.bodyPose,
                                .modelScale = player.scale,
                                .modelOrigin = player.origin},
                               static_cast<std::uint8_t>(runtime.map), *context.particles, context.worldClock,
                               context.random, context.counter, targets.Value());
    // 0x416fc8 and 0x4175de: the A7 that stops the attack, through send(); without the session, it does not go out.
    if (stepped.stopsTheAttack && context.send)
    {
        context.send(Protocol::Pets::WritePetCommand({.pet = Protocol::Pets::RavenPet,
                                                      .command = Protocol::Pets::StopTheAttackCommand,
                                                      .key = Protocol::Pets::NoTargetKey}));
    }
    // 0x417e67: 0x41ed10(0x1d1).
    if (stepped.shouts && context.soundRequest)
    {
        context.soundRequest(RavenShoutSound);
    }
    return {};
}

// 0x4ec110(model, own bones + bone · 0x30, offset, point, 1) with the scale and position 0x505ec6 copied from the
// character to the model. Without own bones (before the first drawing), the matrix at zero (Q-C318).
glm::vec3 OwnBonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, actor.scale, actor.position);
}

Core::Expected<void, std::string> EmitSkillParticle(std::int32_t type, const glm::vec3& point,
                                                    Kernel::ActorState& actor, const glm::vec3& light,
                                                    std::int32_t subtype, float scale,
                                                    const CharacterStepContext& context, std::string_view address)
{
    if (context.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas de la skill", address));
    const auto made = context.particles->CreateChecked(type, point, actor.angles, light, subtype, scale,
                                                       context.worldClock, context.random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{}: la partícula {:#x}/{} de la skill falla con error {}", address, type,
                                            subtype, static_cast<unsigned>(made.Error())));
    return {};
}

// 0x5189f0(c, &position): three 0x619/8 (0x518aa8: 0x530860(0x619, first, position, angles, 8, c, 10, −1, 0, 0,
// −1)). Each one with the angles (rand() % 90, 0, rand() % 360), which are drawn also with the joint table full, and
// the first point at the position minus (0, −500, 0) turned by them, 120 higher. The creation draws its light's rand()
// (FaunaJointCreation, 0x5324ea) only with a free slot.
Core::Expected<void, std::string> EmitChargedSkillJoints(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    constexpr int Count = 3, PitchSpan = 90, YawSpan = 360;
    constexpr glm::vec3 Offset{0.0f, -500.0f, 0.0f};
    constexpr double Lift = 120.0;
    constexpr std::int32_t Type = 0x619, Subtype = 8;
    constexpr float Width = 10.0f;
    for (int index = 0; index < Count; ++index)
    {
        glm::vec3 angles{static_cast<float>(context.random.Next() % PitchSpan), 0.0f,
                         static_cast<float>(context.random.Next() % YawSpan)};
        const glm::vec3 rotated = Math::OriginalVectorRotate(Offset, Math::OriginalAngleMatrix(angles));
        glm::vec3 first{};
        for (int axis = 0; axis < 3; ++axis)
            first[axis] = static_cast<float>(static_cast<double>(actor.position[axis]) -
                                             static_cast<double>(rotated[axis]) + (axis == 2 ? Lift : 0.0));
        if (context.joints.empty())
            return Core::Unexpected(std::string("0x518aa8: falta el pool de joints619/8 de la skill cargada"));
        const World::FishEffectRequest request{.type = Type,
                                               .firstPosition = first,
                                               .secondPosition = actor.position,
                                               .angles = angles,
                                               .subtype = Subtype,
                                               .ownerContext = &actor,
                                               .scale = Width,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
        if (auto joint = Scenes::CreateFaunaJoint(context.joints, request, context.random); !joint.HasValue())
            return Core::Unexpected(std::format("0x518aa8: {}", joint.Error()));
    }
    return {};
}

// 0x506e57..0x506f5a: the unpublished pose uses the zero matrix of Q-C318; it is not the same as a NULL +0x120.
Core::Expected<void, std::string> EmitChargedSkillFlares(Kernel::ActorState& actor, const Models::Skeleton& skeleton,
                                                         const CharacterStepContext& context)
{
    constexpr std::size_t PastBone = 40, BoneStep = 2;
    constexpr glm::vec3 Offset{-15.0f, 0.0f, 0.0f}, Light{0.3f, 0.3f, 1.0f};
    constexpr std::int32_t Type = 0x5ac, Subtype = 6;
    constexpr double ScaleStep = static_cast<double>(0.08f), BaseScale = static_cast<double>(1.3f);
    const float scale = static_cast<float>(static_cast<double>(actor.byte1bc) * ScaleStep + BaseScale);
    for (std::size_t bone = 0; bone < PastBone && bone < skeleton.bones.size(); bone += BoneStep)
    {
        if (skeleton.bones[bone].dummy)
            continue;
        const glm::vec3 point = OwnBonePoint(actor, bone, Offset);
        for (int flare = 0; flare <= static_cast<int>(actor.byte1bc); ++flare)
        {
            if (auto made = EmitSkillParticle(Type, point, actor, Light, Subtype, scale, context, "0x506f02");
                !made.HasValue())
                return made;
        }
    }
    return EmitChargedSkillJoints(actor, context);
}

// 0x506f62..0x506fc1: ten bone picks and 0x5d9/0 particles, already ported; a count of 0 keeps the rand() before the
// IDIV.
Core::Expected<void, std::string> EmitRandomBoneFires(Kernel::ActorState& actor, const Models::Skeleton& skeleton,
                                                      const CharacterStepContext& context)
{
    constexpr int Count = 10;
    constexpr glm::vec3 Offset{-15.0f, 0.0f, 0.0f}, Light{1.0f, 1.0f, 1.0f};
    constexpr std::int32_t Type = 0x5d9;
    for (int index = 0; index < Count; ++index)
    {
        const auto selector = static_cast<std::size_t>(context.random.Next());
        if (skeleton.bones.empty())
            return Core::Unexpected(std::string("0x506f84: el modelo sin huesos dividiría por cero en la acción0x96"));
        const glm::vec3 point = OwnBonePoint(actor, selector % skeleton.bones.size(), Offset);
        if (auto made = EmitSkillParticle(Type, point, actor, Light, 0, 1.0f, context, "0x506fb6"); !made.HasValue())
            return made;
    }
    return {};
}

// 0x506d0a..0x506d7c: in scene 5 and on map 7, the second of every ten of the world clock (ftol % 10000
// below 1000) releases a 0x607 bubble from the head: 0x4ec110 with the model's +0x54 bone (20 in Player.bmd)
// of the own bones (+0x120), (0, 20, −10), the scale (+0x68) and the position (+0x6c) that 0x505ec6 copied
// from the character, and 0x53fe70(0x607, point, +0x1c, (1, 1, 1) of 0x506c82, 0, 1, 0). Before the first drawing
// the bones are those of 0x511290's operator new, uninitialized: MuRet uses a zero matrix (Q-C318).
Core::Expected<void, std::string> BlowTheAtlansBubble(const Kernel::ActorState& actor, std::size_t slot,
                                                      const CharacterStepContext& context)
{
    constexpr std::int32_t ClockPeriod = 10000;
    constexpr std::int32_t BubbleWindow = 1000;
    const std::optional<std::int32_t> clock = Math::OriginalFtolLow(static_cast<double>(context.worldClock));
    if (!clock)
    {
        return Core::Unexpected(std::string("0x506d10: ftol del reloj del mundo fuera de lo acreditado"));
    }
    if (*clock % ClockPeriod >= BubbleWindow)
    {
        return {};
    }
    constexpr glm::vec3 HeadOffset{0.0f, 20.0f, -10.0f};
    const glm::vec3 point = OwnBonePoint(actor, static_cast<std::size_t>(Tables::PlayerHeadBone), HeadOffset);
    if (context.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("0x506d77: falta el pool de partículas de la burbuja de Atlans, lugar {}", slot));
    }
    const auto created = context.particles->CreateChecked(World::DropParticle, point, actor.angles, glm::vec3(1.0f), 0,
                                                          1.0f, context.worldClock, context.random, 0);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("0x506d77: la burbuja 0x607/0 del lugar {} falla con error {}", slot,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

// 0x506d7f..0x506e54: on map 9, in any scene, two rand() % 4 (rand() & 3): with 0, a 0x68/0 drop from 0x4ec110 with
// bone 26 (0x4e0) and then with bone 35 (0x690) of the own bones, (−15, 0, 0), and
// 0x53fe70(0x68, point, +0x1c, (1, 1, 1), 0, 1, 0). The second rand() is drawn even if the first does not give 0.
Core::Expected<void, std::string> DropTheDevilSquareRain(const Kernel::ActorState& actor, std::size_t slot,
                                                         const CharacterStepContext& context)
{
    constexpr std::int32_t RainParticle = 0x68;
    constexpr int RainOdds = 4;
    constexpr std::array<std::size_t, 2> RainBones{26, 35};
    constexpr std::array<const char*, 2> RainAddresses{"0x506dff", "0x506e4f"};
    constexpr glm::vec3 RainOffset{-15.0f, 0.0f, 0.0f};
    for (std::size_t drop = 0; drop < RainBones.size(); ++drop)
    {
        if (context.random.Next() % RainOdds != 0)
            continue;
        const glm::vec3 point = OwnBonePoint(actor, RainBones[drop], RainOffset);
        if (context.particles == nullptr)
        {
            return Core::Unexpected(std::format(
                "{}: falta el pool de partículas de la lluvia de Devil Square, lugar {}", RainAddresses[drop], slot));
        }
        const auto created = context.particles->CreateChecked(RainParticle, point, actor.angles, glm::vec3(1.0f), 0,
                                                              1.0f, context.worldClock, context.random, 0);
        if (!created.HasValue())
        {
            return Core::Unexpected(std::format("{}: la gota 0x68/0 del lugar {} falla con error {}",
                                                RainAddresses[drop], slot, static_cast<unsigned>(created.Error())));
        }
    }
    return {};
}

// 0x505e15..0x505ebb: the box of +0x13c..+0x168 from that of +0x124..+0x138: the corner plus the position and the
// three axes (the length of each side on the diagonal, the rest at 0). Each subtraction and each addition on the x87
// is stored as float: the same as in float.
void BoxTheModelWithoutActions(Kernel::ActorState& actor)
{
    PublishPickingBox(actor, actor.position, actor.boundsMin, {actor.boundsMaxX, actor.boundsMaxY, actor.boundsTop});
}

// 0x505de0(c, c) from 0x508ee6, to its end (06, "Los efectos de los personajes (0x505de0)" and "El cuerpo de los
// monstruos y los NPC"). `hero` is c == [0x743a170].
Core::Expected<void, std::string> StepTheEffects(Kernel::ActorState& actor, std::size_t slot, bool hero,
                                                 const CharacterStepContext& context,
                                                 const Data::Terrain::TerrainLayer<std::uint16_t>* walls)
{
    // 0x505e0b: a model without actions (+0x26 of 0x5769ad8 + type · 0xe0) goes to the box of 0x505e15 and leaves.
    const bool player = actor.type == Tables::PlayerSkeletonModel;
    const Models::Skeleton* skeleton = ModelSkeletonOf(actor, context);
    if (skeleton == nullptr)
    {
        return Core::Unexpected(
            std::format("0x505e0b: falta Player.bmd o el NewFace del modelo {:#x} (lugar {})", actor.type, slot));
    }
    if (skeleton->actions.empty())
    {
        BoxTheModelWithoutActions(actor);
        return {};
    }
    // 0x505ec6..0x505ee4: the position, the scale and the action go to the shared model (+0x6c, +0x68 and
    // +0xa0), which the 0x4ec110 calls below read and which the drawing writes again before posing. MuRet
    // keeps the scale and position of each type: the raven reads Player.bmd's in the safe zone before
    // this point of its owner (Q-C412), and the remains of 0x52e810 read everyone's in the body step, which comes
    // first (Q-C1178); the ones below read the character's own (OwnBonePoint).
    context.world.PlaceModel(actor);
    // 0x505ef2: unseen, nothing else.
    if (!actor.seen)
    {
        return {};
    }
    // 0x505ef8..0x506866 and 0x50686e..0x5068b2: the safe zone is bit 0x01 of the low byte of the logical cell,
    // that of +0x41c and +0x420 (06 § Caminar), not that of the drawn position.
    const LogicalCell cell = ReadLogicalCell(actor, context.random);
    if (walls == nullptr)
    {
        return Core::Unexpected(std::string("0x506362: faltan los atributos de la celda lógica del personaje"));
    }
    const std::uint16_t attribute = (*walls)[Data::Terrain::CellIndex(cell.x, cell.y)];
    actor.safeZone = static_cast<std::uint8_t>((attribute & Pathfinding::SafeZoneWallBit) != 0 ? 1 : 0);
    // 0x5068b9..0x5069ab: the head, without +0x78 & 0x20 or & 0x1000 (0x5068be..0x5068d4). 0x5068da: with another
    // model, 0x4ea190 first, which already stopped at 0x505e0b. 0x5068eb..0x5068fb: anyone but the hero, not dying
    // (+0x389), draws rand() % 32 and, if it gives 0, two more for where it looks (+0x34 and +0x38); then both
    // angles turn toward the target (0x4e96f0 and 0x4e9560). The hero neither draws nor spends: it turns toward the
    // target MoveHero writes with the mouse (0x59b1c2..0x59b287), which MuRet does not write yet.
    if ((actor.flags78 & StillHeadFlag) == 0 && (actor.flags78 & FrozenFlag) == 0)
    {
        // 0x5068da..0x5068e8: with another model, 0x4ea190 first.
        if (!player)
        {
            if (auto target = Scenes::SetTheModelHeadTarget(actor, context.world.Actors(), context.random);
                !target.HasValue())
            {
                return Core::Unexpected(std::format("{} (lugar {})", target.Error(), slot));
            }
        }
        Kernel::StepCharacterHead(actor.headTurn, !hero && actor.dying == 0, context.random);
    }
    if (auto cooldown = StepActorCooldown(actor, context.random, CharacterCooldownServices(context));
        !cooldown.HasValue())
    {
        return cooldown;
    }
    // 0x506acf..0x506b62: with +0x375 below 6, the light of each hand, the right one (+0x2b8) and then the
    // left one (+0x2d8): 0x5f9780 with the object's x and y (+0x10, +0x14) over the dynamic buffer
    // 0x7b4ccc4, WorldState's, which 0x5fbf60 restores on each step. No rand().
    if (actor.pk < FirstPkWithoutLights)
    {
        for (const Kernel::ActorItemPart& hand : actor.hands)
        {
            const auto light =
                std::ranges::find(HandLights, hand.model, [](const HandLight& candidate) { return candidate.model; });
            if (light == HandLights.end())
            {
                continue;
            }
            if (auto added = World::AddDynamicLight(&context.world.DynamicLighting().Lighting(), actor.position,
                                                    light->color, light->radius, "0x506b50");
                !added.HasValue())
            {
                return Core::Unexpected(std::format("{} (la mano del lugar {})", added.Error(), slot));
            }
        }
    }
    // 0x506b64: +0x3d8 at 0. Positive, with +0x3d4 at 0x611 it draws rand() & 3 and, if it gives 0, three more for a
    // position, and with 0x612 it walks the bones with 0x4ec110; it creates nothing (Q-C266). On characters only
    // 0x4fd5f0 writes +0x3d8, lowering it if positive (0x4fd75e); the others that write +0x3d4 and +0x3d8 belong to the
    // UI (*static*).
    // 0x506cac..0x506cf1: rand() & 7, unconditional, and the case of type − 0x152. With another model, its case or the
    // helpers of 0x5085db (Scenes::StepTheModelEffects); 0x50862f..0x508801 belong to the 0x20a, which falls into
    // 0x506cf8 (it looks at map 7 only in scene 5) and does not read the rand() (*static*, Q-C207).
    const std::int32_t map = context.world.Runtime().map;
    if (!player)
    {
        Scenes::ModelEffectsPass pass{.actor = actor,
                                      .slot = slot,
                                      .world = context.world,
                                      .random = context.random,
                                      .skeleton = *skeleton,
                                      .particles = context.particles,
                                      .sprites = context.sprites,
                                      .worldClock = context.worldClock,
                                      .map = map,
                                      .groundMarks = context.groundMarks};
        return Scenes::StepTheModelEffects(pass);
    }
    static_cast<void>(context.random.Next());
    if (context.scene == Scenes::SceneId::Game && map == ClockEffectMap)
    {
        if (auto bubbled = BlowTheAtlansBubble(actor, slot, context); !bubbled.HasValue())
        {
            return bubbled;
        }
    }
    if (map == RandomEffectMap)
    {
        if (auto rained = DropTheDevilSquareRain(actor, slot, context); !rained.HasValue())
        {
            return rained;
        }
    }
    const std::uint8_t action = actor.animation.action;
    if (action == FirstEffectAction || action == SecondEffectAction)
    {
        if (auto flared = EmitChargedSkillFlares(actor, *skeleton, context); !flared.HasValue())
            return flared;
    }
    if (action == BoneEffectAction)
    {
        if (auto fired = EmitRandomBoneFires(actor, *skeleton, context); !fired.HasValue())
            return fired;
    }
    // 0x506fc1..0x507001: 0x4ec110 with the bone of +0x2bd into a local nobody reads, and to 0x50862f.
    // 0x50862f..0x5086ca: only with the published hero (0x508641), the footsteps: the sound of 0x505770 with +0x3e0
    // and +0x3e1.
    if (hero)
    {
        for (const auto sound : TakeActorFootstepSounds(actor, map, context.world.Runtime().heroCellLayer))
        {
            if (sound && context.soundRequest)
            {
                context.soundRequest(*sound);
            }
        }
    }
    // 0x5086cf..0x508801: in Kalima, the 0x20a on actions 0x15, 0x1d to 0x21 and 0x4b (with the hero, all of them
    // footstep actions) releases 0x6f4/1 at (0, −40, 0) turned by +0x1c (0x5fc8f0 and 0x5fcbd0), with rand() % 64
    // in the x (0x508793) and in the y (0x5087b6).
    if (IsKalimaMap(map) && IsOneOf(KalimaParticleActions, action))
    {
        constexpr int FootstepSpan = 64, FootstepCenter = 32;
        constexpr double FootstepHeight = 50;
        constexpr std::int32_t FootstepParticle = 0x6f4, FootstepSubtype = 1;
        const auto matrix = Math::OriginalAngleMatrix(actor.angles);
        glm::vec3 position = Math::OriginalVectorRotate({0, -40, 0}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            position[axis] = static_cast<float>(static_cast<double>(position[axis]) + actor.position[axis]);
        position.x =
            static_cast<float>(static_cast<double>(context.random.Next() % FootstepSpan - FootstepCenter) + position.x);
        position.y =
            static_cast<float>(static_cast<double>(context.random.Next() % FootstepSpan - FootstepCenter) + position.y);
        position.z = static_cast<float>(static_cast<double>(position.z) + FootstepHeight);
        if (context.particles == nullptr)
            return Core::Unexpected(
                std::format("0x5087fc: falta el pool de partículas de los pasos de Kalima, lugar {} ", slot));
        const auto made = context.particles->CreateKalimaChecked(
            FootstepParticle, position, actor.angles, glm::vec3(0.3f), FootstepSubtype, 1.0f, context.worldClock,
            context.random, static_cast<std::int32_t>(context.scene));
        if (!made.HasValue())
            return Core::Unexpected(std::string("0x5087fc: falló el alta6f4/1 de los pasos de Kalima"));
    }
    return {};
}

Core::Expected<void, std::string> StepACharacter(Kernel::ActorState& actor, std::size_t slot, bool hero,
                                                 const CharacterStepContext& context)
{
    if (!actor.alive)
    {
        return {};
    }
    actor.seen = CharacterSeen(context.world.Runtime().terrainPolygon, actor.position);
    if (!hero)
    {
        if (auto other = StepOtherCharacter(actor, slot, context); !other.HasValue())
        {
            return other;
        }
    }
    if (auto body = StepTheBody(actor, slot, hero, context); !body.HasValue())
    {
        return body;
    }
    const Terrain::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    if (auto effects =
            StepTheEffects(actor, slot, hero, context, terrain.attributes ? &terrain.attributes->cells : nullptr);
        !effects.HasValue())
    {
        return effects;
    }
    // 0x508eed: 0x42da90, which outside map 30 returns 0 (0x42da9a); on map 30 it moves the siege objects by +0x376
    // (MonsterStepCases.h).
    const Scenes::CastleObjectStep castle{.runtime = context.world.Runtime(),
                                          .pool = context.world.Actors(),
                                          .height = TerrainHeightOf(context),
                                          .skeleton = ModelSkeletonOf(actor, context)};
    if (auto stepped = Scenes::StepTheCastleObject(actor, castle); !stepped.HasValue())
    {
        return Core::Unexpected(std::format("{} (lugar {})", stepped.Error(), slot));
    }
    return {};
}
} // namespace

bool CharacterSeen(const Terrain::TerrainPolygon& polygon, const glm::vec3& position)
{
    constexpr double CellsPerUnit = static_cast<double>(0.01f);
    constexpr double ViewMargin = -20.0;
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return Terrain::PolygonContains(polygon, cells(position.x), cells(position.y), ViewMargin);
}

LogicalCell ReadLogicalCell(Kernel::ActorState& actor, Crt::CrtRandom& random)
{
    LogicalCell cell;
    {
        const Models::LogicalAccess access(actor.cellYAccesses, random, Models::SmallLogicalCloseRandoms);
        cell.y = actor.cellY;
    }
    {
        const Models::LogicalAccess access(actor.cellXAccesses, random, Models::SmallLogicalCloseRandoms);
        cell.x = actor.cellX;
    }
    return cell;
}

Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context)
{
    Kernel::WorldState& world = context.world;
    // 0x508f06..0x508f0c: 0x412140 with the hero, before the loop.
    if (auto panel = Scenes::RefreshTheSiegePanel(world.Runtime()); !panel.HasValue())
    {
        return panel;
    }
    const Kernel::WorldRuntime& runtime = world.Runtime();
    const Terrain::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : world.Map().terrain;
    if (!terrain.attributes)
    {
        return Core::Unexpected("0x508f14: el TerrainWall (0x7d0d348) del " + MapText(runtime.map) +
                                " no está cargado (0x61a560)");
    }
    Data::Terrain::TerrainLayer<std::uint16_t>& walls = terrain.attributes->cells;
    Pathfinding::ClearCharacterWalls(walls);
    // 0x5fba90 reads the globals of 0x78ecbf0 and 0x78ecc00: the trapezoid of the last drawing of any scene.
    const Terrain::TerrainPolygon& polygon = runtime.terrainPolygon;
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < SteppedCharacters; ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        // 0x508f49..0x509165: alive, not dying and without the 8 of +0x88, it takes its logical cell.
        if (actor.alive && actor.dying == 0 && actor.byte88 != Pathfinding::NonBlockingKind)
        {
            const LogicalCell cell = ReadLogicalCell(actor, context.random);
            Pathfinding::MarkCharacterWall(walls, cell.x, cell.y);
        }
        // 0x50916d..0x5091a0: alive or not, whether it is seen, with the trapezoid of the last 0x5fb570.
        actor.seen = CharacterSeen(polygon, actor.position);
    }
    for (std::size_t slot = 0; slot < SteppedCharacters; ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        const bool hero = slot == runtime.hero;
        if (auto stepped = StepACharacter(actor, slot, hero, context); !stepped.HasValue())
        {
            return stepped;
        }
    }
    // 0x5091d0: once after the last 0x508ea0, also at login and before the selection's preview.
    return Actors::Drawing::StepRibbons(world.DrawTail().ribbons);
}

// 0x5069ae..0x506ac6: the creators of the +0x3e5 brake with the step's context. The 0x5f3/1 goes to the particle
// pool at the drawn position, with the character's angles (+0x1c) and light (+0xf4, already at 1), scale 1 and no
// owner (0x506a57..0x506a73); the 0xc8 or 0xc9 pebble, to the world's effect table at the character's position
// (+0x10), with its angles and its light, subtype 0, no owner and key −1 (0x506a8c..0x506ab7). Both with the step's
// CRT. Without the pool, the particle stops there.
ActorCooldownServices CharacterCooldownServices(const CharacterStepContext& context)
{
    return {
        .particle = [&context](const glm::vec3& position,
                               const Kernel::ActorState& actor) -> Core::Expected<void, std::string>
        {
            constexpr std::int32_t BrakeDustSubtype = 1;
            if (context.particles == nullptr)
                return Core::Unexpected(std::string("0x506a73: falta el pool de partículas del polvo 0x5f3/1"));
            const auto created =
                context.particles->CreateChecked(World::DustParticle, position, actor.angles, actor.light,
                                                 BrakeDustSubtype, 1.0f, context.worldClock, context.random, 0);
            if (!created.HasValue())
                return Core::Unexpected(std::format("0x506a73: el polvo 0x5f3/1 falla con error {}",
                                                    static_cast<unsigned>(created.Error())));
            return {};
        },
        .effect = [&context](std::int32_t type, const Kernel::ActorState& actor) -> Core::Expected<void, std::string>
        {
            static_cast<void>(context.world.Map().effects.CreateStoneChip(
                static_cast<std::int16_t>(type), actor.position, actor.angles, actor.light, context.random));
            return {};
        }};
}

Core::Expected<void, std::string> StepCharacter(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    return StepACharacter(actor, Kernel::ActorPoolSize, false, context);
}

Core::Expected<void, std::string> StepTheBodyAndEffects(Kernel::ActorState& actor, const CharacterStepContext& context)
{
    if (auto body = StepTheBody(actor, Kernel::ActorPoolSize, false, context); !body.HasValue())
    {
        return body;
    }
    const Terrain::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    return StepTheEffects(actor, Kernel::ActorPoolSize, false, context,
                          terrain.attributes ? &terrain.attributes->cells : nullptr);
}
} // namespace Client::Actors
