#include "Session/ServerSession.h"

#include "Session/CharacterEvent.h"
#include "Session/Credentials.h"
#include "Kernel/FaithfulPacer.h"
#include "App/GameLoop.h"
#include "Kernel/PacingMode.h"
#include "Session/ServerAddress.h"
#include "Session/SessionState.h"
#include "Platform/WindowState.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Network/CloseReason.h"
#include "Crypto/SimpleModulus.h"
#include "Network/IdentityObfuscation.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/SocketError.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Session/BuildConstants.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Pets/PetInfo.h"
#include "Protocol/FrontEnd/CharacterCreate.h"
#include "Protocol/FrontEnd/CharacterDelete.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/FrontEnd/CharacterSelect.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Session/Keepalive.h"
#include "Protocol/Session/Login.h"
#include "Protocol/Session/ServerList.h"
#include "Scenes/IScene.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Tables/DialogTable.h"

#include "support/AppPrinters.h"
#include "support/BareWorld.h"
#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/IdleScene.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/SessionPackets.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"
#include "test_server_session_support.h"
#include "Protocol/Opcodes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Client::App::ServerSession over a real Connection and a FakeSocket that plays the ConnectServer and the GameServer
// (CL4.3, CL4.4). The bytes are 04's (El flujo de conexión), written by hand, and the addresses arbitrary: the
// FakeSocket does not connect. support/TestGameServer.h decrypts with the synthetic keys; the login is compared with
// SameBytesWithoutValues because no message carries its bytes. A "frame" is a FakeClock Advance and a Poll (CL4.5).
namespace
{
using Client::App::CharacterCreated;
using Client::App::CharacterDeleted;
using Client::App::CharacterEvent;
using Client::App::CharacterListReceived;
using Client::App::Credentials;
using Client::App::GameLoop;
using Client::App::KeepaliveSpeeds;
using Client::App::ReplyTimeout;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::App::WorldEvent;
using Client::Core::Duration;
using Client::Core::LogLevel;
using Client::Crypto::CipherBlockSize;
using Client::Crypto::EncryptBlock;
using Client::Crypto::PlainBlockSize;
using Client::Input::InputState;
using Client::Kernel::PacingMode;
using Client::Kernel::SceneId;
using Client::Kernel::TickCountMs;
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;
using Client::Network::HeaderSize;
using Client::Network::IdentityObfuscation;
using Client::Network::LinkState;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::SocketError;
using Client::Network::Xor32Decode;
using Client::Platform::WindowState;
using Client::Protocol::Character::CreateSubcode;
using Client::Protocol::Character::DeleteSubcode;
using Client::Protocol::Character::ListSubcode;
using Client::Protocol::Character::SelectSubcode;
using Client::Protocol::FrontEnd::CharacterEntry;
using Client::Protocol::FrontEnd::CharacterSelectResult;
using Client::Protocol::FrontEnd::DeleteCodeSize;
using Client::Protocol::FrontEnd::WriteCharacterCreate;
using Client::Protocol::FrontEnd::WriteCharacterDelete;
using Client::Protocol::FrontEnd::WriteCharacterSelect;
using Client::Protocol::Items::InventoryList;
using Client::Protocol::Session::ClientVersion;
using Client::Protocol::Session::Keepalive;
using Client::Protocol::Session::KeepaliveAdjust20;
using Client::Protocol::Session::LoginRequest;
using Client::Protocol::Session::ServerEntry;
using Client::Protocol::Session::WriteKeepalive;
using Client::Protocol::Session::WriteLoginRequest;
using Client::Scenes::IScene;
using Client::Scenes::SceneManager;
using Client::Session::ServerAddress;
using Client::Session::SessionFailure;
using Client::Session::SessionState;
using Client::Tests::AddressReply;
using Client::Tests::BareWorld;
using Client::Tests::BusyReply;
using Client::Tests::Bytes;
using Client::Tests::CharacterEntryFields;
using Client::Tests::CharacterListBytes;
using Client::Tests::FakeSocket;
using Client::Tests::FromTheGameServer;
using Client::Tests::GameHello;
using Client::Tests::GameServerEncryption;
using Client::Tests::GameServerReceived;
using Client::Tests::Hello;
using Client::Tests::IdleScene;
using Client::Tests::Join;
using Client::Tests::ListRequest;
using Client::Tests::LoginResult;
using Client::Tests::LogoutResult;
using Client::Tests::NameBytes;
using Client::Tests::NoticeBytes;
using Client::Tests::PetInfoRequest;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::ServerList;
using Client::Tests::ServerSessionSupport::Account;
using Client::Tests::ServerSessionSupport::ByteVector;
using Client::Tests::ServerSessionSupport::ConnectHost;
using Client::Tests::ServerSessionSupport::ConnectPort;
using Client::Tests::ServerSessionSupport::EncryptedWorldSequence;
using Client::Tests::ServerSessionSupport::ExpectInventory;
using Client::Tests::ServerSessionSupport::ExpectWorldEntry;
using Client::Tests::ServerSessionSupport::ExpectWorldSequence;
using Client::Tests::ServerSessionSupport::GameHost;
using Client::Tests::ServerSessionSupport::GamePort;
using Client::Tests::ServerSessionSupport::Harness;
using Client::Tests::ServerSessionSupport::InventoryReply;
using Client::Tests::ServerSessionSupport::LabServerCode;
using Client::Tests::ServerSessionSupport::Lines;
using Client::Tests::ServerSessionSupport::OtherWorldEntryReply;
using Client::Tests::ServerSessionSupport::PaddingSeed;
using Client::Tests::ServerSessionSupport::Password;
using Client::Tests::ServerSessionSupport::Settings;
using Client::Tests::ServerSessionSupport::TheCredentials;
using Client::Tests::ServerSessionSupport::TwoCharacters;
using Client::Tests::ServerSessionSupport::WorldEntryReply;

using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr std::string_view KeepaliveSent = "se encoló el keepalive";

// Where the speeds sit in the keepalive without the XOR32 (04 § El flujo de conexión): after the header, the
// opcode, the 00 and the tick.
constexpr std::size_t KeepalivePaddingIndex = 3;
constexpr std::size_t KeepaliveSpeedsIndex = 8;
constexpr std::size_t KeepaliveSpeedsSize = 4;

// A version other than the client's: the one the muproxy.log client sends (BuildConstants.h).
constexpr std::string_view OtherVersion = "09908";

// A one-block C3 with a single useful byte, serial 0: it passes the checksum and has no opcode, so it fails to
// decrypt with TooShort.
ByteVector OnlyTheSerial()
{
    const std::array<std::byte, PlainBlockSize> plain{};
    const auto block = EncryptBlock(GameServerEncryption(), plain, std::uint8_t{1});
    ByteVector bytes = Bytes({0xC3, 0x0D});
    bytes.insert(bytes.end(), block.begin(), block.end());
    return bytes;
}

// The real alternative must own its entries and seven opaque bytes, not views into the Packet. The runtime tests
// below check lifecycle and values; alone they do not prove there are no views.
static_assert(std::is_same_v<std::variant_alternative_t<1, WorldEvent>, InventoryList>);
static_assert(std::is_same_v<decltype(InventoryList::items), std::vector<Client::Protocol::Items::InventoryEntry>>);
static_assert(std::is_same_v<decltype(Client::Protocol::Items::InventoryEntry::item), std::array<std::byte, 7>>);

ByteVector EmptyInventoryReply()
{
    return Bytes({0xC2, 0x00, 0x06, 0xF3, 0x10, 0x00});
}

// The context is observed inside the handler, before any later transition.
TEST(ServerSessionTest, SynchronousDeliveryObservesContemporaryStatesAcrossPolls)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends),
                                 FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::WaitingGameHello, SessionState::WaitingCredentials}));
    tested.session.SubmitCredentials(Credentials{std::string(Account), std::string(Password)});
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(LoginResult(1), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::WaitingGameHello, SessionState::WaitingCredentials,
                                         SessionState::WaitingLoginResult, SessionState::LoggedIn}));
    tested.socket.reads = {FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.eventSink.worldStates.size(), 5u);
    EXPECT_EQ(tested.eventSink.worldStates.back(), SessionState::LoggedIn);
}

TEST(ServerSessionTest, SynchronousDeliveryPreservesMixedOrderAndRawDuplicateSlots)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    tested.eventSink.onCharacter = [](const CharacterEvent& event,
                                      const ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::WaitingCharacterList);
        EXPECT_EQ(std::get<CharacterListReceived>(event).list.characters.size(), 2u);
        EXPECT_EQ(session.Characters()->characters.size(), 1u);
        EXPECT_EQ(session.Characters()->characters.back().name, NameBytes("char0002"));
        return {};
    };
    tested.socket.reads = {Join({FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends),
                                 CharacterListBytes({{0, "char0001"}, {0, "char0002"}}),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"inventory", "list", "entry", "entry"}));
    ASSERT_EQ(tested.eventSink.world.size(), 3u);
    EXPECT_TRUE(std::get<InventoryList>(tested.eventSink.world[0]).items.empty());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(tested.eventSink.world[1]));
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(tested.eventSink.world[2]));
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::WaitingCharacterList, SessionState::ChoosingCharacter,
                                         SessionState::ChoosingCharacter}));
    ASSERT_EQ(tested.session.Characters()->characters.size(), 1u);
    EXPECT_EQ(tested.session.Characters()->characters[0].name, Client::Tests::NameBytes("char0002"));
}

TEST(ServerSessionTest, CharacterListApplicationFailureKeepsTheNormalizedViewAndPrecedesRemoteClose)
{
    for (const bool peerClosed : {false, true})
    {
        SCOPED_TRACE(peerClosed);
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
        tested.eventSink.onCharacter =
            [&tested, peerClosed](const CharacterEvent& event,
                                  const ServerSession& session) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(session.State(), SessionState::WaitingCharacterList);
            EXPECT_EQ(tested.connection.State(), peerClosed ? LinkState::Closed : LinkState::Open);
            const auto& raw = std::get<CharacterListReceived>(event).list.characters;
            EXPECT_EQ(raw.size(), 2u);
            EXPECT_EQ(raw.front().name, NameBytes("char0001"));
            EXPECT_EQ(raw.back().name, NameBytes("char0002"));
            EXPECT_EQ(session.Characters()->characters.size(), 1u);
            EXPECT_EQ(session.Characters()->characters.back().name, NameBytes("char0002"));
            EXPECT_EQ(session.ChosenCharacter(), nullptr);
            return Client::Core::Unexpected(std::string("falló la lista"));
        };
        tested.socket.reads = {CharacterListBytes({{0, "char0001"}, {0, "char0002"}})};
        tested.socket.peerClosed = peerClosed;
        const auto polled = tested.session.Poll();
        ASSERT_FALSE(polled.HasValue());
        EXPECT_EQ(polled.Error(), "falló la lista");
        EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
        EXPECT_EQ(tested.session.State(), SessionState::Failed);
        EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"list"}));
        ASSERT_EQ(tested.session.Characters()->characters.size(), 1u);
        EXPECT_EQ(tested.session.Characters()->characters.back().name, NameBytes("char0002"));
        EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    }
}

TEST(ServerSessionTest, SynchronousDeliveryFinishesBeforeRejectionAndRemoteClose)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.eventSink.onWorld = [&tested](const WorldEvent&,
                                         ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::WaitingGameHello);
        EXPECT_EQ(tested.connection.State(), LinkState::Closed);
        tested.eventSink.trace.push_back("applied");
        return {};
    };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(GameHello(0, ClientVersion), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};
    tested.socket.peerClosed = true;
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"entry", "applied"}));
    EXPECT_EQ(tested.session.Failure(), SessionFailure::HelloRejected);
}

TEST(ServerSessionTest, SynchronousDeliveryRejectionBeforeWorldDropsTheSuffix)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(GameHello(0, ClientVersion), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.trace.empty());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::HelloRejected);
}

TEST(ServerSessionTest, SynchronousDeliverySecondErrorStopsBatchAndSuccessStateAndKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    tested.AdvanceTo(Client::App::KeepalivePeriod);
    const auto keepalives = tested.Keepalives().size();
    tested.eventSink.onCharacter = [](const CharacterEvent&,
                                      const ServerSession&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("falla original")); };
    tested.socket.reads = {
        Join({FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends), CharacterListBytes({{0, "char0001"}}),
              FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    const auto polled = tested.session.Poll();
    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "falla original");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"inventory", "list"}));
    EXPECT_EQ(tested.eventSink.characterStates, (std::vector<SessionState>{SessionState::WaitingCharacterList}));
    // No other send leaves; the close by Fail is also enough to prevent it.
    EXPECT_EQ(tested.Keepalives().size(), keepalives);
    const auto again = tested.session.Poll();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), "falla original");
    EXPECT_EQ(tested.eventSink.trace.size(), 2u);
    tested.session.Start();
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_FALSE(tested.session.Failure().has_value());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
}

TEST(ServerSessionTest, SynchronousDeliveryWorldErrorStopsBeforeFollowingHello)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.eventSink.onWorld = [](const WorldEvent&, ServerSession&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("falta aplicador World")); };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends)})};
    const auto polled = tested.session.Poll();
    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "falta aplicador World");
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"entry"}));
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
}

TEST(ServerSessionTest, SynchronousDeliveryQuitBeforeWorldDropsTheSuffixAndRemainsTerminal)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(LogoutResult(0), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.trace.empty());
    const auto starts = tested.session.StartCount();
    tested.session.Start();
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.StartCount(), starts);
}

TEST(ServerSessionTest, SynchronousDeliveryCompletesTheFakeLoadBeforeTheNextMixedEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    int loads = 0;
    tested.eventSink.onWorld = [&tested, &loads](const WorldEvent&,
                                                 ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::WaitingCharacterList);
        tested.eventSink.trace.push_back("load-begin");
        // A synchronous double: it tests the boundary and the return, it is not the native loader.
        ++loads;
        tested.eventSink.trace.push_back("load-end");
        return {};
    };
    tested.eventSink.onCharacter = [&loads](const CharacterEvent&,
                                            const ServerSession&) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(loads, 2);
        return {};
    };
    tested.socket.reads = {
        Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
              FromTheGameServer(WorldEntryReply(), tested.gameServerSends), CharacterListBytes({})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.trace,
              (std::vector<std::string>{"entry", "load-begin", "load-end", "entry", "load-begin", "load-end", "list"}));
}

// With an explicit collector, the reception keeps the wire without applying the world or changing the UI.
TEST(ServerSessionTest, WorldEntryPreservesAllWireFieldsWithoutApplyingSelection)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.EnterWorld();
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 1u);
    const CharacterEntry* chosen = tested.session.ChosenCharacter();
    const std::uint32_t starts = tested.session.StartCount();
    const ByteVector sent = tested.socket.sent;
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};

    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    EXPECT_EQ(tested.eventSink.worldStates, (std::vector<SessionState>{SessionState::EnteringWorld}));
    EXPECT_EQ(tested.session.ChosenCharacter(), chosen);
    EXPECT_EQ(tested.session.StartCount(), starts);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
}

// Before the hello it also keeps repeated slots and a zero count, without filtering entries.
TEST(ServerSessionTest, WorldInventoryPreservesEveryEntryAndPublishesAnEmptyList)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(InventoryReply(), tested.gameServerSends),
                                 FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)})};

    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[0]));
    const auto* empty = std::get_if<InventoryList>(&events[1]);
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->items.empty());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// A queue per type would lose the interleaved order even with each payload intact.
TEST(ServerSessionTest, WorldEventsPreserveInterleavedOrderInOneBatch)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};

    static_cast<void>(tested.session.Poll());

    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Two equal receptions are still two events; deduplicating does not belong here.
TEST(ServerSessionTest, WorldEventsKeepIdenticalRepeatedReceptions)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};

    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 4u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[1]));
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[2]));
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[3]));
}

// Each TCP split must give the same sequence, even with a packet split between Polls.
TEST(ServerSessionTest, WorldEventsPreserveInterleavedOrderAcrossEveryReadSplit)
{
    Harness baseline(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(baseline.ReachLoggedIn());
    const std::size_t wireSize = EncryptedWorldSequence(baseline).size();
    for (std::size_t split = 1; split < wireSize; ++split)
    {
        SCOPED_TRACE(split);
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        const ByteVector wire = EncryptedWorldSequence(tested);
        const auto middle = wire.begin() + static_cast<std::ptrdiff_t>(split);
        tested.socket.reads = {ByteVector(wire.begin(), middle)};
        static_cast<void>(tested.session.Poll());
        tested.socket.reads = {ByteVector(middle, wire.end())};
        static_cast<void>(tested.session.Poll());
        ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
        EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    }
}

TEST(ServerSessionTest, WorldReceptionsAreDeliveredAcrossPollsBeforeTheRemoteClose)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::LoggedIn);

    tested.socket.reads = {FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)};
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(tested.eventSink.worldStates, std::vector<SessionState>(4, SessionState::LoggedIn));
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(std::span(events).first(3)));
    const auto* empty = std::get_if<InventoryList>(&events[3]);
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->items.empty());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// A valid clear body does not prove the encrypted origin this boundary requires: nothing of the F3 03 reaches the
// sink, and of each F3 10 only 0x4c55a0's common initialization, without the body. Each one reports to the GS with
// the F1 03 00 F3, the F3 10 after emptying.
TEST(ServerSessionTest, WorldEventsRejectClearPacketsAfterTheEncryptedPositive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {Join({WorldEntryReply(), InventoryReply(), EmptyInventoryReply()})};
    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Client::App::ClearInventoryReceived>(events[0]));
    EXPECT_TRUE(std::holds_alternative<Client::App::ClearInventoryReceived>(events[1]));
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 3);
    for (std::size_t report = sentBefore; report < received.packets.size(); ++report)
    {
        EXPECT_TRUE(
            SameBytes(Xor32Decode(received.packets[report]).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF3})));
    }
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// 0x4c55a0 unencrypted: if the sink cannot empty, the session fails and does not send the report.
TEST(ServerSessionTest, AClearInventoryThatFailsToApplyIsNotReported)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    tested.eventSink.onWorld = [](const WorldEvent&, ServerSession&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("sin inventario")); };

    tested.socket.reads = {EmptyInventoryReply()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
}

// 0x4c396a: the unencrypted F3 03 reply does not enter the world and reports to the GS with the F1 03 00 F3.
TEST(ServerSessionTest, AClearWorldEntryReplyIsReportedWithF103)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {WorldEntryReply()};
    static_cast<void>(tested.session.Poll());

    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF3})));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// The encrypted skills are read by their rows in wire order; the F3 11 that removes one needs no Skill.bmd.
TEST(ServerSessionTest, TheEncryptedSkillsAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    tested.socket.reads = {Join({
        FromTheGameServer(Bytes({0xC1, 0x08, 0x19, 0x0D, 0x12, 0x34, 0x80, 0x56}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x09, 0x1E, 0x0E, 0x12, 0x34, 0x80, 0x81, 0x40}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x06, 0x1B, 0x1B, 0x12, 0x34}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x0A, 0x07, 0x01, 0x10, 0x00, 0x00, 0x00, 0x12, 0x34}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x06, 0x48, 0x12, 0x34, 0x10}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x08, 0x69, 0x03, 0x34, 0x12, 0x56, 0x00}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x07, 0xF3, 0x11, 0xFF, 0x00, 0x02}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x05, 0xF3, 0x20, 0x42}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x04, 0x73, 0x00}), tested.gameServerSends),
    })};
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 8u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
    EXPECT_EQ(world->world.Runtime().hud.summonLife, 0x42);
    // The encrypted 73 does nothing (0x4d5b10).
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// 0x4cc562, 0x4ce032 and 0x4d5d8c: the unencrypted 19, 1E and 73 do not enter and report with the F1 03 00 [head].
TEST(ServerSessionTest, TheClearSkillsAreReportedWithF103)
{
    for (const auto& [packet, head] : {std::pair{Bytes({0xC1, 0x08, 0x19, 0x0D, 0x12, 0x34, 0x80, 0x56}), 0x19},
                                       std::pair{Bytes({0xC1, 0x09, 0x1E, 0x0E, 0x12, 0x34, 0x80, 0x81, 0x40}), 0x1E},
                                       std::pair{Bytes({0xC1, 0x04, 0x73, 0x00}), 0xF1}})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        const auto world = std::make_unique<BareWorld>();
        tested.eventSink.routedWorld = &world->resources;
        const std::size_t sentBefore = tested.GameServerReads().packets.size();
        tested.socket.reads = {packet};
        static_cast<void>(tested.session.Poll());
        const GameServerReceived received = tested.GameServerReads();
        ASSERT_EQ(received.packets.size(), sentBefore + 1) << head;
        EXPECT_TRUE(
            SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, head})));
        EXPECT_TRUE(tested.eventSink.routedAfter.empty());
        EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se le avisa al GameServer (F1 03)"), 1u);
        EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    }
}

