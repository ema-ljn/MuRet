#include "Render/ModelBatchParticleState.h"

#include <optional>

namespace Client::Render
{
ModelBatchParticleState ModelBatchParticleState::ForModelPass()
{
    return {0, 1, 1, 0, 0, 0, true, {1, 1, 1, 1}, {0, 0}, std::nullopt, std::nullopt, std::nullopt};
}
} // namespace Client::Render
