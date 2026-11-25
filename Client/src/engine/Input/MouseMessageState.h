#pragma once

// Los flags del WndProc del juego: WM_LBUTTONDBLCLK sustituye al segundo WM_LBUTTONDOWN y sólo prende doubleClick
// (0x4b47b6). Los flancos físicos de InputState siguen disponibles para la selección de personajes (0x640940).
namespace Client::Input
{
struct MouseMessageState
{
    bool leftDown = false;
    bool leftPress = false;
    bool leftRelease = false;
    // Valor del último mensaje, hasta otro mensaje. El juego toma su actualización una sola vez con
    // TakeDoubleClickMessage y conserva después el estado que dejen sus consumidores.
    bool doubleClick = false;
};
} // namespace Client::Input