// The others do not look at the encryption (0x4cbe90, 0x4cbe00, 0x4dbf50, 0x4cc290 and 0x4c5070).
TEST(ServerSessionTest, TheOtherClearSkillsAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    tested.socket.reads = {
        Join({Bytes({0xC1, 0x06, 0x1B, 0x1B, 0x12, 0x34}), Bytes({0xC1, 0x08, 0x69, 0x03, 0x34, 0x12, 0x56, 0x00}),
              Bytes({0xC1, 0x07, 0xF3, 0x11, 0xFF, 0x00, 0x02})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 3u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
}

TEST(ServerSessionTest, LevelUpReceptionsPreserveRepeatedEventsAndRejectTruncations)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    world->world.Runtime().hero = 7;
    const auto wire = Bytes({0xc1, 18, 0xf3, 5, 9, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0});
    tested.socket.reads = {Join({wire, Bytes({0xc1, 5, 0xf3, 5, 9}), wire})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.routedAfter, (std::vector<std::size_t>{0, 0}));
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora la subida de nivel (F3 05): no se puede leer"), 1u);
    EXPECT_EQ(world->world.OwnCharacter().level, 9);
}

// 0x4e4c0a and 0x4e4c42: the raven's orders arrive without looking at the encryption. Without the hero's raven, the
// A7 does nothing.
TEST(ServerSessionTest, TheRavenOrdersAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    tested.socket.reads = {Join({Bytes({0xC1, 0x07, 0xA7, 0x00, 0x03, 0x12, 0x34}),
                                 Bytes({0xC1, 0x09, 0xA8, 0x00, 0x01, 0x12, 0x34, 0x05, 0x67}),
                                 Bytes({0xC1, 0x06, 0xA7, 0x00, 0x03, 0x12})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 2u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora la orden del cuervo (A7): no se puede leer"), 1u);
    EXPECT_FALSE(world->world.Hero().darkSpirit);
}

// 0x4e4ca1: the pets' A9 arrives without looking at the encryption (CL9 milestone 4).
TEST(ServerSessionTest, ThePetInfoIsReadByItsRow)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    // 0x550d10 reads the item set tables (0x40c600).
    world->ReadyForTransitions();
    tested.socket.reads = {Bytes({0xC1, 0x0C, 0xA9, 0x00, 0x00, 0x01, 0x07, 0x00, 0x10, 0x27, 0x00, 0x00})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
}

// 0x4c1ec0: the accepted F1 00 leaves the player's key (pkt[5..6], big endian) for the F3 03.
TEST(ServerSessionTest, TheAcceptedGameHelloKeepsThePlayerIndex)
{
    Harness tested(IdentityObfuscation::Shared());
    EXPECT_EQ(tested.session.PlayerIndex(), 0);

    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    EXPECT_EQ(tested.session.PlayerIndex(), 5);
}

// A valid checksum does not make a short body or an impossible count readable.
TEST(ServerSessionTest, WorldEventsRejectEncryptedTruncationsAfterThePositive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));

    const std::size_t packetsSentBeforeCuts = tested.GameServerReads().packets.size();
    // Each cut keeps up to the subcode: it reaches the handler and fails in the body, not in the checksum.
    std::size_t rejectedEntries = 0;
    std::size_t rejectedInventories = 0;
    for (const ByteVector& valid : {WorldEntryReply(), InventoryReply()})
    {
        const bool wide = valid[0] == std::byte{0xC2};
        const std::size_t minimum = wide ? 5u : 4u;
        for (std::size_t size = minimum; size < valid.size(); ++size)
        {
            SCOPED_TRACE(size);
            ByteVector shortReply(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(size));
            shortReply[wide ? 2u : 1u] = static_cast<std::byte>(size);
            tested.socket.reads = {FromTheGameServer(shortReply, tested.gameServerSends)};
            static_cast<void>(tested.session.Poll());
            EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
            ++(wide ? rejectedInventories : rejectedEntries);
        }
    }
    ByteVector badCount = InventoryReply();
    badCount[5] = std::byte{0xFF};
    tested.socket.reads = {FromTheGameServer(badCount, tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    ++rejectedInventories;
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "(F3 03): no se puede leer"), rejectedEntries);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "(F3 10): no se puede leer"), rejectedInventories);
    // With a correct checksum nothing should be sent, including the F1/03 for a failed decryption.
    EXPECT_EQ(tested.GameServerReads().packets.size(), packetsSentBeforeCuts);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Encrypting a wrong type or opcode must not turn it into a world reply.
TEST(ServerSessionTest, WorldEventsDistinguishPacketKindsAndSubcodes)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));

    ByteVector wideEntry = WorldEntryReply();
    wideEntry.insert(wideEntry.begin() + 1, std::byte{0});
    wideEntry[0] = std::byte{0xC2};
    wideEntry[2] = static_cast<std::byte>(wideEntry.size());
    ByteVector narrowInventory = InventoryReply();
    narrowInventory.erase(narrowInventory.begin() + 1);
    narrowInventory[0] = std::byte{0xC1};
    narrowInventory[1] = static_cast<std::byte>(narrowInventory.size());
    std::vector<ByteVector> wrongKinds{wideEntry, narrowInventory};
    for (ByteVector packet : {WorldEntryReply(), InventoryReply()})
    {
        const std::size_t opcode = packet[0] == std::byte{0xC1} ? 2u : 3u;
        ByteVector otherOpcode = packet;
        otherOpcode[opcode] = std::byte{0xF2};
        wrongKinds.push_back(std::move(otherOpcode));
        packet[opcode + 1] = std::byte{0x7F};
        wrongKinds.push_back(std::move(packet));
    }
    for (const ByteVector& packet : wrongKinds)
    {
        tested.socket.reads = {FromTheGameServer(packet, tested.gameServerSends)};
        static_cast<void>(tested.session.Poll());
        EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    }
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// The peer's later close must not erase what arrived before in the same batch.
TEST(ServerSessionTest, WorldEventsSurviveFailureInTheSamePoll)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.eventSink.onWorld = [&tested](const WorldEvent&,
                                         ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::LoggedIn);
        EXPECT_EQ(tested.connection.State(), LinkState::Closed);
        return {};
    };
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    tested.socket.peerClosed = true;

    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// Start drops the incomplete packet, but not the copies already delivered to the collector.
TEST(ServerSessionTest, WorldEventsStartPreservesDeliveredCopiesAndDropsPartiallyReceivedPackets)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    const ByteVector entry = FromTheGameServer(WorldEntryReply(), tested.gameServerSends);
    ByteVector partial = FromTheGameServer(InventoryReply(), tested.gameServerSends);
    partial.resize(partial.size() / 2);
    tested.socket.reads = {Join({entry, partial})};
    static_cast<void>(tested.session.Poll());
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 03)"));

    tested.session.Start();

    const auto deliveredBeforeStart = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(deliveredBeforeStart.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(deliveredBeforeStart[0]));
    tested.gameServerSends = PacketSerials{};
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    tested.socket.reads = {FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* empty = std::get_if<InventoryList>(&events[0]);
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->items.empty());
}

// QuitOrdered keeps what came before and cuts the rest of that connection's batch.
TEST(ServerSessionTest, WorldEventsStopAtTheExistingConnectionBatchBoundary)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(LogoutResult(0), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends),
                                 FromTheGameServer(OtherWorldEntryReply(), tested.gameServerSends)})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// The handler's warnings would catch an accidental registration in the CS, even while rejecting the clear ones.
TEST(ServerSessionTest, WorldEventsDoNotDispatchConnectServerPacketsAsGameServerPackets)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    tested.socket.reads = {Join(
        {WorldEntryReply(), InventoryReply(), AddressReply(GameHost, GamePort), WorldEntryReply(), InventoryReply()})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 03)"));
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 10)"));
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 03)"));
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 10)"));

    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
}

// SetState(m_state) in the F3/03 handler would restart the wait: it must expire at the original timeout.
TEST(ServerSessionTest, WorldEntryDoesNotExtendTheOriginalReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.AdvanceTo(seconds{5});
    tested.session.RequestCharacterList();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));

    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
}

// The F3/10 is exercised alone: extending the deadline in either of the two handlers must fail.
TEST(ServerSessionTest, WorldInventoryDoesNotExtendTheOriginalReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.AdvanceTo(seconds{5});
    tested.session.RequestCharacterList();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    tested.socket.reads = {FromTheGameServer(InventoryReply(), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[0]));

    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
}

// Receiving or draining must not restart the global phase or change the keepalive's speeds or bytes.
TEST(ServerSessionTest, WorldEventsPreserveTheNextKeepaliveBytesAndTimerPhase)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.SetKeepaliveSpeeds(
        KeepaliveSpeeds{.attackSpeed = 0x033C, .magicSpeed = 0x027C, .characterFlags = KeepaliveAdjust20});
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    ASSERT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(milliseconds{11360});
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    ASSERT_EQ(tested.Keepalives().size(), 1u);

    tested.AdvanceTo(milliseconds{19999});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(seconds{20});
    static_cast<void>(tested.session.Poll());

    const std::vector<Packet> sent = tested.Keepalives();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_TRUE(
        SameBytes(sent[1].Bytes(), Bytes({0xC1, 0x0C, 0x0E, 0x00, 0x20, 0x4E, 0x00, 0x00, 0x28, 0x03, 0x68, 0x02})));
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

TEST(ServerSessionTest, StartsIdle)
{
    Harness tested(IdentityObfuscation::Shared());

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Idle);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_TRUE(tested.socket.connects.empty());
}

// The first thing sent to the ConnectServer is the A9, and only once the connect finishes.
TEST(ServerSessionTest, SendsThePetInfoRequestWhenTheConnectServerOpens)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.socket.pollsUntilConnected = 1;

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    ASSERT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_EQ(tested.socket.connects[0].host, ConnectHost);
    EXPECT_EQ(tested.socket.connects[0].port, ConnectPort);
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(tested.socket.sent.empty());

    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, PetInfoRequest()));
}

// 04's test: after C1 04 00 01, exactly C1 04 F4 02 leaves.
TEST(ServerSessionTest, AsksForTheListAfterTheHello)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
}

// The Opened goes before what arrived with it: the A9 leaves before the F4 02 even if the hello arrives in the same
// Poll as the connect.
TEST(ServerSessionTest, ThePetInfoRequestLeavesBeforeAHelloThatArrivesWithTheConnect)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.socket.reads = {Hello()};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
}

// With the list the session waits for the choice (CL5.5), with the list in arrival order and no F4 03 sent. The
// choice has no timeout: the player may take a while, and 60 s later it still waits without sending anything.
TEST(ServerSessionTest, WaitsForTheChoiceAfterTheList)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {
        Client::Tests::ServerListWithLoads(std::vector<ServerEntry>{{401, 10}, {400, 20}, {402, 100}})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    const std::span<const ServerEntry> list = tested.session.ServerList();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].code, 401);
    EXPECT_EQ(list[0].load, 10);
    EXPECT_EQ(list[1].code, 400);
    EXPECT_EQ(list[2].code, 402);
    EXPECT_EQ(list[2].load, 100);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    tested.PollEachFrameUntil(seconds{60});
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "la lista trae 3 servidores: se espera la elección"));
}

// Any one of the list, not the first: without FoxAnti, the 402 goes out as 92 01.
TEST(ServerSessionTest, ChoosingSendsTheCodeItWasGiven)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({401, 400, 402})};
    static_cast<void>(tested.session.Poll());

    tested.session.ChooseServer(402);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x92, 0x01})})));
}

// ChooseServer outside ChoosingServer: a warning and nothing sent, before the list and with the choice already sent.
TEST(ServerSessionTest, ChoosingOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.session.ChooseServer(LabServerCode);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));

    tested.socket.reads = {ServerList({LabServerCode, 401})};
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseServer(LabServerCode);
    const ByteVector sent = tested.socket.sent;
    tested.session.ChooseServer(401);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{LabServerCode});
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignora la elección de un servidor: la sesión no la espera",
                                        "[warning] se ignora la elección de un servidor: la sesión no la espera"}));
}

// The credits (0x641203 and 0x63f9a0): C1 04 F4 06 in clear through the open socket, without changing state; each
// close sends another.
TEST(ServerSessionTest, TheCreditsSendF406)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({401, 400})};
    static_cast<void>(tested.session.Poll());

    tested.session.SendCreditsNotice();
    tested.session.SendCreditsNotice();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x04, 0xF4, 0x06}),
                                                    Bytes({0xC1, 0x04, 0xF4, 0x06})})));
}

// Without an open socket, nothing: the original does not send with the socket at −1.
TEST(ServerSessionTest, TheCreditsNeedAnOpenSocket)
{
    Harness tested(IdentityObfuscation::Shared());

    tested.session.SendCreditsNotice();

    EXPECT_TRUE(tested.socket.sent.empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no se manda el F4 06 de los créditos"));
}

// The F4 03's address: the session keeps it, closes the CS and connects to the GS, without warnings.
TEST(ServerSessionTest, ReadsTheGameServerAddress)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    ASSERT_TRUE(tested.session.GameServer().has_value());
    const ServerAddress gameServer = tested.session.GameServer().value_or(ServerAddress{});
    EXPECT_EQ(gameServer.host, GameHost);
    EXPECT_EQ(gameServer.port, GamePort);
    EXPECT_EQ(tested.socket.closes, 1);
    ASSERT_EQ(tested.socket.connects.size(), 2u);
    EXPECT_EQ(tested.socket.connects[1].host, GameHost);
    EXPECT_EQ(tested.socket.connects[1].port, GamePort);
    EXPECT_EQ(tested.connection.State(), LinkState::Connecting);
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// From the ConnectServer to the GameServer the log names no host or port: neither the CS's, from the config, nor the
// GS's, from the wire.
TEST(ServerSessionTest, TheLogDoesNotSayTheAddresses)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());

    EXPECT_FALSE(tested.sink.lines.empty());
    EXPECT_EQ(tested.LinesWithAnAddress(), Lines{});
}

// The F4 03 and a hello in the same read: the hello is the CS's and the session stops walking the batch. Dispatched,
// it would go to the GS dispatcher, where the 00 is the chat and has no handler, and there would be a warning. When
// the GS opens nothing leaves: the GS sends the hello.
TEST(ServerSessionTest, WhatFollowsTheAddressIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    tested.socket.reads = {Join({AddressReply(GameHost, GamePort), Hello()})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);

    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// With the GameServer the 00 goes to its dispatcher, which has it as the chat (0x4e2b10, CL8 milestone 3): it is not
// the CS's hello and the session asks for no other list. What applies it decides with the scene: in scene 2,
// 0x4c6070 asks for the list through the GS without reading the packet (0x4c6098).
TEST(ServerSessionTest, WithTheGameServerThe00IsTheChat)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;

    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(), Lines{});
    EXPECT_EQ(tested.eventSink.routedAfter, (std::vector<std::size_t>{0}));

    tested.session.ResendServerListRequest();
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({sentToTheConnectServer, ListRequest()})));
}

// Question 9: an empty list is an error that says so, and the session closes the CS.
TEST(ServerSessionTest, AnEmptyListFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Bytes({0xC2, 0x00, 0x07, 0xF4, 0x06, 0x00, 0x00})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.closes, 1);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "la lista de servidores está vacía"));

    // The session asked for the close: it does not come back as a ConnectServerLost.
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
}

// The empty list and the CS's close in the same Poll: the session fails on the list, which came first, and stops
// walking the batch.
TEST(ServerSessionTest, AnEmptyListBeforeTheCloseKeepsItsFailure)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {ServerList({})};
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
}

// The F4 05 is the CS's reply to a busy server, with the server's byte (the original's handler, 0x4c1bf0, does not
// read it). Like the original, the session asks the same CS for the list again (Q-C71), and with the new list waits
// for another choice.
TEST(ServerSessionTest, ABusyServerAsksForTheListAgain)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    const ByteVector sent = tested.socket.sent;

    tested.socket.reads = {BusyReply()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({sent, ListRequest()})));
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el servidor elegido está ocupado: se pide la lista otra vez"));

    tested.socket.reads = {ServerList({401})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    ASSERT_EQ(tested.session.ServerList().size(), 1u);
    EXPECT_EQ(tested.session.ServerList()[0].code, 401);
}

// Before the choice an F4 05 answers nothing: it is ignored with a warning (Q-C72).
TEST(ServerSessionTest, ABusyAnswerOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {BusyReply()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.Warnings(),
              Lines{"[warning] se ignora el aviso de servidor ocupado (F4 05): llegó fuera de turno"});
}

// An F4 03 before the list is not the address: it is ignored with a warning, without connecting to anything.
TEST(ServerSessionTest, AnUnexpectedPacketIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_EQ(tested.Warnings(),
              Lines{"[warning] se ignora la dirección del GameServer (F4 03): llegó fuera de turno"});
}

// In 8 of 64 connections the original asks for the list twice (04 § El flujo de conexión): here a second hello is
// ignored with a warning and the F4 02 leaves only once.
TEST(ServerSessionTest, ASecondHelloIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora el saludo del ConnectServer (00): llegó fuera de turno"});
}

// A second list, with the server already chosen, does not choose another.
TEST(ServerSessionTest, AListOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    const ByteVector sent = tested.socket.sent;

    tested.socket.reads = {ServerList({401})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la lista de servidores (F4 06): llegó fuera de turno"});
}

// The ConnectServer connection has no keys: an arriving C3 is Undecryptable. The session does nothing with it (the
// warning is the connection's, a single one) and the rest of the batch is dispatched.
TEST(ServerSessionTest, AnUndecryptablePacketIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Join({Bytes({0xC3, 0x05, 0x11, 0x22, 0x33}), ServerList({LabServerCode})})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.ServerList().size(), 1u);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se descarta un C3 que llegó: esta conexión no tiene claves"});
}

// A list that says two servers and carries one does not read: it is ignored with a warning, and the session keeps
// waiting for one that works.
TEST(ServerSessionTest, AListThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x02, 0x90, 0x01, 0x00, 0x00})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la lista de servidores (F4 06): no se puede leer"});
}

// The same with an address that ends before the port.
TEST(ServerSessionTest, AnAddressThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());

    tested.socket.reads = {Bytes({0xC1, 0x06, 0xF4, 0x03, 0x31, 0x00})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la dirección del GameServer (F4 03): no se puede leer"});
}

// An F4 that ends at the opcode has no subcode: the Dispatcher gives Malformed without warning, and the session
// logs the warning, without the bytes.
TEST(ServerSessionTest, AMalformedPacketIsLogged)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Bytes({0xC1, 0x03, 0xF4})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se descarta un paquete mal formado del ConnectServer"});
}

