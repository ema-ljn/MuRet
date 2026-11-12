#pragma once

#include "App/WindowState.h"

#include <string_view>

// Los textos sueltos de la pasada 2D del juego por 0x554180(x, y, texto, 0, 0, …) (06 § Texto): con la fuente que haya
// en el DC (0x57634f4) y los colores de 0x6c8e5c y 0x6c8e64, y el tamaño en 640 × 480 en 0x77f31b8 y 0x77f31bc.
namespace Client::Scenes
{
class ITextDrawer;
struct HudState;

// 0x554180(x, y, texto, 0, 0, …): el texto en (x, y) de 640 × 480 si no está vacío (0x465950 con el modo 1), y su
// tamaño, aun vacío, pasado a 640 × 480 (Render::Text::DesignX y DesignY) en hud.arrowsTextWidth y arrowsTextHeight.
void DrawHudLine(ITextDrawer& text, App::PixelSize screen, HudState& hud, int x, int y, std::string_view line);

// 0x623690(x, y, texto): GetTextExtentPointA con la fuente del DC y 0x554180 en x − (cx · 640 / W) / 2
// (Render::Text::CenteredAt).
void DrawHudCentered(ITextDrawer& text, App::PixelSize screen, HudState& hud, int x, int y, std::string_view line);
} // namespace Client::Scenes
