#include "App/LabXorRemapTables.h"
#include "App/SessionState.h"
#include "Network/XorRemap/XorRemapObfuscation.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "test_server_session_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>

// Client::App::ServerSession over the XOR and remap layer of the lab server (test_server_session.cpp has the rest).
namespace
{
using Client::App::LabXorRemapTables;
using Client::App::SessionState;
using Client::Network::XorRemapObfuscation;
using Client::Tests::Bytes;
using Client::Tests::Hello;
using Client::Tests::Join;
using Client::Tests::ListRequest;
using Client::Tests::PetInfoRequest;
using Client::Tests::SameBytes;
using Client::Tests::ServerSessionSupport::ByteVector;
using Client::Tests::ServerSessionSupport::EncryptedWorldSequence;
using Client::Tests::ServerSessionSupport::ExpectWorldSequence;
using Client::Tests::ServerSessionSupport::Harness;
using Client::Tests::ServerSessionSupport::LabServerCode;
using Client::Tests::ServerSessionSupport::LabServerList;

// WasDecrypted() must survive the inbound mask, which comes off before framing and decrypting.
TEST(ServerSessionXorRemapTest, WorldEventsSurviveTheRealTransport)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    Harness tested(xorRemap);
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    ByteVector wire = EncryptedWorldSequence(tested);
    for (std::byte& value : wire)
    {
        value ^= LabXorRemapTables().inboundXor;
    }
    tested.socket.reads = {wire};
    static_cast<void>(tested.session.Poll());

    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// The lab's list becomes F4 06, and ChooseServer(400) leaves as the ConnectServer's code 0.
TEST(ServerSessionXorRemapTest, ChoosingSendsTheCode)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    Harness tested(xorRemap);
    tested.session.Start();
    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());
    tested.socket.reads = {LabServerList()};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    ASSERT_EQ(tested.session.ServerList().size(), 1u);
    EXPECT_EQ(tested.session.ServerList()[0].code, LabServerCode);

    tested.session.ChooseServer(LabServerCode);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{LabServerCode});
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00})})));
}
} // namespace
