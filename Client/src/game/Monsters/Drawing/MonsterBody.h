#pragma once

#include "Core/Expected.h"
#include "Monsters/Drawing/MonsterBodyPasses.h"

#include <string>

namespace Client::Monsters::Drawing
{
// 0x6002e0 from 0x600436, with +0x58 other than −2 and the fourth argument of 0x602e90 outside 10: the body of a
// monster or an NPC with its cases by type and by map, over the model 0x5ffce0 left (its color and its per-vertex
// light) and the meshes of 0x4ebc80. In order:
// - the colors of 0x171 in map 10, 0x164 in map 9 and 0x16d with the fourth argument (0x600436..0x6004a4);
// - type 0x16c (0x6004ed): with (0.4, 0.6, 1) and +0x88 at 0, RenderBody(2) with Chrome01 (0x5c0); with the color at 1
//   and +0x88 at 0xff, RenderBody(2), and it leaves without what comes after the body;
// - the colors of +0x78 & 1 and & 2 (0x600e69) and the light of 0x175, 0x161, 0x184 and 0x185 (0x600ec4);
// - the chain (0x438fe0, 0x42cc10, 0x4403b0, 0x42f170, 0x425d90 and 0x4306a0, MonsterBodyChain.h): the first that draws
//   goes straight to 0x602365;
// - the cases after the chain (0x600f74..0x60235c) or the common RenderBody(2) (0x60235e);
// - 0x602365: with +0x78 & 2, RenderBody(2) with blendMesh −2 and the light at 1; type 0x177 (0x602391) and types 0x1f3
//   and 0x1f4 (0x60243e).
// The cases of the types no monster spawn gives (MonsterKinds) stay cut with their address, and so does what is not
// ported: the edge class of 0x183 (0x6011f1). The per-vertex effects of 0x4ee770 (0x18e and 0x18f on death,
// Actors/Drawing/VertexEffects.h) come out only in the original's frames.
[[nodiscard]] Core::Expected<void, std::string> DrawMonsterBody(MonsterBodyFrame& frame);
} // namespace Client::Monsters::Drawing
