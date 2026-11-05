#include "Scenes/CharacterHead.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"

#include <glm/ext/vector_float2.hpp>

namespace Client::Scenes
{
namespace
{
constexpr int SortOneIn = 32;
constexpr int TurnSpan = 128;
constexpr int TurnCenter = 64;
constexpr int NodSpan = 32;
constexpr int NodCenter = 16;
constexpr double Turn = 360.0;                      // 0x6b869c
constexpr double Share = static_cast<double>(0.2f); // 0x6b8854

float F32(double value)
{
    return static_cast<float>(value);
}
} // namespace

void StepCharacterHead(CharacterHead& head, bool sorts, Render::Models::CrtRandom& random)
{
    // 0x5068fd: rand() & 0x8000001f con el arreglo del signo, el % 32 de C.
    if (sorts && random.Next() % SortOneIn == 0)
    {
        head.target.x = static_cast<float>((random.Next() % TurnSpan) - TurnCenter);
        head.target.y = static_cast<float>((random.Next() % NodSpan) - NodCenter);
        for (int k = 0; k < 2; ++k)
        {
            if (head.target[k] < 0.0f)
            {
                head.target[k] = F32(static_cast<double>(head.target[k]) + Turn);
            }
        }
    }
    for (int k = 0; k < 2; ++k)
    {
        const float step = F32(Render::Models::AngleDistance(head.angles[k], head.target[k], true) * Share);
        head.angles[k] = F32(Render::Models::TurnAngle(head.angles[k], head.target[k], step));
    }
}
} // namespace Client::Scenes
