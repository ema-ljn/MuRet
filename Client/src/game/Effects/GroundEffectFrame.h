#pragma once

#include "Core/Expected.h"
#include "Effects/ModelEffects.h"
#include "Kernel/KalimaWater.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Effects
{
struct GroundEffectResources;
}

// D-C4: 0x52dc20 consumes RNG and changes effects only on the original's frames. The in-between drawings replay their
// input, including the prefix before an error. Contract: 06 § El dibujo del juego and Los efectos del login.
namespace Client::Effects
{
class GroundEffectFrame
{
public:
    [[nodiscard]] Core::Expected<void, std::string> Draw(Render::ModelBatch& batch,
                                                         std::span<Effects::ModelEffect> effects,
                                                         const Effects::GroundEffectResources& resources,
                                                         bool originalFrame);

    // On entering the scene or replacing its world; keeps the buffers' capacity.
    void Reset();

private:
    void Capture(std::span<Effects::ModelEffect> effects, const Effects::GroundEffectResources& resources);
    std::unique_ptr<Kernel::KalimaWater> m_kalimaSnapshot;
    std::vector<Effects::ModelEffect> m_originalEffects;
    std::vector<Effects::ModelEffect> m_replayEffects;
    std::uint32_t m_randomState = 1;
    float m_worldClock = 0.0f;
    std::optional<std::uint8_t> m_map;
};
} // namespace Client::Effects
