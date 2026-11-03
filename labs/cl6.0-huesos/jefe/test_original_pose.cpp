#include "Bones.h"
#include "Quaternion.h"
#include "Slerp.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

// Jefe de la CL6.0 (docs/learning/ola-cl6.0-huesos-y-cuaterniones.md, actividad 11): entre dos
// keys, y en qué key está el original.
//
// El original posa cada hueso entre dos keys con el slerp de 0x5fce90 (0x4eb860), y avanza la
// animación por paso (0x4ec200). Dos preguntas que la traza del 06 no contesta sola: qué se ve si
// en vez del slerp se interpolan los ángulos o se hace un lerp normalizado, y por qué los frames de
// la tabla del 06 son uno menos que los del original.
namespace
{
using Labs::AlmostEqual;
using Labs::AngleQuaternion;
using Labs::HalfLifeSlerp;
using Labs::Quat;
using Labs::QuaternionMatrix;
using Labs::Transform;
using Labs::Vec3;

constexpr float Pi = std::numbers::pi_v<float>;

constexpr float Radians(float degrees)
{
    return degrees * Pi / 180;
}

Vec3 Radians(Vec3 degrees)
{
    return Vec3{Radians(degrees[0]), Radians(degrees[1]), Radians(degrees[2])};
}

// Un vértice llevado solo por el giro: el hueso en el origen.
Vec3 Turn(Quat rotation, Vec3 vertex)
{
    return Transform(QuaternionMatrix(rotation), vertex);
}

float Distance(const Vec3& a, const Vec3& b)
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Lo que se puede hacer en vez del slerp: los ángulos interpolados y pasados a cuaternión, o el
// lerp de los cuaterniones (dando vuelta q como el slerp) normalizado.
Quat EulerLerp(Vec3 from, Vec3 to, float t)
{
    return AngleQuaternion(
        Vec3{from[0] + (to[0] - from[0]) * t, from[1] + (to[1] - from[1]) * t, from[2] + (to[2] - from[2]) * t});
}

Quat Nlerp(Quat p, Quat q, float t)
{
    const float dot = p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w;
    const float sign = dot < 0 ? -1.0f : 1.0f;
    const Quat mixed{.x = p.x + (sign * q.x - p.x) * t,
                     .y = p.y + (sign * q.y - p.y) * t,
                     .z = p.z + (sign * q.z - p.z) * t,
                     .w = p.w + (sign * q.w - p.w) * t};
    const float length = std::sqrt(mixed.x * mixed.x + mixed.y * mixed.y + mixed.z * mixed.z + mixed.w * mixed.w);
    return Quat{.x = mixed.x / length, .y = mixed.y / length, .z = mixed.z / length, .w = mixed.w / length};
}

// La distancia más grande de un vértice entre dos maneras de interpolar, de t = 0 a t = 1, cada
// 0,01.
struct Gaps
{
    float nlerp = 0;
    float euler = 0;
};

Gaps LargestGaps(Vec3 fromDegrees, Vec3 toDegrees, Vec3 vertex)
{
    const Vec3 from = Radians(fromDegrees);
    const Vec3 to = Radians(toDegrees);
    const Quat p = AngleQuaternion(from);
    const Quat q = AngleQuaternion(to);
    Gaps gaps;
    for (int step = 0; step <= 100; ++step)
    {
        const float t = static_cast<float>(step) / 100;
        const Vec3 slerped = Turn(HalfLifeSlerp(p, q, t), vertex);
        gaps.nlerp = std::max(gaps.nlerp, Distance(Turn(Nlerp(p, q, t), vertex), slerped));
        gaps.euler = std::max(gaps.euler, Distance(Turn(EulerLerp(from, to, t), vertex), slerped));
    }
    return gaps;
}

// Dos keys a 20° alrededor de z, de 170° a −170° (190°). A mitad de camino, el slerp está en 180°,
// por el camino corto. Los ángulos interpolados dan 0°: dan la vuelta larga, por 90° y 0°.
TEST(OriginalPoseTest, SlerpBeatsEulerBetweenTwoKeys)
{
    const Vec3 from = Radians(Vec3{0, 0, 170});
    const Vec3 to = Radians(Vec3{0, 0, -170});
    const Quat p = AngleQuaternion(from);
    const Quat q = AngleQuaternion(to);
    const Vec3 tip{1, 0, 0};

    const Vec3 slerped = Turn(HalfLifeSlerp(p, q, 0.5f), tip);
    EXPECT_NEAR(slerped[0], -1, 1e-6f);
    EXPECT_NEAR(slerped[1], 0, 1e-6f);
    const Vec3 lerped = Turn(EulerLerp(from, to, 0.5f), tip);
    EXPECT_NEAR(lerped[0], 1, 1e-6f);
    EXPECT_NEAR(lerped[1], 0, 1e-6f);

    // A un cuarto: el slerp en 175°, los ángulos en 85°. A 90° uno del otro, el vértice de la
    // punta queda a √2.
    const Vec3 quarter = Turn(HalfLifeSlerp(p, q, 0.25f), tip);
    EXPECT_NEAR(quarter[0], std::cos(Radians(175)), 1e-6f);
    EXPECT_NEAR(quarter[1], std::sin(Radians(175)), 1e-6f);
    EXPECT_NEAR(Distance(quarter, Turn(EulerLerp(from, to, 0.25f), tip)), std::sqrt(2.0f), 1e-5f);
}

// Por qué el 05 dice que el slerp y el lerp normalizado no se distinguen en la traza, y los
// ángulos interpolados sí. Entre dos keys de una animación (acá, 6,9° de diferencia en los tres
// ejes), un vértice a 50 del hueso se aparta del slerp en milésimas con el lerp normalizado, por
// debajo del 0,015 del ajuste de la selección (06); con los ángulos, en centésimas, por arriba.
// Con keys más lejanas (33°), los dos crecen y el lerp normalizado ya pasa el 0,015, pero los
// ángulos siguen más de 40 veces peor.
TEST(OriginalPoseTest, NlerpHidesBelowTheTraceErrorAndEulerDoesNot)
{
    constexpr float TraceError = 0.015f;
    const Vec3 vertex{50, 0, 0};

    const Gaps close = LargestGaps(Vec3{10, 20, 30}, Vec3{12, 24, 36}, vertex);
    EXPECT_LT(close.nlerp, 0.001f);
    EXPECT_GT(close.euler, 2 * TraceError);

    const Gaps far = LargestGaps(Vec3{30, 40, 50}, Vec3{50, 20, 80}, vertex);
    EXPECT_GT(far.nlerp, TraceError);
    EXPECT_GT(far.euler, 40 * far.nlerp);
    EXPECT_GT(far.euler, 1.5f);
}

// Las keys de un hueso en una acción de 7, como la 3 de la selección: un giro que cambia en x y en
// z de key en key.
std::vector<Quat> SevenKeys()
{
    std::vector<Quat> keys;
    for (int key = 0; key < 7; ++key)
    {
        const auto k = static_cast<float>(key);
        keys.push_back(AngleQuaternion(Vec3{0.1f * k, -0.05f * k, 0.35f * k}));
    }
    return keys;
}

// La lectura de la tabla del 06: el frame f está entre la key ⌊f⌋ y la siguiente, con el peso de
// la parte fraccionaria.
Quat TablePose(const std::vector<Quat>& keys, float frame)
{
    const auto now = static_cast<std::size_t>(frame);
    const std::size_t next = (now + 1) % keys.size();
    return HalfLifeSlerp(keys[now], keys[next], frame - static_cast<float>(now));
}

// El estado del original para un hueso: el frame de la acción y la key anterior que guardó el
// objeto (0x4ec200 la escribe cuando cambia la parte entera del frame).
struct OriginalAnimation
{
    float frame = 0;
    float priorFrame = 0;

