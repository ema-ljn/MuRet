#include "Movement/GameMovement.h"
#include "Movement/HeroMovementHooks.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Hud/GameUi.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Models/Skeleton.h"
#include "Pathfinding/PathCell.h"
#include "Protocol/Movement/Movement.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Terrain/WorldTerrain.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::Movement::IHeroMovementHooks: what the hero's step (MoveHero, 0x59adf0) asks of other modules, slot by slot
// in the original's order, and the step that does not start without them.
namespace
{
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;
using Client::Movement::HeroMovementContext;
using Client::Movement::StepHeroMovement;
using Client::Ui::PanelId;

constexpr std::int32_t ScreenWorldWidth = 640;
constexpr std::string_view NoHooksAddress = "0x59adf0";

std::string PanelCall(std::string_view slot, PanelId panel)
{
    return std::format("{}({:#x})", slot, panel);
}

// Records each slot it is asked for and answers as if nothing were there; `failing` names the slot that stops.
class RecordingHooks final : public Client::Movement::IHeroMovementHooks
{
public:
    explicit RecordingHooks(std::string_view failing = {}, bool tourRunning = false)
        : m_failing(failing), m_tourRunning(tourRunning)
    {
    }

    [[nodiscard]] const std::vector<std::string>& Calls() const
    {
        return m_calls;
    }

    bool CameraTourRunning(const WorldRuntime&) const override
    {
        static_cast<void>(Record("CameraTourRunning"));
        return m_tourRunning;
    }
    Client::Ui::OldWindowGlobals OldWindowGlobalsOf(WorldRuntime& runtime) const override
    {
        static_cast<void>(Record("OldWindowGlobalsOf"));
        return Client::Hud::OldWindowGlobalsOf(runtime);
    }
    std::int32_t WorldWidth(const HeroMovementContext&) const override
    {
        static_cast<void>(Record("WorldWidth"));
        return ScreenWorldWidth;
    }
    bool IsPanelOpen(const HeroMovementContext&, PanelId panel) const override
    {
        static_cast<void>(Record(PanelCall("IsPanelOpen", panel)));
        return false;
    }
    Expected<void, std::string> ClosePanel(const HeroMovementContext&, PanelId panel) const override
    {
        return Record(PanelCall("ClosePanel", panel));
    }
    void CloseTheSerialEntry(WorldRuntime&) const override
    {
        static_cast<void>(Record("CloseTheSerialEntry"));
    }
    Expected<bool, std::string> IsAttackable(const WorldState&, bool) const override
    {
        if (auto recorded = Record("IsAttackable"); !recorded.HasValue())
            return Client::Core::Unexpected(recorded.Error());
        return false;
    }
    Expected<void, std::string> TalkToTheNpc(ActorState&, const HeroMovementContext&) const override
    {
        return Record("TalkToTheNpc");
    }
    Expected<void, std::string> InteractOnArrival(ActorState&, const HeroMovementContext&) const override
    {
        return Record("InteractOnArrival");
    }
    Expected<void, std::string> UseTheHeroSkill(ActorState&, const HeroMovementContext&) const override
    {
        return Record("UseTheHeroSkill");
    }

private:
    Expected<void, std::string> Record(std::string slot) const
    {
        m_calls.push_back(slot);
        if (slot == m_failing)
            return Client::Core::Unexpected(slot + " failed");
        return {};
    }

    std::string m_failing;
    bool m_tourRunning = false;
    mutable std::vector<std::string> m_calls;
};

// A hero standing at 182, 115 of map 0, with the player's actions and an empty terrain.
class HeroOnTheGround
{
public:
    HeroOnTheGround()
    {
        world->Runtime().map = 0;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        ActorState& hero = world->Actors()[0];
        hero.alive = true;
        hero.type = Client::Tables::PlayerSkeletonModel;
        hero.cellX = hero.targetX = 182;
        hero.cellY = hero.targetY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Kernel::NoItemModel;
        hero.alpha = 1.0f;
        hero.byte88 = 1;
        hero.baseMovementSpeed = 10;
        skeleton.actions.resize(205);
        for (auto& action : skeleton.actions)
            action.keyCount = 10;
    }

