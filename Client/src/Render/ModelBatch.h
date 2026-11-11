#pragma once

#include "Core/Expected.h"
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

// El 3D de un pase como datos, sin GL: el par del SpriteBatch (CL5.2). Los triángulos de los modelos
// en el mundo y los tramos que Gl::GlModelRenderer dibuja cada uno con un glDrawArrays, en el orden
// en que llegan: nunca se reordena, como el original, que los manda en el orden en que dibuja.
namespace Client::Render
{
// Las mezclas de 0x4ecc30 (06 § El modelo en el binario). Ninguna toca glDepthFunc.
enum class ModelBlend : std::uint8_t
{
    // 0x620980: sin mezcla, con culling, escribe la profundidad y sin alpha test.
    Opaque,
    // 0x620a00(1): SRC_ALPHA / ONE_MINUS_SRC_ALPHA, sin culling, con alpha test y escribe la
    // profundidad.
    Alpha,
    // 0x620a90: ONE / ONE, sin culling ni alpha test, y no escribe la profundidad.
    Additive,
    // La sombra: 0x620980 con la máscara que apaga 0x4eff40, sin mezcla, con culling, sin alpha test y
    // sin escribir la profundidad.
    Shadow,
    // La sombra de un objeto del mundo (el caballo, 0x6002e0): 0x620a00(1) y lo que apaga 0x4eff40 (la
    // máscara, la textura y el alpha test), SRC_ALPHA / ONE_MINUS_SRC_ALPHA, sin culling, sin alpha test
    // y sin escribir la profundidad.
    BlendedShadow,
    // El remolino 90 del mapa 55 (0x6002e0 con 0x620b10): ZERO / ONE_MINUS_SRC_COLOR, sin culling ni
    // alpha test, y no escribe la profundidad: oscurece lo de atrás donde su color es claro.
    Subtractive,
    // 0x620b90: ONE_MINUS_SRC_COLOR / ONE, sin culling, alpha test ni escritura de profundidad.
    InverseColorAdditive,
    // 620c90: ZERO / SRC_COLOR, sin culling, alpha test ni escritura de profundidad.
    Multiplicative,
};

// 621400 ya entrega coordenadas de ojo; las mallas siguen entregando coordenadas del mundo.
enum class ModelCoordinateSpace : std::uint8_t
{
    World,
    Eye,
};

// Efectos parciales acreditados: no ejecutan los otros ejes ni dibujan vértices.
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
};

struct ModelDrawState
{
    // La textura, o ninguna.
    std::optional<TextureSlot> texture;
    ModelBlend blend = ModelBlend::Opaque;
    // false con la bandera 0x2000 de 0x4ecc30 (0x620870 apaga el depth test).
    bool depthTest = true;
    ModelCoordinateSpace space = ModelCoordinateSpace::World;
    // Nombre físico de620780(NEG32), distinto del selector lógico. Incluye el nombre0 explícito.
    // Invariante: texture y rawTextureName son mutuamente excluyentes. El renderer rechaza
    // explícitamente un comando ambiguo, sin elegir una prioridad entre ambos.
    std::optional<std::uint32_t> rawTextureName = std::nullopt;

    // Ausencia conserva el perfil legacy de ModelBlend; presencia desacopla cada eje.
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
    std::size_t vertexCount = 0; // de a 3
    ModelStateOperation operation = ModelStateOperation::Draw;
    // GL_FOG prendido con su niebla, o apagado. No es del material: va aparte de `state` (ModelBatch::SetFog).
    std::optional<ModelFog> fog = std::nullopt;
    // GL_TEXTURE_ENV_MODE, que tampoco es del material (ModelBatch::SetTextureEnv).
    TextureEnvMode textureEnv = TextureEnvMode::Modulate;
};

class ModelBatch
{
public:
    // Empieza un pase: vacía triángulos/tramos y vuelve a ModelDrawState{}. Sólo620db0
    // inicializa material; un flush de continuación o6213e0 conserva los ejes GL vigentes.
    // 0x620db0, con 0x7d368a0 en 0 (06 § Cámara), también apaga la niebla (0x620fa5): con
    // initializeMaterial la deja en std::nullopt; sin él, la conserva. El modo de la textura no lo toca nadie.
    void Begin(const Pass3D& pass, bool initializeMaterial = true);
    [[nodiscard]] bool InitializesMaterial() const
    {
        return m_initializeMaterial;
    }

    // El estado de los triángulos que siguen.
    void SetState(const ModelDrawState& state);

    // Estado observable aun sin geometría: material/binder de fauna antes de Begin o de un error.
    // Un tramo de cero vértices cambia el estado en GlModelRenderer sin publicar un quad parcial.
    // Triangle puede continuar este mismo tramo cuando su estado coincide.
    void ApplyState(const ModelDrawState& state)
    {
        ++m_stateSerial;
        m_state = state;
        if (!ContinuesTheLastDraw())
            m_commands.push_back(NewDraw());
    }

    // Publica un único efecto, incluso sin geometría. El serial se escribe antes del sink GL.
    void ApplyOperation(const ModelDrawState& state, ModelStateOperation operation);

    // Autoridad del perfil diferido: incluso SetState con igual valor es una intervención ajena
    // al binder. El serial no es una observación ni una restauración de caches GL nativos.
    [[nodiscard]] const ModelDrawState& State() const
    {
        return m_state;
    }
    [[nodiscard]] std::uint64_t StateSerial() const
    {
        return m_stateSerial;
    }

    // ANTES de Release/cargas que muten GL fuera del lote. No modifica GL ni asigna un nombre0.
    // El owner debe enviar/vaciar trabajo pendiente primero y no usar el backend entre efectos.
    [[nodiscard]] Core::Expected<void, std::error_code> InvalidateTextureKnowledge()
    {
        if (!m_commands.empty() || m_primitiveOpen)
            return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
        ++m_stateSerial;
        m_state.texture.reset();
        m_state.rawTextureName.reset();
        return {};
    }

    // Marca canónica del único quad sin anidar del backend. Begin(pass) no fabrica su cierre;
    // una intervención ajena durante el quad se detecta por serial y conserva el prefijo abierto.
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

    // Un triángulo con el estado de ahora: va en el último tramo si tiene el mismo, o en uno nuevo.
    void Triangle(const std::array<ModelVertex, 3>& corners);

    // glEnable(GL_FOG) con la niebla de 0x42b0a0, o glDisable(GL_FOG) con std::nullopt (0x42b0f0): vale para
    // los tramos que siguen, cambie o no su estado, hasta otro SetFog o un Begin que inicializa el material. No
    // es del material: no cambia State() ni el serial que mira el backend de las partículas.
    void SetFog(const std::optional<ModelFog>& fog);
    [[nodiscard]] const std::optional<ModelFog>& Fog() const;

    // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, ...): vale para los tramos que siguen, hasta otro
    // SetTextureEnv. Tampoco es del material, y Begin no lo toca: en el original nadie más lo cambia.
    void SetTextureEnv(TextureEnvMode mode);
    [[nodiscard]] TextureEnvMode TextureEnv() const;

    [[nodiscard]] const Pass3D& Pass() const;
    [[nodiscard]] std::span<const ModelVertex> Vertices() const;
    [[nodiscard]] std::span<const ModelDrawCommand> Commands() const;

private:
    // Si el último tramo es un dibujo con el estado, la niebla y el modo de la textura de ahora.
    [[nodiscard]] bool ContinuesTheLastDraw() const;
    // Un tramo de dibujo vacío con todo lo de ahora, desde el próximo vértice.
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
