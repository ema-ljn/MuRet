#pragma once

namespace Client::Protocol::Pets
{
struct PetOrder;
struct PetAttack;
} // namespace Client::Protocol::Pets

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The incoming raven orders: the A7 and the A8 on the Dark Lord's raven object (+0x430, Pets/DarkSpirit.h). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Las órdenes del cuervo". What the raven step does with them (states
// 2, 5 and 6 of 0x416a20) is in Pets/DarkSpirit (06, "El ataque del cuervo").
namespace Client::Pets
{

// 0x421b50: with the hero's raven (0x743a170), 0x416510(key, order): the order at +0x45c; outside states 5 and 6,
// +0x1ad to 0; with order 3, +0x1ad to 1 and the target (+8) at the key's character (0x5110d0, 400 if absent). No
// rand().
void ApplyPetOrder(const Protocol::Pets::PetOrder& order, Kernel::WorldState& world);

// 0x4e4c42 and 0x421b70: with the owner's raven (0x5110d0, 400 if absent), 0x416570(target, type): the target at +8;
// with 0x1000 in the owner's +0x78, nothing else; otherwise the target's slot at +0x3b0 and, with 0x4000, state type +
// 5 (0x4164f0) and nothing else. Otherwise +0x38d to 0, state type + 5 and, in 5, +0x1ad to 1, +0xf0 to 0.5, the speed
// (+0xe4) to rand() % 10 + 20 and sound 0x1d2; in 6, sound 0x1d0 (CL10, WorldRuntime::lastSoundId).
void ApplyPetAttack(const Protocol::Pets::PetAttack& attack, const Scenes::WorldEntryResources& resources);
} // namespace Client::Pets
