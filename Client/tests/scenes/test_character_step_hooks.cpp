#include "Actors/CharacterStep.h"
#include "Actors/CharacterStepHooks.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Models/Skeleton.h"
#include "Terrain/WorldTerrain.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Client::Actors::ICharacterStepHooks: what the characters' step (0x508f00) asks of other modules, slot by slot in
// the original's order, and the step that does not start without them.
namespace
{
using Client::Actors::CharacterStepContext;
using Client::Actors::ModelActionEnd;
using Client::Actors::StepJoint;
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;
using Client::Models::Skeleton;

constexpr std::uint32_t Seed = 0x508f00;
constexpr std::size_t HeroSlot = 0;
constexpr std::size_t OtherSlot = 1;
constexpr std::string_view NoHooksAddress = "0x508f00";

// Records each slot it is asked for and answers as if nothing were there; `failing` names the slot that stops.
class RecordingHooks final : public Client::Actors::ICharacterStepHooks
{
public:
    explicit RecordingHooks(std::string_view failing = {}) : m_failing(failing) {}

    [[nodiscard]] const std::vector<std::string>& Calls() const
    {
        return m_calls;
    }

    Expected<void, std::string> RefreshTheSiegePanel(WorldRuntime&) const override
    {
        return Record("RefreshTheSiegePanel");
    }
    bool CrywolfAltarGoal(const WorldState&) const override
    {
        static_cast<void>(Record("CrywolfAltarGoal"));
        return false;
    }
    Expected<void, std::string> QueueTheGateName(const ActorState&, const CharacterStepContext&) const override
    {
        return Record("QueueTheGateName");
    }
    Expected<void, std::string> StepTheOwnTimers(const CharacterStepContext&) const override
    {
        return Record("StepTheOwnTimers");
    }
    void StepTheFallingBall(ActorState&, const std::function<double(float, float)>&) const override
    {
        static_cast<void>(Record("StepTheFallingBall"));
    }
    void StepTheAlpha(ActorState&) const override
    {
        static_cast<void>(Record("StepTheAlpha"));
    }
    bool TurnTheNpc(ActorState&, std::int32_t) const override
    {
        static_cast<void>(Record("TurnTheNpc"));
        return false;
    }
    Expected<void, std::string> SayTheNpcLine(const ActorState&, std::size_t,
                                              const CharacterStepContext&) const override
    {
        return Record("SayTheNpcLine");
    }
    Expected<void, std::string> SayTheNamedLine(const ActorState&, std::size_t,
                                                const CharacterStepContext&) const override
    {
        return Record("SayTheNamedLine");
    }
    bool IsInTheParty(const WorldState&, const ActorState&) const override
    {
        static_cast<void>(Record("IsInTheParty"));
        return false;
    }
    float ModelSpeedOf(std::uint16_t, std::uint8_t, float, float speed) const override
    {
        static_cast<void>(Record("ModelSpeedOf"));
        return speed;
    }
    Expected<void, std::string> FinishTheTurnedNpc(ActorState&, std::size_t, const CharacterStepContext&) const override
    {
        return Record("FinishTheTurnedNpc");
    }
    Expected<ModelActionEnd, std::string> FinishTheModelAction(ActorState&, const Skeleton&, WorldRuntime&,
                                                               CrtRandom&) const override
    {
        if (auto recorded = Record("FinishTheModelAction"); !recorded.HasValue())
            return Client::Core::Unexpected(recorded.Error());
        return ModelActionEnd::Done;
    }
    Expected<void, std::string> SetActorDie(ActorState&, const CharacterStepContext&) const override
    {
        return Record("SetActorDie");
    }
    Expected<void, std::string> CreateTheJoint(const StepJoint&, const CharacterStepContext&) const override
    {
        return Record("CreateTheJoint");
    }
    Expected<void, std::string> StepTheAttack(ActorState&, std::size_t, const CharacterStepContext&,
                                              const std::optional<glm::vec3>&) const override
    {
        return Record("StepTheAttack");
    }
    void QueueTheHealthBar(const ActorState&, std::deque<Client::Kernel::QueuedHealthBar>&) const override
    {
        static_cast<void>(Record("QueueTheHealthBar"));
    }
    Expected<void, std::string> EmitEarlyMonsterRibbonTrails(ActorState&, const Skeleton*,
                                                             const CharacterStepContext&) const override
    {
        return Record("EmitEarlyMonsterRibbonTrails");
    }
    Expected<void, std::string> StepTheSwingsOf1abTo1b4(ActorState&, std::size_t,
                                                        const CharacterStepContext&) const override
    {
        return Record("StepTheSwingsOf1abTo1b4");
    }
    Expected<void, std::string> EmitLateMonsterRibbonTrails(ActorState&, const Skeleton*,
                                                            const CharacterStepContext&) const override
    {
        return Record("EmitLateMonsterRibbonTrails");
    }
    Expected<void, std::string> EmitTheFallingBallFire(const ActorState&, const CharacterStepContext&) const override
    {
        return Record("EmitTheFallingBallFire");
    }
    Expected<void, std::string> StepTheRaven(ActorState&, std::size_t, const CharacterStepContext&) const override
    {
        return Record("StepTheRaven");
    }
    Expected<void, std::string> SetTheModelHeadTarget(ActorState&, const CharacterStepContext&) const override
    {
        return Record("SetTheModelHeadTarget");
    }
    Expected<void, std::string> AddTheHandLight(const glm::vec3&, const glm::vec3&, std::int32_t,
                                                const CharacterStepContext&) const override
    {
        return Record("AddTheHandLight");
    }
    Expected<void, std::string> StepTheModelEffects(ActorState&, std::size_t, const Skeleton&,
                                                    const CharacterStepContext&) const override
    {
        return Record("StepTheModelEffects");
    }
    Expected<void, std::string> StepTheCastleObject(ActorState&, const CharacterStepContext&,
                                                    std::function<double(float, float)>, const Skeleton*) const override
    {
        return Record("StepTheCastleObject");
    }

private:
    Expected<void, std::string> Record(std::string_view slot) const
    {
        m_calls.emplace_back(slot);
        if (slot == m_failing)
            return Client::Core::Unexpected(std::string(slot) + " failed");
        return {};
    }

