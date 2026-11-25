#include "Data/Models/PlayerActions.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace Client::Data::Models
{
namespace
{
// Cada escritura de 0x60dbe0 en +4 de una acción de Player.bmd, con la dirección de la instrucción que
// escribe (la de un bucle, una vez). Cada float tiene los bits que escribe el original: 0.28f es
// 0x3E8F5C29, el inmediato de 0x60f326.
constexpr std::array<ActionSpeedWrite, 47> SpeedWrites = {{
    {1, 12, 0.28f},    // 0x60f349
    {3, 3, 0.26f},     // 0x60f35f
    {4, 4, 0.24f},     // 0x60f372
    {5, 5, 0.24f},     // 0x60f381
    {7, 7, 0.22f},     // 0x60f394
    {8, 8, 0.22f},     // 0x60f3a3
    {13, 33, 0.3f},    // 0x60f3c7
    {21, 21, 0.35f},   // 0x60f3dd
    {29, 29, 0.35f},   // 0x60f3ef
    {152, 194, 0.32f}, // 0x60f413
    {195, 196, 0.45f}, // 0x60f437
    {197, 204, 0.4f},  // 0x60f45b
    {194, 194, 0.4f},  // 0x60f46c
    {171, 171, 0.28f}, // 0x60f482
    {172, 172, 0.28f}, // 0x60f494
    {201, 201, 0.2f},  // 0x60f4aa
    {202, 202, 0.2f},  // 0x60f4bc
    {64, 64, 0.2f},    // 0x60f4ce
    {65, 65, 0.2f},    // 0x60f4e0
    {138, 138, 0.24f}, // 0x60f4f2
    {139, 139, 0.3f},  // 0x60f504
    {140, 140, 0.3f},  // 0x60f516
    {141, 141, 0.24f}, // 0x60f528
    {67, 67, 0.45f},   // 0x60f53a
    {72, 72, 0.3f},    // 0x60f598
    {73, 73, 0.3f},    // 0x60f5aa
    {74, 74, 0.3f},    // 0x60f5bc
    {75, 75, 0.3f},    // 0x60f5ce
    {76, 76, 0.2f},    // 0x60f5e0
    {77, 77, 0.28f},   // 0x60f5f7
    {78, 78, 0.3f},    // 0x60f609
    {79, 79, 0.3f},    // 0x60f61b
    {80, 80, 0.28f},   // 0x60f62d
    {81, 81, 0.3f},    // 0x60f63f
    {82, 82, 0.3f},    // 0x60f651
    {83, 83, 0.2f},    // 0x60f663
    {84, 84, 1.0f},    // 0x60f679
    {85, 85, 1.0f},    // 0x60f68b
    {86, 125, 0.45f},  // 0x60f6aa
    {106, 117, 0.71f}, // 0x60f6ce
    {118, 121, 0.4f},  // 0x60f6ee
    {93, 93, 0.3f},    // 0x60f6ff
    {88, 88, 0.3f},    // 0x60f710
    {90, 90, 0.3f},    // 0x60f721
    {89, 89, 0.28f},   // 0x60f732
    {91, 91, 0.3f},    // 0x60f743
    {137, 137, 0.3f},  // 0x60f754
}};
} // namespace

std::span<const ActionSpeedWrite> PlayerActionSpeeds()
{
    return SpeedWrites;
}

std::optional<float> PlayerActionSpeed(std::size_t action)
{
    std::optional<float> speed;
    for (const ActionSpeedWrite& write : SpeedWrites)
    {
        if (action >= write.first && action <= write.last)
        {
            speed = write.speed;
        }
    }
    return speed;
}
} // namespace Client::Data::Models
