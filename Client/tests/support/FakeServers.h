#pragma once

#include "Core/IClock.h"
#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/LoopbackServer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The fake ConnectServer and GameServer: two LoopbackServers on 127.0.0.1, on ports the system picks, polled on the
// client's thread. They follow 04-protocolo.md, "El flujo de conexión", with the lab's wire bytes and its FoxAnti: the
// CS sends the list in the lab's form and expects the choice already mapped to code 0, and the GS sends everything
// encrypted and with the 0xAA on each byte. The F4 03 gives 127.0.0.1 and the fake GS's port, not the wire's address.
// The GS accepts and counts keepalives before and after the login, answers an F3 00 with a clear list of two
// characters (char0001 in slot 0 and char0002 in slot 2) and stores an F3 03.
// Each answers only what comes next in the script. Anything else goes to Complaints(): the server cuts the client
// without a byte and stops answering, so the session fails at once instead of waiting for the timeout. Needs a live
// SocketApi::NetworkInit.
namespace Client::Tests
{
class FakeServers
{
public:
    // The server-side keys (GameServerKeys() in TestGameServer.h): the GS decrypts the login and encrypts the greeting
    // and the result with them.
    explicit FakeServers(Network::ConnectionKeys serverKeys);

    // A synthetic idle timeout to test the human wait without sleeping: a valid 0E renews it, zero disables it. It is
    // not a measured timeout of the original GS. The clock outlives these servers and is read on the same thread.
    FakeServers(Network::ConnectionKeys serverKeys, Core::IClock& clock, Core::Duration idleTimeout);

    [[nodiscard]] std::uint16_t ConnectServerPort() const;
    [[nodiscard]] std::uint16_t GameServerPort() const;

    // One round of both: accept, read what the client sent and answer what comes next.
    void Poll();

    // Whether the GS received a login that decrypts, with the expected serial, and accepted it.
    [[nodiscard]] bool SawLogin() const;
    // That login, decrypted and still with its XOR32, to compare with WriteLoginRequest using SameBytesWithoutValues;
    // empty until it arrives.
    [[nodiscard]] const std::optional<Network::Packet>& Login() const;
    // The keepalives the GS received, encrypted, before or after the login.
    [[nodiscard]] std::size_t Keepalives() const;
    // Whether the synthetic timeout closed the GS for lack of a valid 0E.
    [[nodiscard]] bool IdleExpired() const;
    // The F3 03 the GS received, in clear and without the XOR32; empty until it arrives.
    [[nodiscard]] const std::optional<Network::Packet>& CharacterSelect() const;
    // Whether the CS, with the address already answered, saw the client leave (04: "el cliente corta con el CS").
    [[nodiscard]] bool ClientLeftTheConnectServer() const;
    // What they saw off the script, in words; empty if everything went as 04 says.
    [[nodiscard]] const std::vector<std::string>& Complaints() const;

private:
    enum class ConnectStep : std::uint8_t
    {
        WaitingClient,
        WaitingPetInfoRequest, // the A9
        WaitingListRequest,    // the F4 02
        WaitingServerSelect,   // the F4 03
        Answered,              // the address went out: what follows is the disconnect
        Stopped,               // something off the script: it cut the client
    };

    enum class GameStep : std::uint8_t
    {
        WaitingClient,
        WaitingLogin, // the greeting went out
        LoggedIn,     // the result went out: what follows is keepalives
        Stopped,      // something off the script: it cut the client
    };

    void PollConnectServer();
    void PollGameServer();
    void CheckGameServerIdle();
    void OnConnectServerPacket(const Network::Packet& packet);
    void OnGameServerPacket(const Network::Packet& packet);
    // After the login: an encrypted keepalive, or a clear F3.
    void OnAfterLogin(const Network::Packet& packet);
    // Already decrypted, with a validated serial; before and after the login it uses the same counter.
    void OnKeepalive();
    void OnCharacterPacket(const Network::Packet& packet);

    // std::nullopt if there is no whole packet or if splitting it failed.
    [[nodiscard]] std::optional<Network::Packet> NextPacket(Network::PacketReader& reader, bool& failed);
    // Whether `packet` is `expected`; otherwise the CS stops with a complaint naming `what`.
    bool Expect(const Network::Packet& packet, const std::vector<std::byte>& expected, std::string_view what);
    // Record `complaint`, cut the client and stop answering.
    void StopConnectServer(std::string complaint);
    void StopGameServer(std::string complaint);
    // What the GS answers: encrypted with its key and serials, and with FoxAnti's 0xAA.
    void SendFromTheGameServer(const std::vector<std::byte>& plain);
    // What the GS answers in clear, like the character list: only with FoxAnti's 0xAA.
    void SendClearFromTheGameServer(std::vector<std::byte> plain);

    Network::ConnectionKeys m_keys;
    LoopbackServer m_connectServer;
    LoopbackServer m_gameServer;
    Network::PacketReader m_connectReader;
    Network::PacketReader m_gameReader;
    Network::PacketSerials m_gameSerials; // both GS directions, from 0
    ConnectStep m_connectStep = ConnectStep::WaitingClient;
    GameStep m_gameStep = GameStep::WaitingClient;
    std::optional<Network::Packet> m_login;
    std::size_t m_keepalives = 0;
    Core::IClock* m_clock = nullptr;
    Core::Duration m_idleTimeout{};
    std::optional<Core::Duration> m_lastKeepalive;
    bool m_idleExpired = false;
    bool m_characterListSent = false;
    std::optional<Network::Packet> m_characterSelect;
    std::vector<std::string> m_complaints;
};
} // namespace Client::Tests
