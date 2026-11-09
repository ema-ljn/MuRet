#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/ILeafWaveEvaluator.h"

#include <cstdint>
#include <optional>
#include <system_error>

namespace Client::World
{
// Adaptador del perfil matemático portable de ADR0014, con perfil masked-invalid:
// una conversión inválida entrega 0, los low32 del resultado inválido enmascarado.
// No acredita equivalencia universal con FSIN, máscaras x87 desconocidas ni el posible
// trap no enmascarado; tampoco modifica el entorno FE/x87.
// Idle no retiene valor; Evaluate exitoso pasa a Pending. Otro Evaluate descarta Pending
// y falla con invalid_argument. Truncate consume Pending; en Idle falla con ese mismo error.
class PortableLeafWaveEvaluator final : public ILeafWaveEvaluator
{
public:
    [[nodiscard]] Core::Expected<void, std::error_code> Evaluate(LeafWave wave, float time) override;
    [[nodiscard]] Core::Expected<std::int32_t, std::error_code> Truncate() override;

private:
    std::optional<Render::Models::Detail::Retained64> m_pending;
};
} // namespace Client::World
