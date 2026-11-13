#pragma once

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}

// CL9 hito 4: las dos lecturas que comparten AttackStep.cpp, AttackFrameSkills.cpp y AttackSkillCases.cpp en el paso
// del ataque (06, "El paso del ataque"). Interno de esos tres: MonsterAttackStep.cpp tiene las suyas.
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// 0x517630 y los accesos en línea a +0x38e: una entrada de un byte, un rand() al cerrarla en 0.
inline constexpr std::uint16_t SkillCloseRandoms = 1;

// 0x517630 (thiscall sobre 0x57630c8) y sus copias en línea: abre +0x38e, lo lee y lo cierra.
[[nodiscard]] std::uint8_t ReadTheSkill(ActorState& actor, Render::Models::CrtRandom& random);

// 0x59fb80(skill, …): abre el registro propio ([0x7681eb4], 0x40ad90), busca `skill` entre los 64 bytes de +0x6c de
// [0x7681eb0] y lo cierra (0x40b4d0: 3.848 rand() si queda en 0). Devuelve su lugar, o 0 si no está. Los otros
// argumentos que le empuja el caller quedan en la pila para 0x53e2b0.
[[nodiscard]] std::int32_t OwnSkillIndex(const CharacterStepContext& context, std::uint8_t skill);
} // namespace Client::Scenes
