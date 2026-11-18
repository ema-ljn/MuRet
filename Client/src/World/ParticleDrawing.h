#pragma once
#include "Core/Expected.h"
#include "World/Particles.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <system_error>
namespace Client::World
{
class IParticleDrawingServices;
enum class ParticleDrawingPhase
{
    Dimensions,
    MaterialFlag,
    Material,
    Type620850,
    Type620870,
    Branch,
    SubtractivePrefix,
    Helper6208f0,
    Helper6208b0,
    SubtractiveTail,
    BlendedPrefix,
    BlendedTail,
    Bind,
    Begin,
    ColorFlag,
    Color,
    TexCoord,
    Vertex,
    End,
    // glTexEnvi de 0x5489eb; va al final para no mover los números de las fases que ya salen en los errores.
    TextureEnv
};
struct ParticleDrawingError
{
    ParticleError reason;
    ParticleDrawingPhase phase;
    std::error_code backendError;
};
struct ParticleDrawingFailure
{
    std::size_t slot;
    ParticleDrawingError error;
};
// 548550: mismo pool, índice físico y prefijo sin rollback. Clock es el G vivo, no delta/seed.
// Destinos acreditados: Fire default salvo5/7/8/11/12/13; Smoke con las guardas y relecturas cerradas;
// Cloud8/10; Drop/Thunder/Spark/67/68, el brillo de la mano 5fd y la explosión 5ed (con sus cuadros de 4 × 4) directos
// sin guarda de subtype;
// 6ab directo y 6ac con 0x620c10 antes del billboard, la /2 entre GL_ADD y GL_MODULATE (0x5489eb). Otros destinos
// fallan en Branch
// DESPUÉS del prefijo. Es el único contrato del dibujo: Create/Step conservan su propio subconjunto,
// que no filtra lo que se dibuja.
// Pool, clock y servicios permanecen vivos; no reload/release concurrente ni retención diferida.

[[nodiscard]] Core::Expected<void, ParticleDrawingFailure>
DrawParticlesChecked(Particles& particles, std::uint8_t mode, std::reference_wrapper<const float> clock,
                     IParticleDrawingServices& services);
} // namespace Client::World
