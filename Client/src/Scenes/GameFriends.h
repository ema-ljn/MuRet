#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/Friends.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/OldWindows.h"

#include <string>

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// Los amigos del juego (CL9 hito 9; 04 § Los amigos y 06 § HUD y ventanas, "Los amigos"): abrir la ventana desde el
// panel 1 (0x491080) y los paquetes C0..C4 (0x4dc010..0x4dc720) sobre el administrador de las ventanas viejas y el
// modelo de [0x5763518].
namespace Client::Scenes
{
class WorldState;
class IGameUiRequests;
struct ChatReceiveContext;

// Los textos de Text.bmd de las ventanas viejas: los de la caja (228, 991, 1037 y 1038) y los demás por su índice.
[[nodiscard]] UI::OldWindowTexts OldWindowTextsOf(const Data::Text::TextTable* texts);

// El paquete de lo que pide una ventana de amigos (Protocol/Friends, y Protocol/FriendChat y Protocol/Letters del
// hito 10).
[[nodiscard]] Protocol::Outgoing FriendPacketOf(const UI::FriendSend& send);

// OldWindowStepView::send del juego: el paquete por la sesión. La carta (C2 C5) sale cifrada como C4
// (0x4992fe..0x499416, Q-C1002): antes, un rand() del relleno (0x4992fe) y el serial por la tabla de protección
// (0x5767913), como el C3 F1 02 (0x62449a..0x6244f2). Los demás salen en claro, sin rand().
void SendAFriendPacket(IGameUiRequests& requests, Render::Models::CrtRandom& random, const UI::FriendSend& send);

// Lo que 0x491080 lee del juego.
struct FriendsOpenContext
{
    WorldState& world;
    // El rand() de la CRT: lo saca el cierre del registro propio (3.848).
    Render::Models::CrtRandom& random;
    // Con qué se mide el mensaje del nivel; sin medidor, el mensaje corta.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    const Data::Text::TextTable* texts = nullptr;
};

// 0x491080(0x186, 0x106): 0x490830, el nivel (+0xe) con el registro propio abierto (3.848 rand() al cerrarlo); con
// menos de 6, el texto 1067 (0x555190 con el tipo 1, si no está entre los últimos: 0x45db70 con 1); si no,
// UI::OpenTheFriendsWindow y, si la armó y las dos colas del cartel están quietas (0x46b7d0), el texto 1244 al cartel.
[[nodiscard]] Core::Expected<void, std::string> OpenTheFriends(const FriendsOpenContext& context,
                                                               const UI::OldWindowGlobals& globals,
                                                               const UI::OldWindowStepView& view);

// 0x4dc010: la lista entera (vacía y llena, por nombre y por estado), la pestaña, 0x4918d0(1), el tope de las cartas
// (0x5767970) y, con cartas, el texto 1072 con cuántas y el tope.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendList(const Protocol::Friends::FriendList& list,
                                                                const ChatReceiveContext& context);

// 0x4dc3d0: el resultado de agregar. 1, el texto 1075 y el amigo a la lista; 0, 3, 4, 5 y 6, una caja (0x48ee60(10))
// con el texto 1047 (con el nombre), 1048, 1049 (con el nombre), 1050 o 1068.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendAddResult(const Protocol::Friends::FriendAddResult& result,
                                                                     const ChatReceiveContext& context);

// 0x4dc5b0: "<nombre><1051>" en una caja de dos botones (0x48ee60(9), dueño −1) que guarda el nombre (0x4a4440), y el
// sonido 0x2a.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendRequest(const Protocol::Friends::FriendRequest& request,
                                                                   const ChatReceiveContext& context);

// 0x4dc690: 0, la caja con el texto 1052; 1, el amigo fuera de la lista.
[[nodiscard]] Core::Expected<void, std::string>
ApplyFriendDeleteResult(const Protocol::Friends::FriendDeleteResult& result, const ChatReceiveContext& context);

// 0x4dc720: con 0xfc, todos fuera de línea y 0x4918d0(0); si no, el estado del amigo.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendState(const Protocol::Friends::FriendState& state,
                                                                 const ChatReceiveContext& context);
} // namespace Client::Scenes
