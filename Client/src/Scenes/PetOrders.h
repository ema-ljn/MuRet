#pragma once

#include <cstdint>

namespace Client::Protocol::Pet
{
struct PetOrder;
struct PetAttack;
} // namespace Client::Protocol::Pet

// Las órdenes del cuervo que llegan (CL9 hito 4): el A7 y el A8 sobre el objeto del cuervo del Dark Lord (+0x430,
// Scenes/DarkSpirit.h). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las órdenes del cuervo". Lo que el
// paso del cuervo hace con ellas (los estados 2, 5 y 6 de 0x416a20) está en Scenes/DarkSpirit (06, "El ataque del
// cuervo").
namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;

// 0x421b50: con el cuervo del héroe (0x743a170), 0x416510(clave, orden): la orden en +0x45c; fuera de los estados 5 y
// 6, +0x1ad en 0; con la orden 3, +0x1ad en 1 y el blanco (+8) en el personaje de la clave (0x5110d0, el 400 si no
// está). Sin rand().
void ApplyPetOrder(const Protocol::Pet::PetOrder& order, WorldState& world);

// 0x4e4c42 y 0x421b70: con el cuervo del dueño (0x5110d0, el 400 si no está), 0x416570(blanco, tipo): el blanco en +8;
// con 0x1000 en +0x78 del dueño, nada más; si no, el lugar del blanco en +0x3b0 y, con 0x4000, el estado tipo + 5
// (0x4164f0) y nada más. Si no, +0x38d en 0, el estado tipo + 5 y, en el 5, +0x1ad en 1, +0xf0 en 0,5, la velocidad
// (+0xe4) en rand() % 10 + 20 y el sonido 0x1d2; en el 6, el sonido 0x1d0 (CL10, WorldRuntime::lastSoundId).
void ApplyPetAttack(const Protocol::Pet::PetAttack& attack, const WorldEntryResources& resources);
} // namespace Client::Scenes
