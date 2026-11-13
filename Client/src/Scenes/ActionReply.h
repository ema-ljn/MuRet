#pragma once

#include "Core/Expected.h"
#include "Scenes/ActionSend.h"

#include <string>

namespace Client::Protocol::Movement
{
struct Action;
}

// La acción de un personaje que llega del GameServer (CL9 hito 1; 04 § La acción, 18; 06 § El chat, "El emote"). El
// GameServer la reenvía a los del viewport, no a quien la hizo (CGActionRecv, 0x425cd0 del GS): el emote propio se ve
// por 0x576a60 y el de los demás por acá. *Estático*.
namespace Client::Scenes
{
struct WorldEntryResources;

// 0x4cb750: el personaje de la clave (0x5110d0, el primero de los 400 vivo; sin ninguno, MuRet descarta el paquete:
// Q-C778). Con el caballo oscuro (+0x318 en 0x1c16) no hace nada. Si no, el ángulo de la dirección ((pkt[5] − 1) · 45),
// +0x378 (Movement) en 0 y la X y la Y en el centro de su celda blanco (+0x39a y +0x39b · 100 + 50, Q-C779), y la
// acción de pkt[6]: 0x12 la 0x98 y el sonido 0x6f (CL10); 0x78 y 0x79 corta (0x4f4a10, hito 3); 0x7a y 0x7b 0x4f2250
// (sentado, con el C1 18 de `sendAction`); 0x7e la 5; de 0x80 a 0x9b la animación de la tabla, con la del elfo; y
// cualquier otra, ella misma (0x4ea030 no pone la que el modelo no tiene). No saca rand(), salvo 0x4f2250.
[[nodiscard]] Core::Expected<void, std::string> ApplyActionReply(const Protocol::Movement::Action& reply,
                                                                 const WorldEntryResources& resources,
                                                                 const ActionSender& sendAction);
} // namespace Client::Scenes
