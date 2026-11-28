#pragma once

#include "Core/Expected.h"
#include "Effects/ModelEffects.h"
#include "MapScripts/Kalima/KalimaWorld.h"

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
namespace Client::Terrain
{
struct GroundEffectResources;
}

// D-C4: 0x52dc20 consume RNG y cambia efectos solo en los frames del original. Los dibujos
// intermedios repiten su entrada, incluido el prefijo anterior a un error. Contrato: 06 § El dibujo
// del juego y Los efectos del login.
namespace Client::Scenes
{
class GroundEffectFrame
{
public:
    [[nodiscard]] Core::Expected<void, std::string> Draw(Render::ModelBatch& batch,
                                                         std::span<World::ModelEffect> effects,
                                                         const Terrain::GroundEffectResources& resources,
                                                         bool originalFrame);

    // Al entrar a la escena o reemplazar su mundo; conserva la capacidad de los buffers.
    void Reset();

private:
    void Capture(std::span<World::ModelEffect> effects, const Terrain::GroundEffectResources& resources);
    std::unique_ptr<World::KalimaWorld> m_kalimaSnapshot;
    std::vector<World::ModelEffect> m_originalEffects;
    std::vector<World::ModelEffect> m_replayEffects;
    std::uint32_t m_randomState = 1;
    float m_worldClock = 0.0f;
    std::optional<std::uint8_t> m_map;
};
} // namespace Client::Scenes
