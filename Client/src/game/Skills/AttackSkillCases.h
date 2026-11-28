#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// CL9 hito 4, fase 6b: los casos de habilidad del paso del ataque (0x4fd5f0) que el hito 3 dejó cortados (08, P-C258):
// el switch de la cabeza (0x4fdeeb), el primero con blanco (0x504372) y las acciones 0x38 a 0x3c (0x5042ce, 0x52e740).
// 06, "El paso del ataque". Cada caso corre hasta donde crea un efecto con modelo, un joint o una partícula que MuRet
// no tiene (la CL11), con los rand() de antes; los que crean lo que World ya sabe crear (el terremoto 0xfa, el calco
// 0x61e/1, la flecha y los efectos de los magos de World/WizardModelEffects.h) siguen hasta el final.
namespace Client::Scenes
{

// La luz local de 0x4fd5f0 ([EBP − 0x30]): (1, 1, 1) en 0x4fd81e, sin condición; la partícula 0x5ac de las acciones
// 0x4d, 0x4f y 0x5a pasado el frame 5,5 la deja en (0,3f; 0,5; 1) (0x4fdb3f, Scenes::CreateTheActionFlare). La leen
// los casos 0x08 de la cabeza (0x500c32) y 0x07 con blanco (0x50474e).
inline constexpr glm::vec3 BodyLight{1.0f, 1.0f, 1.0f};

// 0x4fdc17..0x4fdeeb y sus casos, con `skill` la lectura de +0x38e de la cabeza: 5, 8, 9, 0xa, 0xc..0xe, 0x1e..0x24,
// 0x26..0x2a, 0x2c, 0x30, 0x31, 0x35, 0x37, 0x38, 0x3f..0x41, 0x43..0x48, 0x4b y 0x4c (la tabla de bytes de 0x505530 y
// la de saltos de 0x5054b4). Los demás, como el 0, no hacen nada (0x502aff). `actor` es c y o; `slot`, su lugar en el
// pool y `light`, la luz local de 0x4fd5f0. Todos los casos siguen en 0x502aff.
[[nodiscard]] Core::Expected<void, std::string> StepTheHeadSkill(Kernel::ActorState& actor, std::size_t slot,
                                                                 std::uint8_t skill,
                                                                 const Actors::CharacterStepContext& context,
                                                                 const glm::vec3& light = BodyLight);

// 0x504372 y sus casos, con `skill` la lectura de +0x38e de 0x50432a y `victim` el blanco de +0x3a4: 1, 2, 4, 7, 0xb,
// 0x10, 0x11, 0x18, 0x1a..0x1c, 0x2d, 0x2e, 0x33, 0x34, 0x3c, 0x3d, 0x49 y 0x4a (la tabla de bytes de 0x5055c8 y la de
// saltos de 0x505578). El 3 y los demás no hacen nada. Todos siguen en 0x505069. Los ángulos hacia el blanco de
// 0x5042d3..0x504324 (los de o con el rumbo de 0x4e93f0 de o al blanco, sin rand()) los arma cada caso que los lee.
[[nodiscard]] Core::Expected<void, std::string>
StepTheTargetSkill(Kernel::ActorState& actor, Kernel::ActorState& victim, std::size_t slot, std::uint8_t skill,
                   const Actors::CharacterStepContext& context, const glm::vec3& light = BodyLight);

// 0x5042ce: 0x52e740(0, blanco, blanco + 0x10, o + 0x1c), con las acciones 0x38 a 0x3c de o.
[[nodiscard]] Core::Expected<void, std::string> SparkTheTarget(const Kernel::ActorState& actor,
                                                               const Kernel::ActorState& victim, std::size_t slot,
                                                               const Actors::CharacterStepContext& context);
} // namespace Client::Scenes
