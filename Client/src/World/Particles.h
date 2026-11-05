#pragma once

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Las partículas (CL7.6 B2): la tabla de 0x53fe70 (0x743a198, 3.000 lugares de 0x78 bytes), su paso,
// 0x5446e0, con el viento de 0x7609bb8, y lo que su dibujo, 0x548550(0), le pide a 0x621400. De sus
// tipos, MuRet solo arma el aliento del caballo del login (0x5f2, Effect\smoke01.jpg, subtipo 0); los
// demás vienen con los efectos de cada escena. Contrato: 06 § Los efectos del login. Verificado bit a
// bit contra el código real emulado (tests/vectors/world55/particles.txt).
namespace Client::World
{
inline constexpr std::size_t ParticleCount = 3000;
inline constexpr std::uint16_t BreathParticle = 0x5f2;

struct Particle
{
    bool alive = false;         // +0x0
    std::int32_t type = 0;      // +0x4
    std::int32_t texture = 0;   // +0x8, la textura con que se dibuja (0x53fe70 le copia el tipo)
    std::int32_t subtype = 0;   // +0xc
    float scale = 0.0f;         // +0x10
    glm::vec3 position{};       // +0x14
    glm::vec3 angles{};         // +0x20, en grados: los de la velocidad
    glm::vec3 light{};          // +0x2c
    std::int32_t life = 0;      // +0x3c, pasos que le quedan
    std::uint32_t owner = 0;    // +0x40, el dueño (un puntero en el original)
    float rotation = 0.0f;      // +0x44, la del billboard, en grados
    float field48 = 0.0f;       // +0x48
    bool moves = false;         // +0x4c: el paso le suma la velocidad girada por los ángulos
    float verticalSpeed = 0.0f; // +0x50
    glm::vec3 velocity{};       // +0x54
    glm::vec3 start{};          // +0x6c, donde nació
};

// El viento de las partículas: 0x7609bb8 (x, y), entre −1,7 y 1,7, y lo que cambia en cada paso,
// 0x7609bc4, entre −0,6 y 0,6.
struct ParticleWind
{
    glm::vec2 wind{};
    glm::vec2 change{};
};

class Particles
{
public:
    // 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, dueño) en el primer lugar libre, con el reloj
    // del mundo (DAT_05767b88, en ms) y el rand() de la CRT. Con los 3.000 llenos no se crea ni saca
    // rand(). MuRet solo arma el 0x5f2 con el subtipo 0: los demás no se crean.
    std::optional<std::size_t> Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                                      Render::Models::CrtRandom& random);
    // Apaga todos (al entrar a la escena) y deja el viento en 0.
    void Clear();

    [[nodiscard]] std::span<Particle> All();
    [[nodiscard]] std::span<const Particle> All() const;
    [[nodiscard]] bool Any() const;
    [[nodiscard]] ParticleWind& Wind();
    [[nodiscard]] const ParticleWind& Wind() const;

private:
    std::array<Particle, ParticleCount> m_particles{};
    ParticleWind m_wind;
};

// 0x5446e0: el viento (dos rand(), uno por eje) y cada partícula viva: la vida baja uno y en 0 se apaga
// (pero el paso de su tipo corre igual), con +0x4c se mueve (0x4e98d0), y el caso de su tipo.
void StepParticles(Particles& particles, Render::Models::CrtRandom& random);

// Lo que 0x548550(0) le pide a 0x621400 por una partícula viva, con el tamaño de su textura (+0x20 y
// +0x24 de su lugar) y sus componentes (+0x28): con 3, la mezcla aditiva (0x620a90); si no, la normal
// sin la máscara (0x620a00(0)).
struct ParticleDraw
{
    std::int32_t texture = 0;
    bool additive = true;
    glm::vec3 position{};
    float width = 0.0f;
    float height = 0.0f;
    glm::vec3 light{};
    float rotation = 0.0f;
    glm::vec2 uv{};
    glm::vec2 uvSize{1.0f, 1.0f};
};

// La del 0x5f2 con el subtipo 0: el cuadrado de la textura por la escala, girado +0x44, con toda la
// textura. Las de los tipos que MuRet no arma: nullopt.
[[nodiscard]] std::optional<ParticleDraw> ParticleDrawOf(const Particle& particle, const glm::vec2& textureSize,
                                                         int components);
} // namespace Client::World