TEST(ServerSessionTest, LosingTheConnectServerFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el ConnectServer: {}",
                                                           CloseReasonText(CloseReason::PeerClosed))));
}

// Without a timeout the wait for the choice does not hang: a CS that closes with the list on screen is a
// ConnectServerLost.
TEST(ServerSessionTest, LosingTheConnectServerWhileChoosingFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// And the account's: a GS that closes with the account on screen is a GameServerLost.
TEST(ServerSessionTest, LosingTheGameServerWhileWaitingForTheCredentialsFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// Also while connecting: a CS that refuses the connection.
TEST(ServerSessionTest, AConnectServerThatRefusesFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.socket.connectResult = SocketError::Refused;

    tested.session.Start();
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
    EXPECT_TRUE(tested.socket.sent.empty());
}

// The GameServer the CS gave refuses the connection.
TEST(ServerSessionTest, LosingTheGameServerFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    tested.socket.connectResult = SocketError::Refused;

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el GameServer: {}",
                                                           CloseReasonText(CloseReason::Refused))));
}

// A Send from a handler can close the connection (here, the F4 02 to a CS that already closed): the rest of the
// batch is not dispatched, and the close comes out in the next Poll.
TEST(ServerSessionTest, WhatFollowsACloseInAHandlerIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.sendError = SocketError::Closed;
    tested.socket.reads = {Join({Hello(), ServerList({LabServerCode})})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "se descarta un C1 F4 que iba a salir"));

    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// If the CS closes in the same Poll a packet arrives, the packet is dispatched and then the close: the session does
// not stay waiting for a list that will not come.
TEST(ServerSessionTest, WhatArrivesBeforeTheServerClosesIsHandled)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.reads = {Hello()};
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Start starts over from any state, and forgets the failure and the previous GameServer.
TEST(ServerSessionTest, StartingAgainStartsClean)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({})};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    tested.socket.reads = {Hello(), ServerList({LabServerCode})};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    tested.session.ChooseServer(LabServerCode);
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    // The first Poll reads the address and opens the GS; the second gives its Opened.
    static_cast<void>(tested.session.Poll());
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_TRUE(tested.session.ServerList().empty());
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    ASSERT_EQ(tested.socket.connects.size(), 4u);
    EXPECT_EQ(tested.socket.connects[3].host, ConnectHost);
    EXPECT_EQ(tested.socket.connects[3].port, ConnectPort);
    const ByteVector before = tested.socket.sent;
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(std::span(tested.socket.sent).subspan(before.size()), PetInfoRequest()));
    EXPECT_EQ(tested.LinesWithAnAddress(), Lines{});
}

// The F4 03's GameServer: the session connects to that one, and when it opens waits for the hello without sending
// anything, because the GS greets first.
TEST(ServerSessionTest, ConnectsToTheGameServerItWasGiven)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    ASSERT_EQ(tested.socket.connects.size(), 2u);
    EXPECT_EQ(tested.socket.connects[1].host, GameHost);
    EXPECT_EQ(tested.socket.connects[1].port, GamePort);

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "conectó con el GameServer"));
}

// With the GS's hello accepted and the version, the session waits for the account and the password (CL5.5): the
// global timer's keepalive leaves, but not the login, and the human wait has no timeout. After 60 s it still waits
// for credentials and has sent the original's three keepalives.
TEST(ServerSessionTest, WaitsForTheCredentialsAfterTheGameHello)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer saludó: se espera la cuenta y la clave"));
    tested.PollEachFrameUntil(seconds{60});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);
    EXPECT_EQ(tested.Keepalives().size(), 3u);
}

// 04's test: with C1 0C F1 00 01 00 05 "10209" and the account and password, the login leaves encrypted as C3 with
// serial 0, and the GS decrypts it to WriteLoginRequest's 49 bytes with the account, the password and the clock's
// tick at SubmitCredentials.
TEST(ServerSessionTest, SubmittingSendsTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.clock.Advance(std::chrono::milliseconds{123456});

    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.serials, std::vector<SerialCheck>{SerialCheck::InOrder});
    EXPECT_EQ(received.packets[0].Bytes().size(), 49u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[0].Bytes(), expected.Value().packet.Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se manda el login"));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// SubmitCredentials outside WaitingCredentials: a warning, without the account or the password, and nothing sent.
// Before the hello, and with the login already sent.
TEST(ServerSessionTest, SubmittingOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.session.SubmitCredentials(TheCredentials());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);

    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    tested.session.SubmitCredentials(TheCredentials());
    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 1u);
    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignoran la cuenta y la clave: la sesión no las espera",
                                        "[warning] se ignoran la cuenta y la clave: la sesión no las espera"}));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// The lab GS sends the hello encrypted: the GS connection has the keys and decrypts it.
TEST(ServerSessionTest, AHelloThatArrivesEncryptedAlsoWaitsForTheCredentials)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// A hello with a result other than 1: HelloRejected, no login, and the session closes the GS. It is checked before
// the version: with both wrong, it is also HelloRejected.
TEST(ServerSessionTest, ARejectedHelloFails)
{
    for (const std::string_view version : {ClientVersion, OtherVersion})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

        tested.socket.reads = {GameHello(0, version)};
        static_cast<void>(tested.session.Poll());

        EXPECT_EQ(tested.session.State(), SessionState::Failed) << version;
        EXPECT_EQ(tested.session.Failure(), SessionFailure::HelloRejected) << version;
        EXPECT_EQ(tested.connection.State(), LinkState::Closed) << version;
        EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart) << version;
        EXPECT_TRUE(tested.Logged(LogLevel::Error, "el GameServer rechazó la conexión (resultado 0)")) << version;
    }
}

// Another version, with the hello accepted: VersionMismatch, no login, with both versions in the log.
TEST(ServerSessionTest, AnotherVersionFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {GameHello(1, OtherVersion)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::VersionMismatch);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("el GameServer tiene la versión {} y este cliente la {}",
                                                           OtherVersion, ClientVersion)));
}

// The version comes from the server: a byte that does not print (a \0, a line break, a DEL) is logged as '?', so the
// log stays one line.
TEST(ServerSessionTest, AVersionThatDoesNotPrintIsLoggedWithQuestionMarks)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    ByteVector hello = GameHello(1, ClientVersion);
    hello[8] = std::byte{0x00};
    hello[9] = std::byte{0x0A};
    hello[10] = std::byte{0x7F};

    tested.socket.reads = {hello};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::VersionMismatch);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "la versión 1???9 y este cliente la 10209"));
}

TEST(ServerSessionTest, AnAcceptedLoginIsLoggedIn)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer aceptó el login"));
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Another result: like the original, which goes back to state 2 with the GS open (0x643560), the session waits again
// for the account and the password, with the result kept for the scene's notice. The next login leaves with the next
// serial and forgets the previous result.
TEST(ServerSessionTest, ARejectedLoginKeepsItsResult)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.socket.reads = {FromTheGameServer(LoginResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el GameServer rechazó el login (resultado 0)"));
    tested.PollEachFrameUntil(seconds{60});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.Keepalives().size(), 3u);

    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 5u);
    EXPECT_TRUE(
        std::ranges::all_of(received.serials, [](SerialCheck serial) { return serial == SerialCheck::InOrder; }));
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    EXPECT_TRUE(SameBytesWithoutValues(received.packets.back().Bytes(), expected.Value().packet.Bytes()));
}

// A C3 with a bad checksum: the session sends the F1 03, which the GS decrypts to C1 06 F1 7B C1 73 with serial 1
// (the login spent 0), and keeps waiting for the result. The one that arrives later, with the next serial, is read.
TEST(ServerSessionTest, ABadChecksumSendsTheReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    ByteVector bad = FromTheGameServer(LoginResult(1), tested.gameServerSends);
    const std::size_t firstChecksum = HeaderSize(PacketType::C3) + CipherBlockSize - 1;
    ASSERT_GT(bad.size(), firstChecksum);
    bad[firstChecksum] ^= std::byte{0xFF};

    tested.socket.reads = {bad};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_EQ(received.serials, (std::vector<SerialCheck>{SerialCheck::InOrder, SerialCheck::InOrder}));
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[1].Bytes(), NoticeBytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se le avisa al GameServer (F1 03)"));

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// What does not measure whole blocks also gets the report, as the contract says.
TEST(ServerSessionTest, APacketThatIsNotWholeBlocksSendsTheReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.socket.reads = {Bytes({0xC3, 0x05, 0x11, 0x22, 0x33})};
    static_cast<void>(tested.session.Poll());

    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[1].Bytes(), NoticeBytes()));
}

// With the other errors, only the connection's warning: a C3 that passes the checksum and has no opcode (TooShort)
// does not send the F1 03.
TEST(ServerSessionTest, AnotherUndecryptablePacketSendsNoReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.socket.reads = {OnlyTheSerial()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.Warnings().size(), 1u);
}

TEST(ServerSessionTest, AHelloAfterTheLoginDoesNotSendAnotherLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_TRUE(tested.Warnings().empty());
    const auto events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Client::App::GuildTableReset>(events[0]));
}

// A result before the hello is not the login's.
TEST(ServerSessionTest, ALoginResultOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {LoginResult(1)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora el resultado del login (F1 01): llegó fuera de turno"});
}

// A hello that ends before the version, and a result without the result: they do not read, and the session keeps
// waiting.
TEST(ServerSessionTest, WhatDoesNotReadFromTheGameServerIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Bytes({0xC1, 0x07, 0xF1, 0x00, 0x01, 0x00, 0x05})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);

    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    tested.session.SubmitCredentials(TheCredentials());
    tested.socket.reads = {Bytes({0xC1, 0x04, 0xF1, 0x01})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);

    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignora el saludo del GameServer (F1 00): no se puede leer",
                                        "[warning] se ignora el resultado del login (F1 01): no se puede leer"}));
}

// An 11-byte account does not fit in the login (neither ReadCredentials nor the TextField let it through): the
// session sends nothing, fails with CredentialsTooLong and the error does not carry the account.
TEST(ServerSessionTest, CredentialsThatDoNotFitFail)
{
    const std::string tooLong = std::string(Account) + "xyz";
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.session.SubmitCredentials(Credentials{tooLong, std::string(Password)});

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::CredentialsTooLong);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "no se puede escribir el login"));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// The GS that closes with the login accepted: GameServerLost, with the reason in the log.
TEST(ServerSessionTest, LosingTheGameServerAfterTheLoginFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el GameServer: {}",
                                                           CloseReasonText(CloseReason::PeerClosed))));
}

// The order to close the game (Q-C106): the F1 02 with 0, encrypted as the GS sends it at the end of its 5 s
// countdown. The session closes the connection and stays in QuitOrdered, with no failure and no errors in the log:
// it is not a GameServerLost, and main.cpp closes the game without a notice.
TEST(ServerSessionTest, TheGameServersQuitOrderClosesWithoutAFailure)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer ordenó cerrar el juego (F1 02 con 0)"));
    EXPECT_EQ(tested.Warnings(), Lines{});
    EXPECT_EQ(tested.CountLogged(LogLevel::Error, ""), 0u);
}

// 0x4c2710 does not look at the scene: the order also closes in the selection, and the list goes with it.
TEST(ServerSessionTest, TheQuitOrderAlsoClosesInTheSelection)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
}

// The GS closes the connection right after the order (CloseClient), and both can arrive in the same read: the close
// is the last of the batch and is not walked. Still no failure.
TEST(ServerSessionTest, TheCloseThatFollowsTheQuitOrderIsNotALoss)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_FALSE(tested.Logged(LogLevel::Error, "se perdió la conexión"));
}

// 0x4c2710 looks neither at the scene nor at whether it arrived encrypted: the order in clear, with the account on
// screen, also closes.
TEST(ServerSessionTest, AQuitOrderInClearWhileWaitingForTheCredentialsAlsoCloses)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.socket.reads = {LogoutResult(0)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
}

// With the order the keepalive stops, the session does not reconnect and waits for nothing: in the next 40 s nothing
// leaves, nothing connects and there is no NoReply.
TEST(ServerSessionTest, TheQuitOrderStopsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::size_t sentBefore = tested.socket.sent.size();
    const std::size_t connectsBefore = tested.socket.connects.size();

    tested.PollEachFrameUntil(seconds{43});

    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
}

// Outside 0/1/2 the original does nothing; without the result, it cannot be read.
TEST(ServerSessionTest, TheOtherLogoutResultsAreIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(3), tested.gameServerSends), Bytes({0xC1, 0x04, 0xF1, 0x02})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    const auto ignored = [](int result)
    {
        return std::format("[warning] se ignora la orden de salida del GameServer (F1 02 con {}): motivo desconocido",
                           result);
    };
    EXPECT_EQ(tested.Warnings(),
              (Lines{ignored(3), "[warning] se ignora la orden de salida del GameServer (F1 02): no se puede leer"}));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// What reached the GS from `from` on, without the XOR32.
std::vector<Packet> GameServerPacketsFrom(const Harness& tested, std::size_t from)
{
    const GameServerReceived received = tested.GameServerReads();
    std::vector<Packet> packets;
    for (std::size_t index = from; index < received.packets.size(); ++index)
    {
        packets.push_back(Xor32Decode(received.packets[index]));
    }
    return packets;
}

// The recorder does not implement the world. These wire cases explicitly simulate an empty box; the real condition
// and interleaving are tested with SessionEventApplier/ReturnToSelection.
Client::Core::Expected<void, std::string> ApplyAnEmptyWorldReset(const WorldEvent& event, ServerSession& session)
{
    if (std::holds_alternative<Client::App::SelectionWorldReset>(event) ||
        std::holds_alternative<Client::App::MapServerJoined>(event))
    {
        session.SendWindowClose();
        session.SendWindowClose();
    }
    return {};
}

// In the world, with the F3 03 published by the sink.
void ReachInWorld(Harness& tested)
{
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    tested.eventSink.onWorld = [](const WorldEvent& event,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        if (std::holds_alternative<CharacterSelectResult>(event))
        {
            session.PublishWorldEntry();
        }
        return ApplyAnEmptyWorldReset(event, session);
    };
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.session.State(), SessionState::InWorld);
    static_cast<void>(tested.eventSink.TakeWorldEvents());
}

// 0x4c3626..0x4c390f: the encrypted F1 02 with 1 gives the sink the return to the selection, with the session still
// in the world, and then the F3 00 (0x4c3653) and the two C1 03 31 (0x419900 and 0x4499b0) leave, in that order; the
// old list and choice are forgotten and the session waits for the new one. With it, one is chosen and entered again.
TEST(ServerSessionTest, AnEncryptedLogoutWithOneGoesToTheSelection)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {FromTheGameServer(LogoutResult(1), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Client::App::ReturnToSelectionReceived>(events[0]));
    EXPECT_TRUE(std::holds_alternative<Client::App::SelectionWorldReset>(events[1]));
    EXPECT_EQ(tested.eventSink.worldStates[tested.eventSink.worldStates.size() - 2], SessionState::InWorld);
    EXPECT_EQ(tested.eventSink.worldStates.back(), SessionState::WaitingCharacterList);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_FALSE(tested.session.JoinedWorld());
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    const std::vector<Packet> sent = GameServerPacketsFrom(tested, sentBefore);
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_TRUE(SameBytes(sent[0].Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x00})));
    EXPECT_TRUE(SameBytes(sent[1].Bytes(), Bytes({0xC1, 0x03, 0x31})));
    EXPECT_TRUE(SameBytes(sent[2].Bytes(), Bytes({0xC1, 0x03, 0x31})));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer ordenó volver a la selección (F1 02 con 1)"));

    // The new selection does not ask for the list again: the session does not expect it.
    tested.session.RequestCharacterList();
    EXPECT_EQ(GameServerPacketsFrom(tested, sentBefore).size(), 3u);
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    EXPECT_EQ(tested.CharacterPacketsSent(SelectSubcode).size(), 2u);
}

// 0x4c2ef3..0x4c2f51: closes the GS and leaves state 0, without error; scene 2 starts the CS.
TEST(ServerSessionTest, AnEncryptedTwoClosesTheOldConnectionWithoutConsumingItsRemainingPackets)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.socket.sent.size();
    tested.socket.reads = {FromTheGameServer(LogoutResult(2), tested.gameServerSends),
                           CharacterListBytes(TwoCharacters())};
    tested.socket.peerClosed = true;

    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(tested.session.State(), SessionState::Idle);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.eventSink.TakeWorldEvents().size(), 2u);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_FALSE(tested.Logged(LogLevel::Error, "se perdió la conexión"));
    tested.PollEachFrameUntil(seconds{43});
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    tested.socket.peerClosed = false;
    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
}

// Unlike the 1, the 2 clears the game BEFORE checking the encryption (0x4c275c..0x4c2781).
TEST(ServerSessionTest, AClearTwoStillDeliversTheCleanupAndReportsWithoutClosing)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    tested.socket.reads = {LogoutResult(2)};

    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.eventSink.TakeWorldEvents().size(), 1u);
    const auto sent = GameServerPacketsFrom(tested, sentBefore);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(sent[0].Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF1})));
}

TEST(ServerSessionTest, EveryReadableLogoutResultReleasesThePendingUiRequest)
{
    for (const int result : {0, 1, 2, 3})
    {
        SCOPED_TRACE(result);
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        bool requested = true;
        tested.eventSink.onLogoutResponse = [&] { requested = false; };
        tested.socket.reads = {LogoutResult(result)};

        ASSERT_TRUE(tested.session.Poll().HasValue());

        EXPECT_FALSE(requested);
    }
}

// 0x4c2f5b: unencrypted, the 1 neither clears nor changes scene: it only reports to the GS with the F1 03 00 F1, to
// encrypt (Q-C211). The session stays in the world and the sink receives nothing.
TEST(ServerSessionTest, AClearOneChangesNothing)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    const std::size_t encryptedBefore = tested.GameServerReads().serials.size();

    tested.socket.reads = {LogoutResult(1)};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_TRUE(tested.session.Characters().has_value());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF1})));
    EXPECT_EQ(received.serials.size(), encryptedBefore + 1);
    EXPECT_EQ(tested.Warnings(),
              (Lines{"[warning] la orden de volver a la selección (F1 02 con 1) no llegó cifrada: se le avisa al "
                     "GameServer (F1 03) y se sigue igual"}));
}

// The 03's key as the GS sends it (GCCheckMainExeKeySend): C1 06 03, a padding byte and the key.
std::vector<std::byte> MainCheckKey(std::uint16_t key)
{
    return Bytes({0xC1, 0x06, 0x03, 0x00, static_cast<int>(key & 0xFFU), static_cast<int>(key >> 8U)});
}

// Table 0's key with rand() at 0 (the GS's EncryptCheckSumKey).
constexpr std::uint16_t TableZeroKey = 0xB479;

