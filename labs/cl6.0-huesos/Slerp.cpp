#include "Slerp.h"

#include "Quaternion.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace Labs
{
namespace
{
// Los doubles del original: 1 en 0x6b8d60, 1e-8 en 0x6ba728 y 0,001 en 0x6b8a88. El de 0x6ba720
// es π/2 redondeado a float y guardado en double (los bits 0x3ff921fb60000000).
constexpr double One = 1.0;
constexpr double Epsilon = 1e-8;
constexpr double QuarterTurn = 1.5707963705062866;
constexpr double Tolerance = 0.001;

using Components = std::array<float, 4>;

Components ToComponents(Quat q)
{
    return Components{q.x, q.y, q.z, q.w};
}

Quat FromComponents(const Components& c)
{
    return Quat{.x = c[0], .y = c[1], .z = c[2], .w = c[3]};
}

// El lazo de 0x5fceb2: las dos sumas quedan en la pila de la x87, sin redondear a float, y se
// comparan ahí. Σ(p + q)² − Σ(p − q)² es 4 · p·q: q se da vuelta cuando el producto es negativo.
Components FlipTowards(const Components& p, Components q)
{
    double minus = 0;
    double plus = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double difference = static_cast<double>(p[i]) - q[i];
        minus += difference * difference;
        const double sum = static_cast<double>(q[i]) + p[i];
        plus += sum * sum;
    }
    if (minus > plus)
    {
        for (float& component : q)
        {
            component = -component;
        }
    }
    return q;
}

// 0x5fcef4: el producto en el orden del binario, z, y, w y x, también en la pila.
double Dot(const Components& p, const Components& q)
{
    const double zy = static_cast<double>(q[2]) * p[2] + static_cast<double>(q[1]) * p[1];
    return zy + static_cast<double>(q[3]) * p[3] + static_cast<double>(p[0]) * q[0];
}

// Las dos comparaciones de 0x5fcf13 y 0x5fcf26. La primera usa el cos que quedó en la pila; la
// segunda lo vuelve a cargar del float que guardó el FST de 0x5fcf10.
SlerpCase Branch(double cosine)
{
    if (!(cosine + One > Epsilon))
    {
        return SlerpCase::Opposite;
    }
    const auto rounded = static_cast<double>(static_cast<float>(cosine));
    return One - rounded > Epsilon ? SlerpCase::Sines : SlerpCase::Linear;
}
} // namespace

SlerpCase SlerpBranchForCosine(double cosine)
{
    return Branch(cosine);
}

SlerpCase SlerpBranch(Quat p, Quat q)
{
    const Components first = ToComponents(p);
    return Branch(Dot(first, FlipTowards(first, ToComponents(q))));
}

Quat HalfLifeSlerp(Quat p, Quat q, float t)
{
    const Components first = ToComponents(p);
    const Components second = FlipTowards(first, ToComponents(q));
    const double cosine = Dot(first, second);
    const SlerpCase branch = Branch(cosine);

    if (branch == SlerpCase::Opposite)
    {
        // 0x5fcf9f: un cuaternión perpendicular a p, y de p hacia él con los senos de π/2. El lazo
        // es de tres: la w se queda en p.z, como en Half-Life.
        Components result{-first[1], first[0], -first[3], first[2]};
        const double fromP = std::sin((One - t) * QuarterTurn);
        const double toResult = std::sin(t * QuarterTurn);
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = static_cast<float>(fromP * first[i] + toResult * result[i]);
        }
        return FromComponents(result);
    }

    double fromP = One - t;
    double toQ = t;
    if (branch == SlerpCase::Sines)
    {
        // 0x5fcf3c: acos del cos en float; ω se guarda en float (FST) y su seno sale del ω que
        // quedó en la pila, también guardado en float. Los dos pesos quedan en la pila.
        const double omega = std::acos(static_cast<double>(static_cast<float>(cosine)));
        const auto roundedOmega = static_cast<double>(static_cast<float>(omega));
        const auto sinOmega = static_cast<double>(static_cast<float>(std::sin(omega)));
        fromP = std::sin((One - t) * roundedOmega) / sinOmega;
        toQ = std::sin(roundedOmega * t) / sinOmega;
    }

    Components result{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        result[i] = static_cast<float>(fromP * first[i] + toQ * second[i]);
    }
    return FromComponents(result);
}

bool AlmostEqual(Quat a, Quat b)
{
    const Components first = ToComponents(a);
    const Components second = ToComponents(b);
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (std::fabs(static_cast<double>(first[i]) - second[i]) > Tolerance)
        {
            return false;
        }
    }
    return true;
}
} // namespace Labs
