#pragma once

#include "Core/Expected.h"
#include "Scenes/MonsterBodyPasses.h"

#include <string>

namespace Client::Scenes
{
// La cadena de 0x6002e0 (0x600ef4..0x600f6e) con un monstruo o un NPC: 0x438fe0 (y su 0x43ed20), 0x42cc10 (y su
// 0x42e3d0), 0x4403b0 con el cuarto argumento distinto de 0 (y su 0x442e10), 0x42f170 (siempre 0), 0x425d90 con
// lo mismo (y su 0x428df0) y 0x4306a0 con el cuarto argumento (y su 0x4339d0). true si una dibujó lo suyo: 0x6002e0
// salta a 0x602365 sin sus casos ni su RenderBody. Los tipos de objeto del mundo que no da ninguna alta de monstruo
// no llegan acá (DrawMonsterBody los corta antes).
[[nodiscard]] Core::Expected<bool, std::string> DrawTheBodyChain(MonsterBodyFrame& frame);
} // namespace Client::Scenes
