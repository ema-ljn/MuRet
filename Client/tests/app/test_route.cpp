#include "Session/Route.h"

#include "App/IChatRoomLinks.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Kernel/IPacketSender.h"
#include "Kernel/PlayerKey.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Movement/World.h"
#include "Protocol/Outgoing.h"
#include "Actors/ActionSend.h"
#include "Combat/ActorCombat.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/SocialEvents.h"
#include "Kernel/WorldState.h"
#include "Session/ServerSession.h"
#include "Session/WorldEntry.h"
#include "Tables/PlayerModels.h"

#include "test_server_session_support.h"

#include "support/BareWorld.h"
#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::App::SessionState;
using Client::Protocol::Movement::Walk;
using Client::Tests::ServerSessionSupport::Harness;

struct Handled
{
    Walk walk;
    std::optional<Client::Kernel::PlayerKey> player;
    const Client::Kernel::WorldState* world = nullptr;
    const Client::Scenes::WorldEntryResources* resources = nullptr;
};

// What the handlers below were given, and the error the next one returns.
std::vector<Handled> handled;
std::optional<std::string> nextFailure;

Client::Core::Expected<void, std::string> HandleWithEveryPart(const Walk& walk, Client::Kernel::PlayerKey player,
                                                              Client::Kernel::WorldState& world,
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

struct ChatParts
{
    const Client::Kernel::WorldState* world = nullptr;
    const Client::Crt::CrtRandom* random = nullptr;
    std::optional<std::uint8_t> chosenSlot;
    bool sendsToTheGuild = false;
    const Client::App::IChatRoomLinks* rooms = nullptr;
    const Client::Scenes::WorldEntryResources* worldEntry = nullptr;
};
std::optional<ChatParts> chatParts;

void HandleWithTheChat(const Walk&, const Client::Scenes::ChatReceiveContext& chat)
{
    chatParts = ChatParts{.world = &chat.world,
                          .random = &chat.random,
                          .chosenSlot = chat.chosenSlot,
                          .sendsToTheGuild = static_cast<bool>(chat.guildSend),
                          .rooms = chat.rooms,
                          .worldEntry = chat.worldEntry};
}

struct CombatParts
{
    const Client::Kernel::WorldState* world = nullptr;
    const Client::Crt::CrtRandom* random = nullptr;
    const Client::Models::Skeleton* playerSkeleton = nullptr;
    const Client::Models::IModelLibrary* models = nullptr;
};
std::optional<CombatParts> combatParts;

void HandleWithTheCombat(const Walk&, const Client::Scenes::ActorCombatContext& combat)
{
    combatParts = CombatParts{.world = &combat.world,
                              .random = &combat.random,
                              .playerSkeleton = combat.playerSkeleton,
                              .models = combat.models};
}

void HandleBySitting(const Walk& walk, const Client::Actors::ActionSender& sendAction)
{
    sendAction(walk.direction, 0x7a);
}

struct WorldServices
{
    const Client::Core::Log* log = nullptr;
    const Client::Crt::CrtRandom* random = nullptr;
};
std::optional<WorldServices> worldServices;

void HandleWithTheLogAndTheRandom(const Walk&, Client::Core::Log& log, Client::Crt::CrtRandom& random)
{
    worldServices = WorldServices{.log = &log, .random = &random};
}

void HandleBySendingTheAction(const Walk& walk, const Client::Scenes::GamePacketSender& send)
{
    send(Client::Protocol::Movement::WriteAction(walk.direction, 0x7a));
}

void HandleBySendingThroughThePort(const Walk& walk, const Client::Kernel::IPacketSender& send)
{
    send.Send(Client::Protocol::Movement::WriteAction(walk.direction, 0x7a));
}

const Client::Crt::CrtRandom* answeredFrom = nullptr;

Client::Protocol::Outgoing AnswerFromTheRandom(Client::Crt::CrtRandom& random)
{
    answeredFrom = &random;
    return Client::Protocol::Movement::WriteRejectedTeleport();
}

const Client::App::ServerSession* sessionPart = nullptr;

void HandleWithTheSession(const Walk&, Client::App::ServerSession& session)
{
    sessionPart = &session;
}

constexpr auto RouteEveryPart = &Client::Session::Route<"se ignora el movimiento de prueba",
                                                        &Client::Protocol::Movement::ReadWalk, &HandleWithEveryPart>;
constexpr auto RouteTheWalkAlone = &Client::Session::Route<"se ignora el movimiento de prueba",
                                                           &Client::Protocol::Movement::ReadWalk, &HandleTheWalkAlone>;
constexpr auto RouteEncrypted =
    &Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                            &HandleWithEveryPart,
                            Client::Session::InClear<"el movimiento de prueba no llegó cifrado",
                                                     &Client::Protocol::Movement::WriteRejectedTeleport>>;
constexpr auto RouteAnswered = &Client::Session::Route<
    "se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk, &HandleWithEveryPart,
    Client::Session::AnsweredInClear<"el movimiento de prueba no llegó cifrado", &AnswerFromTheRandom>>;

Client::Network::Packet WalkPacket(bool decrypted = false)
{
    auto packet =
        Client::Network::Packet::FromBytes(Client::Tests::Bytes({0xc1, 8, 0xd3, 0x12, 0x34, 7, 9, 0xf0}), decrypted);
    EXPECT_TRUE(packet.HasValue());
    return std::move(packet).Value();
}

class RouteTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        handled.clear();
        nextFailure.reset();
        chatParts.reset();
        combatParts.reset();
        worldServices.reset();
        answeredFrom = nullptr;
        sessionPart = nullptr;
        ASSERT_NO_FATAL_FAILURE(online.ReachLoggedIn());
        online.eventSink.routedWorld = &world->resources;
    }

    [[nodiscard]] bool Warned(const std::string& text) const
    {
        return std::ranges::any_of(online.sink.lines,
                                   [&text](const std::string& line) { return line.find(text) != std::string::npos; });
    }

    std::unique_ptr<Client::Tests::BareWorld> world = std::make_unique<Client::Tests::BareWorld>();
    Harness online{Client::Network::IdentityObfuscation::Shared()};
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