// 0x4e16c0: answers the GS's 03 in the same Poll with the sum of the file the command line names, as C3 (otherwise
// the GS closes after 3 s). With 23 bytes from 0x10 on and table 0, MainCheckTest's sum.
TEST(ServerSessionTest, TheMainCheckIsAnsweredWithTheSumOfTheExecutable)
{
    Client::Tests::TemporaryFolder folder("main-check");
    const std::filesystem::path executable = folder.Path() / "Main.exe";
    {
        std::ofstream file(executable, std::ios::binary);
        for (int value = 0x10; value < 0x10 + 23; ++value)
            file.put(static_cast<char>(value));
    }
    SessionSettings settings = Settings();
    settings.mainCheckFile = executable;
    Harness tested(IdentityObfuscation::Shared(), settings);
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    const std::size_t encryptedBefore = tested.GameServerReads().serials.size();

    tested.socket.reads = {FromTheGameServer(MainCheckKey(TableZeroKey), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(),
                          Bytes({0xC1, 0x08, 0x03, 0x00, 0x65, 0x74, 0x38, 0xA5})));
    EXPECT_EQ(received.serials.size(), encryptedBefore + 1);
    EXPECT_TRUE(tested.Warnings().empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se contestó la suma del ejecutable (03)"));
}

// Without the file (0x4aec28, the failing CreateFileA) the sum is 0, also in clear from the GS: the dispatcher does
// not look at the encryption. A folder does not open either.
TEST(ServerSessionTest, WithoutTheExecutableTheMainCheckSumIsZero)
{
    Client::Tests::TemporaryFolder folder("main-check-missing");
    for (const std::filesystem::path& missing : {folder.Path() / "Main.exe", folder.Path()})
    {
        SCOPED_TRACE(missing.string());
        SessionSettings settings = Settings();
        settings.mainCheckFile = missing;
        Harness tested(IdentityObfuscation::Shared(), settings);
        ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
        const std::size_t sentBefore = tested.GameServerReads().packets.size();

        tested.socket.reads = {MainCheckKey(TableZeroKey)};
        ASSERT_TRUE(tested.session.Poll().HasValue());

        const GameServerReceived received = tested.GameServerReads();
        ASSERT_EQ(received.packets.size(), sentBefore + 1);
        EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(),
                              Bytes({0xC1, 0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00})));
        ASSERT_EQ(tested.Warnings().size(), 1U);
        EXPECT_NE(tested.Warnings().front().find("la suma del ejecutable (03) va en 0"), std::string::npos);
    }
}

// A 03 without the key is ignored with a warning and gets no answer.
TEST(ServerSessionTest, AMainCheckWithoutItsKeyIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {Bytes({0xC1, 0x05, 0x03, 0x00, 0x79})};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
    EXPECT_EQ(tested.Warnings(),
              (Lines{"[warning] se ignora la clave de la suma del ejecutable (03): no se puede leer"}));
}

// The 1 does not look at the state: also with the login just accepted, like the original, which does not look at the
// scene. If the sink cannot clear, the session fails and sends nothing.
TEST(ServerSessionTest, ALogoutWithOneThatFailsToApplyIsNotFollowed)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::LoggedIn);
        return Client::Core::Unexpected(std::string("sin el mundo"));
    };

    tested.socket.reads = {FromTheGameServer(LogoutResult(1), tested.gameServerSends)};
    const auto polled = tested.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "sin el mundo");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
}

// The ConnectServer has no F1: its dispatcher lacks the order (CL4.3), and an F1 02 with 0 from it closes nothing.
// The original has a single dispatcher and would close; a CS does not send it.
TEST(ServerSessionTest, TheConnectServerCannotOrderTheQuit)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {LogoutResult(0)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
}

// QuitOrdered is final: the game closes in that frame, and a Start from the scene (the account's Esc) does not
// reconnect. A warning, and the session stays the same.
TEST(ServerSessionTest, TheQuitOrderIsFinal)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::size_t connectsBefore = tested.socket.connects.size();
    const std::uint32_t startsBefore = tested.session.StartCount();

    tested.session.Start();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.StartCount(), startsBefore);
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] no se vuelve a conectar: el GameServer ordenó cerrar el juego"});
}

// What arrives after the order in the same read is not dispatched, like the original, which empties its queue on
// close (0x4e8380): an extra hello leaves no out-of-turn warning.
TEST(ServerSessionTest, WhatFollowsTheQuitOrderIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    const ByteVector order = FromTheGameServer(LogoutResult(0), tested.gameServerSends);
    const ByteVector hello = FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends);
    tested.socket.reads = {Join({order, hello})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Question 7: from Start to LoggedIn, no log line carries the account or the password.
TEST(ServerSessionTest, TheLogDoesNotHaveTheCredentials)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    EXPECT_FALSE(tested.sink.lines.empty());
    EXPECT_FALSE(tested.LoggedTheCredentials());
    EXPECT_EQ(tested.LinesWithAnAddress(), Lines{});
}

// Start forgets the login result, and the CS opens again without keys: a C3 there does not send the F1 03.
TEST(ServerSessionTest, StartingAgainForgetsTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);
    static_cast<void>(tested.session.Poll());
    const std::size_t sentBefore = tested.socket.sent.size();
    tested.socket.reads = {Bytes({0xC3, 0x05, 0x11, 0x22, 0x33})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
}
// The first one leaves in the same Poll that reads the accepted login, like 0x4ad100's: a C3 with serial 1 (the
// login spent 0) that the GS decrypts to the same as WriteKeepalive with the clock's tick at that moment and the
// speeds at 0 and 0, without adjustment.
TEST(ServerSessionTest, SendsTheFirstKeepaliveWithTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(milliseconds{12345}));

    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_EQ(received.serials, (std::vector<SerialCheck>{SerialCheck::InOrder, SerialCheck::InOrder}));
    const auto expected = WriteKeepalive(Keepalive{TickCountMs(tested.clock.Now()), 0, 0});
    EXPECT_TRUE(SameBytes(received.packets[1].Bytes(), expected.packet.Bytes()));
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
}

// Without SetKeepaliveSpeeds the keepalive without the XOR32 carries 0 and 0 in both speeds and the 00 after the
// opcode, like the first of the 8 connections to the GS (CL4.R, step 6).
TEST(ServerSessionTest, WithoutACharacterTheSpeedsAreZero)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    const std::vector<Packet> keepalives = tested.Keepalives();
    ASSERT_EQ(keepalives.size(), 1u);
    const std::span<const std::byte> bytes = keepalives[0].Bytes();
    ASSERT_EQ(bytes.size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    EXPECT_EQ(bytes[KeepalivePaddingIndex], std::byte{0});
    EXPECT_TRUE(SameBytes(bytes.subspan(KeepaliveSpeedsIndex), Bytes({0x00, 0x00, 0x00, 0x00})));
}

// The login at t = 3 s does not restart the global timer: none new at 19.999 s; at 20 s, one; up to t = 100 s, 6 in
// total (3, 20, 40, 60, 80 and 100), with serials 1 to 6. LoggedIn has no timeout: with nothing from the GS in 100 s,
// the session goes on.
TEST(ServerSessionTest, KeepaliveEveryTwentySecondsAndNotBefore)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    tested.AdvanceTo(milliseconds{19999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(seconds{20});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(seconds{100});

    EXPECT_EQ(tested.Keepalives().size(), 6u);
    EXPECT_EQ(tested.GameServerReads().serials.size(), 7u);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// The original's timer 1000 looks at the GS connection, not the login (04 § El flujo de conexión). In 98 s four
// keepalives leave, while waiting for the hello, the credentials and the result, without extending the limits on the
// server's replies.
TEST(ServerSessionTest, KeepaliveBeforeTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.PollEachFrameUntil(seconds{29});
    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    tested.PollEachFrameUntil(seconds{69});
    tested.session.SubmitCredentials(TheCredentials());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingLoginResult);

    tested.PollEachFrameUntil(seconds{98});

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 5u);
    EXPECT_EQ(tested.Keepalives().size(), 4u);
}

// A prelogin keepalive spends serial 0; the login spends 1 and its reply sends the 2. The next timer still expires at
// 40 s, not at 45 after the accepted login.
TEST(ServerSessionTest, AcceptingTheLoginDoesNotRestartTheGlobalTimer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.PollEachFrameUntil(seconds{25});
    ASSERT_EQ(tested.Keepalives().size(), 1u);

    tested.session.SubmitCredentials(TheCredentials());
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(milliseconds{39999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.Keepalives().size(), 3u);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 4u);
}

// The timer runs without a connection and with the CS: those expirations neither send nor stay pending. The GS
// opened at 65 s waits for the next global expiration, at 80 s.
TEST(ServerSessionTest, TheGlobalTimerConsumesTicksWithoutAGameServer)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.PollEachFrameUntil(seconds{20});
    EXPECT_TRUE(tested.socket.sent.empty());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const std::size_t sentToConnectServer = tested.socket.sent.size();
    tested.PollEachFrameUntil(seconds{65});
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(tested.socket.sent.size(), sentToConnectServer);

    tested.session.ChooseServer(LabServerCode);
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());
    tested.gameServerStart = tested.socket.sent.size();
    static_cast<void>(tested.session.Poll());
    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(milliseconds{79999});
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{80});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 1u);
}

// Cancelling or reconnecting keeps the timer, but each connection resets its serial to 0.
TEST(ServerSessionTest, ReconnectingDoesNotRestartTheGlobalTimer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.PollEachFrameUntil(seconds{25});
    ASSERT_EQ(tested.Keepalives().size(), 1u);
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.AdvanceTo(milliseconds{39999});
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 1u);
    EXPECT_EQ(tested.GameServerReads().serials, (std::vector<SerialCheck>{SerialCheck::InOrder}));
}

// The immediate send of the accepted F1 01 and the expired timer are independent producers.
TEST(ServerSessionTest, TheLoginAndTheDueTimerCanEachSendAKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.AdvanceTo(seconds{20});
    ASSERT_TRUE(tested.Keepalives().empty());
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);
    tested.AdvanceTo(milliseconds{39999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 3u);
}

// A non-blocking connect still in progress neither spends serials nor queues the 20 s tick.
TEST(ServerSessionTest, AConnectingGameServerDoesNotQueueTheDueKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    tested.AdvanceTo(seconds{19});
    tested.socket.pollsUntilConnected = 2;
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());
    tested.gameServerStart = tested.socket.sent.size();
    tested.AdvanceTo(seconds{20});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.connection.State(), LinkState::Connecting);
    tested.AdvanceTo(seconds{21});
    static_cast<void>(tested.session.Poll());
    tested.AdvanceTo(seconds{22});
    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 1u);
}

// If the immediate send fails, the periodic one is not attempted and the rest of the batch is not processed.
TEST(ServerSessionTest, AFailedImmediateKeepaliveDoesNotAttemptTheDueTimer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.AdvanceTo(seconds{20});
    tested.socket.sendError = SocketError::Closed;
    tested.socket.reads = {Join({LoginResult(1), LogoutResult(0)})};
    const std::size_t sendAttempts = tested.socket.sendAttempts;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.socket.sendAttempts, sendAttempts + 1);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "keepalive"), 0u);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "que iba a salir: la conexión está cerrada"));
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// The peer may close after delivering F1 01: what was dropped is not logged as sent.
TEST(ServerSessionTest, AClosingLoginBatchDoesNotLogASentKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.socket.peerClosed = true;
    const std::size_t sendAttempts = tested.socket.sendAttempts;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_EQ(tested.socket.sendAttempts, sendAttempts);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "keepalive"), 0u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "que iba a salir: la conexión está cerrada"));
    EXPECT_EQ(tested.GameServerReads().packets.size(), 1u);
}

// Between the 0E that closes the GS and its deferred Closed, the UI must not erase the rejection or fake a login.
TEST(ServerSessionTest, AFailedPreloginKeepalivePreservesTheCredentialsStateUntilClosed)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.socket.reads = {FromTheGameServer(LoginResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    tested.AdvanceTo(seconds{20});
    tested.socket.sendError = SocketError::Closed;
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.connection.State(), LinkState::Closed);
    const std::size_t loginMessages = tested.CountLogged(LogLevel::Info, "se manda el login");
    const std::size_t sendAttempts = tested.socket.sendAttempts;
    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "se manda el login"), loginMessages);
    EXPECT_EQ(tested.socket.sendAttempts, sendAttempts);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "keepalive"), 0u);
    EXPECT_FALSE(tested.LoggedTheCredentials());
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// A 60 s frame (a breakpoint) with the login at t = 3 s: at t = 63 s only one leaves, with serial 2, and the next at
// 83 s, without recovering the 20, 40 and 60 s expirations.
TEST(ServerSessionTest, ASixtySecondFrameSendsOneKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    tested.AdvanceTo(seconds{63});
    static_cast<void>(tested.session.Poll());
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(milliseconds{82960});
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(seconds{83});
    EXPECT_EQ(tested.Keepalives().size(), 3u);
}

// With the GameLoop and the window minimized the Poll goes on (CL4.1) and the keepalive with it: 100 s after the
// login, the login's and five more. Main puts the session in the SceneManager's Poll, as here, with a scene that does
// nothing in place of the selection; nothing is drawn.
TEST(ServerSessionTest, KeepsTheKeepaliveWhileMinimized)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    RecordingFrameTarget frame;
    SceneManager scenes([](SceneId) -> std::unique_ptr<IScene> { return std::make_unique<IdleScene>(); }, frame,
                        tested.clock, [&tested] { return tested.session.Poll(); }, tested.log);
    ASSERT_TRUE(scenes.Start(SceneId::CharacterSelect).HasValue());
    GameLoop loop{tested.clock, scenes, PacingMode::Decoupled};
    WindowState minimized;
    minimized.minimized = true;
    InputState input;
    const Duration end = tested.clock.Now() + seconds{100};

    while (tested.clock.Now() <= end)
    {
        loop.RunFrame(minimized, input);
    }

    EXPECT_EQ(tested.Keepalives().size(), 6u);
    EXPECT_GT(loop.Stats().frames, 0u);
    EXPECT_TRUE(frame.frames.empty());
}

// The speeds SetKeepaliveSpeeds gave, with KeepaliveSpeed's adjustment: 0x0328 + 20 and 0x0268 + 20 with
// KeepaliveAdjust20 come out as 28 03 68 02. The XOR32 is still on after decrypting: it is checked without it.
TEST(ServerSessionTest, UsesTheSpeedsItWasGiven)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.SetKeepaliveSpeeds(KeepaliveSpeeds{.attackSpeed = std::uint16_t{0x0328 + 20},
                                                      .magicSpeed = std::uint16_t{0x0268 + 20},
                                                      .characterFlags = KeepaliveAdjust20});
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    const std::vector<Packet> keepalives = tested.Keepalives();
    ASSERT_EQ(keepalives.size(), 1u);
    const std::span<const std::byte> bytes = keepalives[0].Bytes();
    ASSERT_EQ(bytes.size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    EXPECT_TRUE(SameBytes(bytes.subspan(KeepaliveSpeedsIndex), Bytes({0x28, 0x03, 0x68, 0x02})));
}

// Speeds that arrive with the login already accepted (CL6 gives them on choosing the character) go in the next
// keepalive: the login's leaves with 0 and 0, and the 20 s one with the new ones.
TEST(ServerSessionTest, SpeedsGivenAfterTheLoginGoInTheNextKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.session.SetKeepaliveSpeeds(KeepaliveSpeeds{.attackSpeed = std::uint16_t{0x0328 + 20},
                                                      .magicSpeed = std::uint16_t{0x0268 + 20},
                                                      .characterFlags = KeepaliveAdjust20});

    tested.AdvanceTo(seconds{23});
    static_cast<void>(tested.session.Poll());

    const std::vector<Packet> keepalives = tested.Keepalives();
    ASSERT_EQ(keepalives.size(), 2u);
    ASSERT_EQ(keepalives[0].Bytes().size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    ASSERT_EQ(keepalives[1].Bytes().size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    EXPECT_TRUE(SameBytes(keepalives[0].Bytes().subspan(KeepaliveSpeedsIndex), Bytes({0x00, 0x00, 0x00, 0x00})));
    EXPECT_TRUE(SameBytes(keepalives[1].Bytes().subspan(KeepaliveSpeedsIndex), Bytes({0x28, 0x03, 0x68, 0x02})));
}

// The GS that cuts with the login accepted: GameServerLost with PeerClosed in the log, the keepalive stops and the
// session does not reconnect on its own. Nothing leaves in the next 40 s.
TEST(ServerSessionTest, LosingTheGameServerStopsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el GameServer: {}",
                                                           CloseReasonText(CloseReason::PeerClosed))));
    const std::size_t sentBefore = tested.socket.sent.size();
    const std::size_t connectsBefore = tested.socket.connects.size();

    tested.PollEachFrameUntil(seconds{43});

    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
}

// Start from LoggedIn goes back to the CS: the timer goes on, but sends no keepalive to the CS, only the A9.
TEST(ServerSessionTest, StartingAgainStopsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.session.Start();
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.PollEachFrameUntil(seconds{25});

    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(std::span(tested.socket.sent).subspan(sentBefore), PetInfoRequest()));
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
}

// A CS that accepts and does not greet: after the A9's ReplyTimeout the session closes and fails with NoReply, and the
// log says which server and what was expected. One ms before, it still waits.
TEST(ServerSessionTest, AConnectServerThatDoesNotGreetFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.AdvanceTo(ReplyTimeout - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    tested.AdvanceTo(ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "el ConnectServer no contestó en 30 s: se esperaba su saludo"));
}

// Each wait counts from its start: with the hello at 20 s, the list is waited for until 50 s.
TEST(ServerSessionTest, EachWaitCountsFromItsStart)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    tested.AdvanceTo(seconds{20});
    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerList);

    tested.AdvanceTo(milliseconds{49999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    tested.AdvanceTo(seconds{50});
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Error, "el ConnectServer no contestó en 30 s: se esperaba la lista de servidores"));
}

// The GS that does not answer the login is also NoReply, with the GS in the log.
TEST(ServerSessionTest, AGameServerThatDoesNotAnswerTheLoginFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.AdvanceTo(ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Error, "el GameServer no contestó en 30 s: se esperaba el resultado del login"));
}

// A CS that gives the list but not the address: the address wait counts from the choice (at 10 s), so it fails at
// 40 s and not at 30.
TEST(ServerSessionTest, AConnectServerThatDoesNotGiveTheAddressFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    tested.AdvanceTo(seconds{10});
    tested.session.ChooseServer(LabServerCode);
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerAddress);

    tested.AdvanceTo(seconds{40} - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error,
                              "el ConnectServer no contestó en 30 s: se esperaba la dirección del GameServer"));
}

// A GS that accepts and does not greet: the wait counts from the open (at 5 s) and fails at 35 s, with the GS in the
// log.
TEST(ServerSessionTest, AGameServerThatDoesNotGreetFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    tested.AdvanceTo(seconds{5});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);

    tested.AdvanceTo(seconds{35} - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    tested.AdvanceTo(seconds{35});
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "el GameServer no contestó en 30 s: se esperaba su saludo"));
}

// What arrived during a long frame is read before checking the timeout: a result that was in the socket after 60 s
// without frames gets in, and the session does not fail.
TEST(ServerSessionTest, AReplyThatArrivedDuringALongFrameIsRead)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.AdvanceTo(seconds{60});
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
}

// Main logs each state change (CL4.5): from Start to LoggedIn, one info line per state, in order, then the first
// keepalive's. The CS close line is the Connection's. The choice does not say the code, and the login does not say
// the account.
TEST(ServerSessionTest, EachStateChangeIsLogged)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    Lines infos;
    for (std::size_t index = 0; index < tested.sink.lines.size(); ++index)
    {
        if (tested.sink.levels[index] == LogLevel::Info)
        {
            infos.push_back(tested.sink.lines[index]);
        }
    }
    EXPECT_EQ(infos,
              (Lines{"[info] se conecta al ConnectServer", "[info] conectó con el ConnectServer",
                     "[info] el ConnectServer saludó: se pide la lista de servidores",
                     "[info] la lista trae 1 servidores: se espera la elección",
                     "[info] se elige un servidor: se pide su dirección",
                     "[info] el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese",
                     "[info] la conexión se cerró: la cerró el cliente",
                     "[info] conectó con el GameServer: se espera su saludo",
                     "[info] el GameServer saludó: se espera la cuenta y la clave", "[info] se manda el login",
                     "[info] el GameServer aceptó el login", "[info] se encoló el keepalive para el GameServer"}));
}

// The character list and the world entry (CL6.3). The names are synthetic (support/CharacterPackets.h).

