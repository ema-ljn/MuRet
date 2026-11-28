#pragma once

#include "Core/Expected.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "Particles/IParticleDrawingServices.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// 0x548550's services without GL or resources, for Particles::DrawParticlesChecked: each image's dimensions
// (Resource+20/+24 by Slot+08) and each type's material byte (Resource+28 by Slot+04) that the test sets, and what the
// drawing requested of each slot. The types MuRet does not bind yet (0x620850 and 0x620870) fail with their phase,
// without empty callbacks.
namespace Client::Tests
{
struct RecordedParticleDraw
{
    // The blend the material left: 0x620a90 with the byte at 3, 0x620a00(0) with another, and 0x620b10 or 0x620c10 if
    // the drawing requested it afterwards.
    enum class Blend : std::uint8_t
    {
        Alpha,
        Additive,
        Subtractive,
        // 0x620c10, mode 6 (SRC_ALPHA / ONE_MINUS_SRC_ALPHA).
        Blended,
    };

    // What reached 0x621400's billboard.
    std::int32_t texture = 0;
    Blend blend = Blend::Additive;
    // The texture mode it was drawn with (glTexEnvi, 0x5489eb).
    Render::TextureEnvMode textureEnv = Render::TextureEnvMode::Modulate;
    glm::vec3 position{};
    float width = 0.0f;
    float height = 0.0f;
    glm::vec3 light{};
    float rotation = 0.0f;
    glm::vec2 uv{};
    glm::vec2 uvSize{};
};

class ParticleDrawRecorder final : public Particles::IParticleDrawingServices
{
public:
    // By image (Slot+08) and by type (Slot+04); a missing one is its boundary's error.
    std::map<std::int32_t, glm::vec2> dimensions;
    std::map<std::int32_t, std::uint8_t> materialFlags;
    // One billboard per drawn slot, in pool order.
    std::vector<RecordedParticleDraw> draws;
    // The texture mode the last glTexEnvi left; GL starts in GL_MODULATE.
    Render::TextureEnvMode textureEnv = Render::TextureEnvMode::Modulate;
    std::size_t textureEnvCalls = 0;

    [[nodiscard]] Core::Expected<glm::vec2, Particles::ParticleError> Dimensions(std::int32_t image) override;
    [[nodiscard]] Core::Expected<std::uint8_t, Particles::ParticleError> MaterialFlag(std::int32_t type) override;
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

// 0x548550(`mode`) over a pool that only has `particle`, at slot `slot` (the real index the mist reads), with
// `textureSize` for its image and `materialFlag` for its type, if present. Returns the requested billboard, nothing if
// the slot was not drawn, or the error. `particle` comes back with what the drawing wrote to it. If `textureEnv` is
// set, it receives the texture mode left after the drawing.
[[nodiscard]] Core::Expected<std::optional<RecordedParticleDraw>, Particles::ParticleDrawingError>
DrawOneParticle(Particles::Particle& particle, std::optional<glm::vec2> textureSize,
                std::optional<std::uint8_t> materialFlag, float clock = 0.0f, std::size_t slot = 0,
                std::uint8_t mode = 0, Render::TextureEnvMode* textureEnv = nullptr);
} // namespace Client::Tests
