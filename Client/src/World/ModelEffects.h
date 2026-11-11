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

namespace Client::Render::Models
{
class CrtRandom;
}

// Los efectos con modelo (CL7.6 A): la tabla de 0x518cf0 (200 lugares de 0x1d4 bytes).
// El dragón conserva los vectores históricos; llegada, teleport y torre de Noria se contrastaron
// estáticamente con ASM. Para el paso del juego y su CRT, usar World/GameModelEffects.h.
namespace Client::World
{
inline constexpr std::size_t ModelEffectCount = 200;
// El dragón que vuela en el login, con su modelo (Scenes::DragonModel) y la escala que le pasa 0x606660.
inline constexpr std::int16_t DragonEffect = 0xb4;
inline constexpr float DragonScale = 1.6f;
// El efecto de llegada: lo crea dos veces la respuesta del F3 03 sobre el héroe (06, "El héroe").
inline constexpr std::int16_t ArrivalEffect = 0x61f;
inline constexpr std::int16_t TeleportEffect = 0x5c6;

// Proyección de los campos compartidos por los efectos de 0x1d4 bytes.
struct ModelEffect
{
    bool alive = false;                        // +0x0
    bool seen = false;                         // +0x16c, del último dibujo de este lugar
    std::int16_t type = 0;                     // +0x2, el modelo
    std::int32_t subtype = 0;                  // +0x4, la rama del efecto
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
    std::uint16_t disturbanceState = 0;       // +0x8a, guard de437860 del efecto9
    bool vertexLight = true;                  // +0xe8
    glm::vec3 start{};                        // +0x17c, la partida
    // De MuRet: la posición antes del último paso, para dibujar entre dos pasos (D-C4).
    glm::vec3 previousPosition{};
    std::int32_t textureFrame = 0;           // +0x1c4: selector firmado del calco 0x14e en52d8f2.
    const std::int16_t* itemType = nullptr;  // +108 del efecto→+8c del dueño durable, paraf4/fa.
    const std::uint8_t* itemLevel = nullptr; // +108→+8e; no inferir desde ActorState.
};

class ModelEffects
{
public:
    // 438a1a→5191be: tipo9/0, ownerNULL, escalaargumento0; primer libre, lleno sinCRT.
    std::optional<std::size_t> CreateKalimaEffect(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(type, position, angles, light, 0, dueño, −1, 0, 0, 0, scale) en el primer lugar sin el
    // byte vivo, o nada si están todos. Una escala de 0 o menos pasa a 0,9 (0x518d3c). Del 0xb4: la
    // posición del dueño con la z 3.400 más arriba (0x518fc9), los ángulos con el alabeo en 0 (0x518fb3),
    // la luz en blanco (la salida 0x51a97b pisa `light`), vida 1.000, la velocidad (0, −35, 0), la de la
    // animación en 1, +0xdc, +0xe4 y +0xec en 1, luz por vértice y la partida en la posición. Del 0x61f
    // con el subtipo 0: la posición, los ángulos y la luz tal cual, 20 pasos de vida y la animación a
    // 0,3, sin rand(). El 0x5c6 conserva ese prefijo con vida 10 (0x51e4f6), también sin rand().
    // Noria tiene su creador separado con CRT; los demás tipos no se crean.
    std::optional<std::size_t> Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, float scale);
    // 0x51c36e/0x51c38a: los efectos 322..324 de la torre de Noria; 322/323 ponen sourceLight en cero.
    std::optional<std::size_t> CreateNoriaEffect(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                 glm::vec3& sourceLight, Render::Models::CrtRandom& random);
    // 0x6070a0: sólo apaga el byte vivo de cada lugar (Q-C138).
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
