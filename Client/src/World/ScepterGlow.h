#pragma once

#include "Core/IClock.h"
#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// El brillo del cetro del Dark Lord del login (CL7.6 B1): el caso 0x620 (HDK_Mace) de 0x509340
// (0x50a169..0x50a398), que pide cinco sprites por frame después de dibujar el ítem. Contrato: 06 § Los
// efectos del login. Verificado contra el código real emulado en 300 casos
// (tests/vectors/world55/scepter.txt).
namespace Client::World
{
class FrameSprites;

inline constexpr std::uint16_t HdkMaceModel = 0x620;
inline constexpr std::size_t ScepterSpriteCount = 5;

// Los cinco, aditivos y con la luz (1; 0,3; 0), en TransformPosition(hueso 0 del ítem, desplazamiento)
// con la escala y el origen del cuerpo: dos Shiny02 (0x5fd) y un flare01 (0x5ac) en el tridente, y dos
// Shiny02 en el pomo, con la posición y la escala de seis rand() del frame. Con la tabla llena no se
// crean, pero los rand() salen igual.
void CreateScepterGlow(FrameSprites& sprites, const Render::Models::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, Render::Models::CrtRandom& random);

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
[[nodiscard]] ScepterGlowRandoms DrawScepterGlowRandoms(Render::Models::CrtRandom& random);
// Los cinco con los rand() ya sacados.
void CreateScepterGlow(FrameSprites& sprites, const Render::Models::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, const ScepterGlowRandoms& randoms);

// El brillo del cetro de un personaje a cualquier FPS, corregido como la tela (Q-C124) y la mascota (Q-C20):
// 0x509340 lo sortea en cada frame dibujado del original, y MuRet saca los seis rand() una vez por cada
// frame del original (Render::Models::OriginalFrameTime, 46,875 ms) que pasó desde el suyo anterior y, en
// uno sin ninguno, repite el último sorteo en el hueso de ahora. El primero sortea; un salto de más de
// MaxScepterFramesPerAdvance frames saca los de esos y descarta el resto. En el modo fiel, cada frame dura
// uno del original y sortea una vez, como el original.
inline constexpr int MaxScepterFramesPerAdvance = 64;

class ScepterGlowCarry
{
public:
    void Create(FrameSprites& sprites, const Render::Models::OriginalMatrix& bone, float bodyScale,
                const glm::vec3& bodyOrigin, Render::Models::CrtRandom& random, Core::Duration clock);

private:
    std::optional<Core::Duration> m_lastClock;
    Core::Duration m_carry{};
    ScepterGlowRandoms m_last{};
};
} // namespace Client::World
