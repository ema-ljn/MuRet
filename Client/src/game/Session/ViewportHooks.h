#pragma once

#include "Actors/ViewportHooks.h"
#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The viewport's slots (Actors/ViewportHooks.h), each one in the module that owns the call. Route.h builds them per
// packet.
namespace Client::Session
{
class ViewportHooks final : public Actors::IViewportHooks
{
public:
    explicit ViewportHooks(const Scenes::WorldEntryResources& resources);

    [[nodiscard]] Core::Expected<void, std::string> StartTheTeleportAction(Kernel::ActorState& actor) const override;
    void CreateTheArrivalEffect(Kernel::ActorState& actor) const override;
    [[nodiscard]] Core::Expected<void, std::string> CreateTheKalimaBeam(Kernel::ActorState& actor) const override;
    [[nodiscard]] Core::Expected<void, std::string> RefreshTheSiegePanel() const override;

    [[nodiscard]] std::uint8_t PublishedHeroClass() const override;
    [[nodiscard]] Core::Expected<std::size_t, std::string>
    CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y, std::int32_t key) const override;
    [[nodiscard]] Core::Expected<void, std::string> CreateTheMagicCircle(Kernel::ActorState& actor) const override;
    [[nodiscard]] Core::Expected<void, std::string> DressThePublishedHero() const override;

    void ReleaseThePublishedHerosRaven() const override;
    [[nodiscard]] Core::Expected<void, std::string> RecreateTheRaven(Kernel::ActorState& actor) const override;
    void TurnOffTheHelpersOf(std::size_t rider) const override;
    [[nodiscard]] Core::Expected<void, std::string> CreateTheHelper(std::uint16_t type, const glm::vec3& position,
                                                                    std::size_t rider) const override;

private:
    const Scenes::WorldEntryResources& m_resources;
};

class ViewportLeaveHooks final : public Actors::IViewportLeaveHooks
{
public:
    explicit ViewportLeaveHooks(Kernel::WorldState& world);

    void ForgetTheSwitchHolder(std::uint16_t key) const override;
    void RemoveTheShopTitle(std::int32_t key) const override;

private:
    Kernel::WorldState& m_world;
};
} // namespace Client::Session
