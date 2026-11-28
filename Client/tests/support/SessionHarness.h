#pragma once

#include "Session/Credentials.h"
#include "Session/ISessionEventSink.h"
#include "Session/ServerAddress.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Protocol/Session/BuildConstants.h"

#include "support/CharacterPackets.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <vector>

// A ServerSession over a FakeSocket, taken up to the accepted login without the login scene, for the tests of the
// scenes that follow (CL6.3): the selection, the world load and the game. The packets are those of SessionPackets.h,
// in clear, and the account and the list are synthetic.
namespace Client::Tests
{
struct LoggedInSession
{
    explicit LoggedInSession(App::ISessionEventSink* eventSinkOverride = nullptr)
        : log(sink, Core::LogLevel::Debug), connection(socket, Network::IdentityObfuscation::Shared(), clock, log, 1),
          session(connection, clock, log, GameClientKeys(),
                  App::SessionSettings{Session::ServerAddress{"10.0.0.1", 44405}},
                  eventSinkOverride != nullptr ? *eventSinkOverride : eventSink)
    {
    }

    // From Start to the accepted login: the CS hello and list, the choice, the address, the GS's Opened and hello, the
    // account and the result.
    void LogIn()
    {
        session.Start();
        socket.reads = {Hello()};
        static_cast<void>(session.Poll());
        socket.reads = {ServerList({400})};
        static_cast<void>(session.Poll());
        session.ChooseServer(400);
        socket.reads = {AddressReply("10.0.0.2", 55901)};
        static_cast<void>(session.Poll());
        static_cast<void>(session.Poll());
        socket.reads = {GameHello(1, Protocol::Session::ClientVersion)};
        static_cast<void>(session.Poll());
        session.SubmitCredentials(App::Credentials{"user0001", "pass0002"});
        socket.reads = {LoginResult(1)};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), Session::SessionState::LoggedIn);
    }

    // The GS's character list, in clear.
    void ReceiveTheList(const std::vector<CharacterEntryFields>& characters)
    {
        socket.reads = {CharacterListBytes(characters)};
        static_cast<void>(session.Poll());
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Core::Log log;
    Network::Connection connection;
    RecordingSessionEventSink eventSink;
    App::ServerSession session;
};
} // namespace Client::Tests
