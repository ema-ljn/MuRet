#include "Scenes/ModelBody.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Render/Models/Animation.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x4f96e2..0x4f9737.
constexpr std::uint16_t BlinkingModel = 0x192;
constexpr std::uint16_t SlowDyingModel = 0x1a9;
constexpr std::uint8_t DyingAction = 6;
constexpr float LateFrame = 6.0f;         // 0x6b87f4
constexpr float FasterLateFactor = 4.0f;  // 0x6b8a44
constexpr float SlowerDyingFactor = 0.5f; // 0x6b882c

// 0x4fcbb8 y 0x4fce68: el tipo que no pasa por 0x4ea030.
constexpr std::uint16_t NamedModel = 0xc5;
// 0x4fcbef..0x4fcbfc: en el mapa 1 (Dungeon), el 0x28.
using Maps::DungeonMap;
constexpr std::uint16_t DungeonModel = 0x28;
// 0x4fcc11..0x4fcc1b: los monstruos, de Monster01 (0x152) a antes del 0x1ca.
constexpr std::uint16_t FirstMonsterModel = 0x152;
constexpr std::uint16_t EndOfMonsterModels = 0x1ca;
// 0x4fcc48..0x4fcc62: las acciones que vuelven a la 0.
constexpr std::array<std::uint8_t, 7> RestingActions{1, 5, 3, 4, 8, 9, 0xb};
// 0x4fcc7a..0x4fcc86: el 0x1ce y el 0x1cf.
constexpr std::uint16_t FirstTalkingNpc = 0x1ce;
constexpr std::uint16_t LastTalkingNpc = 0x1cf;
constexpr int TalkingNpcSpan = 16;    // 0x4fcc9f
constexpr int TalkingNpcResting = 12; // 0x4fccab

// 0x42d893..0x42d8c5: el mapa 30 y la tabla de bytes 0x42d9f8 sobre la de saltos 0x42d9e8, con +0x376 − 221.
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
constexpr std::uint32_t CarriedFlag = 0x20000; // 0x42d99d

bool IsOneOf(std::span<const std::uint8_t> values, std::uint8_t value)
{
    for (const std::uint8_t candidate : values)
        if (candidate == value)
            return true;
    return false;
}

// 0x4ea030 con el modelo del personaje.
void SetModelAction(ActorState& actor, const Render::Models::Skeleton& skeleton, std::uint8_t action)
{
    Render::Models::SetAction(actor.animation, skeleton, action);
}

// Lo que 0x42d890 deja de la acción: la anterior (+0x112), la key (+0x118), la acción (+0x111) y el frame (+0x114), sin
// 0x4ea030.
void PutTheAction(ActorState& actor, std::uint8_t action)
{
    actor.animation.previousAction = action;
    actor.animation.previousKey = 0;
    actor.animation.action = action;
    actor.animation.frame = 0.0f;
}

