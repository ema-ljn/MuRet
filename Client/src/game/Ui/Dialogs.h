#pragma once

#include <cstdint>

// The game's dialogs (06 § HUD y ventanas, "Los diálogos del juego"): 0x7d3a10c is the open one (0: none) and
// 0x7d3a110 the waiting one. 0x623e70 handles them each step and 0x62db60 draws them each frame (milestone 6).
namespace Client::Ui
{
// 0x623ea8
inline constexpr std::int32_t EscapeMenuDialog = 0x6e;
// 0x56b387: with it, SendMove does not set the hero walking.
inline constexpr std::int32_t DisconnectDialog = 0x70;
// 0x6236f0: forced even with another one open.
inline constexpr std::int32_t ForcedDialog = 0x78;
// 0x623f4d: Esc closes it like the menu.
inline constexpr std::int32_t OptionsDialog = 0x93;
// 0x659860 and 0x6599a0: the only dialog with which a panel can be opened.
inline constexpr std::int32_t PanelFriendlyDialog = 0x8a;
// 0x6236f0: with the 0x9a open, the 0x7f does not get in.
inline constexpr std::int32_t ChaosCastleDialog = 0x9a;
inline constexpr std::int32_t IgnoredOverChaosCastle = 0x7f;

struct DialogState
{
    // 0x7d3a10c, .bss 0.
    std::int32_t current = 0;
    // 0x7d3a110, .bss 0.
    std::int32_t pending = 0;
};

// 0x6236f0(id): 0 moves to the waiting one; 0x78 forces it; with one open, it waits; otherwise it opens. With the
// 0x9a open, the 0x7f does not get in.
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
} // namespace Client::Ui
