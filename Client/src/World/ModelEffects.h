#pragma once

#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Los efectos con modelo (CL7.6 A): la tabla de 0x518cf0 (0x7491fd8, 200 lugares de 0x1d4 bytes) y su
// paso, 0x5208a0. De sus tipos, MuRet solo arma el dragón del login (0xb4, 06 § El dragón de fondo);
// los demás vienen con los efectos de cada escena. Verificado bit a bit contra el código real emulado
// en 1.500 pasos (tests/vectors/world55/dragon.txt).
namespace Client::World
{
inline constexpr std::size_t ModelEffectCount = 200;
// El dragón que vuela en el login, con su modelo (Scenes::DragonModel) y la escala que le pasa 0x606660.
inline constexpr std::int16_t DragonEffect = 0xb4;
inline constexpr float DragonScale = 1.6f;
// El efecto de llegada: lo crea dos veces la respuesta del F3 03 sobre el héroe (06, "El héroe").
inline constexpr std::int16_t ArrivalEffect = 0x61f;

// Lo que el efecto de 0x1d4 bytes guarda y usa el dragón.
struct ModelEffect
{
    bool alive = false;                        // +0x0
    std::int16_t type = 0;                     // +0x2, el modelo
    Render::Models::ObjectTransform transform; // +0xc la escala, +0x10 la posición, +0x1c los ángulos
    std::int32_t life = 0;                     // +0x60, pasos que le quedan
    glm::vec3 velocity{};                      // +0xcc, por paso, girada por los ángulos
    float speed = 0.0f;                        // +0xd8, keys por paso
    float turnPending = 0.0f;                  // +0xdc: en el próximo paso lejos, da la vuelta
    float turnDirection = 0.0f;                // +0xe4: 1 o −1, el sentido del giro
    float flagEc = 0.0f;                       // +0xec
    std::uint8_t state = 0;                    // +0x88: 0 y 1 giran, 2 va derecho
    // +0x84: al empezar cada paso, la meta del alabeo del estado (30, −30 o 0); al dar la vuelta, el
    // yaw con el que se compara el giro de ±60°. Los dos usos son del original.
    float reference = 0.0f;
    Render::Models::AnimationState animation; // +0x111 la acción, +0x114 el frame, +0x118 el anterior
    glm::vec3 light{};                        // +0xf4
    float alpha = 1.0f;                       // +0x174
    Render::Models::ObjectState objectState;  // +0x58, +0x64 y +0x68
    bool vertexLight = true;                  // +0xe8
    glm::vec3 start{};                        // +0x17c, la partida
    // De MuRet: la posición antes del último paso, para dibujar entre dos pasos (D-C4).
    glm::vec3 previousPosition{};
};

class ModelEffects
{
public:
    // 0x518cf0(type, position, angles, light, 0, dueño, −1, 0, 0, 0, scale) en el primer lugar sin el
    // byte vivo, o nada si están todos. Una escala de 0 o menos pasa a 0,9 (0x518d3c). Del 0xb4: la
    // posición del dueño con la z 3.400 más arriba (0x518fc9), los ángulos con el alabeo en 0 (0x518fb3),
    // la luz en blanco (la salida 0x51a97b pisa `light`), vida 1.000, la velocidad (0, −35, 0), la de la
    // animación en 1, +0xdc, +0xe4 y +0xec en 1, luz por vértice y la partida en la posición. Del 0x61f
    // con el subtipo 0: la posición, los ángulos y la luz tal cual, 20 pasos de vida y la animación a
    // 0,3, sin rand(). Los otros tipos no se crean.
    std::optional<std::size_t> Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, float scale);
    // 0x6070a0: apaga el byte vivo de cada lugar (Q-C138: el original deja lo demás; acá Create arma el
    // lugar entero).
    void Clear();

    [[nodiscard]] std::span<ModelEffect> Effects();
    [[nodiscard]] std::span<const ModelEffect> Effects() const;

private:
    std::array<ModelEffect, ModelEffectCount> m_effects{};
};

// 0x5208a0 sobre un efecto vivo, con el esqueleto de su modelo (para el avance, o nullptr): el caso del
// tipo (el 0xb4: 0x52096c..0x520b72) y la cola común (0x52cb98): la animación a +0xd8 (0x4ec200; si la
// acción termina, el frame en sus keys y la acción en 0xff), la posición más la velocidad girada por los
// ángulos (0x51fc80 con 0x5fc8f0 y 0x5fcbd0) y una vida menos; con la vida en 0, muere.
void StepModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton);
} // namespace Client::World
