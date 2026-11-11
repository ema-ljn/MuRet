#pragma once

#include "Core/Expected.h"
#include "World/KalimaWaves.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Client::Render
{
class ModelBatch;
namespace Models
{
class CrtRandom;
}
} // namespace Client::Render

// 06 § Kalima: dueño portable de4377a0/723c48. El mapa no sustituye su existencia.
namespace Client::World
{
[[nodiscard]] bool IsKalimaMap(std::int32_t map);
struct KalimaFrameContext
{
    std::int32_t logicSteps;
    float worldTime;
    std::uint8_t heroTileX;
    std::uint8_t heroTileY;
    Render::Models::CrtRandom& random;
    // Cada callback devuelve el valor por copia, después de cerrar su acceso lógico.
    // Update los invoca en los puntos nativos; no prelee ambos ejes.
    std::function<Core::Expected<std::int32_t, std::string>()> readHeroX{};
    std::function<Core::Expected<std::int32_t, std::string>()> readHeroY{};
};
class KalimaWorld final
{
public:
    explicit KalimaWorld(std::int32_t map);
    ~KalimaWorld();
    KalimaWorld(const KalimaWorld&) = delete;
    KalimaWorld& operator=(const KalimaWorld&) = delete;
    [[nodiscard]] std::unique_ptr<KalimaWorld> CloneForFrame() const;
    // 41cca0: perturbación condicional, selector, ondas y campos periódicos, en ese orden.
    [[nodiscard]] Core::Expected<void, std::string> Update(const KalimaFrameContext& context);
    // 41cf00/41d2d0: misma cámara; publica agua Alpha23 y brillo Additive24.
    [[nodiscard]] Core::Expected<void, std::string> Draw(Render::ModelBatch& batch, std::uint8_t heroTileX,
                                                         std::uint8_t heroTileY);
    // 437860: coordenadas de tile, el radio en celdas de onda. Conserva el selector.
    [[nodiscard]] Core::Expected<void, std::string> Disturb(std::int32_t tileX, std::int32_t tileY, std::int32_t radius,
                                                            std::int32_t amplitude, Render::Models::CrtRandom& random);
    // 4378e0/41dc80: clampa negativos; fuera del plano definido no inventa un alias.
    [[nodiscard]] std::optional<float> Height(float x, float y) const;
    [[nodiscard]] const KalimaWaveState& Waves() const;
    [[nodiscard]] std::int32_t Map() const;

private:
    struct Storage;
    std::unique_ptr<Storage> m_storage;
};
} // namespace Client::World
