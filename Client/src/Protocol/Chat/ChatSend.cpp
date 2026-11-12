#include "Protocol/Chat/ChatSend.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Client::Protocol::Chat
{
namespace
{
// El texto como lo copia el original: hasta ChatTextBytes, con el '\0' si entra.
Outgoing WriteSaid(std::uint8_t opcode, std::span<const std::byte, ChatNameSize> name, std::string_view text)
{
    std::string said(text.substr(0, text.find('\0')));
    said.push_back('\0');
    said.resize(ChatTextBytes(text));
    return Xor32C1(Network::PacketWriter::C1(opcode).PutBytes(name).PutBytes(std::as_bytes(std::span(said))));
}
} // namespace

std::size_t ChatTextBytes(std::string_view text)
{
    const std::size_t length = std::min(text.find('\0'), text.size());
    return std::min(length + 1, ChatTextLimit);
}

Outgoing WriteChat(std::span<const std::byte, ChatNameSize> name, std::string_view text)
{
    return WriteSaid(ChatSendOpcode, name, text);
}

Outgoing WriteWhisper(std::span<const std::byte, ChatNameSize> target, std::string_view text)
{
    return WriteSaid(WhisperSendOpcode, target, text);
}
} // namespace Client::Protocol::Chat
