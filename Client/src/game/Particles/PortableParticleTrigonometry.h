#pragma once

#include "Core/Expected.h"
#include "Particles/IParticleTrigonometry.h"

namespace Client::World
{
// Perfil fijo limitado (ADR0014): sin/cos con la aritmética binaria propia de OriginalRetainedMath
// (error < 2^-250) → retenido64 RN → producto PC53 exacto en double.
// Admite argumentos finitos |a| < 2^63 y factores f32 finitos positivos.
// No acredita igualdad FSIN/FCOS. Es la que usa el gameplay para los senos y cosenos de las partículas
// (GameParticles, GameModelEffects, las llamas de Scenes/ModelEffects, ArrowEffects, GameCharacterEffects y los
// objetos de los mapas de eventos).
class PortableParticleTrigonometry final : public IParticleTrigonometry
{
public:
    [[nodiscard]] Core::Expected<double, ParticleError> SineProductPC53(double argument,
                                                                        float multiplier) const override;
    [[nodiscard]] Core::Expected<double, ParticleError> CosineProductPC53(double argument,
                                                                          float multiplier) const override;
};
} // namespace Client::World
