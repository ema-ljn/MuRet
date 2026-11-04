#pragma once

#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
};

struct ModelDrawState
{
    // La textura, o ninguna.
    std::optional<TextureSlot> texture;
    ModelBlend blend = ModelBlend::Opaque;
    // false con la bandera 0x2000 de 0x4ecc30 (0x620870 apaga el depth test).
    bool depthTest = true;

    friend bool operator==(const ModelDrawState&, const ModelDrawState&) = default;
};

struct ModelDrawCommand
{
    ModelDrawState state;
    std::size_t firstVertex = 0;
    std::size_t vertexCount = 0; // de a 3
};

class ModelBatch
{
public:
    // Empieza un pase: vacía los triángulos y los tramos, y el estado vuelve al de ModelDrawState{}.
    void Begin(const Pass3D& pass);

    // El estado de los triángulos que siguen.
    void SetState(const ModelDrawState& state);

    // Un triángulo con el estado de ahora: va en el último tramo si tiene el mismo, o en uno nuevo.
    void Triangle(const std::array<ModelVertex, 3>& corners);

    [[nodiscard]] const Pass3D& Pass() const;
    [[nodiscard]] std::span<const ModelVertex> Vertices() const;
    [[nodiscard]] std::span<const ModelDrawCommand> Commands() const;

private:
    Pass3D m_pass;
    ModelDrawState m_state;
    std::vector<ModelVertex> m_vertices;
    std::vector<ModelDrawCommand> m_commands;
};
} // namespace Client::Render
