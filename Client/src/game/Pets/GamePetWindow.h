#pragma once

#include "Core/Expected.h"

#include <string>

// The Dark Lord's pet window and the raven's label (06 § El A9 de las mascotas, "La ventana de la mascota"). The window
// step (0x424759) is in Hud/GameUi; what it draws comes from the 0x723960 table (WorldRuntime::pets) and the hero's
// raven (ActorState::darkSpirit), which the A9 writes.
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

namespace Client::Pets
{

// 0x424160(hero), which 0x5f7440 always calls (0x5f75e7), between the quests window and the 0x2000: the button and the
// frame (0x4249a0; with the character panel closed, only 0x723911 to 0; the back is 0x5ee220), with the window open tab
// 0 (0x424e40, the stats by 0x4149b0) or tab 1 (0x416670), and with a raven its label (vtable + 0xc, 0x4182e0 →
// 0x418460). Each own-record access draws 3848 rand() when it closes at 0, and the horse's damage opens skill 0x3e of
// Skill.bmd (60). The type-0 branch of 0x4149b0 without the orders (0x415869), which neither calls, cuts.
[[nodiscard]] Core::Expected<void, std::string> DrawThePetWindow(Ui::GameHudContext& context,
                                                                 Ui::PanelRegistry& panels);
} // namespace Client::Pets
