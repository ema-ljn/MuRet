#pragma once

#include "Core/Expected.h"
#include "World/ItemState.h"

#include <cstdint>
#include <string>

// Los cuerpos de los tooltips de un ítem (06 § Los tooltips, CL8 hito 5): el del ítem (0x5b2aa0) y el de reparar
// (0x5b8150); el de la skill (0x5b90b0) está en SkillTooltip.h. Cada uno limpia sus renglones de HudState::tooltip, los
// llena y dibuja la caja (0x5af090). 0x5f7bc0 los llama con el registro propio abierto alrededor.
namespace Client::Scenes
{
struct GameHudContext;

struct ItemTooltipContext
{
    GameHudContext& hud;
    // 0x78a9bac y 0x78a9ba8: dónde va (el centro de arriba de la caja).
    std::int32_t x = 0;
    std::int32_t y = 0;
    // El ítem de 0x78abaa8: la casilla o el lugar del equipo.
    const World::ItemState& item;
    // 0x78aba27: el último argumento de 0x5c4110, o 0 desde el equipo.
    std::uint8_t grid = 0;
};

// 0x5b2aa0(x, y, ítem, grilla).
[[nodiscard]] Core::Expected<void, std::string> DrawTheItemTooltip(const ItemTooltipContext& context);

// 0x5b8150(x, y, ítem, grilla): con el modo de reparar, el nombre, la durabilidad y el precio de la reparación.
[[nodiscard]] Core::Expected<void, std::string> DrawTheRepairTooltip(const ItemTooltipContext& context);
} // namespace Client::Scenes
