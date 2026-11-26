#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "EngineApi.h"
#include "Network/CloseReason.h"
#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"
#include "Network/Delivery.h"
#include "Network/IObfuscation.h"
#include "Network/ISocket.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"
#include "Network/SendQueue.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <variant>
#include <vector>

// A packet's path through a server connection, in the original's order (03-red-y-cifrado.md, "Cadena de recepción"
// and "C3/C4", and IObfuscation.h). Receiving: the socket, the framing with the obfuscation's mask (PacketReader),
// AfterCut, a C3/C4's decryption and BeforeDispatch. Sending: encryption of what goes Encrypted (with the
// obfuscation's BeforeEncrypt), BeforeSend and the queue (SendQueue). The loop polls it once per frame on its thread
// (D-C5) and it never waits, except resolving the name in Open. What happens comes out as events, in order, and is
// not dispatched here: Network does not see Protocol. Keys are per connection: the GameServer's has them, the
// ConnectServer's does not; without keys an incoming C3/C4 is Undecryptable and what goes Encrypted does not leave.
// C3/C4 serials restart at 0 on each Open, like 0x4c1340 when connecting to the GameServer. A framing error closes
// with BadStream, since where the next packet starts is lost; an obfuscation InvalidPacket is dropped with a warning
// and the connection goes on, since the header gave the length.
namespace Client::Network
{
// The original has none: its connect blocks (0x4e8570).
inline constexpr Core::Duration ConnectTimeout = std::chrono::seconds{10};

// Reads of 0x2000, the original's recv buffer (0x4e88af), until there is no more or up to 8, to bound a frame's work.
// The 8 is MuRet's.
inline constexpr std::size_t ReceiveChunkSize = 0x2000;
inline constexpr int MaxReadsPerPoll = 8;

// If resolving the host and starting to connect takes longer, it is logged: the system resolver waits on getaddrinfo,
// and the game does not draw meanwhile.
inline constexpr Core::Duration SlowConnectStart = std::chrono::milliseconds{100};

// The C3/C4 keys with the GameServer: Enc1.dat for what leaves and Dec2.dat for what arrives (03 § C3/C4).
struct ConnectionKeys
{
    Client::Crypto::EncryptionKey encryption;
    Client::Crypto::DecryptionKey decryption;
};

struct Opened // the connect finished: goes before what arrived in the same Poll
{
};
// Not dispatched: the connection's user decides whether to send the F1 03 notice (the original sends it with a bad
// checksum). The connection already logged it.
struct Undecryptable
{
    // std::nullopt if the connection has no keys (the ConnectServer's).
    std::optional<Client::Crypto::CryptoError> error;
};
struct Closed // always last in the batch; the reason also stays in LastCloseReason()
{
    CloseReason reason;
};
using Event = std::variant<Opened, Packet, Undecryptable, Closed>;

class ENGINE_API Connection
{
public:
    // The socket, the obfuscation, the clock and the log belong to the builder and must outlive it; a temporary
    // obfuscation does not compile. `paddingSeed` seeds the std::mt19937 of the encrypted padding byte, which the
    // original takes from rand(): the tests give a fixed one.
    Connection(ISocket& socket, const IObfuscation& obfuscation, Core::IClock& clock, Core::Log& log,
               std::uint32_t paddingSeed);
    Connection(ISocket& socket, const IObfuscation&& obfuscation, Core::IClock& clock, Core::Log& log,
               std::uint32_t paddingSeed) = delete;

    // It keeps references.
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() = default;

    // Closes whatever is open without an event (its user asked), empties the framing and the queue, resets the serials
    // to 0, keeps the keys (std::nullopt: no encryption, as with the ConnectServer) and starts connecting. A connect
    // the socket already finished comes out in the next Poll: the Opened, because PollConnect repeats the result
    // (ISocket.h), or the Closed if it failed.
    void Open(std::string_view host, std::uint16_t port, std::optional<ConnectionKeys> keys);

    // Once per frame: PollConnect, then the ConnectTimeout (a long frame does not cut a connect that already finished),
    // sends what is queued, reads, frames, translates and decrypts. Whoever walks the batch drops the rest if midway it
    // called Open or Close, or if State() became Closed (a Send from a handler can close it): it belongs to a
    // connection that is gone. It only reads or sends with the socket connected: a Closed there is the other side
    // closing.
    [[nodiscard]] std::vector<Event> Poll();

    // XOR32 is already applied if it carries it (Protocol). What goes Encrypted is encrypted as C3/C4 with the next
    // serial and a padding byte from the std::mt19937; if EncryptPacket fails it does not leave, with a warning naming
    // the type, opcode and error, and the connection goes on. Then BeforeSend, the queue and, with the connection open,
    // whatever the socket takes: with something pending, the new goes behind, never direct. While connecting it waits
    // in the queue until the Opened. A full queue or a socket failure closes it, and the Closed comes out in the next
    // Poll. Closed, it is dropped with a warning; so is what goes Encrypted without keys.
    void Send(Packet packet, Delivery delivery);

    // Without an event: its user asked, and LastCloseReason() gives ByClient. Does nothing if already closed.
    void Close();

    [[nodiscard]] LinkState State() const;

    // Since the last Open; std::nullopt if it did not close.
    [[nodiscard]] std::optional<CloseReason> LastCloseReason() const;

private:
    void PollConnect(std::vector<Event>& events);

    // false if it closed.
    bool FlushQueue();

    void ReadAvailable(std::vector<Event>& events);

    // false if the framing failed and it closed.
    bool TakePackets(std::vector<Event>& events);

    // AfterCut, decryption if it is a C3/C4, and BeforeDispatch: the Packet to dispatch, or a C3/C4's Undecryptable.
    // std::nullopt if dropped, with its warning.
    std::optional<Event> Translate(Packet packet);

    // The rebuilt C1/C2, or why not, with its warning.
    Core::Expected<Packet, Undecryptable> Decrypt(const Packet& encrypted);

    std::byte NextPadding();

    // Closes the socket, empties the framing and the queue, and logs it. The Closed comes out in the next Poll, or at
    // the end of this one, except with ByClient.
    void CloseWith(CloseReason reason);

    ISocket& m_socket;
    const IObfuscation& m_obfuscation;
    Core::IClock& m_clock;
    Core::Log& m_log;

    LinkState m_state = LinkState::Closed;
    Core::Duration m_connectStartedAt{};
    std::optional<CloseReason> m_lastCloseReason;
    std::optional<CloseReason> m_closeToReport; // the Closed not yet reported by a Poll
    PacketReader m_reader;
    SendQueue m_queue;
    std::optional<ConnectionKeys> m_keys;
    PacketSerials m_serials;
    std::mt19937 m_padding;
    std::array<std::byte, ReceiveChunkSize> m_receiveBuffer{};
};
} // namespace Client::Network
