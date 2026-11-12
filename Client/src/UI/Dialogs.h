#pragma once

#include <cstdint>

// Los avisos del juego (06 § HUD y ventanas, "Los diálogos del juego"): 0x7d3a10c es el abierto (0: ninguno) y
// 0x7d3a110 el que espera. Los atiende 0x623e70 en cada paso y los dibuja 0x62db60 en cada frame (hito 6).
namespace Client::UI
{
// 0x623ea8: el menú de Esc.
inline constexpr std::int32_t EscapeMenuDialog = 0x6e;
// 0x56b387: la desconexión, con la que SendMove no pone a caminar al héroe.
inline constexpr std::int32_t DisconnectDialog = 0x70;
// 0x6236f0: el que se fuerza aunque haya otro abierto.
inline constexpr std::int32_t ForcedDialog = 0x78;
// 0x623f4d: las opciones, que Esc cierra como el menú.
inline constexpr std::int32_t OptionsDialog = 0x93;
// 0x659860 y 0x6599a0: el único aviso con el que se puede abrir un panel.
inline constexpr std::int32_t PanelFriendlyDialog = 0x8a;
// 0x6236f0: con el 0x9a abierto, el 0x7f no entra.
inline constexpr std::int32_t ChaosCastleDialog = 0x9a;
inline constexpr std::int32_t IgnoredOverChaosCastle = 0x7f;

struct DialogState
{
    // 0x7d3a10c, .bss 0.
    std::int32_t current = 0;
    // 0x7d3a110, .bss 0.
    std::int32_t pending = 0;
};

// 0x6236f0(id): con 0 pasa al que espera; con 0x78 lo fuerza; con uno abierto, lo deja esperando; si no, lo abre.
// Con el 0x9a abierto, el 0x7f no entra.
inline void PushDialog(DialogState& dialogs, std::int32_t id)
{
    if (dialogs.current == ChaosCastleDialog && id == IgnoredOverChaosCastle)
        return;
    if (id == 0)
    {
        dialogs.current = dialogs.pending;
        dialogs.pending = 0;
        return;
    }
    if (id == ForcedDialog)
    {
        dialogs.current = id;
        return;
    }
    if (dialogs.current != 0)
    {
        dialogs.pending = id;
        return;
    }
    dialogs.current = id;
}
} // namespace Client::UI