// In LoggedIn the F3 00 leaves in clear with its XOR32, as 0x634e0e sends it, and the session waits for the list
// (state 0x32). The GS got two encrypted ones, the login and the first keepalive: the F3 00 was not one of them.
TEST(ServerSessionTest, RequestCharacterListSendsF300)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.session.RequestCharacterList();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    const std::vector<Packet> requests = tested.CharacterPacketsSent(ListSubcode);
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_TRUE(SameBytes(requests[0].Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x00})));
    EXPECT_EQ(tested.GameServerReads().serials.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se pide la lista de personajes"));
}

TEST(ServerSessionTest, RequestingTheCharacterListOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.session.RequestCharacterList();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Warning, "se ignora el pedido de la lista de personajes: la sesión no lo espera"));
}

// The list arrives in clear (04): the session keeps it in the packet's order and waits for the choice (the 0x33),
// with none chosen.
TEST(ServerSessionTest, ADuplicatedListSlotChoosesTheLastConstructedEntryAndKeepsBothEvents)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter({{0, "char0001"}, {0, "char0002"}}));
    tested.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.session.ChosenCharacter()->name, NameBytes("char0002"));
    const auto events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto& list = std::get<CharacterListReceived>(events[0]).list.characters;
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, NameBytes("char0001"));
    EXPECT_EQ(list[1].name, NameBytes("char0002"));
}

TEST(ServerSessionTest, TheListMovesToChoosingCharacter)
{
    Harness tested(IdentityObfuscation::Shared());

    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    ASSERT_TRUE(tested.session.Characters().has_value());
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 2u);
    EXPECT_EQ(characters[0].slot, 0);
    EXPECT_EQ(characters[1].slot, 3);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "la lista trae 2 personajes: se espera la elección"));
}

TEST(ServerSessionTest, CharacterEventsStartEmpty)
{
    Harness tested(IdentityObfuscation::Shared());

    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// The batch owns every field, including the order and the repeated slots the reader accepts.
TEST(ServerSessionTest, TakingTheListEventPreservesItsPayloadAndConsumesItOnce)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    const std::vector<CharacterEntryFields> entries{
        {3, "char0002", 513, 0x81, 0x21, {0x12, 0x34, 0x56}, 0x72}, {0, "char0001"}, {3, "char0003"}};
    tested.socket.reads = {CharacterListBytes(entries, 3, 5, 7)};

    static_cast<void>(tested.session.Poll());
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();

    ASSERT_EQ(events.size(), 1u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->list.creatableClasses, 5);
    EXPECT_EQ(received->list.notice, 7);
    ASSERT_EQ(received->list.characters.size(), 3u);
    const CharacterEntry& entry = received->list.characters[0];
    EXPECT_EQ(entry.slot, 3);
    EXPECT_EQ(entry.name, NameBytes("char0002"));
    EXPECT_EQ(entry.level, 513);
    EXPECT_EQ(entry.flags, 0x81);
    EXPECT_EQ(entry.classByte, 0x21);
    const std::array<std::byte, Client::Protocol::FrontEnd::EquipmentSize> equipment{std::byte{0x12}, std::byte{0x34},
                                                                                     std::byte{0x56}};
    EXPECT_EQ(entry.equipment, equipment);
    EXPECT_EQ(entry.guildStatus, 0x72);
    EXPECT_FALSE(entry.withoutEquipment);
    EXPECT_EQ(received->list.characters[1].slot, 0);
    EXPECT_EQ(received->list.characters[2].slot, 3);
    EXPECT_EQ(received->list.characters[2].name, NameBytes("char0003"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    ASSERT_TRUE(tested.session.Characters().has_value());
    // The event keeps three builds; the UI view keeps only the last one of each slot.
    const auto& visible = tested.session.Characters()->characters;
    ASSERT_EQ(visible.size(), 2u);
    EXPECT_EQ(visible[0].slot, 0);
    EXPECT_EQ(visible[0].name, NameBytes("char0001"));
    EXPECT_EQ(visible[1].slot, 3);
    EXPECT_EQ(visible[1].name, NameBytes("char0003"));
}

// With the count at 0 the original still moves to 0x33: the selection, with nobody to choose.
TEST(ServerSessionTest, AnEmptyListAlsoChooses)
{
    Harness tested(IdentityObfuscation::Shared());

    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter({}));

    ASSERT_TRUE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.session.Characters()->characters.empty());
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    ASSERT_NE(received, nullptr);
    EXPECT_TRUE(received->list.characters.empty());
}

TEST(ServerSessionTest, ACharacterListOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la lista de personajes (F3 00): llegó fuera de turno"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ACharacterListThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());

    tested.socket.reads = {CharacterListBytes({{}}, 2)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la lista de personajes (F3 00): no se puede leer"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// Choosing belongs to the click (0x6342c0): it keeps the chosen one and sends nothing.
TEST(ServerSessionTest, ChooseCharacterSendsNothing)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.session.ChooseCharacter(std::uint8_t{3});

    const CharacterEntry* chosen = tested.session.ChosenCharacter();
    ASSERT_NE(chosen, nullptr);
    EXPECT_EQ(chosen->slot, 3);
    EXPECT_EQ(chosen->name, NameBytes("char0002"));
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
}

// A click on nobody (std::nullopt, 0x6342c0's -1) or a slot not in the list leaves none chosen.
TEST(ServerSessionTest, ChoosingNothingForgetsTheChoice)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    tested.session.ChooseCharacter(std::nullopt);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    tested.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    tested.session.ChooseCharacter(std::uint8_t{2});
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
}

TEST(ServerSessionTest, ChoosingACharacterOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());

    tested.session.ChooseCharacter(std::uint8_t{0});

    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la elección de un personaje: la sesión no la espera"));
}

// With one chosen, the F3 03 leaves in clear with its name as it came in the list, and the session moves to 0x3c.
// The log says the slot and not the name.
TEST(ServerSessionTest, EnterWorldSendsF303WithTheChosenName)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});

    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    const std::vector<Packet> selects = tested.CharacterPacketsSent(SelectSubcode);
    ASSERT_EQ(selects.size(), 1u);
    EXPECT_TRUE(SameBytes(selects[0].Bytes(), Xor32Decode(WriteCharacterSelect(NameBytes("char0002")).packet).Bytes()));
    EXPECT_EQ(tested.GameServerReads().serials.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se entra al mundo con el personaje del lugar 3"));
}

TEST(ServerSessionTest, EnterWorldWithoutAChoiceWarns)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(tested.CharacterPacketsSent(SelectSubcode).empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no se entra al mundo: no hay un personaje elegido"));
}

TEST(ServerSessionTest, EnteringTheWorldOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.CharacterPacketsSent(SelectSubcode).empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la entrada al mundo: la sesión no la espera"));
}

// The keepalive goes on in the three new states with the global timer (at 20, 40 and 60 s): changing state does not
// restart it. If it started with the request, at 10 s, at 22 s there would be only one.
TEST(ServerSessionTest, TheKeepaliveRunsInTheThreeNewStates)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{1}));

    tested.PollEachFrameUntil(seconds{10});
    tested.session.RequestCharacterList();
    tested.PollEachFrameUntil(seconds{22});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.PollEachFrameUntil(seconds{30});
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.EnterWorld();
    tested.PollEachFrameUntil(seconds{42});
    EXPECT_EQ(tested.Keepalives().size(), 3u);
    tested.PollEachFrameUntil(seconds{62});

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    EXPECT_EQ(tested.Keepalives().size(), 4u);
}

std::array<std::byte, DeleteCodeSize> SyntheticDeleteCode()
{
    std::array<std::byte, DeleteCodeSize> code{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        code[k] = static_cast<std::byte>('1' + k);
    }
    return code;
}

// The create reply with 1 (0x4c2290): the name, the slot, the level and the class.
std::vector<std::byte> CreatedReply(std::uint8_t slot, std::uint8_t classByte = 0x20)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x13, 0xF3, 0x01, 0x01});
    const auto name = NameBytes("char0003");
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.push_back(std::byte{slot});
    bytes.push_back(std::byte{0x01});
    bytes.push_back(std::byte{0x00});
    bytes.push_back(std::byte{classByte});
    return bytes;
}

TEST(ServerSessionTest, SynchronousDeliveryCreateAndDeleteFailuresKeepTheirPreviousEffectsWithoutSuccessTransition)
{
    for (const bool deleting : {false, true})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        tested.eventSink.characters.clear();
        tested.eventSink.trace.clear();
        tested.eventSink.onCharacter =
            [deleting](const CharacterEvent&, const ServerSession& session) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(session.State(),
                      deleting ? SessionState::WaitingDeleteResult : SessionState::WaitingCreateResult);
            EXPECT_EQ(session.Characters()->characters.size(), deleting ? 1u : 3u);
            return Client::Core::Unexpected(std::string("falló el personaje"));
        };
        if (deleting)
        {
            tested.session.ChooseCharacter(std::uint8_t{0});
            tested.session.DeleteCharacter(SyntheticDeleteCode());
            tested.socket.reads = {Bytes({0xc1, 0x05, 0xf3, 0x02, 0x01})};
        }
        else
        {
            tested.session.ChooseCharacter(std::uint8_t{0});
            ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
            EXPECT_EQ(tested.session.ChosenCharacter()->name, NameBytes("char0001"));
            tested.session.CreateCharacter("char0003", 0);
            tested.socket.reads = {CreatedReply(1)};
        }
        const auto polled = tested.session.Poll();
        ASSERT_FALSE(polled.HasValue());
        EXPECT_EQ(polled.Error(), "falló el personaje");
        EXPECT_EQ(tested.session.State(), SessionState::Failed);
        EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
        EXPECT_EQ(tested.eventSink.characters.size(), 1u);
        // The error stops the suffix; it does not revert the stores made before the callback.
        EXPECT_EQ(tested.session.CreateResult(),
                  deleting ? std::nullopt : std::optional<std::uint8_t>{std::uint8_t{1}});
        EXPECT_EQ(tested.session.DeleteResult(),
                  deleting ? std::optional<std::uint8_t>{std::uint8_t{1}} : std::nullopt);
        ASSERT_TRUE(tested.session.Characters().has_value());
        const auto& characters = tested.session.Characters()->characters;
        ASSERT_EQ(characters.size(), deleting ? 1u : 3u);
        EXPECT_EQ(characters[0].slot, deleting ? 3 : 0);
        EXPECT_EQ(characters[0].name, NameBytes(deleting ? "char0002" : "char0001"));
        if (deleting)
        {
            EXPECT_EQ(tested.session.ChosenCharacter(), nullptr); // Its entry no longer exists.
        }
        else
        {
            EXPECT_EQ(characters[1].slot, 3);
            EXPECT_EQ(characters[1].name, NameBytes("char0002"));
            const auto& created = characters[2];
            EXPECT_EQ(created.slot, 1);
            EXPECT_EQ(created.name, NameBytes("char0003"));
            EXPECT_EQ(created.level, 1);
            EXPECT_EQ(created.flags, 0);
            EXPECT_EQ(created.classByte, 0x20);
            EXPECT_EQ(created.guildStatus, 0xff);
            EXPECT_TRUE(created.withoutEquipment);
            EXPECT_EQ(created.equipment, (std::array<std::byte, Client::Protocol::FrontEnd::EquipmentSize>{}));
            // Asked again: the creation may have reallocated the vector.
            ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
            EXPECT_EQ(tested.session.ChosenCharacter()->slot, 0);
            EXPECT_EQ(tested.session.ChosenCharacter()->name, NameBytes("char0001"));
        }
    }
}

// The final snapshot no longer has the deleted one; the collector keeps the list, the normalized creation and the
// deletion.
TEST(ServerSessionTest, CharacterEventsDeliverListCreationAndDeletionPayloadsInOrder)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.session.ChooseCharacter(std::uint8_t{0}); // out of turn: does not change the sent slot
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseCharacter(std::uint8_t{0});

    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();

    ASSERT_EQ(events.size(), 3u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    const auto* created = std::get_if<CharacterCreated>(&events[1]);
    const auto* deleted = std::get_if<CharacterDeleted>(&events[2]);
    ASSERT_NE(received, nullptr);
    ASSERT_NE(created, nullptr);
    ASSERT_NE(deleted, nullptr);
    ASSERT_EQ(received->list.characters.size(), 2u);
    EXPECT_EQ(received->list.characters[1].slot, 3);
    EXPECT_EQ(received->list.characters[1].name, NameBytes("char0002"));
    EXPECT_EQ(created->entry.slot, 3);
    EXPECT_EQ(created->entry.name, NameBytes("char0003"));
    EXPECT_EQ(created->entry.level, 1);
    EXPECT_EQ(created->entry.flags, 0);
    EXPECT_EQ(created->entry.classByte, 0x20);
    const std::array<std::byte, Client::Protocol::FrontEnd::EquipmentSize> noEquipment{};
    EXPECT_EQ(created->entry.equipment, noEquipment);
    EXPECT_TRUE(created->entry.withoutEquipment);
    EXPECT_EQ(created->entry.guildStatus, 0xff);
    EXPECT_EQ(deleted->slot, 3);
    EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.session.ChosenCharacter()->slot, 0);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, TwoAcceptedIdenticalCreationsPublishTwoEvents)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 1u);

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        tested.session.CreateCharacter("char0003", 1);
        tested.socket.reads = {CreatedReply(1)};
        static_cast<void>(tested.session.Poll());
        ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    }

    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 2u);
    for (const CharacterEvent& event : events)
    {
        const auto* created = std::get_if<CharacterCreated>(&event);
        ASSERT_NE(created, nullptr);
        EXPECT_EQ(created->entry.slot, 1);
        EXPECT_EQ(created->entry.name, NameBytes("char0003"));
        EXPECT_TRUE(created->entry.withoutEquipment);
    }
    EXPECT_EQ(tested.session.Characters()->characters.size(), 3u);
}

TEST(ServerSessionTest, AnAcceptedListSurvivesACloseInTheSamePoll)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.socket.peerClosed = true;

    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->list.characters.size(), 2u);
    ASSERT_TRUE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
}

// F1/02 with 0 invalidates the rest of the batch; the creation accepted before stays available.
TEST(ServerSessionTest, AcceptedEventsSurviveQuitAndLaterPacketsStayIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 1u);
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {
        Join({CreatedReply(1), FromTheGameServer(LogoutResult(0), tested.gameServerSends), CreatedReply(2)})};

    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* created = std::get_if<CharacterCreated>(&events[0]);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->entry.slot, 1);
}

// In ChoosingCharacter, CreateCharacter sends the F3 01 with the name and the class and waits for its reply (0x34).
// The log says the class and not the name.
TEST(ServerSessionTest, CreateCharacterSendsF301WithTheNameAndTheClass)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.CreateCharacter("char0003", 1);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    const std::vector<Packet> creates = tested.CharacterPacketsSent(CreateSubcode);
    ASSERT_EQ(creates.size(), 1u);
    EXPECT_TRUE(
        SameBytes(creates[0].Bytes(), Xor32Decode(WriteCharacterCreate(NameBytes("char0003"), 1).packet).Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se pide crear un personaje de la clase 1"));
    EXPECT_FALSE(tested.Logged(LogLevel::Info, "char0003"));
}

// A name that does not fit in the packet is not sent, nor is a creation outside ChoosingCharacter.
TEST(ServerSessionTest, CreatingOutOfTurnOrWithALongNameIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.session.CreateCharacter("char0003", 1);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora el pedido de crear un personaje: la sesión no lo espera"));

    Harness choosing(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(choosing.ReachChoosingCharacter());
    choosing.session.CreateCharacter("abcdefghijk", 1);
    EXPECT_EQ(choosing.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(choosing.CharacterPacketsSent(CreateSubcode).empty());
    EXPECT_TRUE(choosing.Logged(LogLevel::Warning, "no se crea el personaje: el nombre no entra en 10 bytes"));
}

// With 1 the new one goes to its slot, without equipment (0x517270, Q-C103), and the session goes back to the choice.
TEST(ServerSessionTest, TheNewOneGoesToItsSlotWithoutEquipment)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);

    tested.socket.reads = {CreatedReply(1)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 3u);
    EXPECT_EQ(characters[2].slot, 1);
    EXPECT_EQ(characters[2].name, NameBytes("char0003"));
    EXPECT_EQ(characters[2].level, 1);
    EXPECT_EQ(characters[2].classByte, 0x20);
    EXPECT_TRUE(characters[2].withoutEquipment);
    EXPECT_FALSE(characters[0].withoutEquipment);
    // Without a guild: 0x511290 sets +0x368 to 0xff (0x511ca6) and the reply does not touch it.
    EXPECT_EQ(characters[2].guildStatus, 0xff);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se creó el personaje del lugar 1"));
}

// One that arrives at an occupied slot overwrites it (0x4c2290 writes chars[slot]); one at a slot beyond 4 is not
// added: the original gives it no position.
TEST(ServerSessionTest, TheNewOneReplacesItsSlotAndOneBeyondFourIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_EQ(tested.session.Characters()->characters[1].name, NameBytes("char0003"));
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 2u);

    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(5)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el personaje creado no se suma: el lugar 5 no está en la selección"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// With 0 or 2, only the result, for the window's notice; with another, the original does nothing and keeps waiting
// (Q-C114).
TEST(ServerSessionTest, ARefusedCreationKeepsItsResult)
{
    for (const int result : {0, 2})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        static_cast<void>(tested.eventSink.TakeCharacterEvents());
        tested.session.CreateCharacter("char0003", 1);
        tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, static_cast<unsigned char>(result)})};
        static_cast<void>(tested.session.Poll());
        EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter) << result;
        EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{static_cast<std::uint8_t>(result)});
        EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
        EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    }
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x05})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el resultado 5 no lo atiende el original"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ACreateReplyOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());

    tested.socket.reads = {CreatedReply(1)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Warning, "se ignora la respuesta de crear un personaje (F3 01): llegó fuera de turno"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, AMalformedCreationPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x01})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ASecondCreateReplyWithoutANewRequestPublishesNoEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Join({CreatedReply(1), CreatedReply(1)})};

    static_cast<void>(tested.session.Poll());

    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<CharacterCreated>(events[0]));
}

// DeleteCharacter sends the F3 02 with the chosen one's name and the code, and waits (0x38). The log says the slot,
// and neither the name nor the code.
TEST(ServerSessionTest, DeleteCharacterSendsF302WithTheChosenNameAndTheCode)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});

    tested.session.DeleteCharacter(SyntheticDeleteCode());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    const std::vector<Packet> deletes = tested.CharacterPacketsSent(DeleteSubcode);
    ASSERT_EQ(deletes.size(), 1u);
    EXPECT_TRUE(
        SameBytes(deletes[0].Bytes(),
                  Xor32Decode(WriteCharacterDelete(NameBytes("char0002"), SyntheticDeleteCode()).packet).Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se pide borrar el personaje del lugar 3"));
    EXPECT_FALSE(tested.Logged(LogLevel::Info, "1234"));
}

TEST(ServerSessionTest, DeletingWithoutAChoiceOrOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(tested.CharacterPacketsSent(DeleteSubcode).empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no se borra un personaje: no hay uno elegido"));

    Harness logged(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(logged.ReachLoggedIn());
    logged.session.DeleteCharacter(SyntheticDeleteCode());
    EXPECT_TRUE(logged.Logged(LogLevel::Warning, "se ignora el pedido de borrar un personaje: la sesión no lo espera"));
}

// With 1 the one sent is removed and none stays chosen: a new one in the same slot is not chosen. The original
// deletes the chosen one when the reply arrives (Q-C104); in MuRet it cannot change while waiting, because
// ChooseCharacter belongs to ChoosingCharacter.
TEST(ServerSessionTest, DeleteRemovesTheSlotItSent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());

    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 1u);
    EXPECT_EQ(characters[0].slot, 0);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se borró el personaje del lugar 3"));
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
}

// Start forgets both results, like the list.
TEST(ServerSessionTest, StartForgetsTheResults)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x00})};
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x00})};
    static_cast<void>(tested.session.Poll());
    ASSERT_TRUE(tested.session.CreateResult().has_value());
    ASSERT_TRUE(tested.session.DeleteResult().has_value());

    tested.session.Start();

    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_EQ(tested.session.DeleteResult(), std::nullopt);
}

