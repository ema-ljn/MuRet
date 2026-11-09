#pragma once

#include "Core/Expected.h"

#include <cstdint>

namespace Client::World
{
enum class ParticleError : std::uint8_t;

// Producto PC53 de la salida trigonométrica retenida64 por el factor f32 exacto.
// El argumento ya viene preparado en PC53; no se redondea el seno/coseno a double antes del producto.
// Evaluador obligatorio, sin acceso/mutación/reentrada al mundo, reloj o RNG, ni referencias retenidas.
// Su implementación declara su dominio y devuelve error cuando no puede representarlo.
class IParticleTrigonometry
{
public:
    virtual ~IParticleTrigonometry();

    [[nodiscard]] virtual Core::Expected<double, ParticleError> SineProductPC53(double argument,
                                                                                float multiplier) const = 0;
    [[nodiscard]] virtual Core::Expected<double, ParticleError> CosineProductPC53(double argument,
                                                                                  float multiplier) const = 0;
};
} // namespace Client::World
