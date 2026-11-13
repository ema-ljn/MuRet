#pragma once

#include "Scenes/WorldRuntime.h"
#include "UI/OptionsWindow.h"

// Los globales que leen y escriben las opciones (+0x7e8; CL8, hito 8; 06 § Las opciones), en WorldRuntime:
// el ataque automático (0x6c8e3c, ui.autoAttack), el sonido de los avisos (0x77f3b78, chat.noticeSound), los
// carteles (0x6c7a8c, chat.slidesOn), el volumen (0x7d3a118, ui.volume) y el brillo (0x6c9dcc, ui.glowDetail).
namespace Client::Scenes
{
// 0x6446e0: lo que las opciones leen al armarse.
[[nodiscard]] UI::OptionsSettings OptionsSettingsOf(const WorldRuntime& runtime);

// 0x644730: el global de `event`. Con el volumen, el original llama después a 0x632d80 (el DirectSound de
// 0x41eed0), de la CL10 (P-C79): queda el número.
void ApplyOption(WorldRuntime& runtime, const UI::OptionsEvent& event);
} // namespace Client::Scenes
