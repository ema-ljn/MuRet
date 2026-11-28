#pragma once

#include "Kernel/WorldRuntime.h"
#include "FrontEnd/OptionsWindow.h"

// The globals the options read and write (+0x7e8; CL8, milestone 8; 06 § Las opciones), in WorldRuntime: auto attack
// (0x6c8e3c, ui.autoAttack), the notice sound (0x77f3b78, chat.noticeSound), the banners (0x6c7a8c, chat.slidesOn),
// the volume (0x7d3a118, ui.volume) and the glow (0x6c9dcc, ui.glowDetail).
namespace Client::Hud
{
// 0x6446e0: what the options read when built.
[[nodiscard]] FrontEnd::OptionsSettings OptionsSettingsOf(const Kernel::WorldRuntime& runtime);

// 0x644730: `event`'s global. With the volume, the original then calls 0x632d80 (0x41eed0's DirectSound), from CL10
// (P-C79): the number stays.
void ApplyOption(Kernel::WorldRuntime& runtime, const FrontEnd::OptionsEvent& event);
} // namespace Client::Hud
