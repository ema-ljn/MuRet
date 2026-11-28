#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::MapScripts::Aida
{
// 0x425260 (0x604535), with map 33 or 54, after the common rand() of 0x602ef4 (which it does not use): the 0x5c6
// sprites of bones 6, 7, 8, 12, 13 and 17 of the 0x1e and the 0x47; the cascades 0x6f0/2 (0x38, rand() & 1), 0x6f2/4
// (0x39) and 0x6f1/2 (0x3a, rand() % 3); the twenty owned mists 0x622/1 of the 0x3b, the 0x3e and the 0x3f while +0x58
// is not −2; the butterfly 0xae of the 0x3c with timeGetTime() & 0x3ff < 10, which then leaves the object at −2; the
// light 0.3 and the flame 0x5de/7 of the 0x43; and the 0x46, which with rand() % 5 at 1 and timeGetTime() & 0x3ff < 30
// creates effect 0xb3 (Effects::ModelEffects::CreateAidaWisp). The 0x29 and the 0x41/0x42 only draw
// (DrawAidaMeshPass) and do nothing here. A missing pool or clock cuts where the ASM does, keeping what was already
// created.
[[nodiscard]] Core::Expected<void, std::string> StepAidaObjectFrame(Kernel::WorldObject& object,
                                                                    const Kernel::GameObjectPass& pass);
} // namespace Client::MapScripts::Aida
