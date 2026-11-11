#pragma once

#include "Core/Expected.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "World/IParticleDrawingServices.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// Los servicios de 0x548550 sin GL ni recursos, para World::DrawParticlesChecked: las dimensiones de cada
// imagen (Recurso+20/+24 por Slot+08) y el byte de material de cada tipo (Recurso+28 por Slot+04) que fija
// el test, y lo que el dibujo pidió de cada lugar. Los tipos que MuRet todavía no liga (0x620850 y
// 0x620870) fallan con su fase, sin callbacks vacíos.
namespace Client::Tests
{
struct RecordedParticleDraw
{
    // La mezcla que dejó el material: 0x620a90 con el byte en 3, 0x620a00(0) con otro, y 0x620b10 o 0x620c10 si
    // el dibujo la pidió después.
    enum class Blend : std::uint8_t
    {
        Alpha,
        Additive,
        Subtractive,
        // 0x620c10, el modo 6 (SRC_ALPHA / ONE_MINUS_SRC_ALPHA).
        Blended,
    };

    // Lo que llegó al billboard de 0x621400.
    std::int32_t texture = 0;
    Blend blend = Blend::Additive;
    // El modo de la textura con el que se dibujó (glTexEnvi, 0x5489eb).
    Render::TextureEnvMode textureEnv = Render::TextureEnvMode::Modulate;
    glm::vec3 position{};
    float width = 0.0f;
    float height = 0.0f;
    glm::vec3 light{};
    float rotation = 0.0f;
    glm::vec2 uv{};
    glm::vec2 uvSize{};
};

class ParticleDrawRecorder final : public World::IParticleDrawingServices
{
public:
    // Por imagen (Slot+08) y por tipo (Slot+04); lo que falta es el error de su frontera.
    std::map<std::int32_t, glm::vec2> dimensions;
    std::map<std::int32_t, std::uint8_t> materialFlags;
    // Un billboard por lugar dibujado, en el orden del pool.
    std::vector<RecordedParticleDraw> draws;
    // El modo de la textura que dejó el último glTexEnvi; GL arranca en GL_MODULATE.
    Render::TextureEnvMode textureEnv = Render::TextureEnvMode::Modulate;
    // Cuántas veces se llamó a glTexEnvi.
    std::size_t textureEnvCalls = 0;

    [[nodiscard]] Core::Expected<glm::vec2, World::ParticleError> Dimensions(std::int32_t image) override;
    [[nodiscard]] Core::Expected<std::uint8_t, World::ParticleError> MaterialFlag(std::int32_t type) override;
    [[nodiscard]] Result Material(std::uint8_t rawFlag) override;
    [[nodiscard]] Result Subtractive() override;
    [[nodiscard]] Result Blended() override;
    [[nodiscard]] Result Type620850() override;
    [[nodiscard]] Result Type620870() override;
    [[nodiscard]] Result TextureEnv(Render::TextureEnvMode mode) override;
    [[nodiscard]] Result Billboard(Render::ParticleBillboardRequest request) override;

private:
    RecordedParticleDraw::Blend m_blend = RecordedParticleDraw::Blend::Additive;
};

// 0x548550(`mode`) sobre un pool que solo tiene `particle`, en el lugar `slot` (el índice real que lee la
// neblina), con `textureSize` para su imagen y `materialFlag` para su tipo, si están. Devuelve el billboard
// que pidió, nada si el lugar no se dibujó, o el error. `particle` vuelve con lo que el dibujo le escribió. Si
// `textureEnv` está, recibe el modo de la textura que quedó después del dibujo.
[[nodiscard]] Core::Expected<std::optional<RecordedParticleDraw>, World::ParticleDrawingError>
DrawOneParticle(World::Particle& particle, std::optional<glm::vec2> textureSize,
                std::optional<std::uint8_t> materialFlag, float clock = 0.0f, std::size_t slot = 0,
                std::uint8_t mode = 0, Render::TextureEnvMode* textureEnv = nullptr);
} // namespace Client::Tests
