#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <optional>
#include <span>

// El picking de los personajes: el segmento del mouse (Render::Pass3D::RayThrough, 0x620480) contra
// la caja de cada uno. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El picking", y
// Q-C100. Todo *estático*.
namespace Client::Render::Models
{
// Una caja alineada a los ejes, en el mundo.
struct Box
{
    glm::vec3 min{};
    glm::vec3 max{};
};

// La caja del 0x20a desde su posición (0x511290, 0x512270 y 0x5122d4): de (−60, −60, 0) a
// (40, 40, 120), corrida y sin girar con el personaje (Q-C100).
inline constexpr glm::vec3 PlayerBoxMin{-60.0f, -60.0f, 0.0f};
inline constexpr glm::vec3 PlayerBoxMax{40.0f, 40.0f, 120.0f};
[[nodiscard]] Box PlayerBoxAt(const glm::vec3& position);

// 0x622900: si el segmento de `origin` a `end` toca la caja, por separación en 6 ejes: los 3 de la
// caja y su producto con el segmento (0x6227b0). Un borde que apenas se toca, cuenta.
[[nodiscard]] bool SegmentHitsBox(const glm::vec3& origin, const glm::vec3& end, const Box& box);

// 0x59e700: de los personajes en `positions` cuya caja de jugador toca el segmento, el de menor
// distancia² de `origin` (la cámara) a su posición, no al punto donde lo toca. std::nullopt si no
// toca ninguno. Con dos a la misma distancia, el primero.
[[nodiscard]] std::optional<std::size_t> NearestHit(const glm::vec3& origin, const glm::vec3& end,
                                                    std::span<const glm::vec3> positions);
} // namespace Client::Render::Models
