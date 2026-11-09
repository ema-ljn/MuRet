#pragma once

#include <cstdint>

// 0x4e9980 ordinario: vistas prestadas de +0x170, +0x174 y +0x68; modo de +0x16d.
// Perfil PC53/RN, operandos f32 exactos y un R53 por operación, seguido de stores f32.
// El caller mantiene RN y excepciones enmascaradas. No reproduce flags/status nativos,
// traps, cambios concurrentes ni alias con constantes/frame. Se permiten alias entre vistas.
// No valida rangos ni devuelve un estado: sólo modifica current y, si corresponde, ceiling.
namespace Client::World
{
void StepObjectAlpha(const float& target, float& current, float& ceiling, std::uint8_t mode);
} // namespace Client::World
