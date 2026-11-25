#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// Perfil de valores PC53/RN + conservación C0 observada + constantes de archivo (ADR 0012).
// C0 incluye menor o unordered: NaN puede devolver valores finitos. No acredita una sesión XP,
// status/ABI x87, payloads NaN ni equivalencia universal FPATAN/libm.
// Callers: Scenes/DarkSpirit (también MoveToward) y CharacterHead; fixtures world55/raven.txt,
// raven_safe_zone.txt y head.txt.
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

// 0x4e9810: gira `angles` hacia `target` desde `position` de a `step` grados: el rumbo (z) con 0x4e93f0 en
// el plano y 0x4e9560, y el cabeceo (x) con 360 menos 0x4e93f0 de (z, largo en el plano) a (z del objetivo,
// 0). No mueve `position`. Devuelve la distancia en la x87, sin guardar en float: la raíz de dz² más los
// cuadrados de dx y dy que guardó en float.
[[nodiscard]] double MoveToward(const glm::vec3& position, glm::vec3& angles, const glm::vec3& target, float step);
} // namespace Client::Render::Models