// The chat's context is the world's with the session's: its chosen character, its chat rooms and its GameServer for
// what the guild's window sends.
TEST_F(RouteTest, AChatHandlerGetsTheChatContextOfTheWorldAndTheSession)
{
    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleWithTheChat>(online.session, WalkPacket());

    ASSERT_TRUE(chatParts.has_value());
    EXPECT_EQ(chatParts->world, &world->world);
    EXPECT_EQ(chatParts->random, &world->cloth.random);
    EXPECT_FALSE(chatParts->chosenSlot.has_value());
    EXPECT_TRUE(chatParts->sendsToTheGuild);
    EXPECT_EQ(chatParts->rooms, static_cast<const Client::App::IChatRoomLinks*>(&online.session));
    EXPECT_EQ(chatParts->worldEntry, &world->resources);
}

TEST_F(RouteTest, ACombatHandlerGetsTheCombatContextOfTheWorld)
{
    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleWithTheCombat>(online.session, WalkPacket());

    ASSERT_TRUE(combatParts.has_value());
    EXPECT_EQ(combatParts->world, &world->world);
    EXPECT_EQ(combatParts->random, &world->cloth.random);
    EXPECT_EQ(combatParts->playerSkeleton, world->models.SkeletonOf(Client::Tables::PlayerSkeletonModelId));
    EXPECT_EQ(combatParts->models, &world->models);
}

// The action sender sends the C1 05 18 through the session's GameServer, with the XOR32.
TEST_F(RouteTest, AnActionSenderSendsThroughTheSession)
{
    const auto before = online.GameServerReads().packets.size();

    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleBySitting>(online.session, WalkPacket());

    const auto received = online.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(received.packets.back()).Bytes(),
                                         Client::Tests::Bytes({0xc1, 5, 0x18, 15, 0x7a})));
}

TEST_F(RouteTest, AHandlerGetsTheLogAndTheRandomOfTheWorld)
{
    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleWithTheLogAndTheRandom>(online.session, WalkPacket());

    ASSERT_TRUE(worldServices.has_value());
    EXPECT_EQ(worldServices->log, &world->log);
    EXPECT_EQ(worldServices->random, &world->cloth.random);
}

TEST_F(RouteTest, AGamePacketSenderSendsThroughTheSession)
{
    const auto before = online.GameServerReads().packets.size();

    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleBySendingTheAction>(online.session, WalkPacket());

    const auto received = online.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(received.packets.back()).Bytes(),
                                         Client::Tests::Bytes({0xc1, 5, 0x18, 15, 0x7a})));
}

TEST_F(RouteTest, TheKernelsPacketSenderSendsThroughTheSession)
{
    const auto before = online.GameServerReads().packets.size();

    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleBySendingThroughThePort>(online.session, WalkPacket());

    const auto received = online.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(received.packets.back()).Bytes(),
                                         Client::Tests::Bytes({0xc1, 5, 0x18, 15, 0x7a})));
}