    std::string_view m_failing;
    mutable std::vector<std::string> m_calls;
};

// The hero standing in slot 0 of map 0, with Player.bmd's actions without keys (the body neither advances nor
// finishes) and the own record's equipment empty. Another player standing in slot 1 when asked. On the heap: the
// WorldState does not fit in Windows' 1 MB stack beside the test's frames.
struct StepWorld
{
    StepWorld()
    {
        world->Runtime().hero = HeroSlot;
        world->OwnCharacter().ClearEquipmentForEntry();
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Stand(HeroSlot, 1);
    }

    ActorState& Stand(std::size_t slot, std::uint8_t characterClass)
    {
        ActorState& actor = world->Actors()[slot];
        actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.animation.action = 1;
        return actor;
    }

    [[nodiscard]] CharacterStepContext Context(const Client::Actors::ICharacterStepHooks* hooks)
    {
        return {.world = *world, .random = random, .playerSkeleton = &skeleton, .terrain = &terrain, .hooks = hooks};
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

// 0x508f06, then 0x4fd5f0 with the hero: 0x4fc450 (0x4fd6fe), 0x4e9980 (0x4fd72e), 0x4fcd00's three (0x4fcd0c,
// 0x4fcd60, 0x4fcd65) and the speed (0x4f96d4), the attack (0x4fdb9a) and the health bar (0x5053fc); unseen, 0x505de0
// asks nothing, and 0x42da90 (0x508eed) comes last.
TEST(CharacterStepHooksTest, TheHeroAsksTheSlotsInTheOriginalsOrder)
{
    StepWorld tested;
    const RecordingHooks hooks;

    const auto stepped = Client::Actors::StepCharacters(tested.Context(&hooks));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const std::vector<std::string> expected{
        "RefreshTheSiegePanel", "StepTheOwnTimers", "StepTheAlpha",  "TurnTheNpc",        "SayTheNpcLine",
        "SayTheNamedLine",      "ModelSpeedOf",     "StepTheAttack", "QueueTheHealthBar", "StepTheCastleObject"};
    EXPECT_EQ(hooks.Calls(), expected);
}

// 0x508ba0 for anyone but the hero asks where the walk goes (Crywolf's altar) before the body, which does not step
// the own record's timers.
TEST(CharacterStepHooksTest, AnotherCharacterAsksForTheWalkFirstAndSkipsTheOwnTimers)
{
    StepWorld tested;
    tested.world->Actors()[HeroSlot].alive = false;
    tested.Stand(OtherSlot, 2);
    const RecordingHooks hooks;

    const auto stepped = Client::Actors::StepCharacters(tested.Context(&hooks));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const std::vector<std::string> expected{
        "RefreshTheSiegePanel", "CrywolfAltarGoal", "StepTheAlpha",  "TurnTheNpc",        "SayTheNpcLine",
        "SayTheNamedLine",      "ModelSpeedOf",     "StepTheAttack", "QueueTheHealthBar", "StepTheCastleObject"};
    EXPECT_EQ(hooks.Calls(), expected);
}

// A slot that fails stops the step there, with its error, and asks nothing after it.
TEST(CharacterStepHooksTest, AFailingSlotStopsTheStepWithItsError)
{
    StepWorld tested;
    const RecordingHooks hooks("SayTheNpcLine");

    const auto stepped = Client::Actors::StepCharacters(tested.Context(&hooks));

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), "SayTheNpcLine failed");
    ASSERT_FALSE(hooks.Calls().empty());
    EXPECT_EQ(hooks.Calls().back(), "SayTheNpcLine");
}

// Without the hooks none of the three entries starts: no rand() and nothing written.
TEST(CharacterStepHooksTest, WithoutHooksTheStepDoesNotStart)
{
    StepWorld tested;
    ActorState& hero = tested.world->Actors()[HeroSlot];
    hero.seen = true;
    const CharacterStepContext context = tested.Context(nullptr);
    const std::uint32_t before = tested.random.State();

    const auto all = Client::Actors::StepCharacters(context);
    const auto one = Client::Actors::StepCharacter(hero, context);
    const auto body = Client::Actors::StepTheBodyAndEffects(hero, context);

    for (const auto* stepped : {&all, &one, &body})
    {
        ASSERT_FALSE(stepped->HasValue());
        EXPECT_NE(stepped->Error().find(NoHooksAddress), std::string::npos) << stepped->Error();
    }
    EXPECT_EQ(tested.random.State(), before);
    EXPECT_TRUE(hero.seen);
}
} // namespace
