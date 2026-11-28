#include "Session/ViewportHooks.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Scenes/BlastEffectCalls.h"
#include "Scenes/CastleBattleReceive.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperTable.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SiegeBattleUi.h"
#include "Scenes/SocialState.h"
#include "Session/WorldEntry.h"
#include "Session/WorldTransitionSupport.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Session
{
namespace
{
// The −1 key of the effects the viewport creates.
constexpr std::uint16_t NoEffectKey = 0xffff;
} // namespace

ViewportHooks::ViewportHooks(const Scenes::WorldEntryResources& resources) : m_resources(resources) {}

Core::Expected<void, std::string> ViewportHooks::StartTheTeleportAction(Kernel::ActorState& actor) const
{
    return Scenes::StartTheTeleportAction(actor, m_resources);
}

void ViewportHooks::CreateTheArrivalEffect(Kernel::ActorState& actor) const
{
    static_cast<void>(m_resources.world.Map().effects.Create(World::ModelEffectArguments{.type = World::ArrivalEffect,
                                                                                         .position = actor.position,
                                                                                         .angles = actor.angles,
                                                                                         .light = actor.light,
                                                                                         .owner = &actor,
                                                                                         .key = NoEffectKey,
                                                                                         .scale = 0.0f}));
}

// 0x530860(0x610, position, position, angles, 8, player, 20, −1, 0, 0, −1).
Core::Expected<void, std::string> ViewportHooks::CreateTheKalimaBeam(Kernel::ActorState& actor) const
{
    constexpr std::int32_t KalimaBeamJoint = 0x610, KalimaBeamSubtype = 8, NoKey = -1, NoTarget = -1;
    constexpr float KalimaBeamWidth = 20.0f;
    const auto beam = Scenes::CreateFaunaJoint(m_resources.world.DrawTail().joints,
                                               {KalimaBeamJoint, actor.position, actor.position, actor.angles,
                                                KalimaBeamSubtype, &actor, KalimaBeamWidth, NoKey, 0, 0, NoTarget},
                                               m_resources.cloth.random, Scenes::JointOwnerKind::Actor);
    if (!beam.HasValue())
        return Core::Unexpected(beam.Error());
    return {};
}

Core::Expected<void, std::string> ViewportHooks::RefreshTheSiegePanel() const
{
    return Scenes::RefreshTheSiegePanel(m_resources.world.Runtime());
}

std::uint8_t ViewportHooks::PublishedHeroClass() const
{
    return Scenes::PublishedHero(m_resources).characterClass;
}

Core::Expected<std::size_t, std::string> ViewportHooks::CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y,
                                                                      std::int32_t key) const
{
    return Scenes::CreateMonster(type, x, y, key, m_resources);
}

// 0x518cf0(0xec, +0x10, +0x1c, +0xf4, 0, the character, −1, 0, 0, 0, 0), without a rand().
Core::Expected<void, std::string> ViewportHooks::CreateTheMagicCircle(Kernel::ActorState& actor) const
{
    constexpr std::int16_t MagicCircle = 0xec;
    const auto circle = Scenes::CreateBlastEffectOutsideTheStep({.type = MagicCircle,
                                                                 .position = actor.position,
                                                                 .angles = actor.angles,
                                                                 .light = actor.light,
                                                                 .subtype = 0,
                                                                 .key = NoEffectKey},
                                                                &actor, m_resources.world, m_resources.cloth.random);
    if (!circle.HasValue())
        return Core::Unexpected(circle.Error());
    return {};
}

Core::Expected<void, std::string> ViewportHooks::DressThePublishedHero() const
{
    return Scenes::DressTransitionHero(Scenes::PublishedHero(m_resources), m_resources);
}

void ViewportHooks::ReleaseThePublishedHerosRaven() const
{
    Scenes::PublishedHero(m_resources).darkSpirit.reset();
}

Core::Expected<void, std::string> ViewportHooks::RecreateTheRaven(Kernel::ActorState& actor) const
{
    return Scenes::RecreateTransitionRaven(actor, m_resources);
}

void ViewportHooks::TurnOffTheHelpersOf(std::size_t rider) const
{
    Kernel::TurnOffHelpersOf(m_resources.world.Helpers(), rider);
}

Core::Expected<void, std::string> ViewportHooks::CreateTheHelper(std::uint16_t type, const glm::vec3& position,
                                                                 std::size_t rider) const
{
    const auto created = Scenes::CreateHelper(m_resources.world, type, position, rider, &m_resources.cloth.random);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return {};
}

ViewportLeaveHooks::ViewportLeaveHooks(Kernel::WorldState& world) : m_world(world) {}

void ViewportLeaveHooks::ForgetTheSwitchHolder(std::uint16_t key) const
{
    Scenes::ForgetTheSwitchHolder(m_world.Runtime(), key);
}

void ViewportLeaveHooks::RemoveTheShopTitle(std::int32_t key) const
{
    Scenes::PersonalShopState& shops = m_world.Runtime().social.personalShop;
    if (const std::optional<std::size_t> owner = Scenes::ShopTitleWithKey(shops, key))
        Scenes::RemoveShopTitle(shops, *owner);
}
} // namespace Client::Session
