#include "Session/ServerSession.h"

#include "Session/SessionState.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Network/LinkState.h"
#include "Network/PacketSerials.h"
#include "Network/ProtocolError.h"
#include "Protocol/Weather/Weather.h"
#include "Models/Cloth.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Weather/LeafWeather.h"

#include "support/BareWorld.h"
#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/MinimumSize.h"
#include "support/Packets.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Tests::Bytes;
using Client::Tests::PacketOf;

TEST(WeatherProtocolTest, KeepsTheWholeWeatherByteIncludingUnknownHighNibbles)
{
    for (int code = 0; code <= 0xff; ++code)
    {
        const auto weather = Client::Protocol::Weather::ReadWeather(PacketOf(Bytes({0xc1, 4, 0x0f, code})));
        ASSERT_TRUE(weather.HasValue()) << code;
        EXPECT_EQ(weather.Value().code, code);
    }
}

TEST(WeatherProtocolTest, RejectsWrongOpcodesAndContainersBeforeReadingTheField)
{
    for (auto bytes : {Bytes({0xc1, 3, 0x0e}), Bytes({0xc2, 0, 5, 0x0f, 0x17}), Bytes({0xc3, 4, 0x0f, 0x17}),
                       Bytes({0xc4, 0, 5, 0x0f, 0x17})})
    {
        EXPECT_EQ(Client::Tests::ErrorOf(Client::Protocol::Weather::ReadWeather(PacketOf(std::move(bytes)))),
                  Client::Network::ProtocolError::WrongPacket);
    }
}

TEST(WeatherProtocolTest, RequiresTheWeatherByte)
{
    EXPECT_EQ(Client::Tests::ErrorOf(Client::Protocol::Weather::ReadWeather(PacketOf(Bytes({0xc1, 3, 0x0f})))),
              Client::Network::ProtocolError::TooShort);
}

TEST(WeatherProtocolTest, IgnoresTrailingBytes)
{
    const auto weather = Client::Protocol::Weather::ReadWeather(PacketOf(Bytes({0xc1, 6, 0x0f, 0x1f, 0x00, 0xff})));
    ASSERT_TRUE(weather.HasValue());
    EXPECT_EQ(weather.Value().code, 0x1f);
}

// The session with the weather's row routed to a world without Data, and the target after each routed reception.
struct WeatherHarness
{
    WeatherHarness()
    {
        online.eventSink.routedWorld = &bare->resources;
        online.eventSink.onRoutedReception = [this](Client::App::ServerSession&)
        { targets.push_back(world.Runtime().leafWeather.target); };
    }

    std::unique_ptr<Client::Tests::BareWorld> bare = std::make_unique<Client::Tests::BareWorld>();
    Client::Kernel::WorldState& world = bare->world;
    Client::Tests::LoggedInSession online;
    std::vector<std::int32_t> targets;
};

TEST(WeatherApplierTest, StartsWithTheStaticTargetAndCounterAndTheDataLeafCount)
{
    const Client::Kernel::WorldRuntime runtime;
    EXPECT_EQ(runtime.leafWeather.target, 0);
    EXPECT_EQ(runtime.leafWeather.current, 0);
    EXPECT_EQ(runtime.leafWeather.firstWave, 30); // .data[6c8c5c] = 1e 00 00 00.
    EXPECT_EQ(runtime.leafWeather.secondWave, 0);
    EXPECT_EQ(runtime.leafWeather.phase, 0);
}

// 0x4e36c4/0x4e36ed write a single DWORD; 0x4e36dd jumps to the default for the other 224 bytes.
TEST(WeatherApplierTest, AllThreeBranchesChangeOnlyTheTarget)
{
    const Client::Weather::LeafWeather before{
        .target = 777, .current = -123, .firstWave = 45, .secondWave = -67, .phase = 890};
    constexpr std::array<std::int32_t, 16> RainTargets{0, 6, 12, 18, 24, 30, 36, 42, 48, 54, 60, 66, 72, 78, 84, 90};
    for (int code = 0; code <= 0xff; ++code)
    {
        SCOPED_TRACE(code);
        Client::Weather::LeafWeather after = before;

        Client::Weather::ApplyWeather(Client::Protocol::Weather::Weather{static_cast<std::uint8_t>(code)}, after);

        const auto expected = code < 16 ? 0 : code < 32 ? RainTargets[static_cast<std::size_t>(code - 16)] : 777;
        EXPECT_EQ(after.target, expected);
        EXPECT_EQ(after.current, before.current);
        EXPECT_EQ(after.firstWave, before.firstWave);
        EXPECT_EQ(after.secondWave, before.secondWave);
        EXPECT_EQ(after.phase, before.phase);
    }
}

// Each of the 256 codes through the row: the world's CRT draws nothing and the session stays logged in.
TEST(WeatherSessionTest, NoCodeConsumesARandom)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    constexpr std::uint32_t Seed = 0x5eed;
    tested.bare->cloth.random = Client::Crt::CrtRandom{Seed};
    for (int code = 0; code <= 0xff; ++code)
    {
        SCOPED_TRACE(code);
        tested.online.socket.reads = {Bytes({0xc1, 4, 0x0f, static_cast<std::uint8_t>(code)})};

        ASSERT_TRUE(tested.online.session.Poll().HasValue());

        EXPECT_EQ(tested.bare->cloth.random.State(), Seed);
    }
    EXPECT_EQ(tested.targets.size(), 256u);
    EXPECT_EQ(tested.online.session.State(), Client::Session::SessionState::LoggedIn);
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_FALSE(tested.bare->selection.HasReceivedList());
}