// With another result nobody is removed and the chosen one stays: the notice belongs to the window.
TEST(ServerSessionTest, ARefusedDeletionKeepsTheCharacter)
{
    for (const int result : {0, 3, 9})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        static_cast<void>(tested.eventSink.TakeCharacterEvents());
        tested.session.ChooseCharacter(std::uint8_t{3});
        tested.session.DeleteCharacter(SyntheticDeleteCode());
        tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, static_cast<unsigned char>(result)})};
        static_cast<void>(tested.session.Poll());
        EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter) << result;
        EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{static_cast<std::uint8_t>(result)});
        EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
        EXPECT_NE(tested.session.ChosenCharacter(), nullptr);
        EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    }
}

TEST(ServerSessionTest, AMalformedDeletionPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.socket.reads = {Bytes({0xC1, 0x04, 0xF3, 0x02})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    EXPECT_EQ(tested.session.DeleteResult(), std::nullopt);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ADeleteReplyOutOfTurnPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// Both replies are waited for with the Waiting* timeout, and the keepalive goes on in both states.
TEST(ServerSessionTest, CreatingAndDeletingHaveTheReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.AdvanceTo(seconds{5});
    tested.session.CreateCharacter("char0003", 1);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(tested.Logged(LogLevel::Error,
                              "el GameServer no contestó en 30 s: se esperaba el resultado de crear un personaje"));

    Harness deleting(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(deleting.ReachChoosingCharacter());
    deleting.session.ChooseCharacter(std::uint8_t{0});
    deleting.AdvanceTo(seconds{5});
    deleting.session.DeleteCharacter(SyntheticDeleteCode());
    deleting.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(deleting.session.Poll());
    EXPECT_TRUE(deleting.Logged(LogLevel::Error,
                                "el GameServer no contestó en 30 s: se esperaba el resultado de borrar un personaje"));
}

TEST(ServerSessionTest, TheKeepaliveRunsWhileCreatingAndDeleting)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{1}));
    tested.session.RequestCharacterList();
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.PollEachFrameUntil(seconds{2});
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    const std::size_t before = tested.Keepalives().size();

    tested.session.CreateCharacter("char0003", 1);
    tested.PollEachFrameUntil(seconds{22});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.Keepalives().size(), before + 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x00})};
    tested.PollEachFrameUntil(seconds{23});
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.PollEachFrameUntil(seconds{42});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    EXPECT_EQ(tested.Keepalives().size(), before + 2);
}

// The list is waited for with the Waiting* timeout, from the request.
TEST(ServerSessionTest, WaitingCharacterListHasTheReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.AdvanceTo(seconds{5});
    tested.session.RequestCharacterList();

    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Error, "el GameServer no contestó en 30 s: se esperaba la lista de personajes"));
}

// The choice waits for the player: it has no timeout.
TEST(ServerSessionTest, ChoosingCharacterHasNoTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.PollEachFrameUntil(ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
}

// The original leaves the mosaic without a timeout while the F3 03's reply has not arrived (06 § La entrada al mundo,
// step 4): the entry has none either.
TEST(ServerSessionTest, EnteringWorldHasNoTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.EnterWorld();

    tested.PollEachFrameUntil(ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
}

// The world entry (CL7.L1, "La sesión"): the sink that applies the F3 03's reply publishes the 0x3d in its row 9
// (0x4c4854, 06, 0x4c3940's table). The rest of that application already runs in InWorld, and the F3 10 that arrives
// behind it in the same batch sees it too.
TEST(ServerSessionTest, PublishingTheEntryFromTheSinkMovesToInWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    std::optional<SessionState> afterPublishing;
    tested.eventSink.onWorld = [&afterPublishing](const WorldEvent& event,
                                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        if (std::holds_alternative<CharacterSelectResult>(event))
        {
            EXPECT_FALSE(session.JoinedWorld());
            session.PublishWorldEntry();
            afterPublishing = session.State();
        }
        return {};
    };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};

    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(afterPublishing, SessionState::InWorld);
    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_TRUE(tested.session.JoinedWorld());
    EXPECT_FALSE(tested.session.Failure().has_value());
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"list", "entry", "inventory"}));
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::EnteringWorld, SessionState::InWorld}));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se aplicó la entrada al mundo: la sesión está en el mundo"));
}

// In the world no reply is awaited: there is no timeout (ADR 0007), and the keepalive goes on with the global timer,
// which publishing does not restart. At 20 s, with the entry published at 5, one already left: if the phase started
// on publishing, it would leave at 25.
TEST(ServerSessionTest, InWorldHasNoReplyTimeoutAndKeepsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    ASSERT_EQ(tested.Keepalives().size(), 1u); // the login's immediate one
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    tested.AdvanceTo(seconds{5});
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.session.State(), SessionState::InWorld);

    tested.PollEachFrameUntil(seconds{20});
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(seconds{5} + ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_FALSE(tested.session.Failure().has_value());
    EXPECT_EQ(tested.Keepalives().size(), 5u); // and at 40, 60 and 80 s
    EXPECT_FALSE(tested.Logged(LogLevel::Error, "no contestó"));
}

// What follows the 0x3d in 0x4c3940 (from row 10 on) can also fail in the applier: with InWorld already published,
// the session ends in ApplicationFailed, closes and cuts the batch, and Poll returns the error until the next Start.
TEST(ServerSessionTest, AFailureAfterPublishingEndsInApplicationFailed)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        EXPECT_EQ(session.State(), SessionState::InWorld);
        return Client::Core::Unexpected(std::string("falló lo que sigue al 0x3d"));
    };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};

    const auto polled = tested.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "falló lo que sigue al 0x3d");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_FALSE(tested.session.JoinedWorld());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"list", "entry"}));
    const auto again = tested.session.Poll();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), "falló lo que sigue al 0x3d");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
}

// Only 0x4c3940 writes the 0x3d: neither the F3 10 (0x4c55a0) nor anyone outside that reply. Publishing from
// elsewhere is a warning and nothing, so it does not take the session out of QuitOrdered either, which is final.
TEST(ServerSessionTest, PublishingOutsideTheEntryApplicationIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    tested.socket.reads = {FromTheGameServer(InventoryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);

    tested.session.PublishWorldEntry();
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    tested.session.PublishWorldEntry();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_FALSE(tested.session.JoinedWorld());
    constexpr std::string_view Ignored = "se ignora la publicación de la entrada al mundo: solo la hace la respuesta "
                                         "del F3 03";
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, Ignored), 3u);
}

TEST(ServerSessionTest, AStartForgetsTheList)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{0});

    tested.session.Start();

    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
}

TEST(ServerSessionTest, LosingTheGameServerWhileChoosingACharacterFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// The names are the player's: the log says slots (like 0x636290's, without the name).
TEST(ServerSessionTest, TheLogDoesNotHaveTheCharacterNames)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.EnterWorld();

    EXPECT_FALSE(std::ranges::any_of(tested.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}

// 04 § Caminar y pegar y cambios de mapa: the rows of a batch run in wire order. The hero's cell after each reception
// shows it: the walks, the warp and the regen move it, the damage does not.
TEST(ServerSessionTest, WalkDamageTeleportAndRegenArriveInWireOrder)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    world->ReadyForTransitions();
    auto& hero = world->world.Hero();
    hero.alive = true;
    hero.key = tested.session.PlayerIndex();
    std::vector<int> cells;
    tested.eventSink.onRoutedReception = [&hero, &cells](ServerSession&) { cells.push_back(hero.cellX); };
    const auto walk = Bytes({0xc1, 8, 0xd3, 0, 5, 2, 3, 0xf0});
    const auto damage = Bytes({0xc1, 8, 0xd7, 0, 5, 0, 1, 0});
    const auto teleport = Bytes({0xc1, 8, 0x1c, 0, 0, 30, 31, 0});
    const auto regen =
        Bytes({0xc1, 24, 0xf3, 4, 21, 22, 0, 0, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8});
    const auto back = Bytes({0xc1, 8, 0xd3, 0, 5, 4, 5, 0x70});
    tested.socket.reads = {Join({walk, damage, FromTheGameServer(teleport, tested.gameServerSends), regen, back})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(cells, (std::vector<int>{2, 2, 30, 21, 4}));
    EXPECT_TRUE(tested.eventSink.world.empty());
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// 04 § Los otros jugadores: the 12, 45, 25, DF and 14 are read by their rows (without a world, the session drops them
// after reading), and of the five only the 12 leaves its copy in the ring (0x4e372e).
TEST(ServerSessionTest, TheViewportPacketsAreReadAndOnlyThe12IsCopied)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    auto players = Bytes({0xc2, 0, 5 + 0x2c, 0x12, 1, 0x80, 0x45, 130, 125, 0x20});
    players.resize(5 + 0x2c);
    auto transformed = Bytes({0xc2, 0, 5 + 0x1c, 0x45, 1, 0x00, 0x46, 70, 80, 0x01, 0x23});
    transformed.resize(5 + 0x1c);
    const auto leave = Bytes({0xc1, 6, 0x14, 1, 0x80, 0x45});
    const auto shown = Bytes({0xc1, 12, 0x25, 0x00, 0x45, 1, 0x80, 0, 0, 0, 0xd0, 0});
    const auto position = Bytes({0xc1, 7, 0xdf, 0x00, 0x45, 140, 141});
    tested.socket.reads = {Join({players, transformed, shown, position, leave})};
    EXPECT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.world.empty());
    EXPECT_FALSE(std::ranges::any_of(tested.sink.lines, [](const std::string& line)
                                     { return line.find("no se puede leer") != std::string::npos; }));
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 1u);
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[0], players));
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// 04 § Los monstruos y los NPC que entran: the 13, 1F and 68 are read by their rows, without a copy in the ring
// (0x4e37d1, 0x4e37e9 and 0x4e4643); a short one is ignored with a warning.
TEST(ServerSessionTest, TheMonsterPacketsAreReadAndAShortOneIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    auto monsters = Bytes({0xc2, 0, 5 + 0x10, 0x13, 1, 0x80, 0x46, 0x01, 0x15});
    monsters.resize(5 + 0x10);
    auto summons = Bytes({0xc2, 0, 5 + 0x18, 0x1f, 1, 0x00, 0x47, 0x00, 0x98});
    summons.resize(5 + 0x18);
    auto objects = Bytes({0xc2, 0, 5 + 0x1c, 0x68, 1, 2, 0x00, 0x03, 0x00, 0x48});
    objects.resize(5 + 0x1c);
    const auto shortMonsters = Bytes({0xc2, 0, 6, 0x13, 1, 0x80});
    tested.socket.reads = {Join({monsters, summons, objects, shortMonsters})};
    EXPECT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.world.empty());
    const auto unreadable = std::ranges::count_if(tested.sink.lines, [](const std::string& line)
                                                  { return line.find("no se puede leer") != std::string::npos; });
    EXPECT_EQ(unreadable, 1);
    EXPECT_TRUE(std::ranges::any_of(
        tested.sink.lines, [](const std::string& line)
        { return line.find("se ignoran los monstruos del viewport (13): no se puede leer") != std::string::npos; }));
    EXPECT_TRUE(tested.eventSink.packetCopies.empty());
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// 0x4e372e and 0x4e3ace: the dispatcher copies the 12 and the 2A before calling the handler, also when the reader
// cannot read them.
TEST(ServerSessionTest, AShortViewportPacketOrDurabilityStillTakesItsCopy)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto players = Bytes({0xc2, 0, 6, 0x12, 1, 0x80});
    const auto durability = Bytes({0xc1, 4, 0x2a, 2});
    tested.socket.reads = {Join({players, durability})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.world.empty());
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 2u);
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[0], players));
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[1], durability));
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// 0x4e3594..0x4e361c: the dispatcher copies the 0B to the queue of 0x77f27b4 before its case (CL9 hito 11), even short
// and unreadable; its row reads the whole one after the copy.
TEST(ServerSessionTest, AnEventStateTakesItsCopyEvenShort)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    const auto shortState = Bytes({0xc1, 4, 0x0b, 1});
    const auto state = Bytes({0xc1, 5, 0x0b, 1, 3});
    tested.socket.reads = {Join({shortState, state})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 2u);
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[0], shortState));
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[1], state));
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el paquete de evento 0B: no se puede leer"), 1u);
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// P-C33: even a 1C without fields goes through the rejection before the semantic reader.
TEST(ServerSessionTest, UnencryptedTeleportIsAnsweredWithF103BeforeReadingItsFields)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Bytes({0xc1, 3, 0x1c})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    const auto received = tested.GameServerReads();
    ASSERT_FALSE(received.packets.empty());
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xc1, 6, 0xf1, 3, 0, 0x1c})));
    EXPECT_TRUE(received.packets.back().WasDecrypted());
    EXPECT_TRUE(tested.eventSink.world.empty());
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 1u);
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[0], Bytes({0xc1, 3, 0x1c})));
}

// 04 § Mover y equipar ítems: the encrypted 24, the 28, the 2A and the F3 14 and F3 13 are read by their rows; the
// unencrypted 24 is answered with the C1 06 F1 03 00 16 (0x4d166b) without reading it, and the client's 24 leaves
// with XOR32 and as C3. The item is made up.
TEST(ServerSessionTest, TheItemMovesAreReadAndTheClearOneIsReported)
{
    namespace Inventory = Client::Protocol::Items;
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    auto equipment = Bytes({0xc1, 0x18, 0xf3, 0x13, 0x12, 0x34, 0});
    equipment.resize(0x18, std::byte{0x11});
    tested.socket.reads = {Join(
        {FromTheGameServer(Bytes({0xc1, 0x0c, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0}), tested.gameServerSends),
         Bytes({0xc1, 5, 0x28, 0x16, 1}), Bytes({0xc1, 6, 0x2a, 2, 9, 0}),
         Bytes({0xc1, 0x0c, 0xf3, 0x14, 0x17, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0}), equipment})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
    EXPECT_TRUE(tested.eventSink.world.empty());
    // 0x4e38ca and 0x4e3ace: the 24 and the 2A leave their copy in the queue; the 28, the F3 14 and the F3 13 do not.
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 2u);
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[0],
                          Bytes({0xc1, 0x0c, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0})));
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[1], Bytes({0xc1, 6, 0x2a, 2, 9, 0})));

    const auto before = tested.GameServerReads().packets.size();
    tested.socket.reads = {Bytes({0xc1, 0x0c, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    const auto reported = tested.GameServerReads();
    ASSERT_EQ(reported.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(reported.packets.back()).Bytes(), Bytes({0xc1, 6, 0xf1, 3, 0, 0x16})));
    EXPECT_TRUE(reported.packets.back().WasDecrypted());
    EXPECT_TRUE(tested.eventSink.world.empty());
    // The unencrypted 24 is copied too, before the flag.
    EXPECT_EQ(tested.eventSink.packetCopies.size(), 3u);

    ASSERT_TRUE(
        tested.session
            .SendItemMove(Inventory::ItemMove{.sourceFlag = 0,
                                              .sourceSlot = 0x15,
                                              .item = {std::byte{0x21}, std::byte{0x0b}, std::byte{0x2c}, std::byte{0},
                                                       std::byte{0}, std::byte{0x70}, std::byte{0}},
                                              .targetFlag = 0,
                                              .targetSlot = 0x16})
            .HasValue());
    const auto sent = tested.GameServerReads();
    ASSERT_EQ(sent.packets.size(), before + 2);
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.packets.back()).Bytes(),
                          Bytes({0xc1, 0x0e, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0, 0, 0x16})));
    EXPECT_TRUE(sent.packets.back().WasDecrypted());
}

// 0x4d4af0: in clear, the 36 is not read and is answered with C1 06 F1 03 00 36 in C3, which draws two rand() of the
// world's CRT.
TEST(ServerSessionTest, TheTradeRequestInClearIsAnsweredWithTwoRandomsOfTheWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    Client::Crt::CrtRandom expected = world->cloth.random;
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    const auto before = tested.GameServerReads().packets.size();

    tested.socket.reads = {Bytes({0xc1, 0x0d, 0x36, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0})};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    const auto reported = tested.GameServerReads();
    ASSERT_EQ(reported.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(reported.packets.back()).Bytes(), Bytes({0xc1, 6, 0xf1, 3, 0, 0x36})));
    EXPECT_TRUE(reported.packets.back().WasDecrypted());
    EXPECT_EQ(world->cloth.random.State(), expected.State());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning,
                                 "el pedido de comercio (36) no llegó cifrado: se le avisa al GameServer (F1 03)"),
              1u);
    EXPECT_EQ(tested.eventSink.routedAfter, (std::vector<std::size_t>{0}));
    EXPECT_EQ(world->world.Runtime().social.trade.otherName[0], std::byte{0});
}

// 04 § La vida y el maná: the 26 and the 27 reach the world also before the F3 03 (0x4d3510 and 0x4d3820 do not look
// at the scene); the short 26 with 0xff is ignored. The values are made up.
TEST(ServerSessionTest, TheLifeAndTheManaReachTheWorldBeforeTheMap)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    tested.socket.reads = {Join({Bytes({0xc1, 7, 0x26, 0xfe, 0x01, 0x2c, 0}), Bytes({0xc1, 5, 0x26, 0xff, 0x01}),
                                 Bytes({0xc1, 8, 0x27, 0xff, 0x00, 0x5a, 0x00, 0x1e})})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter, (std::vector<std::size_t>{0, 0}));
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora la vida (26): no se puede leer"), 1u);
    EXPECT_EQ(world->world.OwnCharacter().maxLife, 0x12c);
    EXPECT_EQ(world->world.OwnCharacter().mana, 0x5a);
    EXPECT_EQ(world->world.OwnCharacter().bp, 0x1e);
}

