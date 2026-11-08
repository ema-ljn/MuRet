#pragma once

#include <cstdint>

// Perfil de valores PC53/RN + conservación C0 observada + constantes de archivo (ADR 0012).
// C0 incluye menor o unordered: NaN puede devolver valores finitos. No acredita una sesión XP,
// status/ABI x87, payloads NaN ni equivalencia universal FPATAN/libm.
// Callers: Scenes/DarkSpirit y CharacterHead; fixtures world55/raven.txt y head.txt.
namespace Client::Render::Models
{
// 0x4e93f0: el rumbo de (x1, y1) a (x2, y2), en grados, como lo deja en la pila de la x87.
[[nodiscard]] double CreateAngle(float x1, float y1, float x2, float y2);

// 0x4e9560: giro de `from` hacia `to` según sus ramas asimétricas. Conserva el signo de `step`;
// un paso negativo puede alejarse del objetivo. Distingue P derramado de Q retenido.
[[nodiscard]] double TurnAngle(float from, float to, float step);

// 0x4e96f0: una suma de360 y store f32 si C0 (negativo o unordered), sin normalización completa.
// De base devuelve to-from. Con from<180 o unordered usa (360-to)+from si from+180 no es mayor
// ordenado que to; con from>=180 usa (360-from)+to sólo si from-180>to. Son ramas asimétricas.
// El byte mode exactamente1 niega el resultado menor que cero o unordered.
[[nodiscard]] double AngleDistance(float from, float to, std::uint8_t mode);
} // namespace Client::Render::Models
