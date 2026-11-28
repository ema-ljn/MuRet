#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>
#include <string_view>

// The connection of each chat room with a friend (CL9 milestone 10; 04 § Las cartas y el chat con un amigo, "La
// conexión de la sala"): 0x49de50 opens, per room, a socket of the same class as the GameServer's (0x4e8090,
// 0x4e81b0) to port 55980 of the CA's IP, with WSAAsyncSelect and the message 0x500 + its place (0x4e8570). What
// arrives takes the same path as the GameServer's (0x49e510 → 0x4e1d50 with 1 → the switch 0x4e2a60); what leaves,
// its send. The session (App::ServerSession) implements it and the Messenger rooms use it. The methods of that class
// that a room releases (0x49e110, the destructor of the 0x402c-byte object, → 0x4e8180 0x4e8860 0x4e8bd0 0x4e8c30
// 0x4e8ea0 0x4e8ef0 0x4e8f10 0x4e8f50 0x4e9330 0x4e9350; P-C216, *inferred* from their callers) are replaced by
// Network::Connection and its ISocket.
namespace Client::Chat
{
// 0x49de50's port (0xdaac).
inline constexpr std::uint16_t ChatRoomPort = 55980;

class IChatRoomLinks
{
public:
    virtual ~IChatRoomLinks() = default;
    // 0x49de50: opens the connection of room `room` of chat `window` to the ChatRoomPort of `address`. If the room
    // already has one, it keeps it (the original opens another that it does not record, *inferred*).
    virtual void OpenChatRoom(std::uint32_t room, std::int32_t window, std::string_view address) = 0;
    // The send of the room's connection (0x493510 looks it up); while it connects, the packet waits in its queue.
    virtual void SendToChatRoom(std::uint32_t room, Protocol::Outgoing packet) = 0;
    // 0x49dfd0: closes the room's connection (0x4e8380) and forgets it.
    virtual void CloseChatRoom(std::uint32_t room) = 0;

protected:
    IChatRoomLinks() = default;
    IChatRoomLinks(const IChatRoomLinks&) = default;
    IChatRoomLinks& operator=(const IChatRoomLinks&) = default;
};
} // namespace Client::Chat
