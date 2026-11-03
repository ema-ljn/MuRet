#pragma once

#include "Quaternion.h"

#include <cstdint>

// Misión de la CL6.0 (docs/learning/ola-cl6.0-huesos-y-cuaterniones.md, actividad 6): el slerp
// del original, 0x5fce90, el QuaternionSlerp de Half-Life, y la igualdad de 0x5fc700, que decide
// si la pose lo llama. No es el de glm (actividad 7).
namespace Labs
{
// Por qué camino sale el slerp de 0x5fce90.
enum class SlerpCase : std::uint8_t
{
    // Con senos: sin((1 − t)·ω)/sin ω y sin(t·ω)/sin ω, con ω = acos(cos).
    Sines,
    // Lineal, 1 − t y t, cuando 1 − cos ≤ 1e-8 (el double de 0x6ba728), con el cos en float.
    Linear,
    // p y q casi opuestos: 1 + cos ≤ 1e-8. Arma un cuaternión perpendicular a p y gira hacia él.
    Opposite,
};

// La rama que toma HalfLifeSlerp(p, q, t), sin calcular nada más.
[[nodiscard]] SlerpCase SlerpBranch(Quat p, Quat q);

// Lo mismo desde el cos como queda en la pila de la x87, antes del FST de 0x5fcf10: la rama de los
// opuestos lo mira así, y la lineal, redondeado a float.
[[nodiscard]] SlerpCase SlerpBranchForCosine(double cosine);

// 0x5fce90(p, q, t, qt): da vuelta q si Σ(p − q)² > Σ(p + q)² (en el original, el q del llamador;
// acá, la copia) y después interpola por la rama de SlerpBranch. No normaliza.
[[nodiscard]] Quat HalfLifeSlerp(Quat p, Quat q, float t);

// 0x5fc700: las cuatro componentes a 0,001 o menos (el double de 0x6b8a88). Con dos keys así, la
// pose de 0x4eb860 copia la primera y no llama al slerp.
[[nodiscard]] bool AlmostEqual(Quat a, Quat b);
} // namespace Labs
