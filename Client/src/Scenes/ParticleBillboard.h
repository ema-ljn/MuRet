#pragma once

#include "Core/Expected.h"
#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"

#include <cstdint>
#include <optional>

// Proyección geométrica de 621400 para partículas, sin binder ni estado GL. El caller proporciona
// la bandera de color por imagen (lectura tardía nativa), distinta del material por tipo de 548550.
namespace Client::Scenes
{
[[nodiscard]] Core::Expected<Render::Billboard, World::ParticleError>
MakeParticleBillboard(const Render::Models::OriginalMatrix& view, const World::ParticleDraw& draw,
                      std::optional<std::uint8_t> colorFlag);
} // namespace Client::Scenes
