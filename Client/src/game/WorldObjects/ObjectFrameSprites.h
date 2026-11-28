#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Models
{
struct Skeleton;
}
namespace Client::WorldObjects
{
// 0x602ed0 with map 0's 0x38 (MerchantAnimal01) (0x603149): two 0x5ac sprites on bones 48 and 57, with scale L · 5 and
// light (L · 0.6; L · 0.3; L · 0.1), with L = (common rand() % 30 + 70) · 0.01f. The offset it passes to 0x4ec110 is
// the uninitialized stack ([ebp − 0x28], Q-C256): with the object posed, 0x6002e0's frame stayed there (the try level
// at −1 in y), and both come out as NaN. Unposed, what 0x5ffdf0 left is not read and it stops.
[[nodiscard]] Core::Expected<void, std::string> StepMerchantAnimalFrame(const Kernel::WorldObject& object,
                                                                        int commonRandom,
                                                                        const Kernel::GameObjectPass& pass, bool posed);

// 0x602ed0 with map 2 (0x6031e5):
// - the 0x64 (0x603251): two 0x5d5 sprites of 2.5 at (0; 0; 150) on bone 0 of the global pose, with light (L; L; L)
//   and turn r and −r (r = ftol(clock · 0.1f) % 360);
// - the 0x67 (0x6031f7): with the model's frame (+0xa8) at the last key of the object's action (+0x111), one rand():
//   & 31 at 0 sets action 1 with 0x4ea030, otherwise 0. The +0xa8 is the one 0x4eb860 left when posing this object in
//   0x5ffdf0: (short)ftol(frame), at 0 if negative or not a key of the model's action (+0xa0). Unposed, it is another
//   drawing's of that model and it stops; without actions, 0x4eb860 did not write it, and with the action outside the
//   table 0x603205 reads past it: they stop too.
// A type that is neither does nothing.
[[nodiscard]] Core::Expected<void, std::string> StepDeviasObjectFrame(Kernel::WorldObject& object, int commonRandom,
                                                                      const Kernel::GameObjectPass& pass,
                                                                      const Models::Skeleton* skeleton, bool posed);

// 0x602ed0 with map 4 (0x603799): the 0x13 and the 0x14 leave two sprites (turn r and −r, with r = ftol(clock · 0.1f)
// % 360) on bones 15 and 19 (0.3) and 21 (1.5): 0x61e with (L; L · 0.2; 0) the 0x13 and 0x5d5 with (L · 0.4; L · 0.8;
// L) the 0x14; the 0x28, two 0x5d5 of 2.5 over the object, 260 higher, with (L; L; L). Then all fall to map 6's case
// (StepArenaObjectFrame).
[[nodiscard]] Core::Expected<void, std::string> StepLostTowerObjectFrame(const Kernel::WorldObject& object,
                                                                         int commonRandom,
                                                                         const Kernel::GameObjectPass& pass,
                                                                         bool posed);

// 0x602ed0 with map 6 (0x6039cb), which the 4 also falls to: the 9 leaves a 0x5ac sprite with scale L · 5 and light (L
// · 0.6; L · 0.3; L · 0.1) on bone 1 with the uninitialized stack offset, like map 0's 0x38: NaN with the object
// posed, a stop unposed.
[[nodiscard]] Core::Expected<void, std::string> StepArenaObjectFrame(const Kernel::WorldObject& object,
                                                                     int commonRandom,
                                                                     const Kernel::GameObjectPass& pass, bool posed);
} // namespace Client::WorldObjects
