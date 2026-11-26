#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Render/ModelFog.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureEnvMode.h"
#include "Render/TextureSlot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

// A pass's 3D as data, without GL. Ranges are never reordered, like the original, which sends them in the order it
// draws.
namespace Client::Render
{
// The blends of 0x4ecc30 (06 § El modelo en el binario). None touches glDepthFunc.
enum class ModelBlend : std::uint8_t
{
    // 0x620980: no blend, culling, depth write, no alpha test.
    Opaque,
    // 0x620a00(1): SRC_ALPHA / ONE_MINUS_SRC_ALPHA, no culling, alpha test, depth write.
    Alpha,
    // 0x620a90: ONE / ONE, no culling or alpha test, no depth write.
    Additive,
    // The shadow: 0x620980 with the mask 0x4eff40 turns off: no blend, culling, no alpha test, no depth write.
    Shadow,
    // A world object's shadow (the horse, 0x6002e0): 0x620a00(1) plus what 0x4eff40 turns off (mask, texture and alpha
    // test): SRC_ALPHA / ONE_MINUS_SRC_ALPHA, no culling, no alpha test, no depth write.
    BlendedShadow,
    // Whirl 90 of map 55 (0x6002e0 with 0x620b10): ZERO / ONE_MINUS_SRC_COLOR, no culling or alpha test, no depth
    // write: it darkens what is behind where its color is bright.
    Subtractive,
    // 0x620b90: ONE_MINUS_SRC_COLOR / ONE, no culling, alpha test or depth write.
    InverseColorAdditive,
    // 620c90: ZERO / SRC_COLOR, no culling, alpha test or depth write.
    Multiplicative,
};

// 621400 already yields eye coordinates; meshes still yield world coordinates.
enum class ModelCoordinateSpace : std::uint8_t
{
    World,
    Eye,
};

// Proven partial effects: they neither run the other axes nor draw vertices.
enum class ModelStateOperation : std::uint8_t
{
    Draw,
    Blend,
    Cull,
    DepthWrite,
    DepthTest,
    AlphaTest,
    Texture2d,
    Binding,
    TextureEnv,
    Fog,
};

struct ModelDrawState
{
    std::optional<TextureSlot> texture;
    ModelBlend blend = ModelBlend::Opaque;
    // false with 0x4ecc30's flag 0x2000 (0x620870 disables the depth test).
    bool depthTest = true;
    ModelCoordinateSpace space = ModelCoordinateSpace::World;
    // 620780(NEG32)'s physical name, distinct from the logical selector, including an explicit name 0. Invariant:
    // `texture` and rawTextureName are mutually exclusive; the renderer rejects an ambiguous command without picking a
    // priority.
    std::optional<std::uint32_t> rawTextureName = std::nullopt;

    // Absent keeps ModelBlend's legacy profile; present decouples each axis.
    std::optional<bool> cullEnabled = std::nullopt;
    std::optional<bool> depthWrite = std::nullopt;
    std::optional<bool> alphaTest = std::nullopt;
    std::optional<bool> texture2d = std::nullopt;

    [[nodiscard]] bool HasValidTextureBinding() const
    {
        return !texture.has_value() || !rawTextureName.has_value();
    }

    friend bool operator==(const ModelDrawState&, const ModelDrawState&) = default;
};

struct ModelDrawCommand
{
    ModelDrawState state;
    std::size_t firstVertex = 0;
    std::size_t vertexCount = 0; // in threes
    ModelStateOperation operation = ModelStateOperation::Draw;
    // GL_FOG on with its fog, or off. Not part of the material: kept apart from `state` (ModelBatch::SetFog).
    std::optional<ModelFog> fog = std::nullopt;
    // GL_TEXTURE_ENV_MODE, not part of the material either (ModelBatch::SetTextureEnv).
    TextureEnvMode textureEnv = TextureEnvMode::Modulate;
};

class ENGINE_API ModelBatch
{
public:
    // Empties triangles/ranges and resets to ModelDrawState{}. Only 620db0 initializes the material; a continuation
    // flush or 6213e0 keeps the current GL axes. 0x620db0, with 0x7d368a0 at 0 (06 § Cámara), also turns fog off
    // (0x620fa5): with initializeMaterial it becomes std::nullopt; without it, it is kept. Nothing touches the texture
    // env mode.
    void Begin(const Pass3D& pass, bool initializeMaterial = true);
    [[nodiscard]] bool InitializesMaterial() const
    {
        return m_initializeMaterial;
    }

