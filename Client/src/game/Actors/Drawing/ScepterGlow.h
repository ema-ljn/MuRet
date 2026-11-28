#pragma once

#include "Core/IClock.h"
#include "Models/Animation.h"
#include "Math/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

// El brillo del cetro del Dark Lord del login (CL7.6 B1): el caso 0x620 (HDK_Mace) de 0x509340
// (0x50a169..0x50a398), que pide cinco sprites por frame después de dibujar el ítem. Contrato: 06 § Los
// efectos del login. Verificado contra el código real emulado en 300 casos
// (tests/vectors/world55/scepter.txt).
namespace Client::Actors::Drawing
{

inline constexpr std::uint16_t HdkMaceModel = 0x620;
inline constexpr std::size_t ScepterSpriteCount = 5;

// Los cinco, aditivos y con la luz (1; 0,3; 0), en TransformPosition(hueso 0 del ítem, desplazamiento)
// con la escala y el origen del cuerpo: dos Shiny02 (0x5fd) y un flare01 (0x5ac) en el tridente, y dos
// Shiny02 en el pomo, con la posición y la escala de seis rand() del frame. Con la tabla llena no se
// crean, pero los rand() salen igual.
void CreateScepterGlow(Particles::FrameSprites& sprites, const Math::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, Crt::CrtRandom& random);

// Los seis rand() de un frame, ya con su módulo, en el orden en que salen: el p y la escala del tridente
// (% 60 y % 10), los del flare01 (% 60 y % 10) y los del pomo (% 40 y % 8).
struct ScepterGlowRandoms
{
    int tridentStep = 0;
    int tridentScale = 0;
    int flareStep = 0;
    int flareScale = 0;
    int pommelStep = 0;
    int pommelScale = 0;
};
[[nodiscard]] ScepterGlowRandoms DrawScepterGlowRandoms(Crt::CrtRandom& random);
// Los cinco con los rand() ya sacados.
void CreateScepterGlow(Particles::FrameSprites& sprites, const Math::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, const ScepterGlowRandoms& randoms);

// El brillo del cetro de un personaje a cualquier FPS, corregido como la tela (Q-C124) y la mascota (Q-C20):
// 0x509340 lo sortea en cada frame dibujado del original, y MuRet saca los seis rand() una vez por cada
// frame del original (Models::OriginalFrameTime, 46,875 ms) que pasó desde el suyo anterior y, en
// uno sin ninguno, repite el último sorteo en el hueso de ahora. El primero sortea; un salto de más de
// MaxScepterFramesPerAdvance frames saca los de esos y descarta el resto. En el modo fiel (`pace`
// OncePerDraw), uno por dibujo, como el original (Models::OriginalFrameCarry).
inline constexpr int MaxScepterFramesPerAdvance = Models::MaxOriginalFramesPerDraw;

class ScepterGlowCarry
{
public:
    void Create(Particles::FrameSprites& sprites, const Math::OriginalMatrix& bone, float bodyScale,
                const glm::vec3& bodyOrigin, Crt::CrtRandom& random, Core::Duration clock,
                Models::OriginalFramePace pace = Models::OriginalFramePace::ByTheClock);
    // Create vuelve a contar desde cero, como en el primer frame: el próximo sortea un frame y no pone al día el
    // tiempo que pasó sin dibujo. Es para un frame retenido (Q-C85), como la tela.
    void RestartTheClock()
    {
        m_frames = {};
    }

private:
    Models::OriginalFrameCarry m_frames;
    ScepterGlowRandoms m_last{};
};
} // namespace Client::Actors::Drawing