// 04 § Pegar, el daño y la muerte (CL9 milestone 3): the encrypted 16 and 9C (as the GS builds them:
// GCKillPlayerExpSend and the Blood Castle reward) and the 5-byte 17 are read by their rows; the GS's 15
// (GCDamageSend) has no case; the unencrypted 16 and 9C are answered with the F1 03 and their opcode, without
// reading them. The keys are made up.
TEST(ServerSessionTest, TheKillsAndTheDeathAreReadAndTheClearOnesAreReported)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto kill = Bytes({0xc1, 9, 0x16, 0x81, 0x2c, 0x03, 0xe8, 0x00, 0x2a});
    const auto reward = Bytes({0xc1, 0x0c, 0x9c, 0xff, 0xff, 0xcc, 0x01, 0x00, 0x20, 0x4e, 0x00, 0x00});
    tested.socket.reads = {Join({FromTheGameServer(kill, tested.gameServerSends), Bytes({0xc1, 7, 0x15, 0, 1, 0, 9}),
                                 FromTheGameServer(reward, tested.gameServerSends), Bytes({0xc1, 5, 0x17, 0, 7})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no llegó cifrada"), 0u);

    for (const Client::Network::Opcode opcode :
         {Client::Protocol::KillExperienceOpcode, Client::Protocol::ExtendedKillExperienceOpcode})
    {
        const auto before = tested.GameServerReads().packets.size();
        tested.socket.reads = {Bytes({0xc1, 9, Client::Network::ByteOf(opcode), 0, 1, 0, 2, 0, 3})};
        ASSERT_TRUE(tested.session.Poll().HasValue());
        const auto reported = tested.GameServerReads();
        ASSERT_EQ(reported.packets.size(), before + 1);
        EXPECT_TRUE(SameBytes(Xor32Decode(reported.packets.back()).Bytes(),
                              Bytes({0xc1, 6, 0xf1, 3, 0, Client::Network::ByteOf(opcode)})));
        EXPECT_TRUE(reported.packets.back().WasDecrypted());
        EXPECT_TRUE(tested.eventSink.world.empty());
    }
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// 07 Q-C1240: with GameServer 0.97 the 22 (an 8-byte C3), the 23 without pkt[4] and the one-byte 2C do not carry what
// the client reads, but 0x4d0cc0, 0x4d10d0 and 0x4de500 release their wait on every path: their rows warn that they
// cannot be read and release only that. The 20 with 9-byte entries writes nothing fixed. The values are made up.
TEST(ServerSessionTest, TheShortFloorItemRepliesStillReleaseTheirWaits)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    Client::Kernel::WorldRuntime& runtime = world->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.floor.pendingDrop = 22;
    runtime.ui.field5767954 = 10;
    runtime.cursorItemCount = 1;
    runtime.cursorItem.occupied = 1;
    tested.socket.reads = {
        Join({FromTheGameServer(Bytes({0xc1, 0x08, 0x22, 0x0c, 0x00, 0x18, 0x2c, 0}), tested.gameServerSends),
              Bytes({0xc1, 0x04, 0x23, 0x01}), Bytes({0xc1, 0x03, 0x23}), Bytes({0xc1, 0x04, 0x2c, 0x23}),
              Bytes({0xc2, 0x00, 0x0e, 0x20, 1, 0x00, 0x05, 130, 120, 0x00, 0x18, 0x2c, 0, 0})})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 4u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora lo levantado del piso (22): no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora lo tirado al piso (23): no se puede leer"), 2u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora la fruta de stats (2C): no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignoran los ítems del piso (20): no se puede leer"), 1u);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
    EXPECT_EQ(runtime.floor.pendingDrop, -1);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(runtime.cursorItem.occupied, 0u);
    EXPECT_EQ(runtime.ui.field5767954, 0);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// GameServer 0.97's GCMoneySend (C3 08 22 FE, the zen when buying in the shop) reaches the world with its zen,
// without the cannot-read warning, and not like the short 22 that only releases the request (07 Q-C1240). The 918
// is made up.
TEST(ServerSessionTest, TheShortZenOfThe22ReachesTheWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    const auto texts = Client::Tests::MakeTextTable({{918, "levantado"}});
    world->resources.texts = &texts;
    tested.eventSink.routedWorld = &world->resources;
    world->world.Runtime().floor.pendingPickUp = 5;
    tested.socket.reads = {
        FromTheGameServer(Bytes({0xc1, 0x08, 0x22, 0xfe, 0x00, 0x01, 0x86, 0xa0}), tested.gameServerSends)};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Debug, "el 22 con el zen llegó corto"), 1u);
    EXPECT_EQ(world->world.OwnCharacter().zen, 100000u);
    EXPECT_EQ(world->world.Runtime().floor.pendingPickUp, -1);
}

// The F3 06's reply (0x4d3200, 0x4e30cd in the F3 sub-dispatcher) is read by its row; a short one, a warning.
TEST(ServerSessionTest, TheLevelUpPointReplyIsReadByItsRow)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join(
        {FromTheGameServer(Bytes({0xc1, 0x0a, 0xf3, 0x06, 0x12, 0x00, 0x34, 0x12, 0x78, 0x56}), tested.gameServerSends),
         Bytes({0xc1, 0x05, 0xf3, 0x06, 0x12})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el punto de subir de nivel (F3 06): no se puede leer"),
              1u);
    EXPECT_TRUE(tested.eventSink.world.empty());
}

// The F3 08 (0x4d3b50, 0x4e30cd in the F3 sub-dispatcher), the PK state the server sends on entering the world, is
// read by its row; a short one, a warning.
TEST(ServerSessionTest, ThePkLevelIsReadByItsRow)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {
        Join({FromTheGameServer(Bytes({0xc1, 0x07, 0xf3, 0x08, 0x12, 0x34, 0x03}), tested.gameServerSends),
              Bytes({0xc1, 0x06, 0xf3, 0x08, 0x12, 0x34})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el estado de PK (F3 08): no se puede leer"), 1u);
    EXPECT_TRUE(tested.eventSink.world.empty());
}

// The connection encrypts only the gate: the other three world C1 leave with XOR32 and without C3.
TEST(ServerSessionTest, GateRequestAndTeleportDoneUseTheirDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    constexpr std::array<Client::Protocol::Movement::Cell, 4> Path{
        {{0xb6, 0x73}, {0xb5, 0x73}, {0xb4, 0x74}, {0xb3, 0x74}}};
    ASSERT_TRUE(tested.session.SendWalk(Path, {0, 0}, 0.0f).HasValue());
    tested.session.RequestGate(23);
    tested.session.ConfirmTeleport();
    tested.session.RequestCrywolfState();
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 4);
    EXPECT_TRUE(
        SameBytes(Xor32Decode(received.packets[before]).Bytes(), Bytes({0xc1, 8, 0xd3, 0xb6, 0x73, 0x73, 0x76, 0x70})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 1]).Bytes(), Bytes({0xc1, 6, 0x1c, 23, 0, 0})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 2]).Bytes(), Bytes({0xc1, 4, 0xf3, 0x12})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 3]).Bytes(), Bytes({0xc1, 4, 0xbd, 0})));
    EXPECT_FALSE(received.packets[before].WasDecrypted());
    EXPECT_TRUE(received.packets[before + 1].WasDecrypted());
    EXPECT_FALSE(received.packets[before + 2].WasDecrypted());
    EXPECT_FALSE(received.packets[before + 3].WasDecrypted());
}

TEST(ServerSessionTest, InvalidWalkDoesNotSendOrDisturbTheSession)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.socket.sent.size();
    EXPECT_FALSE(tested.session.SendWalk({}, {1, 2}, 0.0f).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

TEST(ServerSessionTest, WorldRequestsNeverGoToTheConnectServer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto before = tested.socket.sent.size();
    tested.session.RequestGate(23);
    tested.session.ConfirmTeleport();
    tested.session.RequestCrywolfState();
    constexpr std::array<Client::Protocol::Movement::Cell, 1> Path{{{1, 2}}};
    EXPECT_FALSE(tested.session.SendWalk(Path, {1, 2}, 0.0f).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
}

// 0x4e3e98..0x4e3f1d: the dispatcher copies each 1C to 0x77f27b4's queue before the flag and the reader, the
// rejected one too; the copy is the sink's, not a loan from the reader.
TEST(ServerSessionTest, TheTeleportCopyPrecedesAdmissionAndRemainsAnOwnedReception)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    world->ReadyForTransitions();
    world->world.Hero().alive = true;
    auto clear = Bytes({0xc1, 3, 0x1c});
    const auto accepted = Bytes({0xc1, 8, 0x1c, 0, 0, 30, 31, 0});
    tested.socket.reads = {Join({clear, FromTheGameServer(accepted, tested.gameServerSends)})};
    tested.eventSink.onRoutedReception = [&tested, &accepted](ServerSession&)
    {
        EXPECT_EQ(tested.eventSink.packetCopies.size(), 2u);
        EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies.back(), accepted));
    };
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 2u);
    clear[2] = std::byte{0xff};
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies.front(), Bytes({0xc1, 3, 0x1c})));
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 1u);
}

// CheckApplication also cuts the batch for the routed rows: the warp with an item on the cursor fails in 0x5be150,
// and the return that follows does not arrive.
TEST(ServerSessionTest, AWorldApplicationFailureStopsTheMovementBatchAtTheSecondEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    auto& hero = world->world.Hero();
    hero.alive = true;
    hero.key = tested.session.PlayerIndex();
    world->world.Runtime().cursorItemCount = 1;
    tested.socket.reads = {Join({Bytes({0xc1, 8, 0xd3, 0, 5, 2, 3, 0x70}),
                                 FromTheGameServer(Bytes({0xc1, 8, 0x1c, 0, 0, 30, 31, 0}), tested.gameServerSends),
                                 Bytes({0xc1, 8, 0xd3, 0, 5, 4, 5, 0x70})})};
    const auto result = tested.session.Poll();
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x5be150"), std::string::npos) << result.Error();
    EXPECT_EQ(tested.eventSink.routedAfter, (std::vector<std::size_t>{0, 0}));
    EXPECT_EQ(hero.cellX, 2);
    EXPECT_EQ(tested.eventSink.packetCopies.size(), 1u);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_FALSE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 2u);
}

TEST(ServerSessionTest, MovementAndWarpPreserveTheirOrderAcrossEveryReadSplit)
{
    for (std::size_t split = 1;; ++split)
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        // The walks are the player's (the F1 00 gave key 5) and the warp is in its map: they write its cell.
        const auto world = std::make_unique<BareWorld>();
        tested.eventSink.routedWorld = &world->resources;
        world->ReadyForTransitions();
        auto& hero = world->world.Hero();
        hero.alive = true;
        hero.key = tested.session.PlayerIndex();
        std::vector<int> cells;
        tested.eventSink.onRoutedReception = [&hero, &cells](ServerSession&) { cells.push_back(hero.cellX); };
        const auto sequence =
            Join({Bytes({0xc1, 8, 0xd3, 0, 5, 2, 3, 0xf0}),
                  FromTheGameServer(Bytes({0xc1, 8, 0x1c, 0, 0, 30, 31, 0}), tested.gameServerSends),
                  Bytes({0xc1, 8, 0xd7, 0, 1, 0, 2, 0x40}), Bytes({0xc1, 8, 0xd3, 0, 5, 4, 5, 0x70})});
        if (split >= sequence.size())
        {
            break;
        }
        tested.socket.reads = {ByteVector(sequence.begin(), sequence.begin() + static_cast<std::ptrdiff_t>(split))};
        ASSERT_TRUE(tested.session.Poll().HasValue());
        tested.socket.reads = {ByteVector(sequence.begin() + static_cast<std::ptrdiff_t>(split), sequence.end())};
        const auto polled = tested.session.Poll();
        ASSERT_TRUE(polled.HasValue()) << polled.Error();
        EXPECT_EQ(cells, (std::vector<int>{2, 30, 30, 4})) << split;
        EXPECT_TRUE(tested.eventSink.world.empty()) << split;
        EXPECT_EQ(hero.cellX, 4) << split;
        EXPECT_EQ(hero.cellY, 5) << split;
        EXPECT_EQ(hero.direction, 7) << split;
    }
}

TEST(ServerSessionTest, TruncatedMovementAndWarpDoNotReachTheSink)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    for (const int head : {0xd3, 0xd7, 0x1c})
    {
        for (int size = 3; size < 8; ++size)
        {
            auto packet = Bytes({0xc1, size, head});
            packet.resize(static_cast<std::size_t>(size), std::byte{0});
            tested.socket.reads = {FromTheGameServer(packet, tested.gameServerSends)};
            ASSERT_TRUE(tested.session.Poll().HasValue());
        }
    }
    for (int size = 4; size < 24; ++size)
    {
        auto packet = Bytes({0xc1, size, 0xf3, 4});
        packet.resize(static_cast<std::size_t>(size), std::byte{0});
        tested.socket.reads = {packet};
        ASSERT_TRUE(tested.session.Poll().HasValue());
    }
    EXPECT_TRUE(tested.eventSink.routedAfter.empty());
    EXPECT_EQ(tested.eventSink.packetCopies.size(), 5u);
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

TEST(ServerSessionTest, AClosedGameServerDoesNotQueueWorldRequests)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.connection.Close();
    const auto before = tested.socket.sent.size();
    tested.session.RequestGate(23);
    tested.session.ConfirmTeleport();
    tested.session.RequestCrywolfState();
    constexpr std::array<Client::Protocol::Movement::Cell, 1> Path{{{1, 2}}};
    EXPECT_FALSE(tested.session.SendWalk(Path, {1, 2}, 0.0f).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
}

TEST(ServerSessionTest, FacingUsesTheWorldConnectionAndXorDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    ASSERT_TRUE(tested.session.SendFacing(0xf7).HasValue());
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xc1, 5, 0x18, 0xf7, 0x7a})));
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// 0x4e4b3a..0x4e4bf2: the 9F and the A0..A3 arrive in order, without a scene guard; the short A1 leaves its warning.
// The values are made up.
TEST(ServerSessionTest, TheQuestPacketsAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    // The A1 and the A2 open the quest's dialog (0x419710): Dialog.bmd and, with no answers, the text 609.
    world->world.Runtime().dialogTable.assign(Client::Tables::DialogCount, {});
    const auto texts = Client::Tests::MakeTextTable({{609, "Cerrar"}});
    world->resources.texts = &texts;
    tested.socket.reads = {Join({Bytes({0xc1, 5, 0x9f, 1, 0x22}), Bytes({0xc1, 4, 0xa0, 3}),
                                 Bytes({0xc1, 5, 0xa1, 2, 0x10}), Bytes({0xc1, 6, 0xa2, 2, 0, 0x20}),
                                 Bytes({0xc1, 7, 0xa3, 0x80, 0x21, 0xc8, 5}), Bytes({0xc1, 4, 0xa1, 2})})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 5u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el estado de una misión (A1): no se puede leer"), 1u);
    EXPECT_EQ(world->world.Runtime().quest.flags[1], 0x22);
}

// 0x4e4547: the BC 00 and the BC 01 are event packets read by their rows; a short one leaves a warning and nothing
// more (CL9 hitos 6 and 11). Made-up values.
TEST(ServerSessionTest, TheJewelMixAnswerIsReadByItsRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    // The unmix with 7 (0x4da5f0) and the mix with 4 (0x4e4605) show a text.
    const auto texts = Client::Tests::MakeTextTable({{1815, "Un texto inventado"}, {1817, "Un texto inventado"}});
    world->resources.texts = &texts;
    tested.socket.reads = {
        Join({Bytes({0xc1, 5, 0xbc, 1, 7}), Bytes({0xc1, 4, 0xbc, 0}), Bytes({0xc1, 5, 0xbc, 0, 4})})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 2u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el paquete de evento BC: no se puede leer"), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// 0x4da373: the 86 with 100 (the trainer's mix) is read by its row with the item's seven bytes from pkt[4], like the
// 1 (CL9 milestone 6); the short 86 with 100, a warning. Made-up values.
TEST(ServerSessionTest, TheTrainerMixIsReadByItsRow)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({Bytes({0xc1, 0x0b, 0x86, 100, 0x0d, 0x08, 0x40, 0, 0, 0, 0x10}),
                                 Bytes({0xc1, 0x04, 0x86, 0}), Bytes({0xc1, 0x04, 0x86, 100})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el 86 de las ventanas de los NPC: no se puede leer"),
              1u);
    EXPECT_TRUE(tested.eventSink.world.empty());
}

// The A0, the A2 and the 30 leave with the XOR32 and encrypted as C3; 0x419900's C1 03 31, in clear.
TEST(ServerSessionTest, TheQuestRequestsGoToTheGameServer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    tested.session.RequestQuestList();
    tested.session.SendSetQuest(1, 1);
    tested.session.SendTalk(0x0145);
    tested.session.SendWindowClose();
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 4);
    const auto& packets = received.packets;
    EXPECT_TRUE(SameBytes(Xor32Decode(packets[before]).Bytes(), Bytes({0xc1, 3, 0xa0})));
    EXPECT_TRUE(packets[before].WasDecrypted());
    EXPECT_TRUE(SameBytes(Xor32Decode(packets[before + 1]).Bytes(), Bytes({0xc1, 5, 0xa2, 1, 1})));
    EXPECT_TRUE(packets[before + 1].WasDecrypted());
    EXPECT_TRUE(SameBytes(Xor32Decode(packets[before + 2]).Bytes(), Bytes({0xc1, 5, 0x30, 0x01, 0x45})));
    EXPECT_TRUE(packets[before + 2].WasDecrypted());
    EXPECT_TRUE(SameBytes(packets[before + 3].Bytes(), Bytes({0xc1, 3, 0x31})));
    EXPECT_FALSE(packets[before + 3].WasDecrypted());
}

// 0x4f2416 and 0x576a60: the C1 05 18 of sitting and of the emote, with the XOR32 through the GS; without the GS open,
// nothing.
TEST(ServerSessionTest, TheActionUsesTheWorldConnectionAndXorDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto beforeChoosing = tested.socket.sent.size();
    tested.session.SendAction(3, 0x82);
    EXPECT_EQ(tested.socket.sent.size(), beforeChoosing);

    Harness inWorld(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(inWorld.ReachLoggedIn());
    const auto before = inWorld.GameServerReads().packets.size();
    inWorld.session.SendAction(0xf7, 0x82);
    const auto received = inWorld.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xc1, 5, 0x18, 0xf7, 0x82})));
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// 0x4e39ab: the 18 goes to 0x4cb750 without a guard; its row reads it. A short one, a warning and nothing.
TEST(ServerSessionTest, TheActionOfAnotherIsReadByItsRow)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({Bytes({0xc1, 6, 0x18, 0x12, 0x34, 3}), Bytes({0xc1, 7, 0x18, 0x92, 0x34, 3, 0x83})})};
    EXPECT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora la acción (18): no se puede leer"), 1u);
    EXPECT_TRUE(tested.eventSink.world.empty());
}

