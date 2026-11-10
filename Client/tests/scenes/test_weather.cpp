#include "App/ServerSession.h"

#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/LinkState.h"
#include "Network/PacketSerials.h"
#include "Protocol/ProtocolError.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/LeafWeather.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using Client::Tests::Bytes;
using Client::Tests::PacketOf;

TEST(WeatherProtocolTest, KeepsTheWholeWeatherByteIncludingUnknownHighNibbles)
{
    for (int code = 0; code <= 0xff; ++code)
    {
        const auto weather = Client::Protocol::World::ReadWeather(PacketOf(Bytes({0xc1, 4, 0x0f, code})));
        ASSERT_TRUE(weather.HasValue()) << code;
        EXPECT_EQ(weather.Value().code, code);
    }
}

TEST(WeatherProtocolTest, RejectsWrongOpcodesAndContainersBeforeReadingTheField)
{
    for (auto bytes : {Bytes({0xc1, 3, 0x0e}), Bytes({0xc2, 0, 5, 0x0f, 0x17}), Bytes({0xc3, 4, 0x0f, 0x17}),
                       Bytes({0xc4, 0, 5, 0x0f, 0x17})})
    {
        EXPECT_EQ(Client::Tests::ErrorOf(Client::Protocol::World::ReadWeather(PacketOf(std::move(bytes)))),
                  Client::Protocol::ProtocolError::WrongPacket);
    }
}

TEST(WeatherProtocolTest, RequiresTheWeatherByte)
{
    EXPECT_EQ(Client::Tests::ErrorOf(Client::Protocol::World::ReadWeather(PacketOf(Bytes({0xc1, 3, 0x0f})))),
              Client::Protocol::ProtocolError::TooShort);
}

TEST(WeatherProtocolTest, IgnoresTrailingBytes)
{
    const auto weather = Client::Protocol::World::ReadWeather(PacketOf(Bytes({0xc1, 6, 0x0f, 0x1f, 0x00, 0xff})));
    ASSERT_TRUE(weather.HasValue());
    EXPECT_EQ(weather.Value().code, 0x1f);
}

struct WeatherHarness
{
    WeatherHarness()
    {
        online.eventSink.onWorld = [this](const Client::App::WorldEvent& event, Client::App::ServerSession& session)
        {
            auto applied = applier.ApplyWorld(event, session);
            targets.push_back(world.Runtime().leafWeather.target);
            return applied;
        };
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Scenes::SessionEventApplier applier{Client::Scenes::WorldEntryResources{.world = world,
                                                                                    .selection = selection,
                                                                                    .dataRoot = {},
                                                                                    .cloth = cloth,
                                                                                    .camera = camera,
                                                                                    .cameraInput = input,
                                                                                    .models = models,
                                                                                    .log = log}};
    Client::Tests::LoggedInSession online;
    std::vector<std::int32_t> targets;
};

TEST(WeatherApplierTest, StartsWithTheStaticTargetAndCounterAndTheDataLeafCount)
{
    const Client::Scenes::WorldRuntime runtime;
    EXPECT_EQ(runtime.leafWeather.target, 0);
    EXPECT_EQ(runtime.leafWeather.current, 0);
    EXPECT_EQ(runtime.leafWeather.firstWave, 30); // .data[6c8c5c] = 1e 00 00 00.
    EXPECT_EQ(runtime.leafWeather.secondWave, 0);
    EXPECT_EQ(runtime.leafWeather.phase, 0);
}

// 4e36c4/4e36ed escriben un solo DWORD; 4e36dd salta al default para los otros 224 bytes.
TEST(WeatherApplierTest, AllThreeBranchesChangeOnlyTheTargetAndConsumeNoRandom)
{
    WeatherHarness tested;
    const Client::World::LeafWeather before{
        .target = 777, .current = -123, .firstWave = 45, .secondWave = -67, .phase = 890};
    constexpr std::array<std::int32_t, 16> RainTargets{0, 6, 12, 18, 24, 30, 36, 42, 48, 54, 60, 66, 72, 78, 84, 90};
    constexpr std::uint32_t Seed = 0x5eed;
    tested.cloth.random = Client::Render::Models::CrtRandom{Seed};
    for (int code = 0; code <= 0xff; ++code)
    {
        SCOPED_TRACE(code);
        tested.world.Runtime().leafWeather = before;
        const Client::App::WorldEvent event = Client::Protocol::World::Weather{static_cast<std::uint8_t>(code)};

        ASSERT_TRUE(tested.applier.ApplyWorld(event, tested.online.session).HasValue());

        const auto& after = tested.world.Runtime().leafWeather;
        const auto expected = code < 16 ? 0 : code < 32 ? RainTargets[static_cast<std::size_t>(code - 16)] : 777;
        EXPECT_EQ(after.target, expected);
        EXPECT_EQ(after.current, before.current);
        EXPECT_EQ(after.firstWave, before.firstWave);
        EXPECT_EQ(after.secondWave, before.secondWave);
        EXPECT_EQ(after.phase, before.phase);
        EXPECT_EQ(tested.cloth.random.State(), Seed);
    }
    EXPECT_EQ(tested.online.session.State(), Client::App::SessionState::Idle);
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}

TEST(WeatherSessionTest, DeliversEveryWeatherReceptionBeforeTheNextPacket)
{
    Client::Tests::LoggedInSession tested;
    ASSERT_NO_FATAL_FAILURE(tested.LogIn());
    std::size_t applied = 0;
    tested.eventSink.onWorld = [&applied](const Client::App::WorldEvent&,
                                          Client::App::ServerSession&) -> Client::Core::Expected<void, std::string>
    {
        ++applied;
        return {};
    };
    tested.socket.reads = {
        Client::Tests::Join({Client::Tests::Bytes({0xc1, 4, 0x0f, 0x17}), Client::Tests::Bytes({0xc1, 4, 0x0f, 0x00}),
                             Client::Tests::Bytes({0xc1, 4, 0x0f, 0x27})})};

    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(applied, 3u);
    EXPECT_EQ(tested.eventSink.world.size(), 3u);
    EXPECT_EQ(tested.session.State(), Client::App::SessionState::LoggedIn);
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
    EXPECT_EQ(tested.online.session.State(), Client::App::SessionState::LoggedIn);
    EXPECT_EQ(tested.online.socket.sent.size(), sentBefore);
    const auto& events = tested.online.eventSink.world;
    ASSERT_EQ(events.size(), 4u);
    constexpr std::array<std::uint8_t, 4> NativeCodes{0x17, 0x27, 0x1f, 0x0f};
    for (std::size_t index = 0; index < NativeCodes.size(); ++index)
    {
        const auto* weather = std::get_if<Client::Protocol::World::Weather>(&events[index]);
        ASSERT_NE(weather, nullptr);
        EXPECT_EQ(weather->code, NativeCodes[index]);
    }
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
    EXPECT_EQ(tested.online.eventSink.world.size(), 1u);
    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 6);
    EXPECT_EQ(tested.online.session.State(), Client::App::SessionState::LoggedIn);
}