    void SetState(const ModelDrawState& state);

    // State observable even without geometry: the fauna material/binder before Begin or an error. A zero-vertex range
    // changes the state in GlModelRenderer without publishing a partial quad; Triangle can continue it if its state
    // matches.
    void ApplyState(const ModelDrawState& state)
    {
        ++m_stateSerial;
        m_state = state;
        if (!ContinuesTheLastDraw())
            m_commands.push_back(NewDraw());
    }

    // Publishes a single effect, even without geometry. The serial is written before the GL sink.
    void ApplyOperation(const ModelDrawState& state, ModelStateOperation operation);

    // Authority of the deferred profile: even SetState with an equal value is a foreign intervention for the binder.
    // The serial neither observes nor restores native GL caches.
    [[nodiscard]] const ModelDrawState& State() const
    {
        return m_state;
    }
    [[nodiscard]] std::uint64_t StateSerial() const
    {
        return m_stateSerial;
    }

    // Call BEFORE Release/loads that mutate GL outside the batch. Does not modify GL or assign a name 0. The owner must
    // submit/flush pending work first and not use the backend between effects.
    [[nodiscard]] Core::Expected<void, std::error_code> InvalidateTextureKnowledge()
    {
        if (!m_commands.empty() || m_primitiveOpen)
            return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
        ++m_stateSerial;
        m_state.texture.reset();
        m_state.rawTextureName.reset();
        return {};
    }

    // Canonical mark of the backend's single non-nested quad. Begin(pass) does not fabricate its close; a foreign
    // intervention during the quad is detected by serial and keeps the prefix open.
    [[nodiscard]] Core::Expected<void, std::error_code> BeginPrimitive()
    {
        if (m_primitiveOpen)
            return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
        m_primitiveOpen = true;
        return {};
    }
    void EndPrimitive()
    {
        m_primitiveOpen = false;
    }
    [[nodiscard]] bool HasOpenPrimitive() const
    {
        return m_primitiveOpen;
    }

    // Goes into the last range if it has the current state, or into a new one.
    void Triangle(const std::array<ModelVertex, 3>& corners);

    // glEnable(GL_FOG) with 0x42b0a0's fog, or glDisable(GL_FOG) with std::nullopt (0x42b0f0): applies to the following
    // ranges, whether or not their state changes, until another SetFog or a Begin that initializes the material. Not
    // part of the material: it changes neither State() nor the serial the particle backend watches.
    void SetFog(const std::optional<ModelFog>& fog);
    [[nodiscard]] const std::optional<ModelFog>& Fog() const;

    // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, ...) for the following ranges, until another SetTextureEnv. Not
    // part of the material either, and Begin does not touch it: in the original nothing else changes it.
    void SetTextureEnv(TextureEnvMode mode);
    [[nodiscard]] TextureEnvMode TextureEnv() const;

    [[nodiscard]] const Pass3D& Pass() const;
    [[nodiscard]] std::span<const ModelVertex> Vertices() const;
    [[nodiscard]] std::span<const ModelDrawCommand> Commands() const;

private:
    // Whether the last range is a draw with the current state, fog and texture env mode.
    [[nodiscard]] bool ContinuesTheLastDraw() const;
    // An empty draw range with everything current, starting at the next vertex.
    [[nodiscard]] ModelDrawCommand NewDraw() const;

    Pass3D m_pass;
    ModelDrawState m_state;
    std::optional<ModelFog> m_fog;
    TextureEnvMode m_textureEnv = TextureEnvMode::Modulate;
    std::uint64_t m_stateSerial = 0;
    bool m_primitiveOpen = false;
    bool m_initializeMaterial = false;
    std::vector<ModelVertex> m_vertices;
    std::vector<ModelDrawCommand> m_commands;
};
} // namespace Client::Render
