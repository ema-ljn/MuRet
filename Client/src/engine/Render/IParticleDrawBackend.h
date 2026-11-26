#pragma once
#include "Core/Expected.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureEnvMode.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <system_error>
namespace Client::Render
{
// Mandatory context boundaries, with no initial state or empty implementations. D3 must supply the raw32
// caches/binder and the real submission: this interface does not prove production GL.
class IParticleDrawBackend
{
public:
    using Result = Core::Expected<void, std::error_code>;
    virtual ~IParticleDrawBackend() = default;
    [[nodiscard]] virtual Result Additive() = 0;          // 620a90, complete.
    [[nodiscard]] virtual Result AlphaInheritDepth() = 0; // 620a00(raw0), without forcing the mask.
    // 620b10's own body: mode-4/blend prefix; two OPAQUE helpers in order (never replace them with assumed effects);
    // alpha/texture/fog tail. Prefix and Tail reread their state after the previous boundaries, never a snapshot across
    // helpers. Prefix: if mode32 != 4, store 4 before enabling BLEND and factors ZERO/ONE_MINUS_SRC_COLOR; a hit skips
    // both calls. Tail: alpha nonzero → store 0/disable, texture 0 → store 1/enable, fog nonzero → enable without
    // store.

    [[nodiscard]] virtual Result SubtractivePrefix() = 0;
    [[nodiscard]] virtual Result Helper6208f0() = 0;
    [[nodiscard]] virtual Result Helper6208b0() = 0;
    [[nodiscard]] virtual Result SubtractiveTail() = 0;
    // 0x620c10: the same body with mode 6 and factors SRC_ALPHA / ONE_MINUS_SRC_ALPHA; the tail is 0x620b10's.
    [[nodiscard]] virtual Result BlendedPrefix() = 0;
    [[nodiscard]] virtual Result BlendedTail() = 0;
    [[nodiscard]] virtual Result Type620850() = 0;
    [[nodiscard]] virtual Result Type620870() = 0;
    // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode), outside a quad: 0x5489eb sets GL_ADD before 0x620c10 and
    // restores GL_MODULATE after the billboard.
    [[nodiscard]] virtual Result TextureEnv(TextureEnvMode mode) = 0;
    // 620780: selector32 is the key; hit/miss/name/NEG32 belong to the durable D3 context.
    [[nodiscard]] virtual Result BindTexture(std::int32_t selector) = 0;
    [[nodiscard]] virtual Result Begin() = 0;
    [[nodiscard]] virtual Result Color3(const glm::vec3& light) = 0;
    [[nodiscard]] virtual Result Color4(const Color& color) = 0;
    [[nodiscard]] virtual Result TexCoord(glm::vec2 uv) = 0;
    [[nodiscard]] virtual Result Vertex(const glm::vec3& position) = 0;
    // Never publish an incomplete quad if an earlier boundary failed; never simulate a rollback either.
    [[nodiscard]] virtual Result End() = 0;
};
} // namespace Client::Render
