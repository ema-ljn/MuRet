#pragma once

#include "Tables/TextTable.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Ui/Notices.h"

#include <cstdint>
#include <optional>

// The checks of the selection's delete button (0x63edf0; Client/docs/research/06-render-escenas-y-ui.md,
// "Borrar un personaje"). *Static*.
namespace Client::FrontEnd
{
// Whether the chosen server's group lets a high-level one be deleted: strcmp of its name
// (GroupNameText of code / 20, the one 0x4c1860 put at 0x7d36bb0 + 0x21e · DAT_006d5ec4) with
// text 559. A missing text counts as empty. Without a chosen server (MuRet without ConnectServer),
// yes: the original always goes through the list.
[[nodiscard]] bool FreeDeletionGroup(const Tables::TextTable* texts, std::optional<std::uint16_t> chosenServer);

// The notice the delete button opens with `chosen`, in this order: with a guild (+0x368 other than
// 0xff), 0x92; level 80 or more (0x6d5e24) outside the free group, 0x83; with flags
// 0x12, 0x61; otherwise, the confirmation 0x5e.
[[nodiscard]] Ui::NoticeId NoticeOfDeleteButton(const Protocol::FrontEnd::CharacterEntry& chosen,
                                                bool freeDeletionGroup);
} // namespace Client::FrontEnd
