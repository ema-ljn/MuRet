#pragma once

#include "Ui/OldWindowBase.h"

#include <array>
#include <cstdint>

// [0x7d536fc], constructor 0x661090. Abierto y pestaña tienen un único dueño en DelegatedPanels; los registros,
// selecciones, impuestos y tesoro viven en CastleData. Acá sólo los campos propios restantes.
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