// 0x61c210 sends the F3 30 in clear with the XOR32, through the GS: it does not go through the C3 encryption.
TEST(ServerSessionTest, OptionDataUsesTheWorldConnectionAndXorDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    Client::Protocol::Player::OptionBody body{};
    body[0] = 0x21;
    body[10] = 0x0a;
    body[0x17] = 0x5c;
    ASSERT_TRUE(tested.session.SendOptionData(body).HasValue());
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    const auto decoded = Xor32Decode(received.packets.back());
    const auto bytes = decoded.Bytes();
    ASSERT_EQ(bytes.size(), 0x1cu);
    EXPECT_TRUE(SameBytes(bytes.first(4), Bytes({0xc1, 0x1c, 0xf3, 0x30})));
    EXPECT_EQ(bytes[4], std::byte{0x21});
    EXPECT_EQ(bytes[14], std::byte{0x0a});
    EXPECT_EQ(bytes[0x1b], std::byte{0x5c});
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// 0x6599a0(4) sends the C1 03 42 in clear, without XOR32 or encryption, and only with the socket open.
TEST(ServerSessionTest, PartyListRequestGoesClearToTheGameServerOnly)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto beforeChoosing = tested.socket.sent.size();
    tested.session.RequestPartyList();
    EXPECT_EQ(tested.socket.sent.size(), beforeChoosing);

    Harness inWorld(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(inWorld.ReachLoggedIn());
    const auto before = inWorld.GameServerReads().packets.size();
    inWorld.session.RequestPartyList();
    const auto received = inWorld.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(received.packets.back().Bytes(), Bytes({0xc1, 3, 0x42})));
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// CL9 hito 12: the castle's packets outside the battle are read by their rows. The B2 19 that the lab sends after each
// /move is read too (hito 13); the B8 with another subcode still has no handler. A B2 05 that lacks what it reads is
// ignored with a warning, without closing the session. Made-up values.
TEST(ServerSessionTest, TheCastleAndBattlePacketsAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    // The B2 01 with 3 shows the text 1506 (0x4df080).
    const auto texts = Client::Tests::MakeTextTable({{1506, "Un texto inventado"}});
    world->resources.texts = &texts;
    tested.socket.reads = {
        Join({Bytes({0xc1, 5, 0xb2, 0x01, 3}), Bytes({0xc1, 5, 0xb2, 0x19, 0}), Bytes({0xc1, 5, 0xb8, 1, 4}),
              Bytes({0xc1, 5, 0xb8, 2, 4}), Bytes({0xc1, 0x0c, 0xb3, 1, 1, 0, 0, 0, 0, 0, 0, 0}),
              Bytes({0xc1, 5, 0xb2, 0x05, 1}), Bytes({0xc1, 6, 0xb2, 0x1a, 1, 7}), Bytes({0xc1, 5, 0xb5, 0, 2})})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 6u);
    EXPECT_EQ(world->world.Hero().byte372, 4);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "el opcode B2 y el subcódigo 19 no tiene handler"));
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el opcode B8 y el subcódigo 02 no tiene handler"));
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el paquete del castillo: no se puede leer"), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// CL9 hito 13: the battle's packets are read by their rows. The B9 04 falls to the original's default (0x4e58c8) and
// still has no handler; a BA that lacks what it reads is ignored with a warning, without closing the session. Made-up
// values.
TEST(ServerSessionTest, TheCastleBattlePacketsAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    std::vector<std::byte> holder = Bytes({0xc1, 0x1b, 0xb2, 0x20, 0x01, 0x3d});
    holder.resize(0x1b);
    tested.socket.reads = {Join({Bytes({0xc1, 7, 0xb9, 0x01, 0x00, 0x05, 3}), Bytes({0xc1, 5, 0xb9, 0x04, 1}),
                                 Bytes({0xc1, 5, 0xb9, 0x05, 1}), Bytes({0xc1, 7, 0xba, 0x00, 0x05, 0x28, 2}),
                                 Bytes({0xc1, 5, 0xba, 0x00, 0x05}), Bytes({0xc1, 5, 0xbb, 0x00, 0x00}), holder})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 5u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el opcode B9 y el subcódigo 04 no tiene handler"));
    EXPECT_EQ(
        tested.CountLogged(LogLevel::Warning, "se ignora el paquete de la batalla del castillo: no se puede leer"), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// CL9 milestone 13: the B1 00 (0x4e5056 → 0x412b30) with a made-up host, port and codes.
constexpr std::string_view MapServerHost = "10.0.0.3";
constexpr std::uint16_t MapServerPort = 55919;

ByteVector MapServerMovePacket(std::string_view host)
{
    ByteVector bytes = Bytes({0xc1, 0x28, 0xb1, 0x00});
    ByteVector field(16);
    for (std::size_t i = 0; i < host.size() && i < field.size(); ++i)
        field[i] = static_cast<std::byte>(host[i]);
    bytes.insert(bytes.end(), field.begin(), field.end());
    const ByteVector rest = Bytes({MapServerPort & 0xff,
                                   MapServerPort >> 8,
                                   0,
                                   0,
                                   0x31,
                                   0x32,
                                   0x33,
                                   0x34,
                                   0x35,
                                   0x36,
                                   0x37,
                                   0x38,
                                   0x39,
                                   0x3a,
                                   0x3b,
                                   0x3c,
                                   0x3d,
                                   0x3e,
                                   0x3f,
                                   0x40});
    bytes.insert(bytes.end(), rest.begin(), rest.end());
    return bytes;
}

// With the login sent: 0x412b30's world (MapServerLeft), the GameServer closed and the new one opened with the
// packet's host and port. Its hello, without looking at the result or the version, keeps the key and, with the move
// pending, applies 0x412bb0's world (MapServerJoined) and sends 0x4c2480's two C1 03 31 and the encrypted B1 01.
TEST(ServerSessionTest, TheMapServerMoveReconnectsAndPresentsTheCharacter)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.eventSink.onWorld = ApplyAnEmptyWorldReset;
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    static_cast<void>(tested.eventSink.TakeWorldEvents());
    const std::size_t connectsBefore = tested.socket.connects.size();
    tested.socket.reads = {MapServerMovePacket(MapServerHost)};
    static_cast<void>(tested.session.Poll());

    auto events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Client::App::MapServerLeft>(events[0]));
    ASSERT_EQ(tested.socket.connects.size(), connectsBefore + 1);
    EXPECT_EQ(tested.socket.connects.back().host, MapServerHost);
    EXPECT_EQ(tested.socket.connects.back().port, MapServerPort);
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(tested.LinesWithAnAddress().empty());

    tested.gameServerStart = tested.socket.sent.size();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    PacketSerials newServerSends;
    tested.socket.reads = {
        FromTheGameServer(Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x00, 0x01, 0x23, '0', '9', '9', '0', '8'}), newServerSends)};
    static_cast<void>(tested.session.Poll());

    events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Client::App::MapServerJoined>(events[0]));
    EXPECT_TRUE(std::holds_alternative<Client::App::MapServerAuthenticationPrepared>(events[1]));
    EXPECT_TRUE(std::holds_alternative<Client::App::GuildTableReset>(events[2]));
    EXPECT_EQ(tested.session.PlayerIndex(), 0x0123);
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 3u);
    EXPECT_TRUE(SameBytes(received.packets[0].Bytes(), Bytes({0xc1, 3, 0x31})));
    EXPECT_TRUE(SameBytes(received.packets[1].Bytes(), Bytes({0xc1, 3, 0x31})));
    EXPECT_TRUE(received.packets[2].WasDecrypted());
    const ByteVector moveBytes = MapServerMovePacket(MapServerHost);
    const Packet auth = Xor32Decode(received.packets[2]);
    ASSERT_EQ(auth.Bytes().size(), 0x45u);
    EXPECT_TRUE(SameBytes(auth.Bytes().first(4), Bytes({0xc1, 0x45, 0xb1, 0x01})));
    // The chosen one's name (slot 0) and the B1 00's codes.
    EXPECT_TRUE(SameBytes(auth.Bytes().subspan(16, 12), Bytes({'c', 'h', 'a', 'r', '0', '0', '0', '1', 0, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(auth.Bytes().subspan(28, 16), std::span<const std::byte>(moveBytes).subspan(24, 16)));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// Before the login, 0x412b30 only keeps the move ([0x5767928] at 0): it neither closes nor connects, and the hello
// follows the login's path.
TEST(ServerSessionTest, AMapServerMoveBeforeTheLoginIsOnlyKept)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    const std::size_t connectsBefore = tested.socket.connects.size();
    tested.socket.reads = {MapServerMovePacket(MapServerHost),
                           FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
}

// A host without a 0 in its 0x24 bytes (Q-C1402): the GameServer is already closed, as in the original, and no other
// is connected.
TEST(ServerSessionTest, AMapServerMoveWithAnOpenHostDoesNotConnect)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t connectsBefore = tested.socket.connects.size();
    ByteVector open = MapServerMovePacket("abcdefghijklmnop");
    for (std::size_t at = 20; at < open.size(); ++at)
        open[at] = std::byte{0x41};
    tested.socket.reads = {open};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "Q-C1402"));
}

// What the castle windows send goes through the GameServer with XOR32 and without C3, and nothing without it open.
TEST(ServerSessionTest, CastlePacketsGoToTheGameServerOnly)
{
    namespace Castle = Client::Protocol::CastleSiege;
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto beforeChoosing = tested.socket.sent.size();
    tested.session.SendCastlePacket(Castle::WriteRegister());
    EXPECT_EQ(tested.socket.sent.size(), beforeChoosing);

    Harness inWorld(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(inWorld.ReachLoggedIn());
    const auto before = inWorld.GameServerReads().packets.size();
    inWorld.session.SendCastlePacket(Castle::WriteTaxRate(3, 100000));
    const auto received = inWorld.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(
        SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xc1, 9, 0xb2, 9, 3, 0, 1, 0x86, 0xa0})));
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// CL9 hito 8: the guild's packets (a C1 and the C2 of the list) are read by their rows; one that cannot be read is
// ignored without ending the session.
TEST(ServerSessionTest, GuildPacketsAreReadByTheirRows)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    // The EB 01 with 0 shows the text 1328.
    const auto texts = Client::Tests::MakeTextTable({{1328, "Un texto inventado"}});
    world->resources.texts = &texts;
    const auto join = Bytes({0xc1, 5, 0x50, 0x01, 0x23});
    const auto list =
        Bytes({0xc2, 0, 0x18, 0x52, 1, 0, 0, 0, 5, 0, 0, 0, 0, 'T', 'e', 'r', 'o', 's', 0, 0, 0, 0, 0, 0});
    const auto broken = Bytes({0xc1, 3, 0x51});
    const auto refresh = Bytes({0xc1, 5, 0xeb, 1, 0});
    tested.socket.reads = {Join({join, list, broken, refresh})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 3u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "se ignora el paquete del gremio: no se puede leer"), 1u);
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// What the guild replies send goes through the GameServer just as Protocol::Guild builds it.
TEST(ServerSessionTest, GuildPacketsGoToTheGameServer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    tested.session.SendGuildPacket(Client::Protocol::Guild::WriteMemberListRequest());
    tested.session.SendGuildPacket(Client::Protocol::Guild::WriteJoinAnswer(true, 0x0123));
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 2);
    EXPECT_TRUE(SameBytes(received.packets[before].Bytes(), Bytes({0xc1, 3, 0x52})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 1]).Bytes(), Bytes({0xc1, 6, 0x51, 1, 0x01, 0x23})));
}

TEST(ServerSessionTest, FacingCannotGoToTheConnectServerOrAClosedConnection)
{
    Harness tested(IdentityObfuscation::Shared());
    EXPECT_FALSE(tested.session.SendFacing(3).HasValue());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto before = tested.socket.sent.size();
    EXPECT_FALSE(tested.session.SendFacing(3).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
}

// The chat rooms with a friend (CL9 milestone 10, P-C211): each with its socket, which EnableChatRooms' factory
// builds and which is noted here for the script.
struct ChatRoomHarness
{
    ChatRoomHarness()
    {
        tested.session.EnableChatRooms(Client::App::ChatRoomSockets{.make =
                                                                        [this]
                                                                    {
                                                                        auto socket = std::make_unique<FakeSocket>();
                                                                        socket->pollsUntilConnected = connectingPolls;
                                                                        socket->connectResult = connectResult;
                                                                        sockets.push_back(socket.get());
                                                                        return socket;
                                                                    },
                                                                    .obfuscation = &IdentityObfuscation::Shared(),
                                                                    .paddingSeed = PaddingSeed});
    }

    Harness tested{IdentityObfuscation::Shared()};
    std::vector<FakeSocket*> sockets;
    int connectingPolls = 1;
    std::optional<SocketError> connectResult;
};

// 0x49de50 connects to 55980 at the CA's IP; the C1 10 00 waits for the connect (0x493530) and leaves as is; what
// arrives goes to the sink with the room and the window (0x49e510 → 0x4e1d50 with 1); every 15 s, with the room noted,
// the ChatRoomPingDue (timer 1002); an open room's FD_CLOSE leaves it noted until CloseChatRoom (0x49e480 → 0x4e8380).
TEST(ServerSessionTest, AChatRoomHasItsOwnConnection)
{
    ChatRoomHarness rooms;
    ServerSession& session = rooms.tested.session;
    rooms.tested.AdvanceTo(std::chrono::seconds{15});
    ASSERT_TRUE(session.Poll().HasValue());
    EXPECT_TRUE(rooms.tested.eventSink.TakeWorldEvents().empty());

    session.OpenChatRoom(0x0304, 42, "10.0.0.9");
    ASSERT_EQ(rooms.sockets.size(), 1u);
    FakeSocket& socket = *rooms.sockets[0];
    ASSERT_EQ(socket.connects.size(), 1u);
    EXPECT_EQ(socket.connects[0].host, "10.0.0.9");
    EXPECT_EQ(socket.connects[0].port, 55980);
    EXPECT_EQ(session.ChatRoomCount(), 1u);
    const Client::Protocol::Outgoing join = Client::Protocol::Messenger::WriteJoin(0x0304, 77);
    const ByteVector joinBytes(join.packet.Bytes().begin(), join.packet.Bytes().end());
    session.SendToChatRoom(0x0304, join);
    EXPECT_TRUE(socket.sent.empty());

    socket.reads = {Bytes({0xc1, 0x04, 0x00, 0x01})};
    ASSERT_TRUE(session.Poll().HasValue());
    ASSERT_TRUE(session.Poll().HasValue());
    EXPECT_TRUE(SameBytes(socket.sent, joinBytes));
    std::vector<WorldEvent> events = rooms.tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* packet = std::get_if<Client::App::ChatRoomPacket>(&events[0]);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->room, 0x0304u);
    EXPECT_EQ(packet->window, 42);
    EXPECT_TRUE(SameBytes(packet->packet.Bytes(), Bytes({0xc1, 0x04, 0x00, 0x01})));

    rooms.tested.AdvanceTo(std::chrono::seconds{30});
    ASSERT_TRUE(session.Poll().HasValue());
    events = rooms.tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Client::App::ChatRoomPingDue>(events[0]));

    socket.peerClosed = true;
    ASSERT_TRUE(session.Poll().HasValue());
    events = rooms.tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* closed = std::get_if<Client::App::ChatRoomClosed>(&events[0]);
    ASSERT_NE(closed, nullptr);
    EXPECT_EQ(closed->room, 0x0304u);
    EXPECT_EQ(closed->window, 42);
    EXPECT_TRUE(closed->opened);
    EXPECT_EQ(session.ChatRoomCount(), 1u);

    session.CloseChatRoom(0x0304);
    EXPECT_EQ(session.ChatRoomCount(), 0u);
    session.SendToChatRoom(0x0304, Client::Protocol::Messenger::WritePing());
    EXPECT_TRUE(rooms.tested.Logged(LogLevel::Warning, "no tiene conexión"));
}

// 0x4e2a60's handlers read each room packet by position without looking at its length (Q-C1011): one shorter than
// what its handler reads is ignored with a warning without the text, one per opcode (00, 01, 02, 04 and 0D), and the
// session goes on. A 0D with its '\0' inside, shorter than pkt[0x68], reaches the sink.
TEST(ServerSessionTest, AShortChatRoomPacketIsIgnoredAndTheSessionGoesOn)
{
    ChatRoomHarness rooms;
    ServerSession& session = rooms.tested.session;
    session.OpenChatRoom(0x0304, 42, "10.0.0.9");
    ASSERT_EQ(rooms.sockets.size(), 1u);
    FakeSocket& socket = *rooms.sockets[0];
    socket.reads = {Bytes({0xc1, 0x03, 0x00}),
                    Bytes({0xc1, 0x08, 0x01, 0x00, 0x05, 'H', 'o', 'r'}),
                    Bytes({0xc1, 0x0b, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 'H', 'o'}),
                    Bytes({0xc1, 0x07, 0x04, 0x01, 0x0a, 'h', 'o'}),
                    Bytes({0xc1, 0x0a, 0x0d, 0x00, 0x00, 'q', 'u', 'e', 's', 'o'}),
                    Bytes({0xc1, 0x0b, 0x0d, 0x00, 0x00, 'q', 'u', 'e', 's', 'o', 0x00})};
    for (int poll = 0; poll < 8; ++poll)
        ASSERT_TRUE(session.Poll().HasValue());
    const std::vector<WorldEvent> events = rooms.tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* packet = std::get_if<Client::App::ChatRoomPacket>(&events[0]);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->packet.Opcode(), Client::Network::Opcode{0x0d});
    EXPECT_EQ(rooms.tested.CountLogged(LogLevel::Warning, "de la sala 772 del chat con un amigo"), 5u);
    EXPECT_FALSE(rooms.tested.Logged(LogLevel::Warning, "queso"));
    EXPECT_FALSE(rooms.tested.Logged(LogLevel::Warning, "Hor"));
    EXPECT_EQ(session.Failure(), std::nullopt);
    EXPECT_EQ(session.ChatRoomCount(), 1u);
}

// The failing connect: the room is forgotten (0x49de50 does not note it). A room that already has its own does not
// open another.
TEST(ServerSessionTest, AChatRoomThatDoesNotConnectIsForgotten)
{
    ChatRoomHarness rooms;
    rooms.connectResult = SocketError::Refused;
    ServerSession& session = rooms.tested.session;
    session.OpenChatRoom(9, 7, "10.0.0.9");
    session.OpenChatRoom(9, 8, "10.0.0.9");
    EXPECT_EQ(rooms.sockets.size(), 1u);
    ASSERT_TRUE(session.Poll().HasValue());
    ASSERT_TRUE(session.Poll().HasValue());
    const std::vector<WorldEvent> events = rooms.tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* closed = std::get_if<Client::App::ChatRoomClosed>(&events[0]);
    ASSERT_NE(closed, nullptr);
    EXPECT_EQ(closed->window, 7);
    EXPECT_FALSE(closed->opened);
    EXPECT_EQ(session.ChatRoomCount(), 0u);
}

// Without EnableChatRooms (no network), the room does not open and warns.
TEST(ServerSessionTest, WithoutChatRoomSocketsNoRoomOpens)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.OpenChatRoom(9, 7, "10.0.0.9");
    EXPECT_EQ(tested.session.ChatRoomCount(), 0u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no hay red para las salas"));
}
// 0x4c1e2c: after sending the login only the key is read; no result or version is required.
TEST(ServerSessionTest, AHelloAfterLoginUsesTheKeyWithoutRequiringTheVersion)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    static_cast<void>(tested.eventSink.TakeWorldEvents());
    const auto before = tested.socket.sent.size();
    tested.socket.reads = {FromTheGameServer(Bytes({0xc1, 7, 0xf1, 0, 0, 0x81, 0x23}), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.PlayerIndex(), 0x8123);
    EXPECT_EQ(tested.eventSink.TakeWorldEvents().size(), 1u);
    EXPECT_EQ(tested.socket.sent.size(), before);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// 0x4133cb reads the tick after 0x4c2480; a world application may advance the clock.
TEST(ServerSessionTest, TheMapServerAuthenticationReadsItsTickAfterTheWorldReset)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    static_cast<void>(tested.eventSink.TakeWorldEvents());
    tested.eventSink.onWorld = [&tested](const WorldEvent& event,
                                         ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        if (std::holds_alternative<Client::App::MapServerJoined>(event))
            tested.clock.Advance(std::chrono::milliseconds(17));
        return ApplyAnEmptyWorldReset(event, session);
    };
    tested.socket.reads = {MapServerMovePacket(MapServerHost)};
    static_cast<void>(tested.session.Poll());
    tested.gameServerStart = tested.socket.sent.size();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    PacketSerials serial;
    tested.socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), serial)};
    static_cast<void>(tested.session.Poll());
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 3u);
    const auto auth = Xor32Decode(received.packets[2]);
    const auto bytes = auth.Bytes();
    const auto tick = std::to_integer<std::uint32_t>(bytes[44]) | (std::to_integer<std::uint32_t>(bytes[45]) << 8u) |
                      (std::to_integer<std::uint32_t>(bytes[46]) << 16u) |
                      (std::to_integer<std::uint32_t>(bytes[47]) << 24u);
    EXPECT_EQ(tick, Client::Kernel::TickCountMs(tested.clock.Now()));
}

} // namespace

// 0x4deef0, the F3 24's record: its row reads it, it does not fall to an unknown opcode, and a present byte of 3 or
// more leaves the outcome as it was.
TEST(ServerSessionTest, GuildWarOutcomeIsReadByItsRowWithItsRawPresentByte)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto world = std::make_unique<BareWorld>();
    tested.eventSink.routedWorld = &world->resources;
    const auto before = world->world.Runtime().events.guildWarOutcome.present;
    tested.socket.reads = {Bytes({0xc1, 5, 0xf3, 0x24, 255})};
    const auto polled = tested.session.Poll();
    ASSERT_TRUE(polled.HasValue()) << polled.Error();
    EXPECT_EQ(tested.eventSink.routedAfter.size(), 1u);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "no tiene handler"));
    EXPECT_EQ(world->world.Runtime().events.guildWarOutcome.present, before);
}
