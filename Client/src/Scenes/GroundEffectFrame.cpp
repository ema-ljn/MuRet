#include "Scenes/GroundEffectFrame.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/GroundEffectDrawing.h"
#include "World/ModelEffects.h"

#include <algorithm>
#include <span>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> GroundEffectFrame::Draw(Render::ModelBatch& batch,
                                                          std::span<World::ModelEffect> effects,
                                                          const World::GroundEffectResources& resources,
                                                          bool originalFrame)
{
    if (originalFrame)
    {
        // La entrada, antes del primer rand() o cambio del dibujo, incluso si después corta.
        m_originalEffects.resize(effects.size());
        std::copy(effects.begin(), effects.end(), m_originalEffects.begin());
        m_replayEffects.resize(effects.size());
        m_randomState = resources.random.State();
        m_worldClock = resources.worldClock;
        m_map = resources.map;
        return DrawGroundEffects(batch, effects, resources);
    }
    if (!m_map)
    {
        return Core::Unexpected(std::string("0x52dc20: falta el frame original de los efectos del suelo"));
    }
    if (*m_map != resources.map)
    {
        return Core::Unexpected(
            std::string("0x52dc20: el mapa cambió desde el frame original de los efectos del suelo"));
    }

    // Los dos buffers retienen capacidad. Cada repetición empieza con la misma entrada, nunca
    // con las mutaciones del dibujo anterior ni con el RNG o el reloj actuales del caller.
    std::copy(m_originalEffects.begin(), m_originalEffects.end(), m_replayEffects.begin());
    Render::Models::CrtRandom random(m_randomState);
    const World::GroundEffectResources replay{
        .heights = resources.heights, .random = random, .worldClock = m_worldClock, .map = *m_map};
    return DrawGroundEffects(batch, m_replayEffects, replay);
}

void GroundEffectFrame::Reset()
{
    m_map.reset();
    m_originalEffects.clear();
    m_replayEffects.clear();
}
} // namespace Client::Scenes
