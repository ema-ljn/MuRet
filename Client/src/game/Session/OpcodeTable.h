#pragma once

#include "Network/Packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace Client::App
{
class ServerSession;
}

// What each code that reaches the client does: one row per case of the server's dispatcher (0x4e2b10 for both servers,
// coverage/dispatcher-cases.tsv) and one per code the client only writes. dispatcher_cases.py checks the rows against
// the cases and the opcode lists.
namespace Client::Session
{
// A case MuRet does not handle yet: the packet goes the way of a code without a row.
struct Unported
{
};
// A code the client only writes: one that arrives goes the way of a code without a row.
struct SentOnly
{
};
using RowHandler = void (*)(App::ServerSession& session, const Network::Packet& packet);

struct OpcodeRow
{
    Network::Opcode opcode;
    // Without one, the row takes the whole opcode.
    std::optional<Network::Subcode> subcode;
    // The case of 0x4e2b10 that reaches the handler; 0 for a code the client only writes.
    std::uint32_t address = 0;
    std::string_view name;
    // The dispatcher copies the packet to the ring of the last 0B, 12, 1C, 24 and 2A before the handler, even short or
    // rejected (ViewportPacketRing.h).
    bool copiesToRing = false;
    std::variant<RowHandler, Unported, SentOnly> action;
};

// The rows in the order of the cases of 0x4e2b10, by opcode and then by subcode.
[[nodiscard]] std::span<const OpcodeRow> GameServerOpcodes();
[[nodiscard]] std::span<const OpcodeRow> ConnectServerOpcodes();

inline constexpr bool CopiedToRing = true;

[[nodiscard]] constexpr OpcodeRow Row(Network::Opcode opcode, std::optional<Network::Subcode> subcode,
                                      std::uint32_t address, std::string_view name,
                                      std::variant<RowHandler, Unported, SentOnly> action, bool copiesToRing = false)
{
    return {opcode, subcode, address, name, copiesToRing, action};
}

// A row's handler that calls the session's `Handler`.
template <void (App::ServerSession::*Handler)(const Network::Packet&)>
void Call(App::ServerSession& session, const Network::Packet& packet)
{
    (session.*Handler)(packet);
}

// No two rows share a code, and no opcode has a row of its own and rows by subcode.
[[nodiscard]] constexpr bool EachCodeHasOneRow(std::span<const OpcodeRow> rows)
{
    for (std::size_t first = 0; first < rows.size(); ++first)
    {
        for (std::size_t second = first + 1; second < rows.size(); ++second)
        {
            const OpcodeRow& a = rows[first];
            const OpcodeRow& b = rows[second];
            if (a.opcode == b.opcode && (!a.subcode || !b.subcode || *a.subcode == *b.subcode))
                return false;
        }
    }
    return true;
}
} // namespace Client::Session
