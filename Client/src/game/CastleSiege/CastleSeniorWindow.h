#pragma once

#include "Ui/OldWindowBase.h"

#include <array>
#include <cstdint>

// [0x7d536fc], constructor 0x661090. Open and tab have a single owner in DelegatedPanels; the records, selections,
// taxes and treasury live in CastleData. Here only the remaining own fields.
namespace Client::CastleSiege
{
struct CastleSeniorWindow
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    bool closeWith87 = true;
    std::uint32_t dialogId = 0;
    std::array<Ui::OldButton, 7> buttons = DefaultButtons();

    [[nodiscard]] static std::array<Ui::OldButton, 7> DefaultButtons();
};
} // namespace Client::CastleSiege
