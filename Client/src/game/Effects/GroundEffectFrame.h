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
namespace Client::Effects
{
struct GroundEffectResources;
}

// D-C4: 0x52dc20 consume RNG y cambia efectos solo en los frames del original. Los dibujos
// intermedios repiten su entrada, incluido el prefijo anterior a un error. Contrato: 06 § El dibujo
// del juego y Los efectos del login.
namespace Client::Effects
{
class GroundEffectFrame
{
public:
    [[nodiscard]] Core::Expected<void, std::string> Draw(Render::ModelBatch& batch,
                                                         std::span<Effects::ModelEffect> effects,
                                                         const Effects::GroundEffectResources& resources,
                                                         bool originalFrame);

    // Al entrar a la escena o reemplazar su mundo; conserva la capacidad de los buffers.
    void Reset();

private:
    void Capture(std::span<Effects::ModelEffect> effects, const Effects::GroundEffectResources& resources);
    std::unique_ptr<MapScripts::Kalima::KalimaWorld> m_kalimaSnapshot;
    std::vector<Effects::ModelEffect> m_originalEffects;
    std::vector<Effects::ModelEffect> m_replayEffects;
    std::uint32_t m_randomState = 1;
    float m_worldClock = 0.0f;
    std::optional<std::uint8_t> m_map;
};
} // namespace Client::Effects
