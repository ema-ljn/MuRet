#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>

#include "World/IBirdStepServices.h"

namespace Client::Render::Models
{
class CrtRandom;
}

// Partes puras acreditadas de446120/446b60; el estado canónico y el RNG pertenecen a Core.
namespace Client::World
{
struct BirdStepResult
{
    std::size_t visited;
    std::size_t finalized;
    std::error_code error;
};

// 446120: requiere exactamente las vistas de los40 slots FÍSICOS del pool canónico compartido:
// birds[i].data() == baseCanónica + i*BirdRecordSize, orden/base estables toda la llamada, sin
// duplicados, solapamientos, desplazamientos ni sustituciones. El span de vistas tampoco se muta.
// Main garantiza esta asociación antes de invocar el step y usa la misma base/count40 en servicios
// (445ec0→4e9a40 excluye por índice). Precondición de binding; no otro pool ni almacén de identidad.
// Conserva prefijos ante error. Perfil aprobado PC53/RN: double R53 por operación y storesF32,
// sin contracción/fast-math y con RN establecido por el binding; no guarda/sondeo FP adelantado.
// SinSpill/Wave/AudioDistance/ConvertTileCoordinate siguen requiriendo backend numérico explícito.
[[nodiscard]] BirdStepResult StepBirds(std::span<BirdRecord> birds, IBirdStepServices& services);

// Prefijo para mapa estable. No consume el draw terminal del índice2 de Kalima: el step completo
// debe hacerlo aparte antes de salir, además de los gates de los dos slots procesables.
[[nodiscard]] std::size_t BirdStepCount(std::int32_t map);

// Vistas a los campos canónicos del actor, sin otro pool ni copia del estado de animación.
struct BirdStepTail
{
    std::uint8_t& alive;
    std::int32_t& state;
    std::int32_t& timer;
    glm::vec3& angles;
};

// Tail ordinario4468df..44696a, DESPUÉS de445ec0 y ANTES del audio/distancia/4e9980.
// Relee map y tileAttributes solo con timer<=0; el giro exige mapa7 y word exacto1.
// Siempre baja por state>=2 y decrementa el timer, incluso tras baja. No incluye otros handlers.
void FinishOrdinaryBirdMotion(const BirdStepTail& bird, const std::int32_t& map, const std::uint16_t& tileAttributes);

// Color de los casos AE/BC después del render. BC debe invocarlo después de su primer transform y
// reutilizar el resultado para ambos sprites, sin otro draw ni reinicialización entre ellos.
// Otros tipos: nullopt sin consumir RNG; no representa el resto del drawing ni sus efectos.
[[nodiscard]] std::optional<glm::vec3> BirdGlowLight(std::int16_t type, Render::Models::CrtRandom& random);
} // namespace Client::World
