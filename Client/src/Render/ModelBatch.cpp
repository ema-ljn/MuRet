#include "Render/ModelBatch.h"

#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"

#include <array>
#include <span>

namespace Client::Render
{
void ModelBatch::Begin(const Pass3D& pass)
{
    m_pass = pass;
    m_state = ModelDrawState{};
    m_vertices.clear();
    m_commands.clear();
}

void ModelBatch::SetState(const ModelDrawState& state)
{
    m_state = state;
}

void ModelBatch::Triangle(const std::array<ModelVertex, 3>& corners)
{
    if (m_commands.empty() || m_commands.back().state != m_state)
    {
        m_commands.push_back(ModelDrawCommand{m_state, m_vertices.size(), 0});
    }
    m_vertices.insert(m_vertices.end(), corners.begin(), corners.end());
    m_commands.back().vertexCount += corners.size();
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