TEST_F(RouteTest, OnlyTheSessionsOwnHandlersNameTheSession)
{
    Client::Session::Route<"se ignora el movimiento de prueba", &Client::Protocol::Movement::ReadWalk,
                           &HandleWithTheSession>(online.session, WalkPacket());

    EXPECT_EQ(sessionPart, &online.session);
}

TEST_F(RouteTest, AnUnreadablePacketReachesNoHandlerAndIsIgnoredWithAWarning)
{
    RouteEveryPart(online.session, Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 5, 0xd3, 0x12, 0x34})));

    EXPECT_TRUE(handled.empty());
    EXPECT_TRUE(online.eventSink.routedAfter.empty());
    EXPECT_TRUE(Warned("se ignora el movimiento de prueba: no se puede leer"));
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

// 0x4c6e32, 0x4cf200 and 0x4cfce0 look at the flag before reading: in clear, the F1 03 and nothing more.
TEST_F(RouteTest, APacketThatMustArriveEncryptedIsReportedInClearWithoutBeingRead)
{
    const auto before = online.GameServerReads().packets.size();

    RouteEncrypted(online.session, WalkPacket());

    EXPECT_TRUE(handled.empty());
    EXPECT_TRUE(online.eventSink.routedAfter.empty());
    EXPECT_TRUE(Warned("el movimiento de prueba no llegó cifrado: se le avisa al GameServer (F1 03)"));
    const auto received = online.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(received.packets.back()).Bytes(),
                                         Client::Tests::Bytes({0xc1, 6, 0xf1, 3, 0, 0x1c})));
    EXPECT_TRUE(received.packets.back().WasDecrypted());
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

TEST_F(RouteTest, AnEncryptedPacketPassesTheCheckAndIsHandled)
{
    const auto before = online.GameServerReads().packets.size();

    RouteEncrypted(online.session, WalkPacket(true));

    ASSERT_EQ(handled.size(), 1u);
    EXPECT_EQ(handled[0].walk.characterId, 0x1234);
    EXPECT_EQ(online.GameServerReads().packets.size(), before);
    EXPECT_FALSE(Warned("no llegó cifrado"));
}

// 0x4d4af0 answers the 36 in clear itself, with an F1 03 that draws from the CRT: the answer ends the reception.
TEST_F(RouteTest, APacketAnsweredInClearGetsItsAnswerFromTheWorldsRandomWithoutBeingRead)
{
    const auto before = online.GameServerReads().packets.size();

    RouteAnswered(online.session, WalkPacket());

    EXPECT_TRUE(handled.empty());
    EXPECT_EQ(answeredFrom, &world->cloth.random);
    EXPECT_EQ(online.eventSink.routedAfter, (std::vector<std::size_t>{0}));
    EXPECT_TRUE(Warned("el movimiento de prueba no llegó cifrado: se le avisa al GameServer (F1 03)"));
    const auto received = online.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(received.packets.back()).Bytes(),
                                         Client::Tests::Bytes({0xc1, 6, 0xf1, 3, 0, 0x1c})));
    EXPECT_TRUE(received.packets.back().WasDecrypted());
    EXPECT_NE(online.session.State(), SessionState::Failed);
}

TEST_F(RouteTest, AnEncryptedPacketIsNotAnsweredAndIsHandled)
{
    const auto before = online.GameServerReads().packets.size();

    RouteAnswered(online.session, WalkPacket(true));

    ASSERT_EQ(handled.size(), 1u);
    EXPECT_EQ(handled[0].walk.characterId, 0x1234);
    EXPECT_EQ(answeredFrom, nullptr);
    EXPECT_EQ(online.GameServerReads().packets.size(), before);
    EXPECT_FALSE(Warned("no llegó cifrado"));
}

TEST_F(RouteTest, WithoutAWorldAPacketInClearIsNotAnswered)
{
    online.eventSink.routedWorld = nullptr;
    const auto before = online.GameServerReads().packets.size();

    RouteAnswered(online.session, WalkPacket());

    EXPECT_TRUE(handled.empty());
    EXPECT_EQ(answeredFrom, nullptr);
    EXPECT_TRUE(online.eventSink.routedAfter.empty());
    EXPECT_EQ(online.GameServerReads().packets.size(), before);
    EXPECT_FALSE(Warned("no llegó cifrado"));
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
