#pragma once

#include "Actors/CharacterStepHooks.h"
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

// The characters' step slots (Actors/CharacterStepHooks.h), each one in the module that owns the call. They keep no
// state: whoever builds a step context can own one.
namespace Client::Session
{
class CharacterStepHooks final : public Actors::ICharacterStepHooks
{
public:
    [[nodiscard]] Core::Expected<void, std::string> RefreshTheSiegePanel(Kernel::WorldRuntime& runtime) const override;

    [[nodiscard]] Core::Expected<void, std::string>
    QueueTheGateName(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheOwnTimers(const Actors::CharacterStepContext& context) const override;
    void StepTheFallingBall(Kernel::ActorState& actor,
                            const std::function<double(float, float)>& height) const override;
    void StepTheAlpha(Kernel::ActorState& actor) const override;

    [[nodiscard]] bool TurnTheNpc(Kernel::ActorState& actor, std::int32_t map) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    SayTheNpcLine(const Kernel::ActorState& actor, std::size_t slot,
                  const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    SayTheNamedLine(const Kernel::ActorState& actor, std::size_t slot,
                    const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] bool IsInTheParty(const Kernel::WorldState& world, const Kernel::ActorState& actor) const override;
    [[nodiscard]] float ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame, float speed) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    FinishTheTurnedNpc(Kernel::ActorState& actor, std::size_t slot,
                       const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<Actors::ModelActionEnd, std::string>
    FinishTheModelAction(Kernel::ActorState& actor, const Models::Skeleton& skeleton, Kernel::WorldRuntime& runtime,
                         Crt::CrtRandom& random) const override;

    [[nodiscard]] Core::Expected<void, std::string>
    SetActorDie(Kernel::ActorState& actor, const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    CreateTheJoint(const Actors::StepJoint& joint, const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheAttack(Kernel::ActorState& actor, std::size_t slot, const Actors::CharacterStepContext& context,
                  const std::optional<glm::vec3>& flareLight) const override;
    void QueueTheHealthBar(const Kernel::ActorState& actor, std::deque<Kernel::QueuedHealthBar>& queue) const override;

    [[nodiscard]] Core::Expected<void, std::string>
    EmitEarlyMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                 const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheSwingsOf1abTo1b4(Kernel::ActorState& actor, std::size_t slot,
                            const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    EmitLateMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                const Actors::CharacterStepContext& context) const override;

    [[nodiscard]] Core::Expected<void, std::string>
    EmitTheFallingBallFire(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheRaven(Kernel::ActorState& actor, std::size_t slot,
                 const Actors::CharacterStepContext& context) const override;

    [[nodiscard]] Core::Expected<void, std::string>
    SetTheModelHeadTarget(Kernel::ActorState& actor, const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    AddTheHandLight(const glm::vec3& position, const glm::vec3& color, std::int32_t radius,
                    const Actors::CharacterStepContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheModelEffects(Kernel::ActorState& actor, std::size_t slot, const Models::Skeleton& skeleton,
                        const Actors::CharacterStepContext& context) const override;

    [[nodiscard]] Core::Expected<void, std::string>
    StepTheCastleObject(Kernel::ActorState& actor, const Actors::CharacterStepContext& context,
                        std::function<double(float, float)> height, const Models::Skeleton* skeleton) const override;
};
} // namespace Client::Session
