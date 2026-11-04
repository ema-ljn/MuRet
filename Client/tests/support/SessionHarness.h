#pragma once

#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Protocol/BuildConstants.h"

#include "support/CharacterPackets.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/RecordingSink.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <vector>

// Una ServerSession sobre un FakeSocket, llevada hasta el login aceptado sin la escena del login,
// para los tests de las escenas que siguen (CL6.3): la selección, la carga del mundo y el juego. Los
// paquetes son los de SessionPackets.h, en claro, y la cuenta y la lista, sintéticas.
namespace Client::Tests
{
struct LoggedInSession
{
    LoggedInSession()
        : log(sink, Core::LogLevel::Debug), connection(socket, Network::IdentityObfuscation::Shared(), clock, log, 1),
          session(connection, clock, log, GameClientKeys(), App::SessionSettings{App::ServerAddress{"10.0.0.1", 44405}})
    {
    }

    // Del Start al login aceptado: el saludo y la lista del CS, la elección, la dirección, el Opened
    // y el saludo del GS, la cuenta y el resultado.
    void LogIn()
    {
        session.Start();
        socket.reads = {Hello()};
        session.Poll();
        socket.reads = {ServerList({400})};
        session.Poll();
        session.ChooseServer(400);
        socket.reads = {AddressReply("10.0.0.2", 55901)};
        session.Poll();
        session.Poll();
        socket.reads = {GameHello(1, Protocol::ClientVersion)};
        session.Poll();
        session.SubmitCredentials(App::Credentials{"user0001", "pass0002"});
        socket.reads = {LoginResult(1)};
        session.Poll();
        ASSERT_EQ(session.State(), App::SessionState::LoggedIn);
    }

    // La lista de personajes del GS, en claro.
    void ReceiveTheList(const std::vector<CharacterEntryFields>& characters)
    {
        socket.reads = {CharacterListBytes(characters)};
        session.Poll();
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Core::Log log;
    Network::Connection connection;
    App::ServerSession session;
};
} // namespace Client::Tests
