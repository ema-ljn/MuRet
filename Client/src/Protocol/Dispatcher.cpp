#include "Protocol/Dispatcher.h"

#include "Core/Log.h"
#include "Network/Packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace Client::Protocol
{
Dispatcher::Dispatcher(Core::Log& log) : m_log(log) {}

bool Dispatcher::On(std::uint8_t opcode, Handler handler)
{
    if (!handler || m_opcodes.contains(opcode))
    {
        return false;
    }
    m_opcodes[opcode].whole = std::move(handler);
    return true;
}

bool Dispatcher::On(std::uint8_t opcode, std::uint8_t subcode, Handler handler)
{
    if (!handler)
    {
        return false;
    }
    const auto registered = m_opcodes.find(opcode);
    if (registered != m_opcodes.end() && (registered->second.whole || registered->second.bySubcode.contains(subcode)))
    {
        return false;
    }
    m_opcodes[opcode].bySubcode.emplace(subcode, std::move(handler));
    return true;
}

DispatchResult Dispatcher::Dispatch(const Network::Packet& packet)
{
    // std::nullopt en un C3/C4: su opcode viaja cifrado, y la Connection lo descifra antes.
    const std::optional<std::uint8_t> opcode = packet.Opcode();
    if (!opcode)
    {
        return DispatchResult::Malformed;
    }
    const auto registered = m_opcodes.find(*opcode);
    if (registered == m_opcodes.end())
    {
        if (!m_warnedOpcodes.test(*opcode))
        {
            m_warnedOpcodes.set(*opcode);
            m_log.Warning("un paquete con el opcode {:02X} no tiene handler: se descarta, y este aviso no se repite",
                          *opcode);
        }
        return DispatchResult::Unknown;
    }
    if (registered->second.whole)
    {
        registered->second.whole(packet);
        return DispatchResult::Handled;
    }
    return DispatchBySubcode(*opcode, registered->second, packet);
}

DispatchResult Dispatcher::DispatchBySubcode(std::uint8_t opcode, OpcodeHandlers& handlers,
                                             const Network::Packet& packet)
{
    // El subcódigo es el byte que sigue al opcode: pkt[3] en un C1, pkt[4] en un C2.
    const std::span<const std::byte> bytes = packet.Bytes();
    const std::size_t subcodeAt = Network::MinPacketSize(packet.Type());
    if (bytes.size() <= subcodeAt)
    {
        return DispatchResult::Malformed;
    }
    const auto subcode = std::to_integer<std::uint8_t>(bytes[subcodeAt]);
    const auto registered = handlers.bySubcode.find(subcode);
    if (registered == handlers.bySubcode.end())
    {
        if (!handlers.warnedSubcodes.test(subcode))
        {
            handlers.warnedSubcodes.set(subcode);
            m_log.Warning("un paquete con el opcode {:02X} y el subcódigo {:02X} no tiene handler: se descarta, y "
                          "este aviso no se repite",
                          opcode, subcode);
        }
        return DispatchResult::Unknown;
    }
    registered->second(packet);
    return DispatchResult::Handled;
}
} // namespace Client::Protocol
