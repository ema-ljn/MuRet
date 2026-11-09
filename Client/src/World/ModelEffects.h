#pragma once

#include "Core/Expected.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <functional>
#include <span>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los efectos con modelo (CL7.6 A): la tabla de 0x518cf0 (0x7491fd8, 200 lugares de 0x1d4 bytes) y su
// paso, 0x5208a0. MuRet arma el dragón del login (0xb4, 06 § El dragón de fondo), llegada 61f/0 y los tipos
// 135/142/143/144 de los constructores de mapa. El dragón conserva sus 1.500 pasos golden.
namespace Client::World
{
inline constexpr std::size_t ModelEffectCount = 200;
// El dragón que vuela en el login, con su modelo (Scenes::DragonModel) y la escala que le pasa 0x606660.
inline constexpr std::int16_t DragonEffect = 0xb4;
inline constexpr float DragonScale = 1.6f;
// 518cf0 / 52dc20: llegada, únicamente subtipo 0.
inline constexpr std::int16_t ArrivalEffect = 0x61f;

// Estado nativo de los campos usados; no representa memoria x86 ni contiene direcciones originales.
struct ModelEffect
{
    std::uint8_t alive = 0;                    // +0x0
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
    // 518cf0 conserva los campos no escritos del registro reutilizado. Estos nombres neutrales
    // mantienen los anchos comprobados; owner es una identidad lógica, nunca una dirección host.
    std::int32_t subtype = 0;         // +04
    std::uint32_t owner = 0;          // +108
    std::uint8_t visible = 0;         // +16c, NO inicializado por Create
    std::uint8_t field16d = 0;        // +16d
    std::uint8_t field89 = 0;         // +89
    std::uint8_t field1bd = 0;        // +1bd
    std::uint16_t field8a = 0;        // +8a
    std::uint32_t field74 = 0;        // +74
    std::uint32_t field100 = 0;       // +100
    std::uint16_t hiddenMeshHigh = 0; // mitad alta del store dword +58
    std::uint16_t blendMeshHigh = 0;  // mitad alta del store dword +64
    float previousFrame = 0.0f;       // +118, no el índice derivado del pose
};

class ModelEffects
{
public:
    // 0x518cf0(type, position, angles, light, 0, dueño, −1, 0, 0, 0, scale) en el primer lugar sin el
    // byte vivo, o nada si están todos. Una escala de 0 o menos pasa a 0,9 (0x518d3c). Del 0xb4: la
    // posición del dueño con la z 3.400 más arriba (0x518fc9), los ángulos con el alabeo en 0 (0x518fb3),
    // la luz en blanco (la salida 0x51a97b pisa `light`), vida 1.000, la velocidad (0, −35, 0), la de la
    // animación en 1, +0xdc, +0xe4 y +0xec en 1, luz por vértice y la partida en la posición. Los otros
    // tipos admitidos son 61f/0, 135 y 144. 142/143 requieren el overload mutable con RNG.
    // El prefijo conserva lo no escrito, incluido visible;135 fija vida 100, luz de mezcla 0,3 y
    // partida copiada del slot. 144 fija vida 00ffffff, malla de mezcla −2 y escala 0,6.
    std::optional<std::size_t> Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, float scale, std::uint32_t owner = 0);
    // 142/143 copian light al slot y después borran el vector de entrada; requieren tres sorteos.
    // Las demás rutas conservan la cadencia anterior. subtype se escribe antes de las copias.
    std::optional<std::size_t> Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                      glm::vec3& light, float scale, Render::Models::CrtRandom& random,
                                      std::uint32_t owner = 0, std::int32_t subtype = 0);
    // 0x6070a0: apaga el byte vivo de cada lugar (Q-C138: el original deja lo demás; acá Create arma el
    // lugar entero para el dragón; llegada conserva los campos no escritos).
    void Clear();

    [[nodiscard]] std::span<ModelEffect> Effects();
    [[nodiscard]] std::span<const ModelEffect> Effects() const;

private:
    std::optional<std::size_t> CreateStored(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                            const glm::vec3& light, float scale, std::uint32_t owner,
                                            std::int32_t subtype, Render::Models::CrtRandom* random,
                                            glm::vec3* mutableLight);
    std::array<ModelEffect, ModelEffectCount> m_effects{};
};

// La metadata compartida de un modelo: M+a0 y la tabla viva M+30. El dueño mantiene su lifetime.
struct ModelEffectAnimationBinding
{
    // Ubicación fija de M+a0; su byte es mutable, el préstamo no se reasigna.
    const std::reference_wrapper<std::uint8_t> action;
    std::span<const Render::Models::ActionInfo> actions{};
};

// Fronteras requeridas de 5208a0: el reloj vivo, la metadata por tipo y 4ec200 (retorno AL no cero).
// Advance recibe el registro vivo y speed por valor; puede reemplazar la tabla/selector de metadata.
struct ModelEffectStepBindings
{
    std::function<float()> readClock;
    std::function<ModelEffectAnimationBinding*(std::int16_t)> animationOf;
    std::function<bool(ModelEffectAnimationBinding&, ModelEffect&, float)> advance;
};

enum class ModelEffectStepError : std::uint8_t
{
    MissingBinding,
    MissingMetadata,
    InvalidMetadataAction,
    UnsupportedTypeChange,
};

[[nodiscard]] bool IsConstructedMapEffect(std::int16_t type);
// Sólo 135/142/143/144. Un prefijo RNG; cola con metadata viva, movimiento y vida módulo32.
// Errores de binding son explícitos; no se sustituye metadata ausente por un no-op.
Core::Expected<void, ModelEffectStepError> StepConstructedModelEffect(ModelEffect& effect,
                                                                      Render::Models::CrtRandom& random,
                                                                      const ModelEffectStepBindings& bindings);

// 0x5208a0 sobre un efecto vivo, con el esqueleto de su modelo (para el avance, o nullptr): el caso del
// tipo (el 0xb4: 0x52096c..0x520b72) y la cola común (0x52cb98): la animación a +0xd8 (0x4ec200; si la
// acción termina, el frame en sus keys y la acción en 0xff), la posición más la velocidad girada por los
// ángulos (0x51fc80 con 0x5fc8f0 y 0x5fcbd0) y una vida menos; con la vida en 0, muere.
void StepModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton);

// 5208a0 / 51fc80 para 61f/0: un sorteo del RNG compartido, movimiento girado y decremento
// módulo32 antes de expirar. Como el original, no comprueba alive; el caller selecciona los vivos.
void StepArrivalModelEffect(ModelEffect& effect, Render::Models::CrtRandom& random);
} // namespace Client::World
