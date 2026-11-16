#pragma once

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// La espera de las skills (+0x76c del registro propio, por el lugar de la barra), CL9 hito 4. Contrato: 06, "La
// espera de las skills". Cada acceso al registro propio gasta sus 3.848 rand() al cerrarlo en 0, y cada uno a una
// entrada de Skill.bmd, 60 (0x40f600), en el orden del original.
namespace Client::Scenes
{
class WorldState;

struct SkillDelayContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    // Skill.bmd (0x76bd1d8). nullptr: lo que lo lee corta.
    const Data::Tables::SkillTable* skills = nullptr;
};

// 0x5521f0(skill, &requisito): el WORD +0x2e de la entrada de Skill.bmd, abierta y cerrada.
[[nodiscard]] Core::Expected<std::int32_t, std::string> SkillDelayRequirement(std::uint8_t skill,
                                                                              const SkillDelayContext& context);

// 0x552990(lugar): la skill del lugar de la barra (+0x6c + lugar) y su espera (+0x28 de Skill.bmd); con espera, si la
// del registro ya llegó a 0 y el requisito de 0x5521f0 no pasa de +0x36 + +0x20 (el liderazgo), la pone.
[[nodiscard]] Core::Expected<void, std::string> StartSkillDelay(std::uint8_t slot, const SkillDelayContext& context);

// 0x552c50(lugar): lo mismo, pero dice si se puede usar: no con la espera todavía corriendo ni con el requisito sin
// cumplir. Con la skill 0x4c no la pone.
[[nodiscard]] Core::Expected<bool, std::string> TryStartSkillDelay(std::uint8_t slot, const SkillDelayContext& context);

// 0x552f10(transcurrido): con el registro abierto, baja las esperas de los +0x6a + 2 primeros lugares (hasta 256) que
// están corriendo, sin pasar de 0. La llama 0x4eb620 en la escena 5 con los milisegundos del frame (GameScene::Step, al
// empezar cada frame del original).
[[nodiscard]] Core::Expected<void, std::string> CountDownSkillDelays(std::int32_t elapsed,
                                                                     const SkillDelayContext& context);
} // namespace Client::Scenes
