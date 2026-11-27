#include "Session/Route.h"

#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Kernel/PlayerKey.h"
#include "Network/Packet.h"
#include "Protocol/Movement/Movement.h"
#include "Scenes/WorldState.h"
#include "Session/ServerSession.h"
#include "Session/WorldEntry.h"

#include "support/BareWorld.h"
#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
using Client::App::SessionState;
using Client::Protocol::Movement::Walk;

struct Handled
{
    Walk walk;
    std::optional<Client::Kernel::PlayerKey> player;
    const Client::Scenes::WorldState* world = nullptr;
    const Client::Scenes::WorldEntryResources* resources = nullptr;
};

// What the handlers below were given, and the error the next one returns.
std::vector<Handled> handled;
std::optional<std::string> nextFailure;

Client::Core::Expected<void, std::string> HandleWithEveryPart(const Walk& walk, Client::Kernel::PlayerKey player,
                                                              Client::Scenes::WorldState& world,
                                                              const Client::Scenes::WorldEntryResources& resources)
{
    handled.push_back({walk, player, &world, &resources});
    if (nextFailure)
        return Client::Core::Unexpected(*nextFailure);
    return {};
}

void HandleTheWalkAlone(const Walk& walk)
{
    handled.push_back({.walk = walk, .player = std::nullopt, .world = nullptr, .resources = nullptr});
}

constexpr auto RouteEveryPart = &Client::Session::Route<"se ignora el movimiento de prueba",
                                                        &Client::Protocol::Movement::ReadWalk, &HandleWithEveryPart>;
constexpr auto RouteTheWalkAlone = &Client::Session::Route<"se ignora el movimiento de prueba",
                                                           &Client::Protocol::Movement::ReadWalk, &HandleTheWalkAlone>;

Client::Network::Packet WalkPacket()
{
    return Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 8, 0xd3, 0x12, 0x34, 7, 9, 0xf0}));
}

class RouteTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        handled.clear();
        nextFailure.reset();
        ASSERT_NO_FATAL_FAILURE(online.LogIn());
        online.eventSink.routedWorld = &world->resources;
    }

    [[nodiscard]] bool Warned(const std::string& text) const
    {
        return std::ranges::any_of(online.sink.lines,
                                   [&text](const std::string& line) { return line.find(text) != std::string::npos; });
    }

    std::unique_ptr<Client::Tests::BareWorld> world = std::make_unique<Client::Tests::BareWorld>();
    Client::Tests::LoggedInSession online;
};

TEST_F(RouteTest, HandsTheHandlerTheReadValueAndThePartsItNames)
{
    RouteEveryPart(online.session, WalkPacket());

    ASSERT_EQ(handled.size(), 1u);
    EXPECT_EQ(handled[0].walk.characterId, 0x1234);
    EXPECT_EQ(handled[0].walk.cell.x, 7);
    EXPECT_EQ(handled[0].walk.cell.y, 9);
    EXPECT_EQ(handled[0].walk.direction, 15);
    ASSERT_TRUE(handled[0].player.has_value());
    EXPECT_EQ(handled[0].player->Value(), online.session.PlayerIndex());
    EXPECT_EQ(handled[0].world, &world->world);
    EXPECT_EQ(handled[0].resources, &world->resources);
    EXPECT_EQ(online.eventSink.routedAfter, (std::vector<std::size_t>{0}));
    EXPECT_TRUE(online.eventSink.world.empty());
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

TEST_F(RouteTest, AHandlerThatReturnsNothingEndsItsReceptionToo)
{
    RouteTheWalkAlone(online.session, WalkPacket());

    ASSERT_EQ(handled.size(), 1u);
    EXPECT_EQ(handled[0].walk.characterId, 0x1234);
    EXPECT_EQ(online.eventSink.routedAfter, (std::vector<std::size_t>{0}));
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

TEST_F(RouteTest, AnUnreadablePacketReachesNoHandlerAndIsIgnoredWithAWarning)
{
    RouteEveryPart(online.session, Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 5, 0xd3, 0x12, 0x34})));

    EXPECT_TRUE(handled.empty());
    EXPECT_TRUE(online.eventSink.routedAfter.empty());
    EXPECT_TRUE(Warned("se ignora el movimiento de prueba: no se puede leer"));
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

TEST_F(RouteTest, WithoutAWorldThePacketReachesNoHandler)
{
    online.eventSink.routedWorld = nullptr;

    RouteEveryPart(online.session, WalkPacket());

    EXPECT_TRUE(handled.empty());
    EXPECT_TRUE(online.eventSink.routedAfter.empty());
    EXPECT_FALSE(Warned("se ignora el movimiento de prueba"));
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

TEST_F(RouteTest, AHandlerErrorFailsTheSessionAfterItsReceptionEnds)
{
    nextFailure = "the handler failed";
    std::optional<SessionState> stateAtTheEnd;
    online.eventSink.onRoutedReception = [&stateAtTheEnd](Client::App::ServerSession& session)
    { stateAtTheEnd = session.State(); };

    RouteEveryPart(online.session, WalkPacket());

    ASSERT_EQ(handled.size(), 1u);
    ASSERT_TRUE(stateAtTheEnd.has_value());
    EXPECT_NE(*stateAtTheEnd, SessionState::Failed);
    EXPECT_EQ(online.session.State(), SessionState::Failed);
    EXPECT_EQ(online.session.Failure(), Client::App::SessionFailure::ApplicationFailed);
    const auto polled = online.session.Poll();
    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "the handler failed");
}
} // namespace