    // 0x4ec200 con una acción que repite y sin lock_positions: suma la velocidad, guarda la key
    // vieja si cruzó a otra y, al pasar la última, vuelve con el módulo de la parte entera.
    void Step(float speed, int keys)
    {
        const int before = static_cast<int>(frame);
        frame += speed;
        if (static_cast<int>(frame) != before)
        {
            priorFrame = static_cast<float>(before);
        }
        if (static_cast<float>(keys) <= frame)
        {
            const int whole = static_cast<int>(frame);
            frame = (frame - static_cast<float>(whole)) + static_cast<float>(whole % keys);
        }
    }
};

// 0x4eb860 para un hueso: de la key anterior a la de ahora, ⌊frame⌋, con la parte fraccionaria del
// frame. Si las dos son casi iguales (0x5fc700), la anterior.
Quat OriginalPose(const std::vector<Quat>& keys, const OriginalAnimation& animation)
{
    const auto now = static_cast<std::size_t>(animation.frame);
    const auto prior = static_cast<std::size_t>(std::max(0, static_cast<int>(animation.priorFrame)));
    const Quat& from = keys[prior < keys.size() ? prior : 0];
    const Quat& to = keys[now < keys.size() ? now : 0];
    if (AlmostEqual(from, to))
    {
        return from;
    }
    return HalfLifeSlerp(from, to, animation.frame - static_cast<float>(now));
}

// Los cinco frames del personaje de la selección en la tabla del 06.
constexpr std::array<float, 5> TableFrames{1.737f, 3.320f, 4.896f, 6.739f, 1.578f};

// El original guarda f + 1 (módulo las keys) y ⌊f⌋ como anterior, y posa entre ⌊f⌋ y ⌊f⌋ + 1: la
// misma pose que la tabla. El frame f + 1 leído como la tabla es la pose de una key más adelante.
TEST(OriginalPoseTest, TheOriginalStoresTheNextKey)
{
    const std::vector<Quat> keys = SevenKeys();
    for (const float f : TableFrames)
    {
        SCOPED_TRACE(f);
        const OriginalAnimation original{.frame = std::fmod(f + 1, 7.0f), .priorFrame = std::floor(f)};
        const Quat byTheOriginal = OriginalPose(keys, original);
        const Quat byTheTable = TablePose(keys, f);
        EXPECT_NEAR(byTheOriginal.x, byTheTable.x, 1e-6f);
        EXPECT_NEAR(byTheOriginal.y, byTheTable.y, 1e-6f);
        EXPECT_NEAR(byTheOriginal.z, byTheTable.z, 1e-6f);
        EXPECT_NEAR(byTheOriginal.w, byTheTable.w, 1e-6f);

        const Vec3 tip{50, 0, 0};
        const Vec3 oneKeyAhead = Turn(TablePose(keys, std::fmod(f + 1, 7.0f)), tip);
        EXPECT_GT(Distance(oneKeyAhead, Turn(byTheTable, tip)), 10);
    }
}

// Los saltos del 06 son pasos de 0,26, la velocidad de la acción 3: desde el primer frame de la
// tabla, 33, 33, 34 y 34 pasos caen en los otros cuatro (a menos de 0,005: la tabla tiene tres
// decimales y salió de un ajuste).
TEST(OriginalPoseTest, TheJumpsAreStepsOfTheAction)
{
    constexpr float Speed = 0.26f;
    constexpr int Keys = 7;
    OriginalAnimation animation{.frame = TableFrames[0] + 1, .priorFrame = std::floor(TableFrames[0])};
    constexpr std::array<int, 4> Steps{33, 33, 34, 34};
    for (std::size_t swap = 0; swap < Steps.size(); ++swap)
    {
        SCOPED_TRACE(swap);
        for (int step = 0; step < Steps[swap]; ++step)
        {
            animation.Step(Speed, Keys);
        }
        const float tableFrame = std::fmod(animation.frame - 1 + Keys, static_cast<float>(Keys));
        EXPECT_NEAR(tableFrame, TableFrames[swap + 1], 0.005f);
        // La anterior es la key de antes de la de ahora: ⌊frame⌋ − 1, módulo las keys.
        EXPECT_EQ(static_cast<int>(animation.priorFrame), (static_cast<int>(animation.frame) + Keys - 1) % Keys);
    }
}
} // namespace
