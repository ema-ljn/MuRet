#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
struct WorldRuntime;
class WorldState;
struct ActorState;
struct QueuedHealthBar;
} // namespace Client::Kernel

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Actors
{
struct CharacterStepContext;

// What 0x4fcbb0 tells 0x4fcd00 to do after the end of another model's action.
enum class ModelActionEnd : std::uint8_t
{
    Done,
    // 0x4fcbe3: 0x42d890 returned 1, so 0x4f9580 again.
    AdvanceAgain,
    // 0x4fcc3c: a monster's 6 with +0x38a at 0 leaves its remains (0x52e810).
    LeaveRemains,
};

// A joint the step creates with 0x530860, without a key, a skill or a target: the bones' 0x614/7 (0x4fdabf) and the
// charged skill's 0x619/8 (0x518aa8). The points and the angles are lent for the call only.
struct StepJoint
{
    std::int32_t type;
    glm::vec3& first;
    glm::vec3& second;
    glm::vec3& angles;
    std::int32_t subtype;
    Kernel::ActorState* owner;
    float width;
};

// What the characters' step (0x508f00) asks of other modules, one slot per call of the original, in the order the step
// makes them. Session::CharacterStepHooks fills the slots.
class ICharacterStepHooks
{
public:
    virtual ~ICharacterStepHooks() = default;

    // 0x508f06: 0x412140, the siege's panel, before the loop.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    RefreshTheSiegePanel(Kernel::WorldRuntime& runtime) const = 0;

    // The body (0x4fd5f0).
    // 0x4fd603..0x4fd698: the 0xc5's name to 0x4381f0.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    QueueTheGateName(const Kernel::ActorState& actor, const CharacterStepContext& context) const = 0;
    // 0x4fd6fe: 0x4fc450, the own record's timers, with the hero.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    StepTheOwnTimers(const CharacterStepContext& context) const = 0;
    // 0x4fd70e: 0x4fb910, the 0xf1 that falls.
    virtual void StepTheFallingBall(Kernel::ActorState& actor,
                                    const std::function<double(float, float)>& height) const = 0;
    // 0x4fd72e: 0x4e9980, the alpha toward its goal and the blendMesh light.
    virtual void StepTheAlpha(Kernel::ActorState& actor) const = 0;

    // The animation (0x4fcd00).
    // 0x4fcd0c..0x4fcd55: the NPC that turns on maps 0 and 2.
    [[nodiscard]] virtual bool TurnTheNpc(Kernel::ActorState& actor, std::int32_t map) const = 0;
    // 0x4fcd60: 0x4fc790, the NPC's message.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    SayTheNpcLine(const Kernel::ActorState& actor, std::size_t slot, const CharacterStepContext& context) const = 0;
    // 0x4fcd65..0x4fce15: the name message of 0x6c88b0.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    SayTheNamedLine(const Kernel::ActorState& actor, std::size_t slot, const CharacterStepContext& context) const = 0;
    // 0x4f969d..0x4f96ba: 0x59e680, whether the character is in the party.
    [[nodiscard]] virtual bool IsInTheParty(const Kernel::WorldState& world, const Kernel::ActorState& actor) const = 0;
    // 0x4f96d4..0x4f9737: the speed of the other types.
    [[nodiscard]] virtual float ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame,
                                             float speed) const = 0;
    // 0x4fce3f..0x4fce57: 0x4fc8c0, the end of the NPC that turned.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    FinishTheTurnedNpc(Kernel::ActorState& actor, std::size_t slot, const CharacterStepContext& context) const = 0;
    // 0x4fce5e: 0x4fcbb0, the end of another model's action.
    [[nodiscard]] virtual Core::Expected<ModelActionEnd, std::string>
    FinishTheModelAction(Kernel::ActorState& actor, const Models::Skeleton& skeleton, Kernel::WorldRuntime& runtime,
                         Crt::CrtRandom& random) const = 0;

    // The body again.
    // 0x4fd77b..0x4fd7dc: 0x4f5660, dying again.
    [[nodiscard]] virtual Core::Expected<void, std::string> SetActorDie(Kernel::ActorState& actor,
                                                                        const CharacterStepContext& context) const = 0;
    // 0x530860, from the bones (0x4fdabf) and from the charged skill (0x518aa8).
    [[nodiscard]] virtual Core::Expected<void, std::string>
    CreateTheJoint(const StepJoint& joint, const CharacterStepContext& context) const = 0;
    // 0x4fdb9a..0x50539e: the attack, with 0x4fd5f0's local light, or the flare's (0x4fdb3f) when it made one.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    StepTheAttack(Kernel::ActorState& actor, std::size_t slot, const CharacterStepContext& context,
                  const std::optional<glm::vec3>& flareLight) const = 0;
    // 0x5053fc: 0x42a2b0, the health bar.
    virtual void QueueTheHealthBar(const Kernel::ActorState& actor,
                                   std::deque<Kernel::QueuedHealthBar>& queue) const = 0;

    // The weapon trail (0x4fcee0), after the player's.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    EmitEarlyMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                 const CharacterStepContext& context) const = 0;
    // 0x4fd5d7: 0x432bd0.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    StepTheSwingsOf1abTo1b4(Kernel::ActorState& actor, std::size_t slot, const CharacterStepContext& context) const = 0;
    [[nodiscard]] virtual Core::Expected<void, std::string>
    EmitLateMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                const CharacterStepContext& context) const = 0;

    // The end of the body.
    // 0x505425..0x505444: 0x52f360, the 0xf1's class 0 fire.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    EmitTheFallingBallFire(const Kernel::ActorState& actor, const CharacterStepContext& context) const = 0;
    // 0x50544b: 0x420dd0, the raven of +0x430.
    [[nodiscard]] virtual Core::Expected<void, std::string> StepTheRaven(Kernel::ActorState& actor, std::size_t slot,
                                                                         const CharacterStepContext& context) const = 0;

    // The effects (0x505de0).
    // 0x5068da..0x5068e8: 0x4ea190, where another model's head looks.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    SetTheModelHeadTarget(Kernel::ActorState& actor, const CharacterStepContext& context) const = 0;
    // 0x506b50: 0x5f9780, a hand's light.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    AddTheHandLight(const glm::vec3& position, const glm::vec3& color, std::int32_t radius,
                    const CharacterStepContext& context) const = 0;
    // 0x506cac..0x506cf1: another model's case, or the helpers of 0x5085db.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    StepTheModelEffects(Kernel::ActorState& actor, std::size_t slot, const Models::Skeleton& skeleton,
                        const CharacterStepContext& context) const = 0;

    // 0x508eed: 0x42da90, the siege's objects on map 30.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    StepTheCastleObject(Kernel::ActorState& actor, const CharacterStepContext& context,
                        std::function<double(float, float)> height, const Models::Skeleton* skeleton) const = 0;
};
} // namespace Client::Actors
