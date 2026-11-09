#pragma once

#include "Core/Expected.h"
#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"

#include <cstdint>
#include <optional>

// Proyección geométrica de 621400 para partículas, sin binder ni estado GL. El caller proporciona
// colorFlag por imagen: el byte Recurso+28, mismo campo que materialFlag de 548550 pero indexado
// por el selector de imagen completo, no por Slot+04 (tipo), y leído después de glBegin nativo.
// 548550 capturó dimensiones de Recurso+20/+24 por Slot+08 y material por tipo antes de su servicio.
// Este adaptador recibe el valor: no ejecuta ese lookup tardío ni el binder/GL. flag3 selecciona
// glColor3fv; los demás glColor4f, sin inferir formato RGBA del loader ni recursos runtime.
namespace Client::Scenes
{
[[nodiscard]] Core::Expected<Render::Billboard, World::ParticleError>
MakeParticleBillboard(const Render::Models::OriginalMatrix& view, const World::ParticleDraw& draw,
                      std::optional<std::uint8_t> colorFlag);
} // namespace Client::Scenes
