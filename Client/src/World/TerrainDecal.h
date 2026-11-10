#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

// Un calco en el suelo (0x5fb2f0): una textura girada y apoyada sobre las celdas del terreno alrededor de un
// punto, cada celda un fan de cuatro vértices con las alturas del .OZB (0x5faec0). Lo llaman las marcas del
// suelo (0x549130) y otros catorce lugares sin portar, entre ellos los efectos del suelo (0x52dc20). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Las marcas del suelo".
namespace Client::World
{
// Lo que recibe 0x5fb2f0, en el orden de sus argumentos.
struct TerrainDecal
{
    // El slot de la textura, que liga 0x620780.
    std::int32_t slot = 0;
    // El centro, en el mundo.
    float x = 0.0f;
    float y = 0.0f;
    // Los lados, en celdas: el mayor fija cuántas celdas cubre y el cociente estira la u.
    float width = 0.0f;
    float height = 0.0f;
    // El color: glColor3fv con el alfa en 1 y glColor4f con otro, que dan lo mismo.
    glm::vec3 color{};
    // El giro alrededor de z, en grados (0x5fc8f0 con (0, 0, giro)).
    float angle = 0.0f;
    float alpha = 0.0f;
    // Lo que sube cada vértice sobre la altura de su esquina.
    float lift = 0.0f;
};

// 0x5fb2f0, con la mezcla y la profundidad que dejó quien lo llama: liga el slot y, con n = ftol(lado mayor)
// + 1, dibuja las (2n + 1)² celdas de (cx − n, cy − n) a (cx + n, cy + n) alrededor de la celda del centro,
// cada una con las UV de su lugar en el calco giradas alrededor de (0,5; 0,5). Las celdas con x o y fuera de
// 0..254 no se dibujan (0x5faec0). Las cuentas van como en la x87: en 53 bits, guardadas en float donde el
// binario las guarda. Corta con error con un slot fuera de la tabla o con un ftol que no da un entero de 64
// bits.
[[nodiscard]] Core::Expected<void, std::string>
DrawTerrainDecal(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights, const TerrainDecal& decal);
} // namespace Client::World