// 0x42d890(c, c): fuera del mapa 30, 0. En el 30, por +0x376.
bool FinishTheCastleObject(ActorState& actor, const Render::Models::Skeleton& skeleton, WorldRuntime& runtime)
{
    if (runtime.map != CastleMap)
        return false;
    switch (actor.word376)
    {
    case 221: // 0x42d981
    case 222:
        SetModelAction(actor, skeleton, 0);
        return true;
    case 277: // 0x42d995
        if ((actor.flags78 & CarriedFlag) == CarriedFlag)
        {
            actor.byte42c = 0;
            SetModelAction(actor, skeleton, 2);
        }
        else if (actor.flags78 == 0)
        {
            actor.byte42c = 1;
            SetModelAction(actor, skeleton, 0);
        }
        return true;
    case 286: // 0x42d8cc
    case 287:
    {
        constexpr std::uint8_t OpenedAction = 4;
        constexpr std::uint8_t OpeningAction = 5;
        constexpr std::uint8_t ClosingAction = 2;
        const std::uint8_t action = actor.animation.action;
        if (action == OpeningAction)
        {
            PutTheAction(actor, OpenedAction);
            constexpr std::uint8_t Opened = 2;
            constexpr std::int32_t OpenedTime = 0x12c;
            runtime.castleByte7239d8 = Opened;
            actor.byte110 = 1;
            actor.dword100 = OpenedTime;
            return true;
        }
        if (action == OpenedAction || action == ClosingAction)
        {
            PutTheAction(actor, OpenedAction);
            return true;
        }
        if (action == 0)
        {
            PutTheAction(actor, 0);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

} // namespace

float ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame, float speed)
{
    if (IsNewFace(type))
        return speed + speed;
    if (type == BlinkingModel && action == DyingAction && frame > LateFrame)
        return speed * FasterLateFactor;
    if (type == SlowDyingModel && action == DyingAction)
        return speed * SlowerDyingFactor;
    return speed;
}

Core::Expected<ModelActionEnd, std::string> FinishTheModelAction(ActorState& actor,
                                                                 const Render::Models::Skeleton& skeleton,
                                                                 WorldRuntime& runtime, Crt::CrtRandom& random)
{
    const std::uint16_t type = actor.type;
    if (type == NamedModel)
    {
        actor.animation.action = 1;
        return ModelActionEnd::Done;
    }
    if (FinishTheCastleObject(actor, skeleton, runtime))
        return ModelActionEnd::AdvanceAgain;
    if (runtime.map == DungeonMap && type == DungeonModel)
    {
        SetModelAction(actor, skeleton, 0);
        return ModelActionEnd::Done;
    }
    if (type >= FirstMonsterModel && type < EndOfMonsterModels)
    {
        const std::uint8_t action = actor.animation.action;
        // 0x4fcc1d..0x4fcc3c: +0x38a en 1 (el ECX en 1 de 0x4fcbd5) antes de 0x52e810, que corre quien llama.
        if (action == DyingAction)
        {
            if (actor.byte38a != 0)
                return ModelActionEnd::Done;
            actor.byte38a = 1;
            return ModelActionEnd::LeaveRemains;
        }
        if (IsOneOf(RestingActions, action))
            SetModelAction(actor, skeleton, 0);
        return ModelActionEnd::Done;
    }
    if (type >= FirstTalkingNpc && type <= LastTalkingNpc)
    {
        if (random.Next() % TalkingNpcSpan < TalkingNpcResting)
        {
            SetModelAction(actor, skeleton, 0);
            return ModelActionEnd::Done;
        }
        SetModelAction(actor, skeleton, static_cast<std::uint8_t>(1 + random.Next() % 2));
        return ModelActionEnd::Done;
    }
    if (IsNewFace(type))
        return ModelActionEnd::Done;
    SetModelAction(actor, skeleton, static_cast<std::uint8_t>(random.Next() % 2));
    return ModelActionEnd::Done;
}

void LookAtTheTarget(ActorState& actor, const ActorState& target)
{
    const auto heading = static_cast<float>(
        Render::Models::CreateAngle(actor.position.x, actor.position.y, target.position.x, target.position.y));
    // 0x4ea11a..0x4ea128: C0 en 1 (menor o sin orden, NaN) mira.
    constexpr double FacingLimit = 90.0; // 0x6b8948
    const double distance = Render::Models::AngleDistance(actor.angles.z, heading, 1);
    if (!(distance < FacingLimit) && !std::isnan(distance))
    {
        actor.headTurn.target = {};
        return;
    }
    constexpr double HeadHeight = 50.0;               // 0x6b86b0
    constexpr auto Share = static_cast<double>(0.2f); // 0x6b8854
    constexpr float FullTurn = Math::FullTurnAsFloat; // 0x6b869c
    actor.headTurn.target.x = static_cast<float>(static_cast<double>(actor.angles.z) - static_cast<double>(heading));
    actor.headTurn.target.y = static_cast<float>(
        (static_cast<double>(target.position.z) - (static_cast<double>(actor.position.z) + HeadHeight)) * Share);
    for (int axis = 0; axis < 2; ++axis)
        if (actor.headTurn.target[axis] < 0.0f)
            actor.headTurn.target[axis] += FullTurn;
}

Core::Expected<void, std::string> SetTheModelHeadTarget(ActorState& actor, std::span<const ActorState> pool,
                                                        Crt::CrtRandom& random)
{
    const std::uint8_t action = actor.animation.action;
    if (action == DyingAction)
        return {};
    if (action == 0)
    {
        constexpr int OneIn = 32;
        if (random.Next() % OneIn != 0)
            return {};
        constexpr int TurnSpan = 128;
        constexpr int TurnCentre = 64;
        constexpr int NodSpan = 48;
        constexpr int NodCentre = 16;
        constexpr float FullTurn = Math::FullTurnAsFloat;
        actor.headTurn.target.x = static_cast<float>((random.Next() % TurnSpan) - TurnCentre);
        actor.headTurn.target.y = static_cast<float>((random.Next() % NodSpan) - NodCentre);
        for (int axis = 0; axis < 2; ++axis)
            if (actor.headTurn.target[axis] < 0.0f)
                actor.headTurn.target[axis] += FullTurn;
        return {};
    }
    constexpr std::uint8_t TrackingAction = 2;
    constexpr std::uint16_t NoTarget = 0xffff;
    if (action == TrackingAction && actor.attackTarget != NoTarget)
    {
        // 0x4ea236: MOVSX, el lugar con signo sobre 0x743a16c.
        const auto target = static_cast<std::int16_t>(actor.attackTarget);
        if (target < 0 || static_cast<std::size_t>(target) >= pool.size())
            return Core::Unexpected(
                std::format("0x4ea24c: el blanco +0x3a4 ({}) cae fuera de los {} personajes", target, pool.size()));
        LookAtTheTarget(actor, pool[static_cast<std::size_t>(target)]);
        return {};
    }
    actor.headTurn.target = {};
    return {};
}

} // namespace Client::Scenes
