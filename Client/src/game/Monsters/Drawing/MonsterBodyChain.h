#pragma once

#include "Core/Expected.h"
#include "Monsters/Drawing/MonsterBodyPasses.h"

#include <string>

namespace Client::Monsters::Drawing
{
// The chain of 0x6002e0 (0x600ef4..0x600f6e) with a monster or an NPC: 0x438fe0 (and its 0x43ed20), 0x42cc10 (and its
// 0x42e3d0), 0x4403b0 with the fourth argument other than 0 (and its 0x442e10), 0x42f170 (always 0), 0x425d90 with the
// same (and its 0x428df0) and 0x4306a0 with the fourth argument (and its 0x4339d0). true if one drew its own: 0x6002e0
// jumps to 0x602365 without its cases or its RenderBody. The world object types no monster spawn gives do not get here
// (DrawMonsterBody cuts them before).
[[nodiscard]] Core::Expected<bool, std::string> DrawTheBodyChain(MonsterBodyFrame& frame);
} // namespace Client::Monsters::Drawing
