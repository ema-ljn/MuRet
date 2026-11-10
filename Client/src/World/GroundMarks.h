#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Las marcas del suelo: la tabla de 100 lugares de 0x78 bytes en 0x760cab0..0x760f990, en la .bss, que llena
// 0x548fe0, envejece 0x549090 en cada paso del juego y dibuja 0x549130 con el terreno (0x5fc365). La carga del
// mapa no la vacía (Q-C228). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las marcas del suelo".
namespace Client::World
{
// Un lugar de la tabla. De sus 0x78 bytes, estas funciones tocan solo estos campos.
struct GroundMark
{
    // +0: prendida.
    bool live = false;
    // +4: el tipo, que es también el slot de su textura: los dos creadores ponen 0x5e3, Effect\blood01.tga.
    std::int32_t type = 0;
    // +0x10: el lado del calco, en celdas.
    float size = 0.0f;
    // +0x14..+0x1c: dónde, en el mundo. La z se copia y no la lee nadie.
    glm::vec3 position{};
    // +0x28: el giro, en grados; el dibujo lo pasa cambiado de signo.
    float angle = 0.0f;
    // +0x2c..+0x34: el color.
    glm::vec3 color{};
    // +0x38: el alfa.
    float alpha = 0.0f;
    // +0x3c: la vida, en pasos del juego.
    std::int32_t life = 0;

    friend bool operator==(const GroundMark&, const GroundMark&) = default;
};

// 0x548fe0..0x54900a: los 100 lugares, (0x760f990 − 0x760cab0) / 0x78.
inline constexpr std::size_t GroundMarkCount = 100;
using GroundMarkTable = std::array<GroundMark, GroundMarkCount>;

// Los argumentos de 0x548fe0, en su orden.
struct GroundMarkRequest
{
    std::int32_t type = 0;
    glm::vec3 position{};
    float angle = 0.0f;
    glm::vec3 color{};
    float size = 0.0f;
};

// 0x548fe0: en el primer lugar apagado, la marca prendida, con el alfa en 1 y, con el tipo 0x5e3, 0x5e4 o
// 0x606, la vida en rand() % 32 + 50 (0x54906e); con otro tipo la vida queda como estaba. Sin lugar apagado
// apaga el de menos vida (el primero, si empatan) y no pone la nueva, sin rand() (Q-C231). Corta con error,
// sin tocar la tabla, si además ninguna vida baja de 9.999: el original escribe un 0 en la dirección del tipo.
[[nodiscard]] Core::Expected<void, std::string> AddGroundMark(GroundMarkTable& marks, const GroundMarkRequest& request,
                                                              Render::Models::CrtRandom& random);

// 0x549090, desde el paso del juego (0x63712d): a cada marca prendida le baja la vida. El 0x5e3 crece 0,004
// de lado; el 0x5e3, el 0x5e4 y el 0x606 toman el color (0,1; 0; 0), se apagan sin vida y, con menos de 50,
// van con el alfa en vida · 0,02. El 7 achica 0,05 el lado y se apaga debajo de 0,1. Sin rand().
void StepGroundMarks(GroundMarkTable& marks);

// Los componentes de la textura del slot (+0x28 del slot de 0x7d3a198): sin valor, un slot que MuRet no subió.
using SlotComponents = std::function<std::optional<std::uint8_t>(std::int32_t slot)>;

// 0x549130, con la profundidad que dejó 0x5fc260 (0x5fc35b): cada marca prendida, en el orden de la tabla,
// con la mezcla aditiva si la textura de su tipo tiene 3 componentes y la normal si no, y el calco de
// 0x5fb2f0 con su lado, su color, el giro cambiado de signo, su alfa y 5 sobre el terreno. Corta con error
// en la primera marca cuyo slot MuRet no conoce, o si el calco corta.
[[nodiscard]] Core::Expected<void, std::string> DrawGroundMarks(Render::ModelBatch& batch, const GroundMarkTable& marks,
                                                                const Data::Terrain::TerrainHeights& heights,
                                                                const SlotComponents& components);
} // namespace Client::World
