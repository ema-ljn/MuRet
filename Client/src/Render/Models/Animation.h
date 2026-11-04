#pragma once

#include "Core/IClock.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <chrono>
#include <cstdint>

// Cuánto avanza la animación de un modelo (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto
// avanza la animación"): el cuerpo por paso de lógica con la velocidad de su acción, y la mascota y
// las alas por tiempo, a la velocidad que tienen a 21,33 FPS (Q-C20 corregido).
namespace Client::Render::Models
{
// La escena 4 decide "terminó" con el frame + 2 (Q-C21); las demás, con el real.
enum class AnimationScene : std::uint8_t
{
    Other,
    CharacterSelect,
};

// 0x4ec200 con `speed` (la de la acción, o la que le pase quien llama), sobre la acción de ahora, que
// es `action`. Con una key o ninguna no hace nada. Si no: suma la velocidad; si cambió la key entera,
// guarda la vieja y su acción como anteriores; con el frame en 0 o menos le suma las keys menos 1; y
// al pasar la última, si la acción repite, vuelve con el módulo (las keys, o las keys menos 1 con
// lock_positions), y si no, se queda en las keys menos 0,01. Devuelve si terminó: en la selección lo
// decide el frame + 2, pero el módulo usa el real.
bool AdvanceAnimation(AnimationState& state, const ActionInfo& action, float speed, AnimationScene scene);

// 0x4ea030: nada con una acción que `skeleton` no tiene o con la misma; si no, la de ahora y su key
// (el original guarda el frame tal cual, y la pose lo trunca) pasan a anteriores y el frame a 0.
void SetAction(AnimationState& state, const Skeleton& skeleton, std::uint8_t action);

// 1 / 21,33 FPS: el frame del modo fiel de la CL3 (64 frames cada 3 s).
inline constexpr Core::Duration OriginalFrameTime = std::chrono::microseconds{46'875};

// Lo que avanzan por frame del original la mascota Helper02 (0x1c13, personaje + 0x318) y las alas
// (personaje + 0x2f8): 0,5, y 0,25 o 1 con el personaje en la acción 0x1e o 0x1f (0x50ba30).
inline constexpr float PetKeysPerOriginalFrame = 0.5f;
[[nodiscard]] float WingKeysPerOriginalFrame(std::uint8_t characterAction);

// Una animación que avanza por tiempo: el estado, y lo que todavía no llega a un frame del original.
struct TimedAnimation
{
    AnimationState state;
    Core::Duration carry{};
};

// Q-C20 corregido: `keysPerOriginalFrame` cada OriginalFrameTime de `elapsed`, con la regla de
// AdvanceAnimation. Un `elapsed` negativo cuenta 0.
void AdvanceByTime(TimedAnimation& animation, const ActionInfo& action, float keysPerOriginalFrame,
                   Core::Duration elapsed);

// La copia que posa el render entre dos pasos: `alpha` de un paso más, con la misma regla, así las
// keys que cruza guardan su anterior como en el paso.
[[nodiscard]] AnimationState Interpolated(const AnimationState& state, const ActionInfo& action, float speed,
                                          float alpha);
} // namespace Client::Render::Models
