#include "Protocol/Chat/ChatSend.h"

#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace Client::Protocol::Chat
{
namespace
{
// The text as the original copies it: ChatTextBytes of it, with the '\0' if it fits.
Outgoing WriteSaid(Network::Opcode opcode, std::span<const std::byte, ChatNameSize> name, std::string_view text)
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
    return WriteSaid(ChatOpcode, name, text);
}

Outgoing WriteWhisper(std::span<const std::byte, ChatNameSize> target, std::string_view text)
{
    return WriteSaid(WhisperOpcode, target, text);
}
} // namespace Client::Protocol::Chat
