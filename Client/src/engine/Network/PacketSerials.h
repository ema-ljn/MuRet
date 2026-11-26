#pragma once

#include "EngineApi.h"

#include <cstdint>

// The send and receive serials of a connection's C3/C4 (03-red-y-cifrado.md, "C3/C4"): the first encrypted byte. In
// the original they are two global bytes (0x05767913 and 0x05767914) that 0x4c1340 resets when connecting to the
// GameServer; here they belong to each connection.
namespace Client::Network
{
enum class SerialCheck : std::uint8_t
{
    InOrder,
    Resynced, // it was another, and from there the next one is expected (Q-C04)
};

class ENGINE_API PacketSerials
{
public:
    // From 255 back to 0.
    [[nodiscard]] std::uint8_t TakeSend();

    // The original, on a mismatch, keeps the received one without adding 1, so the next one does not match either and
    // the offset stays (Q-C04). Here it resyncs, and Resynced tells the user to log it once.
    [[nodiscard]] SerialCheck Receive(std::uint8_t received);

    // A packet that arrived and is not dispatched: the next of the expected one is expected, ignoring the serial it
    // carried. With a bad checksum the original does this. With InvalidBlock or TooShort, if the first block passed its
    // checksum the serial is known; not resyncing with it is MuRet's choice.
    void SkipReceive();

    // Like 0x4c1340 when connecting to the GameServer.
    void Reset();

private:
    std::uint8_t m_send = 0;
    std::uint8_t m_receive = 0;
};
} // namespace Client::Network