TEST(WeatherSessionTest, AcceptsTheNormalizedEncryptedPacketWithoutASeparateEncryptionGate)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    Client::Network::PacketSerials serverSerials;
    tested.online.socket.reads = {Client::Tests::FromTheGameServer(Bytes({0xc1, 4, 0x0f, 0x1f}), serverSerials)};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.targets, (std::vector<std::int32_t>{90}));
    EXPECT_EQ(tested.online.eventSink.world.size(), 1u);
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
    ASSERT_EQ(online.session.State(), Client::App::SessionState::WaitingGameHello);
    online.socket.reads = {Bytes({0xc1, 4, 0x0f, 0x17})};

    ASSERT_TRUE(online.session.Poll().HasValue());

    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 42);
    EXPECT_EQ(online.session.State(), Client::App::SessionState::WaitingGameHello);
}

TEST(WeatherSessionTest, DoesNotRegisterWeatherWithTheConnectServer)
{
    WeatherHarness tested;
    tested.world.Runtime().leafWeather.target = 77;
    tested.online.session.Start();
    tested.online.socket.reads = {Client::Tests::Join({Client::Tests::Hello(), Bytes({0xc1, 4, 0x0f, 0x17})})};

    ASSERT_TRUE(tested.online.session.Poll().HasValue());

    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 77);
    EXPECT_TRUE(tested.online.eventSink.world.empty());
    EXPECT_EQ(tested.online.session.State(), Client::App::SessionState::WaitingServerList);
}

TEST(WeatherSessionTest, KeepsTheAppliedPrefixAndStopsAtTheFirstSinkFailure)
{
    WeatherHarness tested;
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    std::size_t calls = 0;
    tested.online.eventSink.onWorld =
        [&tested, &calls](const Client::App::WorldEvent& event,
                          Client::App::ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        auto applied = tested.applier.ApplyWorld(event, session);
        ++calls;
        if (calls == 2)
            return Client::Core::Unexpected(std::string("corte de prueba después de aplicar 0F"));
        return applied;
    };
    tested.online.socket.reads = {Client::Tests::Join(
        {Bytes({0xc1, 4, 0x0f, 0x17}), Bytes({0xc1, 4, 0x0f, 0x1f}), Bytes({0xc1, 4, 0x0f, 0x00})})};

    const auto polled = tested.online.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "corte de prueba después de aplicar 0F");
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(tested.online.eventSink.world.size(), 2u);
    EXPECT_EQ(tested.world.Runtime().leafWeather.target, 90);
    EXPECT_EQ(tested.online.session.Failure(), Client::App::SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.online.connection.State(), Client::Network::LinkState::Closed);
    const auto again = tested.online.session.Poll();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), polled.Error());
    EXPECT_EQ(calls, 2u);
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
    EXPECT_EQ(tested.online.session.Failure(), Client::App::SessionFailure::GameServerLost);
}
} // namespace