    [[nodiscard]] HeroMovementContext Context(const Client::Movement::IHeroMovementHooks* hooks)
    {
        return {.world = *world,
                .random = random,
                .playerSkeleton = &skeleton,
                .skills = &skills,
                .terrain = &terrain,
                .hooks = hooks};
    }

    // A click on 179, 116 that the window takes (0x6c8dcc already reached), with a walk that is sent.
    [[nodiscard]] HeroMovementContext GroundClick(const Client::Movement::IHeroMovementHooks* hooks)
    {
        world->Runtime().mouse.updateTime = 6;
        HeroMovementContext context = Context(hooks);
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget = []() -> Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
        { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
        context.sendWalk = [](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                              float) -> Expected<void, std::string> { return {}; };
        return context;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    Client::Models::Skeleton skeleton;
    Client::Tables::SkillTable skills{std::vector<std::byte>(Client::Tables::SkillTableSize)};
};

// 0x59ae24, the world's width (0x59b188), the second guild window (0x59b357) and, without a click, the skill
// (0x59e255).
TEST(HeroMovementHooksTest, AnIdleStepAsksTheSlotsInTheOriginalsOrder)
{
    HeroOnTheGround tested;
    const RecordingHooks hooks;

    const auto stepped = StepHeroMovement(tested.Context(&hooks));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const std::vector<std::string> expected{"CameraTourRunning", "WorldWidth",
                                            PanelCall("IsPanelOpen", Client::Ui::SecondGuildPanel), "UseTheHeroSkill"};
    EXPECT_EQ(hooks.Calls(), expected);
}

// The click on the ground outside the safe zone: 0x558bb0, the trainer (0x56acf7) and the panels the walk closes, in
// 0x56b397..0x56b4d6's order, before the skill.
TEST(HeroMovementHooksTest, AGroundClickAsksForThePanelsTheWalkCloses)
{
    HeroOnTheGround tested;
    const RecordingHooks hooks;

    const auto stepped = StepHeroMovement(tested.GroundClick(&hooks));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    std::vector<std::string> expected{"CameraTourRunning", "WorldWidth",
                                      PanelCall("IsPanelOpen", Client::Ui::SecondGuildPanel), "IsAttackable",
                                      PanelCall("IsPanelOpen", Client::Ui::TrainerPanel)};
    for (const PanelId panel : {Client::Ui::TradePanel, Client::Ui::WarehousePanel, Client::Ui::SecondGuildPanel,
                                Client::Ui::ChaosBoxPanel, Client::Ui::QuestPanel, Client::Ui::Panel2000,
                                Client::Ui::NpcShopPanel, Client::Ui::CastleSiegePanel, Client::Ui::EventNpcPanel})
        expected.push_back(PanelCall("IsPanelOpen", panel));
    expected.emplace_back("UseTheHeroSkill");
    EXPECT_EQ(hooks.Calls(), expected);
    EXPECT_EQ(tested.world->Hero().movement, 1);
}

// 0x59ae24: during Crywolf's camera tour the hero stops and the step asks nothing else.
TEST(HeroMovementHooksTest, TheCameraTourStopsTheStepAfterItsSlot)
{
    HeroOnTheGround tested;
    const RecordingHooks hooks({}, true);

    const auto stepped = StepHeroMovement(tested.GroundClick(&hooks));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(hooks.Calls(), std::vector<std::string>{"CameraTourRunning"});
}

// A slot that fails stops the step there, with its error, and asks nothing after it.
TEST(HeroMovementHooksTest, AFailingSlotStopsTheStepWithItsError)
{
    HeroOnTheGround tested;
    const RecordingHooks hooks("IsAttackable");

    const auto stepped = StepHeroMovement(tested.GroundClick(&hooks));

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), "IsAttackable failed");
    ASSERT_FALSE(hooks.Calls().empty());
    EXPECT_EQ(hooks.Calls().back(), "IsAttackable");
}

// Without the hooks the step does not start: no rand(), and the click's edge is not latched.
TEST(HeroMovementHooksTest, WithoutHooksTheStepDoesNotStart)
{
    HeroOnTheGround tested;
    const HeroMovementContext context = tested.GroundClick(nullptr);
    const std::uint32_t before = tested.random.State();

    const auto stepped = StepHeroMovement(context);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find(NoHooksAddress), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.random.State(), before);
    EXPECT_FALSE(tested.world->Runtime().mouse.pendingClick);
}
} // namespace
