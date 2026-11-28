#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Los sprites de un frame (CL7.6 B1): la tabla de 0x5491e0 (0x760f990, 1.000 lugares de 0x1d4 bytes),
// que llenan el dibujo de los personajes y de los objetos y que 0x549440(0) dibuja y apaga en el mismo
// frame. Contrato: 06 § Los efectos del login.
namespace Client::World
{
inline constexpr std::size_t SpriteCount = 1000;
// La textura con su atlas de 3 × 3 (0x549290): su +4 es el cuadro, no la mezcla, y va con la normal.
inline constexpr std::uint16_t AtlasSprite = 0x637;

struct Sprite
{
    bool alive = false;        // +0x0
    std::uint16_t texture = 0; // +0x2
    std::int32_t blend = 0;    // +0x4: 0 aditiva, 1 0x620b10, 2 la normal, 3 0x620b90 (o el cuadro del atlas)
    float scale = 0.0f;        // +0xc
    glm::vec3 position{};      // +0x10
    glm::vec3 light{};         // +0xf4
    float rotation = 0.0f;     // +0x24, en grados
    float alpha = 1.0f;        // +0x114
    bool seen = false;         // +0x16c
};

class FrameSprites
{
public:
    // 0x5491e0(textura, posición, escala, luz, dueño, rotación, mezcla) en el primer lugar sin el byte
    // vivo, con el alpha en 1. Con los 1.000 llenos, no se crea.
    bool Create(std::uint16_t texture, const glm::vec3& position, float scale, const glm::vec3& light, float rotation,
                std::int32_t blend);
    // 0x549510: prende +0x16c de los vivos, antes del pase.
    void MarkSeen();
    // Apaga todos (al entrar a la escena).
    void Clear();

    [[nodiscard]] std::span<Sprite> Sprites();
    // Si hay alguno vivo.
    [[nodiscard]] bool Any() const;

private:
    // En el heap: la escena que la lleva tiene que caber en la pila.
    std::vector<Sprite> m_sprites = std::vector<Sprite>(SpriteCount);
};

// 0x549290, al dibujar: visto, el alpha sube 0,1 con tope 1; sin ver, baja 0,1 con piso 0,2. En 53 bits y
// guardado en float.
void StepSpriteAlpha(Sprite& sprite);
} // namespace Client::World
