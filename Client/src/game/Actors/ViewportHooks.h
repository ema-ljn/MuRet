#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// What the viewport's handlers (Actors/In/) ask of other modules, one slot per call of the original, listed in the
// order each handler makes them. Session::ViewportHooks fills the slots.
namespace Client::Actors
{
class IViewportHooks
{
public:
    virtual ~IViewportHooks() = default;

    // The 0x12 (0x4c8860).
    // 0x4c8a3e: 0x53d930 on a player of state 1.
    [[nodiscard]] virtual Core::Expected<void, std::string> StartTheTeleportAction(Kernel::ActorState& actor) const = 0;
    // 0x4c905f: effect 0x61f on a player just arrived, with it as owner.
    virtual void CreateTheArrivalEffect(Kernel::ActorState& actor) const = 0;
    // 0x4c91dc: in Kalima, joint 0x610/8 with the player as owner.
    [[nodiscard]] virtual Core::Expected<void, std::string> CreateTheKalimaBeam(Kernel::ActorState& actor) const = 0;
    // 0x412140, the siege's panel.
    [[nodiscard]] virtual Core::Expected<void, std::string> RefreshTheSiegePanel() const = 0;

    // The 0x45 (0x4c92a0).
    // 0x4c9328: the published hero's class.
    [[nodiscard]] virtual std::uint8_t PublishedHeroClass() const = 0;
    // 0x5141e0(type, X, Y, key).
    [[nodiscard]] virtual Core::Expected<std::size_t, std::string>
    CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y, std::int32_t key) const = 0;
    // 0x4c965c: the magic circle 0xec with the character as owner.
    [[nodiscard]] virtual Core::Expected<void, std::string> CreateTheMagicCircle(Kernel::ActorState& actor) const = 0;
    // 0x512a30 on the published hero.
    [[nodiscard]] virtual Core::Expected<void, std::string> DressThePublishedHero() const = 0;

    // The 0x25 (0x4c8380).
    // 0x4c8438: emptying the left hand releases the published hero's raven.
    virtual void ReleaseThePublishedHerosRaven() const = 0;
    // 0x420d60: filling it arms it again.
    [[nodiscard]] virtual Core::Expected<void, std::string> RecreateTheRaven(Kernel::ActorState& actor) const = 0;
    // 0x443d40, for an empty pet.
    virtual void TurnOffTheHelpersOf(std::size_t rider) const = 0;
    // 0x443fa0, the pet's helper.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    CreateTheHelper(std::uint16_t type, const glm::vec3& position, std::size_t rider) const = 0;
};

// What the 0x14 (0x4ca780) asks for each key, before and after it removes the character.
class IViewportLeaveHooks
{
public:
    virtual ~IViewportLeaveHooks() = default;

    // Q-C802: the siege's switch holder, with the whole key.
    virtual void ForgetTheSwitchHolder(std::uint16_t key) const = 0;
    // 0x4ca7f0..0x4ca809: the personal shop sign with the key, if there is one (0x451630 and 0x451430).
    virtual void RemoveTheShopTitle(std::int32_t key) const = 0;
};
} // namespace Client::Actors
