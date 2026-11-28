#include "Effects/GroundEffectFrame.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Render/ModelBatch.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Effects/GroundEffectDrawing.h"
#include "Effects/ModelEffects.h"
#include "MapScripts/Kalima/KalimaWorld.h"

#include <algorithm>
#include <span>
#include <string>

namespace Client::Effects
{
Core::Expected<void, std::string> GroundEffectFrame::Draw(Render::ModelBatch& batch,
                                                          std::span<Effects::ModelEffect> effects,
                                                          const Effects::GroundEffectResources& resources,
                                                          bool originalFrame)
{
    // El primer frame puede tener0 pasos. Conserva su entrada y proyecta sobre copias privadas.
    if (originalFrame || !m_map)
        Capture(effects, resources);
    if (originalFrame)
        return WorldObjects::DrawGroundEffects(batch, effects, resources);
    if (*m_map != resources.map)
    {
        return Core::Unexpected(
            std::string("0x52dc20: el mapa cambió desde el frame original de los efectos del suelo"));
    }

    // Los dos buffers retienen capacidad. Cada repetición empieza con la misma entrada, nunca
    // con las mutaciones del dibujo anterior ni con el RNG o el reloj actuales del caller.
    std::copy(m_originalEffects.begin(), m_originalEffects.end(), m_replayEffects.begin());
    Crt::CrtRandom random(m_randomState);
    const Effects::GroundEffectResources replay{.heights = resources.heights,
                                                .random = random,
                                                .worldClock = m_worldClock,
                                                .map = *m_map,
                                                .kalima = m_kalimaSnapshot.get(),
                                                .ownerPosition = resources.ownerPosition};
    return WorldObjects::DrawGroundEffects(batch, m_replayEffects, replay);
}

void GroundEffectFrame::Capture(std::span<Effects::ModelEffect> effects,
                                const Effects::GroundEffectResources& resources)
{
    m_originalEffects.resize(effects.size());
    std::copy(effects.begin(), effects.end(), m_originalEffects.begin());
    m_replayEffects.resize(effects.size());
    m_randomState = resources.random.State();
    m_worldClock = resources.worldClock;
    m_map = resources.map;
    m_kalimaSnapshot = resources.kalima ? resources.kalima->CloneForFrame() : nullptr;
}

void GroundEffectFrame::Reset()
{
    m_map.reset();
    m_kalimaSnapshot.reset();
    m_originalEffects.clear();
    m_replayEffects.clear();
}
} // namespace Client::Effects