TEST(WeatherSessionTest, DeliversEveryWeatherReceptionBeforeTheNextPacket)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    tested.online.socket.reads = {
        Client::Tests::Join({Client::Tests::Bytes({0xc1, 4, 0x0f, 0x17}), Client::Tests::Bytes({0xc1, 4, 0x0f, 0x00}),
                             Client::Tests::Bytes({0xc1, 4, 0x0f, 0x27})})};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{42, 0, 0}));
    EXPECT_TRUE(tested.online.eventSink.world.empty());
    EXPECT_EQ(tested.online.session.State(), Client::Session::SessionState::LoggedIn);
}

TEST(WeatherSessionTest, AppliesTheThreeBranchesInWireOrderWithoutAdvancingTheRainCounter)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    tested.world.Runtime().leafWeather.target = 77;
    tested.world.Runtime().leafWeather.current = 19;
    const auto sentBefore = tested.online.socket.sent.size();
    tested.online.socket.reads = {Client::Tests::Join({Bytes({0xc1, 4, 0x0f, 0x17}), Bytes({0xc1, 4, 0x0f, 0x27}),
                                                       Bytes({0xc1, 4, 0x0f, 0x1f}), Bytes({0xc1, 4, 0x0f, 0x0f})})};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{42, 42, 90, 0}));
    EXPECT_EQ(tested.world.Runtime().leafWeather.current, 19);
    EXPECT_EQ(tested.online.session.State(), Client::Session::SessionState::LoggedIn);
    EXPECT_EQ(tested.online.socket.sent.size(), sentBefore);
}

TEST(WeatherSessionTest, DiscardsMalformedWeatherAndContinuesTheSameBatch)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    tested.world.Runtime().leafWeather.target = 77;
    tested.online.socket.reads = {
        Client::Tests::Join({Bytes({0xc1, 3, 0x0f}), Bytes({0xc2, 0, 5, 0x0f, 0x17}), Bytes({0xc1, 4, 0x0f, 0x11})})};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{6}));
    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 6);
    EXPECT_EQ(tested.online.session.State(), Client::Session::SessionState::LoggedIn);
}

TEST(WeatherSessionTest, AcceptsTheNormalizedEncryptedPacketWithoutASeparateEncryptionGate)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    Client::Network::PacketSerials serverSerials;
    tested.online.socket.reads = {Client::Tests::FromTheGameServer(Bytes({0xc1, 4, 0x0f, 0x1f}), serverSerials)};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{90}));
}

TEST(WeatherSessionTest, AppliesBeforeTheGameHelloWithoutChangingTheSessionState)
{
    WeatherHarness tested;
    auto& online = tested.online;
    online.session.Start();
    online.socket.reads = {Client::Tests::Hello()};
    ASSERT_TRUE(online.session.Poll().HasValue());
    online.socket.reads = {Client::Tests::ServerList({400})};
    ASSERT_TRUE(online.session.Poll().HasValue());
    online.session.ChooseServer(400);
    online.socket.reads = {Client::Tests::AddressReply("10.0.0.2", 55901)};
    ASSERT_TRUE(online.session.Poll().HasValue());
    ASSERT_TRUE(online.session.Poll().HasValue());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::WaitingGameHello);
    online.socket.reads = {Bytes({0xc1, 4, 0x0f, 0x17})};

    ASSERT_TRUE(online.session.Poll().HasValue());

    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 42);
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::WaitingGameHello);
}

TEST(WeatherSessionTest, DoesNotRegisterWeatherWithTheConnectServer)
{
    WeatherHarness tested;
    tested.world.Runtime().leafWeather.target = 77;
    tested.online.session.Start();
    tested.online.socket.reads = {Client::Tests::Join({Client::Tests::Hello(), Bytes({0xc1, 4, 0x0f, 0x17})})};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 77);
    EXPECT_TRUE(tested.targets.empty());
    EXPECT_EQ(tested.online.session.State(), Client::Session::SessionState::WaitingServerList);
}

// A failing reception between the weathers (the warp with an item on the cursor, 0x5be150): the weathers before it
// stay applied and the one after it never arrives.
TEST(WeatherSessionTest, KeepsTheAppliedPrefixAndStopsAtTheFirstFailure)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    tested.world.Runtime().cursorItemCount = 1;
    Client::Network::PacketSerials serverSerials;
    tested.online.socket.reads = {
        Client::Tests::Join({Bytes({0xc1, 4, 0x0f, 0x17}), Bytes({0xc1, 4, 0x0f, 0x1f}),
                             Client::Tests::FromTheGameServer(Bytes({0xc1, 8, 0x1c, 0, 0, 30, 31, 0}), serverSerials),
                             Bytes({0xc1, 4, 0x0f, 0x00})})};

    const auto polled = tested.online.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_NE(polled.Error().find("0x5be150"), std::string::npos) << polled.Error();
    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{42, 90, 90}));
    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 90);
    EXPECT_EQ(tested.online.session.Failure(), Client::Session::SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.online.connection.State(), Client::Network::LinkState::Closed);
    const auto again = tested.online.session.Poll();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), polled.Error());
    EXPECT_EQ(tested.targets.size(), 3u);
    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 90);
}

TEST(WeatherSessionTest, AppliesThePacketBeforeARemoteCloseAlreadyObservedByThePoll)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    tested.online.socket.reads = {Bytes({0xc1, 4, 0x0f, 0x17})};
    tested.online.socket.peerClosed = true;

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 42);
    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{42}));
    EXPECT_EQ(tested.online.session.Failure(), Client::Session::SessionFailure::GameServerLost);
}

TEST(WeatherProtocolTest, ReadsItsMinimumSizeAndNotOneByteLess)
{
    Client::Tests::ExpectMinimumSize(Client::Protocol::Weather::ReadWeather, Bytes({0xc1, 0x04, 0x0f, 0x10}));
}
} // namespace
