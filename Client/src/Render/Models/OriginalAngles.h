#pragma once

#include <cstdint>

// Perfil de valores PC53/RN + conservación C0 observada + constantes de archivo (ADR 0012).
// C0 incluye menor o unordered: NaN puede devolver valores finitos. No acredita una sesión XP,
// status/ABI x87, payloads NaN ni equivalencia universal FPATAN/libm.
namespace Client::Render::Models
{
// 0x4e93f0: el rumbo de (x1, y1) a (x2, y2), en grados, como lo deja en la pila de la x87.
[[nodiscard]] double CreateAngle(float x1, float y1, float x2, float y2);

// 0x4e9560: giro de `from` hacia `to` según sus ramas asimétricas. Conserva el signo de `step`;
// un paso negativo puede alejarse del objetivo. Distingue P derramado de Q retenido.
[[nodiscard]] double TurnAngle(float from, float to, float step);

// 0x4e96f0: de `from` a `to`, con una suma de 360 si son negativos (y guardados en float), la resta
// `to − from`, o la vuelta por el otro lado si queda a más de media vuelta (360 − to + from, o 360 − from
// + to, con ramas que no son simétricas: con `from` en 180 o más mira `from − 180 > to`, y con menos, `from
// + 180 ≤ to`). Con byte `mode` exactamente 1, niega el resultado negativo.
[[nodiscard]] double AngleDistance(float from, float to, std::uint8_t mode);
} // namespace Client::Render::Models
