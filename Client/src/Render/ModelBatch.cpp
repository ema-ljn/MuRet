#include "Render/ModelBatch.h"

#include "Render/ModelFog.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureEnvMode.h"

#include <array>
#include <optional>
#include <span>

namespace Client::Render
{
void ModelBatch::Begin(const Pass3D& pass, bool initializeMaterial)
{
    ++m_stateSerial;
    m_pass = pass;
    m_initializeMaterial = initializeMaterial;
    m_state = ModelDrawState{};
    if (initializeMaterial)
        m_fog.reset();
    m_vertices.clear();
    m_commands.clear();
}

void ModelBatch::SetState(const ModelDrawState& state)
{
    ++m_stateSerial;
    m_state = state;
}

void ModelBatch::ApplyOperation(const ModelDrawState& state, ModelStateOperation operation)
{
    ++m_stateSerial;
    m_state = state;
    ModelDrawCommand command = NewDraw();
    command.operation = operation;
    m_commands.push_back(command);
}

void ModelBatch::Triangle(const std::array<ModelVertex, 3>& corners)
{
    if (!ContinuesTheLastDraw())
        m_commands.push_back(NewDraw());
    m_vertices.insert(m_vertices.end(), corners.begin(), corners.end());
    m_commands.back().vertexCount += corners.size();
}

void ModelBatch::SetFog(const std::optional<ModelFog>& fog)
{
    m_fog = fog;
}

const std::optional<ModelFog>& ModelBatch::Fog() const
{
    return m_fog;
}

void ModelBatch::SetTextureEnv(TextureEnvMode mode)
{
    m_textureEnv = mode;
}

TextureEnvMode ModelBatch::TextureEnv() const
{
    return m_textureEnv;
}

bool ModelBatch::ContinuesTheLastDraw() const
{
    if (m_commands.empty())
        return false;
    const ModelDrawCommand& last = m_commands.back();
    return last.operation == ModelStateOperation::Draw && last.state == m_state && last.fog == m_fog &&
           last.textureEnv == m_textureEnv;
}

ModelDrawCommand ModelBatch::NewDraw() const
{
    return ModelDrawCommand{.state = m_state,
                            .firstVertex = m_vertices.size(),
                            .vertexCount = 0,
                            .operation = ModelStateOperation::Draw,
                            .fog = m_fog,
                            .textureEnv = m_textureEnv};
}

const Pass3D& ModelBatch::Pass() const
{
    return m_pass;
}

std::span<const ModelVertex> ModelBatch::Vertices() const
{
    return m_vertices;
}

std::span<const ModelDrawCommand> ModelBatch::Commands() const
{
    return m_commands;
}
} // namespace Client::Render
