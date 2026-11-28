#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::WorldObjects
{
struct ObjectDraw;
struct WorldObject;
} // namespace Client::WorldObjects
namespace Client::WorldObjects
{
struct GameObjectPass;
} // namespace Client::WorldObjects

namespace Client::MapScripts::Crywolf
{

// 0x430510 (0x602ed0 → 0x6044ed), with map 34 (0x4303c0), after the common rand() of 0x602ef4 (which it does not use):
// each object adds 1 to the weather count (0x723c0c) and, past 70000, a rand() resets it to 0 and leaves the weather
// (0x5763358) at rand() % 3. The 0x29 turns the glow 1° (0x723c10, which goes back to 0 at 360) and leaves two white
// 0x60f sprites of 1.3 over it, at a height of 350 plus the count of 0x6046a0 (0x5763538, or 40 minus it past 20): the
// first unrotated and the second with the glow's turn. The 0x54 takes a rand() and, at 0 modulo 3 with Crywolf's state
// at 1, releases the smoke 0x5f2/21 with light (1, 1, 1), its scale and no owner. Without the globals, the pool or the
// count, it cuts where the ASM does, with the earlier work already done.
[[nodiscard]] Core::Expected<void, std::string> StepCrywolfObjectFrame(const WorldObjects::WorldObject& object,
                                                                       const WorldObjects::GameObjectPass& pass);

// 0x4306a0, the last link of the chain of 0x6002e0, with map 34 and an object that 0x6002e0 draws (`draw`, the one
// from WorldObjects::DrawOf, in Body). With the 0x24, the model color at 0.4 and its RenderBody; with the 0xfa, at
// (0; 0; 0.9). The 0x29 and the 0x38 draw the body with the color they bring; the 0x39, 0x47, 0x48, 0x49, 0x4a, 0x4d,
// 0x4e and 0x51 leave without a body (Hidden). The others go on to 0x4339d0, which has nothing outside 0x171..0x1b4,
// and 0x6002e0 draws their body. With an original frame, also, each type's random draws, before the common rand() of
// 0x602ef4: the 0x38 with the state at 0, a rand() for the light (rand() % 10000 · 0.0001f) and, with another at 0
// modulo 3, effect 0x61d/3 (Effects::ModelEffects::CreateFadingDecal, with 4 at +0x8a); the 0x39 and the 0x47, with
// rand() % 3 at 0, the spark 0x6ec/5 (and the 0x39, the smoke 0x5f2/21) with light (1, 1, 1), its scale and no owner;
// the 0x48, with rand() % 10 at 0, another rand() for the light L = ((rand() & 3) + 3) · 0.1f and the smoke 0x5f2/33
// 50 higher, with light (L; L · 0.6f; L · 0.2f), its scale and no owner; the 0x4a, the smoke with the state at 1; the
// 0x4d and the 0x4e, with rand() % 6 at 0, the cloud 0x622/9 with its light; and the 0x49, with the weather at 0, the
// HUD sand (0x576353c) at 1. The 0x51 with the state at 2 leaves the PC53 RGB of 0x430a40..0x430ac1; the caller keeps
// the previous pose and runs 0x44→0x240 of mesh 0.
[[nodiscard]] Core::Expected<void, std::string> CrywolfObjectBody(const WorldObjects::WorldObject& object,
                                                                  WorldObjects::ObjectDraw& draw,
                                                                  const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::Crywolf
