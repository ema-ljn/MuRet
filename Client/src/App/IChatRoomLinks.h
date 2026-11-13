#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>
#include <string_view>

// La conexión de cada sala del chat con un amigo (CL9 hito 10; 04 § Las cartas y el chat con un amigo, "La conexión
// de la sala"): 0x49de50 abre, por sala, un socket de la misma clase que el del GameServer (0x4e8090, 0x4e81b0) al
// puerto 55980 de la IP del CA, con WSAAsyncSelect y el mensaje 0x500 + su lugar (0x4e8570). Lo que llega pasa por el
// mismo camino que lo del GameServer (0x49e510 → 0x4e1d50 con 1 → 0x4e6240); lo que sale, por su send. Lo implementa
// la sesión (App::ServerSession) y lo usan las salas de Scenes. Los métodos de esa clase que suelta una sala (0x49e110,
// el destructor del objeto de 0x402c bytes, → 0x4e8180 0x4e8860 0x4e8bd0 0x4e8c30 0x4e8ea0 0x4e8ef0 0x4e8f10 0x4e8f50
// 0x4e9330 0x4e9350; P-C216, *inferido* por quién los llama) los reemplazan Network::Connection y su ISocket.
namespace Client::App
{
// El puerto de 0x49de50 (0xdaac).
inline constexpr std::uint16_t ChatRoomPort = 55980;

class IChatRoomLinks
{
public:
    virtual ~IChatRoomLinks() = default;
    // 0x49de50: abre la conexión de la sala `room` del chat `window` al ChatRoomPort de `address`. Con una que ya
    // estaba para la sala, la deja (el original abre otra que no anota, *inferido*).
    virtual void OpenChatRoom(std::uint32_t room, std::int32_t window, std::string_view address) = 0;
    // El send de la conexión de la sala (0x493510 la busca); mientras conecta, espera en su cola.
    virtual void SendToChatRoom(std::uint32_t room, Protocol::Outgoing packet) = 0;
    // 0x49dfd0: cierra la conexión de la sala (0x4e8380) y la olvida.
    virtual void CloseChatRoom(std::uint32_t room) = 0;

protected:
    IChatRoomLinks() = default;
    IChatRoomLinks(const IChatRoomLinks&) = default;
    IChatRoomLinks& operator=(const IChatRoomLinks&) = default;
};
} // namespace Client::App
